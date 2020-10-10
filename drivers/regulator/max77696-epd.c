/*
 * Copyright (C) 2012 Maxim Integrated Product
 * Jayden Cha <jayden.cha@maxim-ic.com>
 * Copyright 2012-2013 Amazon Technologies, Inc.
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
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max77696.h>
#include <linux/mfd/max77696-events.h>
#include <max77696_registers.h>
#include <mach/boardid.h>
#include <llog.h>

#define DRIVER_DESC    "MAX77696 VREG Driver"
#define DRIVER_NAME    MAX77696_EPD_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#ifdef VERBOSE
#define dev_noise(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_noise(args...) do { } while (0)
#endif /* VERBOSE */

#define EPD_POWERDOWN_TIMEOUT_US    10000 // 10ms

struct max77696_regulator_data {
	struct	platform_device *pdev;
	int		id;
	int		min_uV, max_uV, step_uV;
	int		neg_sign;
};

struct max77696_epd_data {
	struct max77696_chip      *chip;
	struct max77696_i2c       *i2c;
	struct device             *dev;
	struct regulator_desc 	  *rdesc;
	/* Client regulator devices */
	struct max77696_regulator_data *regdata;
	int pwrgood_gpio;
	int pwrgood_irq;
	int irq;
	struct regulator* display_3v2_regulator;
	struct mutex lock;
};

#define __get_i2c(chip)                (&((chip)->pmic_i2c))
#define __lock(me)                     mutex_lock(&((me)->lock))
#define __unlock(me)                   mutex_unlock(&((me)->lock))

/* GPIO Register Read/Write */
#define max77696_vreg_reg_read(me, reg, val_ptr) \
	max77696_read((me)->i2c, VREG_REG(reg), val_ptr)
#define max77696_vreg_reg_write(me, reg, val) \
        max77696_write((me)->i2c, VREG_REG(reg), val)
#define max77696_vreg_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, VREG_REG(reg), dst, len)
#define max77696_vreg_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, VREG_REG(reg), src, len)
#define max77696_vreg_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, VREG_REG(reg), mask, val_ptr)
#define max77696_vreg_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, VREG_REG(reg), mask, val)

#define mV_to_uV(mV) (mV * 1000)
#define uV_to_mV(uV) (uV / 1000)
#define V_to_uV(V) (mV_to_uV(V * 1000))
#define uV_to_V(uV) (uV_to_mV(uV) / 1000)

#define EPD_VC5FLT    (BIT(0))
#define EPD_VDDHFLT   (BIT(1))
#define EPD_VPOSFLT   (BIT(2))
#define EPD_VNEGFLT   (BIT(3))
#define EPD_VEEFLT    (BIT(4))
#define EPD_VHVINPFLT (BIT(5))
#define EPD_VCOMFLT   (BIT(6))

/* VREG Register Single Bit Ops */
#define max77696_vreg_reg_get_bit(me, reg, bit, val_ptr) \
	({\
            int __rc = max77696_vreg_reg_read_masked(me, reg,\
                VREG_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = VREG_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_vreg_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_vreg_reg_write_masked(me, reg,\
                VREG_REG_BITMASK(reg, bit), VREG_REG_BITSET(reg, bit, val));\
        })


static int max77696_epd_regulator_device_init(struct max77696_epd_data *me, int reg,
					struct regulator_init_data *initdata);


/* Event handling functions */
int max77696_epdok_mask(void *obj, u16 event, bool mask_f)
{
	u8 irq, bit_pos, buf=0x0;
	int rc;
	struct max77696_epd_data *me = (struct max77696_epd_data *)obj;

	DECODE_EVENT(event, irq, bit_pos);
	__lock(me);
	rc = max77696_vreg_reg_read(me, EPDOKINTM, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDOKINTM read error [%d]\n", rc);
		goto out;
	}

	/*
	 * We don't want to receive this interrupt through topsys
	 * since it has a dedicated ISR (max77696_epd_pok_isr), so
	 * never unmask it.
	 */
	if (mask_f) {
		rc = max77696_vreg_reg_write(me, EPDOKINTM, (buf | bit_pos));
		if (unlikely(rc)) {
			dev_err(me->dev, "EPDOKINTM write error [%d]\n", rc);
			goto out;
		}
	}
out:
	__unlock(me);
	return rc;
}

extern struct max77696_chip* max77696;

int max77696_epd_set_vddh(int vddh_mV)
{
	struct max77696_chip *chip = max77696;
	struct max77696_epd_data *me;
	int rc = 0;
	
	if (unlikely(!chip)) {
		pr_err("%s: max77696_chip is not ready\n", __func__);
		return -ENODEV;
	}

	me = chip->epd_ptr;
	if (unlikely(!me)) {
		pr_err("%s: max77696_epd is not ready\n", __func__);
		return -ENODEV;
	}

	__lock(me);
	rc = max77696_vreg_reg_write(me, EPDVDDH, (vddh_mV - 15000) / 470 );
	__unlock(me);
 	if (unlikely(rc)) {
		dev_err(me->dev, "EPDVDDH write error [%d]\n", rc);
   	}

	return rc;
}
EXPORT_SYMBOL(max77696_epd_set_vddh);

/******** Regulator ops *********/
static int max77696_epd_vreg_set_voltage(struct regulator_dev *dev, int min_uV, 
						int max_uV, unsigned *selector);
static int max77696_epd_vreg_get_voltage(struct regulator_dev *dev);
static int max77696_epd_vreg_enable(struct regulator_dev *dev);
static int max77696_epd_vreg_disable(struct regulator_dev *dev);
static int max77696_epd_vreg_is_enabled(struct regulator_dev *dev);

static int max77696_epd_vreg_set_voltage(struct regulator_dev *dev, int min_uV, 
						int max_uV, unsigned *selector)
{
	return 0;
}

static int max77696_epd_vreg_get_voltage(struct regulator_dev *dev)
{
	return 0;
}

static int max77696_epd_vreg_enable (struct regulator_dev *rdev)
{
	return 0;
}

static int max77696_epd_vreg_is_enabled (struct regulator_dev *rdev)
{
	return 0;
}

static int max77696_epd_vreg_disable (struct regulator_dev *rdev)
{
	return 0;
}

static struct regulator_ops max77696_vreg_epd_ops = {
	.set_voltage = max77696_epd_vreg_set_voltage,
	.get_voltage = max77696_epd_vreg_get_voltage,

	/* enable/disable regulator */
	.enable      = max77696_epd_vreg_enable,
	.disable     = max77696_epd_vreg_disable,
	.is_enabled  = max77696_epd_vreg_is_enabled,
};

/******Display ops *****/
static int max77696_epd_display_enable(struct regulator_dev *rdev);
static int max77696_epd_display_disable(struct regulator_dev *rdev);
static int max77696_epd_display_is_enabled(struct regulator_dev *rdev);

static int _display_enable(struct max77696_epd_data *me)
{
	int rc = 0;

	dev_dbg(me->dev, "enable EPD psy master\n");

	__lock(me);
	rc = max77696_vreg_reg_set_bit(me, EPDCNFG, EPDEN, 1);
	__unlock(me);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDCNFG write error [%d]\n", rc);
	}

	return rc;
}

static int _display_disable(struct max77696_epd_data *me)
{
	int rc = 0;

	dev_dbg(me->dev, "disable EPD psy master\n");
	__lock(me);
	rc = max77696_vreg_reg_set_bit(me, EPDCNFG, EPDEN, 0);
	__unlock(me);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDCNFG write error [%d]\n", rc);
	}

	return rc;
}

static inline int _display_is_enabled(struct max77696_epd_data *me)
{
	u8 rc;
	u8 fault = 0x0;
	u8 epden = 0;

	__lock(me);
	rc = max77696_vreg_reg_get_bit(me, EPDCNFG, EPDEN, &epden);
	__unlock(me);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDCNFG read error [%d]\n", rc);
		return 0;
	}
	if (epden == 0)
	{
		dev_dbg(me->dev, "%s EPDEN == 0\n", __func__);
		return 0;
	}
	__lock(me);
	rc = max77696_vreg_reg_read(me, EPDINT, &fault);
	__unlock(me);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDINT read error [%d]\n", rc);
		return 0;	
	}
	if (unlikely(fault)) {
		if (fault & MAX77696_EPDINTS_VCOMFLTS)  dev_err(me->dev, "VCOM_fault");
		if (fault & MAX77696_EPDINTS_HVINPFLTS) dev_err(me->dev, "HVINP_fault");
		if (fault & MAX77696_EPDINTS_VEEFLTS)   dev_err(me->dev, "VEE_fault");
		if (fault & MAX77696_EPDINTS_VNEGFLTS)  dev_err(me->dev, "VNEG_fault");
		if (fault & MAX77696_EPDINTS_VPOSFLTS)  dev_err(me->dev, "VPOS_fault");
		if (fault & MAX77696_EPDINTS_VDDFLTS)   dev_err(me->dev, "VDD_fault");
		if (fault & MAX77696_EPDINTS_VC5FLTS)   dev_err(me->dev, "VC5_fault");
		dev_err(me->dev, "EPDINT: Regulator Fault Detected [%02x]\n", fault);
		return 0;
	}
	
	return !!gpio_get_value(me->pwrgood_gpio);
}

static int _display_wait_for_disable(struct max77696_epd_data *me, long timeout)
{
	long elapsed = 0;
	while (_display_is_enabled(me)) {
		if (elapsed >= timeout)
			return -EFAULT;

		udelay(100);
		elapsed += 100;
	}

	return 0;
}

static int max77696_epd_display_enable(struct regulator_dev *rdev)
{
	struct max77696_epd_data *me = rdev_get_drvdata(rdev);
	return _display_enable(me);
}

static int max77696_epd_display_disable(struct regulator_dev *rdev)
{
	struct max77696_epd_data *me = rdev_get_drvdata(rdev);
	int rc = _display_disable(me);
	if (rc)
		return rc;
	return _display_wait_for_disable(me, EPD_POWERDOWN_TIMEOUT_US);
}

static int max77696_epd_display_is_enabled(struct regulator_dev *rdev)
{
	struct max77696_epd_data *me = rdev_get_drvdata(rdev);
	return _display_is_enabled(me);
}

static struct regulator_ops max77696_epd_display_ops = {
	.set_voltage = max77696_epd_vreg_set_voltage,
	.get_voltage = max77696_epd_vreg_get_voltage,

	/* enable/disable regulator */
	.enable      = max77696_epd_display_enable,
	.disable     = max77696_epd_display_disable,
	.is_enabled  = max77696_epd_display_is_enabled,
};


/*
 * EPD regulator constraints data
 */
struct max77696_regulator_data reg_data[MAX77696_EPD_NR_REGS] = {
        [MAX77696_EPD_ID_EPDEN] = {
		.id = MAX77696_EPD_ID_EPDEN,
	},
	[MAX77696_EPD_ID_EPDVCOM] = {
		.id = MAX77696_EPD_ID_EPDVCOM,
		.min_uV = V_to_uV(0),
		.max_uV = V_to_uV(5),
		.step_uV = 9985,
		.neg_sign = true,
	},
        [MAX77696_EPD_ID_EPDVEE] = {
		.id = MAX77696_EPD_ID_EPDVEE,
		.min_uV = V_to_uV(15),
		.max_uV = V_to_uV(28),
		.step_uV = mV_to_uV(420),
		.neg_sign = true,
	},
        [MAX77696_EPD_ID_EPDVNEG] = {
                .id = MAX77696_EPD_ID_EPDVNEG,
		.min_uV = V_to_uV(8),
		.max_uV = V_to_uV(18),
		.step_uV = mV_to_uV(500),
		.neg_sign = true,
	},
        [MAX77696_EPD_ID_EPDVPOS] = {
		.id = MAX77696_EPD_ID_EPDVPOS,
		.min_uV = V_to_uV(8),
		.max_uV = V_to_uV(18),
		.step_uV = mV_to_uV(500),
		.neg_sign = false,
	},
        [MAX77696_EPD_ID_EPDVDDH] = {
		.id = MAX77696_EPD_ID_EPDVDDH,
		.min_uV = V_to_uV(15),
		.max_uV = mV_to_uV(29500),
		.step_uV = mV_to_uV(470),
		.neg_sign = false,
	},
};

/*********** VCOM ops **************/

static int max77696_epd_vcom_enable(struct regulator_dev *rdev);
static int max77696_epd_vcom_disable(struct regulator_dev *rdev);
static int max77696_epd_vcom_is_enabled(struct regulator_dev *rdev);
static int max77696_epd_vcom_set_voltage(struct regulator_dev *rdev, int min_uV, 
						int max_uV, unsigned *selector);
static int max77696_epd_vcom_get_voltage(struct regulator_dev *dev);

static int _vcom_enable(struct max77696_epd_data *me)
{
	int rc=0;

	dev_dbg(me->dev, "enable VCOM\n");
	__lock(me);
	rc = max77696_vreg_reg_set_bit(me, EPDCNFG, VCOMEN, 1);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDCNFG write error [%d]\n", rc);
		goto out;
	}
out:
	__unlock(me);
	return rc;
}

static int _vcom_disable(struct max77696_epd_data *me)
{
	int rc = 0;

	dev_dbg(me->dev, "disable VCOM\n");
	__lock(me);
	rc = max77696_vreg_reg_set_bit(me, EPDCNFG, VCOMEN, 0);
	__unlock(me);
 	if (unlikely(rc)) {
		dev_err(me->dev, "EPDCNFG write error [%d]\n", rc);
   	}

	return rc;
}

static int _vcom_is_enabled(struct max77696_epd_data *me)
{
	u8 ena = 0;
	int rc;

	__lock(me);
	rc = max77696_vreg_reg_get_bit(me, EPDCNFG, VCOMEN, &ena);
	__unlock(me);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDVCOMR read error [%d]\n", rc);
	}

	return !!ena;
}

static int _vcom_set_voltage(struct max77696_epd_data *me, int min_uV, int max_uV)
{
	int value = 0x0;
	int rc;
	
	if(min_uV < me->regdata[MAX77696_EPD_ID_EPDVCOM].min_uV ||
		max_uV > me->regdata[MAX77696_EPD_ID_EPDVCOM].max_uV) {
		return -EINVAL;
	}

	value = DIV_ROUND_CLOSEST(max_uV, me->regdata[MAX77696_EPD_ID_EPDVCOM].step_uV);

	__lock(me);
	if(value >= 0xFF) {
		value -= 0xFF;
		rc = max77696_vreg_reg_set_bit(me, EPDVCOMR, VCOMR, 1);
	} else {
		rc = max77696_vreg_reg_set_bit(me, EPDVCOMR, VCOMR, 0);
	}
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDVCOMR write error [%d]\n", rc);
		goto out;
	}
	rc = max77696_vreg_reg_write(me, EPDVCOM, (u8)value);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDVCOM write error [%d]\n", rc);
		goto out;
	}
out:	
	__unlock(me);
	return rc;
}

static int _vcom_get_uv(struct max77696_epd_data *me)
{
	int rc;
	u8 vcom_r;
	u8 vcom;
	int vcom_uv = 0;

	__lock(me);
	
	rc = max77696_vreg_reg_get_bit(me, EPDVCOMR, VCOMR, &vcom_r);
	
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDVCOMR write error [%d]\n", rc);
		goto out;
	}
	rc = max77696_vreg_reg_read(me, EPDVCOM, &vcom);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDVCOM write error [%d]\n", rc);
		goto out;
	}
out:	
	__unlock(me);
	vcom_uv = (vcom_r << 8) | vcom;
	vcom_uv *= reg_data[MAX77696_EPD_ID_EPDVCOM].step_uV;

	return rc ? rc : vcom_uv;	
}

static int max77696_epd_vcom_enable(struct regulator_dev *rdev)
{
	struct max77696_epd_data *me = rdev_get_drvdata(rdev);
	return _vcom_enable(me);
}

static int max77696_epd_vcom_disable(struct regulator_dev *rdev)
{
	struct max77696_epd_data *me = rdev_get_drvdata(rdev);
	return _vcom_disable(me);
}

static int max77696_epd_vcom_is_enabled(struct regulator_dev *rdev)
{
	struct max77696_epd_data *me = rdev_get_drvdata(rdev);
	return _vcom_is_enabled(me);
}

static int max77696_epd_vcom_set_voltage(struct regulator_dev *rdev, int min_uV, 
						int max_uV, unsigned *selector)
{
	struct max77696_epd_data *me = rdev_get_drvdata(rdev);
	return _vcom_set_voltage(me, max_uV, max_uV);
}

static int max77696_epd_vcom_get_voltage(struct regulator_dev *rdev)
{
	struct max77696_epd_data *me = rdev_get_drvdata(rdev);
	return _vcom_get_uv(me);
}

static struct regulator_ops max77696_epd_vcom_ops = {
	.set_voltage = max77696_epd_vcom_set_voltage,
	.get_voltage = max77696_epd_vcom_get_voltage,

	/* enable/disable regulator */
	.enable      = max77696_epd_vcom_enable,
	.disable     = max77696_epd_vcom_disable,
	.is_enabled  = max77696_epd_vcom_is_enabled,
};
/******************VCOM ops end ***********/

/*
 * Regulator descriptors
 */
static struct regulator_desc epd_rdesc[MAX77696_EPD_NR_REGS] = {
	
	[MAX77696_EPD_ID_EPDEN] = {
		.name = "max77696-display",
		.id = MAX77696_EPD_ID_EPDEN,
                .ops = &max77696_epd_display_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	[MAX77696_EPD_ID_EPDVCOM] = {
		.name = "max77696-vcom",
		.id = MAX77696_EPD_ID_EPDVCOM,
		.ops = &max77696_epd_vcom_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
        [MAX77696_EPD_ID_EPDVEE] = {
		.name = "max77696-vee",
		.id = MAX77696_EPD_ID_EPDVEE,
		.ops = &max77696_vreg_epd_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
        [MAX77696_EPD_ID_EPDVNEG] = {
                .name = "max77696-vneg",
                .id = MAX77696_EPD_ID_EPDVNEG,
	        .ops = &max77696_vreg_epd_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
        [MAX77696_EPD_ID_EPDVPOS] = {
		.name = "max77696-vpos",
		.id = MAX77696_EPD_ID_EPDVPOS,
		.ops = &max77696_vreg_epd_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
        [MAX77696_EPD_ID_EPDVDDH] = {
		.name = "max77696-vddh",
		.id = MAX77696_EPD_ID_EPDVDDH,
		.ops = &max77696_vreg_epd_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};	  

/*
 * Regulator init/probing/exit functions
 */
static int epd_regulator_probe(struct platform_device *pdev)
{
	struct max77696_epd_data *me = platform_get_drvdata(pdev);
	struct regulator_dev *rdev;
	int rc = 0;

	rdev = regulator_register(&(epd_rdesc[pdev->id]), &(pdev->dev),
		pdev->dev.platform_data, me);

	if (unlikely(IS_ERR(rdev))) {
		rc = PTR_ERR(rdev);
		rdev = NULL;
		dev_err(me->dev, "failed to register regulator device %s with rc=[%d]\n", epd_rdesc[pdev->id].name, rc);
	}

	platform_set_drvdata(pdev, rdev); 

	return rc;
}

static int epd_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);
	regulator_unregister(rdev);
	return 0;
}

static struct platform_driver epd_regulator_driver = {
        .probe = epd_regulator_probe,
        .remove = epd_regulator_remove,
        .driver = {
                .name = "epd-vreg",
        },
};
/* regulator init/probing/exit functions */
static int max77696_epd_regulator_device_init(struct max77696_epd_data *me, int reg,
						struct regulator_init_data *initdata)
{
	struct platform_device *pdev;
	int rc = 0;

	if (me->regdata[reg].pdev)
		return -EBUSY;

	pdev = platform_device_alloc("epd-vreg", reg);
	if (!unlikely(pdev)) {
		return -ENOMEM;
	}

	me->regdata[reg].pdev = pdev;

	initdata->driver_data = me;
	pdev->dev.platform_data = initdata;
	pdev->dev.parent = me->dev;
	platform_set_drvdata(pdev, me);

	rc = platform_device_add(pdev);
	if (unlikely(rc)) {
		dev_err(me->dev, "Failed to add platform device for regulator %s: %d\n", epd_rdesc[reg].name, rc);
		platform_device_del(pdev);
		pdev = NULL;
	}
	return rc;
}

static inline void max77696_llog_fault(const char *fault)
{
	LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
	    "kernel", MAX77696_EPD_NAME, fault, 1, "");
}

static irqreturn_t max77696_epd_isr(int irq, void *data)
{
	struct max77696_epd_data *me = data;
	u8 rc;
	u8 fault = 0x0;

	rc = max77696_vreg_reg_read(me, EPDINT, &fault);
	if (unlikely(rc)) {
		dev_err(me->dev, "EPDINT read error [%d]\n", rc);
	}

	if (unlikely(fault)) {
		if (fault & MAX77696_EPDINTS_VCOMFLTS)  max77696_llog_fault("VCOM_fault");
		if (fault & MAX77696_EPDINTS_HVINPFLTS) max77696_llog_fault("HVINP_fault");
		if (fault & MAX77696_EPDINTS_VEEFLTS)   max77696_llog_fault("VEE_fault");
		if (fault & MAX77696_EPDINTS_VNEGFLTS)  max77696_llog_fault("VNEG_fault");
		if (fault & MAX77696_EPDINTS_VPOSFLTS)  max77696_llog_fault("VPOS_fault");
		if (fault & MAX77696_EPDINTS_VDDFLTS)   max77696_llog_fault("VDD_fault");
		if (fault & MAX77696_EPDINTS_VC5FLTS)   max77696_llog_fault("VC5_fault");

		dev_err(me->dev, "EPDINT: Regulator Fault Detected [%02x]\n", fault);
		pmic_event_callback(EVENT_EPD_FAULT);
	}

	return IRQ_HANDLED;
}

static irqreturn_t max77696_epd_pok_isr(int irq, void *data)
{
	pmic_event_callback(EVENT_EPD_POK);

	return IRQ_HANDLED;
}

static struct max77696_event_handler max77696_epdpok_handle = {
	.mask_irq = max77696_epdok_mask,
	.event_id = EVENT_EPD_POK,
};

static struct max77696_event_handler max77696_epdfault_handle = {
	.mask_irq = NULL,
	.event_id = EVENT_EPD_FAULT,
};

static ssize_t vcom_voltage_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77696_epd_data *me = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", _vcom_get_uv(me));
}

static ssize_t vcom_voltage_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct max77696_epd_data *me = dev_get_drvdata(dev);
	int uv = simple_strtol(buf, NULL, 10);
	
	_vcom_set_voltage(me, uv, uv);
	return size;
}

static ssize_t vcom_regulator_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77696_epd_data *me = dev_get_drvdata(dev);
	
	if(_vcom_is_enabled(me) > 0) {
		return sprintf(buf, "1");
	} 
	else {
		return sprintf(buf, "0");
	}
}

static ssize_t vcom_regulator_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct max77696_epd_data *me = dev_get_drvdata(dev);
	int rc;
	int enable = ('0' == buf[0]) ? 0 : 1;
	if(enable) {
		rc = _vcom_enable(me);
	}
	else {
		rc = _vcom_disable(me);
	}
	if(rc)
		printk(KERN_ERR "Cannot %s %s", 
				(enable ? "enable" : "disable"), 
				"vcom regulator!!\n" );
	return size;
}

static ssize_t display_regulator_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77696_epd_data *me = dev_get_drvdata(dev);
	
	if(_display_is_enabled(me) > 0) {
		return sprintf(buf, "1");
	} 
	else {
		return sprintf(buf, "0");
	}
}

static ssize_t display_regulator_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct max77696_epd_data *me = dev_get_drvdata(dev);
	int rc;
	int enable = ('0' == buf[0]) ? 0 : 1;
	if(enable) {
		rc = _display_enable(me);
	}
	else {
		rc = _display_disable(me);
	}
	if(rc)
		printk(KERN_ERR "Cannot %s display regulator!!\n", (enable ? "enable" : "disable"));
	return size;
}

static ssize_t gate_all_regulators_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	//TBD-- need a way to gate all the rails and hot swap the display
	//struct max77696_epd_data *me = dev_get_drvdata(dev);
	return 0;
}

static ssize_t gate_all_regulators_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	//TBD-- need a way to gate all the rails and hot swap the display
	//struct max77696_epd_data *me = dev_get_drvdata(dev);
	return size;
}

static ssize_t display_3v2_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77696_epd_data *me = dev_get_drvdata(dev);

	if(regulator_is_enabled(me->display_3v2_regulator))
		return sprintf(buf, "display 3v2 is enabled");
	else 
		return sprintf(buf, "display 3v2 is disabled");
	
}

static ssize_t display_3v2_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct max77696_epd_data *me = dev_get_drvdata(dev);
	int rc;
	
	switch(buf[0]){
		case '0':
			rc = regulator_disable(me->display_3v2_regulator);
			if (unlikely(rc)) {
				dev_err(dev, "%s error [%d]\n", __func__, rc);
			}
		break;
		case '1':
			rc = regulator_enable(me->display_3v2_regulator);
			if (unlikely(rc)) {
				dev_err(dev, "%s error [%d]\n", __func__, rc);
			}
		break;	
		default:
		break;
	}
	return size;
}

static struct device_attribute attributes[] = {
	__ATTR(display_3v2_enable, S_IRUSR | S_IWUSR, display_3v2_show, display_3v2_store),
	__ATTR(vcom_microvolt, S_IRUSR | S_IWUSR, vcom_voltage_show, vcom_voltage_store),
	__ATTR(vcom_regulator_enable, S_IRUSR | S_IWUSR, vcom_regulator_show, vcom_regulator_store),
	__ATTR(display_regulator_enable, S_IRUSR | S_IWUSR, display_regulator_show, display_regulator_store),
	__ATTR(gate_all_epd_regulators, S_IRUSR | S_IWUSR, gate_all_regulators_show, gate_all_regulators_store),
};

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
	{	
		if (device_create_file(dev, attributes + i))
			goto undo;
	}
	return 0;
undo:
	for (; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}

static void remove_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);
}

static __devinit int max77696_epd_vreg_probe (struct platform_device *pdev)
{
	struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77696_epd_platform_data *pdata = pdev->dev.platform_data;
	struct max77696_epd_data *me;
	int rc, i=0;
	u8 buf;

	if (unlikely(!pdata)  || !pdata->init_data) {
		dev_err(&(pdev->dev), "platform data is missing\n");
		return -ENODEV;
	}

	me = kzalloc(sizeof(*me), GFP_KERNEL);
	if (unlikely(!me)) {
		dev_err(&(pdev->dev), "out of memory (%uB requested)\n", sizeof(*me));
		return -ENOMEM;
	}
	
	mutex_init(&(me->lock));
	me->chip = chip;
	me->i2c  = __get_i2c(chip);
	me->dev  = &(pdev->dev);
	me->rdesc = epd_rdesc;
	me->regdata = reg_data;
	if(CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696)
		me->display_3v2_regulator = regulator_get(NULL, "DISP_GATED");
	else
		me->display_3v2_regulator = regulator_get(NULL, "DISP_GATED-old");

	regulator_enable(me->display_3v2_regulator);

	me->irq = chip->irq_base + MAX77696_ROOTINT_EPD;
	me->pwrgood_gpio = pdata->pwrgood_gpio;
	me->pwrgood_irq = pdata->pwrgood_irq;
	platform_set_drvdata(pdev, me);
	dev_set_drvdata(me->dev, me);

	rc = gpio_request(me->pwrgood_gpio, "epd_pok");
	if (unlikely(rc)) {
		dev_err(me->dev, "Failed to request pok gpio[%d]\n", rc);
		goto out_err1;
	}
	gpio_direction_input(me->pwrgood_gpio);

	rc = add_sysfs_interfaces(me->dev);
	if (unlikely(rc)) {
		dev_err(me->dev, "Failed to create sysfs interface[%d]\n", rc);
		goto out_err2;
	}

	rc = platform_driver_register(&epd_regulator_driver);
	if (unlikely(rc)) {
		dev_err(me->dev, "Failed to register platform driver[%d]\n", rc);
		goto out_err3;
	}

	for (i = 0; i < MAX77696_EPD_NR_REGS; i++) {
		rc = max77696_epd_regulator_device_init(me, i, &pdata->init_data[i]);
		if (unlikely(rc)) {
			dev_err(me->dev, "Platform init() failed: %d\n", rc);
			goto out_err4;
		}
	}
	
	rc = max77696_vreg_reg_read(me, EPDOKINTM, &buf);
	if (rc) {
		dev_err(me->dev, "EPD OK Interrupt Mask Read Error [%d]\n", rc);
		goto out_err4;
	} 
	rc = max77696_vreg_reg_write(me, EPDOKINTM, (buf | VREG_EPDOKINT_EPDPN_M | VREG_EPDOKINT_EPDPOK_M));
	if (unlikely(rc)) {
		dev_err(me->dev,"EPD OK Interrupt Mask Write Error [%d]\n",rc);
		goto out_err4;
	}

	/* Request epd interrupt */
	rc = request_threaded_irq(me->irq, NULL, max77696_epd_isr,
		(IRQF_TRIGGER_RISING | IRQF_DISABLED), DRIVER_NAME, me);

	if(unlikely(rc)) {
		dev_err(me->dev, "Failed to register threaded irq with irq:%d (%d)\n",
			me->irq, rc);
		goto out_err4;
	}

	/* Request epd pok interrupt */
	rc = request_threaded_irq(me->pwrgood_irq, NULL, max77696_epd_pok_isr,
		(IRQF_TRIGGER_RISING | IRQF_ONESHOT), DRIVER_NAME, me);

	if(unlikely(rc)) {
		dev_err(me->dev, "Failed to register threaded irq with irq:%d (%d)\n",
			me->pwrgood_irq, rc);
		goto out_err5;
	}

	/* register and subscribe to events */
	rc = max77696_eventhandler_register(&max77696_epdpok_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "Failed to register event[%d] handle with err [%d]\n", \
							max77696_epdpok_handle.event_id, rc);
		goto out_err6;
	}

	/* register and subscribe to events */
	rc = max77696_eventhandler_register(&max77696_epdfault_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "Failed to register event[%d] handle with err [%d]\n", \
							max77696_epdfault_handle.event_id, rc);
		goto out_err7;
	}
	
	BUG_ON(chip->epd_ptr);
	chip->epd_ptr = me;

	pr_info("\nEPD vreg probe complete!\n");
	SUBDEVICE_SET_LOADED(epd, chip);
	return 0;

out_err7:
	max77696_eventhandler_unregister(&max77696_epdpok_handle);
out_err6:
	free_irq(me->pwrgood_irq, me);
out_err5:
	free_irq(me->irq, me);
out_err4:
	platform_driver_unregister(&epd_regulator_driver);
out_err3:
	remove_sysfs_interfaces(me->dev);
out_err2:
	gpio_free(me->pwrgood_gpio);
out_err1:
	regulator_put(me->display_3v2_regulator);
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);
	return rc;
}



#ifdef CONFIG_PM_SLEEP

static int max77696_epd_vreg_suspend(struct platform_device *pdev, pm_message_t state) {
	struct max77696_epd_data *me = platform_get_drvdata(pdev);
	regulator_disable(me->display_3v2_regulator);
	return 0;
}

static int max77696_epd_vreg_resume(struct platform_device *pdev) {
	struct max77696_epd_data *me = platform_get_drvdata(pdev);
	regulator_enable(me->display_3v2_regulator);
	return 0;
}
#endif

static int  __devexit max77696_epd_vreg_remove(struct platform_device *pdev)
{
	struct max77696_epd_data *me = platform_get_drvdata(pdev);

	platform_driver_unregister(&epd_regulator_driver);
	regulator_disable(me->display_3v2_regulator);
	regulator_put(me->display_3v2_regulator);
	free_irq(me->pwrgood_irq, me);
	free_irq(me->irq, me);
	gpio_free(me->pwrgood_gpio);
	max77696_eventhandler_unregister(&max77696_epdfault_handle);
	max77696_eventhandler_unregister(&max77696_epdpok_handle);
	remove_sysfs_interfaces(me->dev);
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);

	return 0;
}

static struct platform_driver max77696_epd_vreg_driver =
{
	.driver.name  = DRIVER_NAME,
	.driver.owner = THIS_MODULE,
	.probe        = max77696_epd_vreg_probe,
	.remove       = __devexit_p(max77696_epd_vreg_remove),
#ifdef CONFIG_PM_SLEEP
	.suspend      = max77696_epd_vreg_suspend,
	.resume       = max77696_epd_vreg_resume,
#endif
};

static int __init max77696_epd_vreg_init(void)
{
	pr_info("max77696> regulator init\n");

	return platform_driver_register(&max77696_epd_vreg_driver);
}
subsys_initcall(max77696_epd_vreg_init);

static void __exit max77696_epd_vreg_exit(void)
{
    platform_driver_unregister(&max77696_epd_vreg_driver);
}
module_exit(max77696_epd_vreg_exit);

MODULE_DESCRIPTION("MAX77696 E-Paper Display Power Supply Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
