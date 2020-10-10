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
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>

#include <linux/mfd/max77696.h>
#include <max77696_registers.h>

#define DRIVER_DESC    "MAX77696 32kHz Oscillator Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_32K_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

struct max77696_32k {
	struct max77696_chip *chip;
	struct max77696_i2c  *i2c;
	struct device        *dev;
	struct kobject       *kobj;
	struct mutex          lock;
};

#define __get_i2c(chip)                (&((chip)->pmic_i2c))
#define __lock(me)                     mutex_lock(&((me)->lock))
#define __unlock(me)                   mutex_unlock(&((me)->lock))

/* OSC Register Read/Write */
#define max77696_osc_reg_read(me, reg, val_ptr) \
        max77696_read((me)->i2c, OSC_REG(reg), val_ptr)
#define max77696_osc_reg_write(me, reg, val) \
        max77696_write((me)->i2c, OSC_REG(reg), val)
#define max77696_osc_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, OSC_REG(reg), dst, len)
#define max77696_osc_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, OSC_REG(reg), src, len)
#define max77696_osc_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, OSC_REG(reg), mask, val_ptr)
#define max77696_osc_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, OSC_REG(reg), mask, val)

/* OSC Register Single Bit Ops */
#define max77696_osc_reg_get_bit(me, reg, bit, val_ptr) \
        ({\
            int __rc = max77696_osc_reg_read_masked(me, reg,\
                OSC_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = OSC_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_osc_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_osc_reg_write_masked(me, reg,\
                OSC_REG_BITMASK(reg, bit), OSC_REG_BITSET(reg, bit, val));\
        })

#define MAX77696_32K_ATTR(name, mode, reg, bits) \
static ssize_t max77696_32k_##name##_show (struct device *dev,\
    struct device_attribute *devattr, char *buf)\
{\
    struct platform_device *pdev = to_platform_device(dev);\
    struct max77696_32k *me = platform_get_drvdata(pdev);\
    u8 val;\
    int rc;\
    __lock(me);\
    rc = max77696_osc_reg_get_bit(me, reg, bits, &val);\
    if (unlikely(rc)) {\
        dev_err(dev, ""#reg" read error %d\n", rc);\
        goto out;\
    }\
    rc = (int)snprintf(buf, PAGE_SIZE, "%u\n", val);\
out:\
    __unlock(me);\
    return (ssize_t)rc;\
}\
static ssize_t max77696_32k_##name##_store (struct device *dev,\
    struct device_attribute *devattr, const char *buf, size_t count)\
{\
    struct platform_device *pdev = to_platform_device(dev);\
    struct max77696_32k *me = platform_get_drvdata(pdev);\
    u8 val;\
    int rc;\
    __lock(me);\
    val = (u8)simple_strtoul(buf, NULL, 10);\
    rc = max77696_osc_reg_set_bit(me, reg, bits, val);\
    if (unlikely(rc)) {\
        dev_err(dev, ""#reg" write error %d\n", rc);\
        goto out;\
    }\
out:\
    __unlock(me);\
    return (ssize_t)count;\
}\
static DEVICE_ATTR(name, mode, max77696_32k_##name##_show,\
    max77696_32k_##name##_store)

MAX77696_32K_ATTR(32kload,  S_IRUGO|S_IWUSR, 32KLOAD, 32KLOAD);
MAX77696_32K_ATTR(32kstat,  S_IRUGO,         32KSTAT, 32KSTAT);
MAX77696_32K_ATTR(mode_set, S_IRUGO|S_IWUSR, 32KSTAT, MODE_SET);

static struct attribute *max77696_32k_attr[] = {
    &dev_attr_32kload.attr,
    &dev_attr_32kstat.attr,
    &dev_attr_mode_set.attr,
    NULL
};

static const struct attribute_group max77696_32k_attr_group = {
    .attrs = max77696_32k_attr,
};

static __devinit int max77696_32k_probe (struct platform_device *pdev)
{
    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_32k_platform_data *pdata = pdev->dev.platform_data;
    struct max77696_32k *me = NULL;
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

    rc = sysfs_create_group(me->kobj, &max77696_32k_attr_group);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to create attribute group %d\n", rc);
        goto out_err;
    }

#if 0
    /* Set platform data if doesn't match the default 
	 * To be used later if needed
	 */	
    max77696_osc_reg_set_bit(me, 32KLOAD, 32KLOAD,  pdata->load_cap);
    max77696_osc_reg_set_bit(me, 32KSTAT, MODE_SET, pdata->op_mode );
#endif

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(32k, chip);
    return 0;

out_err:
    platform_set_drvdata(pdev, NULL);
    mutex_destroy(&(me->lock));
    kfree(me);
    return rc;
}

static __devexit int max77696_32k_remove (struct platform_device *pdev)
{
    struct max77696_32k *me = platform_get_drvdata(pdev);

    sysfs_remove_group(&pdev->dev.kobj, &max77696_32k_attr_group);

    platform_set_drvdata(pdev, NULL);
    mutex_destroy(&(me->lock));
    kfree(me);

    return 0;
}

static struct platform_driver max77696_32k_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .probe        = max77696_32k_probe,
    .remove       = __devexit_p(max77696_32k_remove),
};

static __init int max77696_32k_driver_init (void)
{
    return platform_driver_register(&max77696_32k_driver);
}

static __exit void max77696_32k_driver_exit (void)
{
    platform_driver_unregister(&max77696_32k_driver);
}

module_init(max77696_32k_driver_init);
module_exit(max77696_32k_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

