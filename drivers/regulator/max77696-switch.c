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
#include <linux/regulator/max77696-switch.h>

#define LSW_SW0_CNTRL_REG		0x5B
#define LSW_LS0_ADE_M			0x08
#define LSW_LS0_RT_M			0x06
#define LSW_LS0_RT_SFT			1
#define LSW_OUT_LS0_M			0x01

#define LSW_SW1_CNTRL_REG		0x5C
#define LSW_LS1_ADE_M			0x08
#define LSW_LS1_RT_M			0x06
#define LSW_LS1_RT_SFT			1
#define LSW_OUT_LS1_M			0x01

#define LSW_SW2_CNTRL_REG		0x5D
#define LSW_LS2_ADE_M			0x08
#define LSW_LS2_RT_M			0x06
#define LSW_LS2_RT_SFT			1
#define LSW_OUT_LS2_M			0x01

#define LSW_SW3_CNTRL_REG		0x5E
#define LSW_LS3_ADE_M			0x08
#define LSW_LS3_RT_M			0x06
#define LSW_LS3_RT_SFT			1
#define LSW_OUT_LS3_M			0x01

#define MAX77696_MODE_NORMAL		0x10
#define MAX77696_MODE_DISABLE		0x00

#define MAX77696_VREG_TYPE_SD    	0X00
#define MAX77696_VREG_TYPE_LDO   	0X01

#define MAX77696_SD_MODE_M		0x30
#define MAX77696_SD_MODE_SHIFT   	4
#define MAX77696_LDO_MODE_M      	0xC0
#define MAX77696_LDO_MODE_SHIFT  	6

#define MAX77696_LDO_VOLT_M      	0x3F

struct max77696_vreg {
	struct max77696_lsw_pdata 	*pdata;
	struct regulator_dev		*rdev;
	u8 id;
    	u8 cfg_reg;
	u8 volt_shadow;
	u8 cfg_shadow;
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

#define LSW_DEF(_id, _cfg_reg) \
    [_id] = { \
	   .id = _id, \
        .cfg_reg = _cfg_reg, \
    }

static struct max77696_vreg max77696_regulators[MAX77696_LSW_MAX] = {
    LSW_DEF(MAX77696_LSW_ID_0, LSW_SW0_CNTRL_REG),
    LSW_DEF(MAX77696_LSW_ID_1, LSW_SW1_CNTRL_REG),
    LSW_DEF(MAX77696_LSW_ID_2, LSW_SW2_CNTRL_REG),
    LSW_DEF(MAX77696_LSW_ID_3, LSW_SW3_CNTRL_REG),
};

#define VREG_DESC(_id, _name, _ops) \
	[_id] = { \
		.name = _name, \
		.id = _id, \
		.ops = _ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
	}

static struct regulator_desc max77696_regulator_desc[MAX77696_LSW_MAX] = {
    VREG_DESC(MAX77696_LSW_ID_0, "max77696_sw0", &max77696_ldo_ops),
    VREG_DESC(MAX77696_LSW_ID_1, "max77696_sw1", &max77696_ldo_ops),
    VREG_DESC(MAX77696_LSW_ID_2, "max77696_sw2", &max77696_ldo_ops),
    VREG_DESC(MAX77696_LSW_ID_3, "max77696_sw3", &max77696_ldo_ops),
};

static int max77696_regulator_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV) 
{
    struct max77696_chip *chip = dev_get_drvdata(rdev->dev.parent);
    int rc = 0;
    
    pr_info("max77696> %s: not applicable for Load Switch %d\n", __func__, 
	chip->dev->addr);

    return rc;
}

static int max77696_regulator_get_voltage(struct regulator_dev *rdev)
{
    pr_info("max77696> %s: not applicable for load switch.\n", __func__);
    
    return 0;
}

static int max77696_regulator_enable(struct regulator_dev *rdev)
{
    int rc = -EDOM;

    pr_info("max77696> %s: \n", __func__);

    rc = max77696_regulator_set_mode(rdev, MAX77696_MODE_NORMAL);

    return rc;
}

static int max77696_regulator_disable(struct regulator_dev *rdev)
{
    int rc = -EDOM;
    
    pr_info("max77696> %s: \n", __func__);

    rc = max77696_regulator_set_mode(rdev, MAX77696_MODE_DISABLE);

    return rc;
}

static int max77696_regulator_is_enabled(struct regulator_dev *rdev)
{
    struct max77696_vreg *vreg= rdev_get_drvdata(rdev);
    
    pr_info("max77696> %s: id=%d\n", __func__, vreg->id);

    return (max77696_regulator_get_mode(rdev) != 0);
}

static int max77696_regulator_set_mode(struct regulator_dev *rdev, 
    unsigned int power_mode)
{
    struct max77696_vreg *vreg= rdev_get_drvdata(rdev);
    int rc = 0;

    pr_info("max77696> %s: id=%d, default for lsw\n", __func__, vreg->id);

    return rc;
}

static unsigned int max77696_regulator_get_mode(struct regulator_dev *rdev)
{
    struct max77696_vreg *vreg= rdev_get_drvdata(rdev);

    pr_info("max77696> %s: id=%d, default for lsw\n", __func__, vreg->id);

    return 0;
}

static int max77696_init_regulator(struct max77696_chip *chip,
        struct max77696_vreg *vreg)
{
    	int rc = 0;

    	rc = max77696_read(chip, vreg->cfg_reg, &vreg->cfg_shadow, 1);

    	pr_err("max77696> max77696 regulator init. cfg_reg=%x, rc=%d\n",
		vreg->cfg_reg, rc);

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

    	if (pdev->id >= 0 && pdev->id < MAX77696_LSW_MAX) {
        	chip = platform_get_drvdata(pdev);
		rdesc = &max77696_regulator_desc[pdev->id];
        	vreg = &max77696_regulators[pdev->id];
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
		.name = "max77696-switch",
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

MODULE_DESCRIPTION("MAX77696 Load Switch Driver");
MODULE_LICENSE("GPL v2 OR TBD");
MODULE_VERSION("1.0");


