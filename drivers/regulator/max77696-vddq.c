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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/max77696.h>
#include <max77696_registers.h>

#define DRIVER_DESC    "MAX77696 LPDDR2 Termination Supply Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_VDDQ_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".1"

#ifdef VERBOSE
#define dev_verbose(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_verbose(args...) do { } while (0)
#endif /* VERBOSE */

#define VDDQ_VREG_NAME                 MAX77696_VDDQ_NAME"-vreg"
#define VDDQ_VREG_DESC_NAME(_name)     MAX77696_NAME"-vreg-"_name

#define mV_to_uV(mV)                   (mV * 1000)
#define uV_to_mV(uV)                   (uV / 1000)
#define V_to_uV(V)                     (mV_to_uV(V * 1000))
#define uV_to_V(uV)                    (uV_to_mV(uV) / 1000)

struct max77696_vddq_vreg_desc {
    struct regulator_desc rdesc;
};

struct max77696_vddq_vreg {
    struct max77696_vddq_vreg_desc *desc;
    struct regulator_dev           *rdev;
};

struct max77696_vddq {
    struct mutex                    lock;
    struct max77696_chip           *chip;
    struct max77696_i2c            *i2c;
    struct device                  *dev;

    int                             half_vddq_in_uV;
    struct max77696_vddq_vreg       vreg;
};

#define __get_i2c(chip)                (&((chip)->pmic_i2c))
#define __lock(me)                     mutex_lock(&((me)->lock))
#define __unlock(me)                   mutex_unlock(&((me)->lock))

/* VDDQ Register Read/Write */
#define max77696_vddq_reg_read(me, reg, val_ptr) \
        max77696_read((me)->i2c, VDDQ_REG(reg), val_ptr)
#define max77696_vddq_reg_write(me, reg, val) \
        max77696_write((me)->i2c, VDDQ_REG(reg), val)
#define max77696_vddq_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, VDDQ_REG(reg), dst, len)
#define max77696_vddq_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, VDDQ_REG(reg), src, len)
#define max77696_vddq_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, VDDQ_REG(reg), mask, val_ptr)
#define max77696_vddq_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, VDDQ_REG(reg), mask, val)

/* VDDQ Register Single Bit Ops */
#define max77696_vddq_reg_get_bit(me, reg, bit, val_ptr) \
        ({\
            int __rc = max77696_vddq_reg_read_masked(me, reg,\
                VDDQ_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = VDDQ_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_vddq_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_vddq_reg_write_masked(me, reg,\
                VDDQ_REG_BITMASK(reg, bit), VDDQ_REG_BITSET(reg, bit, val));\
        })

static int max77696_vddq_set_margin (struct max77696_vddq *me, int margin_uV)
{
    u8 adj;
    int adj_rate, rc;

    adj_rate = DIV_ROUND_UP(margin_uV * 100, me->half_vddq_in_uV);
    adj_rate = min(64, max(-60, adj_rate));

    if (adj_rate > 0) {
        adj = (( adj_rate - 1) >> 2) | 0x10;
    } else {
        adj = ((-adj_rate + 3) >> 2) | 0x00;
    }

    rc = max77696_vddq_reg_set_bit(me, VDDQSET, ADJ, adj);
    if (unlikely(rc)) {
        dev_err(me->dev, "VDDQSET write error [%d]\n", rc);
        goto out;
    }

    dev_verbose(me->dev, "set VDDQOUT margin %duV --> 0x%02x\n",
        margin_uV, adj);
    dev_dbg(me->dev, "VDDQOUT = %d%% x VDDQIN\n",
        DIV_ROUND_UP(100 + adj_rate, 2));

out:
    return rc;
}

static int max77696_vddq_get_margin (struct max77696_vddq *me)
{
    u8 adj;
    int margin_uV = 0, adj_rate, rc;

    rc = max77696_vddq_reg_get_bit(me, VDDQSET, ADJ, &adj);
    if (unlikely(rc)) {
        dev_err(me->dev, "VDDQSET read error [%d]\n", rc);
        goto out;
    }

    if (adj & 0x10) {
        adj_rate =  ((((int)adj & 0xf) + 1) << 2);
    } else {
        adj_rate = -((((int)adj & 0xf) + 0) << 2);
    }

    margin_uV = DIV_ROUND_UP(me->half_vddq_in_uV * adj_rate, 100);

    dev_verbose(me->dev, "get VDDQOUT margin %duV <-- 0x%02x\n",
        margin_uV, adj);
    dev_dbg(me->dev, "VDDQOUT = %d%% x VDDQIN\n",
        DIV_ROUND_UP(100 + adj_rate, 2));

out:
    return margin_uV;
}

static int max77696_vddq_vreg_set_voltage (struct regulator_dev *rdev,
    int min_uV, int max_uV, unsigned* selector)
{
    struct max77696_vddq *me = rdev_get_drvdata(rdev);
    int rc;

    __lock(me);
    rc = max77696_vddq_set_margin(me, min_uV - me->half_vddq_in_uV);
    __unlock(me);

    return rc;
}

static int max77696_vddq_vreg_get_voltage (struct regulator_dev *rdev)
{
    struct max77696_vddq *me = rdev_get_drvdata(rdev);
    int rc;

    __lock(me);
    rc = max77696_vddq_get_margin(me) + me->half_vddq_in_uV;
    __unlock(me);

    return rc;
}

static struct regulator_ops max77696_vddq_vreg_ops = {
    .set_voltage = max77696_vddq_vreg_set_voltage,
    .get_voltage = max77696_vddq_vreg_get_voltage,

    /* enable/disable regulator */
//  .enable      = ?,
//  .disable     = ?,
//  .is_enabled  = ?,

    /* get/set regulator operating mode (defined in regulator.h) */
//  .set_mode    = ?,
//  .get_mode    = ?,
};

/* VDDQ VREG Descriptor */
static struct max77696_vddq_vreg_desc max77696_vddq_vreg_desc = {
    .rdesc.name  = VDDQ_VREG_DESC_NAME("vddq"),
    .rdesc.ops   = &max77696_vddq_vreg_ops,
    .rdesc.type  = REGULATOR_VOLTAGE,
    .rdesc.owner = THIS_MODULE,
};

static __devinit int max77696_vddq_probe (struct platform_device *pdev)
{
    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_vddq_platform_data *pdata = dev_get_platdata(&(pdev->dev));
    struct max77696_vddq_vreg_desc *desc = &max77696_vddq_vreg_desc;
    struct regulator_init_data *init_data = &(pdata->init_data);
    struct regulator_dev *rdev;
    struct max77696_vddq *me;
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

    mutex_init(&(me->lock));
    me->chip = chip;
    me->i2c  = __get_i2c(chip);
    me->dev  = &(pdev->dev);

    /* Save half_vddq_in_uV */
    me->half_vddq_in_uV = (pdata->vddq_in_uV >> 1);

    me->vreg.desc = desc;

    /* Register regulator */
    rdev = regulator_register(&(desc->rdesc), me->dev, init_data, me);
    if (unlikely(IS_ERR(rdev))) {
        rc = PTR_ERR(rdev);
        rdev = NULL;

        dev_err(me->dev, "failed to register regulator for %s [%d]\n",
            desc->rdesc.name, rc);
        goto out_err;
    }

    me->vreg.rdev = rdev;
    platform_set_drvdata(pdev, rdev);

    dev_dbg(me->dev, "VDDQ  IN %duV  OUT %duV\n",
        pdata->vddq_in_uV,
        pdata->vddq_in_uV / 2 + max77696_vddq_get_margin(me));

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(vddq, chip);
    return 0;

out_err:
    if (likely(rdev)) {
        regulator_unregister(rdev);
        me->vreg.rdev = NULL;
    }
    mutex_destroy(&(me->lock));
    platform_set_drvdata(pdev, NULL);
    kfree(me);
    return rc;
}

static __devexit int max77696_vddq_remove (struct platform_device *pdev)
{
    struct regulator_dev *rdev = platform_get_drvdata(pdev);
    struct max77696_vddq *me = rdev_get_drvdata(rdev);

    regulator_unregister(rdev);

    mutex_destroy(&(me->lock));
    platform_set_drvdata(pdev, NULL);
    kfree(me);

    return 0;
}

static struct platform_driver max77696_vddq_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .probe        = max77696_vddq_probe,
    .remove       = __devexit_p(max77696_vddq_remove),
};

static __init int max77696_vddq_driver_init (void)
{
    return platform_driver_register(&max77696_vddq_driver);
}

static __exit void max77696_vddq_driver_exit (void)
{
    platform_driver_unregister(&max77696_vddq_driver);
}

subsys_initcall(max77696_vddq_driver_init);
module_exit(max77696_vddq_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

