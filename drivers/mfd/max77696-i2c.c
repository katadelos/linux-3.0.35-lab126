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
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/mfd/max77696.h>
#include <mach/boardid.h>
#ifdef CONFIG_FALCON
#include <max77696_registers.h>
extern int in_falcon(void);
#endif
#define DRIVER_DESC    "MAX77696 I2C Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

/* MAX77696 I2C Slave Addresses
 *   Real-Time Clock / Energy Harvester  W 0xD0  R 0xD1
 *   USB Interface Circuit               W 0x6A  R 0x6B
 *   Fuel Gauge                          W 0x68  R 0x69
 *   Power Management / GPIO/ other      W 0x78  R 0x79
 */
#define RTC_I2C_ADDR   (0xD0>>1)
#define UIC_I2C_ADDR   (0x6A>>1)
#define GAUGE_I2C_ADDR (0x68>>1)

#define LOG_SIZE 1000
typedef struct {
    unsigned long jiffies;
    u8 action;
    u8 slave_addr;
    u8 slave_reg;
    u8 value;
} i2c_log_entry;

#define LOG_WRITE 1
#define LOG_READ  2

static i2c_log_entry i2clog[LOG_SIZE];
static int log_end;

DEFINE_MUTEX(log_mutex);

void log_i2c(u8 action, u8 slave_addr, u8 slave_reg, u8 value) {
    i2c_log_entry *entry;

    mutex_lock(&log_mutex);
    entry = &(i2clog[log_end]);
    log_end = (log_end + 1) % LOG_SIZE;
    mutex_unlock(&log_mutex);

    entry -> jiffies = jiffies;
    entry -> slave_addr = slave_addr;
    entry -> slave_reg = slave_reg;
    entry -> value = value;
    entry -> action = action;
}

void dump_i2c_log(void) {
    int i;
    bool setstart = false;
    unsigned long startjiffies = 0;
    i2c_log_entry *entry;
    mutex_lock(&log_mutex);
    for (i=0;i<LOG_SIZE;i++) {
        entry = &(i2clog[(log_end+i)%LOG_SIZE]);
        if (entry -> action) {
            if (!setstart) {
                startjiffies=entry -> jiffies;
                setstart = true;
            }

            printk(KERN_ERR "I2C LOG: %010lu %02x:%02x %s %02x\n",
                entry -> jiffies - startjiffies,
                entry -> slave_addr,
                entry -> slave_reg,
                entry -> action == LOG_WRITE ? "WRITE" : "READ",
                entry -> value);
            entry -> action = 0;
        }
    }

    log_end = 0;

    mutex_unlock(&log_mutex);
}

extern int max77696_chip_init (struct max77696_chip *chip,
    struct max77696_platform_data* pdata);
extern void max77696_chip_exit (struct max77696_chip *chip);

/* for internal reference */
struct max77696_chip* max77696;

/* Reading from Sequential Registers */
static int max77696_i2c_seq_read (struct max77696_i2c *me,
    u8 addr, u8 *dst, u16 len)
{
    struct i2c_client *client = me->client;
    struct i2c_adapter *adap = client->adapter;
    struct i2c_msg msg[2];
    int rc;
    int i;

    msg[0].addr   = client->addr;
    msg[0].flags  = client->flags & I2C_M_TEN;
    msg[0].len    = 1;
    msg[0].buf    = (char*)(&addr);

    msg[1].addr   = client->addr;
    msg[1].flags  = client->flags & I2C_M_TEN;
    msg[1].flags |= I2C_M_RD;
    msg[1].len    = len;
    msg[1].buf    = (char*)dst;

    rc = i2c_transfer(adap, msg, 2);

    for (i=0;i<len;i++) {
        log_i2c(LOG_READ, client->addr, addr+i, dst[i]);
    }

    /* If everything went ok (i.e. 2 msg transmitted), return 0,
       else error code. */
    return (rc == 2) ? 0 : rc;
}

/* Writing to Sequential Registers */
static int max77696_i2c_seq_write (struct max77696_i2c *me,
    u8 addr, const u8 *src, u16 len)
{
    struct i2c_client *client = me->client;
    struct i2c_adapter *adap = client->adapter;
    struct i2c_msg msg[1];
    u8 buf[len + 1];
    int rc;
    int i;

    buf[0] = addr;
    memcpy(&buf[1], src, len);

    msg[0].addr  = client->addr;
    msg[0].flags = client->flags & I2C_M_TEN;
    msg[0].len   = len + 1;
    msg[0].buf   = (char*)buf;

    rc = i2c_transfer(adap, msg, 1);

    for (i=0;i<len;i++) {
        log_i2c(LOG_WRITE, client->addr, addr+i, src[i]);
    }

    /* If everything went ok (i.e. 1 msg transmitted), return 0,
       else error code. */
    return (rc == 1) ? 0 : rc;
}

/* Reading from a Single Register */
static int max77696_i2c_single_read (struct max77696_i2c *me, u8 addr, u8 *val)
{
    return max77696_i2c_seq_read(me, addr, val, 1);
}

/* Writing to a Single Register */
static int max77696_i2c_single_write (struct max77696_i2c *me, u8 addr, u8 val)
{
    struct i2c_client *client = me->client;
    struct i2c_adapter *adap = client->adapter;
    struct i2c_msg msg[1];
    u8 buf[2];
    int rc;

    buf[0] = (char)addr;
    buf[1] = (char)val;

    msg[0].addr  = client->addr;
    msg[0].flags = client->flags & I2C_M_TEN;
    msg[0].len   = 2;
    msg[0].buf   = (char*)buf;

    log_i2c(LOG_WRITE, client->addr, addr, val);
    rc = i2c_transfer(adap, msg, 1);

    /* If everything went ok (i.e. 1 msg transmitted), return 0,
       else error code. */
    return (rc == 1) ? 0 : rc;
}

static u8 max77696_pmic_regaddr = 0;

static ssize_t max77696_pmic_i2clog_store (struct device *dev,
				struct device_attribute *devattr, const char *buf, size_t count)
{
    dump_i2c_log();
	return count;
}
static DEVICE_ATTR(max77696_pmic_i2clog, S_IWUSR, NULL, max77696_pmic_i2clog_store);

static ssize_t max77696_pmic_regoff_show (struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "0x%x\n", max77696_pmic_regaddr);
	return (ssize_t)rc;
}

static ssize_t max77696_pmic_regoff_store (struct device *dev,
				struct device_attribute *devattr, const char *buf, size_t count)
{
	unsigned int val = 0;
	if (sscanf(buf, "%x", &val) <= 0) {
		printk(KERN_ERR "Could not store MAX77696_UIC register offset\n");
		return -EINVAL;
	}
	max77696_pmic_regaddr = (u8)val;
	return (ssize_t)count;
}
static DEVICE_ATTR(max77696_pmic_regoff, S_IWUSR|S_IRUGO, max77696_pmic_regoff_show, max77696_pmic_regoff_store);

static ssize_t max77696_pmic_regval_show (struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct max77696_chip *chip = max77696;
	u8 val = 0;
	int rc;
	max77696_i2c_single_read(&(chip->pmic_i2c), max77696_pmic_regaddr, &val);
	rc = (int)sprintf(buf, "MAX77696_UIC REG_ADDR=0x%x : REG_VAL=0x%x\n", max77696_pmic_regaddr,val);
	return (ssize_t)rc;
}

static ssize_t max77696_pmic_regval_store (struct device *dev,
				struct device_attribute *devattr, const char *buf, size_t count)
{
	struct max77696_chip *chip = max77696;
	unsigned int val = 0;
	if (sscanf(buf, "%x", &val) <= 0) {
		printk(KERN_ERR "Could not store MAX77696_UIC register value\n");
		return -EINVAL;
	}
	max77696_i2c_single_write(&(chip->pmic_i2c), max77696_pmic_regaddr,(u8)val);
	return (ssize_t)count;
}
static DEVICE_ATTR(max77696_pmic_regval, S_IWUSR|S_IRUGO, max77696_pmic_regval_show, max77696_pmic_regval_store);

static struct attribute *max77696_pmic_attr[] = {
	&dev_attr_max77696_pmic_regoff.attr,
	&dev_attr_max77696_pmic_regval.attr,
	&dev_attr_max77696_pmic_i2clog.attr,
	NULL
};  
  
static const struct attribute_group max77696_pmic_attr_group = {
	.attrs = max77696_pmic_attr,
};

static struct i2c_board_info max77696_rtc_i2c_board_info = {
    I2C_BOARD_INFO(MAX77696_RTC_NAME, RTC_I2C_ADDR),
};

static struct i2c_board_info max77696_uic_i2c_board_info = {
    I2C_BOARD_INFO(MAX77696_UIC_NAME, UIC_I2C_ADDR),
};

static struct i2c_board_info max77696_gauge_i2c_board_info = {
    I2C_BOARD_INFO(MAX77696_GAUGE_NAME, GAUGE_I2C_ADDR),
};

static __devinit int max77696_i2c_probe (struct i2c_client *client,
    const struct i2c_device_id *id)
{
    struct max77696_platform_data* pdata = client->dev.platform_data;
    struct max77696_chip *chip;
    int rc;

    if (unlikely(!pdata)) {
        dev_err(&(client->dev), "platform data is missing\n");
        return -EINVAL;
    }

    chip = kzalloc(sizeof(*chip), GFP_KERNEL);
    if (unlikely(!chip)) {
        dev_err(&(client->dev),
            "out of memory (%uB requested)\n", sizeof(*chip));
        return -ENOMEM;
    }

    max77696 = chip;

    dev_set_drvdata(&(client->dev), chip);
    chip->dev  = &(client->dev);
    chip->kobj = &(client->dev.kobj);

    chip->core_irq = pdata->core_irq;
    chip->irq_base = pdata->irq_base;

    chip->pmic_i2c.client = client;
    i2c_set_clientdata(chip->pmic_i2c.client, chip);

    chip->pmic_i2c.read       = max77696_i2c_single_read;
    chip->pmic_i2c.write      = max77696_i2c_single_write;
    chip->pmic_i2c.bulk_read  = max77696_i2c_seq_read;
    chip->pmic_i2c.bulk_write = max77696_i2c_seq_write;

    chip->rtc_i2c.client = i2c_new_device(client->adapter,
        &max77696_rtc_i2c_board_info);
    if (unlikely(!chip->rtc_i2c.client)) {
        dev_err(chip->dev, "failed to create rtc i2c device\n");
        rc = -EIO;
        goto out_err;
    }

    i2c_set_clientdata(chip->rtc_i2c.client, chip);

    chip->rtc_i2c.read       = max77696_i2c_single_read;
    chip->rtc_i2c.write      = max77696_i2c_single_write;
    chip->rtc_i2c.bulk_read  = max77696_i2c_seq_read;
    chip->rtc_i2c.bulk_write = max77696_i2c_seq_write;

    chip->uic_i2c.client = i2c_new_device(client->adapter,
        &max77696_uic_i2c_board_info);
    if (unlikely(!chip->uic_i2c.client)) {
        dev_err(chip->dev, "failed to create uic i2c device\n");
        rc = -EIO;
        goto out_err;
    }

    i2c_set_clientdata(chip->uic_i2c.client, chip);

    chip->uic_i2c.read       = max77696_i2c_single_read;
    chip->uic_i2c.write      = max77696_i2c_single_write;
    chip->uic_i2c.bulk_read  = max77696_i2c_seq_read;
    chip->uic_i2c.bulk_write = max77696_i2c_seq_write;

    chip->gauge_i2c.client = i2c_new_device(client->adapter,
        &max77696_gauge_i2c_board_info);
    if (unlikely(!chip->gauge_i2c.client)) {
        dev_err(chip->dev, "failed to create gauge i2c device\n");
        rc = -EIO;
        goto out_err;
    }

    i2c_set_clientdata(chip->gauge_i2c.client, chip);

    chip->gauge_i2c.read       = max77696_i2c_single_read;
    chip->gauge_i2c.write      = max77696_i2c_single_write;
    chip->gauge_i2c.bulk_read  = max77696_i2c_seq_read;
    chip->gauge_i2c.bulk_write = max77696_i2c_seq_write;

    device_set_wakeup_capable(chip->dev, 1);

    rc = max77696_chip_init(chip, pdata);
    if (unlikely(rc)) {
        goto out_err;
    }

    rc = sysfs_create_group(&(chip->pmic_i2c.client->dev.kobj), &max77696_pmic_attr_group);
    if (unlikely(rc)) {
		dev_err(chip->dev, "failed to create attribute group [%d]\n", rc);
        goto out_err;
    }
    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    return 0;

out_err:
    max77696 = NULL;
    i2c_set_clientdata(client, NULL);

    if (likely(chip->gauge_i2c.client)) {
        i2c_unregister_device(chip->gauge_i2c.client);
    }
    if (likely(chip->uic_i2c.client)) {
        i2c_unregister_device(chip->uic_i2c.client);
    }
    if (likely(chip->rtc_i2c.client)) {
        i2c_unregister_device(chip->rtc_i2c.client);
    }

    kfree(chip);

    return rc;
}

static __devexit int max77696_i2c_remove (struct i2c_client *client)
{
    struct max77696_chip *chip;

    chip = i2c_get_clientdata(client);
    BUG_ON(chip != max77696);

    sysfs_remove_group(&(chip->pmic_i2c.client->dev.kobj), &max77696_pmic_attr_group);

    max77696 = NULL;
    i2c_set_clientdata(client, NULL);

    if (likely(chip)) {
        max77696_chip_exit(chip);

        if (likely(chip->gauge_i2c.client)) {
            i2c_unregister_device(chip->gauge_i2c.client);
        }

        if (likely(chip->uic_i2c.client)) {
            i2c_unregister_device(chip->uic_i2c.client);
        }

        if (likely(chip->rtc_i2c.client)) {
            i2c_unregister_device(chip->rtc_i2c.client);
        }

        kfree(chip);
    }

    return 0;
}

#ifdef CONFIG_PM_SLEEP

#ifdef CONFIG_FALCON
/* During hibernate if we can't save/restore a register we panic instead of letting the device continue in an
 * inconsistent state. Since we are going to panic on failure, we might as well allow a generous number of retries on
 * each register operation.
 */
#define REGISTER_RETRY 30

/*
 * This list represents all of the registers in the main PMIC block that should
 * be restored after a hibernate. This is every register that is not a read
 * only status, interrupt status, register lockout control, watchdog control,
 * or shutdown control.
 *
 * JSEVEN-4174: Don't restore LED brightness regs (0x6c-0x6e).
 * WS-1306: Don't restore LDO7 since already done in uboot 
 */
static u8 pmic_regs_to_save[] = {
0x01, 0x02, 0x03, 0x06, 0x09, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
0x13, 0x14, 0x16, 0x18, 0x1a, 0x1b, 0x1d, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2f,
0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c,
0x3d, 0x3e, 0x3f, 0x41, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b,
0x4c, 0x4d, 0x4e, /* 0x4f,*/ 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x59, 0x5a,
0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
0x6b, /* 0x6c, 0x6d, 0x6e,*/ 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84,
0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91,
0x92, 0x93, 0x94, 0x95, 0x9a, 0x9b, 0xa3, 0xa5, 0xaf, 0xb0, 0xb3, 0xb4, 0xb5
};


#define UIC_SAVE_START (0x5)
#define UIC_SAVE_END   (0xC)


#define CHGB_SAVE_START (0x36)
#define CHGB_SAVE_END   (0x3c)
#define CHGB_SAVE_EXTRA (0x3f)

static u8 rtc_registers_to_dump[] = {
    RTC_RTCINTM_REG,
    RTC_RTCCNTLM_REG,
    RTC_RTCCNTL_REG,
    RTC_RTCUPDATE0_REG,
    RTC_RTCSMPL_REG,
    RTC_RTCAE1_REG,
    RTC_RTCSECA1_REG,
    RTC_RTCMINA1_REG,
    RTC_RTCHOURA1_REG,
    RTC_RTCDOWA1_REG,
    RTC_RTCMONTHA1_REG,
    RTC_RTCYEARA1_REG,
    RTC_RTCDOMA1_REG,
    RTC_RTCAE2_REG,
    RTC_RTCSECA2_REG,
    RTC_RTCMINA2_REG,
    RTC_RTCHOURA2_REG,
    RTC_RTCDOWA2_REG,
    RTC_RTCMONTHA2_REG,
    RTC_RTCYEARA2_REG,
    RTC_RTCDOMA2_REG,
};

// Read these separately.  These must be synced first, and are expected to change.
static u8 rtc_time_registers[] = {
    RTC_RTCSEC_REG,
    RTC_RTCMIN_REG,
    RTC_RTCHOUR_REG,
    RTC_RTCDOW_REG,
    RTC_RTCMONTH_REG,
    RTC_RTCYEAR_REG,
    RTC_RTCDOM_REG,
};

static u8 pmic_saved_vals[ARRAY_SIZE(pmic_regs_to_save)];

static u8 chgb_saved_vals[(CHGB_SAVE_END - UIC_SAVE_END + 1)];
static u8 chgb_saved_extra;

static u8 uic_saved_vals[(UIC_SAVE_END - UIC_SAVE_START + 1)];

static u8 rtc_reg_vals[ARRAY_SIZE(rtc_registers_to_dump)];

/*
 * These symbols (imx_i2c*) are defined here so that the kernel can compile as
 * a self-contained unit.
 * They will be assigned to a valid pointer by snapshot driver.
 * BE VERY CAREFUL WITH THESE.  They are not synchronized and cannot be used
 * outside of hibernate.
 */
int dummy_read_func (u8 slave_addr, u8 slave_register, u8 *outbuf, int count)
{
	panic("imx_i2c_read_bytes called without being initialized");
}
int dummy_write_func (u8 slave_addr, u8 slave_register, u8 value)
{
	panic("imx_i2c_write_single called without being initialized");
}
int (*imx_i2c_read_bytes)(u8 slave_addr, u8 slave_register, u8 *outbuf, int count) = dummy_read_func;
EXPORT_SYMBOL(imx_i2c_read_bytes);
int (*imx_i2c_write_single)(u8 slave_addr, u8 slave_register, u8 value) = dummy_write_func;
EXPORT_SYMBOL(imx_i2c_write_single);

static void max77696_i2c_retry_read_register(struct max77696_i2c *i2c, u8 reg, u8 *buffer, const char *error_src) {
    int retries, rc;

    for (retries = 0; retries < REGISTER_RETRY; retries++) {
        if ((rc = imx_i2c_read_bytes(i2c->client->addr, reg, buffer, 1))) {
            *buffer = 0;
            printk(KERN_CRIT "%s: Failed to read value for register %02hhx (%d)\n",
                error_src, reg, rc);
        } else {
            return;
        }
    }

    panic("%s: Unable to read value for PMIC register\n", error_src);
}

static void max77696_i2c_retry_write_register(struct max77696_i2c *i2c, u8 reg, u8 value, const char *error_src) {
    int retries, rc;

    for (retries = 0; retries < REGISTER_RETRY; retries++) {
        if ((rc = imx_i2c_write_single(i2c->client->addr, reg, value))) {
            printk(KERN_CRIT "%s: Failed to write value to register [%02hhx]=%02hhx (%d)\n",
                error_src, reg, value, rc);
        } else {
            return;
        }
    }

    panic("%s: Unable to write value to PMIC register\n", error_src);
}

static void max77696_i2c_retry_read_register16(struct max77696_i2c *i2c, u8 reg, u16 *buffer, const char *error_src) {
    int retries, rc;

    for (retries = 0; retries < REGISTER_RETRY; retries++) {
        if ((rc = imx_i2c_read_bytes(i2c->client->addr, reg, (u8*)buffer, 2))) {
            *buffer = 0;
            printk(KERN_CRIT "%s: Failed to read value for register %02hhx (%d)\n",
                error_src, reg, rc);
        } else {
            return;
        }
    }

    panic("%s: Unable to read value for PMIC register\n", error_src);
}

static void save_pmic_block(struct max77696_chip *chip)
{
    int i;

    for (i=0; i<ARRAY_SIZE(pmic_regs_to_save); i++) {
        max77696_i2c_retry_read_register(&(chip->pmic_i2c), pmic_regs_to_save[i], pmic_saved_vals + i, __func__);
    }
}

static void restore_pmic_block(struct max77696_chip *chip)
{
    int i;

    // Unlock the charger registers
    max77696_i2c_retry_write_register(&(chip->pmic_i2c), CHGA_CHG_CNFG_06_REG, CHGA_CHG_CNFG_06_CHGPROT_M, __func__);

    for (i=0; i<ARRAY_SIZE(pmic_regs_to_save); i++) {
        max77696_i2c_retry_write_register(&(chip->pmic_i2c), pmic_regs_to_save[i], pmic_saved_vals[i], __func__);
    }

    // Lock the charger registers
    max77696_i2c_retry_write_register(&(chip->pmic_i2c), CHGA_CHG_CNFG_06_REG, 0, __func__);
}

static void save_uic_block(struct max77696_chip *chip)
{
    int i;

    for (i=UIC_SAVE_START; i<= UIC_SAVE_END; i++) {
        max77696_i2c_retry_read_register(&(chip->uic_i2c), (u8)i, uic_saved_vals +(i-UIC_SAVE_START), __func__);
    }
}

static void restore_uic_block(struct max77696_chip *chip)
{
    int i;

    for (i=UIC_SAVE_START; i<= UIC_SAVE_END; i++) {
        max77696_i2c_retry_write_register(&(chip->uic_i2c), (u8)i, uic_saved_vals[i-UIC_SAVE_START], __func__);
    }
}

// Refer to datasheet
static void max77696_rtc_sync_read_buffer (struct max77696_chip *chip)
{
    u8 rtcupdate0;

    max77696_i2c_retry_read_register(&(chip->rtc_i2c), RTC_RTCUPDATE0_REG, &rtcupdate0, __func__);
    rtcupdate0 |= RTC_RTCUPDATE0_RBUDR_M;
    max77696_i2c_retry_write_register(&(chip->rtc_i2c), RTC_RTCUPDATE0_REG, rtcupdate0, __func__);

    udelay(200);
}

static void dump_rtc_regs (struct max77696_chip *chip)
{
    int i;
    u8 regval;

    max77696_rtc_sync_read_buffer(chip);

    for (i=0; i<ARRAY_SIZE(rtc_registers_to_dump); i++) {
        max77696_i2c_retry_read_register(&(chip->rtc_i2c), rtc_registers_to_dump[i], &regval, __func__);
        rtc_reg_vals[i] = regval;
	printk("%x:%x%s, ", rtc_registers_to_dump[i], rtc_reg_vals[i],
			regval == rtc_reg_vals[i] ? "" : "*" );
    }
    printk("\n");

    for (i=0; i<ARRAY_SIZE(rtc_time_registers); i++) {
        max77696_i2c_retry_read_register(&(chip->rtc_i2c), rtc_time_registers[i], &regval, __func__);
	printk("%x:%x, ", rtc_time_registers[i], regval);
    }
    printk("\n");
}

static void save_chgb_block(struct max77696_chip *chip)
{
    int i;

    for (i=CHGB_SAVE_START; i<= CHGB_SAVE_END; i++) {
        // Charger B is on the same i2c slave address as the RTC, so the
        // struct max77696_chip uses the rtc i2c client for both.
        max77696_i2c_retry_read_register(&(chip->rtc_i2c), (u8)i, chgb_saved_vals + (i-CHGB_SAVE_START), __func__);
    }

    max77696_i2c_retry_read_register(&(chip->rtc_i2c), (u8)CHGB_SAVE_EXTRA, &chgb_saved_extra, __func__);

    printk(KERN_INFO "Dumping RTC regs before hibernate...");
    dump_rtc_regs(chip);
}

static void restore_chgb_block(struct max77696_chip *chip)
{
    int i;

    for (i=CHGB_SAVE_START; i<= CHGB_SAVE_END; i++) {
        max77696_i2c_retry_write_register(&(chip->rtc_i2c), (u8)i, chgb_saved_vals[i-CHGB_SAVE_START], __func__);
    }

    max77696_i2c_retry_write_register(&(chip->rtc_i2c), CHGB_SAVE_EXTRA, chgb_saved_extra, __func__);
 
    printk(KERN_INFO "Dumping RTC regs during resume...");
    dump_rtc_regs(chip);
}

void max77696_save_internal_state (void)
{
	if (in_falcon()) {
		/* There isn't too much interaction between the modules, so this is done in no particular order */
		save_pmic_block(max77696);
		save_uic_block(max77696);
		save_chgb_block(max77696);
	}
}
EXPORT_SYMBOL(max77696_save_internal_state);

void max77696_restore_internal_state (void)
{
	if (in_falcon()) {
		u16 status;
		max77696_i2c_retry_read_register16(&(max77696->gauge_i2c), (u8)FG_STATUS_REG, &status, __func__);

		/* check for POR bit before we restore PMIC regs, FG will be restored 
		 * correctly in battery resume */
		if (status & FG_STATUS_POR) {
			printk(KERN_ERR "POR bit set during hibernate!");
		}

		restore_pmic_block(max77696);
		restore_uic_block(max77696);
		restore_chgb_block(max77696);
	}
}
EXPORT_SYMBOL(max77696_restore_internal_state);
#endif

static int max77696_i2c_suspend (struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max77696_chip *chip = i2c_get_clientdata(client);

#ifdef CONFIG_FALCON
    if(in_falcon()) {
		/* Note: WS-1212 Enable LDO bandgap bias when entering FSHDN (SW workaround - option 1b) */
		if (!lab126_board_rev_greater_eq(BOARD_ID_WHISKY_WAN_DVT1_1_REV_C) &&
			!lab126_board_rev_greater_eq(BOARD_ID_WHISKY_WFO_DVT1_1_REV_C)) {
			max77696_ldo_set_bias_enable(true);
		}
    }
#endif

	disable_irq(chip->core_irq);
	enable_irq_wake(chip->core_irq);
	return 0;
}

static int max77696_i2c_resume (struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max77696_chip *chip = i2c_get_clientdata(client);

	disable_irq_wake(chip->core_irq);
	enable_irq(chip->core_irq);

#ifdef CONFIG_FALCON
    if(in_falcon()) {
		/* Note: WS-1212 Disable LDO bandgap bias when exiting FSHDN (SW workaround - option 1b) */
		if (!lab126_board_rev_greater_eq(BOARD_ID_WHISKY_WAN_DVT1_1_REV_C) &&
			!lab126_board_rev_greater_eq(BOARD_ID_WHISKY_WFO_DVT1_1_REV_C)) {
			max77696_ldo_set_bias_enable(false);
	    }
	}
#endif
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(max77696_i2c_pm,
    max77696_i2c_suspend, max77696_i2c_resume);

static const struct i2c_device_id max77696_i2c_ids[] = {
    { DRIVER_NAME, 0 },
    { },
};
MODULE_DEVICE_TABLE((me)->i2c, max77696_i2c_ids);

static struct i2c_driver max77696_i2c_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .driver.pm    = &max77696_i2c_pm,
    .probe        = max77696_i2c_probe,
    .remove       = __devexit_p(max77696_i2c_remove),
    .id_table     = max77696_i2c_ids,
};

static __init int max77696_i2c_driver_init (void)
{
    return i2c_add_driver(&max77696_i2c_driver);
}

static __exit void max77696_i2c_driver_exit (void)
{
    i2c_del_driver(&max77696_i2c_driver);
}

arch_initcall(max77696_i2c_driver_init);
module_exit(max77696_i2c_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

