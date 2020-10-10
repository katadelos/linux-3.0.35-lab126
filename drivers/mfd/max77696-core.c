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
#include <linux/interrupt.h>
#include <linux/i2c.h>

#include <linux/power_supply.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77696.h>

#define DRIVER_DESC    "MAX77696 Driver Core"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_CORE_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

extern int max77696_irq_init (struct max77696_chip *chip,
		struct max77696_platform_data *pdata);
extern void max77696_irq_exit (struct max77696_chip *chip);

extern int max77696_topsys_init (struct max77696_chip *chip,
		struct max77696_platform_data *pdata);
extern void max77696_topsys_exit (struct max77696_chip *chip);

static int max77696_chip_add_subdevices (struct max77696_chip *chip,
		const char *dev_name, int n_devs, void *dev_pdata, size_t dev_pdata_sz)
{
	struct mfd_cell subdev;
	struct resource no_res;
	int i, rc;

	memset(&no_res, 0x00, sizeof(no_res));

	for (i = 0; i < n_devs; i++) {
		memset(&subdev, 0x00, sizeof(struct mfd_cell));

		subdev.name           = dev_name;
		subdev.id             = i;
		subdev.platform_data  = (char*)dev_pdata + (i * dev_pdata_sz);
		subdev.pdata_size     = dev_pdata_sz;
		subdev.resources      = &no_res;
		subdev.num_resources  = 1;

		subdev.ignore_resource_conflicts = 1;

		rc = mfd_add_devices(chip->dev, 0, &subdev, 1, NULL, 0);
		if (unlikely(rc)) {
			dev_err(chip->dev, "failed to add %s.%d [%d]\n", dev_name, i, rc);
			return rc;
		}
	}

	return 0;
}

static int max77696_chip_pmic_reg_read (struct max77696_chip *chip,
		u8 addr, u16 *data)
{
	u8 buf = 0;
	int rc;

	rc = max77696_read(&(chip->pmic_i2c), addr, &buf);
	if (unlikely(rc)) {
		dev_err(chip->dev, "failed to read PMIC register %02X [%d]\n",
				addr, rc);
	}

	*data = (u16)buf;

	return rc;
}

static int max77696_chip_pmic_reg_write (struct max77696_chip *chip,
		u8 addr, u16 data)
{
	int rc;

	rc = max77696_write(&(chip->pmic_i2c), addr, (u8)data);
	if (unlikely(rc)) {
		dev_err(chip->dev, "failed to write PMIC register %02X [%d]\n",
				addr, rc);
	}

	return rc;
}

static int max77696_chip_rtc_reg_read (struct max77696_chip *chip,
		u8 addr, u16 *data)
{
	u8 buf = 0;
	int rc;

	rc = max77696_read(&(chip->rtc_i2c), addr, &buf);
	if (unlikely(rc)) {
		dev_err(chip->dev, "failed to read RTC register %02X [%d]\n",
				addr, rc);
	}

	*data = (u16)buf;

	return rc;
}

static int max77696_chip_rtc_reg_write (struct max77696_chip *chip,
		u8 addr, u16 data)
{
	int rc;

	rc = max77696_write(&(chip->rtc_i2c), addr, (u8)data);
	if (unlikely(rc)) {
		dev_err(chip->dev, "failed to write RTC register %02X [%d]\n",
				addr, rc);
	}

	return rc;
}

static int max77696_chip_uic_reg_read (struct max77696_chip *chip,
		u8 addr, u16 *data)
{
	u8 buf = 0;
	int rc;

	rc = max77696_read(&(chip->uic_i2c), addr, &buf);
	if (unlikely(rc)) {
		dev_err(chip->dev, "failed to read UIC register %02X [%d]\n",
				addr, rc);
	}

	*data = (u16)buf;

	return rc;
}

static int max77696_chip_uic_reg_write (struct max77696_chip *chip,
		u8 addr, u16 data)
{
	int rc;

	rc = max77696_write(&(chip->uic_i2c), addr, (u8)data);
	if (unlikely(rc)) {
		dev_err(chip->dev, "failed to write UIC register %02X [%d]\n",
				addr, rc);
	}

	return rc;
}

static int max77696_chip_gauge_reg_read (struct max77696_chip *chip,
		u8 addr, u16 *data)
{
	u16 buf = 0;
	int rc;

	rc = max77696_bulk_read(&(chip->gauge_i2c), addr, (u8*)(&buf), 2);
	if (unlikely(rc)) {
		dev_err(chip->dev, "failed to read GAUGE register %02X [%d]\n",
				addr, rc);
	}

	*data = __le16_to_cpu(buf);

	return rc;
}

static int max77696_chip_gauge_reg_write (struct max77696_chip *chip,
		u8 addr, u16 data)
{
	u16 buf = __cpu_to_le16(data);
	int rc;

	rc = max77696_bulk_write(&(chip->gauge_i2c), addr, (u8*)(&buf), 2);
	if (unlikely(rc)) {
		dev_err(chip->dev, "failed to write GAUGE register %02X [%d]\n",
				addr, rc);
	}

	return rc;
}

#define MAX77696_DBG_REG(module_name) \
	static u8 max77696_dbg_##module_name##_reg_addr;\
static ssize_t max77696_dbg_##module_name##_reg_addr_show (struct device *dev,\
		struct device_attribute *devattr, char *buf)\
{\
	int rc;\
	rc  = (int)snprintf(buf, PAGE_SIZE,\
			""#module_name" reg addr 0x%02X\n",\
			max77696_dbg_##module_name##_reg_addr);\
	return (ssize_t)rc;\
}\
static ssize_t max77696_dbg_##module_name##_reg_addr_store (struct device *dev,\
		struct device_attribute *devattr, const char *buf, size_t count)\
{\
	max77696_dbg_##module_name##_reg_addr = (u8)simple_strtoul(buf, NULL, 16);\
	return (ssize_t)count;\
}\
static ssize_t max77696_dbg_##module_name##_reg_data_show (struct device *dev,\
		struct device_attribute *devattr, char *buf)\
{\
	struct max77696_chip *chip = dev_get_drvdata(dev);\
	u16 data;\
	int rc, ofst;\
	rc = max77696_chip_##module_name##_reg_read(chip, \
			max77696_dbg_##module_name##_reg_addr, &data);\
	ofst = (int)snprintf(buf, PAGE_SIZE,\
			"read  "#module_name" reg addr 0x%02X ",\
			max77696_dbg_##module_name##_reg_addr);\
	ofst += (int)snprintf(buf + ofst, PAGE_SIZE,\
			"data 0x%04X ", data);\
	ofst += (int)snprintf(buf + ofst, PAGE_SIZE, "rc %d\n", rc);\
	return (ssize_t)ofst;\
}\
static ssize_t max77696_dbg_##module_name##_reg_data_store (struct device *dev,\
		struct device_attribute *devattr, const char *buf, size_t count)\
{\
	struct max77696_chip *chip = dev_get_drvdata(dev);\
	u16 data;\
	int rc;\
	data = (u16)simple_strtoul(buf, NULL, 16);\
	rc = max77696_chip_##module_name##_reg_write(chip,\
			max77696_dbg_##module_name##_reg_addr, data);\
	pr_info("\nwrite "#module_name" reg addr 0x%02X data 0x%04X rc %d\n",\
			max77696_dbg_##module_name##_reg_addr, data, rc);\
	return (ssize_t)count;\
}\
static DEVICE_ATTR(module_name##_reg_addr, S_IWUSR|S_IRUGO,\
		max77696_dbg_##module_name##_reg_addr_show,\
		max77696_dbg_##module_name##_reg_addr_store);\
static DEVICE_ATTR(module_name##_reg_data, S_IWUSR|S_IRUGO,\
		max77696_dbg_##module_name##_reg_data_show,\
		max77696_dbg_##module_name##_reg_data_store)

MAX77696_DBG_REG(pmic);
MAX77696_DBG_REG(rtc);
MAX77696_DBG_REG(uic);
MAX77696_DBG_REG(gauge);

static struct attribute *max77696_dbg_attr[] = {
	&dev_attr_pmic_reg_addr.attr,
	&dev_attr_pmic_reg_data.attr,
	&dev_attr_rtc_reg_addr.attr,
	&dev_attr_rtc_reg_data.attr,
	&dev_attr_uic_reg_addr.attr,
	&dev_attr_uic_reg_data.attr,
	&dev_attr_gauge_reg_addr.attr,
	&dev_attr_gauge_reg_data.attr,
	NULL
};

static const struct attribute_group max77696_dbg_attr_group = {
	.attrs = max77696_dbg_attr,
};

__devinit int max77696_chip_init (struct max77696_chip *chip,
		struct max77696_platform_data *pdata)
{
	int rc;

	/* Initialize interrupts */
	rc = max77696_irq_init(chip, pdata);
	if (unlikely(rc)) {
		return rc;
	}

	/* Initialize TOPSYS */
	rc = max77696_topsys_init(chip, pdata);
	if (unlikely(rc)) {
		return rc;
	}

	/* Create sysfs entries for debugging */
	if (likely(pdata->core_debug)) {
		rc = sysfs_create_group(chip->kobj, &max77696_dbg_attr_group);
		if (unlikely(rc)) {
			dev_err(chip->dev, "failed to create attribute group [%d]\n", rc);
			return rc;
		}
	}

	/* Add sub devices */

#define __ADD_SUBDEV(_name, _n_devs, _pdata_member) \
	do {\
		int __rc = max77696_chip_add_subdevices(chip, _name, _n_devs,\
				&(pdata->_pdata_member), sizeof(pdata->_pdata_member));\
		if (unlikely(__rc)) {\
			return __rc;\
		}\
	} while (0)

	__ADD_SUBDEV(MAX77696_32K_NAME, 1, osc_pdata);

#ifdef CONFIG_GPIO_MAX77696
	__ADD_SUBDEV(MAX77696_GPIO_NAME, 1, gpio_pdata);
#endif

#ifdef CONFIG_WATCHDOG_MAX77696
	__ADD_SUBDEV(MAX77696_WDT_NAME, 1, wdt_pdata);
#endif

#ifdef CONFIG_RTC_DRV_MAX77696
	__ADD_SUBDEV(MAX77696_RTC_NAME, 1, rtc_pdata);
#endif

#ifdef CONFIG_REGULATOR_MAX77696
	__ADD_SUBDEV(MAX77696_BUCK_NAME, 1, buck_pdata);
	__ADD_SUBDEV(MAX77696_LDO_NAME, 1, ldo_pdata);
	__ADD_SUBDEV(MAX77696_LSW_NAME, 1, lsw_pdata);
	__ADD_SUBDEV(MAX77696_EPD_NAME, 1, epd_pdata);
	__ADD_SUBDEV(MAX77696_VDDQ_NAME, 1, vddq_pdata);
#endif

#ifdef CONFIG_LEDS_MAX77696
	__ADD_SUBDEV(MAX77696_LEDS_NAME, MAX77696_LED_NR_LEDS, led_pdata[0]);
#endif

#ifdef CONFIG_BACKLIGHT_MAX77696
	__ADD_SUBDEV(MAX77696_BL_NAME, 1, bl_pdata);
#endif

#ifdef CONFIG_SENSORS_MAX77696
	__ADD_SUBDEV(MAX77696_ADC_NAME, 1, adc_pdata);
#endif

#ifdef CONFIG_BATTERY_MAX77696
	__ADD_SUBDEV(MAX77696_GAUGE_NAME, 1, gauge_pdata);
#endif

#ifdef CONFIG_CHARGER_MAX77696
	__ADD_SUBDEV(MAX77696_UIC_NAME, 1, uic_pdata);
	__ADD_SUBDEV(MAX77696_CHARGER_NAME, 1, chg_pdata);
	__ADD_SUBDEV(MAX77696_EH_NAME, 1, eh_pdata);
#endif

#ifdef CONFIG_INPUT_MAX77696_ONKEY
	__ADD_SUBDEV(MAX77696_ONKEY_NAME, 1, onkey_pdata);
#endif

	return 0;
}

__devexit void max77696_chip_exit (struct max77696_chip *chip)
{
	max77696_topsys_exit(chip);
	max77696_irq_exit(chip);
	sysfs_remove_group(chip->kobj, &max77696_dbg_attr_group);
	mfd_remove_devices(chip->dev);
}

/*******************************************************************************
 * LOW-LEVEL INTERFACES TO CONTROL
 ******************************************************************************/

extern struct max77696_chip* max77696;

/*
 * MAX77696_CORE_MID  0
 * MAX77696_RTC_MID   1
 * MAX77696_UIC_MID   2
 * MAX77696_GAUGE_MID 3
 */

static int (*max77696_chip_reg_read[4]) (struct max77696_chip *chip,
		u8 addr, u16 *data) =
{
	max77696_chip_pmic_reg_read,
	max77696_chip_rtc_reg_read,
	max77696_chip_uic_reg_read,
	max77696_chip_gauge_reg_read,
};

static int (*max77696_chip_reg_write[4]) (struct max77696_chip *chip,
		u8 addr, u16 data) =
{
	max77696_chip_pmic_reg_write,
	max77696_chip_rtc_reg_write,
	max77696_chip_uic_reg_write,
	max77696_chip_gauge_reg_write,
};

int max77696_chip_read (u8 module, u8 addr, u16 *data)
{
	BUG_ON(module >= ARRAY_SIZE(max77696_chip_reg_read));
	return max77696_chip_reg_read[module](max77696, addr, data);
}
EXPORT_SYMBOL(max77696_chip_read);

int max77696_chip_write (u8 module, u8 addr, u16 data)
{
	BUG_ON(module >= ARRAY_SIZE(max77696_chip_reg_write));
	return max77696_chip_reg_write[module](max77696, addr, data);
}
EXPORT_SYMBOL(max77696_chip_write);

static LIST_HEAD(max77696_chip_wakeup_voter_list);

struct max77696_chip_wakeup_voter {
    struct list_head  list;
    struct device    *dev;
};

int max77696_chip_set_wakeup (struct device *dev, bool enable)
{
    struct max77696_chip_wakeup_voter *voter;
    struct list_head *list;
    int rc = 0;

    BUG_ON(!max77696 || !dev);

    dev_dbg(dev, "%s MAX77696 to wake up the system ...\n",
        enable? "enabling" : "disabling");

    list_for_each(list, &max77696_chip_wakeup_voter_list) {
        voter = list_entry(list, struct max77696_chip_wakeup_voter, list);
        if (voter->dev == dev) {
            goto found;
        }
    }

    voter = NULL;

found:
    if (enable) {
        if (unlikely(voter)) {
            goto out;
        }

        voter = kzalloc(sizeof(*voter), GFP_KERNEL);
        if (unlikely(!voter)) {
            dev_err(dev, "out of memory (%uB requested)\n", sizeof(*voter));
            rc = -ENOMEM;
            goto out;
        }

        if (unlikely(list_empty(&max77696_chip_wakeup_voter_list))) {
            device_init_wakeup(max77696->dev, 1);
        }
        device_init_wakeup(dev, 1);

        INIT_LIST_HEAD(&(voter->list));
        voter->dev = dev;
        list_add(&(voter->list), &max77696_chip_wakeup_voter_list);

        dev_dbg(dev, "added to voter list of device wakeup\n");
    } else {
        if (unlikely(!voter)) {
            goto out;
        }

        list_del(&(voter->list));
        kfree(voter);

        device_init_wakeup(dev, 0);
        if (unlikely(list_empty(&max77696_chip_wakeup_voter_list))) {
            device_init_wakeup(max77696->dev, 0);
        }

        dev_dbg(dev, "removed from voter list of device wakeup\n");
    }

out:
    return rc;
}
EXPORT_SYMBOL(max77696_chip_set_wakeup);

/*******************************************************************************
 * INTER-ACTIONS BETWEEN SUB DEVICES
 ******************************************************************************/

#ifdef CONFIG_CHARGER_MAX77696
static bool max77696_charger_online = 0;
static bool max77696_charger_enable = 0;
static bool max77696_eh_online      = 0;
static bool max77696_eh_enable      = 0;
#endif /* CONFIG_CHARGER_MAX77696 */

#ifdef CONFIG_BATTERY_MAX77696
bool max77696_battery_online_def_cb (void)
{
	return 1;
}

#ifdef CONFIG_CHARGER_MAX77696
bool max77696_charger_online_def_cb (void)
{
	return (max77696_charger_online || max77696_eh_online);
}

bool max77696_charger_enable_def_cb (void)
{
	return (max77696_charger_enable || max77696_eh_enable);
}
#endif /* CONFIG_CHARGER_MAX77696 */
#endif /* CONFIG_BATTERY_MAX77696 */

#ifdef CONFIG_CHARGER_MAX77696
void max77696_uic_notify_def_cb (const struct max77696_uic_notify *noti)
{
	struct power_supply *psy = power_supply_get_by_name(MAX77696_PSY_CHG_NAME);

	if (!noti->vb_volt) {
		/* cable removal notification */
		return;
	}

	switch (noti->chg_type) {
		case MAX77696_UIC_CHGTYPE_USB:
		case MAX77696_UIC_CHGTYPE_SELFENUM_0P5AC:
			psy->type = POWER_SUPPLY_TYPE_USB; /* Standard Downstream Port */
			break;

		case MAX77696_UIC_CHGTYPE_CDP:
			psy->type = POWER_SUPPLY_TYPE_USB_CDP; /* Charging Downstream Port */
			break;

		case MAX77696_UIC_CHGTYPE_DEDICATED_1P5A:
		case MAX77696_UIC_CHGTYPE_APPLE_0P5AC:
		case MAX77696_UIC_CHGTYPE_APPLE_1P0AC:
		case MAX77696_UIC_CHGTYPE_APPLE_2P0AC:
		case MAX77696_UIC_CHGTYPE_OTH_0:
		case MAX77696_UIC_CHGTYPE_OTH_1:
			psy->type = POWER_SUPPLY_TYPE_USB_DCP; /* Dedicated Charging Port */
			break;
	}
}

void max77696_charger_notify_def_cb (struct power_supply *psy,
		bool online, bool enable)
{
	max77696_charger_online = online;
	max77696_charger_enable = enable;

	power_supply_changed(psy);
}

void max77696_eh_notify_def_cb (struct power_supply *psy,
		bool online, bool enable)
{
	max77696_eh_online = online;
	max77696_eh_enable = enable;

	power_supply_changed(psy);
}
#endif /* CONFIG_CHARGER_MAX77696 */

