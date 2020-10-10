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

#define DRIVER_DESC    "MAX77696 Load Switches Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_LSW_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#ifdef VERBOSE
#define dev_verbose(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_verbose(args...) do { } while (0)
#endif /* VERBOSE */

#define LSW_VREG_NAME                 MAX77696_LSW_NAME"-vreg"
#define LSW_VREG_DESC_NAME(_name)     MAX77696_NAME"-vreg-"_name
#define LSW_NREG                      MAX77696_LSW_NR_REGS

#define mV_to_uV(mV)                  (mV * 1000)
#define uV_to_mV(uV)                  (uV / 1000)
#define V_to_uV(V)                    (mV_to_uV(V * 1000))
#define uV_to_V(uV)                   (uV_to_mV(uV) / 1000)

struct max77696_lsw_vreg_desc {
    struct regulator_desc rdesc;
    u8                    cntrl_reg;
};

struct max77696_lsw_vreg {
    struct max77696_lsw_vreg_desc *desc;
    struct regulator_dev          *rdev;
    struct platform_device        *pdev;
};

struct max77696_lsw {
    struct mutex               lock;
    struct max77696_chip      *chip;
    struct max77696_i2c       *i2c;
    struct device             *dev;

    struct max77696_lsw_vreg   vreg[LSW_NREG];
};

#define __get_i2c(chip)                (&((chip)->pmic_i2c))
#define __lock(me)                     mutex_lock(&((me)->lock))
#define __unlock(me)                   mutex_unlock(&((me)->lock))

/* LSW Register Read/Write */
#define max77696_lsw_reg_read(me, reg, val_ptr) \
        max77696_read((me)->i2c, LSW_REG(reg), val_ptr)
#define max77696_lsw_reg_write(me, reg, val) \
        max77696_write((me)->i2c, LSW_REG(reg), val)
#define max77696_lsw_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, LSW_REG(reg), dst, len)
#define max77696_lsw_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, LSW_REG(reg), src, len)
#define max77696_lsw_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, LSW_REG(reg), mask, val_ptr)
#define max77696_lsw_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, LSW_REG(reg), mask, val)

/* LSW Register Single Bit Ops */
#define max77696_lsw_reg_get_bit(me, reg, bit, val_ptr) \
        ({\
            int __rc = max77696_lsw_reg_read_masked(me, reg,\
                LSW_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = LSW_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_lsw_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_lsw_reg_write_masked(me, reg,\
                LSW_REG_BITMASK(reg, bit), LSW_REG_BITSET(reg, bit, val));\
        })

#define __rdev_name(rdev_ptr) \
        (rdev_ptr->desc->name)
#define __rdev_to_max77696_lsw(rdev_ptr) \
        ((struct max77696_lsw*)rdev_get_drvdata(rdev_ptr))
#define __rdev_to_max77696_lsw_vreg(rdev_ptr) \
        (&(__rdev_to_max77696_lsw(rdev_ptr)->vreg[rdev_ptr->desc->id]))

static int max77696_lsw_vreg_enable (struct regulator_dev *rdev)
{
    struct max77696_lsw *me = __rdev_to_max77696_lsw(rdev);
    struct max77696_lsw_vreg *vreg = __rdev_to_max77696_lsw_vreg(rdev);
    struct max77696_lsw_vreg_desc *desc = vreg->desc;
    u8 addr, mask, val;
    int rc;

    __lock(me);

    addr = desc->cntrl_reg;
    mask = LSW_REG_BITMASK(SW_CNTRL, OUTLS);
    val  = 0xFF;

    rc = max77696_write_masked(me->i2c, addr, mask, val);
    if (unlikely(rc)) {
        dev_err(me->dev, "SW_CNTRL write error [%d]\n", rc);
    }

    __unlock(me);
    return rc;
}

static int max77696_lsw_vreg_disable (struct regulator_dev *rdev)
{
    struct max77696_lsw *me = __rdev_to_max77696_lsw(rdev);
    struct max77696_lsw_vreg *vreg = __rdev_to_max77696_lsw_vreg(rdev);
    struct max77696_lsw_vreg_desc *desc = vreg->desc;
    u8 addr, mask, val;
    int rc;

    __lock(me);

    addr = desc->cntrl_reg;
    mask = LSW_REG_BITMASK(SW_CNTRL, OUTLS);
    val  = 0x00;

    rc = max77696_write_masked(me->i2c, addr, mask, val);
    if (unlikely(rc)) {
        dev_err(me->dev, "SW_CNTRL write error [%d]\n", rc);
    }

    __unlock(me);
    return rc;
}

static int max77696_lsw_vreg_is_enabled (struct regulator_dev *rdev)
{
    struct max77696_lsw *me = __rdev_to_max77696_lsw(rdev);
    struct max77696_lsw_vreg *vreg = __rdev_to_max77696_lsw_vreg(rdev);
    struct max77696_lsw_vreg_desc *desc = vreg->desc;
    u8 addr, mask, val;
    int rc;

    __lock(me);

    addr = desc->cntrl_reg;
    mask = LSW_REG_BITMASK(SW_CNTRL, OUTLS);
    val  = 0x00;

    rc = max77696_read_masked(me->i2c, addr, mask, &val);
    if (unlikely(rc)) {
        dev_err(me->dev, "SW_CNTRL read error [%d]\n", rc);
    }

    __unlock(me);
    return !!val;
}

static struct regulator_ops max77696_lsw_vreg_ops = {
    .enable      = max77696_lsw_vreg_enable,
    .disable     = max77696_lsw_vreg_disable,
    .is_enabled  = max77696_lsw_vreg_is_enabled,
};

#define LSW_VREG_DESC(_id, _name, _cntrl_reg) \
        [MAX77696_LSW_ID_##_id] = {\
            .rdesc.name  = LSW_VREG_DESC_NAME(_name),\
            .rdesc.id    = MAX77696_LSW_ID_##_id,\
            .rdesc.ops   = &max77696_lsw_vreg_ops,\
            .rdesc.type  = REGULATOR_VOLTAGE,\
            .rdesc.owner = THIS_MODULE,\
            .cntrl_reg   = LSW_REG(_cntrl_reg),\
        }

#define VREG_DESC(id) (&(max77696_lsw_vreg_descs[id]))
static struct max77696_lsw_vreg_desc max77696_lsw_vreg_descs[LSW_NREG] = {
    LSW_VREG_DESC(LSW1, "lsw1", SW1_CNTRL),
    LSW_VREG_DESC(LSW2, "lsw2", SW2_CNTRL),
    LSW_VREG_DESC(LSW3, "lsw3", SW3_CNTRL),
    LSW_VREG_DESC(LSW4, "lsw4", SW4_CNTRL),
};

static int max77696_lsw_vreg_probe (struct platform_device *pdev)
{
    struct max77696_lsw *me = platform_get_drvdata(pdev);
    struct max77696_lsw_vreg *vreg = &(me->vreg[pdev->id]);
    struct max77696_lsw_vreg_desc *desc = VREG_DESC(pdev->id);
    struct max77696_lsw_platform_data *pdata = dev_get_platdata(&(pdev->dev));
    struct regulator_init_data *init_data = pdata->init_data;
    struct regulator_dev *rdev;
    int rc;
    u8 val, ade, addr, mask;

    /* Save my descriptor */
    vreg->desc = desc;

    /* Register my own regulator device */
    rdev = regulator_register(&(desc->rdesc), &(pdev->dev), init_data, me);
    if (unlikely(IS_ERR(rdev))) {
        rc = PTR_ERR(rdev);
        rdev = NULL;

        dev_err(&(pdev->dev), "failed to register regulator for %s [%d]\n",
            desc->rdesc.name, rc);
        goto out_err;
    }

    vreg->rdev = rdev;
    platform_set_drvdata(pdev, rdev);


    // initialize active discharge enable
    BUG_ON(!init_data->driver_data);
    ade = *(u8*)init_data->driver_data;

    addr = desc->cntrl_reg;

    mask = LSW_REG_BITMASK(SW_CNTRL, LSADE);
    val  = ade ? 0xFF : 0x00;

    rc = max77696_write_masked(me->i2c, addr, mask, val);
    if (unlikely(rc)) {
        dev_err(me->dev, "SW_CNTRL write error [%d]\n", rc);
    }

    return 0;

out_err:
    if (likely(rdev)) {
        regulator_unregister(rdev);
        me->vreg[pdev->id].rdev = NULL;
    }
    return rc;
}

static int max77696_lsw_vreg_remove (struct platform_device *pdev)
{
    struct regulator_dev *rdev = platform_get_drvdata(pdev);
    struct max77696_lsw_vreg *vreg = __rdev_to_max77696_lsw_vreg(rdev);

    regulator_unregister(rdev);
    vreg->rdev = NULL;

    return 0;
}

static struct platform_driver max77696_lsw_vreg_driver = {
    .probe       = max77696_lsw_vreg_probe,
    .remove      = max77696_lsw_vreg_remove,
    .driver.name = LSW_VREG_NAME,
};

static __always_inline
void max77696_lsw_unregister_vreg_drv (struct max77696_lsw *me)
{
    platform_driver_unregister(&max77696_lsw_vreg_driver);
}

static __always_inline
int max77696_lsw_register_vreg_drv (struct max77696_lsw *me)
{
    return platform_driver_register(&max77696_lsw_vreg_driver);
}

static __always_inline
void max77696_lsw_unregister_vreg_dev (struct max77696_lsw *me, int id)
{
    /* nothing to do */
}

static __always_inline
int max77696_lsw_register_vreg_dev (struct max77696_lsw *me, int id,
    struct regulator_init_data *init_data)
{
    struct max77696_lsw_vreg *vreg = &(me->vreg[id]);
    int rc;

    vreg->pdev = platform_device_alloc(LSW_VREG_NAME, id);
    if (unlikely(!vreg->pdev)) {
        dev_err(me->dev, "failed to alloc pdev for %s.%d\n", LSW_VREG_NAME, id);
        rc = -ENOMEM;
        goto out_err;
    }

    platform_set_drvdata(vreg->pdev, me);
    vreg->pdev->dev.platform_data = init_data;
    vreg->pdev->dev.parent        = me->dev;

    rc = platform_device_add(vreg->pdev);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to pdev for %s.%d [%d]\n",
            LSW_VREG_NAME, id, rc);
        goto out_err;
    }

    return 0;

out_err:
    if (likely(me->vreg[id].pdev)) {
        platform_device_del(me->vreg[id].pdev);
        me->vreg[id].pdev = NULL;
    }
    return rc;
}

static void max77696_lsw_unregister_vreg (struct max77696_lsw *me)
{
    int i;

    for (i = 0; i < LSW_NREG; i++) {
        max77696_lsw_unregister_vreg_dev(me, i);
    }

    max77696_lsw_unregister_vreg_drv(me);
}

static int max77696_lsw_register_vreg (struct max77696_lsw *me,
    struct max77696_lsw_platform_data *pdata)
{
    int i, rc;
    struct regulator_init_data* init_data = pdata->init_data;

    rc = max77696_lsw_register_vreg_drv(me);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to register vreg drv for %s [%d]\n",
            LSW_VREG_NAME, rc);
        goto out;
    }

    for (i = 0; i < LSW_NREG; i++, init_data++) {
        dev_verbose(me->dev, "registering vreg dev for %s.%d ...\n",
            LSW_VREG_NAME, i);

	init_data->driver_data = &pdata->ade[i];
        rc = max77696_lsw_register_vreg_dev(me, i, init_data);
        if (unlikely(rc)) {
            dev_err(me->dev, "failed to register vreg dev for %s.%d [%d]\n",
                LSW_VREG_NAME, i, rc);
            goto out;
        }
    }

out:
    return rc;
}

static __devinit int max77696_lsw_probe (struct platform_device *pdev)
{
    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_lsw_platform_data *pdata = dev_get_platdata(&(pdev->dev));
    struct max77696_lsw *me;
    int rc;

    if (unlikely(!pdata)) {
        dev_err(&(pdev->dev), "platform data is missing\n");
        return -ENODEV;
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
    
    /* Register load switches driver & device */
    rc = max77696_lsw_register_vreg(me, pdata);
    if (unlikely(rc)) {
        goto out_err_reg_vregs;
    }

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(lsw, chip);
    return 0;

out_err_reg_vregs:
    mutex_destroy(&(me->lock));
    platform_set_drvdata(pdev, NULL);
    kfree(me);
    return rc;
}

static __devexit int max77696_lsw_remove (struct platform_device *pdev)
{
    struct max77696_lsw *me = platform_get_drvdata(pdev);

    max77696_lsw_unregister_vreg(me);

    mutex_destroy(&(me->lock));
    platform_set_drvdata(pdev, NULL);
    kfree(me);

    return 0;
}

static struct platform_driver max77696_lsw_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .probe        = max77696_lsw_probe,
    .remove       = __devexit_p(max77696_lsw_remove),
};

static __init int max77696_lsw_driver_init (void)
{
    return platform_driver_register(&max77696_lsw_driver);
}

static __exit void max77696_lsw_driver_exit (void)
{
    platform_driver_unregister(&max77696_lsw_driver);
}

subsys_initcall(max77696_lsw_driver_init);
module_exit(max77696_lsw_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

