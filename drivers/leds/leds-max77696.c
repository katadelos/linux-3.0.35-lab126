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

#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/mfd/max77696.h>
#include <max77696_registers.h>

#define DRIVER_DESC    "MAX77696 LED Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_LEDS_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#define LED_NLED                    MAX77696_LED_NR_LEDS

extern struct max77696_chip* max77696;

struct max77696_led {
	struct max77696_chip *chip;
	struct max77696_i2c  *i2c;
	struct device        *dev;
	struct kobject       *kobj;

	int                   id;
	struct led_classdev   led_cdev;
	bool                  manual_mode;
	int                   new_brightness;
	bool                  flashing;
	u8                    flashd;
	u8                    flashp;
	u8                    sol_brightness;
	u8                    init_state;
	struct work_struct    work;
	struct mutex          lock;
};

#define __cdev_to_max77696_led(led_cdev_ptr) \
	container_of(led_cdev_ptr, struct max77696_led, led_cdev)
#define __work_to_max77696_led(work_ptr) \
	container_of(work_ptr, struct max77696_led, work)

#define __get_i2c(chip)                  (&((chip)->pmic_i2c))
#define __lock(me)                       mutex_lock(&((me)->lock))
#define __unlock(me)                     mutex_unlock(&((me)->lock))

/* LED Register Read/Write */
#define max77696_led_reg_read(me, reg, val_ptr) \
	max77696_read((me)->i2c, LEDIND_REG(reg), val_ptr)
#define max77696_led_reg_write(me, reg, val) \
	max77696_write((me)->i2c, LEDIND_REG(reg), val)
#define max77696_led_reg_bulk_read(me, reg, dst, len) \
	max77696_bulk_read((me)->i2c, LEDIND_REG(reg), dst, len)
#define max77696_led_reg_bulk_write(me, reg, src, len) \
	max77696_bulk_write((me)->i2c, LEDIND_REG(reg), src, len)
#define max77696_led_reg_read_masked(me, reg, mask, val_ptr) \
	max77696_read_masked((me)->i2c, LEDIND_REG(reg), mask, val_ptr)
#define max77696_led_reg_write_masked(me, reg, mask, val) \
	max77696_write_masked((me)->i2c, LEDIND_REG(reg), mask, val)

/* LED Register Single Bit Ops */
#define max77696_led_reg_get_bit(me, reg, bit, val_ptr) \
	({\
	 int __rc = max77696_led_reg_read_masked(me, reg,\
		 LEDIND_REG_BITMASK(reg, bit), val_ptr);\
	 *(val_ptr) = LEDIND_REG_BITGET(reg, bit, *(val_ptr));\
	 __rc;\
	 })
#define max77696_led_reg_set_bit(me, reg, bit, val) \
	({\
	 max77696_led_reg_write_masked(me, reg,\
		 LEDIND_REG_BITMASK(reg, bit),\
		 LEDIND_REG_BITSET(reg, bit, val));\
	 })

#define __search_index_of_approxi(_table, _val) \
	({\
	 int __i;\
	 typeof(_val) __diff = (typeof(_val))(abs(_table[0] - _val));\
	 for (__i = 1; __i < ARRAY_SIZE(_table); __i++) {\
	 typeof(_val) __t = (typeof(_val))(abs(_table[__i] - _val));\
	 if (__t > __diff) break;\
	 __diff = __t;\
	 }\
	 (__i - 1);\
	 })

static __always_inline u8 __msec_to_flashd_value (unsigned long duration_ms)
{
	static const unsigned long msec_to_flashd_table[] =
	{
		0,  32,  64,  96, 128, 160, 192, 224,
		256, 288, 320, 384, 448, 512, 576, 620,
	};

	return (u8)__search_index_of_approxi(msec_to_flashd_table, duration_ms);
}

static __always_inline u8 __msec_to_flashp_value (unsigned long period_ms)
{
	static const unsigned long msec_to_flashp_table[] =
	{
		640,  960, 1280, 1600, 1920, 2240, 2560, 2880,
		3200, 3520, 3840, 4480, 5120, 6400, 7680,
	};

	return (u8)__search_index_of_approxi(msec_to_flashp_table, period_ms);
}

/* set mode - auto (default) / manual */
int max77696_led_set_manual_mode (unsigned int led_id, bool manual_mode)
{
	struct max77696_chip *chip = max77696;
	struct max77696_led *me = chip->led_ptr[led_id];
	u8 ledset_reg;
	int rc = -EINVAL;

	if (unlikely(!me)) {
		return rc;
	}

	ledset_reg = LEDIND_STATLEDSET_REG(me->id);
	rc = max77696_write_masked(me->i2c, ledset_reg, LEDIND_STATLEDSET_LEDEN_M, manual_mode);
	if (unlikely(rc)) {
		dev_err(me->dev, "STAT%dLEDSET write error [%d]\n", me->id, rc);
		goto out;
	}
	me->manual_mode = manual_mode;

out:
	return rc;
}
EXPORT_SYMBOL(max77696_led_set_manual_mode);

/* max77696_led_ctrl:
 * 		LED Manual control - ON / OFF / BLINK
 */
int max77696_led_ctrl(unsigned int led_id, int led_state) 
{
	struct max77696_chip *chip = max77696;
	struct max77696_led *me = chip->led_ptr[led_id];
	u8 ledset_reg, ledset_val, flash_reg, flash_val;
	u8 is_manual = 0;
	int rc = -EINVAL;

	if (unlikely(!me)) {
		return rc;
	}

	ledset_reg = LEDIND_STATLEDSET_REG(me->id);
	flash_reg  = LEDIND_STATFLASH_REG (me->id);

	if (led_state == MAX77696_LED_ON) {
		/* LED ON */
		flash_val = LEDIND_REG_BITSET(STATFLASH, FLASHD, LED_FLASHD_ON)|
			LEDIND_REG_BITSET(STATFLASH, FLASHP, LED_FLASHP_ON);
	} else if(led_state == MAX77696_LED_BLINK) {
		/* LED BLINK */
		flash_val = LEDIND_REG_BITSET(STATFLASH, FLASHD, LED_FLASHD_BLINK)|
			LEDIND_REG_BITSET(STATFLASH, FLASHP, LED_FLASHP_BLINK);
	} else if (led_state == MAX77696_LED_OFF) {
		/* LED OFF */
		flash_val = LEDIND_REG_BITSET(STATFLASH, FLASHD, LED_FLASHD_OFF)|
			LEDIND_REG_BITSET(STATFLASH, FLASHP, LED_FLASHP_OFF);
	} else {
		goto out;
	}

	rc = max77696_write(me->i2c, flash_reg, flash_val);
	if (unlikely(rc)) {
		dev_err(me->dev, "STAT%dFLASH write error [%d]\n", me->id, rc);
		goto out;
	}

	rc = max77696_read(me->i2c, LEDIND_STATLEDSET_REG(me->id), &ledset_val);
	if (unlikely(rc)) {
		dev_err(me->dev, "STAT%dLEDSET read error [%d]\n", me->id, rc);
		goto out;
	}
	is_manual = LEDIND_REG_BITGET(STATLEDSET, LEDEN, ledset_val);
	if (!is_manual) {
		rc = max77696_write(me->i2c, ledset_reg, (ledset_val | LEDIND_REG_BITSET(STATLEDSET, LEDEN, 1)));
		if (unlikely(rc)) {
			dev_err(me->dev, "STAT%dLEDSET write error [%d]\n", me->id, rc);
		}
	}	
out:
	return rc;
}
EXPORT_SYMBOL(max77696_led_ctrl);

#define max77696_led_schedule_work(me) \
	if (likely(!work_pending(&((me)->work)))) schedule_work(&((me)->work))

static void max77696_led_work (struct work_struct *work)
{
	struct max77696_led *me = __work_to_max77696_led(work);
	u8 ledset_reg, ledset_val;
	u8 flash_reg, flash_val;
	int rc;

	ledset_reg = LEDIND_STATLEDSET_REG(me->id);
	flash_reg  = LEDIND_STATFLASH_REG (me->id);

	__lock(me);

	ledset_val = LEDIND_REG_BITSET(STATLEDSET, LEDEN, me->manual_mode);

	if (me->new_brightness <= 0) {
		/* off led, no flashing */
		flash_val = LEDIND_REG_BITSET(STATFLASH, FLASHD, LED_FLASHD_OFF)|
			LEDIND_REG_BITSET(STATFLASH, FLASHP, LED_FLASHP_OFF);
	} else {
		/* set flashing parameters */
		if (me->flashing) {
			flash_val =
				LEDIND_REG_BITSET(STATFLASH, FLASHD, me->flashd)|
				LEDIND_REG_BITSET(STATFLASH, FLASHP, me->flashp);
		} else {
			flash_val = LEDIND_REG_BITSET(STATFLASH, FLASHD, LED_FLASHD_ON)|
				LEDIND_REG_BITSET(STATFLASH, FLASHP, LED_FLASHP_ON);
		}

		/* set brightness */
		if (me->new_brightness >= LED_MAX_BRIGHTNESS) {
			ledset_val |=
				LEDIND_REG_BITSET(STATLEDSET, LEDSET, LED_MAX_BRIGHTNESS);
		} else {
			ledset_val |=
				LEDIND_REG_BITSET(STATLEDSET, LEDSET, me->new_brightness);
		}
	}

	rc = max77696_write(me->i2c, ledset_reg, ledset_val);
	if (unlikely(rc)) {
		dev_err(me->dev, "STAT%dLEDSET write error [%d]\n", me->id, rc);
	}

	rc = max77696_write(me->i2c, flash_reg, flash_val);
	if (unlikely(rc)) {
		dev_err(me->dev, "STAT%dFLASH write error [%d]\n", me->id, rc);
	}

	__unlock(me);
	return;
}

static void max77696_led_brightness_set (struct led_classdev *led_cdev,
		enum led_brightness value)
{
	struct max77696_led *me = __cdev_to_max77696_led(led_cdev);

	__lock(me);

	if (me->flashing) {
		me->flashing = 0;
	} else {
		me->new_brightness = (int)value;
	}

	max77696_led_schedule_work(me);

	__unlock(me);
}

static int max77696_led_blink_set (struct led_classdev *led_cdev,
		unsigned long *delay_on, unsigned long *delay_off)
{
	struct max77696_led *me = __cdev_to_max77696_led(led_cdev);

	__lock(me);

	me->flashing = 1;

	if (unlikely(!delay_on || !delay_off || !(*delay_on) || !(*delay_off))) {
		me->flashd = LED_FLASHD_DEF;
		me->flashp = LED_FLASHP_DEF;
	} else {
		me->flashd = __msec_to_flashd_value(*delay_on);
		me->flashp = __msec_to_flashp_value(*delay_on + *delay_off);
	}

	max77696_led_schedule_work(me);

	__unlock(me);
	return 0;
}

static ssize_t max77696_led_manual_mode_show (struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_led *me = platform_get_drvdata(pdev);
	u8 val;
	int rc;

	__lock(me);

	rc = max77696_read(me->i2c, LEDIND_STATLEDSET_REG(me->id), &val);
	if (unlikely(rc)) {
		dev_err(dev, "STAT%dLEDSET read error [%d]\n", me->id, rc);
		goto out;
	}

	rc  = (int)snprintf(buf, PAGE_SIZE, "%u\n",
			LEDIND_REG_BITGET(STATLEDSET, LEDEN, val));

out:
	__unlock(me);
	return (ssize_t)rc;
}

static ssize_t max77696_led_manual_mode_store (struct device *dev,
		struct device_attribute *devattr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_led *me = platform_get_drvdata(pdev);

	__lock(me);

	me->manual_mode = (bool)(!!simple_strtoul(buf, NULL, 10));

	max77696_led_schedule_work(me);

	__unlock(me);
	return (ssize_t)count;
}

static ssize_t max77696_led_flash_param_show (struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_led *me = platform_get_drvdata(pdev);
	u8 val;
	int rc;

	__lock(me);

	rc = max77696_read(me->i2c, LEDIND_STATFLASH_REG(me->id), &val);
	if (unlikely(rc)) {
		dev_err(dev, "STAT%dLEDSET read error [%d]\n", me->id, rc);
		goto out;
	}

	rc = (int)snprintf(buf, PAGE_SIZE, "%u,%u,%u\n",
			LEDIND_REG_BITGET(STATFLASH, FLASHD, val),
			LEDIND_REG_BITGET(STATFLASH, FLASHP, val),
			me->flashing);

out:
	__unlock(me);
	return (ssize_t)rc;
}

static ssize_t max77696_led_flash_param_store (struct device *dev,
		struct device_attribute *devattr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_led *me = platform_get_drvdata(pdev);
	int opts[4];

	__lock(me);

	/* format: duration_val, period_val, flashing_flag */
	get_options(buf, 4, opts);

	if (unlikely(opts[0] != 3)) {
		dev_err(me->dev, "missing parameter(s)\n");
		goto out;
	}

	me->flashd   = (unsigned long)opts[1];
	me->flashp   = (unsigned long)opts[2];
	me->flashing = (bool)(!!opts[3]);

	max77696_led_schedule_work(me);

out:
	__unlock(me);
	return (ssize_t)count;
}

static ssize_t max77696_led_ctrl_store (struct device *dev,
		struct device_attribute *devattr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_led *me = platform_get_drvdata(pdev);
	bool enable;

	__lock(me);
	enable = (bool)(simple_strtoul(buf, NULL, 10));
	max77696_led_ctrl(me->id, enable);
	__unlock(me);

	return (ssize_t)count;
}

static DEVICE_ATTR(manual_mode, S_IWUSR|S_IRUGO,
		max77696_led_manual_mode_show, max77696_led_manual_mode_store);
static DEVICE_ATTR(flash_param, S_IWUSR|S_IRUGO,
		max77696_led_flash_param_show, max77696_led_flash_param_store);
static DEVICE_ATTR(ctrl, S_IWUSR,
		NULL, max77696_led_ctrl_store);

static struct attribute *max77696_led_attr[] = {
	&dev_attr_manual_mode.attr,
	&dev_attr_flash_param.attr,
	&dev_attr_ctrl.attr,
	NULL
};

static const struct attribute_group max77696_led_attr_group = {
	.attrs = max77696_led_attr,
};

static __devinit int max77696_led_probe (struct platform_device *pdev)
{
	struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77696_led_platform_data *pdata = pdev->dev.platform_data;
	struct max77696_led *me;
	int rc;
	u8 ledset_reg, flash_reg, ledset_val;

	if (unlikely(!pdata)) {
		dev_err(&(pdev->dev), "platform data is missing\n");
		return -EINVAL;
	}

	if (unlikely((unsigned)(pdev->id) >= LED_NLED)) {
		dev_err(&(pdev->dev), "invalid id(%d)\n", pdev->id);
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

	me->id             = pdev->id;
	me->manual_mode    = pdata->manual_mode;
	me->sol_brightness = pdata->sol_brightness;
	me->init_state     = pdata->init_state;
	me->new_brightness = LED_HALF / 8;	/* set to 0x1F */

	me->led_cdev.name            = pdata->info.name;
	me->led_cdev.flags           = pdata->info.flags;
	me->led_cdev.default_trigger = pdata->info.default_trigger;
	me->led_cdev.brightness_set  = max77696_led_brightness_set;
	me->led_cdev.blink_set       = max77696_led_blink_set;
	me->led_cdev.max_brightness  = LED_MAX_BRIGHTNESS;
	me->led_cdev.brightness      = LED_HALF / 8;

	INIT_WORK(&(me->work), max77696_led_work);

	rc = sysfs_create_group(me->kobj, &max77696_led_attr_group);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to create attribute group [%d]\n", rc);
		goto out_err_sysfs;
	}

	rc = led_classdev_register(me->dev, &(me->led_cdev));
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register led(%d) device [%d]\n",
				me->id, rc);
		goto out_err_reg_led;
	}

	BUG_ON(chip->led_ptr[me->id]);
	chip->led_ptr[me->id] = me;

	if(me->sol_brightness != LED_DEF_BRIGHTNESS) {
		ledset_reg = LEDIND_STATLEDSET_REG(me->id);
		rc = max77696_write_masked(me->i2c, ledset_reg, LEDIND_STATLEDSET_LEDSET_M, me->sol_brightness);
		if (unlikely(rc)) {
			dev_err(me->dev, "STAT%dLEDSET write error [%d]\n", me->id, rc);
			goto out_err_sol;
		}
	}

	if (me->init_state == LED_FLASHD_OFF) {
		/* LED OFF */
		flash_reg  = LEDIND_STATFLASH_REG (me->id);
		rc = max77696_write(me->i2c, flash_reg, 
				LEDIND_REG_BITSET(STATFLASH, FLASHD, LED_FLASHD_OFF) |
				LEDIND_REG_BITSET(STATFLASH, FLASHP, LED_FLASHP_OFF));
		if (unlikely(rc)) {
			dev_err(me->dev, "STAT%dLEDFLASH write error [%d]\n", me->id, rc);
			goto out_err_sol;
		}
	}

	if (me->manual_mode) {
		ledset_reg = LEDIND_STATLEDSET_REG(me->id);
		rc = max77696_read(me->i2c, ledset_reg, &ledset_val);
		if (unlikely(rc)) {
			dev_err(me->dev, "STAT%dLEDSET read error [%d]\n", me->id, rc);
			goto out_err_sol;
		}

		rc = max77696_write(me->i2c, ledset_reg, (ledset_val | LEDIND_REG_BITSET(STATLEDSET, LEDEN, 1)));
		if (unlikely(rc)) {
			dev_err(me->dev, "STAT%dLEDSET write error [%d]\n", me->id, rc);
			goto out_err_sol;
		}
	}

	pr_info(DRIVER_DESC" #%d "DRIVER_VERSION" Installed\n", pdev->id);

	if (pdev->id==0) {
		SUBDEVICE_SET_LOADED(led0, chip);
	} else if (pdev->id==1) {
		SUBDEVICE_SET_LOADED(led1, chip);
	}

	return 0;

out_err_sol:
	led_classdev_unregister(&(me->led_cdev));
out_err_reg_led:
	sysfs_remove_group(me->kobj, &max77696_led_attr_group);
out_err_sysfs:
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);
	return rc;
}

static __devexit int max77696_led_remove (struct platform_device *pdev)
{
	struct max77696_led *me = platform_get_drvdata(pdev);

	me->chip->led_ptr[me->id] = NULL;

	sysfs_remove_group(me->kobj, &max77696_led_attr_group);
	led_classdev_unregister(&(me->led_cdev));

	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);

	return 0;
}

static struct platform_driver max77696_led_driver = {
	.driver.name  = DRIVER_NAME,
	.driver.owner = THIS_MODULE,
	.probe        = max77696_led_probe,
	.remove       = __devexit_p(max77696_led_remove),
};

static __init int max77696_led_driver_init (void)
{
	return platform_driver_register(&max77696_led_driver);
}

static __exit void max77696_led_driver_exit (void)
{
	platform_driver_unregister(&max77696_led_driver);
}

module_init(max77696_led_driver_init);
module_exit(max77696_led_driver_exit);

/*******************************************************************************
 * EXTERNAL FUNCTIONS
 ******************************************************************************/

void max77696_led_update_changes (unsigned int led_id)
{
	struct max77696_chip *chip = max77696;
	struct max77696_led *me = chip->led_ptr[led_id];

	if (unlikely(!me)) {
		return;
	}

	__lock(me);
	max77696_led_schedule_work(me);
	__unlock(me);
}
EXPORT_SYMBOL(max77696_led_update_changes);

void max77696_led_enable_manual_mode (unsigned int led_id, bool manual_mode)
{
	struct max77696_chip *chip = max77696;
	struct max77696_led *me = chip->led_ptr[led_id];

	if (unlikely(!me)) {
		return;
	}

	__lock(me);
	me->manual_mode = manual_mode;
	max77696_led_schedule_work(me);
	__unlock(me);
}
EXPORT_SYMBOL(max77696_led_enable_manual_mode);

void max77696_led_set_brightness (unsigned int led_id,
		enum led_brightness value)
{
	struct max77696_chip *chip = max77696;
	struct max77696_led *me = chip->led_ptr[led_id];

	if (unlikely(!me)) {
		return;
	}

	__lock(me);
	me->new_brightness = (int)value;
	__unlock(me);
}
EXPORT_SYMBOL(max77696_led_set_brightness);

void max77696_led_set_blink (unsigned int led_id,
		unsigned long duration_ms, unsigned long period_ms)
{
	struct max77696_chip *chip = max77696;
	struct max77696_led *me = chip->led_ptr[led_id];

	if (unlikely(!me)) {
		return;
	}

	__lock(me);
	me->flashing = 1;
	me->flashd   = __msec_to_flashd_value(duration_ms);
	me->flashp   = __msec_to_flashp_value(period_ms  );
	__unlock(me);
}
EXPORT_SYMBOL(max77696_led_set_blink);

void max77696_led_disable_blink (unsigned int led_id)
{
	struct max77696_chip *chip = max77696;
	struct max77696_led *me = chip->led_ptr[led_id];

	if (unlikely(!me)) {
		return;
	}

	__lock(me);
	me->flashing = 0;
	__unlock(me);
}
EXPORT_SYMBOL(max77696_led_disable_blink);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

