/*
 * Copyright (C) 2012 Maxim Integrated Product
 * Jayden Cha <jayden.cha@maxim-ic.com>
 * Copyright 2012-2014 Amazon Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>

#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/mfd/max77696.h>
#include <mach/boardid.h>
#include <max77696_registers.h>

#define DRIVER_DESC    "MAX77696 ADC Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_ADC_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".1"

#define ADC_NR_CHANNELS                MAX77696_ADC_NR_CHS
#define ADC_CONVERSION_TIME_OUT        10   /* in milli-seconds */

/* Display thermistor */
#define DISP_ADC_TEMP_THRESHOLD   60000
#define DISP_DEFAULT_TEMP	  25

signed int display_temp_c = DISP_DEFAULT_TEMP;
EXPORT_SYMBOL(display_temp_c);

struct max77696_adc {
	struct max77696_chip *chip;
	struct max77696_i2c  *i2c;
	struct device        *dev;
	struct kobject       *kobj;

	struct device        *hwmon;
	struct mutex          lock;
};

#define __get_i2c(chip)                (&((chip)->pmic_i2c))
#define __lock(me)                     mutex_lock(&((me)->lock))
#define __unlock(me)                   mutex_unlock(&((me)->lock))

/* ADC Register Read/Write */
#define max77696_adc_reg_read(me, reg, val_ptr) \
        max77696_read((me)->i2c, ADC_REG(reg), val_ptr)
#define max77696_adc_reg_write(me, reg, val) \
        max77696_write((me)->i2c, ADC_REG(reg), val)
#define max77696_adc_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, ADC_REG(reg), dst, len)
#define max77696_adc_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, ADC_REG(reg), src, len)
#define max77696_adc_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, ADC_REG(reg), mask, val_ptr)
#define max77696_adc_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, ADC_REG(reg), mask, val)

/* ADC Register Single Bit Ops */
#define max77696_adc_reg_get_bit(me, reg, bit, val_ptr) \
        ({\
            int __rc = max77696_adc_reg_read_masked(me, reg,\
                ADC_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = ADC_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_adc_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_adc_reg_write_masked(me, reg,\
                ADC_REG_BITMASK(reg, bit), ADC_REG_BITSET(reg, bit, val));\
        })

static void adc_disp_gettemp_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(adc_disp_temp_work, adc_disp_gettemp_work);

static int max77696_adc_channel_setup_ain_mux(struct max77696_adc *me,
	u8 channel);
static int max77696_adc_channel_release_ain_mux(struct max77696_adc *me,
	u8 channel);

static __inline int max77696_adc_adcdly_read (struct max77696_adc *me, u16 *val)
{
    u8 tmp;
    int rc;

    rc = max77696_adc_reg_get_bit(me, ADCDLY, ADCDLY, &tmp);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCDLY read error [%d]\n", rc);
        goto out;
    }

    *val = (((u16)tmp + 2) * 500);

out:
    return rc;
}

static __inline int max77696_adc_adcdly_write (struct max77696_adc *me, u16 val)
{
    u8 tmp;
    int rc;

    tmp = ((val > 1000)? ((u8)DIV_ROUND_UP(val, 500) - 2) : 0);

    rc = max77696_adc_reg_set_bit(me, ADCDLY, ADCDLY, tmp);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCDLY write error [%d]\n", rc);
        goto out;
    }

out:
    return rc;
}

static __inline int max77696_adc_adcavg_read (struct max77696_adc *me, u8 *val)
{
    u8 tmp;
    int rc;

    rc = max77696_adc_reg_get_bit(me, ADCCNTL, ADCAVG, &tmp);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCCNTL read error [%d]\n", rc);
        goto out;
    }

    switch (tmp) {
    case ADC_AVG_RATE_1_SAMPLE:
        *val = 1;
        break;

    case ADC_AVG_RATE_2_SAMPLES:
        *val = 2;
        break;

    case ADC_AVG_RATE_16_SAMPLES:
        *val = 16;
        break;

    case ADC_AVG_RATE_32_SAMPLES:
        *val = 32;
        break;
    }

out:
    return rc;
}

static __inline int max77696_adc_cnfgadc_write(struct max77696_adc *me, u8 sel)
{
    u8 tmp;
    int rc;

    if(sel >= 50) {
        tmp = 0x03;
    } else if(sel >= 10) {
	tmp = 0x02;
    } else if(sel >= 5) {
	tmp = 0x01;
    } else {
	tmp = 0x0;
    }

    rc = max77696_adc_reg_set_bit(me, ADCICNFG, IADC, tmp);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCICNFG write error [%d]\n", rc);
    }

    return rc;
}

static __inline int max77696_adc_adcavg_write (struct max77696_adc *me, u8 val)
{
    u8 tmp;
    int rc;

    if (val >= 32) {
        tmp = ADC_AVG_RATE_32_SAMPLES;
    } else if (val >= 16) {
        tmp = ADC_AVG_RATE_16_SAMPLES;
    } else if (val >= 2) {
        tmp = ADC_AVG_RATE_2_SAMPLES;
    } else {
        tmp = ADC_AVG_RATE_1_SAMPLE;
    }

    rc = max77696_adc_reg_set_bit(me, ADCCNTL, ADCAVG, tmp);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCCNTL write error [%d]\n", rc);
    }

    return rc;
}

static ssize_t max77696_adc_adcavg_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct max77696_adc *me = platform_get_drvdata(pdev);
    u8 val = 0;
    int rc;

    __lock(me);

    rc = max77696_adc_adcavg_read(me, &val);
    if (unlikely(rc)) {
        goto out;
    }

    rc  = (int)snprintf(buf, PAGE_SIZE, "%u\n", val);

out:
    __unlock(me);
    return (ssize_t)rc;
}

static ssize_t max77696_adc_adcavg_store (struct device *dev,
    struct device_attribute *devattr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
    struct max77696_adc *me = platform_get_drvdata(pdev);
    u8 val;
    int rc;

    __lock(me);

    val = (u8)simple_strtoul(buf, NULL, 10);

    rc = max77696_adc_adcavg_write(me, val);
    if (unlikely(rc)) {
        goto out;
    }

out:
    __unlock(me);
    return (ssize_t)count;
}

static DEVICE_ATTR(adc_samples, S_IWUSR | S_IRUGO,
    max77696_adc_adcavg_show, max77696_adc_adcavg_store);

static ssize_t max77696_adc_adcdly_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct max77696_adc *me = platform_get_drvdata(pdev);
    u16 val;
    int rc;

    __lock(me);

    rc = max77696_adc_adcdly_read(me, &val);
    if (unlikely(rc)) {
        goto out;
    }

    rc = (int)snprintf(buf, PAGE_SIZE, "%u\n", val);

out:
    __unlock(me);
    return (ssize_t)rc;
}

static ssize_t max77696_adc_adcdly_store (struct device *dev,
    struct device_attribute *devattr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
    struct max77696_adc *me = platform_get_drvdata(pdev);
    u16 val;
    int rc;

    __lock(me);

    val = (u16)simple_strtoul(buf, NULL, 10);

    rc = max77696_adc_adcdly_write(me, val);
    if (unlikely(rc)) {
        goto out;
    }

out:
    __unlock(me);
    return (ssize_t)count;
}

static DEVICE_ATTR(adc_delay,   S_IWUSR | S_IRUGO,
    max77696_adc_adcdly_show, max77696_adc_adcdly_store);

/* sys entry to read temp in celsius */
static ssize_t max77696_adc_display_temp_show (struct device *dev,
    struct device_attribute *devattr, char *buf) {

	return sprintf(buf, "%d\n", display_temp_c);
}

static DEVICE_ATTR(display_temp_c, S_IWUSR | S_IRUGO,
    max77696_adc_display_temp_show, NULL);

#define max77696_adc_channel_setup_null   NULL
#define max77696_adc_channel_release_null NULL

/* LAB126: Commented out IMON Buck and LDO setup in below functions
 * We dont need to enable them unless we implement current monitoring
 * if any, in future */
static int max77696_adc_channel_setup_imon_buck (struct max77696_adc *me,
    u8 channel)
{
#if 0
    u8 buck = (u8)(channel - MAX77696_ADC_CH_IMONB1);
    return max77696_buck_set_imon_enable(buck, 1);
#endif
    return 0;
}

static int max77696_adc_channel_release_imon_buck (struct max77696_adc *me,
    u8 channel)
{
#if 0
    u8 buck = (u8)(channel - MAX77696_ADC_CH_IMONB1);
    return max77696_buck_set_imon_enable(buck, 0);
#endif
    return 0;
}

static int max77696_adc_channel_setup_imon_ldo (struct max77696_adc *me,
    u8 channel)
{
#if 0
    u8 ldo = (u8)(channel - MAX77696_ADC_CH_IMONL1);
    return max77696_ldo_set_imon_enable(ldo, 1);
#endif
    return 0;
}

static int max77696_adc_channel_release_imon_ldo (struct max77696_adc *me,
    u8 channel)
{
#if 0
    u8 ldo = (u8)(channel - MAX77696_ADC_CH_IMONL1);
    return max77696_ldo_set_imon_enable(ldo, 0);
#endif
    return 0;
}

struct max77696_adc_channel {
    u8  physical_channel;

    int (*setup)(struct max77696_adc *me, u8 channel);
    int (*release)(struct max77696_adc *me, u8 channel);
};

#define ADC_CHANNEL(_ch, _phys_ch, _ctrl_fn) \
        [MAX77696_ADC_CH_##_ch] = {\
            .physical_channel = _phys_ch,\
            .setup            = max77696_adc_channel_setup_##_ctrl_fn,\
            .release          = max77696_adc_channel_release_##_ctrl_fn,\
        }

static struct max77696_adc_channel max77696_adc_channels[ADC_NR_CHANNELS] =
{
    ADC_CHANNEL(VSYS2,   0, null     ),
    ADC_CHANNEL(TDIE,    1, null     ),
    ADC_CHANNEL(VSYS1,   3, null     ),
    ADC_CHANNEL(VCHGINA, 4, null     ),
    ADC_CHANNEL(ICHGINA, 5, null     ),
    ADC_CHANNEL(IMONL1,  6, imon_ldo ),
    ADC_CHANNEL(IMONL2,  6, imon_ldo ),
    ADC_CHANNEL(IMONL3,  6, imon_ldo ),
    ADC_CHANNEL(IMONL4,  6, imon_ldo ),
    ADC_CHANNEL(IMONL5,  6, imon_ldo ),
    ADC_CHANNEL(IMONL6,  6, imon_ldo ),
    ADC_CHANNEL(IMONL7,  6, imon_ldo ),
    ADC_CHANNEL(IMONL8,  6, imon_ldo ),
    ADC_CHANNEL(IMONL9,  6, imon_ldo ),
    ADC_CHANNEL(IMONL10, 6, imon_ldo ),
    ADC_CHANNEL(IMONB1,  7, imon_buck),
    ADC_CHANNEL(IMONB2,  7, imon_buck),
    ADC_CHANNEL(IMONB3,  8, imon_buck),
    ADC_CHANNEL(IMONB4,  8, imon_buck),
    ADC_CHANNEL(IMONB5, 10, imon_buck),
    ADC_CHANNEL(IMONB6, 10, imon_buck),
    ADC_CHANNEL(AIN0,    9, ain_mux  ),
    ADC_CHANNEL(AIN1,   11, ain_mux  ),
    ADC_CHANNEL(AIN2,   12, ain_mux  ),
    ADC_CHANNEL(AIN3,   13, ain_mux  ),
};

static int max77696_adc_channel_setup_ain_mux(struct max77696_adc *me,
    u8 channel)
{
    u8 tmp = 0x0, adc_ch;
    u8 adc_sel0, adc_sel1;
    int rc;

    adc_ch = max77696_adc_channels[channel].physical_channel;

    /* Select ADC channel to be converted. */
    if (adc_ch > 7) {
        adc_sel0 = 0;
        adc_sel1 = (1 << (adc_ch - 8));
    } else {
        adc_sel0 = (1 << adc_ch);
        adc_sel1 = 0;
    }

    rc = max77696_adc_reg_write(me, ADCSEL0, adc_sel0);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCSEL0 write error [%d]\n", rc);
        goto out;
    }

    rc = max77696_adc_reg_write(me, ADCSEL1, adc_sel1);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCSEL0 write error [%d]\n", rc);
        goto out;
    }

    /*setup ADC Mux */
    switch(channel) {
	case MAX77696_ADC_CH_AIN0:
		tmp = 0x0;
		break;
	case MAX77696_ADC_CH_AIN1:
		tmp = 0x01;
		break;
	case MAX77696_ADC_CH_AIN2:
		tmp = 0x02;
		break;
	case MAX77696_ADC_CH_AIN3:
		tmp = 0x03;
		break;
    }

    rc = max77696_adc_reg_set_bit(me, ADCICNFG, IADCMUX, tmp);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCSEL0 write error [%d]\n", rc);
        goto out;
    }

out:
    return rc;
}

static int max77696_adc_channel_release_ain_mux(struct max77696_adc *me,
    u8 channel)
{
    u8 tmp = 0x0, adc_ch;
    u8 adc_sel0, adc_sel1;
    int rc;

    adc_ch = max77696_adc_channels[channel].physical_channel;

    /* Select ADC channel to be converted and release it! */
    if (adc_ch > 7) {
        adc_sel0 = 0;
        adc_sel1 = (1 << (adc_ch - 8));
        max77696_adc_reg_read(me, ADCSEL1, &tmp);
        rc = max77696_adc_reg_write(me, ADCSEL1, (tmp & ~adc_sel1));
        if (unlikely(rc)) {
            dev_err(me->dev, "ADCSEL1 write error [%d]\n", rc);
            goto out;
        }
    } else {
        adc_sel0 = (1 << adc_ch);
        adc_sel1 = 0;
        max77696_adc_reg_read(me, ADCSEL0, &tmp);
        rc = max77696_adc_reg_write(me, ADCSEL0, (tmp & ~adc_sel0));
        if (unlikely(rc)) {
            dev_err(me->dev, "ADCSEL0 write error [%d]\n", rc);
            goto out;
        }
    }
out:
    return rc;
}

static int max77696_adc_channel_convert (struct max77696_adc *me,
    u8 channel, u16 *data)
{
    u8 adc_busy, adc_ch, tmp;
    u16 val;
    unsigned long timeout;
    int rc;

    adc_ch = max77696_adc_channels[channel].physical_channel;

    /* Enable ADC by setting the ADC Enable (ADCEN) bit to 1
     * which in turn forces the ADC reference on
     * even if ADCREFEN bit is set to 0.
     */
    rc = max77696_adc_reg_set_bit(me, ADCCNTL, ADCEN, 1);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCCNTL write error [%d]\n", rc);
        goto out;
    }

    /* Setup ADC channel for conversion */
    if (likely(max77696_adc_channels[channel].setup)) {
        rc = max77696_adc_channels[channel].setup(me, channel);
        if (unlikely(rc)) {
	    dev_err(me->dev, "failed to setup channel %u [%d]\n", channel, rc);
	    goto out;
	}
    }

    /* Initiate ADC conversion sequence
     * by setting the ADC Start Conversion (ADCCONV) to 1.
     */
    rc = max77696_adc_reg_set_bit(me, ADCCNTL, ADCCONV, 1);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCCNTL write error [%d]\n", rc);
        goto out;
    }

    /* Check availability of ADC val by inspecting the ADCCONV bit.
     * This bit is automatically cleared to 0
     * when an ADC conversion sequence has completed.
     */

    timeout = jiffies + msecs_to_jiffies(ADC_CONVERSION_TIME_OUT);

    do {
        if (unlikely(time_after(jiffies, timeout))) {
            dev_err(me->dev, "adc conversion timed out\n");
            rc = - -ETIMEDOUT;
            goto out;
        }
        msleep(1);
        max77696_adc_reg_get_bit(me, ADCCNTL, ADCCONV, &adc_busy);
    } while (likely(adc_busy));

    /* Read ADC conversion result. */
    rc = max77696_adc_reg_set_bit(me, ADCCHSEL, ADCCH, adc_ch);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCCHSEL write failed [%d]\n", rc);
        goto out;
    }

    max77696_read(me->i2c, 0x2A, &tmp);

    rc = max77696_adc_reg_read(me, ADCDATAH, &tmp);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCDATAH read failed [%d]\n", rc);
        goto out;
    }

    val = (tmp << 8);

    rc = max77696_adc_reg_read(me, ADCDATAL, &tmp);
    if (unlikely(rc)) {
        dev_err(me->dev, "ADCDATAL read failed [%d]\n", rc);
        goto out;
    }

    val = (val | tmp);

    *data = val;

    /* Release ADC channel after conversion */
    if (likely(max77696_adc_channels[channel].release)) {
        max77696_adc_channels[channel].release(me, channel);
    }

out:
    /* Disable the ADC Block and Reference when not in use
     * to save power. */
    max77696_adc_reg_write(me, ADCCNTL, 0);

    return rc;
}

static ssize_t max77696_adc_channel_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
    struct max77696_adc *me = platform_get_drvdata(pdev);
    struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
    u16 val;
    int rc;

    __lock(me);

    rc = max77696_adc_channel_convert(me, (u8)(attr->index), &val);
    if (unlikely(rc)) {
        goto out;
    }

    rc = (int)snprintf(buf, PAGE_SIZE, "%u\n", val);

out:
    __unlock(me);
    return (ssize_t)rc;
}

#define ADC_CHANNEL_DEV_ATTR(_name, _ch) \
        SENSOR_DEVICE_ATTR(_name, S_IRUGO,\
            max77696_adc_channel_show, NULL, MAX77696_ADC_CH_##_ch)

static ADC_CHANNEL_DEV_ATTR(vsys2,   VSYS2  );
static ADC_CHANNEL_DEV_ATTR(tdie,    TDIE   );
static ADC_CHANNEL_DEV_ATTR(vsys1,   VSYS1  );
static ADC_CHANNEL_DEV_ATTR(vchgina, VCHGINA);
static ADC_CHANNEL_DEV_ATTR(ichgina, ICHGINA);
static ADC_CHANNEL_DEV_ATTR(imonl1,  IMONL1 );
static ADC_CHANNEL_DEV_ATTR(imonl2,  IMONL2 );
static ADC_CHANNEL_DEV_ATTR(imonl3,  IMONL3 );
static ADC_CHANNEL_DEV_ATTR(imonl4,  IMONL4 );
static ADC_CHANNEL_DEV_ATTR(imonl5,  IMONL5 );
static ADC_CHANNEL_DEV_ATTR(imonl6,  IMONL6 );
static ADC_CHANNEL_DEV_ATTR(imonl7,  IMONL7 );
static ADC_CHANNEL_DEV_ATTR(imonl8,  IMONL8 );
static ADC_CHANNEL_DEV_ATTR(imonl9,  IMONL9 );
static ADC_CHANNEL_DEV_ATTR(imonl10, IMONL10);
static ADC_CHANNEL_DEV_ATTR(imonb1,  IMONB1 );
static ADC_CHANNEL_DEV_ATTR(imonb2,  IMONB2 );
static ADC_CHANNEL_DEV_ATTR(imonb3,  IMONB3 );
static ADC_CHANNEL_DEV_ATTR(imonb4,  IMONB4 );
static ADC_CHANNEL_DEV_ATTR(imonb5,  IMONB5 );
static ADC_CHANNEL_DEV_ATTR(imonb6,  IMONB6 );
static ADC_CHANNEL_DEV_ATTR(ain0,    AIN0   );
static ADC_CHANNEL_DEV_ATTR(ain1,    AIN1   );
static ADC_CHANNEL_DEV_ATTR(ain2,    AIN2   );
static ADC_CHANNEL_DEV_ATTR(ain3,    AIN3   );

static struct attribute* max77696_adc_attr[] = {
    /* Display temperature in Celsius */
    &dev_attr_display_temp_c.attr,

    /* ADC Sample Average Rate */
    &dev_attr_adc_samples.attr,

    /* ADC Delay (in nano-seconds) */
    &dev_attr_adc_delay.attr,

    /* ADC Channels */
    &sensor_dev_attr_vsys2.dev_attr.attr,
    &sensor_dev_attr_tdie.dev_attr.attr,
    &sensor_dev_attr_vsys1.dev_attr.attr,
    &sensor_dev_attr_vchgina.dev_attr.attr,
    &sensor_dev_attr_ichgina.dev_attr.attr,
    &sensor_dev_attr_imonl1.dev_attr.attr,
    &sensor_dev_attr_imonl2.dev_attr.attr,
    &sensor_dev_attr_imonl3.dev_attr.attr,
    &sensor_dev_attr_imonl4.dev_attr.attr,
    &sensor_dev_attr_imonl5.dev_attr.attr,
    &sensor_dev_attr_imonl6.dev_attr.attr,
    &sensor_dev_attr_imonl7.dev_attr.attr,
    &sensor_dev_attr_imonl8.dev_attr.attr,
    &sensor_dev_attr_imonl9.dev_attr.attr,
    &sensor_dev_attr_imonl10.dev_attr.attr,
    &sensor_dev_attr_imonb1.dev_attr.attr,
    &sensor_dev_attr_imonb2.dev_attr.attr,
    &sensor_dev_attr_imonb3.dev_attr.attr,
    &sensor_dev_attr_imonb4.dev_attr.attr,
    &sensor_dev_attr_imonb5.dev_attr.attr,
    &sensor_dev_attr_imonb6.dev_attr.attr,
    &sensor_dev_attr_ain0.dev_attr.attr,
    &sensor_dev_attr_ain1.dev_attr.attr,
    &sensor_dev_attr_ain2.dev_attr.attr,
    &sensor_dev_attr_ain3.dev_attr.attr,
    NULL
};

static const struct attribute_group max77696_adc_attr_group = {
    .attrs = max77696_adc_attr,
};


static __devinit int max77696_adc_probe (struct platform_device *pdev)
{
    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_adc_platform_data *pdata = dev_get_platdata(&(pdev->dev));
    struct max77696_adc *me;
    int rc;

    if (unlikely(!pdata)) {
        dev_err(&(pdev->dev), "platform data is missing\n");
        return -EINVAL;
    }

    me = kzalloc(sizeof(*me), GFP_KERNEL);
    if (unlikely(!me)) {
        dev_err(&(pdev->dev), "out of memory (%uB requested)\n", sizeof(*me));
        return -ENOMEM;
    }

    platform_set_drvdata(pdev, me);

    mutex_init(&(me->lock));
    me->chip = chip;
    me->i2c  = __get_i2c(chip);
    me->dev  = &(pdev->dev);
    me->kobj = &(pdev->dev.kobj);

    /* We don't use any ADC interrupts. We are just polling. */
    max77696_adc_reg_write(me, ADCINTM, 0xFF);

    me->hwmon = hwmon_device_register(me->dev);
    if (unlikely(IS_ERR(me->hwmon))) {
        rc = PTR_ERR(me->hwmon);
        me->hwmon = NULL;

        dev_err(me->dev, "failed to register hwmon device [%d]\n", rc);
        goto out_err;
    }

    rc = sysfs_create_group(me->kobj, &max77696_adc_attr_group);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to create attribute group [%d]\n", rc);
        goto out_err;
    }

    BUG_ON(chip->adc_ptr);
    chip->adc_ptr = me;

    /* Set defaults given via platform data */
    max77696_adc_adcavg_write(me, pdata->avg_rate);
    max77696_adc_adcdly_write(me, pdata->adc_delay);
    max77696_adc_cnfgadc_write(me, pdata->current_src);

    /* Show defaults */
    dev_info(me->dev, "ADC average rate: %u sample(s)\n", pdata->avg_rate);
    dev_info(me->dev, "ADC delay: %u nsec\n", pdata->adc_delay);
    dev_info(me->dev, "ADC cur src: %u uA\n", pdata->current_src);

    schedule_delayed_work(&adc_disp_temp_work, msecs_to_jiffies(0));

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(adc, chip);
    return 0;

out_err:
    if (likely(me->hwmon)) {
        hwmon_device_unregister(me->hwmon);
    }
    platform_set_drvdata(pdev, NULL);
    mutex_destroy(&(me->lock));
    kfree(me);
    return rc;
}

static __devexit int max77696_adc_remove (struct platform_device *pdev)
{
    struct max77696_adc *me = platform_get_drvdata(pdev);

    cancel_delayed_work_sync(&adc_disp_temp_work);
    sysfs_remove_group(me->kobj, &max77696_adc_attr_group);
    hwmon_device_unregister(me->hwmon);

    platform_set_drvdata(pdev, NULL);
    mutex_destroy(&(me->lock));
    kfree(me);

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77696_adc_suspend (struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct max77696_adc *me = platform_get_drvdata(pdev);

    cancel_delayed_work_sync(&adc_disp_temp_work);

    __lock(me);

    /* Disable ADC reference and core, cancel active conversions
     * and return to default number of samples. */
    max77696_adc_reg_write(me, ADCCNTL, 0);
    max77696_adc_cnfgadc_write(me, 0);

    __unlock(me);

    return 0;
}

static int max77696_adc_resume (struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct max77696_adc_platform_data *pdata = dev_get_platdata(&(pdev->dev));
    struct max77696_adc *me = platform_get_drvdata(pdev);

    __lock(me);

    /* write back platform values */
    max77696_adc_adcavg_write(me, pdata->avg_rate);
    max77696_adc_cnfgadc_write(me, pdata->current_src);

    __unlock(me);
    /* Restart the temp collection worker now */
    schedule_delayed_work(&adc_disp_temp_work, msecs_to_jiffies(0));

    return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(max77696_adc_pm,
    max77696_adc_suspend, max77696_adc_resume);

static struct platform_driver max77696_adc_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .driver.pm    = &max77696_adc_pm,
    .probe        = max77696_adc_probe,
    .remove       = __devexit_p(max77696_adc_remove),
};

static __init int max77696_adc_driver_init (void)
{
    return platform_driver_register(&max77696_adc_driver);
}

static __exit void max77696_adc_driver_exit (void)
{
    platform_driver_unregister(&max77696_adc_driver);
}

module_init(max77696_adc_driver_init);
module_exit(max77696_adc_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

/*******************************************************************************
 * EXTERNAL FUNCTIONS
 ******************************************************************************/

extern struct max77696_chip* max77696;

int max77696_adc_read (u8 channel, u16 *data)
{
    struct max77696_chip *chip = max77696;
    struct max77696_adc *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->adc_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_adc is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);

    if (unlikely(channel >= ADC_NR_CHANNELS)) {
        rc = -EINVAL;
        goto out;
    }

    rc = max77696_adc_channel_convert(me, channel, data);

out:
    __unlock(me);
    return rc;
}
EXPORT_SYMBOL(max77696_adc_read);

/************************************************************************
 * ADC SOURCE DRIVER
 ************************************************************************/

/********** NTC Display temperature *************************************/
#define TEMP_RANGE 86

struct lkup_data {
  int temp;
  int code;
};

static struct lkup_data ntc_lkup[TEMP_RANGE] = {
{75,0x95},
{74,0x9A},
{73,0x9F},
{72,0xA4},
{71,0xAA},
{70,0xAF},

{69,0xB4},
{68,0xBA},
{67,0xBF},
{66,0xC5},
{65,0xCA},
{64,0xD0},
{63,0xD6},
{62,0xDC},
{61,0xE3},
{60,0xE9},

{59,0xF0},
{58,0xF7},
{57,0xFE},
{56,0x105},
{55,0x10D},
{54,0x114},
{53,0x11D},
{52,0x125},
{51,0x12D},
{50,0x136},

{49,0x140},
{48,0x149},
{47,0x153},
{46,0x15D},
{45,0x168},
{44,0x172},
{43,0x17E},
{42,0x189},
{41,0x195},
{40,0x1A2},

{39,0x1AF},
{38,0x1BC},
{37,0x1CA},
{36,0x1D8},
{35,0x1E6},
{34,0x1F6},
{33,0x205},
{32,0x215},
{31,0x226},
{30,0x237},

{29,0x249},
{28,0x25B},
{27,0x26E},
{26,0x281},
{25,0x295},
{24,0x2A9},
{23,0x2BE},
{22,0x2D4},
{21,0x2EA},
{20,0x301},

{19,0x319},
{18,0x331},
{17,0x34A},
{16,0x363},
{15,0x37E},
{14,0x399},
{13,0x3B4},
{12,0x3D1},
{11,0x3EE},
{10,0x40C},

{9,0x42B},
{8,0x44A},
{7,0x46A},
{6,0x48B},
{5,0x4AD},
{4,0x4D0},
{3,0x4F3},
{2,0x517},
{1,0x53C},
{0,0x562},

{-1,0x589},
{-2,0x5B1},
{-3,0x5DA},
{-4,0x603},
{-5,0x62E},
{-6,0x659},
{-7,0x686},
{-8,0x6B3},
{-9,0x6E1},
{-10,0x710},
};

/********************* internal functions *******************************/
// returns 'floor' of temp range for any ADC value within that range
static int lkup_temp_data(int data)
{
  int start=0, end=TEMP_RANGE-1, mid;

  if(unlikely(data >= ntc_lkup[end].code)) {
    printk(KERN_INFO "\nADC value out of thermistor range! setting\
 display temp to %d deg C\n", ntc_lkup[end].temp);
    return ntc_lkup[end].temp;
  }

  if(unlikely(data <= ntc_lkup[start].code)) {
    printk(KERN_INFO "\nADC value out of thermistor range! setting\
 display temp to %d deg C\n", ntc_lkup[start].temp);
    return ntc_lkup[start].temp;
  }

  /* modified bsearch */
  while(start<=end) {
    /* will converge eventually! */
    mid = (start+end)/2;

    if(data < ntc_lkup[mid].code)
	    end = mid;
    else if (data > ntc_lkup[mid].code)
	    start = mid;
    else
	    return ntc_lkup[mid].temp;

    if(abs(end-start) <=1) {
        // floor is 'end' idx temp since its descending in lkup
	return ntc_lkup[end].temp;
    }
  }
  return DISP_DEFAULT_TEMP;
}


/********* ADC Source external interfaces *******************************/
/* Display Panel NTC interface */
static void adc_disp_gettemp_work(struct work_struct *work)
{
    u16 data = 0x0;

    max77696_adc_read(MAX77696_ADC_CH_AIN0, &data);

    /* Remove ADC grnd ref noise in boards with analog grnd island */
    if(lab126_board_rev_greater(BOARD_ID_WARIO_2) ||
	lab126_board_is(BOARD_ID_ICEWINE_WARIO_512) ||
	lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO_512) ||
        lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_P5) ||
	lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_P5) ||
	lab126_board_rev_greater(BOARD_ID_PINOT_WFO_EVT1) ||
	lab126_board_rev_greater(BOARD_ID_PINOT_WFO_2GB_EVT1) ||
	lab126_board_is(BOARD_ID_PINOT_2GB) ||
	lab126_board_is(BOARD_ID_PINOT) ||
	lab126_board_is(BOARD_ID_MUSCAT_WAN) ||
	lab126_board_is(BOARD_ID_MUSCAT_WFO) ||
	lab126_board_is(BOARD_ID_MUSCAT_32G_WFO) ||
	lab126_board_is(BOARD_ID_BOURBON_WFO) ||
        lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2) ||
	lab126_board_is(BOARD_ID_WHISKY_WFO) ||
	lab126_board_is(BOARD_ID_WHISKY_WAN) ||
	lab126_board_is(BOARD_ID_WOODY)) { 
            u16 noise = 0x0;
            max77696_adc_read(MAX77696_ADC_CH_AIN2, &noise);
            /* diff to compensate */
            data = abs(data - noise);
    }

    /*this is the only place display_temp gets updated, so no mutex needed! */
    display_temp_c = lkup_temp_data(data);

    schedule_delayed_work(&adc_disp_temp_work, msecs_to_jiffies(DISP_ADC_TEMP_THRESHOLD));
    return;
}


