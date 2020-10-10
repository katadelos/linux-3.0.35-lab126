/*
 * Copyright 2014-2016 Amazon.com, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/sysfs.h>
#include <linux/sysdev.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/power/soda.h>
#include <mach/boardid.h>
#include <linux/mfd/max77696.h>
#include <linux/mfd/max77696-events.h>
#include <linux/time.h>
#include <llog.h>
#include <linux/power/soda_device.h>


struct max77696_socbatt_eventdata socbatt_eventdata;
EXPORT_SYMBOL(socbatt_eventdata);

static u8 soda_chg_regaddr = 0;
static u8 soda_fg_regaddr = 0;
atomic_t ext_charger_atomic_detection = ATOMIC_INIT(0);
atomic_t soda_dock_atomic_detection = ATOMIC_INIT(0);
int soda_battery_capacity = 0;
int boost_ctrl_debug = 0; 
atomic_t soda_usb_charger_connected = ATOMIC_INIT(0);
atomic_t soda_docked = ATOMIC_INIT(SODA_STATE_UNDOCKED);
bool soda_vbus_enable = 0;
bool soda_soft_boost_sts = SODA_BOOST_STS_DISABLED;
bool force_green_led = false;
EXPORT_SYMBOL(soda_battery_capacity);
EXPORT_SYMBOL(soda_usb_charger_connected);
EXPORT_SYMBOL(soda_vbus_enable);
EXPORT_SYMBOL(soda_soft_boost_sts);
EXPORT_SYMBOL(force_green_led);
static bool soda_batt_check_reqd = 1;
static bool soda_force_i2c_data = 0;
volatile static bool soda_sda_mode = SODA_SDA_OPS_DATA;
volatile static bool battery_skist = BATTERY_SODA;
volatile static bool battery_authenticated = false;
volatile static bool battery_temp_error = false;
volatile static bool force_charger_detection = true;
volatile static int soda_uvlo_err_cnt = 0;
volatile static int soda_if_err_cnt = 0;
volatile static int soda_i2c_reset_cnt = 0;
static bool soda_uvlo_event_sent = false;
static bool soda_if_ok_event_sent = false;
static bool soda_if_err_event_sent = false;
static bool charge_full_flag = false;
static bool charger_suspended = false;
static bool soda_chg_prefast_trans_ctrl = SODA_CHG_PRE_FAST_CTRL_AUTO;
static bool soda_chg_enable = SODA_CHG_ENABLE;
static bool soda_chg_enable_src_ctrl = SODA_CHG_ENABLE_CTRL_PIN;
static bool soda_chg_force_thermal_check = false;
static bool soda_chg_hot_sl_volt_comp = SODA_CHG_HOT_SL_FV_COMP_ON;
static int soda_led_green_state = SODA_LED_OFF;
static int soda_led_amber_state = SODA_LED_OFF;
static bool soda_boot_init = true;
static u8 soda_chg_prechg_current = SODA_CHG_CFG_1C_PRE_CC_100MA;
static u8 soda_chg_fastchg_current = SODA_CHG_CFG_1C_FAST_CC_300MA;
static u8 soda_chg_float_volt = SODA_CHG_CFG_1E_FLOAT_VOLT_4P35V;
static u8 soda_chg_power = SODA_CHG_CMD_41_USBAC_LP;
static int soda_init_pins(struct soda_drvdata *me);
static void soda_free_pins(struct soda_drvdata *me);
static int soda_sda_state_configure(struct soda_drvdata *me, int soda_ops_state);
static void soda_sw_boost_ctrl(struct soda_drvdata *me, int enable);
static void soda_vbus_control(struct soda_drvdata *me, int enable);
static void soda_i2c_data_mode_control(struct soda_drvdata *me, int enable);
static void soda_uvlo_event (struct soda_drvdata *me);
static void soda_interface_event (struct soda_drvdata *me, bool if_sts);
static void soda_interface_warning (struct soda_drvdata *me, bool if_sts);
static void soda_uvlo_warning (struct soda_drvdata *me);
static int soda_chg_ctrl_vbus(struct soda_drvdata *me, u8 enable );
static int soda_tp1_flag = 0;	/* test point - tp1 flag */
static int soda_tp2_flag = 0;	/* test point - tp2 flag */
static int soda_tp3_flag = 0;	/* test point - tp3 flag */
static int soda_tp4_flag = 0;	/* test point - tp4 flag */
static int soda_tp5_flag = 0;	/* test point - tp5 flag */
static bool soda_i2c_tp_flag = false;	/* i2c test flag */
static bool soda_debug_data_mode = false;
static pmic_event_callback_t sw_press_event;
static char battery_unique_id[SODA_FG_BAT_UNIQUEID_LEN]="";
static char battery_auth[SODA_FG_BAT_AUTH_LEN]="";
static struct i2c_board_info soda_chg_i2c_board_info = {
	I2C_BOARD_INFO(SODA_CHG_PSY_NAME, SODA_CHG_I2C_ADDR),
};

static struct i2c_board_info soda_fg_i2c_board_info = {
	I2C_BOARD_INFO(SODA_FG_PSY_NAME, SODA_FG_I2C_ADDR),
};

static struct soda_metric_data soda_metric_info;
static struct workqueue_struct *soda_monitor_queue;

extern bool wario_onkey_press_skip;
extern int wario_battery_capacity;
extern int wario_battery_current;
extern int wario_battery_valid;
extern u8 wario_uic_chgtyp;
extern int gpio_soda_ext_chg_detect(unsigned gpio);
extern void gpio_soda_ctrl(unsigned gpio, int enable);
extern void soda_config_sda_line(int i2c_sda_enable);
extern bool soda_i2c_reset(void);
extern int gpio_soda_dock_detect(unsigned gpio);
extern int max77696_uic_is_otg_connected(void);
extern int max77696_uic_force_charger_detection(void);
extern struct max77696_chip* max77696;
extern int max77696_led_set_manual_mode(unsigned int led_id, bool manual_mode);
extern int max77696_led_ctrl(unsigned int led_id, int led_state);
extern int subscribe_socbatt_max(void);
extern int subscribe_socbatt_min(void);
extern void unsubscribe_socbatt_max(void);
extern void unsubscribe_socbatt_min(void);
extern void pmic_soda_connects_usbchg_handler(void);
extern void pmic_soda_connects_dock_handler(void);
extern void pmic_soda_connects_init(unsigned ext_chg_gpio_, unsigned sda_gpio_);

static enum power_supply_property soda_chg_psy_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property soda_fg_psy_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_STATUS,
};

static enum power_supply_property soda_boost_psy_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

bool soda_charger_docked(void)
{
	return (bool)atomic_read(&soda_docked);
}
EXPORT_SYMBOL(soda_charger_docked);

static struct soda_drvdata* g_soda_dd;

static ssize_t soda_chg_regoff_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "0x%x\n", soda_chg_regaddr);
	return (ssize_t)rc;
}

static ssize_t soda_chg_regoff_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	unsigned int val = 0;
	if (sscanf(buf, "%x", &val) <= 0) {
		printk(KERN_ERR "Could not store soda chg register offset\n");
		return -EINVAL;
	}
	soda_chg_regaddr = (u8)val;
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_chg_regoff, S_IWUSR|S_IRUGO, soda_chg_regoff_show, soda_chg_regoff_store);

static ssize_t soda_chg_regval_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	u8 val = 0;
	int rc;

	soda_i2c_read(&(me->soda_chg_i2c), soda_chg_regaddr, &val);
	rc = (int)sprintf(buf, "SODA CHG REG_ADDR=0x%x : REG_VAL=0x%x\n", soda_chg_regaddr,val);
	return (ssize_t)rc;
}

static ssize_t soda_chg_regval_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	unsigned int val = 0;
	if (sscanf(buf, "%x", &val) <= 0) {
		printk(KERN_ERR "Could not store SODA CHG register value\n");
		return -EINVAL;
	}
	soda_i2c_write(&(me->soda_chg_i2c), soda_chg_regaddr, (u8*)&val);
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_chg_regval, S_IWUSR|S_IRUGO, soda_chg_regval_show, soda_chg_regval_store);

static ssize_t soda_fg_battery_is_skist (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;

	if (!atomic_read(&soda_docked)) {
		rc = (int)sprintf(buf, "%d\n", -1);
		return (ssize_t)rc;
	}

	if (battery_skist)
		rc = (int)sprintf(buf, "%d\n", BATTERY_SKIST);
	else
		rc = (int)sprintf(buf, "%d\n",BATTERY_SODA);
	
	return (ssize_t)rc;
	
}
static DEVICE_ATTR(soda_fg_battery_sl, S_IRUGO, soda_fg_battery_is_skist, NULL);

static ssize_t soda_fg_battery_uniqueid_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;

	if (!atomic_read(&soda_docked)) {
		rc = (int)sprintf(buf, "%d\n", -1);
		return (ssize_t)rc;
	}

	rc = (int)sprintf(buf, "%s\n", battery_unique_id);
	
	return (ssize_t)rc;
}

static DEVICE_ATTR(soda_fg_battery_uniqueid, S_IRUGO, soda_fg_battery_uniqueid_show, NULL);

static ssize_t soda_fg_battery_auth_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	
	if (!atomic_read(&soda_docked)) {
		rc = (int)sprintf(buf, "%d\n", -1);
		return (ssize_t)rc;
	}

	rc = (int)sprintf(buf, "%s\n", battery_auth);

	return (ssize_t)rc;
}

static DEVICE_ATTR(soda_fg_battery_auth, S_IRUGO, soda_fg_battery_auth_show, NULL);


static ssize_t soda_fg_regoff_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "0x%x\n", soda_fg_regaddr);
	return (ssize_t)rc;
}

static ssize_t soda_fg_regoff_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	unsigned int val = 0;

	if (sscanf(buf, "%x", &val) <= 0) {
		printk(KERN_ERR "Could not store soda fg register offset\n");
		return -EINVAL;
	}

	soda_fg_regaddr = (u8)val;
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_fg_regoff, S_IWUSR|S_IRUGO, soda_fg_regoff_show, soda_fg_regoff_store);

static ssize_t soda_fg_regval_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	
	u16 val = 0;
	int rc;

	soda_i2c_bulk_read(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	rc = (int)sprintf(buf, "SODA FG REG_ADDR=0x%x : REG_VAL=0x%x\n", soda_fg_regaddr,val);
	return (ssize_t)rc;
}

static ssize_t soda_fg_regval_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	unsigned int val = 0;
	if (sscanf(buf, "%x", &val) <= 0) {
		printk(KERN_ERR "Could not store soda fg register value\n");
		return -EINVAL;
	}
	soda_i2c_bulk_write(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_fg_regval, S_IWUSR|S_IRUGO, soda_fg_regval_show, soda_fg_regval_store);

static ssize_t soda_i2c_sda_pu_ctrl_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not drive soda i2c_sda_pu gpio\n");
		return -EINVAL;
	}
	__lock_soda(me);
	gpio_soda_ctrl(me->i2c_sda_pu_gpio, val);	
	__unlock_soda(me);
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_i2c_sda_pu_ctrl, S_IWUSR, NULL, soda_i2c_sda_pu_ctrl_store);

static ssize_t soda_boost_ctrl_debug_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	return (ssize_t)sprintf(buf, "BOOST CTRL DEBUG FLAG is %d\n", boost_ctrl_debug);
}

static ssize_t soda_boost_ctrl_debug_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = -1;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not toggle debug flag for Boost control\n");
		return -EINVAL;
	}
	boost_ctrl_debug = val;
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_boost_ctrl_debug, S_IWUSR, soda_boost_ctrl_debug_show, soda_boost_ctrl_debug_store);

char *print_socbatt_subscribestate[] =
{
	[SOCBATT_MIN_SUBSCRIBED] = "SOCMIN Subscribed (SOCMAX Unsubscribed)",
	[SOCBATT_MAX_SUBSCRIBED] = "SOCMAX Subscribed (SOCMIN Unsubscribed)",
	[UNSUBSCRIBED_ALL] = "SOCMIN & SOCMAX Both Unsubscribed"
};

static ssize_t soda_socbatt_subscribe_state_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	return (ssize_t)sprintf(buf, "SOC Battery current Subscription state "
	"is: %s\n",print_socbatt_subscribestate[atomic_read(&socbatt_eventdata.subscribe_state)]);
}
static DEVICE_ATTR(socbatt_subscribe_state, S_IWUSR, soda_socbatt_subscribe_state_show, NULL);

static ssize_t soda_boost_ctrl_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", soda_soft_boost_sts);
	return (ssize_t)rc;
}

static ssize_t soda_boost_ctrl_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not drive soda boost gpio\n");
		return -EINVAL;
	}

	if(!boost_ctrl_debug) {
		if(wario_battery_capacity >= SYS_HI_SOC_THRESHOLD && (val > 0)) {
			printk(KERN_ERR "Could not Enable Boost because SOC Battery"
			"cap %d is > %d\n", wario_battery_capacity, SYS_HI_SOC_THRESHOLD);
			return -EINVAL;
		}
		if(wario_battery_capacity < SYS_LO_SOC_THRESHOLD && (val <= 0)) {
			printk(KERN_ERR "Could not Disable Boost because SOC Battery"
			"cap %d is < %d\n", wario_battery_capacity, SYS_LO_SOC_THRESHOLD);
			return -EINVAL;
		}
	}

	__lock_soda_boost(me);
	soda_sw_boost_ctrl(me, val);
	__unlock_soda_boost(me);
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_boost_ctrl, S_IWUSR|S_IRUGO, soda_boost_ctrl_show, soda_boost_ctrl_store);

static ssize_t soda_otg_sw_ctrl_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not drive soda otg_sw gpio\n");
		return -EINVAL;
	}
	__lock_soda(me);
	gpio_soda_ctrl(me->otg_sw_gpio, val);
	__unlock_soda(me);
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_otg_sw_ctrl, S_IWUSR, NULL, soda_otg_sw_ctrl_store);

static ssize_t soda_vbus_enable_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", soda_vbus_enable);
	return (ssize_t)rc;
}

static ssize_t soda_vbus_enable_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not drive soda vbus_en gpio\n");
		return -EINVAL;
	}
	__lock_soda(me);
	soda_vbus_control(me, ((val > 0) ? SODA_VBUS_ON : SODA_VBUS_OFF));
	__unlock_soda(me);
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_vbus_enable, S_IWUSR |S_IRUGO, soda_vbus_enable_show, soda_vbus_enable_store);

static ssize_t soda_usb_charger_conn_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", atomic_read(&soda_usb_charger_connected));
	return (ssize_t)rc;
}
static DEVICE_ATTR(soda_usb_charger_conn, S_IRUGO, soda_usb_charger_conn_show, NULL);

static ssize_t soda_dock_sts_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", atomic_read(&soda_docked));
	return (ssize_t)rc;
}
static DEVICE_ATTR(soda_dock_sts, S_IRUGO, soda_dock_sts_show, NULL);

static ssize_t soda_fg_battery_valid_id_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	if (atomic_read(&soda_docked)) {
		rc = (int)sprintf(buf, "%d\n", battery_authenticated);
	} else {
		rc = (int)sprintf(buf, "%d\n", -1);
	}
	return (ssize_t)rc;
}
static DEVICE_ATTR(soda_fg_battery_valid_id, S_IRUGO, soda_fg_battery_valid_id_show, NULL);

static ssize_t soda_i2c_sda_force_data_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", soda_force_i2c_data);
	return (ssize_t)rc;
}

static ssize_t soda_i2c_sda_force_data_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not drive soda vbus_en gpio\n");
		return -EINVAL;
	}
	__lock_soda(me);
	soda_i2c_data_mode_control(me, ((val > 0) ? SODA_I2C_SDA_ENABLE: SODA_I2C_SDA_DISABLE));
	__unlock_soda(me);
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_i2c_sda_force_data, S_IWUSR |S_IRUGO, soda_i2c_sda_force_data_show, soda_i2c_sda_force_data_store);

static ssize_t soda_i2c_sda_mode_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc = -EINVAL;
	if (soda_sda_mode) 
		rc = (int)sprintf(buf, "INTR\n");
	else 
		rc = (int)sprintf(buf, "DATA\n");
	return (ssize_t)rc;
}
static DEVICE_ATTR(soda_i2c_sda_mode, S_IRUGO, soda_i2c_sda_mode_show, NULL);

#ifdef DEVELOPMENT_MODE
static ssize_t soda_tp1_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not set soda tp1 flag\n");
		return -EINVAL;
	}
	soda_tp1_flag = val;
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_tp1, S_IWUSR, NULL, soda_tp1_store);

static ssize_t soda_tp2_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not set soda tp2 flag\n");
		return -EINVAL;
	}
	soda_tp2_flag = val;
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_tp2, S_IWUSR, NULL, soda_tp2_store);

static ssize_t soda_tp3_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not set soda tp3 flag\n");
		return -EINVAL;
	}
	soda_tp3_flag = val;
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_tp3, S_IWUSR, NULL, soda_tp3_store);

static ssize_t soda_tp4_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not set soda tp4 flag\n");
		return -EINVAL;
	}
	soda_tp4_flag = val;
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_tp4, S_IWUSR, NULL, soda_tp4_store);

static ssize_t soda_tp5_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not set soda tp5 flag\n");
		return -EINVAL;
	}
	soda_tp5_flag = val;
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_tp5, S_IWUSR, NULL, soda_tp5_store);


static ssize_t soda_i2c_tp_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", soda_i2c_tp_flag);
	return (ssize_t)rc;
}

static ssize_t soda_i2c_tp_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not set soda i2c_tp flag\n");
		return -EINVAL;
	}

	if (val > 0)
		soda_i2c_tp_flag = true;
	else
		soda_i2c_tp_flag = false;

	return (ssize_t)count;
}
static DEVICE_ATTR(soda_i2c_tp, S_IWUSR |S_IRUGO, soda_i2c_tp_show, soda_i2c_tp_store);

static ssize_t soda_debug_data_mode_show (struct device *dev, struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", soda_debug_data_mode);
	return (ssize_t)rc;
}

static ssize_t soda_debug_data_mode_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not set soda_debug_data_mode\n");
		return -EINVAL;
	}

	if (val > 0) 
		soda_debug_data_mode= true;
	else 
		soda_debug_data_mode = false;
		
	return (ssize_t)count;
}
static DEVICE_ATTR(soda_debug_data_mode, S_IWUSR |S_IRUGO, soda_debug_data_mode_show, soda_debug_data_mode_store);

static ssize_t soda_i2c_reset_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	int val = 0;
	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not do soda i2c reset\n");
		return -EINVAL;
	}

	if (val > 0) {
		if (soda_i2c_reset())
			printk(KERN_INFO "soda_i2c_reset: succeeded\n");
	} else 
		printk(KERN_ERR "the value should be greater than 0\n");

	return (ssize_t)count;
}
static DEVICE_ATTR(soda_i2c_reset, S_IWUSR, NULL, soda_i2c_reset_store);

static ssize_t soda_uvlo_trigger_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	int val = 0;

	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not do soda i2c reset\n");
		return -EINVAL;
	}

	if (val > 0) 
		soda_uvlo_event (me);

	return (ssize_t)count;
}
static DEVICE_ATTR(soda_uvlo_trigger, S_IWUSR, NULL, soda_uvlo_trigger_store);

static ssize_t soda_if_warn_trigger_store (struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	struct soda_drvdata *me = dev_get_drvdata(dev);
	int val = 0;

	if (sscanf(buf, "%d", &val) <= 0) {
		printk(KERN_ERR "Could not do soda i2c reset\n");
		return -EINVAL;
	}

	if (val > 0) 
		soda_interface_event(me, SODA_I2C_IF_GOOD);
	else 
		soda_interface_event(me, SODA_I2C_IF_BAD);

	return (ssize_t)count;
}
static DEVICE_ATTR(soda_if_warn_trigger, S_IWUSR, NULL, soda_if_warn_trigger_store);

#endif

static struct attribute *soda_std_attr[] = {
	&dev_attr_soda_chg_regoff.attr,
	&dev_attr_soda_chg_regval.attr,
	&dev_attr_soda_fg_regoff.attr,
	&dev_attr_soda_fg_regval.attr,
	&dev_attr_soda_i2c_sda_pu_ctrl.attr,
	&dev_attr_socbatt_subscribe_state.attr,
	&dev_attr_soda_boost_ctrl.attr,
	&dev_attr_soda_boost_ctrl_debug.attr,
	&dev_attr_soda_otg_sw_ctrl.attr,
	&dev_attr_soda_vbus_enable.attr,
	&dev_attr_soda_usb_charger_conn.attr,
	&dev_attr_soda_dock_sts.attr,
	&dev_attr_soda_fg_battery_valid_id.attr,
	&dev_attr_soda_i2c_sda_force_data.attr,
	&dev_attr_soda_i2c_sda_mode.attr,
#ifdef DEVELOPMENT_MODE
	&dev_attr_soda_tp1.attr,
	&dev_attr_soda_tp2.attr,
	&dev_attr_soda_tp3.attr,
	&dev_attr_soda_tp4.attr,
	&dev_attr_soda_tp5.attr,
	&dev_attr_soda_i2c_tp.attr,
	&dev_attr_soda_i2c_reset.attr,
	&dev_attr_soda_debug_data_mode.attr,
	&dev_attr_soda_uvlo_trigger.attr,
	&dev_attr_soda_if_warn_trigger.attr,
#endif
	&dev_attr_soda_fg_battery_uniqueid.attr,
	&dev_attr_soda_fg_battery_auth.attr,
	&dev_attr_soda_fg_battery_sl.attr,
	NULL
};

static const struct attribute_group soda_attr_group = {
	.attrs = soda_std_attr,
};

static void soda_i2c_setsda(void *data, int state)
{
	struct soda_drvdata *me = data;
	if (state) {
		gpio_direction_output(me->sda_gpio, 1);
		gpio_set_value(me->sda_gpio, 1);
	} else {
		gpio_direction_output(me->sda_gpio, 0);
		gpio_set_value(me->sda_gpio, 0);
	}
}

static void soda_i2c_setscl(void *data, int state)
{
	struct soda_drvdata *me = data;
	if (state) {
		gpio_direction_output(me->scl_gpio, 1);
		gpio_set_value(me->scl_gpio, 1);
	} else {
		gpio_direction_output(me->scl_gpio, 0);
		gpio_set_value(me->scl_gpio, 0);
	}
}

static int soda_i2c_getsda(void *data)
{
	struct soda_drvdata *me = data;
	gpio_direction_input(me->sda_gpio);
	return gpio_get_value(me->sda_gpio);
}

static int soda_i2c_getscl(void *data)
{
	struct soda_drvdata *me = data;
	gpio_direction_input(me->scl_gpio);
	return gpio_get_value(me->scl_gpio);
}

/* Reading from Sequential Registers */
static int soda_i2c_seq_read (struct soda_i2c *me,
		u8 addr, u8 *dst, u16 len)
{
	struct i2c_client *client = me->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg[2];
	int rc;

	msg[0].addr   = client->addr;
	msg[0].flags  = client->flags & I2C_M_TEN;
	msg[0].len    = 1;
	msg[0].buf    = (char*)(&addr);

	msg[1].addr   = client->addr;
	msg[1].flags  = client->flags & I2C_M_TEN;
	msg[1].flags |= I2C_M_RD;
	msg[1].len    = len;
	msg[1].buf    = (char*)dst;

	rc = i2c_transfer(adap, msg, 2);

	/* If everything went ok (i.e. 2 msg transmitted), return 0,
	   else error code. */
	return (rc == 2) ? 0 : rc;
}

/* Reading from a Single Register */
static int soda_i2c_single_read (struct soda_i2c *me, u8 addr, u8 *val)
{
	return soda_i2c_seq_read(me, addr, val, 1);
}

/* Writing to Sequential Registers */
static int soda_i2c_seq_write (struct soda_i2c *me,
		u8 addr, const u8 *src, u16 len)
{
	struct i2c_client *client = me->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg[1];
	u8 buf[len + 1];
	int rc;

	buf[0] = addr;
	memcpy(&buf[1], src, len);

	msg[0].addr  = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len   = len + 1;
	msg[0].buf   = (char*)buf;

	rc = i2c_transfer(adap, msg, 1);

	/* If everything went ok (i.e. 1 msg transmitted), return 0,
	   else error code. */
	return (rc == 1) ? 0 : rc;
}

/* Writing to a Single Register */
static int soda_i2c_single_write (struct soda_i2c *me, u8 addr, u8 *val)
{
	return soda_i2c_seq_write(me, addr, val, 1);
}

static void soda_sw_boost_ctrl(struct soda_drvdata *me, int enable)
{
	if (enable > 0) {
		gpio_soda_ctrl(me->boost_ctrl_gpio, 0);
		soda_soft_boost_sts = SODA_BOOST_STS_ENABLED;

		printk(KERN_INFO "KERNEL: I soda::boost enabled\n");
		/* since Boost is enabled, subscribe for Battery HI */
		atomic_set(&socbatt_eventdata.override, SOCBATT_MAXSUBSCRIBE_TRIGGER);
		schedule_delayed_work(&socbatt_eventdata.socbatt_event_work,
			msecs_to_jiffies(SOCBATT_EVTHDL_DELAY));
	} else {
		gpio_soda_ctrl(me->boost_ctrl_gpio, 1);
		soda_soft_boost_sts = SODA_BOOST_STS_DISABLED;

		printk(KERN_INFO "KERNEL: I soda::boost disabled\n");
		/* since Boost is disabled, subscribe for Battery Low */
		atomic_set(&socbatt_eventdata.override, SOCBATT_MINSUBSCRIBE_TRIGGER);
		schedule_delayed_work(&socbatt_eventdata.socbatt_event_work,
			msecs_to_jiffies(SOCBATT_EVTHDL_DELAY));
	}
	return;
}

//wrapper with the global soda_drvdata
void soda_boost_enable(int en, int ping)
{
	char *envp[] = { en ? "SODA=boost_lock": "SODA=boost_unlock", NULL };
	if(g_soda_dd) {
		soda_sw_boost_ctrl(g_soda_dd, en);
		if(ping) {
			kobject_uevent_env(g_soda_dd->kobj, KOBJ_CHANGE, envp);
			printk(KERN_INFO "Sent boost_lock uevent to powerd\n");
		}
	}
}

void soda_otg_gpio_control(int en)
{
	if(g_soda_dd)
		gpio_soda_ctrl(g_soda_dd->otg_sw_gpio, en);
}

void soda_otg_vbus_output(int en, int ping)
{	
	if(en) {
		soda_boost_enable(en, ping);
		msleep(SODA_OTG_VBUS_DELAY_MS);
		if(g_soda_dd) {
			gpio_soda_ctrl(g_soda_dd->vbus_en_gpio, en);
			msleep(SODA_OTG_VBUS_DELAY_MS);
			gpio_soda_ctrl(g_soda_dd->otg_sw_gpio, en);
			msleep(SODA_OTG_VBUS_READY_MS);
		}
	} else {
		if(g_soda_dd) {
			gpio_soda_ctrl(g_soda_dd->otg_sw_gpio, en);
			gpio_soda_ctrl(g_soda_dd->vbus_en_gpio, en);
		}
		soda_boost_enable(en, ping);
	}

}
EXPORT_SYMBOL(soda_otg_vbus_output);

static int soda_fg_update_cycle_count (struct soda_drvdata *me, bool force, bool *updated)
{
	u16 buf;
	int rc = 0, cyc_cnt = me->soda_fg_cyc_cnt;

	if (unlikely(!force && __is_timestamp_expired(me, soda_fg_cyc_cnt_timestamp))) {
		goto out;
	}

	rc = soda_gauge_reg_read_word(me, CYCCNT, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "CYCLECNT read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, soda_fg_cyc_cnt_timestamp);

	me->soda_fg_cyc_cnt = __u16_to_intval(buf);

	*updated |= (bool)(me->soda_fg_cyc_cnt != cyc_cnt);
	dev_noise(me->dev, "(%u) CYCLECNT %d\n", *updated, me->soda_fg_cyc_cnt);

out:
	return rc;
}

static int soda_fg_update_lmd (struct soda_drvdata *me, bool force, bool *updated)
{
	u16 buf;
	int rc = 0, full_chg_cap = me->soda_fg_lmd;

	if (unlikely(!force && __is_timestamp_expired(me, soda_fg_lmd_timestamp))) {
		goto out;
	}

	rc = soda_gauge_reg_read_word(me, FCC, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "LDM read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, soda_fg_lmd_timestamp);

	/* LMD in mAH */
	me->soda_fg_lmd = __u16_to_intval(buf);

	*updated |= (bool)(me->soda_fg_lmd != full_chg_cap);
	dev_noise(me->dev, "(%u) LMD %3dmAH\n", *updated, me->soda_fg_lmd);

out:
	return rc;
}

static int soda_fg_update_nac (struct soda_drvdata *me, bool force, bool *updated)
{
	u16 buf;
	int rc = 0, availcap = me->soda_fg_nac;

	if (unlikely(!force && __is_timestamp_expired(me, soda_fg_nac_timestamp))) {
		goto out;
	}

	rc = soda_gauge_reg_read_word(me, NAC, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "NAC read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, soda_fg_nac_timestamp);

	/* NAC in mAH */
	me->soda_fg_nac = __u16_to_intval(buf);

	*updated |= (bool)(me->soda_fg_nac != availcap);
	dev_noise(me->dev, "(%u) NAC %3dmAH\n", *updated, me->soda_fg_nac);

out:
	return rc;
}

static int soda_fg_update_soc (struct soda_drvdata *me, bool force, bool *updated)
{
	u16 buf;
	int rc = 0, rsoc = me->soda_fg_soc;

	if (unlikely(!force && __is_timestamp_expired(me, soda_fg_soc_timestamp))) {
		goto out;
	}

	rc = soda_gauge_reg_read_word(me, SOC, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "SOC read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, soda_fg_soc_timestamp);

	me->soda_fg_soc = (int)(__u16_to_intval(buf));
	soda_battery_capacity = me->soda_fg_soc;

	*updated |= (bool)(me->soda_fg_soc != rsoc);
	dev_noise(me->dev, "(%u) SOC %3d%%\n", *updated, me->soda_fg_soc);

out:
	return rc;
}

static int soda_fg_update_temp (struct soda_drvdata *me, bool force, bool *updated)
{
	u16 buf;
	int rc = 0;
	int temp_f = me->soda_fg_temp_f;

	if (unlikely(!force && __is_timestamp_expired(me, soda_fg_temp_timestamp))) {
		goto out;
	}

	rc = soda_gauge_reg_read_word(me, TEMP, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "TEMP read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, soda_fg_temp_timestamp);

	/* temp in 0.1 deg K */
	me->soda_fg_temp_c = ((__u16_to_intval(buf) / 10 ) - 273);	    /* Kelvin to Celsius */
	me->soda_fg_temp_f = (((me->soda_fg_temp_c * 9) / 5) + 32);	    /* Celsius to Fahrenheit */

	*updated |= (bool)(me->soda_fg_temp_f != temp_f);
	dev_noise(me->dev, "(%u) Temp  %4d F\n", *updated, me->soda_fg_temp_f);

out:
	return rc;
}

static int soda_fg_update_avgcurrent (struct soda_drvdata *me, bool force, bool *updated)
{
	u16 buf;
	int rc = 0, avgcurrent = me->soda_fg_current;

	if (unlikely(!force && __is_timestamp_expired(me, soda_fg_current_timestamp))) {
		goto out;
	}

	rc = soda_gauge_reg_read_word(me, AVGCURRENT, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "AVGCURRENT read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, soda_fg_current_timestamp);

	/* current in mA */
	me->soda_fg_current  = __s16_to_intval(buf);

	*updated |= (bool)(me->soda_fg_current!= avgcurrent);
	dev_noise(me->dev, "(%u) AVG_CURRENT %3dmA\n", *updated, me->soda_fg_current);

out:
	return rc;
}

static int soda_fg_update_voltage (struct soda_drvdata *me, bool force, bool *updated)
{
	u16 buf;
	int rc = 0, volt = me->soda_fg_volt;

	if (unlikely(!force && __is_timestamp_expired(me, soda_fg_volt_timestamp))) {
		goto out;
	}

	rc = soda_gauge_reg_read_word(me, VOLT, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "VOLT read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, soda_fg_volt_timestamp);

	/* voltage in mV */
	me->soda_fg_volt = __u16_to_intval(buf);

	*updated |= (bool)(me->soda_fg_volt != volt);
	dev_noise(me->dev, "(%u) VOLT  %4dmV\n", *updated, me->soda_fg_volt);

out:
	return rc;
}


static int soda_gauge_get_prop_status (struct soda_drvdata *me)
{
	int rc = POWER_SUPPLY_STATUS_NOT_CHARGING;

	if (soda_battery_capacity >= SODA_BATT_FULL_CAP) {
		rc = POWER_SUPPLY_STATUS_FULL;
		goto exit;
	}

	if (atomic_read(&soda_usb_charger_connected)) {
		if (soda_vbus_enable) 
			rc = POWER_SUPPLY_STATUS_CHARGING;
	} else {
		if(soda_soft_boost_sts) 
			rc = POWER_SUPPLY_STATUS_DISCHARGING;
	}

exit:
	return rc;
}

static int soda_gauge_get_prop_cycle_count (struct soda_drvdata *me)
{
	return me->soda_fg_cyc_cnt;   /* unit counts */
}

static int soda_gauge_get_prop_charge_full (struct soda_drvdata *me)
{
	return me->soda_fg_lmd;     /* unit mAh */
}

static int soda_gauge_get_prop_charge_now (struct soda_drvdata *me)
{
	return me->soda_fg_nac;     /* unit mAh */
}

static int soda_gauge_get_prop_capacity (struct soda_drvdata *me)
{
	return me->soda_fg_soc;      /* unit % */
}

static int soda_gauge_get_prop_temp (struct soda_drvdata *me)
{
	return me->soda_fg_temp_f;   /* unit Fahrenheit */
}

static int soda_gauge_get_prop_current (struct soda_drvdata *me)
{
	return me->soda_fg_current;   /* unit mA */
}

static int soda_gauge_get_prop_voltage (struct soda_drvdata *me)
{
	return me->soda_fg_volt;     /* unit mV */
}

static int soda_charger_get_property (struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct soda_drvdata *me = __psy_to_soda_charger(psy);
	int rc = 0;

	__lock_soda_chg(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_PRESENT:
			/* usb external charger status - connect/disconnect */
			val->intval = (int) atomic_read(&soda_usb_charger_connected);
			break;
		case POWER_SUPPLY_PROP_ONLINE:
			/* soda battery charging status */
			val->intval = (int) soda_vbus_enable;
			break;
		default:
			rc = -EINVAL;
			break;
	}

	__unlock_soda_chg(me);
	return rc;
}

static int soda_charger_set_property (struct power_supply *psy,
		enum power_supply_property psp, const union power_supply_propval *val)
{
	struct soda_drvdata *me = __psy_to_soda_charger(psy);
	int rc = 0;

	if (!atomic_read(&soda_docked)) {
		return -EINVAL;
	}

	__lock_soda_chg(me);

	switch (psp) {
		/* TODO */
		case POWER_SUPPLY_PROP_CURRENT_MAX:
			break;

		default:
			rc = -EINVAL;
	}

	__unlock_soda_chg(me);

	return rc;
}

static int soda_charger_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	struct soda_drvdata *me = __psy_to_soda_charger(psy);
	int rc = 0;

	__lock_soda_chg(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_CURRENT_MAX:
			rc = 1;
			break;
		default:
			break;
	}

	__unlock_soda_chg(me);
	return rc;
}

static int soda_battery_get_property (struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct soda_drvdata *me = __psy_to_soda_battery(psy);
	int rc = 0;

	if (!atomic_read(&soda_docked)) {
		return -ENODEV;
	}

	__lock_soda_fg(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = soda_gauge_get_prop_voltage(me);
			break;

		case POWER_SUPPLY_PROP_CURRENT_AVG:
			val->intval = soda_gauge_get_prop_current(me);
			break;

		case POWER_SUPPLY_PROP_TEMP:
		   val->intval = soda_gauge_get_prop_temp(me);
		   break;

		case POWER_SUPPLY_PROP_CAPACITY:  /* SOC (%) */
			val->intval = soda_gauge_get_prop_capacity(me);
			break;

		case POWER_SUPPLY_PROP_CHARGE_NOW: /* Nominal Available Charge */
			val->intval = soda_gauge_get_prop_charge_now(me);
			break;

		case POWER_SUPPLY_PROP_CHARGE_FULL: /* Last Measured Discharge */
			val->intval = soda_gauge_get_prop_charge_full(me);
			break;

		case POWER_SUPPLY_PROP_CYCLE_COUNT:
			val->intval = soda_gauge_get_prop_cycle_count(me);
			break;

		case POWER_SUPPLY_PROP_STATUS:
		   val->intval = soda_gauge_get_prop_status(me);
		   break;

		default:
			rc = -EINVAL;
			break;
	}

	__unlock_soda_fg(me);
	return rc;
}

static int soda_boost_get_property (struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct soda_drvdata *me = __psy_to_soda_boost(psy);
	int rc = 0;

	__lock_soda_boost(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = (int) soda_soft_boost_sts;
			break;
		default:
			rc = -EINVAL;
			break;
	}

	__unlock_soda_boost(me);
	return rc;
}

static int soda_boost_set_property (struct power_supply *psy,
		enum power_supply_property psp, const union power_supply_propval *val)
{
	struct soda_drvdata *me = __psy_to_soda_boost(psy);
	int rc = 0;

	__lock_soda_boost(me);

	switch (psp) {
		/* Disable or Enable boost */
		case POWER_SUPPLY_PROP_ONLINE:
			soda_sw_boost_ctrl(me, val->intval);
			break;

		default:
			rc = -EINVAL;
	}

	__unlock_soda_boost(me);
	return rc;
}

static int soda_boost_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	struct soda_drvdata *me = __psy_to_soda_boost(psy);
	int rc = 0;

	__lock_soda_boost(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			rc = 1;
			break;
		default:
			break;
	}

	__unlock_soda_boost(me);
	return rc;
}

/* soda_fg_get_battery_uniqueid(me)
 * Read the battery unique ID
 *    retuen val:   0 -- battery unique id read is OK
 *                  other values  -- battery unique id read is failed 
*/
static int soda_fg_get_battery_uniqueid (struct soda_drvdata *me)
{
	unsigned int val = 0;
	int i;
	int rc = 0;

	if (!atomic_read(&soda_docked)) {
		printk(KERN_ERR "%s: soda is not docked\n",__func__);
		rc = -ENODEV;
		goto out;
	}

	soda_fg_regaddr = SODA_FG_BAT_DATA_FLASH_BLOCK;
	val = SODA_FG_BAT_DATA_BLOCK_A;
	rc = soda_i2c_bulk_write(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);

	if (unlikely(rc)) {/* -ENXIO */
		pr_debug("%s: bb i2c wr error [%d]\n",__func__,rc);
		goto out;
	}

	for (i = 0; i < SODA_FG_BAT_UNIQUEID_LEN-1; i++) {
		soda_fg_regaddr = SODA_FG_BAT_ID_BASE + i;
		rc = soda_i2c_bulk_read(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
		if (unlikely(rc)) { /* -ENXIO */
			pr_debug("%s: bb i2c wr error [%d]\n",__func__,rc);
			goto out;
			}
		val &=0xFF;
		battery_unique_id[i] = (char)val;
		printk(KERN_INFO "bat_id[%d] ==%c\n",i, battery_unique_id[i]);
	}

	battery_unique_id[i] = '\0';

	printk(KERN_INFO "soda battery id = %s\n",battery_unique_id);

out:
	return rc;

}

/* soda_fg_get_battery_auth(me)
  * check if the battery is valid battery
  *    retuen val:   0 (SODA_BATTERY_ID_VALID) -- Valid battery
  *                  1 (SODA_BATTERY_ID_INVALID)-- invalid battery
  *                  negative values -- others errors
*/
static int soda_fg_get_battery_auth (struct soda_drvdata *me)
{
	unsigned int val = 0;
	int i = 0;
	int rc = SODA_BATTERY_ID_VALID;

	if (!atomic_read(&soda_docked)) {
		printk(KERN_ERR "%s: soda is not docked\n",__func__);
		rc = -ENODEV;
		goto out;
	}

	soda_fg_regaddr = SODA_FG_BAT_DATA_FLASH_BLOCK;
	val = SODA_FG_BAT_DATA_BLOCK_A;
	rc = soda_i2c_bulk_write(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	if (unlikely(rc)) {/* -ENXIO */
		pr_debug("%s: bb i2c write error [%d]\n",__func__,rc);
		goto out;
	}

	soda_fg_regaddr = SODA_FG_BAT_AUTH_2;
	rc = soda_i2c_bulk_read(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	if (unlikely(rc)) {/* -ENXIO */
		pr_debug("%s: bb i2c read error [%d]\n",__func__,rc);
		goto out;
	}
	val &=0xFF;
	battery_auth[i] = (char)val;
	printk(KERN_INFO "auth[%d] ==%c\n",i, (char)val);
	if (battery_auth[i] != 'T') {
		rc = SODA_BATTERY_ID_INVALID;
		goto out;
	}
	i++;

	soda_fg_regaddr = SODA_FG_BAT_AUTH_3;
	rc = soda_i2c_bulk_read(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	if (unlikely(rc)) {/* -ENXIO */
		pr_debug("%s: bb i2c read error [%d]\n",__func__,rc);
		goto out;
	}
	val &=0xFF;
	battery_auth[i] = (char)val;
	printk(KERN_INFO "auth[%d] ==%c\n",i, (char)val);
	if (battery_auth[i] != 'A') {
		rc = SODA_BATTERY_ID_INVALID;
		goto out;
	}
	i++;

	soda_fg_regaddr = SODA_FG_BAT_AUTH_4;
	rc = soda_i2c_bulk_read(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	if (unlikely(rc)) {/* -ENXIO */
		pr_debug("%s: bb i2c read error [%d]\n",__func__,rc);
		goto out;
	}
	val &=0xFF;
	battery_auth[i] = (char)val;
	printk(KERN_INFO "auth[%d] ==%c\n",i, (char)val);
	if (battery_auth[i] != 'S') {
		rc = SODA_BATTERY_ID_INVALID;
		goto out;
	}
	i++;

	soda_fg_regaddr = SODA_FG_BAT_AUTH_5;
	rc = soda_i2c_bulk_read(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	if (unlikely(rc)) {/* -ENXIO */
		pr_debug("%s: bb i2c read error [%d]\n",__func__,rc);
		goto out;
	}
	val &=0xFF;
	battery_auth[i] = (char)val;
	printk(KERN_INFO "auth[%d] ==%c\n",i, (char)val);
	if (battery_auth[i] != 'O') {
		rc = SODA_BATTERY_ID_INVALID;
		goto out;
	}
	i++;

	soda_fg_regaddr = SODA_FG_BAT_AUTH_6;
	rc = soda_i2c_bulk_read(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	if (unlikely(rc)) {/* -ENXIO */
		pr_debug("%s: bb i2c read error [%d]\n",__func__,rc);
		goto out;
	}
	val &=0xFF;
	battery_auth[i] = (char)val;
	printk(KERN_INFO "auth[%d] ==%c\n",i, (char)val);
	if (battery_auth[i] != 'D') {
		rc = SODA_BATTERY_ID_INVALID;
		goto out;
	}
	i++;

	battery_auth[i] = '\0';

	printk(KERN_INFO "soda battery authentication = %s\n",battery_auth);

	if (rc == SODA_BATTERY_ID_VALID)  
		battery_authenticated = true;

out:
	return rc;
}

/* soda_battery_check_skist (me)
  *    retuen val:   0 -- SODA
  *                  1 -- SKIST
  *                  others for error
*/
static int soda_battery_check_skist(struct soda_drvdata *me)
{
	unsigned int val = 0;
	char skist;
	int rc;

	if (!atomic_read(&soda_docked)) {
		printk(KERN_ERR "%s: battery is not docked\n",__func__);
		rc = -ENODEV;
		goto error;
	}

	soda_fg_regaddr = SODA_FG_BAT_DATA_FLASH_BLOCK;
	val = SODA_FG_BAT_DATA_BLOCK_B;
	rc = soda_i2c_bulk_write(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	if (unlikely(rc)) { /* -ENXIO */
		pr_debug("%s: bb i2c wr error [%d]\n",__func__,rc);
		goto error;
	}

	soda_fg_regaddr = SODA_FG_BAT_SKIST;
	rc = soda_i2c_bulk_read(&(me->soda_fg_i2c), soda_fg_regaddr, (u8*)&val, 2);
	if (unlikely(rc)) { /* -ENXIO */
		pr_debug("%s: bb i2c wr error [%d]\n",__func__,rc);
		goto error;
	}
	val &=0xFF;
	skist = (char)val;

	battery_skist = ((skist == 'S') ? BATTERY_SKIST: BATTERY_SODA);

	rc = battery_skist;

	if (battery_skist)
		printk(KERN_INFO "KERNEL: I soda::battery type=skist\n");
	else
		printk(KERN_INFO "KERNEL: I soda::battery type=soda\n");

error:
	return rc;

}

/* check charge source is DCP 1A or greater
 * return:
 *      1: high power chargers, DCP, Apple 1 and Apple 2
 *      2: low power chargers, PC host 500mA chargers
 *      0: other chargers
 */
static int soda_is_dcp_connected (void)
{
	if ((wario_uic_chgtyp == MAX77696_UIC_CHGTYPE_DEDICATED_1P5A) ||
		(wario_uic_chgtyp == MAX77696_UIC_CHGTYPE_APPLE_1P0AC) ||
		(wario_uic_chgtyp == MAX77696_UIC_CHGTYPE_APPLE_2P0AC)) {
		return WALL_CHARGER;
	} else {
		if ((wario_uic_chgtyp == MAX77696_UIC_CHGTYPE_SELFENUM_0P5AC) ||
			(wario_uic_chgtyp == MAX77696_UIC_CHGTYPE_USB) ||
			(wario_uic_chgtyp == MAX77696_UIC_CHGTYPE_APPLE_0P5AC))
			return USB_CHARGER;
	}
	return 0;
}

static int soda_led_control(int state)
{
	int rc = 0;

	if (state == LED_GREEN_ON) { 
		if (soda_led_green_state != SODA_LED_ON) {
			/* Turn ON green led - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_ON);
			if (unlikely(rc)) {
				goto exit;
			}
			soda_led_green_state = SODA_LED_ON;
		} 
	} else if (state == LED_GREEN_OFF) { 
		if (soda_led_green_state != SODA_LED_OFF) {
			/* Turn OFF green led - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
			if (unlikely(rc)) {
				goto exit;
			}
			soda_led_green_state = SODA_LED_OFF;
		} 
	} else if (state == LED_GREEN_BLINK) {
		if (soda_led_green_state != SODA_LED_BLINK) {
			/* BLINK green LED - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_BLINK);
			if (unlikely(rc)) {
				goto exit;
			}
			soda_led_green_state = SODA_LED_BLINK;
		}	
	} else if (state == LED_AMBER_ON) {
		if (soda_led_amber_state != SODA_LED_ON) {
			/* TURN ON amber LED - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_ON);
			if (unlikely(rc)) {
				goto exit;
			}
			soda_led_amber_state = SODA_LED_ON;
		}
	} else if (state == LED_AMBER_OFF) {
		if (soda_led_amber_state != SODA_LED_OFF) {
			/* TURN OFF amber LED - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_OFF);
			if (unlikely(rc)) {
				goto exit;
			}
			soda_led_amber_state = SODA_LED_OFF;
		}
	} else if (state == LED_AMBER_BLINK) {
		if (soda_led_amber_state != SODA_LED_BLINK) {
			/* BLINK amber LED - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_BLINK);
			if (unlikely(rc)) {
				goto exit;
			}
			soda_led_amber_state = SODA_LED_BLINK;
		}	
	} 

exit:
	return rc;
}

static void soda_led_blinking_handler(struct work_struct * work)
{
	/* Let LED blinking right after the battery is docked */
	soda_led_control(LED_AMBER_BLINK);
	msleep(SODA_LED_BLINK_MS);
	soda_led_control(LED_AMBER_OFF);
}

static void soda_interface_event (struct soda_drvdata *me, bool if_sts)
{
	if (if_sts) {
		char *envp[] = { "SODA=interface_ok", NULL };
		printk(KERN_INFO "KERNEL: I soda::i2c interface access is ok\n");
		kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
	} else {
		char *envp[] = { "SODA=interface_error", NULL };
		printk(KERN_INFO "KERNEL: I soda::i2c interface access is bad\n");
		kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
	}
	return;
}

static void soda_interface_warning (struct soda_drvdata *me, bool if_sts)
{
	if (if_sts) {
		if (!soda_if_ok_event_sent) {
			soda_interface_event(me, if_sts);
			soda_if_ok_event_sent = true;
		}
	} else {
		if (!soda_if_err_event_sent) {
			soda_interface_event(me, if_sts);
			soda_if_err_event_sent = true;
		}
	}
	return;
}

static void soda_uvlo_event (struct soda_drvdata *me)
{
	char *envp[] = { "SODA=uvlo_battery", NULL };
	printk(KERN_INFO "KERNEL: I soda::uvlo battery warning\n");
	kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
	return;
}

static void soda_uvlo_warning (struct soda_drvdata *me)
{
	if (!soda_uvlo_event_sent) {
		soda_uvlo_event (me);
		soda_uvlo_event_sent = true;
	} else {
		pr_debug("%s: soda uvlo warning alreaddy sent \n",__func__);
	}
	return;
}

static int soda_chk_fg_i2c_sts (struct soda_drvdata *me)
{
	int rc = 0;
	bool updated = 0;

	rc = soda_fg_update_voltage(me, 1, &updated);
	if (unlikely(rc)) {		/* -ENXIO */
		pr_debug("%s: soda voltage failed [%d]\n",__func__, rc);
		return SODA_I2C_STS_FAIL;
	}

	return SODA_I2C_STS_PASS;
}

static int soda_chk_chg_i2c_sts (struct soda_drvdata *me)
{
	u8 chip_rev = 0;
	int rc = 0;

	rc = soda_i2c_read(&(me->soda_chg_i2c), SODA_CHG_CMD_4F_REG, &chip_rev);
	if (unlikely(rc)) { 	/* -ENXIO */
		pr_debug("%s: bb i2c rd error [%d]\n",__func__,rc);
		return SODA_I2C_STS_FAIL;
	}

	return SODA_I2C_STS_PASS;
}

static void soda_vbus_control(struct soda_drvdata *me, int enable)
{
	bool updated = 0;

	/* enable vbus */
	if (enable) {
		if (!soda_vbus_enable) {
			char *envp[] = { "SODA=vbus_on", NULL };
			gpio_soda_ctrl(me->vbus_en_gpio, 1);
			soda_vbus_enable = 1;
			kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
			printk(KERN_INFO "KERNEL: I soda::vbus enabled\n");
			updated = 1;
		} 
	} else {
		/* disable vbus */
		if (soda_vbus_enable) {
			char *envp[] = { "SODA=vbus_off", NULL };
			gpio_soda_ctrl(me->vbus_en_gpio, 0);
			soda_vbus_enable = 0;
			kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
			printk(KERN_INFO "KERNEL: I soda::vbus disabled\n");
			updated = 1;
		}
	}

	if (likely(updated)) {
		power_supply_changed(&(me->soda_chg_psy));
	}

	return;
}

static void soda_i2c_data_mode_control(struct soda_drvdata *me, int enable)
{
	if (enable) {
		/* Force SDA into data mode (static) */
		soda_force_i2c_data = 1;
		printk(KERN_INFO "%s: soda i2c data mode \n",__func__);
	} else {
		/* Rollback SDA into normal operation mode (dynamic - data & intr) */
		soda_force_i2c_data = 0;
		printk(KERN_INFO "%s: soda i2c dynamic mode \n",__func__);
	}
	return;
}

static int soda_chg_ctrl_vbus(struct soda_drvdata *me, u8 enable)
{
	int rc = 0;
	u8 chg_enable = enable;

	if (enable) 
		soda_vbus_control(me, SODA_VBUS_ON);		/* enable vbus */
	else
		soda_vbus_control(me, SODA_VBUS_OFF);		/* disable vbus */
	
	rc = soda_charger_reg_set_bit(me, CMD_42, CHARGE_ENABLE, &chg_enable);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG CMD_42 write error [%d]\n", rc);
		return -EIO;
	}
	return rc;
}

static int soda_chg_thermal_check (struct soda_drvdata *me)
{
	int rc = 0;
	u8 chg_ctrl_mode = 0, pre_cc_val = 0, pre_chg_en = 0, chg_ctrl_lock = SODA_CHG_UNLOCK;
	u8 hot_sl_fvc = 0, chg_float_volt = 0, chg_en_src = 0, chg_enable = 0;
	u8 fast_cc_val = 0, usbin_icl = 0;
	u8 chg_stat_cfg = 0;
	u8 chg_rechg_threshold = 1;
	u8 chg_current_power;
	int charger_type;

	if (!soda_vbus_enable && !battery_temp_error)
		return rc;

	rc =  soda_charger_reg_set_bit(me, CMD_40, VOLATILE, &chg_ctrl_lock);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG CMD_40 write error [%d]\n", rc);
		goto out;
	}

	if (soda_chg_force_thermal_check) {
		rc = soda_charger_reg_read(me, CFG_17, &chg_stat_cfg);
		if (unlikely(rc)) {
			dev_err(me->dev, "CHG CFG_17 read error [%d]\n", rc);
			goto out;
		}

		/* Enable SMB STAT pin output (based on charging status) to disable solar charge path 
		 * triggered on VBUS enable 
		 */
		chg_stat_cfg &= (~(SODA_CHG_CFG_17_STAT_PIN_CFG_M | SODA_CHG_CFG_17_STAT_PIN_OP_M));

		rc = soda_charger_reg_write(me, CFG_17, &chg_stat_cfg);
		if (unlikely(rc)) {
			dev_err(me->dev, "CHG CFG_17 write error [%d]\n", rc);
			goto out;
		}
	}
	
	if (me->soda_fg_temp_c < SODA_CHG_TEMP_C_T1 ||  me->soda_fg_temp_c > SODA_CHG_TEMP_C_T4) {
		/* Battery temp is out of range and stop charging, send user event if not do so */
		if (!battery_temp_error) {
			char *envp[] = { "SODA=temp_out_range", NULL };
			printk(KERN_ERR "%s:battery temp is out of range, temp = %d\n",__func__, me->soda_fg_temp_c);
			kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
			battery_temp_error= true;
			/* The Vbus enable/disable should are already handled by the SMB chip
		 	   and the pillow dispaly is handled by powerd, Soda driver don't need
		 	   to involve, keep the farmework for future use in case needed. */
#if 0
			if (soda_usb_charger_connected && soda_docked) {
				rc = soda_chg_ctrl_vbus(me, SODA_VBUS_OFF);
				if (unlikely(rc)) {
					dev_err(me->dev, "disable VBUS failed [%d]\n", rc);
				}
			}
#endif
		}
		return rc;
	} else {
		if (battery_temp_error) {
			battery_temp_error = false;
			/* The Vbus enable/disable should are already handled by the SMB chip
		 	   and the pillow dispaly is handled by powerd, Soda driver don't need
		 	   to involve, keep the farmework for future use in case needed. */
#if 0			
			if (soda_usb_charger_connected && soda_docked) {
				rc = soda_chg_ctrl_vbus(me, SODA_VBUS_ON);	/* enable vbus again */
				if (unlikely(rc)) {
					dev_err(me->dev, "enable VBUS failed [%d]\n", rc);
				}
			}
#endif
		}
	}

	if ( (me->soda_fg_temp_c >= SODA_CHG_TEMP_C_T2) && (me->soda_fg_temp_c < (SODA_CHG_TEMP_C_T4 - SODA_BATT_TEMP_HYS))) {
		charger_type = soda_is_dcp_connected();
		pr_debug("charger_type ==%d, wario_battery_current == %d, soda_tp5_flag = %d, soda_chg_power == %d",
			charger_type, wario_battery_current, soda_tp5_flag, soda_chg_power);
		if ((charger_type == WALL_CHARGER) &&
			(wario_battery_current >= 0) && !soda_tp5_flag) {
			fast_cc_val = SODA_CHG_CFG_1C_FAST_CC_600MA;
			usbin_icl = SODA_CHG_CFG_C_USBIN_ICL_600MA;
			chg_current_power = SODA_CHG_CMD_41_USBAC_HP;
			pr_debug("Set charging current to 700mA, charging power to HIGH\n");
		} else {
			/* fast CC - switch to 400mA */
			if ((charger_type == USB_CHARGER) && (wario_battery_current >= 0) && (wario_battery_current < 50)) {
				fast_cc_val = SODA_CHG_CFG_1C_FAST_CC_400MA;
				usbin_icl = SODA_CHG_CFG_C_USBIN_ICL_400MA;
				pr_debug("Set charging current to 400mA, charging power to LOW\n");
			} else {
				/* fast CC - switch to def 300mA */
				fast_cc_val = SODA_CHG_CFG_1C_FAST_CC_300MA;
				usbin_icl = SODA_CHG_CFG_C_USBIN_ICL_400MA;
				pr_debug("Set charging current to 300mA, charging power to LOW2\n");
			}
			chg_current_power = SODA_CHG_CMD_41_USBAC_LP;
		}

		if (soda_chg_power != chg_current_power) {
			printk(KERN_INFO "set charging power to %s \n", 
				(chg_current_power == SODA_CHG_CMD_41_USBAC_HP) ? "high" :"low");
			rc = soda_charger_reg_set_bit(me, CMD_41, USBAC, &chg_current_power);
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CMD_41 write error [%d]\n", rc);
				goto out;
			}
			soda_chg_power = chg_current_power;
		}

		if ((soda_chg_fastchg_current != fast_cc_val) ||
			soda_chg_force_thermal_check) {
			rc = soda_charger_reg_set_bit(me, CFG_C, USBIN_ICL, &usbin_icl);
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CFG_C write error [%d]\n", rc);
				goto out;
			}
	
			rc = soda_charger_reg_set_bit(me, CFG_1C, FAST_CC,  &fast_cc_val);
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CFG_1C write error [%d]\n", rc);
				goto out;
			}
			soda_chg_fastchg_current = fast_cc_val;
		}
	} 

	if ( (me->soda_fg_temp_c > SODA_CHG_TEMP_C_T1 &&  me->soda_fg_temp_c < SODA_CHG_TEMP_C_T2) || soda_tp1_flag) {
		if (me->soda_fg_volt <= SODA_BATT_VOLTAGE_4P1V && !soda_tp2_flag)
			pre_cc_val = SODA_CHG_CFG_1C_PRE_CC_200MA;		/* Pre CC = 200mA */
		else 
			pre_cc_val = SODA_CHG_CFG_1C_PRE_CC_100MA;		/* Pre CC = 100mA */

		if (!soda_chg_prefast_trans_ctrl || 
			(soda_chg_prechg_current != pre_cc_val) ||
			soda_chg_force_thermal_check) {

			chg_ctrl_mode = SODA_CHG_PRE_FAST_CTRL_CMD;
			rc = soda_charger_reg_set_bit(me, CFG_14, PRE_FAST_CMD, &chg_ctrl_mode);
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CFG_14 write error [%d]\n", rc);
				goto out;
			}

			pre_chg_en = SODA_CHG_CTRL_PRECHG_EN; 
			rc = soda_charger_reg_set_bit(me, CMD_42, PRE_FAST_EN, &pre_chg_en);	/* Switch from Fast to Pre charge */
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CMD_42 write error [%d]\n", rc);
				goto out;
			}

			soda_chg_prefast_trans_ctrl = SODA_CHG_PRE_FAST_CTRL_CMD;

			if ((pre_cc_val == SODA_CHG_CFG_1C_PRE_CC_200MA) &&
				(soda_chg_prechg_current == SODA_CHG_CFG_1C_PRE_CC_100MA) &&  
				(me->soda_fg_volt >= (SODA_BATT_VOLTAGE_4P1V - SODA_BATT_VOLTAGE_HYS))) {
				/* 100mV Hystersis to prevent transition from 100 to 200mA charging due to load variance */
				goto check_float_volt;
			}

			rc = soda_charger_reg_set_bit(me, CFG_1C, PRE_CC,  &pre_cc_val);
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CFG_1C write error [%d]\n", rc);
				goto out;
			}

			soda_chg_prechg_current = pre_cc_val;
		}
	} else {
		if (soda_chg_prefast_trans_ctrl || 
			soda_chg_force_thermal_check) {

			pre_chg_en = SODA_CHG_CTRL_FASTCHG_EN; 
			rc = soda_charger_reg_set_bit(me, CMD_42, PRE_FAST_EN, &pre_chg_en);	/* Switch from Pre to Fast charge */
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CMD_42 write error [%d]\n", rc);
				goto out;
			}


			chg_ctrl_mode = SODA_CHG_PRE_FAST_CTRL_AUTO;
			rc = soda_charger_reg_set_bit(me, CFG_14, PRE_FAST_CMD, &chg_ctrl_mode);	/* rollback to AUTO mode for Pre to Fast charge */
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CFG_14 write error [%d]\n", rc);
				goto out;
			}
	
			pre_cc_val = SODA_CHG_CFG_1C_PRE_CC_100MA;
			rc = soda_charger_reg_set_bit(me, CFG_1C, PRE_CC,  &pre_cc_val);	/* rollback to default charge current = 100mA */
			if (unlikely(rc)) {
				dev_err(me->dev, "CHG CFG_1C write error [%d]\n", rc);
				goto out;
			}

			soda_chg_prechg_current = pre_cc_val;
			soda_chg_prefast_trans_ctrl = SODA_CHG_PRE_FAST_CTRL_AUTO;
		}
	}

check_float_volt:
	 
	rc = soda_charger_reg_set_bit(me, CFG_5, RECHG_VLT, &chg_rechg_threshold);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG 5 write error [%d]\n", rc);
		goto out;
	}

	/* Disable Hot Soft-Limit Float Voltage Compensation */
	if (soda_chg_hot_sl_volt_comp || soda_chg_force_thermal_check) {
		hot_sl_fvc = SODA_CHG_HOT_SL_FV_COMP_OFF;
		rc = soda_charger_reg_set_bit(me, CFG_1A, HOT_SL_FVC, &hot_sl_fvc);
		if (unlikely(rc)) {
			dev_err(me->dev, "CHG CFG_1A write error [%d]\n", rc);
			goto out;
		}
		soda_chg_hot_sl_volt_comp = SODA_CHG_HOT_SL_FV_COMP_OFF;
	}

	if ( (me->soda_fg_temp_c >= SODA_CHG_TEMP_C_T3 &&  me->soda_fg_temp_c < SODA_CHG_TEMP_C_T4) || soda_tp3_flag) {
		/* limit charge voltage to 4.1V */
		chg_float_volt = SODA_CHG_CFG_1E_FLOAT_VOLT_4P1V;
	} else if ((me->soda_fg_temp_c <= (SODA_CHG_TEMP_C_T3 - SODA_BATT_TEMP_HYS))) {
		/* change charge voltage to 4.35V (normal) */
		chg_float_volt = SODA_CHG_CFG_1E_FLOAT_VOLT_4P35V;
	} else if ((me->soda_fg_temp_c > (SODA_CHG_TEMP_C_T3 - SODA_BATT_TEMP_HYS)) && 
		(me->soda_fg_temp_c < SODA_CHG_TEMP_C_T3)) {
		/* retain previous state */
		goto check_charger_state;
	}

	if (soda_chg_float_volt != chg_float_volt ||
		soda_chg_force_thermal_check) {
		rc = soda_charger_reg_set_bit(me, CFG_1E, FLOAT_VOLT, &chg_float_volt);
		if (unlikely(rc)) {
			dev_err(me->dev, "CHG CFG_1E write error [%d]\n", rc);
			goto out;
		}
		soda_chg_float_volt = chg_float_volt;
	}

check_charger_state:
	if ( (me->soda_fg_temp_c > (SODA_CHG_TEMP_C_T4 - SODA_BATT_TEMP_HYS)) || soda_tp4_flag) {
		/* disable charge */
		chg_en_src = SODA_CHG_ENABLE_CTRL_CMD;
		chg_enable = SODA_CHG_DISABLE; 
	} else if ((me->soda_fg_temp_c < (SODA_CHG_TEMP_C_T4 - SODA_BATT_TEMP_HYS))) {
		/* re-enable charge */
		chg_en_src = SODA_CHG_ENABLE_CTRL_PIN;
		chg_enable = SODA_CHG_ENABLE;
	} else if ((me->soda_fg_temp_c == (SODA_CHG_TEMP_C_T4 - SODA_BATT_TEMP_HYS))) {
		/* retain previous state */
		goto done;
	}

	if (soda_chg_enable != chg_enable ||
		soda_chg_force_thermal_check) {

		rc = soda_charger_reg_set_bit(me, CMD_42, CHARGE_ENABLE, &chg_enable);
		if (unlikely(rc)) {
			dev_err(me->dev, "CHG CMD_42 write error [%d]\n", rc);
			goto out;
		}

		rc = soda_charger_reg_set_bit(me, CFG_14, CHG_EN_SRC, &chg_en_src);
		if (unlikely(rc)) {
			dev_err(me->dev, "CHG CFG_14 write error [%d]\n", rc);
			goto out;
		}

		soda_chg_enable = chg_enable;
		soda_chg_enable_src_ctrl = chg_en_src;
	}

done:
	chg_ctrl_lock = SODA_CHG_LOCK;
	rc = soda_charger_reg_set_bit(me, CMD_40, VOLATILE, &chg_ctrl_lock);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG CMD_40 write error [%d]\n", rc);
	}

	if (soda_chg_force_thermal_check)
		soda_chg_force_thermal_check = false;

out:
	return rc;
}

/* soda_clean_fg_values:
		clear soda battery parameters when undocked 
*/
static void soda_clean_fg_values (struct soda_drvdata *me)
{
	me->soda_fg_current = SODA_FG_BAT_PARAM_UNKNOWN;
	me->soda_fg_soc = SODA_FG_BAT_PARAM_UNKNOWN;
	me->soda_fg_volt = SODA_FG_BAT_PARAM_UNKNOWN;
	me->soda_fg_temp_c = SODA_FG_BAT_PARAM_UNKNOWN;
	me->soda_fg_temp_f = SODA_FG_BAT_PARAM_UNKNOWN;
	me->soda_fg_lmd = SODA_FG_BAT_PARAM_UNKNOWN;
	me->soda_fg_nac = SODA_FG_BAT_PARAM_UNKNOWN;
	me->soda_fg_cyc_cnt = SODA_FG_BAT_PARAM_UNKNOWN;
}
static int soda_suspend_charger(struct soda_drvdata *me, bool suspend)
{
	int rc = 0;
	u8 charge_status;

	rc = soda_charger_reg_set_bit(me, CMD_41, CHIP_SUSPEND, &suspend);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG CMD_41 write error [%d]\n", rc);
	}
	return rc;
}

/* soda_charge_full process
	put the SMB chip in suspend mode
	set charge full flag
*/
static void soda_charge_full_handler (struct soda_drvdata *me, bool suspend)
{
	int rc = 0;
	u8 charge_status;

	if (suspend) {
		if (charger_suspended)
			return;

		rc = soda_charger_reg_read(me, CMD_4A, &charge_status);
		if (unlikely(rc)) {
			dev_err(me->dev, "CMD_4A read error [%d]\n", rc);
			goto out;
		}

		if ((charge_status & SODA_CHG_CMD_4A_REG_CHARGE_DONE)
			&& !(charge_status & SODA_CHG_CMD_4A_REG_CHARGE_STATUS)) {
			/* put SMB chip in suspend mode */
			rc = soda_suspend_charger(me, &suspend);
			if (!rc) {
				charge_full_flag = true;
				charger_suspended = true;
			}
		}
	} else {
		if (charger_suspended) {
			rc = soda_suspend_charger(me, &suspend);
			if (!rc)
				charger_suspended = false;
		}
	}
out:
	return;
}

static void soda_onkey_monitor_handler(struct work_struct *work)
{
	struct soda_drvdata *me =  container_of(work, struct soda_drvdata, soda_onkey_monitor);

	if (me == NULL) {
		printk(KERN_ERR "%s: invalid drv data\n",__func__);
		return;
	}

	__lock_soda(me);

	if (!atomic_read(&soda_usb_charger_connected)) {
		if ((atomic_read(&soda_docked) && 
			wario_battery_capacity > BATT1_GREEN_LED_THRESHOLD && 
			soda_battery_capacity > BATT2_GREEN_LED_THRESHOLD) || 
			(!atomic_read(&soda_docked) && 
			(wario_battery_capacity > BATT1_GREEN_LED_THRESHOLD))) {
				soda_led_control(LED_GREEN_ON);
				msleep(SODA_LED_BLINK_MS);
				soda_led_control(LED_GREEN_OFF);
		} else {
				soda_led_control(LED_AMBER_ON);
				msleep(SODA_LED_BLINK_MS);
				soda_led_control(LED_AMBER_OFF);
		}
	}

	__unlock_soda(me);
	return;
}

static void sw_press_event_cb(void *obj, void *param)
{
    struct soda_drvdata *me = (struct soda_drvdata *)param;
	struct max77696_chip *chip = max77696;

	if (wario_onkey_press_skip || !IS_SUBDEVICE_LOADED(led0, chip)
		|| !IS_SUBDEVICE_LOADED(led1, chip) || !IS_SUBDEVICE_LOADED(charger, chip)) {
		printk(KERN_INFO "KERNEL: I soda::button press skipped\n");	
		return;
	}
	
    /* schedule onkey monitor */
    schedule_work(&(me->soda_onkey_monitor));
	return;
}

int (*sodadev_get_mon_state)(sda_state_t* state, int charger_connected, int if_err_cnt, int uvlo_err_cnt) = 0;
EXPORT_SYMBOL(sodadev_get_mon_state);

static void soda_monitor_handler(struct work_struct *work)
{
	struct soda_drvdata *me =  container_of(work, struct soda_drvdata, soda_monitor.work);
	int rc = 0;
	bool updated = 0;
	struct timespec curtime;
	char buff[33];
	bool soda_fg_i2c_sts = false;
	bool soda_chg_i2c_sts = false;
	bool soda_end_monitor = false;
	int soda_fg_low_voltage_sts = SODA_FG_BAT_PARAM_UNKNOWN;

	if (me == NULL) {
		printk(KERN_ERR "%s: invalid drv data\n",__func__);
		return;
	}

	/* check soda dock sts */
	if (!atomic_read(&soda_docked)) {
		printk(KERN_INFO "%s: soda already undocked \n",__func__);	
		return;
	}

	if (soda_sda_mode != SODA_SDA_OPS_DATA)
		soda_sda_state_configure(me, SODA_SDA_OPS_DATA);

	if (soda_debug_data_mode) /* debug mode is enabled, do not do soda monitor loop */
		goto exit_skip_intr_cfg;

	__lock_soda(me);

i2c_retry:
	/* check both fg & chg status */
	soda_fg_i2c_sts = soda_chk_fg_i2c_sts(me);

	if (soda_fg_i2c_sts) {
		if (me->soda_fg_volt <= SODA_FG_LOBAT_VOLTAGE) 
			soda_fg_low_voltage_sts = SODA_FG_VOLT_LTE_3P2V;
		else 
			soda_fg_low_voltage_sts = SODA_FG_VOLT_GT_3P2V;
	}

	soda_chg_i2c_sts = soda_chk_chg_i2c_sts(me);

	pr_debug("%s: status: chg=%d, fg=%d low_volt=%d \n",__func__, soda_chg_i2c_sts, soda_fg_i2c_sts, soda_fg_low_voltage_sts);

	if (!soda_fg_i2c_sts ||
		!soda_chg_i2c_sts) {
		/* Possible conditions: 
		 *              battery uvlo state 1.5-2.4V - FG: BAD; CHG: BAD (with no usb charger)
		 *              battery low voltage state 2.4-2.7V - FG: OK; CHG: BAD (with no usb charger)
		 *              battery low voltage state 2.7-3.2V - FG: OK; CHG: OK (with or without usb charger)
		 *              i2c i/f pin issue [HW pogo pin]
		 *              user undock 
		 */

		soda_clean_fg_values(me);

		if (sodadev_get_mon_state) {
			int ret;
			sda_state_t state = {0};
			int charger_connected = atomic_read(&soda_usb_charger_connected);
			while (ret = sodadev_get_mon_state(&state, charger_connected, soda_if_err_cnt, soda_uvlo_err_cnt)) {
				switch(ret) {
					case 1:
						soda_chg_ctrl_vbus(me, state.sda_val);
						break;
					case 2:
						soda_if_err_cnt++;
						if (soda_chg_i2c_sts) {
							pr_debug("%s: FG=BAD & CHG=OK most likey soda volt range = [1.5V-2.4V]  \n",__func__);
						}
						break;
					case 3:
						if (soda_i2c_reset()) {
							printk(KERN_INFO "KERNEL: I soda::i2c reset pass\n");
							soda_i2c_reset_cnt++;
							if (soda_i2c_reset_cnt < SODA_I2C_RESET_MAX_CNT)  
								soda_if_err_cnt = 0;
							pr_debug("%s: soda_i2c_reset: pass if_err_cnt=%d, soda_i2c_reset_cnt=%d \n",
										__func__, soda_if_err_cnt, soda_i2c_reset_cnt);
							
							// retry before error user error notification 
							goto i2c_retry;
						} else {
							// i2c reset failed
							pr_debug("%s: soda_i2c_reset: fail if_err_cnt=%d, soda_i2c_reset_cnt=%d \n",
										__func__, soda_if_err_cnt, soda_i2c_reset_cnt);
						}
						break;
					case 4:
						soda_interface_warning(me, state.sda_val);		
						soda_end_monitor = true;
						break;
					case 5:
						if (!soda_chg_i2c_sts && (soda_fg_low_voltage_sts != SODA_FG_BAT_PARAM_UNKNOWN)) {
							pr_debug("%s: FG=OK & CHG=BAD most likey soda volt range [2.4 - 2.7V] volt=%dmv \n", 
										__func__, me->soda_fg_volt);
						}
						soda_uvlo_err_cnt++;
						break;
					case 6:
						soda_uvlo_warning(me);
						pr_debug("%s: uvlo warning: uvlo_err_cnt=%d \n", __func__, soda_uvlo_err_cnt);
						soda_uvlo_err_cnt = 0;
						break;
				}
			}

		}
		pr_debug("%s: if_err_cnt=%d, soda_i2c_reset_cnt=%d, soda_uvlo_err_cnt=%d \n",
					__func__, soda_if_err_cnt, soda_i2c_reset_cnt, soda_uvlo_err_cnt);
		/* continue the monitor */
		goto exit_skip_rc_chk;	
	} else if (soda_fg_low_voltage_sts == SODA_FG_VOLT_LTE_3P2V) {
		if (!atomic_read(&soda_usb_charger_connected)) {
			/* send plug-in charger notificaiton */
			soda_uvlo_warning(me);
			pr_debug("%s: uvlo sts: low_volt=%d volt=%dmV\n", __func__, soda_fg_low_voltage_sts, me->soda_fg_volt);
		}
	}
	
	if (soda_if_err_cnt) {
		soda_if_err_cnt = 0;
		soda_i2c_reset_cnt = 0;
	}
	
	if (soda_uvlo_err_cnt) 
		soda_uvlo_err_cnt = 0; 

	if (soda_batt_check_reqd) {
		rc = soda_fg_get_battery_auth(me);
		if (rc == SODA_BATTERY_ID_VALID) {
			printk(KERN_INFO "KERNEL: I soda::valid battery\n");
			/* Enable charging if USB is connected */
			if (atomic_read(&soda_usb_charger_connected)) {
				rc = soda_chg_ctrl_vbus(me, SODA_VBUS_ON); 
				if (unlikely(rc))
					dev_err(me->dev, "enable VBUS failed [%d]\n", rc);
			}
		} else {
			/* clear soda fg values */
			soda_clean_fg_values(me);
			if (rc == SODA_BATTERY_ID_INVALID) {
				char *envp[] = { "SODA=invalid_battery", NULL };
				printk(KERN_INFO "KERNEL: I soda::invalid battery\n");
				kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
				if (atomic_read(&soda_usb_charger_connected)) {
					rc = soda_chg_ctrl_vbus(me, SODA_VBUS_OFF);
					if (unlikely(rc))
						dev_err(me->dev, "disable VBUS failed [%d]\n", rc);
				}
			} else {
				printk(KERN_ERR "%s %d:Read Battery Authenticaiton Failed/check battery docking\n",__func__, rc);
				rc = -EIO;
			}
			goto exit;
		}

		rc = soda_fg_update_soc(me, 1, &updated);
		if (!rc) {
			char *envp[] = { "SODA=soda_ok", NULL };
			kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
		} else {
			pr_debug("%s: soda soc failed [%d]\n",__func__, rc);
			goto exit;
		}

		rc = soda_battery_check_skist(me);
		if ((rc !=BATTERY_SKIST) && (rc!=BATTERY_SODA) ){
			printk(KERN_ERR "%s:Read battery type (Soda/Skist) failed\n",__func__);
			goto exit;
		}

		rc = soda_fg_get_battery_uniqueid(me);
		if (rc) {
			printk(KERN_ERR "%s:Read battery unique id failed\n",__func__);
			goto exit;
		}

		getnstimeofday(&curtime);
		printk(KERN_INFO "Current time in %s is %ld sec \n", __func__, curtime.tv_sec);

		rc = soda_fg_update_temp(me, 1, &updated);
		if (unlikely(rc)) {
			pr_debug("%s: soda current failed [%d]\n",__func__, rc);
			goto exit;
		}

		rc = soda_fg_update_voltage(me, 1, &updated);
		if (unlikely(rc)) {
			pr_debug("%s: soda voltage failed [%d]\n",__func__, rc);
			goto exit;
		}

		if (battery_skist) {
			soda_metric_info.skist_dock.capacity = me->soda_fg_soc;
			soda_metric_info.skist_dock.temp= me->soda_fg_temp_c;
			soda_metric_info.skist_dock.voltage= me->soda_fg_volt;
			soda_metric_info.skist_dock.time = curtime;
			if (!strcmp(soda_metric_info.skist_dock.uniqueid, battery_unique_id)) {
				printk(KERN_INFO "Time diffs between last skist undock and dock = %ld sec\n",soda_metric_info.skist_dock.time.tv_sec - soda_metric_info.skist_undock.time.tv_sec);
				printk(KERN_INFO "Capacity diffs between last skist undock and dock = %d%%\n",soda_metric_info.skist_undock.capacity - soda_metric_info.skist_dock.capacity);
				printk(KERN_INFO "Voltage diffs between last skist undock and dock = %d mv\n",soda_metric_info.skist_undock.voltage- soda_metric_info.skist_dock.voltage);
				snprintf(buff, LOG_BUFF_LENGTH, "%ld", soda_metric_info.skist_dock.time.tv_sec - soda_metric_info.skist_undock.time.tv_sec);
				LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist Diff_time", 1, buff);
				snprintf(buff, LOG_BUFF_LENGTH, "%d %%", soda_metric_info.skist_dock.capacity - soda_metric_info.skist_undock.capacity);
				LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist Diff_Capacity", 1, buff);
				snprintf(buff, LOG_BUFF_LENGTH, "%d", soda_metric_info.skist_dock.voltage - soda_metric_info.skist_undock.voltage);
				LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist Diff_Volt", 1, buff);
			}
			strncpy(soda_metric_info.skist_dock.uniqueid, battery_unique_id, SODA_FG_BAT_UNIQUEID_LEN);
			printk(KERN_INFO "Skist docking battery capacity = %d%%\n",soda_metric_info.skist_dock.capacity);
			printk(KERN_INFO "Skist docking battery temperature = %d C\n",soda_metric_info.skist_dock.temp);
			printk(KERN_INFO "Skist dock volt = %d mV\n",soda_metric_info.skist_dock.voltage);
			snprintf(buff, LOG_BUFF_LENGTH, "%ld sec", soda_metric_info.skist_dock.time.tv_sec);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist Docking_time", 1, buff);
			snprintf(buff, LOG_BUFF_LENGTH, "%d %%", soda_metric_info.skist_dock.capacity);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist Docking_Cap", 1, buff);
			snprintf(buff, LOG_BUFF_LENGTH, "%d mV", soda_metric_info.skist_dock.voltage);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist Docking_Volt", 1, buff);
		} else {
			soda_metric_info.soda_dock.capacity = me->soda_fg_soc;
			soda_metric_info.soda_dock.temp= me->soda_fg_temp_c;
			soda_metric_info.soda_dock.voltage= me->soda_fg_volt;
			soda_metric_info.soda_dock.time = curtime;
			if (!strcmp(soda_metric_info.soda_dock.uniqueid, battery_unique_id)) {
				printk(KERN_INFO "Time diffs between last soda undock and dock = %ld sec\n",soda_metric_info.soda_dock.time.tv_sec - soda_metric_info.soda_undock.time.tv_sec);
				printk(KERN_INFO "Capacity diffs between last soda undock and dock = %d%%\n",soda_metric_info.soda_undock.capacity - soda_metric_info.soda_dock.capacity);
				printk(KERN_INFO "Voltage diffs between last soda undock and dock = %d%%\n",soda_metric_info.skist_undock.voltage- soda_metric_info.skist_dock.voltage);
				snprintf(buff, LOG_BUFF_LENGTH, "%ld", soda_metric_info.soda_dock.time.tv_sec - soda_metric_info.soda_undock.time.tv_sec);
				LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda Diff_time", 1, buff);
				snprintf(buff, LOG_BUFF_LENGTH, "%d %%", soda_metric_info.soda_dock.capacity - soda_metric_info.soda_undock.capacity);
				LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda Diff_Capacity", 1, buff);
				snprintf(buff, LOG_BUFF_LENGTH, "%d", soda_metric_info.soda_dock.voltage - soda_metric_info.soda_undock.voltage);
				LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda Diff_Volt", 1, buff);
			}
			strncpy(soda_metric_info.soda_dock.uniqueid, battery_unique_id, SODA_FG_BAT_UNIQUEID_LEN);
			printk(KERN_INFO "Soda docking capacity = %d%%\n",soda_metric_info.soda_dock.capacity);
			printk(KERN_INFO "Soda docking temp = %d C\n",soda_metric_info.soda_dock.temp);
			printk(KERN_INFO "Soda dock volt = %d mV\n",soda_metric_info.soda_dock.voltage);
			snprintf(buff, LOG_BUFF_LENGTH, "%ld sec", soda_metric_info.soda_dock.time.tv_sec);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda Docking_time", 1, buff);
			snprintf(buff, LOG_BUFF_LENGTH, "%d %%", soda_metric_info.soda_dock.capacity);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda Docking_Cap", 1, buff);
			snprintf(buff, LOG_BUFF_LENGTH, "%d mV", soda_metric_info.soda_dock.voltage);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda Docking_Volt", 1, buff);
		}

		soda_batt_check_reqd = 0;
	}
	/* update soda fg parameters */

	rc = soda_fg_update_voltage(me, 1, &updated);
	if (unlikely(rc)) {
		pr_debug("%s: soda voltage failed [%d]\n",__func__, rc);
		goto exit;
	}

	rc = soda_fg_update_avgcurrent(me, 1, &updated);
	if (unlikely(rc)) {
		pr_debug("%s: soda current failed [%d]\n",__func__, rc);
		goto exit;
	}

	rc = soda_fg_update_temp(me, 1, &updated);
	if (unlikely(rc)) {
		pr_debug("%s: soda current failed [%d]\n",__func__, rc);
		goto exit;
	}

	rc = soda_fg_update_nac(me, 1, &updated);
	if (unlikely(rc)) {
		pr_debug("%s: soda nac failed [%d]\n",__func__, rc);
		goto exit;
	}

	rc = soda_fg_update_lmd(me, 1, &updated);
	if (unlikely(rc)) {
		pr_debug("%s: soda lmd failed [%d]\n",__func__, rc);
		goto exit;
	}
	
	rc = soda_fg_update_soc(me, 1, &updated);
	if (unlikely(rc)) {
		pr_debug("%s: soda soc failed [%d]\n",__func__, rc);
		goto exit;
	}
	
	if ((me->soda_fg_soc >= BATT2_CHARGE_FULL_THRESHOLD)
		&& atomic_read(&soda_usb_charger_connected)) {
		if (!charge_full_flag) {
			soda_charge_full_handler(me, 1);
		}
	}

	if ((me->soda_fg_soc < BATT2_RECHARGE_THRESHOLD)
		&& atomic_read(&soda_usb_charger_connected)) {
		if (charge_full_flag && charger_suspended ) {
			soda_charge_full_handler(me, 0);
		}
	}
	
	rc = soda_fg_update_cycle_count(me, 1, &updated);
	if (unlikely(rc)) {
		pr_debug("%s: soda cycle cnt failed [%d]\n",__func__, rc);
		goto exit;
	}

	if (likely(updated)) {
		power_supply_changed(&(me->soda_fg_psy));
	}

	rc = soda_chg_thermal_check(me);
	if (unlikely(rc)) {
		pr_debug("%s: soda thermal check failed [%d]\n",__func__, rc);
	}

exit:
	if ((rc == -EIO) || (rc == -ENXIO) || soda_i2c_tp_flag) {
		/* error handling */
		printk(KERN_INFO "%s rc = %d\n",__func__, rc);
	} else {
		if (!rc) {
			if (soda_if_err_event_sent) { 
				soda_interface_warning(me, SODA_I2C_IF_GOOD);
				soda_if_err_event_sent = false;
				printk(KERN_INFO "KERNEL: I soda::i2c access is ok\n");
				if (atomic_read(&soda_usb_charger_connected)) {
					rc = soda_chg_ctrl_vbus(me, SODA_VBUS_ON);
					if (unlikely(rc)) {
						dev_err(me->dev, "enable VBUS failed [%d]\n", rc);
					}
				}
			}
		}
	}

exit_skip_rc_chk:
	__unlock_soda(me);

	if (!soda_force_i2c_data && soda_sda_mode != SODA_SDA_OPS_INTR)
		soda_sda_state_configure(me, SODA_SDA_OPS_INTR);

	if (soda_end_monitor) 
		return;

exit_skip_intr_cfg:
	if (rc)
		queue_delayed_work(soda_monitor_queue, &(me->soda_monitor), msecs_to_jiffies(SODA_ERROR_MON_RUN_MS));
	else
		queue_delayed_work(soda_monitor_queue, &(me->soda_monitor), msecs_to_jiffies(SODA_MON_RUN_MS));

	return;
}


int (*sodadev_get_socbatt_state)(sda_state_t* state, int charger_connected, int docked, int override, int subscribe_state, int capacity) = 0;
EXPORT_SYMBOL(sodadev_get_socbatt_state);

/* Dynamically subscribe and unsubscribe based on
 * SOC Battery HIGH and LOW events and depending
 * on various criteria (soda docked, USB plugged)
 */
static void socbatt_event_handler(struct work_struct *work) 
{
	if (!sodadev_get_socbatt_state)
		return;

	struct max77696_socbatt_eventdata *me = container_of(work, struct max77696_socbatt_eventdata,
		socbatt_event_work.work);

	int rc = 0;	
	int charger_connected = (atomic_read(&soda_usb_charger_connected));
	int docked = atomic_read(&soda_docked);
	int override = atomic_read(&me->override);
	int subscribe_state = atomic_read(&me->subscribe_state);
	int capacity = wario_battery_capacity;

	sda_state_t state = {0};
	int ret;
	while (ret = sodadev_get_socbatt_state(&state, charger_connected, docked, override, subscribe_state, capacity)) {
		switch(ret) {
			case 1:
				if(charger_connected)
					printk(KERN_INFO "%s: soda usb charger is connected, attempting "
						"to unsubscribe All", __func__);
				else
					printk(KERN_INFO "%s: usb charger disconnected and Soda undocked "
						"(device is standalone), attempting to unsubscribe All", __func__);
				break;
			case 2:
				unsubscribe_socbatt_max();
				break;
			case 3:
				unsubscribe_socbatt_min();
				break;
			case 4:
				atomic_set(&me->override, state.sda_val);
				break;
			case 5:
				atomic_set(&me->subscribe_state, state.sda_val);
				break;
			case 6:
				printk(KERN_INFO "%s: usb charger disconnected and Soda docked, "
					"attempting to handle events", __func__);
				break;
			case 7:
				printk(KERN_INFO "%s: BattSOC HI event is triggered", __func__);
				break;
			case 8:
				rc = subscribe_socbatt_min();
				if(unlikely(rc)) return;
				break;
			case 9:
				printk(KERN_INFO "%s: BattSOC Low event is triggered", __func__);
				break;
			case 10:
				rc = subscribe_socbatt_max();
				if(unlikely(rc)) return;
				break;
		}

	}
}

static void soda_vbus_monitor_handler(struct work_struct *work)
{
	struct soda_drvdata *me =  container_of(work, struct soda_drvdata, soda_vbus_ctrl_monitor.work);

	if (me == NULL) {
		printk(KERN_ERR "%s: invalid drv data\n",__func__);
		return;
	}

	__lock_soda(me);
	if (atomic_read(&soda_usb_charger_connected) && atomic_read(&soda_docked)) {
		if (battery_authenticated) {
			soda_vbus_control(me, SODA_VBUS_ON);		/* enable vbus */
			soda_chg_force_thermal_check = true;
		}
	} else {
		soda_vbus_control(me, SODA_VBUS_OFF);		/* disable vbus */
	}
	__unlock_soda(me);

	return;
}

static void soda_led_monitor_handler(struct work_struct *work)
{
	struct soda_drvdata *me =  container_of(work, struct soda_drvdata, soda_led_monitor.work);

	if (me == NULL) {
		printk(KERN_ERR "%s: invalid drv data\n",__func__);
		return;
	}

	__lock_soda(me);

	/* TODO: Add check for battery valid based on soda/skist id */
	if (!atomic_read(&soda_usb_charger_connected)) {
		__unlock_soda(me);
		return;
	}

	if (!wario_battery_valid) {
		soda_led_control(LED_AMBER_OFF);
		soda_led_control(LED_GREEN_OFF);

		__unlock_soda(me);
		return;
	}

	if (atomic_read(&soda_docked)) {
		if (((wario_battery_capacity > BATT1_GREEN_LED_THRESHOLD) || force_green_led) &&
			((soda_battery_capacity > BATT2_GREEN_LED_THRESHOLD) || charge_full_flag)) {
			soda_led_control(LED_AMBER_OFF);
			soda_led_control(LED_GREEN_ON);
		} else {
			soda_led_control(LED_GREEN_OFF);
			soda_led_control(LED_AMBER_ON);
		}
	} else {
		if (wario_battery_capacity > BATT1_GREEN_LED_THRESHOLD || (force_green_led == true)) {
			soda_led_control(LED_AMBER_OFF);
			soda_led_control(LED_GREEN_ON);
		} else {
			soda_led_control(LED_GREEN_OFF);
			soda_led_control(LED_AMBER_ON);
		}
	}

	__unlock_soda(me);

	/* schedule led monitor */
	schedule_delayed_work(&(me->soda_led_monitor), msecs_to_jiffies(SODA_LED_MON_MS));
	return;
}

extern int max77696_charger_set_icl(void);
extern void asession_enable(bool connected);
static void soda_init_dock_status (struct soda_drvdata *me,bool boot)
{
	struct timespec curtime;
	char buff[33];

	__lock_soda(me);

	if (gpio_soda_dock_detect(me->sda_gpio)) {	
		if (atomic_read(&soda_dock_atomic_detection) == 1) {
			pr_debug("%s: soda already docked \n", __func__);
			__unlock_soda(me);
			return;
		}
		/* DOCKED */
		atomic_set(&soda_dock_atomic_detection, 1);
		atomic_set(&soda_docked, SODA_STATE_DOCKED);
		kobject_uevent(me->kobj, KOBJ_ADD);
		irq_set_irq_type(me->soda_sda_dock_irq, IRQF_TRIGGER_HIGH);
		printk(KERN_INFO "KERNEL: I soda::docked\n");
		schedule_delayed_work(&(me->soda_led_blinking), msecs_to_jiffies(SODA_LED_INIT_MS));
		/*handle OTG*/
		if(max77696_uic_is_otg_connected()) {
			printk(KERN_INFO "Soda: USB OTG cable is already plugged in. enabling VBUS\n");
			soda_otg_vbus_output(1, 0);
			if(!boot) {
				asession_enable(1);
				max77696_charger_set_icl();
			}else
				printk(KERN_INFO "soda: boot_otg_detect will do the rest..");	
		}
	} else {		
		if (atomic_read(&soda_dock_atomic_detection) == 0) {
			pr_debug("%s: soda already undocked \n", __func__);
			__unlock_soda(me);
			return;
		}

		/* UNDOCKED */
		atomic_set(&soda_dock_atomic_detection, 0);
		atomic_set(&soda_docked, SODA_STATE_UNDOCKED);
		battery_authenticated = false;
		charge_full_flag = false;
		charger_suspended = false;
		/* Send soda undock user event to powerd */
		kobject_uevent(me->kobj, KOBJ_REMOVE);

		/* enable soda dock interrupt */
		irq_set_irq_type(me->soda_sda_dock_irq, IRQF_TRIGGER_LOW);

		soda_batt_check_reqd = 1;

		printk(KERN_INFO "KERNEL: I soda::undocked\n");
		soda_chg_power = SODA_CHG_CMD_41_USBAC_LP;

		/* collect metrics data */
		getnstimeofday(&curtime);
		printk(KERN_INFO "Current time in %s is %ld sec \n", __func__, curtime.tv_sec);
		if (battery_skist) {
			soda_metric_info.skist_undock.capacity = me->soda_fg_soc;
			soda_metric_info.skist_undock.temp = me->soda_fg_temp_c;
			soda_metric_info.skist_undock.voltage = me->soda_fg_volt;
			soda_metric_info.skist_undock.time = curtime;
			printk(KERN_INFO "Skist undock capacity = %d%%\n",soda_metric_info.skist_undock.capacity);
			printk(KERN_INFO "Skist undock temp = %d C\n",soda_metric_info.skist_undock.temp);
			printk(KERN_INFO "Skist undock volt = %d mV\n",soda_metric_info.skist_undock.voltage);
			printk(KERN_INFO "Time diffs between last skist dock and undock = %ld sec\n",soda_metric_info.skist_undock.time.tv_sec - soda_metric_info.skist_dock.time.tv_sec);
			printk(KERN_INFO "Capacity diffs between last skist dock and undock = %d%%\n",soda_metric_info.skist_undock.capacity - soda_metric_info.skist_dock.capacity);
			snprintf(buff, LOG_BUFF_LENGTH, "%ld sec", soda_metric_info.skist_undock.time.tv_sec);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist UnDocking_time", 1, buff);
			snprintf(buff, LOG_BUFF_LENGTH, "%d %%", soda_metric_info.skist_undock.capacity);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist UnDocking_Cap", 1, buff);
			snprintf(buff, LOG_BUFF_LENGTH, "%d mV", soda_metric_info.skist_undock.voltage);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Skist UnDocking_Volt", 1, buff);
		} else {
			soda_metric_info.soda_undock.capacity = me->soda_fg_soc;
			soda_metric_info.soda_undock.temp = me->soda_fg_temp_c;
			soda_metric_info.soda_undock.voltage = me->soda_fg_volt;
			soda_metric_info.soda_undock.time = curtime;
			printk(KERN_INFO "Soda undock battery capacity = %d%%\n",soda_metric_info.soda_undock.capacity);
			printk(KERN_INFO "Soda undock battery temp = %d C\n",soda_metric_info.soda_undock.temp);
			printk(KERN_INFO "Soda undock volt = %d mV\n",soda_metric_info.soda_undock.voltage);
			printk(KERN_INFO "Time diffs between last soda dock and undock = %ld sec\n",soda_metric_info.soda_undock.time.tv_sec - soda_metric_info.soda_dock.time.tv_sec);
			printk(KERN_INFO "Capacity diffs between last soda dock and undock = %d%%\n",soda_metric_info.soda_undock.capacity - soda_metric_info.soda_dock.capacity);
			snprintf(buff, LOG_BUFF_LENGTH, "%ld sec", soda_metric_info.soda_undock.time.tv_sec);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda UnDocking_time", 1, buff);
			snprintf(buff, LOG_BUFF_LENGTH, "%d %%", soda_metric_info.soda_undock.capacity);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda UnDocking_Cap", 1, buff);
			snprintf(buff, LOG_BUFF_LENGTH, "%d mV", soda_metric_info.soda_undock.voltage);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "kernel", "Soda_fg", "Soda UnDocking_Volt", 1, buff);
		}
		me->soda_fg_current = 0; /* clear the charging current if the battery is undocked */

		/*handle OTG*/
		if( max77696_uic_is_otg_connected()) {
			//TODO may need an event to let user space know
			printk(KERN_INFO "soda charger just undocked.. going to kick out OTG vbus already gone");
			gpio_soda_ctrl(me->otg_sw_gpio, 0);
			asession_enable(0);
		}
	}

	__unlock_soda(me);

	/* reset all flags */
	soda_if_err_cnt = 0;
	soda_i2c_reset_cnt = 0;
	soda_uvlo_err_cnt = 0;
	soda_uvlo_event_sent = false;
	soda_if_err_event_sent = false;
	soda_if_ok_event_sent = false;

	if (atomic_read(&soda_docked))
		queue_delayed_work(soda_monitor_queue, &(me->soda_monitor), msecs_to_jiffies(SODA_MON_DOCK_MS));

	return;
}

static void soda_extchg_detect_cb(struct soda_drvdata *me)
{
	if (me == NULL) {
		printk(KERN_ERR "%s: invalid drv data\n",__func__);
		return;
	}

	__lock_soda(me);

	if (gpio_soda_ext_chg_detect(me->ext_chg_gpio)) {
		char *envp[] = { "SODA=usb_con", NULL };
		if (atomic_read(&ext_charger_atomic_detection) == 1) {
			pr_debug("%s: ext charger detection in progress\n", __func__);
			__unlock_soda(me);
			return;
		}
		/* do force manual charger detection again after USB plugged in */
		if (force_charger_detection) {
			force_charger_detection = false;
			max77696_uic_force_charger_detection();
		}

		atomic_set(&ext_charger_atomic_detection, 1);
		atomic_set(&soda_usb_charger_connected, 1);
		kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
		soda_led_control(LED_GREEN_OFF);
		soda_uvlo_event_sent = false;
		soda_uvlo_err_cnt = 0;

		if (wario_battery_valid) 
			soda_led_control(LED_AMBER_ON);

		if (soda_boot_init) { 
			schedule_delayed_work(&(me->soda_led_monitor), msecs_to_jiffies(SODA_LED_INIT_MS));
			soda_boot_init = false;
		} else {
			schedule_delayed_work(&(me->soda_led_monitor), msecs_to_jiffies(SODA_LED_MON_MS));
		}
		printk(KERN_INFO "KERNEL: I soda::usb charger connected\n");
	} else {
		char *envp[] = { "SODA=usb_discon", NULL };
		force_charger_detection = true;
		charge_full_flag = false;
		charger_suspended = false;
		if (atomic_read(&ext_charger_atomic_detection) == 0) {
			pr_debug("%s: ext charger diconnected already \n", __func__);
			__unlock_soda(me);
			return;
		}

		atomic_set(&ext_charger_atomic_detection, 0);
		atomic_set(&soda_usb_charger_connected, 0);
		soda_chg_power = SODA_CHG_CMD_41_USBAC_LP;
		kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
		__unlock_soda(me);
		cancel_delayed_work_sync(&(me->soda_led_monitor));
		__lock_soda(me);
		soda_led_control(LED_GREEN_OFF);
		soda_led_control(LED_AMBER_OFF);
		printk(KERN_INFO "KERNEL: I soda::usb charger disconnected\n");	
	}

	__unlock_soda(me);
	schedule_delayed_work(&(me->soda_vbus_ctrl_monitor), msecs_to_jiffies(SODA_MON_VBUS_MS));
	schedule_delayed_work(&socbatt_eventdata.socbatt_event_work, msecs_to_jiffies(SOCBATT_EVTHDL_DELAY));
	return;
}

static irqreturn_t soda_usbchg_event_handler (int irq, void *data)
{
	struct soda_drvdata *me = (struct soda_drvdata *)data;
	if (me == NULL) {
		printk(KERN_ERR "%s: invalid drv data\n",__func__);
		return IRQ_HANDLED;
	}

	pr_debug("%s: ext charger interrupt \n",__func__);
	soda_extchg_detect_cb(me);
	pmic_soda_connects_usbchg_handler();
	return IRQ_HANDLED;
}

static irqreturn_t soda_dock_event_handler (int irq, void *data)
{
	struct soda_drvdata *me = (struct soda_drvdata *)data;
	if (me == NULL) {
		printk(KERN_ERR "%s: invalid drv data\n",__func__);
		return IRQ_HANDLED;
	}

	pmic_soda_connects_dock_handler();
	msleep(SODA_DOCK_DEB_MS);
	pr_debug("%s: soda dock interrupt: gpio=[%d]\n",__func__, gpio_soda_dock_detect(me->sda_gpio));
	soda_init_dock_status(me, false);

	schedule_delayed_work(&(me->soda_vbus_ctrl_monitor), msecs_to_jiffies(SODA_MON_VBUS_MS));
	schedule_delayed_work(&socbatt_eventdata.socbatt_event_work, msecs_to_jiffies(SOCBATT_EVTHDL_DELAY));
	return IRQ_HANDLED;
}

static irqreturn_t soda_dock_quickcheck_isr (int irq, void *data)
{
	struct soda_drvdata *me = (struct soda_drvdata *)data;
	if (me == NULL) {
		printk(KERN_ERR "%s: invalid drv data\n",__func__);
		return IRQ_HANDLED;
	}
	return IRQ_WAKE_THREAD;
}


int (*sodadev_get_cfg_state)(sda_state_t* state, int ops_state) = 0;
EXPORT_SYMBOL(sodadev_get_cfg_state);

static int soda_sda_state_configure(struct soda_drvdata *me, int soda_ops_state)
{
	if (!sodadev_get_cfg_state)
		return 0;

	int rc = 0;
	int ret;
	sda_state_t state = {0};

	while (ret=sodadev_get_cfg_state(&state, soda_ops_state)) {
		switch (ret) {
			case 1:
				soda_config_sda_line(state.sda_item);
				break;
			case 3:
				pr_debug("%s configuring sda - intr dock_gpio[%d] \n",__func__,gpio_soda_dock_detect(me->sda_gpio));
				// irq for soda dock event
				rc = request_threaded_irq(me->soda_sda_dock_irq, soda_dock_quickcheck_isr, soda_dock_event_handler, 
						state.sda_val,
						state.sda_item, me);
				if (unlikely(rc)) {
					printk(KERN_ERR "%s Failed to claim irq %d, error %d \n",__func__, me->soda_sda_dock_irq, rc);
					break;
				}
				break;
			case 4:
				soda_sda_mode = state.sda_item;
				break;
			case 5:
				soda_init_dock_status(me, false);
				break;
			case 6:
				free_irq(me->soda_sda_dock_irq, me);
				break;
			case 7:
				pr_debug("%s configuring sda - data \n",__func__);
				break;
			default:
				msleep(100);
				break;
		}
	}
	return rc;
}

int soda_init_pins(struct soda_drvdata *me)
{
	int ret = 0;

	ret = gpio_request(me->scl_gpio, "soda_i2c_scl");
	if(unlikely(ret)) return ret;

	ret = gpio_request(me->sda_gpio, "soda_i2c_sda");	
	if(unlikely(ret)) goto out_err_i2c_sda;

	ret = gpio_request(me->otg_sw_gpio, "soda_otg_sw");	
	if(unlikely(ret)) goto out_err_otg_sw;

	ret = gpio_request(me->ext_chg_gpio, "ext_chg_int");
	if(unlikely(ret)) goto out_err_chg_det;

	ret = gpio_request(me->i2c_sda_pu_gpio, "soda_sda_pullup");
	if(unlikely(ret)) goto out_err_i2c_sda_pu;

	ret = gpio_request(me->boost_ctrl_gpio, "soda_boost_ctrl");
	if(unlikely(ret)) goto out_err_boost;

	ret = gpio_request(me->vbus_en_gpio, "soda_vbus_enable");
	if(unlikely(ret)) goto out_err_vbus_en;

	gpio_direction_input(me->ext_chg_gpio);
	gpio_direction_input(me->i2c_sda_pu_gpio);
	gpio_direction_output(me->scl_gpio, 1);
	gpio_direction_output(me->sda_gpio, 1);
	gpio_direction_output(me->boost_ctrl_gpio, 0);
	gpio_direction_output(me->otg_sw_gpio, 0);
	gpio_direction_output(me->vbus_en_gpio, 0);
	return ret;

out_err_vbus_en:
	gpio_free(me->boost_ctrl_gpio);
out_err_boost:
	gpio_free(me->i2c_sda_pu_gpio);
out_err_i2c_sda_pu:
	gpio_free(me->ext_chg_gpio);
out_err_chg_det:
	gpio_free(me->otg_sw_gpio);
out_err_otg_sw:
	gpio_free(me->sda_gpio);
out_err_i2c_sda:
	gpio_free(me->scl_gpio);
	return ret;
}

void soda_free_pins(struct soda_drvdata *me)
{
	gpio_free(me->boost_ctrl_gpio);
	gpio_free(me->vbus_en_gpio);
	gpio_free(me->i2c_sda_pu_gpio);
	gpio_free(me->ext_chg_gpio);
	gpio_free(me->otg_sw_gpio);
	gpio_free(me->sda_gpio);
	gpio_free(me->scl_gpio);
	return;
}

static int __devinit soda_batt_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct soda_platform_data *pdata = pdev->dev.platform_data;
	struct soda_drvdata* me = NULL;

	if (!lab126_board_is(BOARD_ID_WHISKY_WAN) &&
			!lab126_board_is(BOARD_ID_WHISKY_WFO) &&
			!lab126_board_is(BOARD_ID_WOODY) &&
			!lab126_board_is(BOARD_ID_WARIO_5)) {
		return -ENODEV;
	}

	if (unlikely(!pdata)) {
		dev_err(&(pdev->dev), "platform data is missing\n");
		return -EINVAL;
	}

	me = kzalloc(sizeof(struct soda_drvdata), GFP_KERNEL);
	if (unlikely(!me)) {
		dev_err(&(pdev->dev), "Cannot allocate memory (%uB requested)\n", sizeof(struct soda_drvdata));
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, me);
	me->pdata = pdata;
	me->dev = &(pdev->dev);
	me->kobj = &(pdev->dev.kobj);

	me->scl_gpio = pdata->scl_gpio;
	me->sda_gpio = pdata->sda_gpio;
	me->i2c_sda_pu_gpio = pdata->i2c_sda_pu_gpio;
	me->boost_ctrl_gpio = pdata->boost_ctrl_gpio;
	me->vbus_en_gpio = pdata->vbus_en_gpio;
	me->otg_sw_gpio = pdata->otg_sw_gpio;
	me->ext_chg_gpio = pdata->ext_chg_gpio;
	me->ext_chg_irq = pdata->ext_chg_irq;
	me->soda_sda_dock_irq = pdata->soda_sda_dock_irq;

	me->update_interval = msecs_to_jiffies(pdata->update_interval_ms);

	soda_monitor_queue = create_singlethread_workqueue("soda_monitor");

	if (soda_monitor_queue == NULL) {
		printk(KERN_ERR "Coulndn't create soda monitor work queue\n");
		rc = -ENOMEM;
		goto out_err_soda_monitor_queue;
	}

	rc = soda_init_pins(me);
	if (unlikely(rc)) {
		printk(KERN_ERR "%s Failed to init soda gpio [error=%d] \n",__func__, rc);
		goto out_err_soda_gpio;
	}

	soda_config_sda_line(SODA_I2C_SDA_DATA_LINE);

	mutex_init(&(me->soda_lock));
	mutex_init(&(me->soda_chg_lock));
	mutex_init(&(me->soda_fg_lock));
	mutex_init(&(me->soda_boost_lock));

	/* INIT_WORK */
	INIT_DELAYED_WORK(&(me->soda_monitor), soda_monitor_handler);
	INIT_DELAYED_WORK(&(socbatt_eventdata.socbatt_event_work), socbatt_event_handler);
	INIT_DELAYED_WORK(&(me->soda_vbus_ctrl_monitor), soda_vbus_monitor_handler);
	INIT_DELAYED_WORK(&(me->soda_led_monitor), soda_led_monitor_handler);
	INIT_WORK(&(me->soda_onkey_monitor), soda_onkey_monitor_handler);
	INIT_DELAYED_WORK(&(me->soda_led_blinking),soda_led_blinking_handler);

	/* Init SOC Battery events struct */
	atomic_set(&socbatt_eventdata.override, IGNORE_SOCBATT_TRIGGER); /* Ignore explicit triggers as default */
	atomic_set(&socbatt_eventdata.subscribe_state, UNSUBSCRIBED_ALL); /* Default state is unsubscribed to all events */

	/* soda charger power_supply_register */
	me->soda_chg_psy.name = SODA_CHG_PSY_NAME;
	me->soda_chg_psy.type = POWER_SUPPLY_TYPE_MAINS;
	me->soda_chg_psy.properties = soda_chg_psy_props;
	me->soda_chg_psy.num_properties = ARRAY_SIZE(soda_chg_psy_props);
	me->soda_chg_psy.get_property = soda_charger_get_property;
	me->soda_chg_psy.set_property = soda_charger_set_property;
	me->soda_chg_psy.property_is_writeable = soda_charger_property_is_writeable;

	/* soda battery power_supply_register */ 
	me->soda_fg_psy.name = SODA_FG_PSY_NAME;
	me->soda_fg_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	me->soda_fg_psy.properties = soda_fg_psy_props;
	me->soda_fg_psy.num_properties = ARRAY_SIZE(soda_fg_psy_props);
	me->soda_fg_psy.get_property = soda_battery_get_property;

	/* soda boost (gpio o/p to en/dis) power_supply_register */
	me->soda_boost_psy.name = SODA_BOOST_PSY_NAME;
	me->soda_boost_psy.type = POWER_SUPPLY_TYPE_MAINS;
	me->soda_boost_psy.properties = soda_boost_psy_props;
	me->soda_boost_psy.num_properties = ARRAY_SIZE(soda_boost_psy_props);
	me->soda_boost_psy.get_property = soda_boost_get_property;
	me->soda_boost_psy.set_property = soda_boost_set_property;
	me->soda_boost_psy.property_is_writeable = soda_boost_property_is_writeable;

	/* i2c bb paramters */
	me->soda_i2c_adap.owner = THIS_MODULE;
	me->soda_i2c_adap.dev.parent = &pdev->dev;
	me->soda_i2c_adap.algo_data = &me->soda_bit_data;
	strlcpy(me->soda_i2c_adap.name, SODA_I2C, sizeof(me->soda_i2c_adap.name));

	me->soda_bit_data.data = me;
	me->soda_bit_data.setsda = soda_i2c_setsda;  
	me->soda_bit_data.setscl = soda_i2c_setscl; 
	me->soda_bit_data.getsda = soda_i2c_getsda,
	me->soda_bit_data.getscl = soda_i2c_getscl;
	me->soda_bit_data.udelay = pdata->i2c_bb_delay;
	me->soda_bit_data.timeout = HZ;

	soda_i2c_setsda(me, 1);
	soda_i2c_setscl(me, 1);

	rc = i2c_bit_add_bus(&me->soda_i2c_adap);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to add soda i2c bus\n");
		goto out_err_i2c_bus;
	}

	me->soda_chg_i2c.client = i2c_new_device(&me->soda_i2c_adap, &soda_chg_i2c_board_info);
	if (unlikely(!me->soda_chg_i2c.client)) {
		rc = -EIO;
		dev_err(me->dev, "failed to create soda chg i2c device [%d]\n", rc);
		goto out_err_i2c_chg;
	}

	i2c_set_clientdata(me->soda_chg_i2c.client, me);
	me->soda_chg_i2c.read = soda_i2c_single_read;
	me->soda_chg_i2c.write = soda_i2c_single_write;
	me->soda_chg_i2c.bulk_read = soda_i2c_seq_read;
	me->soda_chg_i2c.bulk_write = soda_i2c_seq_write;

	me->soda_fg_i2c.client = i2c_new_device(&me->soda_i2c_adap, &soda_fg_i2c_board_info);
	if (unlikely(!me->soda_fg_i2c.client)) {
		rc = -EIO;
		dev_err(me->dev, "failed to create soda fg i2c device [%d]\n", rc);
		goto out_err_i2c_fg;	
	}

	i2c_set_clientdata(me->soda_fg_i2c.client, me);
	me->soda_fg_i2c.read = soda_i2c_single_read;
	me->soda_fg_i2c.write = soda_i2c_single_write;
	me->soda_fg_i2c.bulk_read = soda_i2c_seq_read;
	me->soda_fg_i2c.bulk_write = soda_i2c_seq_write;

	/* Configure wakeup capable */
	device_set_wakeup_capable(&pdev->dev, true); 

	rc = power_supply_register(me->dev, &(me->soda_chg_psy));
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register soda chg psy device [%d]\n", rc);
		goto out_err_reg_chg_psy;
	}

	rc = power_supply_register(me->dev, &(me->soda_fg_psy));
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register soda fg psy device [%d]\n", rc);
		goto out_err_reg_fg_psy;
	}

	rc = power_supply_register(me->dev, &(me->soda_boost_psy));
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register soda boost psy device [%d]\n", rc);
		goto out_err_reg_boost_psy;
	}

	/* initialize SW boost status default=enable */
	soda_sw_boost_ctrl(me, SODA_BOOST_ENABLE);

	__reset_timestamp(me, soda_fg_cyc_cnt_timestamp);
	__reset_timestamp(me, soda_fg_nac_timestamp);
	__reset_timestamp(me, soda_fg_lmd_timestamp);
	__reset_timestamp(me, soda_fg_soc_timestamp);
	__reset_timestamp(me, soda_fg_current_timestamp);
	__reset_timestamp(me, soda_fg_volt_timestamp);

	soda_extchg_detect_cb(me);

	/* irq for external usb/wall charger connect */
	rc = request_threaded_irq(me->ext_chg_irq, NULL, soda_usbchg_event_handler, 
			(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_EARLY_RESUME | IRQF_ONESHOT),
			SODA_EXT_CHG, me);
	if (unlikely(rc)) {
		printk(KERN_ERR "%s Failed to claim irq %d, error %d \n",__func__, me->ext_chg_irq, rc);
		goto out_err_req_usb_chg_irq;
	}

	/* configure sda for intr */
	soda_config_sda_line(SODA_I2C_SDA_DOCK_INTR);	

	/* irq for soda dock event */
	rc = request_threaded_irq(me->soda_sda_dock_irq, soda_dock_quickcheck_isr, soda_dock_event_handler, 
			(IRQF_TRIGGER_NONE | IRQF_EARLY_RESUME | IRQF_ONESHOT),
			SODA_DOCK_EVENT, me);
	if (unlikely(rc)) {
		printk(KERN_ERR "%s Failed to claim irq %d, error %d \n",__func__, me->soda_sda_dock_irq, rc);
		goto out_err_req_dock_irq;
	}

	soda_sda_mode = SODA_SDA_OPS_INTR;
	soda_init_dock_status(me, true);
	pmic_soda_connects_init(me->ext_chg_gpio, me->sda_gpio);

	rc = sysfs_create_group(me->kobj, &soda_attr_group);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to create soda sysdev attribute group [%d]\n", rc);
		goto out_err_sysfs;
	}

	sw_press_event.param = me;
	sw_press_event.func = sw_press_event_cb;
	rc = pmic_event_subscribe(EVENT_SW_ONKEY_PRESS, &sw_press_event);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (%lu) [%d]\n", EVENT_SW_ONKEY_PRESS, rc);
		goto out_err_onkey_sub;
	}

	/* By now, USB and Soda docked status are updated, so lets run the
	 * SOC battery event handler */
	schedule_delayed_work(&(socbatt_eventdata.socbatt_event_work), msecs_to_jiffies(SOCBATT_EVTHDL_DELAY));

	/* Configure wakeup capable */
	device_set_wakeup_capable(me->dev, 1);
	device_set_wakeup_enable (me->dev, me->soda_sda_dock_irq);
	printk(KERN_INFO "%s Installed \n", __func__);

	g_soda_dd = me;
	return 0;

out_err_onkey_sub:
	sysfs_remove_group(me->kobj, &soda_attr_group);
out_err_sysfs:
	free_irq(me->soda_sda_dock_irq, me);
out_err_req_dock_irq:
	free_irq(me->ext_chg_irq, me);
out_err_req_usb_chg_irq:
	power_supply_unregister(&(me->soda_boost_psy));
out_err_reg_boost_psy:
	power_supply_unregister(&(me->soda_fg_psy));
out_err_reg_fg_psy:
	power_supply_unregister(&(me->soda_chg_psy));
out_err_reg_chg_psy:
	i2c_set_clientdata(me->soda_fg_i2c.client, NULL);
	i2c_unregister_device(me->soda_fg_i2c.client);
out_err_i2c_fg:
	i2c_set_clientdata(me->soda_chg_i2c.client, NULL);
	i2c_unregister_device(me->soda_chg_i2c.client);
out_err_i2c_chg:
out_err_i2c_bus:
	soda_free_pins(me);
out_err_soda_gpio:
	mutex_destroy(&(me->soda_lock));
	mutex_destroy(&(me->soda_chg_lock));
	mutex_destroy(&(me->soda_fg_lock));
	mutex_destroy(&(me->soda_boost_lock));
out_err_soda_monitor_queue:
	platform_set_drvdata(pdev, NULL);
	kfree(me);
	return rc;
}


static int __devexit soda_batt_remove(struct platform_device *pdev)
{
	struct soda_drvdata* me = platform_get_drvdata(pdev);

	pmic_event_unsubscribe(EVENT_SW_ONKEY_PRESS, &sw_press_event);
	sysfs_remove_group(me->kobj, &soda_attr_group);

	cancel_delayed_work_sync(&(me->soda_monitor));
	cancel_delayed_work_sync(&(socbatt_eventdata.socbatt_event_work));
	cancel_delayed_work_sync(&(me->soda_vbus_ctrl_monitor));
	cancel_delayed_work_sync(&(me->soda_led_monitor));
	cancel_work_sync(&(me->soda_onkey_monitor));
	destroy_workqueue(soda_monitor_queue);

	g_soda_dd = NULL;

	power_supply_unregister(&(me->soda_boost_psy));
	power_supply_unregister(&(me->soda_fg_psy));
	power_supply_unregister(&(me->soda_chg_psy));

	i2c_set_clientdata(me->soda_fg_i2c.client, NULL);
	i2c_unregister_device(me->soda_fg_i2c.client);

	i2c_set_clientdata(me->soda_chg_i2c.client, NULL);
	i2c_unregister_device(me->soda_chg_i2c.client);

	free_irq(me->ext_chg_irq, me);
	free_irq(me->soda_sda_dock_irq, me);
	soda_free_pins(me);
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->soda_lock));
	mutex_destroy(&(me->soda_chg_lock));
	mutex_destroy(&(me->soda_fg_lock));
	mutex_destroy(&(me->soda_boost_lock));
	kfree(me);
	return 0;
}

#ifdef CONFIG_PM
static int soda_batt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct soda_drvdata* me = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&(me->soda_monitor));
	cancel_delayed_work_sync(&(socbatt_eventdata.socbatt_event_work));
	cancel_delayed_work_sync(&(me->soda_vbus_ctrl_monitor));
	cancel_delayed_work_sync(&(me->soda_led_monitor));
	cancel_work_sync(&(me->soda_onkey_monitor));

	if (likely(device_may_wakeup(me->dev))) {
		enable_irq_wake(me->soda_sda_dock_irq);
	}

	printk(KERN_INFO "KERNEL: I soda:suspend::\n");
	return 0;
}

static int soda_batt_resume(struct platform_device *pdev)
{
	struct soda_drvdata* me = platform_get_drvdata(pdev);

	printk(KERN_INFO "KERNEL: I soda:resume::\n");

	/* After resume from hiberation, first check the battery docking status and USB cable status */
	pr_debug("In %s calling soda_init_dock_status to check battery docking status\n", __func__);
	soda_init_dock_status(me, false);
	pr_debug("In %s calling soda_extchg_detect_cb to check USB cable status\n", __func__);
	soda_extchg_detect_cb(me);
	queue_delayed_work(soda_monitor_queue, &(me->soda_monitor), msecs_to_jiffies(SODA_MON_DOCK_MS));

	if (likely(device_may_wakeup(me->dev))) {
		disable_irq_wake(me->soda_sda_dock_irq);
	}
	return 0;
}
#else
#define soda_batt_suspend NULL
#define soda_batt_resume  NULL
#endif


static struct platform_driver soda_batt_driver = {
	.driver = {
		.name    = SODA_DRIVER_NAME, 
		.owner   = THIS_MODULE,
	},
	.probe      = soda_batt_probe,
	.remove     = __devexit_p(soda_batt_remove),
	.suspend    = soda_batt_suspend,
	.resume     = soda_batt_resume,
};

static int __init soda_init(void)
{
	return platform_driver_register(&soda_batt_driver);
}

static void __exit soda_exit(void)
{
	platform_driver_unregister(&soda_batt_driver);
}

module_init(soda_init);
module_exit(soda_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Vidhyananth Venkatasamy");
MODULE_DESCRIPTION(SODA_DRIVER_DESC);
MODULE_VERSION(SODA_DRIVER_VERSION);

