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
#include <linux/gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/max77696.h>
#include <max77696_registers.h>
#include <mach/boardid.h>

#define DRIVER_DESC    "MAX77696 Linear Regulators Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_LDO_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".2"

#ifdef VERBOSE
#define dev_verbose(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_verbose(args...) do { } while (0)
#endif /* VERBOSE */

#ifdef CONFIG_FALCON
extern int in_falcon(void);
#endif

#define LDO_VREG_NAME                 MAX77696_LDO_NAME"-vreg"
#define LDO_VREG_DESC_NAME(_name)     MAX77696_NAME"-vreg-"_name
#define LDO_NREG                      MAX77696_LDO_NR_REGS

#define mV_to_uV(mV)                  (mV * 1000)
#define uV_to_mV(uV)                  (uV / 1000)
#define V_to_uV(V)                    (mV_to_uV(V * 1000))
#define uV_to_V(uV)                   (uV_to_mV(uV) / 1000)

/* Power Mode */
#define LDO_POWERMODE_OFF                MAX77696_LDO_POWERMODE_OFF
#define LDO_POWERMODE_LOW_POWER          MAX77696_LDO_POWERMODE_LOW_POWER
#define LDO_POWERMODE_FORCED_LOW_POWER   MAX77696_LDO_POWERMODE_FORCED_LOW_POWER
#define LDO_POWERMODE_NORMAL             MAX77696_LDO_POWERMODE_NORMAL

struct max77696_ldo_vreg_desc {
    struct regulator_desc rdesc;

    int                   min_uV, max_uV, step_uV;
    u8                    cnfg1_reg, cnfg2_reg;
    int                   suspend_uV, resume_uV;
};

struct max77696_ldo_vreg {
    struct max77696_ldo_vreg_desc *desc;
    struct regulator_dev          *rdev;
    struct platform_device        *pdev;

    unsigned int                   mode;
};

struct max77696_ldo {
    struct mutex               lock;
    struct max77696_chip      *chip;
    struct max77696_i2c       *i2c;
    struct device             *dev;

    bool                       imon_mode_manual;
    unsigned long              enabled_imon_bitmap[BITS_TO_LONGS(LDO_NREG)];
    struct max77696_ldo_vreg   vreg[LDO_NREG];
};

#define __get_i2c(chip)                (&((chip)->pmic_i2c))
#define __lock(me)                     mutex_lock(&((me)->lock))
#define __unlock(me)                   mutex_unlock(&((me)->lock))

/* LDO Register Read/Write */
#define max77696_ldo_reg_read(me, reg, val_ptr) \
        max77696_read((me)->i2c, LDO_REG(reg), val_ptr)
#define max77696_ldo_reg_write(me, reg, val) \
        max77696_write((me)->i2c, LDO_REG(reg), val)
#define max77696_ldo_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, LDO_REG(reg), dst, len)
#define max77696_ldo_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, LDO_REG(reg), src, len)
#define max77696_ldo_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, LDO_REG(reg), mask, val_ptr)
#define max77696_ldo_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, LDO_REG(reg), mask, val)

/* LDO Register Single Bit Ops */
#define max77696_ldo_reg_get_bit(me, reg, bit, val_ptr) \
        ({\
            int __rc = max77696_ldo_reg_read_masked(me, reg,\
                LDO_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = LDO_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_ldo_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_ldo_reg_write_masked(me, reg,\
                LDO_REG_BITMASK(reg, bit), LDO_REG_BITSET(reg, bit, val));\
        })

/* LDO Configuration Registers Ops */
#define max77696_ldo_read_cnfg1_reg(me, vreg, cfg_item, cfg_val_ptr) \
        ({\
            struct max77696_ldo_vreg_desc *__desc = vreg->desc;\
            int __rc;\
            __rc = max77696_read_masked((me)->i2c, __desc->cnfg1_reg,\
                LDO_REG_BITMASK(CNFG1, cfg_item), cfg_val_ptr);\
            if (likely(!__rc)) {\
                *(cfg_val_ptr) = (u8)(*(cfg_val_ptr) >>\
                    LDO_REG_BITSHIFT(CNFG1, cfg_item));\
                dev_verbose((me)->dev,\
                    "%s read config1 addr %02X mask %02X val %02X\n",\
                    __desc->rdesc.name,\
                    __desc->cnfg1_reg,\
                    LDO_REG_BITMASK(CNFG1, cfg_item),\
                    *(cfg_val_ptr));\
            } else {\
                dev_err((me)->dev, "LDO_CNFG1_REG read error [%d]\n", __rc);\
            }\
            __rc;\
        })
#define max77696_ldo_write_cnfg1_reg(me, vreg, cfg_item, cfg_val) \
        ({\
            struct max77696_ldo_vreg_desc *__desc = vreg->desc;\
            int __rc;\
            dev_verbose((me)->dev,\
                "%s write config1 addr %02X mask %02X val %02X\n",\
                __desc->rdesc.name,\
                __desc->cnfg1_reg,\
                LDO_REG_BITMASK(CNFG1, cfg_item),\
                (cfg_val) << LDO_REG_BITSHIFT(CNFG1, cfg_item));\
            __rc = max77696_write_masked((me)->i2c, __desc->cnfg1_reg,\
                LDO_REG_BITMASK(CNFG1, cfg_item),\
                (u8)((cfg_val) << LDO_REG_BITSHIFT(CNFG1, cfg_item)));\
            if (unlikely(__rc)) {\
                dev_err((me)->dev, "LDO_CNFG1_REG write error [%d]\n", __rc);\
            }\
            __rc;\
        })

#define max77696_ldo_read_cnfg2_reg(me, vreg, cfg_item, cfg_val_ptr) \
        ({\
            struct max77696_ldo_vreg_desc *__desc = vreg->desc;\
            int __rc;\
            __rc = max77696_read_masked((me)->i2c, __desc->cnfg2_reg,\
                LDO_REG_BITMASK(CNFG2, cfg_item), cfg_val_ptr);\
            if (likely(!__rc)) {\
                *(cfg_val_ptr) = (u8)(*(cfg_val_ptr) >>\
                    LDO_REG_BITSHIFT(CNFG2, cfg_item));\
                dev_verbose((me)->dev,\
                    "%s read config2 addr %02X mask %02X val %02X\n",\
                    __desc->rdesc.name,\
                    __desc->cnfg2_reg,\
                    LDO_REG_BITMASK(CNFG2, cfg_item),\
                    *(cfg_val_ptr));\
            } else {\
                dev_err((me)->dev, "LDO_CNFG2_REG read error [%d]\n", __rc);\
            }\
            __rc;\
        })
#define max77696_ldo_write_cnfg2_reg(me, vreg, cfg_item, cfg_val) \
        ({\
            struct max77696_ldo_vreg_desc *__desc = vreg->desc;\
            int __rc;\
            dev_verbose((me)->dev,\
                "%s write config2 addr %02X mask %02X val %02X\n",\
                __desc->rdesc.name,\
                __desc->cnfg2_reg,\
                LDO_REG_BITMASK(CNFG2, cfg_item),\
                (cfg_val) << LDO_REG_BITSHIFT(CNFG2, cfg_item));\
            __rc = max77696_write_masked((me)->i2c, __desc->cnfg2_reg,\
                LDO_REG_BITMASK(CNFG2, cfg_item),\
                (u8)((cfg_val) << LDO_REG_BITSHIFT(CNFG2, cfg_item)));\
            if (unlikely(__rc)) {\
                dev_err((me)->dev, "LDO_CNFG2_REG write error [%d]\n", __rc);\
            }\
            __rc;\
        })

#define max77696_ldo_read_cnfg3_reg(me, cfg_item, cfg_val_ptr) \
        ({\
            int __rc;\
            __rc = max77696_read_masked((me)->i2c, LDO_CNFG3_REG,\
                LDO_REG_BITMASK(CNFG3, cfg_item), cfg_val_ptr);\
            if (likely(!__rc)) {\
                *(cfg_val_ptr) = (u8)(*(cfg_val_ptr) >>\
                    LDO_REG_BITSHIFT(CNFG3, cfg_item));\
                dev_verbose((me)->dev,\
                    "%s read config3 addr %02X mask %02X val %02X\n",\
                    LDO_VREG_DESC_NAME("*"),\
                    LDO_CNFG3_REG,\
                    LDO_REG_BITMASK(CNFG3, cfg_item),\
                    *(cfg_val_ptr));\
            } else {\
                dev_err((me)->dev, "LDO_CNFG3_REG read error [%d]\n", __rc);\
            }\
            __rc;\
        })
#define max77696_ldo_write_cnfg3_reg(me, cfg_item, cfg_val) \
        ({\
            int __rc;\
            dev_verbose((me)->dev,\
                "%s write config3 addr %02X mask %02X val %02X\n",\
                    LDO_VREG_DESC_NAME("*"),\
                LDO_CNFG3_REG,\
                LDO_REG_BITMASK(CNFG3, cfg_item),\
                (cfg_val) << LDO_REG_BITSHIFT(CNFG3, cfg_item));\
            __rc = max77696_write_masked((me)->i2c, LDO_CNFG3_REG,\
                LDO_REG_BITMASK(CNFG3, cfg_item),\
                (u8)((cfg_val) << LDO_REG_BITSHIFT(CNFG3, cfg_item)));\
            if (unlikely(__rc)) {\
                dev_err((me)->dev, "LDO_CNFG3_REG write error [%d]\n", __rc);\
            }\
            __rc;\
        })

/***
 *** LDO Configuration 1
 ***/

static __always_inline
int max77696_ldo_set_pwrmd_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, u8 pwrmd)
{
    return max77696_ldo_write_cnfg1_reg(me, vreg, PWRMD, pwrmd);
}

static __always_inline
u8 max77696_ldo_get_pwrmd_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    u8 pwrmd = 0;
    max77696_ldo_read_cnfg1_reg(me, vreg, PWRMD, &pwrmd);
    return pwrmd;
}

static int max77696_ldo_set_tv_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, int uV)
{
    struct max77696_ldo_vreg_desc *desc = vreg->desc;
    u8 val;
    int rc;

    if (unlikely(uV < desc->min_uV || uV > desc->max_uV)) {
        dev_err(me->dev, "%s setting voltage out of range\n",
            desc->rdesc.name);
        rc = -EINVAL;
        goto out;
    }

    val = (u8)DIV_ROUND_UP(uV - desc->min_uV, desc->step_uV);
    rc  = max77696_ldo_write_cnfg1_reg(me, vreg, TV, val);

out:
    return rc;
}

static int max77696_ldo_get_tv_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    struct max77696_ldo_vreg_desc *desc = vreg->desc;
    u8 val;
    int voltage = 0, rc;

    rc = max77696_ldo_read_cnfg1_reg(me, vreg, TV, &val);
    if (unlikely(rc)) {
        goto out;
    }

    voltage  = (int)val * desc->step_uV;
    voltage += desc->min_uV;

out:
    return voltage;
}

/***
 *** LDO Configuration 2
 ***/

static __always_inline
int max77696_ldo_set_ovclmp_en_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, u8 ovclmp_en)
{
    return max77696_ldo_write_cnfg2_reg(me, vreg, OVCLMP_EN, ovclmp_en);
}

static __always_inline
u8 max77696_ldo_get_ovclmp_en_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    u8 ovclmp_en = 0;
    max77696_ldo_read_cnfg2_reg(me, vreg, OVCLMP_EN, &ovclmp_en);
    return !!ovclmp_en;
}

static __always_inline
int max77696_ldo_set_alpm_en_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, u8 alpm_en)
{
    return max77696_ldo_write_cnfg2_reg(me, vreg, ALPM_EN, alpm_en);
}

static __always_inline
u8 max77696_ldo_get_alpm_en_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    u8 alpm_en = 0;
    max77696_ldo_read_cnfg2_reg(me, vreg, ALPM_EN, &alpm_en);
    return !!alpm_en;
}

static __always_inline
int max77696_ldo_set_comp_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, u8 comp)
{
    return max77696_ldo_write_cnfg2_reg(me, vreg, COMP, comp);
}

static __always_inline
u8 max77696_ldo_get_comp_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    u8 comp = 0;
    max77696_ldo_read_cnfg2_reg(me, vreg, COMP, &comp);
    return comp;
}

static __always_inline
u8 max77696_ldo_get_pok_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    u8 pok = 0;
    max77696_ldo_read_cnfg2_reg(me, vreg, POK, &pok);
    return !!pok;
}

static __always_inline
int max77696_ldo_set_imon_en_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, u8 imon_en)
{
    return max77696_ldo_write_cnfg2_reg(me, vreg, IMON_EN, imon_en);
}

static __always_inline
u8 max77696_ldo_get_imon_en_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    u8 imon_en = 0;
    max77696_ldo_read_cnfg2_reg(me, vreg, IMON_EN, &imon_en);
    return !!imon_en;
}

static __always_inline
int max77696_ldo_set_ade_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, u8 ade)
{
    return max77696_ldo_write_cnfg2_reg(me, vreg, ADE, ade);
}

static __always_inline
u8 max77696_ldo_get_ade_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    u8 ade = 0;
    max77696_ldo_read_cnfg2_reg(me, vreg, ADE, &ade);
    return !!ade;
}

static __always_inline
int max77696_ldo_set_ramp_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, u8 ramp)
{
    return max77696_ldo_write_cnfg2_reg(me, vreg, RAMP, ramp);
}

static __always_inline
u8 max77696_ldo_get_ramp_bit (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg)
{
    u8 ramp = 0;
    max77696_ldo_read_cnfg2_reg(me, vreg, RAMP, &ramp);
    return !!ramp;
}

/***
 *** LDO Configuration 3
 ***/

static __always_inline
int max77696_ldo_set_l_imon_tf_bit (struct max77696_ldo *me, u8 l_imon_tf)
{
    return max77696_ldo_write_cnfg3_reg(me, L_IMON_TF, l_imon_tf);
}

static __always_inline
u8 max77696_ldo_get_l_imon_tf_bit (struct max77696_ldo *me)
{
    u8 l_imon_tf = 0;
    max77696_ldo_read_cnfg3_reg(me, L_IMON_TF, &l_imon_tf);
    return l_imon_tf;
}

static __always_inline
int max77696_ldo_set_l_imon_en_bit (struct max77696_ldo *me, u8 l_imon_en)
{
    return max77696_ldo_write_cnfg3_reg(me, L_IMON_EN, l_imon_en);
}

static __always_inline
u8 max77696_ldo_get_l_imon_en_bit (struct max77696_ldo *me)
{
    u8 l_imon_en = 0;
    max77696_ldo_read_cnfg3_reg(me, L_IMON_EN, &l_imon_en);
    return !!l_imon_en;
}

static __always_inline
int max77696_ldo_set_sbiasen_bit (struct max77696_ldo *me, u8 sbiasen)
{
    return max77696_ldo_write_cnfg3_reg(me, SBIASEN, sbiasen);
}

static __always_inline
u8 max77696_ldo_get_sbiasen_bit (struct max77696_ldo *me)
{
    u8 sbiasen = 0;
    max77696_ldo_read_cnfg3_reg(me, SBIASEN, &sbiasen);
    return !!sbiasen;
}

static __always_inline
int max77696_ldo_set_biasen_bit (struct max77696_ldo *me, u8 biasen)
{
    return max77696_ldo_write_cnfg3_reg(me, BIASEN, biasen);
}

static __always_inline
u8 max77696_ldo_get_biasen_bit (struct max77696_ldo *me)
{
    u8 biasen = 0;
    max77696_ldo_read_cnfg3_reg(me, BIASEN, &biasen);
    return !!biasen;
}

#define max77696_ldo_enable(me, vreg) \
        max77696_ldo_set_pwrmd_bit(me, vreg, LDO_POWERMODE_NORMAL)

#define max77696_ldo_disable(me, vreg) \
        max77696_ldo_set_pwrmd_bit(me, vreg, LDO_POWERMODE_OFF)

#define max77696_ldo_is_enabled(me, vreg) \
        (!!max77696_ldo_get_pwrmd_bit(me, vreg))

static int max77696_ldo_power_mode (struct max77696_ldo *me,
    struct max77696_ldo_vreg *vreg, unsigned int mode)
{
    u8 pwrmd;
    int rc;

    switch (mode) {
    case REGULATOR_MODE_FAST:
        pwrmd = LDO_POWERMODE_NORMAL;
        break;

    case REGULATOR_MODE_NORMAL:
        pwrmd = LDO_POWERMODE_NORMAL;
        break;

    case REGULATOR_MODE_IDLE:
        pwrmd = LDO_POWERMODE_LOW_POWER;
        break;

    case REGULATOR_MODE_STANDBY:
        pwrmd = LDO_POWERMODE_FORCED_LOW_POWER;
        break;

    default:
        rc = -EINVAL;
        goto out;
    }

    rc = max77696_ldo_set_pwrmd_bit(me, vreg, pwrmd);

out:
    return rc;
}

#define __rdev_name(rdev_ptr) \
        (rdev_ptr->desc->name)
#define __rdev_to_max77696_ldo(rdev_ptr) \
        ((struct max77696_ldo*)rdev_get_drvdata(rdev_ptr))
#define __rdev_to_max77696_ldo_vreg(rdev_ptr) \
        (&(__rdev_to_max77696_ldo(rdev_ptr)->vreg[rdev_ptr->desc->id]))

static int max77696_ldo_vreg_set_voltage (struct regulator_dev *rdev,
    int min_uV, int max_uV, unsigned* selector)
{
    struct max77696_ldo *me = __rdev_to_max77696_ldo(rdev);
    struct max77696_ldo_vreg *vreg = __rdev_to_max77696_ldo_vreg(rdev);
    int rc;

    __lock(me);

    dev_verbose(me->dev, "%s set_voltage(min %duV, max %duV)\n",
        __rdev_name(rdev), min_uV, max_uV);

    rc = max77696_ldo_set_tv_bit(me, vreg, min_uV);

    __unlock(me);
    return rc;
}

static int max77696_ldo_vreg_get_voltage (struct regulator_dev *rdev)
{
    struct max77696_ldo *me = __rdev_to_max77696_ldo(rdev);
    struct max77696_ldo_vreg *vreg = __rdev_to_max77696_ldo_vreg(rdev);
    int rc;

    __lock(me);

    rc = max77696_ldo_get_tv_bit(me, vreg);

    __unlock(me);
    return rc;
}

static int max77696_ldo_vreg_enable (struct regulator_dev *rdev)
{
    struct max77696_ldo *me = __rdev_to_max77696_ldo(rdev);
    struct max77696_ldo_vreg *vreg = __rdev_to_max77696_ldo_vreg(rdev);
    int rc;

    __lock(me);

    if (likely(vreg->mode)) {
        rc = max77696_ldo_power_mode(me, vreg, vreg->mode);
    } else {
        rc = max77696_ldo_enable(me, vreg);
    }

    __unlock(me);
    return rc;
}

static int max77696_ldo_vreg_disable (struct regulator_dev *rdev)
{
    struct max77696_ldo *me = __rdev_to_max77696_ldo(rdev);
    struct max77696_ldo_vreg *vreg = __rdev_to_max77696_ldo_vreg(rdev);
    int rc;

    __lock(me);

    rc = max77696_ldo_disable(me, vreg);

    __unlock(me);
    return rc;
}

static int max77696_ldo_vreg_is_enabled (struct regulator_dev *rdev)
{
    struct max77696_ldo *me = __rdev_to_max77696_ldo(rdev);
    struct max77696_ldo_vreg *vreg = __rdev_to_max77696_ldo_vreg(rdev);
    int rc;

    __lock(me);

    rc = max77696_ldo_is_enabled(me, vreg);

    __unlock(me);
    return rc;
}

static int max77696_ldo_vreg_set_mode (struct regulator_dev *rdev,
    unsigned int mode)
{
    struct max77696_ldo *me = __rdev_to_max77696_ldo(rdev);
    struct max77696_ldo_vreg *vreg = __rdev_to_max77696_ldo_vreg(rdev);
    int rc;

    __lock(me);

    if (likely(max77696_ldo_is_enabled(me, vreg))) {
        rc = max77696_ldo_power_mode(me, vreg, mode);
        if (unlikely(rc)) {
            goto out;
        }
    }

    vreg->mode = mode;

out:
    __unlock(me);
    return rc;
}

static unsigned int max77696_ldo_vreg_get_mode (struct regulator_dev *rdev)
{
    struct max77696_ldo *me = __rdev_to_max77696_ldo(rdev);
    struct max77696_ldo_vreg *vreg = __rdev_to_max77696_ldo_vreg(rdev);
    unsigned int rc;

    __lock(me);

    switch (max77696_ldo_get_pwrmd_bit(me, vreg)) {
    case LDO_POWERMODE_LOW_POWER:
        rc = REGULATOR_MODE_IDLE;
        break;

    case LDO_POWERMODE_FORCED_LOW_POWER:
        rc = REGULATOR_MODE_STANDBY;
        break;

    case LDO_POWERMODE_NORMAL:
        rc = REGULATOR_MODE_NORMAL;
        break;

    default:
        rc = 0; /* diabled */
        break;
    }

    __unlock(me);
    return rc;
}

static struct regulator_ops max77696_ldo_vreg_ops = {
    .set_voltage = max77696_ldo_vreg_set_voltage,
    .get_voltage = max77696_ldo_vreg_get_voltage,

    /* enable/disable regulator */
    .enable      = max77696_ldo_vreg_enable,
    .disable     = max77696_ldo_vreg_disable,
    .is_enabled  = max77696_ldo_vreg_is_enabled,

    /* get/set regulator operating mode (defined in regulator.h) */
    .set_mode    = max77696_ldo_vreg_set_mode,
    .get_mode    = max77696_ldo_vreg_get_mode,
};

#define LDO_VREG_DESC(_id, _name, _cnfg1_reg, _cnfg2_reg, _min, _max, _step, _suspend, _resume) \
        [MAX77696_LDO_ID_##_id] = {\
            .rdesc.name  = LDO_VREG_DESC_NAME(_name),\
            .rdesc.id    = MAX77696_LDO_ID_##_id,\
            .rdesc.ops   = &max77696_ldo_vreg_ops,\
            .rdesc.type  = REGULATOR_VOLTAGE,\
            .rdesc.owner = THIS_MODULE,\
            .min_uV      = _min,\
            .max_uV      = _max,\
            .suspend_uV  = _suspend,\
            .resume_uV   = _resume,\
            .step_uV     = _step,\
            .cnfg1_reg   = LDO_REG(_cnfg1_reg),\
            .cnfg2_reg   = LDO_REG(_cnfg2_reg),\
        }

#define VREG_DESC(id) (&(max77696_ldo_vreg_descs[id]))
static struct max77696_ldo_vreg_desc max77696_ldo_vreg_descs[LDO_NREG] = {
    LDO_VREG_DESC(L1,  "ldo1",  L01_CNFG1, L01_CNFG2,  800000, 3950000, 50000,       0,       0),
    LDO_VREG_DESC(L2,  "ldo2",  L02_CNFG1, L02_CNFG2,  800000, 3950000, 50000,       0,       0),
    LDO_VREG_DESC(L3,  "ldo3",  L03_CNFG1, L03_CNFG2,  800000, 3950000, 50000,       0,       0),
    LDO_VREG_DESC(L4,  "ldo4",  L04_CNFG1, L04_CNFG2,  800000, 2375000, 25000,       0,       0),
    LDO_VREG_DESC(L5,  "ldo5",  L05_CNFG1, L05_CNFG2,  800000, 2375000, 25000,       0,       0),
    LDO_VREG_DESC(L6,  "ldo6",  L06_CNFG1, L06_CNFG2,  800000, 3950000, 50000,       0,       0),
    LDO_VREG_DESC(L7,  "ldo7",  L07_CNFG1, L07_CNFG2,  800000, 3950000, 50000, 1800000, 3200000),
    LDO_VREG_DESC(L8,  "ldo8",  L08_CNFG1, L08_CNFG2,  800000, 2375000, 25000,       0,       0),
    LDO_VREG_DESC(L9,  "ldo9",  L09_CNFG1, L09_CNFG2,  800000, 2375000, 25000,       0,       0),
    LDO_VREG_DESC(L10, "ldo10", L10_CNFG1, L10_CNFG2, 2400000, 5550000, 50000,       0,       0),
};

static int max77696_ldo_vreg_probe (struct platform_device *pdev)
{
    struct max77696_ldo *me = platform_get_drvdata(pdev);
    struct max77696_ldo_vreg *vreg = &(me->vreg[pdev->id]);
    struct max77696_ldo_vreg_desc *desc = VREG_DESC(pdev->id);
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

static int max77696_ldo_vreg_suspend (struct platform_device *pdev, pm_message_t state)
{
    struct regulator_dev *rdev = platform_get_drvdata(pdev);
    struct max77696_ldo_vreg_desc *desc = VREG_DESC(pdev->id);

#ifdef CONFIG_FALCON
    /* don't change LDO7 voltage during hibernation suspend - taken care by fbios before FSHDN */	
    if(in_falcon()) return 0;
#endif

    if (desc->suspend_uV && (!lab126_board_is(BOARD_ID_BOURBON_WFO) &&
	    !lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C) &&
	    !lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2)) ) {
        return max77696_ldo_vreg_set_voltage (rdev, desc->suspend_uV, desc->suspend_uV, NULL);
    }

    return 0;
}

static int max77696_ldo_vreg_resume (struct platform_device *pdev)
{
    struct regulator_dev *rdev = platform_get_drvdata(pdev);
    struct max77696_ldo_vreg_desc *desc = VREG_DESC(pdev->id);
    
#ifdef CONFIG_FALCON
    /* don't change LDO7 voltage during hibernation resume - taken care by uboot */	
    if(in_falcon()) return 0;
#endif

    if (desc->resume_uV && (!lab126_board_is(BOARD_ID_BOURBON_WFO) &&
	    !lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C) &&
	    !lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2)) ) {
        return max77696_ldo_vreg_set_voltage (rdev, desc->resume_uV, desc->resume_uV, NULL);
    }

    return 0;
}

static int max77696_ldo_vreg_remove (struct platform_device *pdev)
{
    struct regulator_dev *rdev = platform_get_drvdata(pdev);
    struct max77696_ldo_vreg *vreg = __rdev_to_max77696_ldo_vreg(rdev);

    regulator_unregister(rdev);
    vreg->rdev = NULL;

    return 0;
}

static struct platform_driver max77696_ldo_vreg_driver = {
    .probe       = max77696_ldo_vreg_probe,
    .remove      = max77696_ldo_vreg_remove,
    .suspend     = max77696_ldo_vreg_suspend,
    .resume      = max77696_ldo_vreg_resume,
    .driver.name = LDO_VREG_NAME,
};

static __always_inline
void max77696_ldo_unregister_vreg_drv (struct max77696_ldo *me)
{
    platform_driver_unregister(&max77696_ldo_vreg_driver);
}

static __always_inline
int max77696_ldo_register_vreg_drv (struct max77696_ldo *me)
{
    return platform_driver_register(&max77696_ldo_vreg_driver);
}

static __always_inline
void max77696_ldo_unregister_vreg_dev (struct max77696_ldo *me, int id)
{
    /* nothing to do */
}

static __always_inline
int max77696_ldo_register_vreg_dev (struct max77696_ldo *me, int id,
    struct regulator_init_data *init_data)
{
    struct max77696_ldo_vreg *vreg = &(me->vreg[id]);
    int rc;

    vreg->pdev = platform_device_alloc(LDO_VREG_NAME, id);
    if (unlikely(!vreg->pdev)) {
        dev_err(me->dev, "failed to alloc pdev for %s.%d\n",
            LDO_VREG_NAME, id);
        rc = -ENOMEM;
        goto out_err;
    }

    platform_set_drvdata(vreg->pdev, me);
    vreg->pdev->dev.platform_data = init_data;
    vreg->pdev->dev.parent        = me->dev;

    rc = platform_device_add(vreg->pdev);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to pdev for %s.%d [%d]\n",
            LDO_VREG_NAME, id, rc);
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

static void max77696_ldo_unregister_vreg (struct max77696_ldo *me)
{
    int i;

    for (i = 0; i < LDO_NREG; i++) {
        max77696_ldo_unregister_vreg_dev(me, i);
    }

    max77696_ldo_unregister_vreg_drv(me);
}

static int max77696_ldo_register_vreg (struct max77696_ldo *me,
    struct regulator_init_data init_data[LDO_NREG])
{
    int i, rc;

    rc = max77696_ldo_register_vreg_drv(me);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to register vreg drv for %s [%d]\n",
            LDO_VREG_NAME, rc);
        goto out;
    }

    for (i = 0; i < LDO_NREG; i++) {
        dev_verbose(me->dev, "registering vreg dev for %s.%d ...\n",
            LDO_VREG_NAME, i);
        rc = max77696_ldo_register_vreg_dev(me, i, &(init_data[i]));
        if (unlikely(rc)) {
            dev_err(me->dev, "failed to register vreg dev for %s.%d [%d]\n",
                LDO_VREG_NAME, i, rc);
            goto out;
        }
    }

out:
    return rc;
}

static __devinit int max77696_ldo_probe (struct platform_device *pdev)
{
    static const char *imon_tf_str[] = {
        [MAX77696_LDO_IMON_TF_8000_OHM] = "8k ohms",
        [MAX77696_LDO_IMON_TF_4000_OHM] = "4k ohms",
        [MAX77696_LDO_IMON_TF_2000_OHM] = "2k ohms",
        [MAX77696_LDO_IMON_TF_1000_OHM] = "1k ohms",
        [MAX77696_LDO_IMON_TF_500_OHM]  = "500 ohms",
        [MAX77696_LDO_IMON_TF_250_OHM]  = "250 ohms",
        [MAX77696_LDO_IMON_TF_125_OHM]  = "125 ohms",
        [MAX77696_LDO_IMON_TF_62p5_OHM] = "62.5 ohms"
    };

    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_ldo_platform_data *pdata = dev_get_platdata(&(pdev->dev));
    struct max77696_ldo *me;
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

    /* Register linear regulators driver & device */
    rc = max77696_ldo_register_vreg(me, pdata->init_data);
    if (unlikely(rc)) {
        goto out_err_reg_vregs;
    }

    BUG_ON(chip->ldo_ptr);
    chip->ldo_ptr = me;

    /* Clear L_IMON_EN as default */
    max77696_ldo_set_l_imon_en_bit(me, 0);

    /* Set defaults given via platform data */
    max77696_ldo_set_l_imon_tf_bit(me, pdata->imon_tf);

    /* Show defaults */
    dev_dbg(me->dev, "current monitor transfer function: %s\n",
        imon_tf_str[pdata->imon_tf & 7]);

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(ldo, chip);
    return 0;

out_err_reg_vregs:
    mutex_destroy(&(me->lock));
    platform_set_drvdata(pdev, NULL);
    kfree(me);
    return rc;
}

static __devexit int max77696_ldo_remove (struct platform_device *pdev)
{
    struct max77696_ldo *me = platform_get_drvdata(pdev);

    me->chip->ldo_ptr = NULL;

    max77696_ldo_unregister_vreg(me);

    mutex_destroy(&(me->lock));
    platform_set_drvdata(pdev, NULL);
    kfree(me);

    return 0;
}

static struct platform_driver max77696_ldo_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .probe        = max77696_ldo_probe,
    .remove       = __devexit_p(max77696_ldo_remove),
};

static __init int max77696_ldo_driver_init (void)
{
    return platform_driver_register(&max77696_ldo_driver);
}

static __exit void max77696_ldo_driver_exit (void)
{
    platform_driver_unregister(&max77696_ldo_driver);
}

subsys_initcall(max77696_ldo_driver_init);
module_exit(max77696_ldo_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

/*******************************************************************************
 * EXTERNAL FUNCTIONS
 ******************************************************************************/

extern struct max77696_chip* max77696;

/***
 *** LDO Individual Configuration
 ***/

/* Power mode */
int max77696_ldo_get_power_mode (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 pwrmd = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    pwrmd = max77696_ldo_get_pwrmd_bit(me, &(me->vreg[ldo]));
    __unlock(me);

    return pwrmd;
}
EXPORT_SYMBOL(max77696_ldo_get_power_mode);

int max77696_ldo_set_power_mode (u8 ldo, u8 mode)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_ldo_set_pwrmd_bit(me, &(me->vreg[ldo]), mode);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_power_mode);

/* Target voltage */
int max77696_ldo_get_target_voltage (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 tv = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    tv = max77696_ldo_get_tv_bit(me, &(me->vreg[ldo]));
    __unlock(me);

    return tv;
}
EXPORT_SYMBOL(max77696_ldo_get_target_voltage);

int max77696_ldo_set_target_voltage (u8 ldo, u8 uV)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_ldo_set_tv_bit(me, &(me->vreg[ldo]), uV);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_target_voltage);

/* Overvoltage clamp enable */
int max77696_ldo_get_overvoltage_clamp_enable (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 ovclmp_en = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    ovclmp_en = max77696_ldo_get_ovclmp_en_bit(me, &(me->vreg[ldo]));
    __unlock(me);

    return ovclmp_en;
}
EXPORT_SYMBOL(max77696_ldo_get_overvoltage_clamp_enable);

int max77696_ldo_set_overvoltage_clamp_enable (u8 ldo, bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_ldo_set_ovclmp_en_bit(me, &(me->vreg[ldo]), !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_overvoltage_clamp_enable);

/* Auto low power mode enable */
int max77696_ldo_get_auto_low_power_mode_enable (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 alpm_en = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    alpm_en = max77696_ldo_get_alpm_en_bit(me, &(me->vreg[ldo]));
    __unlock(me);

    return alpm_en;
}
EXPORT_SYMBOL(max77696_ldo_get_auto_low_power_mode_enable);

int max77696_ldo_set_auto_low_power_mode_enable (u8 ldo, bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_ldo_set_alpm_en_bit(me, &(me->vreg[ldo]), !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_auto_low_power_mode_enable);

/* Compensation */
int max77696_ldo_get_transconductance (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 comp = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    comp = max77696_ldo_get_comp_bit(me, &(me->vreg[ldo]));
    __unlock(me);

    return comp;
}
EXPORT_SYMBOL(max77696_ldo_get_transconductance);

int max77696_ldo_set_transconductance (u8 ldo, u8 ratio)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_ldo_set_comp_bit(me, &(me->vreg[ldo]), ratio);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_transconductance);

/* Voltage okay status */
int max77696_ldo_get_votage_okay_status (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 pok = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    pok = max77696_ldo_get_pok_bit(me, &(me->vreg[ldo]));
    __unlock(me);

    return pok;
}
EXPORT_SYMBOL(max77696_ldo_get_votage_okay_status);

/* Current monitor enable */
int max77696_ldo_get_imon_enable (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 imon_en = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    if (likely(!me->imon_mode_manual)) {
        imon_en = max77696_ldo_get_imon_en_bit(me, &(me->vreg[ldo]));
    }
    __unlock(me);

    return imon_en;
}
EXPORT_SYMBOL(max77696_ldo_get_imon_enable);

int max77696_ldo_set_imon_enable (u8 ldo, bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    struct max77696_ldo_vreg *vreg;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    vreg = &(me->vreg[ldo]);

    __lock(me);

    rc = max77696_ldo_set_imon_en_bit(me, vreg, enable);
    if (unlikely(rc)) {
        goto out;
    }

    if (enable) {
        if (unlikely(bitmap_empty(me->enabled_imon_bitmap, LDO_NREG))) {
            if (likely(!me->imon_mode_manual)) {
                rc = max77696_ldo_set_l_imon_en_bit(me, 1);
                if (unlikely(rc)) {
                    goto out;
                }
            }
        }
        set_bit  (ldo, me->enabled_imon_bitmap);
    } else {
        clear_bit(ldo, me->enabled_imon_bitmap);
        if (unlikely(bitmap_empty(me->enabled_imon_bitmap, LDO_NREG))) {
            if (likely(!me->imon_mode_manual)) {
                rc = max77696_ldo_set_l_imon_en_bit(me, 0);
                if (unlikely(rc)) {
                    goto out;
                }
            }
        }
    }

out:
    __unlock(me);
    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_imon_enable);

/* Active discharge enable */
int max77696_ldo_get_active_discharge_enable (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 ade = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    ade = max77696_ldo_get_ade_bit(me, &(me->vreg[ldo]));
    __unlock(me);

    return ade;
}
EXPORT_SYMBOL(max77696_ldo_get_active_discharge_enable);

int max77696_ldo_set_active_discharge_enable (u8 ldo, bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_ldo_set_ade_bit(me, &(me->vreg[ldo]), !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_active_discharge_enable);

/* Soft-start slew rate */
int max77696_ldo_get_slew_rate (u8 ldo)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 ramp = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    ramp = max77696_ldo_get_ramp_bit(me, &(me->vreg[ldo]));
    __unlock(me);

    return ramp;
}
EXPORT_SYMBOL(max77696_ldo_get_slew_rate);

int max77696_ldo_set_slew_rate (u8 ldo, u8 slew_rate)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    if (unlikely(ldo >= LDO_NREG)) {
        return -EINVAL;
    }

    __lock(me);
    rc = max77696_ldo_set_ramp_bit(me, &(me->vreg[ldo]), slew_rate);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_slew_rate);

/***
 *** LDO Global Configuration
 ***/

/* Current monitor operating mode */
int max77696_ldo_get_imon_operating_mode (void)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 imon_op_mode;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);

    /* Autonomous mode */
    if (unlikely(!me->imon_mode_manual)) {
        imon_op_mode = MAX77696_LDO_OPERATING_MODE_AUTONOMOUS;
        goto out;
    }

    /* Manual mode */
    if (max77696_ldo_get_l_imon_en_bit(me) ||
        !bitmap_empty(me->enabled_imon_bitmap, LDO_NREG)) {
        imon_op_mode = MAX77696_LDO_OPERATING_MODE_MANUAL_ON;
    } else {
        imon_op_mode = MAX77696_LDO_OPERATING_MODE_MANUAL_OFF;
    }

out:
    __unlock(me);
    return imon_op_mode;
}
EXPORT_SYMBOL(max77696_ldo_get_imon_operating_mode);

int max77696_ldo_set_imon_operating_mode (u8 mode)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    bool imon_mode_manual, l_imon_en;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);

    switch (mode) {
    case MAX77696_LDO_OPERATING_MODE_AUTONOMOUS:
        imon_mode_manual = 0;
        l_imon_en = (bool)(!bitmap_empty(me->enabled_imon_bitmap, LDO_NREG));
        break;

    case MAX77696_LDO_OPERATING_MODE_MANUAL_OFF:
        imon_mode_manual = 1;
        l_imon_en = 0;
        break;

    case MAX77696_LDO_OPERATING_MODE_MANUAL_ON:
        imon_mode_manual = 1;
        l_imon_en = 1;
        break;

    default:
        rc = -EINVAL;
        goto out;
    }

    rc = max77696_ldo_set_l_imon_en_bit(me, (u8)l_imon_en);
    if (unlikely(rc)) {
        goto out;
    }

    me->imon_mode_manual = imon_mode_manual;

out:
    __unlock(me);
    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_imon_operating_mode);

/* Current monitor transfer function */
int max77696_ldo_get_imon_transfer_function (void)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 l_imon_tf = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);
    l_imon_tf = max77696_ldo_get_l_imon_tf_bit(me);
    __unlock(me);

    return l_imon_tf;
}
EXPORT_SYMBOL(max77696_ldo_get_imon_transfer_function);

int max77696_ldo_set_imon_transfer_function (u8 tf_val)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);
    rc = max77696_ldo_set_l_imon_tf_bit(me, tf_val);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_imon_transfer_function);

/* Bias enable with SBIAS */
int max77696_ldo_get_sbias_enable (void)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 sbiasen = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);
    sbiasen = max77696_ldo_get_sbiasen_bit(me);
    __unlock(me);

    return sbiasen;
}
EXPORT_SYMBOL(max77696_ldo_get_sbias_enable);

int max77696_ldo_set_sbias_enable (bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);
    rc = max77696_ldo_set_sbiasen_bit(me, !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_sbias_enable);

/* Bias enable */
int max77696_ldo_get_bias_enable (void)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    u8 biasen = 0;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);
    biasen = max77696_ldo_get_biasen_bit(me);
    __unlock(me);

    return biasen;
}
EXPORT_SYMBOL(max77696_ldo_get_bias_enable);

int max77696_ldo_set_bias_enable (bool enable)
{
    struct max77696_chip *chip = max77696;
    struct max77696_ldo *me;
    int rc;

    if (unlikely(!chip)) {
        pr_err("%s: max77696_chip is not ready\n", __func__);
        return -ENODEV;
    }

    me = chip->ldo_ptr;

    if (unlikely(!me)) {
        pr_err("%s: max77696_ldo is not ready\n", __func__);
        return -ENODEV;
    }

    __lock(me);
    rc = max77696_ldo_set_biasen_bit(me, !!enable);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_ldo_set_bias_enable);

