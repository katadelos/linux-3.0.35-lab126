/*
 * Copyright (C) 2012 Maxim Integrated Product
 * Jayden Cha <jayden.cha@maxim-ic.com>
 * Copyright 2012-2015 Amazon Technologies, Inc.
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
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/frontlight.h>

#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/mfd/max77696.h>
#include <max77696_registers.h>

#define DRIVER_DESC    "MAX77696 Backlight Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_BL_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#define MAX_BRIGHTNESS                      0xFFF /* 12bits resolution */
#define MIN_BRIGHTNESS                      0

#define FL_DRIVER_NAME  "frontlight"
#define FL_DEV_MINOR    162

struct max77696_bl {
	struct max77696_chip    *chip;
	struct max77696_i2c     *i2c;
	struct device           *dev;

	struct backlight_device *bd;
	int                      brightness;
	struct mutex             lock;
	int                      do_force_update;
};

struct backlight_device *backlight = NULL;

#define __get_i2c(chip)                 (&((chip)->pmic_i2c))
#define __lock(me)                      mutex_lock(&((me)->lock))
#define __unlock(me)                    mutex_unlock(&((me)->lock))

/* WLED(Backlight) Register Read/Write */
#define max77696_bl_reg_read(me, reg, val_ptr) \
	max77696_read((me)->i2c, WLED_REG(reg), val_ptr)
#define max77696_bl_reg_write(me, reg, val) \
	max77696_write((me)->i2c, WLED_REG(reg), val)
#define max77696_bl_reg_bulk_read(me, reg, dst, len) \
	max77696_bulk_read((me)->i2c, WLED_REG(reg), dst, len)
#define max77696_bl_reg_bulk_write(me, reg, src, len) \
	max77696_bulk_write((me)->i2c, WLED_REG(reg), src, len)
#define max77696_bl_reg_read_masked(me, reg, mask, val_ptr) \
	max77696_read_masked((me)->i2c, WLED_REG(reg), mask, val_ptr)
#define max77696_bl_reg_write_masked(me, reg, mask, val) \
	max77696_write_masked((me)->i2c, WLED_REG(reg), mask, val)

/* WLED(Backlight) Register Single Bit Ops */
#define max77696_bl_reg_get_bit(me, reg, bit, val_ptr) \
	({\
	 int __rc = max77696_bl_reg_read_masked(me, reg,\
		 WLED_REG_BITMASK(reg, bit), val_ptr);\
	 *(val_ptr) = WLED_REG_BITGET(reg, bit, *(val_ptr));\
	 __rc;\
	 })
#define max77696_bl_reg_set_bit(me, reg, bit, val) \
	({\
	 max77696_bl_reg_write_masked(me, reg,\
		 WLED_REG_BITMASK(reg, bit), WLED_REG_BITSET(reg, bit, val));\
	 })

static __inline int max77696_bl_enable (struct max77696_bl *me, int enable)
{
	return max77696_bl_reg_set_bit(me, LEDBST_CNTRL1, LED1EN, !!enable);
}

static __inline int max77696_bl_set (struct max77696_bl *me, int brightness)
{
	u8 buf[2];
	int rc = 0;

	brightness = min_t(int, MAX_BRIGHTNESS, brightness);
	brightness = max_t(int, MIN_BRIGHTNESS, brightness);

	if (unlikely(me->brightness == brightness && !me->do_force_update)) {
		goto out;
	}

	if (unlikely(brightness <= MIN_BRIGHTNESS)) {
		rc = max77696_bl_enable(me, 0);
		if (unlikely(rc)) {
			dev_err(me->dev, "failed to disable %d\n", rc);
			goto out;
		}
		me->brightness = MIN_BRIGHTNESS;
		goto out;
	}

	/* LED1CURRENT_1 (MSB: BIT11 ~ BIT4), LED1CURRENT_2 (LSB: BIT3 ~ BIT0) */

	buf[0] = (u8)(brightness >> 4);
	buf[1] = (u8)(brightness & 0xf);

	rc = max77696_bl_reg_bulk_write(me, LED1CURRENT_1, buf, 2);
	if (unlikely(rc)) {
		dev_err(me->dev, "LED1CURRENT write failed %d\n", rc);
		goto out;
	}

	rc = max77696_bl_enable(me, 1);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to be enabled %d\n", rc);
		goto out;
	}

	me->brightness = brightness;

out:
	me->do_force_update = 0;
	return rc;
}

static int max77696_bl_update_status (struct backlight_device *bd)
{
	struct max77696_bl *me = bl_get_data(bd);
	int brightness = bd->props.brightness;
	int rc;

	__lock(me);

	if (likely(brightness > 0)) {
		if (unlikely((bd->props.state & BL_CORE_SUSPENDED) != 0)) {
			brightness = MIN_BRIGHTNESS;
		} else if (unlikely((bd->props.state & BL_CORE_FBBLANK) != 0)) {
			__unlock(me);
			return 0;
		}
	}

	rc = max77696_bl_set(me, brightness);
	__unlock(me);
	return rc;
}

static int max77696_bl_get_brightness (struct backlight_device *bd)
{
	struct max77696_bl *me = bl_get_data(bd);
	int rc = 0;
	u8 buf[2];
	u8 enable = 0;

	__lock(me);

	max77696_bl_reg_get_bit(me, LEDBST_CNTRL1, LED1EN, &enable);
	if (enable) {
		/* LED1CURRENT_1 (MSB: BIT11 ~ BIT4), LED1CURRENT_2 (LSB: BIT3 ~ BIT0) */
		rc = max77696_bl_reg_bulk_read(me, LED1CURRENT_1, buf, 2);
		if (unlikely(rc)) {
			dev_err(me->dev, "LED1CURRENT read failed %d\n", rc);
			goto out;
		}
		rc = (int)((buf[0] << 4) | (buf[1] & 0xf));
	} else { /* FL LED source disabled */	
		rc = MIN_BRIGHTNESS;
	}

out:
	__unlock(me);
	return rc;
}

static const struct backlight_ops max77696_bl_ops = {
	.options        = BL_CORE_SUSPENDRESUME,
	.update_status  = max77696_bl_update_status,
	.get_brightness = max77696_bl_get_brightness,
};

static const struct backlight_properties max77696_bl_props = {
	.max_brightness = MAX_BRIGHTNESS,
	.type           = BACKLIGHT_RAW,
	.fb_blank       = 0,
};

static long fl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int __user *argp = (int __user *)arg; 
	int ret = -EINVAL;
	int val = 0;

	switch (cmd) {
		case FL_IOCTL_SET_INTENSITY_FORCED:
			if (get_user(val, argp))
				return -EFAULT;
			else {
				struct max77696_bl *me;
				mutex_lock(&backlight->update_lock);
				me = bl_get_data(backlight);
				backlight->props.brightness = val;
				me->do_force_update = 1;
				ret = max77696_bl_update_status(backlight);
				mutex_unlock(&backlight->update_lock);
			}
			break;
		case FL_IOCTL_SET_INTENSITY:
			if (get_user(val, argp))
				return -EFAULT;
			else {
				mutex_lock(&backlight->update_lock);
				backlight->props.brightness = val;
				ret = max77696_bl_update_status(backlight);
				mutex_unlock(&backlight->update_lock);
			}
			break;
		case FL_IOCTL_GET_INTENSITY:
			if (put_user(max77696_bl_get_brightness(backlight), argp))
				return -EFAULT;
			else
				ret = 0;
			break;
		case FL_IOCTL_GET_RANGE_MAX:
			if (put_user(MAX_BRIGHTNESS, argp))
				return -EFAULT;
			else
				ret = 0;
			break;
		default:
			break;
	}
	return ret;
} 

static ssize_t fl_misc_write(struct file *file, const char __user *buf,
		size_t count, loff_t *pos)
{   
	return 0;
}   

static ssize_t fl_misc_read(struct file *file, char __user *buf,
		size_t count, loff_t *pos)
{ 
	return 0;
}

static const struct file_operations fl_misc_fops =
{
	.owner = THIS_MODULE,
	.read  = fl_misc_read,
	.write = fl_misc_write,
	.unlocked_ioctl = fl_ioctl,
};

static struct miscdevice fl_misc_device =
{ 
	.minor = FL_DEV_MINOR,
	.name  = FL_DRIVER_NAME,
	.fops  = &fl_misc_fops,
};

static __devinit int max77696_bl_probe (struct platform_device *pdev)
{
	struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77696_backlight_platform_data *pdata = pdev->dev.platform_data;
	struct max77696_bl *me;
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

	me->bd = backlight_device_register(DRIVER_NAME, me->dev, me,
			&max77696_bl_ops, &max77696_bl_props);
	if (unlikely(IS_ERR(me->bd))) {
		rc = PTR_ERR(me->bd);
		me->bd = NULL;

		dev_err(me->dev, "failed to register backlight device [%d]\n", rc);
		goto out_err;
	}

	platform_set_drvdata(pdev, me->bd);

	/* Update current */
	me->brightness = pdata->brightness;
	me->bd->props.brightness = pdata->brightness;
	me->do_force_update = 0;
	backlight_update_status(me->bd);

	backlight = me->bd;
	if (misc_register(&fl_misc_device)) {
		dev_err(me->dev, "%s Coulnd't register device %d.\n", __FUNCTION__, FL_DEV_MINOR);
		rc = -EBUSY;
		goto out_err;
	}

	pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
	SUBDEVICE_SET_LOADED(bl, chip);
	return 0;

out_err:
	if (likely(me->bd)) {
		backlight_device_unregister(me->bd);
	}
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);
	return rc;
}

static __devexit int max77696_bl_remove (struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);
	struct max77696_bl *me = bl_get_data(bd);

	backlight_device_unregister(bd);

	misc_deregister(&fl_misc_device);

	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);

	return 0;
}

static struct platform_driver max77696_bl_driver = {
	.driver.name  = DRIVER_NAME,
	.driver.owner = THIS_MODULE,
	.probe        = max77696_bl_probe,
	.remove       = __devexit_p(max77696_bl_remove),
};

static __init int max77696_bl_init (void)
{
	return platform_driver_register(&max77696_bl_driver);
}

static __exit void max77696_bl_exit (void)
{
	platform_driver_unregister(&max77696_bl_driver);
};

module_init(max77696_bl_init);
module_exit(max77696_bl_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

