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
#include <linux/init.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include <linux/mfd/max77696-core.h>
#include <linux/regulator/max77696-ddr.h>
#include <max77696_registers.h>


#define MAX77696_MODE_NORMAL		0x10

struct max77696_vreg {
	struct max77696_ddr_pdata 	*pdata;
	struct regulator_dev		*rdev;
	u8 id;
        u8 type;
    	u8 volt_reg;
    	u8 cfg_reg;
    	u32 min_uV;
    	u32 max_uV;
    	u32 step_uV;
	u8 volt_shadow;
	u8 cfg_shadow;
    	u8 power_mode;
};

static int max77696_regulator_set_voltage(struct regulator_dev *dev, int min_uV, int max_uV);
static int max77696_regulator_get_voltage(struct regulator_dev *dev);
static int max77696_regulator_enable(struct regulator_dev *dev);
static int max77696_regulator_disable(struct regulator_dev *dev);
static int max77696_regulator_is_enabled(struct regulator_dev *dev);
static int max77696_regulator_set_mode(struct regulator_dev *dev, unsigned int power_mode);
static unsigned int max77696_regulator_get_mode(struct regulator_dev *dev);

static struct regulator_ops max77696_ldo_ops = {
    .set_voltage = max77696_regulator_set_voltage,
    .get_voltage = max77696_regulator_get_voltage,
    .enable = max77696_regulator_enable,
    .disable = max77696_regulator_disable,
    .is_enabled = max77696_regulator_is_enabled,
    .set_mode = max77696_regulator_set_mode,
    .get_mode = max77696_regulator_get_mode,
};

#define DDR_DEF(_id, _volt_reg, _min, _max, _step) \
    [_id] = { \
	   .id = _id, \
        .min_uV = _min, \
        .max_uV = _max, \
        .step_uV = _step, \
        .volt_reg = _volt_reg, \
        .power_mode = MAX77696_MODE_NORMAL, \
    }

static struct max77696_vreg max77696_ddr[MAX77696_DDR_MAX] = {
    /* Place holder for DDR settings
     */
    DDR_DEF(0, VDDQ_SET_REG, 1200000, 1800000, 18750),
};

#define VREG_DESC(_id, _name, _ops) \
	[_id] = { \
		.name = _name, \
		.id = _id, \
		.ops = _ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
	}

static struct regulator_desc max77696_regulator_desc[MAX77696_DDR_MAX] = {
    VREG_DESC(0, "max77696_ddr", &max77696_ldo_ops),
};

static int max77696_vreg_write(struct max77696_chip *chip, u16 addr, u8 val,
        u8 mask, u8 *bak)
{
	u8 reg = (*bak & ~mask) | (val & mask);

	int ret = max77696_write(chip, addr, &reg, 1);

	if (!ret)
		*bak = reg;

	return ret;
}

static int max77696_regulator_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV) 
{
    struct max77696_vreg *vreg= rdev_get_drvdata(rdev);
    struct max77696_chip *chip = dev_get_drvdata(rdev->dev.parent);
    u8 val;
    int rc;
    
    pr_info("max77696> %s: Addr=%x RequV=%d, MinuV=%d, StpuV=%d\n", __func__, chip->dev->addr, 
	min_uV, vreg->min_uV, vreg->step_uV);

    if (min_uV < vreg->min_uV || max_uV > vreg->max_uV)
        return -EDOM;
            
    val = (min_uV - vreg->min_uV) / vreg->step_uV;
    val <<= VDDQ_VDDQOUT_SFT;

    pr_info("max77696> Set volt reg val = %d\n", val);

    rc = max77696_write(chip, vreg->volt_reg, &val, 1);
    if (rc == 0)
        vreg->volt_shadow = val;

    return rc;
}

static int max77696_regulator_get_voltage(struct regulator_dev *rdev)
{
    struct max77696_vreg *vreg= rdev_get_drvdata(rdev);
    struct max77696_chip *chip = dev_get_drvdata(rdev->dev.parent);
    u8 val;
    int volt;
    int rc;
    
    rc = max77696_read(chip, vreg->volt_reg, &val, 1);
    val >>= VDDQ_VDDQOUT_SFT;

    if (rc == 0)
        vreg->volt_shadow = val;

    volt = val * vreg->step_uV + vreg->min_uV;
    
    pr_info("max77696> %s: val=%d volt=%d\n", __func__, val, volt);
    
    return volt;
}

static int max77696_regulator_enable(struct regulator_dev *rdev)
{
    int rc = 0;

    pr_info("max77696> %s: \n", __func__);

    return rc;
}

static int max77696_regulator_disable(struct regulator_dev *rdev)
{
    int rc = 0;
    
    pr_info("max77696> %s: \n", __func__);

    return rc;
}

static int max77696_regulator_is_enabled(struct regulator_dev *rdev)
{
    struct max77696_vreg *vreg= rdev_get_drvdata(rdev);
    
    pr_info("max77696> %s: id=%d\n", __func__, vreg->id);

    return (max77696_regulator_get_mode(rdev) != 0);
}

static int max77696_regulator_set_mode(struct regulator_dev *rdev, unsigned int power_mode)
{
    struct max77696_vreg *vreg= rdev_get_drvdata(rdev);
    struct max77696_chip *chip = dev_get_drvdata(rdev->dev.parent);
    int rc = 0;

    pr_info("max77696> %s: id=%d, power_mode=%d\n", __func__, vreg->id, power_mode);
            
    vreg->power_mode = power_mode;

    return rc;
}

static unsigned int max77696_regulator_get_mode(struct regulator_dev *rdev)
{
    struct max77696_vreg *vreg= rdev_get_drvdata(rdev);
    struct max77696_chip *chip = dev_get_drvdata(rdev->dev.parent);

    vreg->power_mode = 1;
    
    pr_err("max77696> %s: id=%d, mode=%d\n", __func__, vreg->id, 
	vreg->power_mode);

    return (unsigned int)vreg->power_mode;
}

static int max77696_init_regulator(struct max77696_chip *chip,
        struct max77696_vreg *vreg)
{
    	int rc = 0;

    	rc = max77696_read(chip, vreg->volt_reg, &vreg->volt_shadow, 1);
    	rc = max77696_read(chip, vreg->cfg_reg, &vreg->cfg_shadow, 1);

    	pr_err("max77696> max77696 regulator init. volt_reg=%x cfg_reg=%x, rc=%d\n",
		vreg->volt_reg, vreg->cfg_reg, rc);

	/* FIXME: Device read will fail until PMIC HW is in place.
         * Until then set rc = 0 for early driver testing.
         */
	rc = 0;

    return rc;
}

static int max77696_regulator_probe(struct platform_device *pdev)
{
    	struct regulator_desc *rdesc;
    	struct max77696_chip *chip;
    	struct max77696_vreg *vreg;
    	const char *reg_name = NULL;
    	int rc = 0;

	pr_info("max77696> regulator probe dev_id=%d\n", pdev->id);

    	if (pdev == NULL)
        	return -EINVAL;

        chip = platform_get_drvdata(pdev);
	pr_info("max77696> chip addr=%x\n", chip->dev->addr);

    	if (pdev->id >= 0 && pdev->id < MAX77696_DDR_MAX) {
        	chip = platform_get_drvdata(pdev);
		rdesc = &max77696_regulator_desc[pdev->id];
        	vreg = &max77696_ddr[pdev->id];
		vreg->pdata = pdev->dev.platform_data;
		reg_name = max77696_regulator_desc[pdev->id].name;

        	rc = max77696_init_regulator(chip, vreg);
        	if (rc)
            		goto error;

        	vreg->rdev = regulator_register(rdesc, &pdev->dev,
                	&vreg->pdata->init_data, vreg);

        	if (IS_ERR(vreg->rdev)) {
            		rc = PTR_ERR(vreg->rdev);			
			pr_err("max77696> regulator register err. %d", rc);
		}
    	} else {
        	rc = -ENODEV;
    	}

error:
    	pr_err("max77696> %s: id=%d, name=%s, rc=%d\n", __func__, pdev->id, 
		reg_name, rc);

    	return rc;
}

static int max77696_regulator_remove(struct platform_device *pdev)
{
   	struct regulator_dev *rdev = platform_get_drvdata(pdev);

    	regulator_unregister(rdev);

    	return 0;
}

static struct platform_driver max77696_regulator_driver =
{
	.probe = max77696_regulator_probe,
	.remove = __devexit_p(max77696_regulator_remove),
	.driver = {
		.name = "max77696-ddr",
		.owner = THIS_MODULE,
	},
};

static int __init max77696_regulator_init(void)
{
	pr_info("max77696> regulator init\n");

	return platform_driver_register(&max77696_regulator_driver);
}
subsys_initcall(max77696_regulator_init);

static void __exit max77696_reg_exit(void)
{
    platform_driver_unregister(&max77696_regulator_driver);
}
module_exit(max77696_reg_exit);

MODULE_DESCRIPTION("MAX77696 LPDDR2 Power Supply Driver");
MODULE_LICENSE("GPL v2 OR TBD");
MODULE_VERSION("1.0");


