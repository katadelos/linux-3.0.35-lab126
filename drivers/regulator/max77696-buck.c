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

#define DRIVER_DESC    "MAX77696 Buck Regulators Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_BUCK_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".1"

#ifdef VERBOSE
#define dev_verbose(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_verbose(args...) do { } while (0)
#endif /* VERBOSE */

#define BUCK_VREG_NAME                 MAX77696_BUCK_NAME"-vreg"
#define BUCK_VREG_DESC_NAME(_name)     MAX77696_NAME"-vreg-"_name
#define BUCK_NREG                      MAX77696_BUCK_NR_REGS

#define mV_to_uV(mV)                   (mV * 1000)
#define uV_to_mV(uV)                   (uV / 1000)
#define V_to_uV(V)                     (mV_to_uV(V * 1000))
#define uV_to_V(uV)                    (uV_to_mV(uV) / 1000)

/* Power Mode */
#define BUCK_POWERMODE_OFF             MAX77696_BUCK_OPERATING_MODE_OFF
#define BUCK_POWERMODE_DYNAMIC_STANDBY MAX77696_BUCK_OPERATING_MODE_DYNAMIC_STANDBY
#define BUCK_POWERMODE_FORCED_STANDBY  MAX77696_BUCK_OPERATING_MODE_FORCED_STANDBY
#define BUCK_POWERMODE_NORMAL          MAX77696_BUCK_OPERATING_MODE_NORMAL

struct max77696_buck_vreg_desc {
    struct regulator_desc rdesc;

    int                   min_uV, max_uV, step_uV;
    u8                    vout_reg, cnfg_reg;
};

struct max77696_buck_vreg {
    struct max77696_buck_vreg_desc *desc;
    struct regulator_dev           *rdev;
    struct platform_device         *pdev;

    unsigned int                    mode;
    bool                            enabled_in_suspend;
};

struct max77696_buck {
    struct mutex                lock;
    struct max77696_chip       *chip;
    struct max77696_i2c        *i2c;
    struct device              *dev;

    struct max77696_buck_vreg   vreg[BUCK_NREG];

    u8                          cnfgfps2_save;
    u8                          fpsbuck1_save;
};

#define __get_i2c(chip)                (&((chip)->pmic_i2c))
#define __lock(me)                     mutex_lock(&((me)->lock))
#define __unlock(me)                   mutex_unlock(&((me)->lock))

/* GPIO Register Read/Write */
#define max77696_buck_reg_read(me, reg, val_ptr) \
        max77696_read((me)->i2c, BUCK_REG(reg), val_ptr)
#define max77696_buck_reg_write(me, reg, val) \
        max77696_write((me)->i2c, BUCK_REG(reg), val)
#define max77696_buck_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, BUCK_REG(reg), dst, len)
#define max77696_buck_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, BUCK_REG(reg), src, len)
#define max77696_buck_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, BUCK_REG(reg), mask, val_ptr)
#define max77696_buck_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, BUCK_REG(reg), mask, val)

/* BUCK Register Single Bit Ops */
#define max77696_buck_reg_get_bit(me, reg, bit, val_ptr) \
        ({\
            int __rc = max77696_buck_reg_read_masked(me, reg,\
                BUCK_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = BUCK_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_buck_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_buck_reg_write_masked(me, reg,\
                BUCK_REG_BITMASK(reg, bit), BUCK_REG_BITSET(reg, bit, val));\
        })

/* BUCK Configuration Registers Ops */
#define max77696_buck_read_cnfg_reg(me, vreg, cfg_item, cfg_val_ptr) \
        ({\
            struct max77696_buck_vreg_desc *__desc = vreg->desc;\
            int __rc;\
            __rc = max77696_read_masked((me)->i2c, __desc->cnfg_reg,\
                BUCK_REG_BITMASK(VOUTCNFG, cfg_item), cfg_val_ptr);\
            if (likely(!__rc)) {\
                *(cfg_val_ptr) = (u8)(*(cfg_val_ptr) >>\
                    BUCK_REG_BITSHIFT(VOUTCNFG, cfg_item));\
                dev_verbose((me)->dev,\
                    "%s read config addr %02X mask %02X val %02X\n",\
                    __desc->rdesc.name,\
                    __desc->cnfg_reg,\
                    BUCK_REG_BITMASK(VOUTCNFG, cfg_item),\
                    *(cfg_val_ptr));\
            } else {\
                dev_err((me)->dev, "BUCK_VOUTCNFG_REG read error [%d]\n", __rc);\
            }\
            __rc;\
        })
#define max77696_buck_write_cnfg_reg(me, vreg, cfg_item, cfg_val) \
        ({\
            struct max77696_buck_vreg_desc *__desc = vreg->desc;\
            int __rc;\
            dev_verbose((me)->dev,\
                "%s write config1 addr %02X mask %02X val %02X\n",\
                __desc->rdesc.name,\
                __desc->cnfg_reg,\
                BUCK_REG_BITMASK(VOUTCNFG, cfg_item),\
                (cfg_val) << BUCK_REG_BITSHIFT(VOUTCNFG, cfg_item));\
            __rc = max77696_write_masked((me)->i2c, __desc->cnfg_reg,\
                BUCK_REG_BITMASK(VOUTCNFG, cfg_item),\
                (u8)((cfg_val) << BUCK_REG_BITSHIFT(VOUTCNFG, cfg_item)));\
            if (unlikely(__rc)) {\
                dev_err((me)->dev, "BUCK_VOUTCNFG_REG write error [%d]\n", __rc);\
            }\
            __rc;\
        })

static int max77696_buck_write_vout_reg (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg, int uV)
{
    struct max77696_buck_vreg_desc *desc = vreg->desc;
    u8 val;
    int rc;

    if (unlikely(uV < desc->min_uV || uV > desc->max_uV)) {
        dev_err(me->dev, "%s setting voltage out of range\n",
            desc->rdesc.name);
        rc = -EINVAL;
        goto out;
    }

    val = (u8)DIV_ROUND_UP(uV - desc->min_uV, desc->step_uV);

    dev_verbose(me->dev, "%s write vout reg addr %02X val %02X\n",
        desc->rdesc.name, desc->vout_reg, val);

    rc = max77696_write(me->i2c, desc->vout_reg, val);
    if (unlikely(rc)) {
        dev_err(me->dev, "%s VOUT_REG write error [%d]\n",
            desc->rdesc.name, rc);
        goto out;
    }

out:
    return rc;
}

static int max77696_buck_read_vout_reg (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg)
{
    struct max77696_buck_vreg_desc *desc = vreg->desc;
    u8 val;
    int voltage = 0, rc;

    rc = max77696_read(me->i2c, desc->vout_reg, &val);
    if (unlikely(rc)) {
        dev_err(me->dev, "%s VOUT_REG read error [%d]\n",
            desc->rdesc.name, rc);
        goto out;
    }

    dev_verbose(me->dev, "%s read vout reg addr %02X val %02X\n",
        desc->rdesc.name, desc->vout_reg, val);

    voltage  = (int)val * desc->step_uV;
    voltage += desc->min_uV;

out:
    return voltage;
}

static __always_inline
int max77696_buck_set_rsr_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg, u8 rsr)
{
    return max77696_buck_write_cnfg_reg(me, vreg, BUCKRSR, rsr);
}

static u8 max77696_buck_get_rsr_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg)
{
    u8 rsr = 0;
    max77696_buck_read_cnfg_reg(me, vreg, BUCKRSR, &rsr);
    return rsr;
}

static __always_inline
int max77696_buck_set_pwrmd_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg, u8 pwrmd)
{
    return max77696_buck_write_cnfg_reg(me, vreg, BUCKPWRMD, pwrmd);
}

static u8 max77696_buck_get_pwrmd_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg)
{
    u8 pwrmd = 0;
    max77696_buck_read_cnfg_reg(me, vreg, BUCKPWRMD, &pwrmd);
    return pwrmd;
}

static __always_inline
int max77696_buck_set_aden_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg, u8 aden)
{
    return max77696_buck_write_cnfg_reg(me, vreg, BUCKADEN, aden);
}

static u8 max77696_buck_get_aden_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg)
{
    u8 aden = 0;
    max77696_buck_read_cnfg_reg(me, vreg, BUCKADEN, &aden);
    return !!aden;
}

static __always_inline
int max77696_buck_set_fpwmen_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg, u8 fpwmen)
{
    return max77696_buck_write_cnfg_reg(me, vreg, BUCKFPWMEN, fpwmen);
}

static u8 max77696_buck_get_fpwmen_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg)
{
    u8 fpwmen = 0;
    max77696_buck_read_cnfg_reg(me, vreg, BUCKFPWMEN, &fpwmen);
    return !!fpwmen;
}

static __always_inline
int max77696_buck_set_imonen_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg, u8 imonen)
{
    return max77696_buck_write_cnfg_reg(me, vreg, BUCKIMONEN, imonen);
}

static u8 max77696_buck_get_imonen_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg)
{
    u8 imonen = 0;
    max77696_buck_read_cnfg_reg(me, vreg, BUCKIMONEN, &imonen);
    return !!imonen;
}

static __always_inline
int max77696_buck_set_fsren_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg, u8 fsren)
{
    return max77696_buck_write_cnfg_reg(me, vreg, BUCKFSREN, fsren);
}

static u8 max77696_buck_get_fsren_bit (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg)
{
    u8 fsren = 0;
    max77696_buck_read_cnfg_reg(me, vreg, BUCKFSREN, &fsren);
    return !!fsren;
}

#define max77696_buck_enable(me, vreg) \
        max77696_buck_set_pwrmd_bit(me, vreg, BUCK_POWERMODE_NORMAL)

#define max77696_buck_disable(me, vreg) \
        max77696_buck_set_pwrmd_bit(me, vreg, BUCK_POWERMODE_OFF)

#define max77696_buck_is_enabled(me, vreg) \
        (!!max77696_buck_get_pwrmd_bit(me, vreg))

static int max77696_buck_operating_mode (struct max77696_buck *me,
    struct max77696_buck_vreg *vreg, unsigned int mode)
{
    u8 fpwmen, pwrmd;
    int rc;

    printk("buck mode: %d\n", mode);
    switch (mode) {
    case REGULATOR_MODE_FAST:
        fpwmen = 1;
        pwrmd  = BUCK_POWERMODE_NORMAL;
        break;

    case REGULATOR_MODE_NORMAL:
        fpwmen = 0;
        pwrmd  = BUCK_POWERMODE_NORMAL;
        break;

    case REGULATOR_MODE_IDLE:
        fpwmen = 0;
        pwrmd  = BUCK_POWERMODE_DYNAMIC_STANDBY;
        break;

    case REGULATOR_MODE_STANDBY:
        fpwmen = 0;
        pwrmd  = BUCK_POWERMODE_FORCED_STANDBY;
        break;

    default:
        rc = -EINVAL;
        goto out;
    }

    rc = max77696_buck_set_fpwmen_bit(me, vreg, fpwmen);
    if (unlikely(rc)) {
        goto out;
    }

    rc = max77696_buck_set_pwrmd_bit(me, vreg, pwrmd);
    if (unlikely(rc)) {
        goto out;
    }

out:
    return rc;
}

#define __rdev_name(rdev_ptr) \
        (rdev_ptr->desc->name)
#define __rdev_to_max77696_buck(rdev_ptr) \
        ((struct max77696_buck*)rdev_get_drvdata(rdev_ptr))
#define __rdev_to_max77696_buck_vreg(rdev_ptr) \
        (&(__rdev_to_max77696_buck(rdev_ptr)->vreg[rdev_ptr->desc->id]))

static int max77696_buck_vreg_set_voltage (struct regulator_dev *rdev,
    int min_uV, int max_uV, unsigned* selector)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    dev_verbose(me->dev, "%s set_voltage(min %duV, max %duV)\n",
        __rdev_name(rdev), min_uV, max_uV);

    rc = max77696_buck_write_vout_reg(me, vreg, min_uV);

    __unlock(me);
    return rc;
}

static int max77696_buck_vreg_get_voltage (struct regulator_dev *rdev)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    rc = max77696_buck_read_vout_reg(me, vreg);

    __unlock(me);
    return rc;
}

static int max77696_buck_vreg_enable (struct regulator_dev *rdev)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    if (likely(vreg->mode)) {
        rc = max77696_buck_operating_mode(me, vreg, vreg->mode);
    } else {
        rc = max77696_buck_enable(me, vreg);
    }

    __unlock(me);
    return rc;
}

static int max77696_buck_vreg_disable (struct regulator_dev *rdev)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    rc = max77696_buck_disable(me, vreg);

    __unlock(me);
    return rc;
}

static int max77696_buck_vreg_is_enabled (struct regulator_dev *rdev)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    rc = max77696_buck_is_enabled(me, vreg);

    __unlock(me);
    return rc;
}

static int max77696_buck_vreg_set_mode (struct regulator_dev *rdev,
    unsigned int mode)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    if (likely(max77696_buck_is_enabled(me, vreg))) {
        rc = max77696_buck_operating_mode(me, vreg, mode);
        if (unlikely(rc)) {
            goto out;
        }
    }

    vreg->mode = mode;

out:
    __unlock(me);
    return rc;
}

static unsigned int max77696_buck_vreg_get_mode (struct regulator_dev *rdev)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    u8 fpwmen, pwrmd;
    unsigned int rc;

    __lock(me);

    fpwmen = max77696_buck_get_fpwmen_bit(me, vreg);
    pwrmd  = max77696_buck_get_pwrmd_bit(me, vreg);

    switch (pwrmd) {
    case BUCK_POWERMODE_DYNAMIC_STANDBY:
        rc = REGULATOR_MODE_IDLE;
        break;

    case BUCK_POWERMODE_FORCED_STANDBY:
        rc = REGULATOR_MODE_STANDBY;
        break;

    case BUCK_POWERMODE_NORMAL:
        rc = (fpwmen? REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL);
        break;

    default:
        rc = 0; /* diabled */
        break;
    }

    __unlock(me);
    return rc;
}

static int max77696_buck_vreg_set_suspend_voltage (struct regulator_dev *rdev,
    int uV)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    if (unlikely(!vreg->enabled_in_suspend)) {
        rc = 0;
        goto out;
    }

    rc = max77696_buck_write_vout_reg(me, vreg, uV);

out:
    __unlock(me);
    return rc;
}

static int max77696_buck_vreg_set_suspend_enable (struct regulator_dev *rdev)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);

    __lock(me);

    vreg->enabled_in_suspend = 1;

    __unlock(me);
    return 0;
}

static int max77696_buck_vreg_set_suspend_disable (struct regulator_dev *rdev)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    vreg->enabled_in_suspend = 0;

    rc = max77696_buck_disable(me, vreg);

    __unlock(me);
    return rc;
}

static int max77696_buck_vreg_set_suspend_mode (struct regulator_dev *rdev,
    unsigned int mode)
{
    struct max77696_buck *me = __rdev_to_max77696_buck(rdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);
    int rc;

    __lock(me);

    if (unlikely(!vreg->enabled_in_suspend)) {
        rc = 0;
        goto out;
    }

    rc = max77696_buck_operating_mode(me, vreg, mode);

out:
    __unlock(me);
    return rc;
}

static struct regulator_ops max77696_buck_vreg_ops = {
    .set_voltage         = max77696_buck_vreg_set_voltage,
    .get_voltage         = max77696_buck_vreg_get_voltage,

    /* enable/disable regulator */
    .enable              = max77696_buck_vreg_enable,
    .disable             = max77696_buck_vreg_disable,
    .is_enabled          = max77696_buck_vreg_is_enabled,

    /* get/set regulator operating mode (defined in regulator.h) */
    .set_mode            = max77696_buck_vreg_set_mode,
    .get_mode            = max77696_buck_vreg_get_mode,

    /* the operations below are for configuration of regulator state when
     * its parent PMIC enters a global STANDBY/HIBERNATE state */
    .set_suspend_voltage = max77696_buck_vreg_set_suspend_voltage,
    .set_suspend_enable  = max77696_buck_vreg_set_suspend_enable,
    .set_suspend_disable = max77696_buck_vreg_set_suspend_disable,
    .set_suspend_mode    = max77696_buck_vreg_set_suspend_mode,
};

#define BUCK_VREG_DESC(_id, _name, _vout_reg, _cnfg_reg, _min, _max, _step) \
        [MAX77696_BUCK_ID_##_id] = {\
            .rdesc.name  = BUCK_VREG_DESC_NAME(_name),\
            .rdesc.id    = MAX77696_BUCK_ID_##_id,\
            .rdesc.ops   = &max77696_buck_vreg_ops,\
            .rdesc.type  = REGULATOR_VOLTAGE,\
            .rdesc.owner = THIS_MODULE,\
            .min_uV      = _min,\
            .max_uV      = _max,\
            .step_uV     = _step,\
            .vout_reg    = BUCK_REG(_vout_reg),\
            .cnfg_reg    = BUCK_REG(_cnfg_reg),\
        }

#define VREG_DESC(id) (&(max77696_buck_vreg_descs[id]))
static struct max77696_buck_vreg_desc max77696_buck_vreg_descs[BUCK_NREG] = {
    BUCK_VREG_DESC(B1,    "buck1",    VOUT1,    VOUTCNFG1, 600000, 3387500, 12500),
    BUCK_VREG_DESC(B1DVS, "buck1dvs", VOUT1DVS, VOUTCNFG1, 600000, 3387500, 12500),
    BUCK_VREG_DESC(B2,    "buck2",    VOUT2,    VOUTCNFG2, 600000, 3387500, 12500),
    BUCK_VREG_DESC(B2DVS, "buck2dvs", VOUT2DVS, VOUTCNFG2, 600000, 3387500, 12500),
    BUCK_VREG_DESC(B3,    "buck3",    VOUT3,    VOUTCNFG3, 600000, 3387500, 12500),
    BUCK_VREG_DESC(B4,    "buck4",    VOUT4,    VOUTCNFG4, 600000, 3387500, 12500),
    BUCK_VREG_DESC(B5,    "buck5",    VOUT5,    VOUTCNFG5, 600000, 3387500, 12500),
    BUCK_VREG_DESC(B6,    "buck6",    VOUT6,    VOUTCNFG6, 600000, 3387500, 12500),
};

static int max77696_buck_vreg_probe (struct platform_device *pdev)
{
    struct max77696_buck *me = platform_get_drvdata(pdev);
    struct max77696_buck_vreg *vreg = &(me->vreg[pdev->id]);
    struct max77696_buck_vreg_desc *desc = VREG_DESC(pdev->id);
    struct regulator_init_data *init_data = dev_get_platdata(&(pdev->dev));
    struct regulator_dev *rdev;
    int rc;

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

    return 0;

out_err:
    if (likely(rdev)) {
        regulator_unregister(rdev);
        vreg->rdev = NULL;
    }
    return rc;
}

static int max77696_buck_vreg_remove (struct platform_device *pdev)
{
    struct regulator_dev *rdev = platform_get_drvdata(pdev);
    struct max77696_buck_vreg *vreg = __rdev_to_max77696_buck_vreg(rdev);

    regulator_unregister(rdev);
    vreg->rdev = NULL;

    return 0;
}

static struct platform_driver max77696_buck_vreg_driver = {
    .probe       = max77696_buck_vreg_probe,
    .remove      = max77696_buck_vreg_remove,
    .driver.name = BUCK_VREG_NAME,
};

static __always_inline
void max77696_buck_unregister_vreg_drv (struct max77696_buck *me)
{
    platform_driver_unregister(&max77696_buck_vreg_driver);
}

static __always_inline
int max77696_buck_register_vreg_drv (struct max77696_buck *me)
{
    return platform_driver_register(&max77696_buck_vreg_driver);
}

static __always_inline
void max77696_buck_unregister_vreg_dev (struct max77696_buck *me, int id)
{
    /* nothing to do */
}

static __always_inline
int max77696_buck_register_vreg_dev (struct max77696_buck *me, int id,
    struct regulator_init_data *init_data)
{
    struct max77696_buck_vreg *vreg = &(me->vreg[id]);
    int rc;

    vreg->pdev = platform_device_alloc(BUCK_VREG_NAME, id);
    if (unlikely(!vreg->pdev)) {
        dev_err(me->dev, "failed to alloc pdev for %s.%d\n",
            BUCK_VREG_NAME, id);
        rc = -ENOMEM;
        goto out_err;
    }

    platform_set_drvdata(vreg->pdev, me);
    vreg->pdev->dev.platform_data = init_data;
    vreg->pdev->dev.parent        = me->dev;

    rc = platform_device_add(vreg->pdev);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to pdev for %s.%d [%d]\n",
            BUCK_VREG_NAME, id, rc);
        goto out_err;
    }

    return 0;

out_err:
    if (likely(vreg->pdev)) {
        platform_device_del(vreg->pdev);
        vreg->pdev = NULL;
    }
    return rc;
}

static void max77696_buck_unregister_vreg (struct max77696_buck *me)
{
    int i;

    for (i = 0; i < BUCK_NREG; i++) {
        max77696_buck_unregister_vreg_dev(me, i);
    }

    max77696_buck_unregister_vreg_drv(me);
}

static int max77696_buck_register_vreg (struct max77696_buck *me,
    struct regulator_init_data init_data[BUCK_NREG])
{
    int i, rc;

    rc = max77696_buck_register_vreg_drv(me);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to register vreg drv for %s [%d]\n",
            BUCK_VREG_NAME, rc);
        goto out;
    }

    for (i = 0; i < BUCK_NREG; i++) {
        dev_verbose(me->dev, "registering vreg dev for %s.%d ...\n",
            BUCK_VREG_NAME, i);
        rc = max77696_buck_register_vreg_dev(me, i, &(init_data[i]));
        if (unlikely(rc)) {
            dev_err(me->dev, "failed to register vreg dev for %s.%d [%d]\n",
                BUCK_VREG_NAME, i, rc);
            goto out;
        }
    }

out:
    return rc;
}

static __devinit int max77696_buck_probe (struct platform_device *pdev)
{
    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_buck_platform_data *pdata = dev_get_platdata(&(pdev->dev));
    struct max77696_buck *me;
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

    if (unlikely(!pdata->support_suspend_ops)) {
        max77696_buck_vreg_ops.set_suspend_voltage = NULL;
        max77696_buck_vreg_ops.set_suspend_enable  = NULL;
        max77696_buck_vreg_ops.set_suspend_disable = NULL;
        max77696_buck_vreg_ops.set_suspend_mode    = NULL;
    }

    /* Register buck regulators driver & device */
    rc = max77696_buck_register_vreg(me, pdata->init_data);
    if (unlikely(rc)) {
        goto out_err_reg_vregs;
    }

    BUG_ON(chip->buck_ptr);
    chip->buck_ptr = me;

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(buck, chip);
    return 0;

out_err_reg_vregs:
    mutex_destroy(&(me->lock));
    platform_set_drvdata(pdev, NULL);
    kfree(me);
    return rc;
}

static __devexit int max77696_buck_remove (struct platform_device *pdev)
{
    struct max77696_buck *me = platform_get_drvdata(pdev);

    me->chip->buck_ptr = NULL;

    max77696_buck_unregister_vreg(me);

    mutex_destroy(&(me->lock));
    platform_set_drvdata(pdev, NULL);
    kfree(me);

    return 0;
}

static int max77696_buck_suspend(struct platform_device *device, pm_message_t state) {
	struct max77696_buck *me = platform_get_drvdata(device);
	int ret;

	ret = max77696_buck_reg_read(me, FPSBUCK1, &me->fpsbuck1_save);
	if (ret) {
		dev_err(&device->dev, "Failed to read FPSBUCK1 (%d)\n", ret);
		return ret;
	}

	ret = max77696_read(me->i2c, CNFG_FPS_REG(FPS2), &me->cnfgfps2_save);
	if (ret) {
		dev_err(&device->dev, "Failed to read CNFG_FPS2(%d)\n", ret);
		return ret;
	}

	/*
	 * FPSSRC=2 will place it in sequencer 2 (shuts down in suspend)
	 * Power down slot 7 and power up slot 0 ensures the rail reacts
	 * quickly out of suspend.
	 */
	ret = max77696_buck_reg_write(me, FPSBUCK1,
		BUCK_REG_BITSET(FPSBUCK, SRC, 2) |
		BUCK_REG_BITSET(FPSBUCK, PU, 0) |
		BUCK_REG_BITSET(FPSBUCK, PD, 7));

	if (ret) {
		dev_err(&device->dev, "Failed to write FPSBUCK1(%d)\n", ret);
		return ret;
	}

	/*
	 * setting TFPS=0 set the minimum delay between power sequence events on the sequencer.
	 * Since we are only turning on thing on/off in suspend, there is no reason to delay
	 */
	ret = max77696_write(me->i2c, CNFG_FPS_REG(FPS2), CNFG_FPS_REG_BITSET(TFPS, 0));
	if (ret) {
		dev_err(&device->dev, "Failed to write CNFG_FPS2(%d)\n", ret);
		//Since we already failed, this will probably fail, but we should try to restore it anyway
		max77696_buck_reg_write(me, FPSBUCK1, me->fpsbuck1_save);
		return ret;
	}

	return ret;
}

static int max77696_buck_resume(struct platform_device *device) {
	struct max77696_buck *me = platform_get_drvdata(device);
	int ret;

	ret = max77696_buck_reg_write(me, FPSBUCK1, me->fpsbuck1_save);
	if (ret) {
		dev_err(&device->dev, "Failed to write FPSBUCK1(%d)\n", ret);
		return ret;
	}

	ret = max77696_write(me->i2c, CNFG_FPS_REG(FPS2), me->cnfgfps2_save);
	if (ret) {
		dev_err(&device->dev, "Failed to write CNFG_FPS2(%d)\n", ret);
	}

	return ret;
}

static struct platform_driver max77696_buck_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .probe        = max77696_buck_probe,
    .remove       = __devexit_p(max77696_buck_remove),
    .suspend      = max77696_buck_suspend,
    .resume       = max77696_buck_resume
};

static __init int max77696_buck_driver_init (void)
{
    return platform_driver_register(&max77696_buck_driver);
}

static __exit void max77696_buck_driver_exit (void)
{
    platform_driver_unregister(&max77696_buck_driver);
}

subsys_initcall(max77696_buck_driver_init);
module_exit(max77696_buck_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

/*******************************************************************************
 * EXTERNAL FUNCTIONS
 ******************************************************************************/

extern struct max77696_chip* max77696;

/* Rising Slew Rate Selection */
int max77696_buck_get_rising_slew_rate (u8 buck)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    u8 slew_rate = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    slew_rate = max77696_buck_get_rsr_bit(me, &(me->vreg[buck]));
    __unlock(me);

    return slew_rate;
}
EXPORT_SYMBOL(max77696_buck_get_rising_slew_rate);

int max77696_buck_set_rising_slew_rate (u8 buck, u8 slew_rate)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_buck_set_rsr_bit(me, &(me->vreg[buck]), slew_rate);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_buck_set_rising_slew_rate);

/* Operating Mode Selection */
int max77696_buck_get_operating_mode (u8 buck)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    u8 pwrmd = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    pwrmd = max77696_buck_get_pwrmd_bit(me, &(me->vreg[buck]));
    __unlock(me);

    return pwrmd;
}
EXPORT_SYMBOL(max77696_buck_get_operating_mode);

int max77696_buck_set_operating_mode (u8 buck, u8 mode)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_buck_set_pwrmd_bit(me, &(me->vreg[buck]), mode);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_buck_set_operating_mode);

/* Active-Low Active Discharge Enable */
int max77696_buck_get_active_discharge_enable (u8 buck)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    u8 aden = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    aden = max77696_buck_get_aden_bit(me, &(me->vreg[buck]));
    __unlock(me);

    return aden;
}
EXPORT_SYMBOL(max77696_buck_get_active_discharge_enable);

int max77696_buck_set_active_discharge_enable (u8 buck, bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_buck_set_aden_bit(me, &(me->vreg[buck]), !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_buck_set_active_discharge_enable);

/* Forced PWM Mode Enable */
int max77696_buck_get_fpwm_enable (u8 buck)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    u8 fpwmen = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    fpwmen = max77696_buck_get_fpwmen_bit(me, &(me->vreg[buck]));
    __unlock(me);

    return fpwmen;
}
EXPORT_SYMBOL(max77696_buck_get_fpwm_enable);

int max77696_buck_set_fpwm_enable (u8 buck, bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_buck_set_fpwmen_bit(me, &(me->vreg[buck]), !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_buck_set_fpwm_enable);

/* Current Monitor Enable */
int max77696_buck_get_imon_enable (u8 buck)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    u8 imonen = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    imonen = max77696_buck_get_imonen_bit(me, &(me->vreg[buck]));
    __unlock(me);

    return imonen;
}
EXPORT_SYMBOL(max77696_buck_get_imon_enable);

int max77696_buck_set_imon_enable (u8 buck, bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_buck_set_imonen_bit(me, &(me->vreg[buck]), !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_buck_set_imon_enable);

/* Active-Low Falling Slew Rate Enable */
int max77696_buck_get_falling_slew_rate_enable (u8 buck)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    u8 fsren = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    fsren = max77696_buck_get_fsren_bit(me, &(me->vreg[buck]));
    __unlock(me);

    return fsren;
}
EXPORT_SYMBOL(max77696_buck_get_falling_slew_rate_enable);

int max77696_buck_set_falling_slew_rate_enable (u8 buck, bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_buck *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->buck_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_buck is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(buck >= BUCK_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_buck_set_fsren_bit(me, &(me->vreg[buck]), !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_buck_set_falling_slew_rate_enable);

