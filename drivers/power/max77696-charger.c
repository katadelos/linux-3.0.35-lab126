/*
 * Copyright (C) 2012 Maxim Integrated Product
 * Jayden Cha <jayden.cha@maxim-ic.com>
 * Copyright 2012-2016 Amazon Technologies, Inc.
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
#include <linux/sysdev.h>
#include <linux/i2c.h>

#include <linux/power_supply.h>
#include <linux/mfd/max77696.h>
#include <linux/mfd/max77696-events.h>
#include <max77696_registers.h>
#include <llog.h>

#include <linux/delay.h>

#define DRIVER_DESC    "MAX77696 Main Charger Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_CHARGER_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#ifdef CONFIG_FALCON
extern int in_falcon(void);
#endif
extern int pb_oneshot;
extern void pb_oneshot_unblock_button_events (void);

#ifdef VERBOSE
#define dev_noise(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_noise(args...) do { } while (0)
#endif /* VERBOSE */

#define CHGA_WORK_DELAY                0

/* CHGA Register Read/Write */
#define max77696_charger_reg_read(me, reg, val_ptr) \
	max77696_read((me)->i2c, CHGA_REG(reg), val_ptr)
#define max77696_charger_reg_write(me, reg, val) \
	max77696_write((me)->i2c, CHGA_REG(reg), val)
#define max77696_charger_reg_bulk_read(me, reg, dst, len) \
	max77696_bulk_read((me)->i2c, CHGA_REG(reg), dst, len)
#define max77696_charger_reg_bulk_write(me, reg, src, len) \
	max77696_bulk_write((me)->i2c, CHGA_REG(reg), src, len)
#define max77696_charger_reg_read_masked(me, reg, mask, val_ptr) \
	max77696_read_masked((me)->i2c, CHGA_REG(reg), mask, val_ptr)
#define max77696_charger_reg_write_masked(me, reg, mask, val) \
	max77696_write_masked((me)->i2c, CHGA_REG(reg), mask, val)

#define max77696_charger_reg_get_bit(me, reg, bit, val_ptr) \
	({\
	 int __rc = max77696_charger_reg_read_masked(me, reg,\
		 CHGA_REG_BITMASK(reg, bit), val_ptr);\
	 *(val_ptr) = CHGA_REG_BITGET(reg, bit, *(val_ptr));\
	 __rc;\
	 })
#define max77696_charger_reg_set_bit(me, reg, bit, val) \
	({\
	 max77696_charger_reg_write_masked(me, reg,\
		 CHGA_REG_BITMASK(reg, bit), CHGA_REG_BITSET(reg, bit, val));\
	 })

#ifdef DEBUG
#define __show_details(me, num, which) \
	do {\
		u8 _dtls;\
		max77696_charger_reg_get_bit(me,\
				CHG_DTLS_0##num, which##_DTLS, &_dtls);\
		dev_info((me)->dev, "IRQ_"#which" details: %02X\n", _dtls);\
	} while (0)
#else /* DEBUG */
#define __show_details(me, num, which) \
	do { } while (0)
#endif /* DEBUG */

#define CHARGE_HISTORY_LEN 20
#define CHARGE_HISTORY_DEBOUNCE_LIMIT (HZ/2)
#define CHARGE_HISTORY_DEBOUNCE_TIMEOUT (HZ*15)
#define CHARGE_IGNORE_BIG_DELTA (HZ*5)

/* CHGA Register Single Bit Ops */
struct max77696_charger {
	struct max77696_chip    *chip;
	struct max77696_i2c     *i2c;
	struct device           *dev;
	struct kobject          *kobj;

	void                     (*charger_notify) (struct power_supply*, bool, bool);

	struct power_supply      psy;
	struct delayed_work      chgina_work;
	struct delayed_work      chgsts_work;
	struct delayed_work      thmsts_work;
	struct delayed_work      batok_work;
	unsigned int             irq;
	u8                       irq_unmask;
	u8                       interrupted;
	u8                       icl_ilim;
	bool                     chg_online, chg_enable;
	bool                     wdt_enabled;
	unsigned long            wdt_period;
	struct delayed_work      wdt_work;
	struct delayed_work      gauge_vmn_work;
	struct delayed_work      gauge_vmx_work;
	struct delayed_work      gauge_smn_work;
	struct delayed_work      gauge_smx_work;
	struct delayed_work      gauge_tmn_work;
	struct delayed_work      gauge_tmx_work;
	unsigned int             gauge_irq;
	struct delayed_work      glbl_critbat_work;
	unsigned int             critbat_irq;
	struct delayed_work      gauge_driftadj_work;	
	struct delayed_work      charger_monitor_work;
	struct delayed_work      charger_debounce_work;
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	struct delayed_work      ctv_adjust_work;	
#endif
	struct mutex             lock;
};

extern int max77696_led_set_manual_mode(unsigned int led_id, bool manual_mode);
extern int max77696_led_ctrl(unsigned int led_id, int led_state);
static bool is_charger_a_connected(struct max77696_charger *me);
static bool is_charger_a_enabled(struct max77696_charger *me);
static void log_charger_cb_time(struct max77696_charger *me, unsigned long time);

static unsigned long charge_history[CHARGE_HISTORY_LEN];
static unsigned long charge_history_total;
static unsigned int charge_history_idx = 0;
static bool charge_history_saturated = false;

static unsigned long last_charge_time;
static bool last_charge_time_set = false;

atomic_t chgina_atomic_detection = ATOMIC_INIT(0);
atomic_t socbatt_max_subscribed = ATOMIC_INIT(0);
atomic_t socbatt_min_subscribed = ATOMIC_INIT(0);

struct max77696_charger *g_max77696_charger = NULL;
static bool chga_disabled_by_user = false;
static bool chga_disabled_by_debounce = false;
static bool otg_out_5v = false;
#if defined(CONFIG_MX6SL_WARIO_BASE)
static bool green_led_set = false;
#endif
#if defined(CONFIG_MX6SL_WARIO_WOODY)
extern atomic_t soda_usb_charger_connected;
extern void max77696_gauge_temphi(void);
extern bool force_green_led;
int max77696_charger_ctv_reset (struct max77696_charger* me);
static bool is_charger_done_mode = false;
#endif

static int max77696_charger_mode_set(struct max77696_charger*, int);
static int max77696_charger_mode_get(struct max77696_charger*, int*);
static int max77696_charger_pqen_set(struct max77696_charger*, int);
static int max77696_charger_pqen_get(struct max77696_charger*, int*);
static int max77696_charger_cc_sel_set(struct max77696_charger*, int);
static int max77696_charger_cc_sel_get(struct max77696_charger*, int*);
static int max77696_charger_cv_prm_set(struct max77696_charger*, int);
static int max77696_charger_cv_prm_get(struct max77696_charger*, int*);
static int max77696_charger_cv_jta_set(struct max77696_charger*, int);
static int max77696_charger_cv_jta_get(struct max77696_charger*, int*);
static int max77696_charger_to_time_set(struct max77696_charger*, int);
static int max77696_charger_to_time_get(struct max77696_charger*, int*);
static int max77696_charger_to_ith_set(struct max77696_charger*, int);
static int max77696_charger_to_ith_get(struct max77696_charger*, int*);
static int max77696_charger_jeita_set(struct max77696_charger*, int);
static int max77696_charger_jeita_get(struct max77696_charger*, int*);
static int max77696_charger_t1_set(struct max77696_charger*, int);
static int max77696_charger_t1_get(struct max77696_charger*, int*);
static int max77696_charger_t2_set(struct max77696_charger*, int);
static int max77696_charger_t2_get(struct max77696_charger*, int*);
static int max77696_charger_t3_set(struct max77696_charger*, int);
static int max77696_charger_t3_get(struct max77696_charger*, int*);
static int max77696_charger_t4_set(struct max77696_charger*, int);
static int max77696_charger_t4_get(struct max77696_charger*, int*);
static int max77696_charger_wdt_en_set(struct max77696_charger*, int);
static int max77696_charger_wdt_en_get(struct max77696_charger*, int*);
static int max77696_charger_wdt_period_set(struct max77696_charger*, int);
static int max77696_charger_wdt_period_get(struct max77696_charger*, int*);
static int max77696_charger_charging_set(struct max77696_charger*, int);
static int max77696_charger_charging_get(struct max77696_charger*, int*);
static int max77696_charger_allow_charging_set(struct max77696_charger*, int);
static int max77696_charger_allow_charging_get(struct max77696_charger*, int*);
static int max77696_charger_debounce_charging_set(struct max77696_charger*, int);
static int max77696_charger_debounce_charging_get(struct max77696_charger*, int*);
static int max77696_charger_lpm_set(struct max77696_charger*, bool);
static int max77696_charger_soft_disable(struct max77696_charger* me, bool user_disable, bool debounce_disable, bool otg);
static int chg_state_changed(struct max77696_charger *me);
int max77696_charger_mask(void *obj, u16 event, bool mask_f);
/* functions to subscribe/unsubscribe SOC battery HI/LOW events */
int subscribe_socbatt_max(void);
int subscribe_socbatt_min(void);
void unsubscribe_socbatt_max(void);
void unsubscribe_socbatt_min(void);

extern struct max77696_chip* max77696;
extern int wario_lobat_event;
extern int wario_lobat_condition;
extern int wario_critbat_event;
extern int wario_critbat_condition;
extern int wario_battery_voltage;
extern int wario_battery_current;
extern int wario_battery_capacity;
extern int wario_battery_temp_c;
extern int wario_battery_valid;
extern void max77696_gauge_lobat(void);
extern void max77696_gauge_overheat(void);
extern void max77696_gauge_sochi_event(void);
extern void max77696_gauge_soclo_event(void);
extern int max77696_gauge_driftadj_handler(void);
#if defined(CONFIG_MX6SL_WARIO_BASE)
extern void gpio_wan_ldo_fet_ctrl(int enable);
#endif
static pmic_event_callback_t fg_lobat_evt;
static pmic_event_callback_t fg_overvolt_evt;
static pmic_event_callback_t fg_lotemp_evt;
static pmic_event_callback_t fg_crittemp_evt;
static pmic_event_callback_t fg_critbat_evt;
static pmic_event_callback_t fg_maxsoc_evt;
static pmic_event_callback_t fg_minsoc_evt;
static pmic_event_callback_t chg_detect;
static pmic_event_callback_t chg_status;
static pmic_event_callback_t thm_status;

#define CHG_DTLS_LOBAT_PREQUAL_MODE 0x0
#define CHG_DTLS_FASTCHG_CC_MODE    0x1 
#define CHG_DTLS_FASTCHG_CV_MODE    0x2 
#define CHG_DTLS_TOPOFF_MODE        0x3
#define CHG_DTLS_DONE_MODE          0x4 
#define CHG_DTLS_HI_TEMP_MODE       0x5
#define CHG_DTLS_TIMER_FAULT        0x6
#define CHG_DTLS_THM_SUSP_MODE      0x7
#define CHG_DTLS_INVALID_INPUT      0x8
#define BAT_DTLS_MISSING_BATTERY    0x0
#define BAT_DTLS_TIMER_FAULT        0x2
#define THM_DTLS_OVERHEAT_TEMP      0x6
#define THM_DTLS_COLD_TEMP          0x3

#define CHARGER_MON_INIT            (10*1000)
#define CHARGER_MON_RUN             (30*1000)

#define GAUGE_WORK_DELAY            0
#define GLBL_CRITBAT_WORK_DELAY     0
#if defined(CONFIG_MX6SL_WARIO_WOODY)
#define CHARGER_CTV_ADJUST_DELAY    (72*3600*1000)	/* 3 days based on reliability & charging pattern metric data */
#endif

#define WARIO_GREEN_LED_THRESHOLD   95 		/* Battery capacity (%) */
#define WARIO_BAT_EOC_CAP           100	 	/* Battery capacity (%) */
#define WARIO_BAT_RESTART_CAP       95		/* Battery capacity (%) */
#define WARIO_BAT_EOC_CURRENT       65		/* ICHGTerm = 65mA */
#define WARIO_NORMAL_FCC            466000	/* Fast Charge Current = 466mA */
#define WARIO_REDUCE_FCC            233000	/* Fast Charge Current = 233mA */
	
#define WARIO_TEMP_C_LO_THRESH      0
#define WARIO_TEMP_C_MID_THRESH     15
#define WARIO_TEMP_C_HI_THRESH      45
#define WARIO_TEMP_C_CRIT_THRESH    60

#define VBUS_RAMP_DELAY             200

#define __psy_to_max77696_charger(psy_ptr) \
	container_of(psy_ptr, struct max77696_charger, psy)
#define __chgina_work_to_max77696_charger(chgina_work_ptr) \
	container_of(chgina_work_ptr, struct max77696_charger, chgina_work.work)
#define __chgsts_work_to_max77696_charger(chgsts_work_ptr) \
	container_of(chgsts_work_ptr, struct max77696_charger, chgsts_work.work)
#define __thmsts_work_to_max77696_charger(thmsts_work_ptr) \
	container_of(thmsts_work_ptr, struct max77696_charger, thmsts_work.work)
#define __wdt_work_to_max77696_charger(wdt_work_ptr) \
	container_of(wdt_work_ptr, struct max77696_charger, wdt_work.work)

#define __get_i2c(chip)                 (&((chip)->pmic_i2c))
#define __lock(me)                      mutex_lock(&((me)->lock))
#define __unlock(me)                    mutex_unlock(&((me)->lock))

#define __search_index_of_the_smallest_in_be(_table, _val) \
	({\
	 int __i;\
	 for (__i = 0; __i < ARRAY_SIZE(_table); __i++) {\
	 if (_table[__i] >= _val) break;\
	 }\
	 ((__i >= ARRAY_SIZE(_table))? (ARRAY_SIZE(_table) - 1) : __i);\
	 })

#define max77696_charger_write_irq_mask(me) \
	do {\
		int _rc = max77696_charger_reg_write(me,\
				CHG_INT_MASK, ~((me)->irq_unmask));\
		if (unlikely(_rc)) {\
			dev_err((me)->dev, "CHG_INT_MASK write error [%d]\n", _rc);\
		}\
	} while (0)

static __inline void max77696_charger_enable_irq (struct max77696_charger* me,
		u8 irq_bits, bool forced)
{
	if (unlikely(!forced && (me->irq_unmask & irq_bits) == irq_bits)) {
		/* already unmasked */
		return;
	}

	/* set enabled flag */
	me->irq_unmask |= irq_bits;
	max77696_charger_write_irq_mask(me);
}

static __inline void max77696_charger_disable_irq (struct max77696_charger* me,
		u8 irq_bits, bool forced)
{
	if (unlikely(!forced && (me->irq_unmask & irq_bits) == 0)) {
		/* already masked */
		return;
	}

	/* clear enabled flag */
	me->irq_unmask &= ~irq_bits;
	max77696_charger_write_irq_mask(me);
}

#define MAX77696_CHARGER_ATTR(name, unitstr) \
	static ssize_t max77696_charger_##name##_show (struct sys_device *dev,\
			struct sysdev_attribute *devattr, char *buf)\
{\
	int val = 0, rc;\
	struct max77696_charger *me = g_max77696_charger;\
	__lock(me);\
	rc = max77696_charger_##name##_get(me, &val);\
	if (unlikely(rc)) {\
		goto out;\
	}\
	rc = (int)snprintf(buf, PAGE_SIZE, "%d"unitstr"\n", val);\
	out:\
	__unlock(me);\
	return (ssize_t)rc;\
}\
static ssize_t max77696_charger_##name##_store (struct sys_device *dev,\
		struct sysdev_attribute *devattr, const char *buf, size_t count)\
{\
	int val, rc;\
	struct max77696_charger *me = g_max77696_charger;\
	__lock(me);\
	val = (int)simple_strtol(buf, NULL, 10);\
	rc = max77696_charger_##name##_set(me, val);\
	if (unlikely(rc)) {\
		goto out;\
	}\
out:\
	__unlock(me);\
	return (ssize_t)count;\
}\
static SYSDEV_ATTR(name, S_IWUSR|S_IRUGO, max77696_charger_##name##_show,\
		max77696_charger_##name##_store)

MAX77696_CHARGER_ATTR(mode,              ""    );
MAX77696_CHARGER_ATTR(pqen,              ""    );
MAX77696_CHARGER_ATTR(cc_sel,            "uA"  );
MAX77696_CHARGER_ATTR(cv_prm,            "mV"  );
MAX77696_CHARGER_ATTR(cv_jta,            "mV"  );
MAX77696_CHARGER_ATTR(to_time,           "min" );
MAX77696_CHARGER_ATTR(to_ith,            "uA"  );
MAX77696_CHARGER_ATTR(jeita,             ""    );
MAX77696_CHARGER_ATTR(t1,                "C"   );
MAX77696_CHARGER_ATTR(t2,                "C"   );
MAX77696_CHARGER_ATTR(t3,                "C"   );
MAX77696_CHARGER_ATTR(t4,                "C"   );
MAX77696_CHARGER_ATTR(wdt_en,            ""    );
MAX77696_CHARGER_ATTR(wdt_period,        "msec");
MAX77696_CHARGER_ATTR(charging,          "");
MAX77696_CHARGER_ATTR(allow_charging,    "");
MAX77696_CHARGER_ATTR(debounce_charging, "");

static ssize_t connected_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{

	struct max77696_charger *me = g_max77696_charger;
	if (is_charger_a_connected(me)) 
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}
static SYSDEV_ATTR(connected, S_IRUGO, connected_show, NULL);

static struct sysdev_class wario_chga_sysclass = {
	.name = "wario_charger",
};

static struct sys_device wario_chga_device = {
	.id = 0,
	.cls = &wario_chga_sysclass,
};

static int max77696_charger_sysdev_register(void) 
{
	int rc = 0;
	rc = sysdev_class_register(&wario_chga_sysclass);
	if (!rc) {
		rc = sysdev_register(&wario_chga_device);
		if (!rc) {
			sysdev_create_file(&wario_chga_device, &attr_mode);
			sysdev_create_file(&wario_chga_device, &attr_pqen);
			sysdev_create_file(&wario_chga_device, &attr_cc_sel);
			sysdev_create_file(&wario_chga_device, &attr_cv_prm);
			sysdev_create_file(&wario_chga_device, &attr_cv_jta);
			sysdev_create_file(&wario_chga_device, &attr_to_time);
			sysdev_create_file(&wario_chga_device, &attr_to_ith);
			sysdev_create_file(&wario_chga_device, &attr_jeita);
			sysdev_create_file(&wario_chga_device, &attr_t1);
			sysdev_create_file(&wario_chga_device, &attr_t2);
			sysdev_create_file(&wario_chga_device, &attr_t3);
			sysdev_create_file(&wario_chga_device, &attr_t4);
			sysdev_create_file(&wario_chga_device, &attr_wdt_en);
			sysdev_create_file(&wario_chga_device, &attr_wdt_period);
			sysdev_create_file(&wario_chga_device, &attr_charging);
			sysdev_create_file(&wario_chga_device, &attr_allow_charging);
			sysdev_create_file(&wario_chga_device, &attr_debounce_charging);
			sysdev_create_file(&wario_chga_device, &attr_connected);
		} else {
			sysdev_class_unregister(&wario_chga_sysclass);
		}
	}
	return rc;
}

static void max77696_charger_sysdev_unregister(void)
{
	sysdev_remove_file(&wario_chga_device, &attr_mode);
	sysdev_remove_file(&wario_chga_device, &attr_pqen);
	sysdev_remove_file(&wario_chga_device, &attr_cc_sel);
	sysdev_remove_file(&wario_chga_device, &attr_cv_prm);
	sysdev_remove_file(&wario_chga_device, &attr_cv_jta);
	sysdev_remove_file(&wario_chga_device, &attr_to_time);
	sysdev_remove_file(&wario_chga_device, &attr_to_ith);
	sysdev_remove_file(&wario_chga_device, &attr_jeita);
	sysdev_remove_file(&wario_chga_device, &attr_t1);
	sysdev_remove_file(&wario_chga_device, &attr_t2);
	sysdev_remove_file(&wario_chga_device, &attr_t3);
	sysdev_remove_file(&wario_chga_device, &attr_t4);
	sysdev_remove_file(&wario_chga_device, &attr_wdt_en);
	sysdev_remove_file(&wario_chga_device, &attr_wdt_period);
	sysdev_remove_file(&wario_chga_device, &attr_charging);
	sysdev_remove_file(&wario_chga_device, &attr_allow_charging);
	sysdev_remove_file(&wario_chga_device, &attr_debounce_charging);
	sysdev_remove_file(&wario_chga_device, &attr_connected);
	sysdev_unregister(&wario_chga_device);
	sysdev_class_unregister(&wario_chga_sysclass);
	return;
}

/*
 * MAXSOC event callback from PMIC-MAX77696 
 */
static void max77696_gauge_maxsoc_cb(void *obj, void *param)
{
	printk(KERN_CRIT "KERNEL: I pmic:fg battery capacity::high limit reached soc=%3d%%\n",
					wario_battery_capacity);
	max77696_gauge_sochi_event();
}

/*
 * MINSOC event callback from PMIC-MAX77696 
 */
static void max77696_gauge_minsoc_cb(void *obj, void *param)
{
	printk(KERN_CRIT "KERNEL: I pmic:fg battery capacity::low limit reached soc=%3d%%\n",
					wario_battery_capacity);
	max77696_gauge_soclo_event();
}

/*
 * LOTEMP Threshold event callback from PMIC-MAX77696 
 */
static void max77696_gauge_lotemp_cb(void *obj, void *param)
{
#if defined(CONFIG_MX6SL_WARIO_BASE)
	printk(KERN_CRIT "KERNEL: E pmic:fg battery temp::lowtemp temp=%dC current=%dmA\n",
					wario_battery_temp_c, wario_battery_current);
#elif defined(CONFIG_MX6SL_WARIO_WOODY)
	max77696_gauge_temphi();
#endif
}

/*
 * CRITTEMP Threshold event callback from PMIC-MAX77696 
 */
static void max77696_gauge_crittemp_cb(void *obj, void *param)
{
	max77696_gauge_overheat();
}

/*
 * OVERVOLT event callback from PMIC-MAX77696 
 */
static void max77696_gauge_overvolt_cb(void *obj, void *param)
{
	printk(KERN_CRIT "KERNEL: E pmic:fg battery voltage::overvolt volt=%dmV\n",
					wario_battery_voltage);
}

/*
 * LOBAT event callback from PMIC-MAX77696 
 */
static void max77696_gauge_lobat_cb(void *obj, void *param)
{
	wario_lobat_condition = 1;
	max77696_gauge_lobat();
}

/*
 * CRITBAT event callback from PMIC-MAX77696 
 */
static void max77696_gauge_critbat_cb(void *obj, void *param)
{
	wario_critbat_condition = 1;
	max77696_gauge_lobat();
}

/* Event handling functions */	
int max77696_fg_mask(void *obj, u16 event, bool mask_f)
{
	u8 irq, bit_pos;
	int rc = 0;
	struct max77696_charger *me = (struct max77696_charger *)obj;

	DECODE_EVENT(event, irq, bit_pos);

	__lock(me);

	if(mask_f) {
		max77696_irq_disable_fgirq(bit_pos, 0);     
	} else {
		max77696_irq_enable_fgirq(bit_pos, 0);    
	}

	__unlock(me);
	return rc;
}

static struct max77696_event_handler max77696_fg_lobat_handle = {
	.mask_irq = max77696_fg_mask,
	.event_id = EVENT_FG_VMN,
};

static struct max77696_event_handler max77696_fg_overvolt_handle = {
	.mask_irq = max77696_fg_mask,
	.event_id = EVENT_FG_VMX,
};

static struct max77696_event_handler max77696_fg_crittemp_handle = {
	.mask_irq = max77696_fg_mask,
	.event_id = EVENT_FG_TMX,
};

static struct max77696_event_handler max77696_fg_lotemp_handle = {
	.mask_irq = max77696_fg_mask,
	.event_id = EVENT_FG_TMN,
};

static struct max77696_event_handler max77696_fg_maxsoc_handle = {
	.mask_irq = max77696_fg_mask,
	.event_id = EVENT_FG_SMX,
};

static struct max77696_event_handler max77696_fg_minsoc_handle = {
	.mask_irq = max77696_fg_mask,
	.event_id = EVENT_FG_SMN,
};

static struct max77696_event_handler max77696_chgrin_handle = {
    .mask_irq = NULL,
    .event_id = EVENT_SW_CHGRIN,
};

static struct max77696_event_handler max77696_chgrout_handle = {
    .mask_irq = NULL,
    .event_id = EVENT_SW_CHGROUT,
};

static struct max77696_event_handler max77696_chgsts_handle = {
	.mask_irq = max77696_charger_mask,
	.event_id = EVENT_CHGA_CHG,
};

static struct max77696_event_handler max77696_thmsts_handle = {
	.mask_irq = max77696_charger_mask,
	.event_id = EVENT_CHGA_THM,
};

static struct max77696_event_handler max77696_chgina_handle = {
	.mask_irq = max77696_charger_mask,
	.event_id = EVENT_CHGA_INA,
};

/* Charger - CHGSTS specific callbacks */
/* Event handling functions */
int max77696_charger_mask(void *obj, u16 event, bool mask_f)
{
	u8 irq, bit_pos, buf=0x0;
	int rc;
	struct max77696_charger *me = (struct max77696_charger *)obj;

	DECODE_EVENT(event, irq, bit_pos);
	__lock(me);
	rc = max77696_charger_reg_read(me, CHG_INT_MASK, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_INT_MASK read error [%d]\n", rc);
		goto out;
	}

	if(mask_f) {
		rc = max77696_charger_reg_write(me, CHG_INT_MASK, (buf | bit_pos));
		if (unlikely(rc)) {
			dev_err(me->dev, "CHG_INT_MASK write error [%d]\n", rc);
			goto out;
		}
	}
	else {	
		rc = max77696_charger_reg_write(me, CHG_INT_MASK, (buf & ~bit_pos));
		if (unlikely(rc)) {
			dev_err(me->dev, "CHG_INT_MASK write error [%d]\n", rc);
			goto out;
		}
	}
out:
	__unlock(me);
	return rc;
}

int subscribe_socbatt_max(void)
{
	int rc = -1;

	if(!fg_maxsoc_evt.param || !fg_maxsoc_evt.func) return -EINVAL;

	/* since this is called dynamically and many times, we need to make
	 * sure that there is only one cb subscribed at any given point
	 */
	if(atomic_read(&socbatt_max_subscribed)) {
		printk(KERN_WARNING "%s: Already subscribed!", __func__);
		return -EPERM;
	}

        rc = pmic_event_subscribe(EVENT_FG_SMX, &fg_maxsoc_evt);

	/* set flag to say we have subscribed */
	if(!likely(rc)) {
		printk(KERN_INFO "%s: Now subscribed", __func__);
		atomic_set(&socbatt_max_subscribed, 1);
	}
	return rc;
}
EXPORT_SYMBOL(subscribe_socbatt_max);

int subscribe_socbatt_min(void)
{
	int rc = -1;
	if(!fg_minsoc_evt.param || !fg_minsoc_evt.func) return -EINVAL;

	if(atomic_read(&socbatt_min_subscribed)) {
		printk(KERN_WARNING "%s: Already subscribed!", __func__);
		return -EPERM;
	}

	rc = pmic_event_subscribe(EVENT_FG_SMN, &fg_minsoc_evt);

	/* set flag to say we have subscribed */
	if(!likely(rc)) {
		printk(KERN_INFO "%s: Now subscribed", __func__);
		atomic_set(&socbatt_min_subscribed, 1);
	}
	return rc;
}
EXPORT_SYMBOL(subscribe_socbatt_min);

void unsubscribe_socbatt_max(void)
{
	if(atomic_read(&socbatt_max_subscribed)) {
		pmic_event_unsubscribe(EVENT_FG_SMX, &fg_maxsoc_evt);
		atomic_set(&socbatt_max_subscribed, 0);
		printk(KERN_INFO "%s: Now unsubscribed", __func__);
	} else {
		printk(KERN_WARNING "%s: Already unsubscribed!", __func__);
	}
}
EXPORT_SYMBOL(unsubscribe_socbatt_max);

void unsubscribe_socbatt_min(void)
{
	if(atomic_read(&socbatt_min_subscribed)) {
		pmic_event_unsubscribe(EVENT_FG_SMN, &fg_minsoc_evt);
		atomic_set(&socbatt_min_subscribed, 0);
		printk(KERN_INFO "%s: Now unsubscribed", __func__);
	} else {
		printk(KERN_WARNING "%s: Already unsubscribed!", __func__);
	}
}
EXPORT_SYMBOL(unsubscribe_socbatt_min);

static void thm_status_cb(void *obj, void *param)
{
	struct max77696_charger *me = (struct max77696_charger *)obj;
	u8 intr_sts = 0, chga_dtls00 = 0;
	u8 chg_sts =0, thm_dtls = 0;
	int rc;

	__lock(me);

	rc = max77696_charger_reg_read(me, CHG_INT_OK, &intr_sts);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_INT_OK read error [%d]\n", rc);
		goto out;
	}

	rc = max77696_charger_reg_read(me, CHG_DTLS_00, &chga_dtls00);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHGA_DTLS_00 read error [%d]\n", rc);
		goto out;
	}

	chg_sts = CHGA_REG_BITGET(CHG_INT_OK, CHG, intr_sts);
	thm_dtls = CHGA_REG_BITGET(CHG_DTLS_00, THM_DTLS, chga_dtls00);
	pr_debug("THM Status Change Interrupt %s CHG_INT=0x%x, CHG_DTLS_00=0x%x \n", 
						__func__, intr_sts, chga_dtls00); 
	pr_debug("chga(status) = %s, thm_details = 0x%x, \n", ((chg_sts == 1) ? "ON":"OFF"), thm_dtls);

	if (thm_dtls == THM_DTLS_OVERHEAT_TEMP) {
		/* Critical temp (> T4) */
		printk(KERN_CRIT "KERNEL: E pmic:charger thm fault::overheat temp=%dC current=%dmA\n",
					wario_battery_temp_c, wario_battery_current);
	} else if (thm_dtls == THM_DTLS_COLD_TEMP) {
		/* Cold temp (<< T1) */ 
		printk(KERN_CRIT "KERNEL: E pmic:charger thm fault::cold temp=%dC current=%dmA\n",
					wario_battery_temp_c, wario_battery_current);
	}

out:
	__unlock(me);
	return;
}

static void chg_status_cb(void *obj, void *param)
{
	struct max77696_charger *me = (struct max77696_charger *)obj;
	u8 intr_sts = 0, chga_dtls01 = 0;
	u8 chg_sts = 0, chg_dtls = 0, bat_dtls = 0;
	int rc;

	__lock(me);

	rc = max77696_charger_reg_read(me, CHG_INT_OK, &intr_sts);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_INT_OK read error [%d]\n", rc);
		goto out;
	}

	rc = max77696_charger_reg_read(me, CHG_DTLS_01, &chga_dtls01);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHGA_DTLS_01 read error [%d]\n", rc);
		goto out;
	}

	chg_sts = CHGA_REG_BITGET(CHG_INT_OK, CHG, intr_sts);
	chg_dtls = CHGA_REG_BITGET(CHG_DTLS_01, CHG_DTLS, chga_dtls01);
	bat_dtls = CHGA_REG_BITGET(CHG_DTLS_01, BAT_DTLS, chga_dtls01);

	pr_debug("CHGA Status Change Interrupt %s CHG_INT=0x%x CHG_DTLS_01=0x%x \n",
				 __func__, intr_sts, chga_dtls01);
	pr_debug("chga(status) = %s, chg_details01 = 0x%x, bat_details01=0x%x \n", 
			((chg_sts == 1) ? "ON":"OFF"), chg_dtls, bat_dtls);

	if (!chg_sts) {
		if (chg_dtls == CHG_DTLS_DONE_MODE) {
			printk(KERN_INFO "KERNEL: I pmic:charger status::done mode\n");	
			schedule_delayed_work(&(me->gauge_driftadj_work), msecs_to_jiffies(GAUGE_WORK_DELAY));
#if defined(CONFIG_MX6SL_WARIO_WOODY)
			if (atomic_read(&soda_usb_charger_connected))
				schedule_delayed_work(&(me->ctv_adjust_work), msecs_to_jiffies(CHARGER_CTV_ADJUST_DELAY));
			is_charger_done_mode = true;
#endif
		} else if ( (chg_dtls == CHG_DTLS_TIMER_FAULT) || (bat_dtls == BAT_DTLS_TIMER_FAULT) ) {
			/* ChargerA timeout */
			printk(KERN_ERR "KERNEL: E pmic:charger timer fault::charging over 10hrs\n");
		}

		if (bat_dtls == BAT_DTLS_MISSING_BATTERY) {
			printk(KERN_ERR "KERNEL: E pmic:charger thermistor fault::missing battery\n");
		}
	} else {
		if (chg_dtls == CHG_DTLS_LOBAT_PREQUAL_MODE) {
			pr_debug("CHGA Status Change Interrupt %s - low battery prequal mode \n", __func__);
		} else if (chg_dtls == CHG_DTLS_FASTCHG_CC_MODE) {
			pr_debug("CHGA Status Change Interrupt %s - fast charge const currrent mode \n", __func__);
		} else if (chg_dtls == CHG_DTLS_FASTCHG_CV_MODE) {
			pr_debug("CHGA Status Change Interrupt %s - fast charge const voltage mode \n", __func__);
		} else if (chg_dtls == CHG_DTLS_TOPOFF_MODE) {
			pr_debug("CHGA Status Change Interrupt %s - topoff mode \n", __func__);
		}

#if defined(CONFIG_MX6SL_WARIO_WOODY)
		if ( is_charger_done_mode && chg_dtls != CHG_DTLS_INVALID_INPUT) {	
		/* charger recharge threshold - reset ctv to avoid pulse charging */
			LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
					"kernel", "charger", "recharge_threshold", 1, "");
			max77696_charger_ctv_reset(me);
			is_charger_done_mode = false;
		}
#endif
	}

out:
	__unlock(me);
	return;
}

/*
 * Send uevents, manage delayed work and configure LEDs to reflect
 * a change in the charger's state, either caused by enabling/disabling
 * the charger in software, or physical charger change.
 */
static int chg_state_changed(struct max77696_charger *me)
{
	bool online, enable, soft_enable;
	u8 status;
	int rc;

	rc = max77696_charger_reg_read(me, CHG_INT_OK, &status);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHGINA_STAT read error [%d]\n", rc);
		goto out;
	}

	online = CHGA_REG_BITGET(CHG_INT_OK, CHGINA, status);
	enable = CHGA_REG_BITGET(CHG_INT_OK, CHG,    status);
	soft_enable = (online && !chga_disabled_by_debounce && !chga_disabled_by_user && !otg_out_5v);

	if (soft_enable) {
		if (atomic_read(&chgina_atomic_detection) == 1) {
			pr_debug("%s: detection in progress\n", __func__);
			goto out;
		}
		atomic_set(&chgina_atomic_detection, 1);
		kobject_uevent(me->kobj, KOBJ_ADD);

		/* set LED's to auto mode */
		printk(KERN_INFO "KERNEL: I pmic:charger chgina::charger connected\n");	

		/* cancel any pending lobat work */
		cancel_delayed_work_sync(&(me->gauge_vmn_work));		
		cancel_delayed_work_sync(&(me->glbl_critbat_work));

#if defined(CONFIG_MX6SL_WARIO_BASE)
		/* schedule charger monitor */
		schedule_delayed_work(&(me->charger_monitor_work), msecs_to_jiffies(CHARGER_MON_INIT));
#endif

		if (wario_lobat_condition) {
			wario_lobat_event = 0;
			wario_critbat_event = 0;	
			wario_lobat_condition = 0;
			wario_critbat_condition = 0;
		}

		if(pb_oneshot == HIBER_SUSP) {
			pb_oneshot = HIBER_CHG_IRQ;
			pb_oneshot_unblock_button_events();
		}
	} else { 
		if (atomic_read(&chgina_atomic_detection) == 0) {
			pr_debug("%s: already removed\n", __func__);
			goto out;
		}
		atomic_set(&chgina_atomic_detection, 0);

		printk(KERN_INFO "KERNEL: I pmic:charger chgina::charger disconnected\n");	

		kobject_uevent(me->kobj, KOBJ_REMOVE);	
#if defined(CONFIG_MX6SL_WARIO_BASE)
		cancel_delayed_work_sync(&(me->charger_monitor_work));

		if (wario_battery_valid && !chga_disabled_by_user) {
			/* Note: don't turn on Charge(Amber) LED even after unplug/plug */
			/* set Amber LED to auto mode */
			rc = max77696_led_set_manual_mode(MAX77696_LED_AMBER, false);
			if (unlikely(rc)) {
				dev_err(me->dev, "%s: error led mode ctrl (amber) [%d]\n", __func__, rc);
				goto out;
			}
		}

		/* set Green LED to auto mode */
		rc = max77696_led_set_manual_mode(MAX77696_LED_GREEN, false);
		if (unlikely(rc)) {
			dev_err(me->dev, "%s: error led mode ctrl (green) [%d]\n", __func__, rc);
			goto out;
		}

		green_led_set = false;
		/* Note: Turn gpio LDO control for WAN/4.35V protection */
		gpio_wan_ldo_fet_ctrl(1);	/* LDO */
#elif defined(CONFIG_MX6SL_WARIO_WOODY)
		is_charger_done_mode = false;
		force_green_led = false;
		max77696_charger_ctv_reset(me);	
#endif
	}

	if (unlikely(me->chg_online == online && me->chg_enable == enable)) {
		goto out;
	}

	me->chg_online = online;
	me->chg_enable = enable;
	pr_debug("ChargerA Status ONLINE=%d ENABLE=%d\n", me->chg_online, me->chg_enable);

	if (likely(me->charger_notify)) {
		me->charger_notify(&(me->psy), me->chg_online, me->chg_enable);
	}
out:
	return rc;
}

/* Charger - CHGINA specific callbacks */
static void chg_detect_cb(void *obj, void *param)
{
	struct max77696_charger *me = (struct max77696_charger *)obj;

	__lock(me);

	log_charger_cb_time(me, jiffies);
	chg_state_changed(me);

	__unlock(me);
	return;
}

static void log_charger_cb_time(struct max77696_charger *me, unsigned long time) {
	unsigned long delta;

	if (!last_charge_time_set) {
		last_charge_time = time;
		last_charge_time_set = true;
		return;
	}

	delta = time - last_charge_time;
	last_charge_time = time;
	
	/* if delta between charge-discharge is > Ignore delta(5 secs considered
	 * big enough), then reset the logic as this is not a USB charge-discharge
         * storm
	 */
	if(delta > CHARGE_IGNORE_BIG_DELTA) { 
		charge_history_saturated = false;
		charge_history_idx = 0;
		charge_history_total = 0;
		memset(charge_history, 0, sizeof(charge_history));
		return;
	}

	if (charge_history_saturated) {
		charge_history_total -= charge_history[charge_history_idx];
	}
	charge_history_total+=delta;
	charge_history[charge_history_idx] = delta;

	charge_history_idx = (charge_history_idx + 1) % CHARGE_HISTORY_LEN;
	if (charge_history_idx == 0) {
		charge_history_saturated = true;
	}

	if (charge_history_saturated &&
		charge_history_total < (CHARGE_HISTORY_DEBOUNCE_LIMIT*CHARGE_HISTORY_LEN)) {

		char *envp[] = {"debounce_charging=1", NULL};
		kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
		

		charge_history_saturated = false;
		charge_history_idx = 0;
		charge_history_total = 0;
		memset(charge_history, 0, sizeof(charge_history));

		cancel_delayed_work_sync(&(me->charger_debounce_work));
		max77696_charger_soft_disable(me, chga_disabled_by_user, true, otg_out_5v);
		schedule_delayed_work(&(me->charger_debounce_work), CHARGE_HISTORY_DEBOUNCE_TIMEOUT);
	}
}

static int max77696_charger_lock_config (struct max77696_charger* me,
		u8 config_reg, bool lock)
{
	u8 config_reg_bit;
	int rc;

	config_reg_bit = (1 << (config_reg - CHGA_CHG_CNFG_00_REG));

	if (unlikely(!(CHGA_PROTECTABLE_CNFG_REGS & config_reg_bit))) {
		return 0;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_06, CHGPROT, (lock? 0 : 3));
	if (unlikely(rc)) {
		dev_err(me->dev,
				"failed to un/lock config(%02X) [%d]\n", config_reg, rc);
	}

	return rc;
}

/* Set/Read charger mode */

static int max77696_charger_mode_set (struct max77696_charger* me, int mode)
{
	int rc = 0;

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_00_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_00, MODE, (u8)mode);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_00 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_00_REG, 1);
	return rc;
}

static int max77696_charger_mode_get (struct max77696_charger* me, int *mode)
{
	int rc;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_00, MODE, (u8*)mode);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_00 read error [%d]\n", rc);
		goto out;
	}

out:
	return rc;
}

/* Set/read constant current level */
static int max77696_charger_pqen_set (struct max77696_charger* me, int enable)
{
	int rc;

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_01_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_01, PQEN, (u8)(!!enable));
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_01 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_01_REG, 1);
	return rc;
}

static int max77696_charger_pqen_get (struct max77696_charger* me, int *enable)
{
	int rc;
	u8 pqsel;

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_01_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_01, PQEN, &pqsel);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_01 read error [%d]\n", rc);
		goto out;
	}

	*enable = (int)(!!pqsel);

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_01_REG, 1);
	return rc;
}

/* Set fast charge timer duration */
static int max77696_charger_fast_chg_timer_set (struct max77696_charger* me, int time)
{
	int rc;
	u8 fchg_time = 0;

	if (time) {
		fchg_time = ((time / 2) - 1);
	}

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_01_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_01, FCHGTIME, fchg_time);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_01 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_01_REG, 1);
	return rc;
}

static int max77696_charger_to_time_set (struct max77696_charger* me, int t_min)
{
	int rc;
	u8 to_time;

	to_time = (t_min / 10);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_03_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_03, TO_TIME, to_time);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_03 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_03_REG, 1);
	return rc;
}

static int max77696_charger_to_time_get (struct max77696_charger* me, int *t_min)
{
	int rc;
	u8 to_time;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_03, TO_TIME, &to_time);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_03 read error [%d]\n", rc);
		goto out;
	}

	*t_min = (to_time * 10);

out:
	return rc;
}

static const int uA_to_ith_level[] = {50000, 75000, 100000, 125000};

static int max77696_charger_to_ith_set (struct max77696_charger* me, int uA)
{
	int rc;
	u8 ith_sel;

	ith_sel = (u8)__search_index_of_the_smallest_in_be(uA_to_ith_level, uA);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_03_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_03, TO_ITH, ith_sel);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_03 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_03_REG, 1);
	return rc;
}

static int max77696_charger_to_ith_get (struct max77696_charger* me, int *uA)
{
	int rc;
	u8 ith_sel;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_03, TO_ITH, &ith_sel);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_03 read error [%d]\n", rc);
		goto out;
	}

	*uA = (int)uA_to_ith_level[ith_sel];

out:
	return rc;
}

static int max77696_charger_cc_sel_set (struct max77696_charger* me, int uA)
{
	int rc;
	u8 cc_sel;

	uA     = min(2100000, max(66667, uA));
	cc_sel = (u8)DIV_ROUND_UP(uA * 3, 100000);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_02_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_02, CHG_CC, cc_sel);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_02 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_02_REG, 1);
	return rc;
}

static int max77696_charger_cc_sel_get (struct max77696_charger* me, int *uA)
{
	int rc;
	u8 cc_sel;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_02, CHG_CC, &cc_sel);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_02 read error [%d]\n", rc);
		goto out;
	}

	cc_sel = ((cc_sel > 2)? cc_sel : 2);
	*uA    = DIV_ROUND_UP((int)cc_sel * 100000, 3); /* in 33.3mA steps */

out:
	return rc;
}

/* Set/read constant voltage level */

static const int mV_to_cv_level[] =
{
	3650, 3675, 3700, 3725, 3750, 3775, 3800, 3825, 3850, 3875, 3900, 3925,
	3950, 3975, 4000, 4025, 4050, 4075, 4100, 4125, 4150, 4175, 4200, 4225,
	4250, 4275, 4300, 4325, 4340, 4350, 4375, 4400,
};

static int max77696_charger_cv_prm_set (struct max77696_charger* me, int mV)
{
	int rc;
	u8 cv_prm;

	cv_prm = (u8)__search_index_of_the_smallest_in_be(mV_to_cv_level, mV);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_04_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_04, CHG_CV_PRM, cv_prm);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_04 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_04_REG, 1);
	return rc;
}

static int max77696_charger_cv_prm_get (struct max77696_charger* me, int *mV)
{
	int rc;
	u8 cv_prm;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_04, CHG_CV_PRM, &cv_prm);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_04 read error [%d]\n", rc);
		goto out;
	}

	*mV = (int)mV_to_cv_level[cv_prm];

out:
	return rc;
}

static int max77696_charger_cv_jta_set (struct max77696_charger* me, int mV)
{
	int rc;
	u8 cv_jta;

	cv_jta = (u8)__search_index_of_the_smallest_in_be(mV_to_cv_level, mV);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_05_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_05, CHG_CV_JTA, cv_jta);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_05 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_05_REG, 1);
	return rc;
}

static int max77696_charger_cv_jta_get (struct max77696_charger* me, int *mV)
{
	int rc;
	u8 cv_jta;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_05, CHG_CV_JTA, &cv_jta);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_05 read error [%d]\n", rc);
		goto out;
	}

	*mV = (int)mV_to_cv_level[cv_jta];

out:
	return rc;
}

static int max77696_charger_jeita_set (struct max77696_charger* me, int enable)
{
	int rc;

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_14, JEITA, (u8)(!!enable));
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 1);
	return rc;
}

static int max77696_charger_jeita_get (struct max77696_charger* me, int *enable)
{
	int rc;
	u8 jeita;

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_14, JEITA, &jeita);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 read error [%d]\n", rc);
		goto out;
	}

	*enable = (int)(!!jeita);

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 1);
	return rc;
}

/* Hi/low temp level (T1/T4) & Low temp limit (T2/T3) set/read */

static int max77696_charger_t1_set (struct max77696_charger* me, int temp_C)
{
	int rc;
	u8 t1;

	temp_C = min(0, max(-10, temp_C));
	t1     = (u8)DIV_ROUND_UP(-temp_C, 5);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_14, T1, t1);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 1);
	return rc;
}

static int max77696_charger_t1_get (struct max77696_charger* me, int *temp_C)
{
	int rc;
	u8 t1;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_14, T1, &t1);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 read error [%d]\n", rc);
		goto out;
	}

	*temp_C = ((int)t1 * -5);

out:
	return rc;
}

static int max77696_charger_t2_set (struct max77696_charger* me, int temp_C)
{
	int rc;
	u8 t2;

	temp_C = min(15, max(10, temp_C));
	t2     = (u8)DIV_ROUND_UP(temp_C - 10, 5);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_14, T2, t2);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 1);
	return rc;
}

static int max77696_charger_t2_get (struct max77696_charger* me, int *temp_C)
{
	int rc;
	u8 t2;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_14, T2, &t2);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 read error [%d]\n", rc);
		goto out;
	}

	*temp_C = ((int)t2 * 5) + 10;

out:
	return rc;
}

static int max77696_charger_t3_set (struct max77696_charger* me, int temp_C)
{
	int rc;
	u8 t3;

	temp_C = min(50, max(44, temp_C));
	t3     = (u8)DIV_ROUND_UP(50 - temp_C, 2);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_14, T3, t3);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 1);
	return rc;
}

static int max77696_charger_t3_get (struct max77696_charger* me, int *temp_C)
{
	int rc;
	u8 t3;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_14, T3, &t3);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 read error [%d]\n", rc);
		goto out;
	}

	*temp_C = (50 - ((int)t3 * 2)); 

out:
	return rc;
}

static int max77696_charger_t4_set (struct max77696_charger* me, int temp_C)
{
	int rc;
	u8 t4;

	temp_C = min(60, max(54, temp_C));
	t4     = (u8)DIV_ROUND_UP(60 - temp_C, 2);

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_14, T4, t4);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 write error [%d]\n", rc);
		goto out;
	}

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_14_REG, 1);
	return rc;
}

static int max77696_charger_t4_get (struct max77696_charger* me, int *temp_C)
{
	int rc;
	u8 t4;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_14, T4, &t4);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_14 read error [%d]\n", rc);
		goto out;
	}

	*temp_C = (60 - ((int)t4 * 2));

out:
	return rc;
}

/* Set/read charge safety timer */

#define max77696_charger_wdt_stop(me) \
	cancel_delayed_work_sync(&((me)->wdt_work))

#define max77696_charger_wdt_start(me) \
	if (likely((me)->wdt_enabled && (me)->wdt_period > 0)) {\
		if (likely(!delayed_work_pending(&((me)->wdt_work)))) {\
			schedule_delayed_work(&((me)->wdt_work), (me)->wdt_period);\
		}\
	}

static int max77696_charger_wdt_en_set (struct max77696_charger* me, int enable)
{
	int rc;

	max77696_charger_wdt_stop(me);

	enable = !!enable;

	rc = max77696_charger_lock_config(me, CHGA_CHG_CNFG_00_REG, 0);
	if (unlikely(rc)) {
		return rc;
	}

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_00, WDTEN, (u8)enable);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_00 write error [%d]\n", rc);
		goto out;
	}

	me->wdt_enabled = (bool)enable;

	max77696_charger_wdt_start(me); /* re-start wdt if enabled */

out:
	max77696_charger_lock_config(me, CHGA_CHG_CNFG_00_REG, 1);
	return rc;
}

static int max77696_charger_wdt_en_get (struct max77696_charger* me,
		int *enable)
{
	int rc;
	u8 wdten;

	rc = max77696_charger_reg_get_bit(me, CHG_CNFG_00, WDTEN, &wdten);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_00 read error [%d]\n", rc);
		goto out;
	}

	*enable = (int)(!!wdten) + (int)(!!me->wdt_enabled);

out:
	return rc;
}

	static __always_inline
int max77696_charger_wdt_period_set (struct max77696_charger* me,
		int period_ms)
{
	max77696_charger_wdt_stop(me);

	me->wdt_period = ((period_ms > 0)?
			msecs_to_jiffies((unsigned int)period_ms) : 0);

	max77696_charger_wdt_start(me); /* re-start wdt if enabled */
	return 0;
}

	static __always_inline
int max77696_charger_wdt_period_get (struct max77696_charger* me,
		int *period_ms)
{
	*period_ms = (int)jiffies_to_msecs(me->wdt_period);
	return 0;
}

/*
 * Configure the two potential software charging disable events (user and debounce)
 */
static int max77696_charger_soft_disable(struct max77696_charger* me, bool usr_disable, bool debounce_disable, bool otg_mode)
{
	int rc = 0;
	int retry = 5;
	chga_disabled_by_user = usr_disable;
	chga_disabled_by_debounce = debounce_disable;
	otg_out_5v = otg_mode;

	if (otg_out_5v) {
		/*vbus can be noisy and affect i2c, retry to make sure this packet is going through*/
		do {
			rc = max77696_charger_mode_set(me, MAX77696_CHARGER_MODE_OTG);
			if (unlikely(rc)) {
				dev_err(me->dev, "%s: error charger mode ctrli for otg (CHG) [%d] USB noise?\n", __func__, rc);
				msleep(100);
			}
		}while(rc && retry-- > 0);

		if(unlikely(rc))
			goto out;

		msleep(VBUS_RAMP_DELAY);

	} else if (!chga_disabled_by_user && !chga_disabled_by_debounce) {
		rc = max77696_charger_mode_set(me, MAX77696_CHARGER_MODE_CHG);		/* ChargerA = ON */
		if (unlikely(rc)) {
			dev_err(me->dev, "%s: error charger mode ctrl (CHG) [%d]\n", __func__, rc);
			goto out;
		}
#if defined(CONFIG_MX6SL_WARIO_BASE)
		/* set LED's to auto mode */
		rc = max77696_led_set_manual_mode(MAX77696_LED_AMBER, false);
		if (unlikely(rc)) {
			dev_err(me->dev, "%s: error led mode ctrl (amber) [%d]\n", __func__, rc);
			goto out;
		}
		rc = max77696_led_set_manual_mode(MAX77696_LED_GREEN, false);
		if (unlikely(rc)) {
			dev_err(me->dev, "%s: error led mode ctrl (green) [%d]\n", __func__, rc);
			goto out;
		}
#elif defined(CONFIG_MX6SL_WARIO_WOODY)
		if (atomic_read(&soda_usb_charger_connected)) {
			/* TURN ON LED's - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_ON);
			if (unlikely(rc)) { 
				dev_err(me->dev, "%s: error led ctrl (amber) [%d]\n", __func__, rc);
				goto out;
			}
		}
#endif
	} else {
		rc = max77696_charger_mode_set(me, MAX77696_CHARGER_MODE_OFF);
		if (unlikely(rc)) {
			dev_err(me->dev, "%s: error charger mode ctrl (OFF) [%d]\n", __func__, rc);
			goto out;
		}

		/* TURN OFF LED's - manual mode */
		rc = max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_OFF);
		if (unlikely(rc)) { 
			dev_err(me->dev, "%s: error led ctrl (amber) [%d]\n", __func__, rc);
			goto out;
		}
		rc = max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
		if (unlikely(rc)) {
			dev_err(me->dev, "%s: error led ctrl (green) [%d]\n", __func__, rc);
			goto out;
		}
#if defined(CONFIG_MX6SL_WARIO_BASE)
		green_led_set = false;
#endif
	}
out:
	return chg_state_changed(me);
}

static int max77696_charger_charging_set (struct max77696_charger *me, int enable)
{
	return max77696_charger_allow_charging_set(me, enable);
}

static int max77696_charger_charging_get (struct max77696_charger *me, int *enable)
{
	int rc = 0;
	*enable = 0;

	if (is_charger_a_connected(me)) {
		if (is_charger_a_enabled(me))
			*enable = 1;
	}
	return rc;
}

static int max77696_charger_allow_charging_set (struct max77696_charger *me, int enable) {
	return max77696_charger_soft_disable(me, !enable, chga_disabled_by_debounce, otg_out_5v);
}

int max77696_charger_set_otg(int enable) {
	return max77696_charger_soft_disable(g_max77696_charger, chga_disabled_by_user, chga_disabled_by_debounce, enable);
}
EXPORT_SYMBOL(max77696_charger_set_otg);

static int max77696_charger_allow_charging_get (struct max77696_charger *me, int *enable) {
	*enable = !chga_disabled_by_user;
	return 0;
}

static int max77696_charger_debounce_charging_set (struct max77696_charger *me, int enable) {
	//Only allow the sys entry to turn debounce detection off (and re-enable charging)
	if (!enable) {
		return max77696_charger_soft_disable(me, chga_disabled_by_user, false, otg_out_5v);
	}
	return 0;
}

static int max77696_charger_debounce_charging_get(struct max77696_charger *me, int *enable) {
	*enable = chga_disabled_by_debounce;
	return 0;
}

static int max77696_charger_lpm_set(struct max77696_charger* me, bool enable) 
{
	int rc = 0;

	rc = max77696_charger_reg_set_bit(me, CHG_CNFG_12, CHG_LPM, enable ? 1 : 0);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_12 write error [%d]\n", rc);
	}

	return rc;
}

static bool is_charger_a_connected(struct max77696_charger *me)
{
	u8 status = 0;
	int rc = 0;
	bool conn = false;

	rc = max77696_charger_reg_get_bit(me, CHG_INT_OK, CHGINA, &status);
	if (unlikely(rc)) {
		dev_err(me->dev, "%s CHG_INT_OK read error [%d]\n",__func__, rc);
		goto out;
	}
	if (status)
		conn = true;
out:
	return conn;
}

static bool is_charger_a_enabled(struct max77696_charger *me)
{
	int mode = 0;
	int rc = 0;
	bool stat = false;

	rc = max77696_charger_mode_get(me, &mode);
	if (unlikely(rc)) {
		goto out;
	}

	if (mode == MAX77696_CHARGER_MODE_CHG) 
		stat = true;
out:
	return stat; 
}

static void max77696_charger_wdt_ping (struct max77696_charger* me)
{
	max77696_charger_reg_set_bit(me, CHG_CNFG_06, WDTCLR, 1);
	dev_noise(me->dev, "watchdog timer cleared\n");
}

static void max77696_gauge_driftadj_work(struct work_struct *work)
{
	int rc = 0;
	struct max77696_charger *me = container_of(work, struct max77696_charger, gauge_driftadj_work.work);
	
	/* Note: gauge drift issue (j42-5579)
	 * Fix gauge abnormal SOC(%) issue by adjusting gauge REPCAP/FULLCAP 
	 * Triggerred after reaching charger done state.
	 */ 
	rc = max77696_gauge_driftadj_handler();
	if (unlikely(rc)) 
		dev_err(me->dev, "%s: gauge driftadj failed post charger done [%d]\n", __func__, rc);

	return;
}

#if defined(CONFIG_MX6SL_WARIO_WOODY)
static void max77696_charger_ctv_adjust_work (struct work_struct *work)
{
	int rc = 0;
	struct max77696_charger *me = container_of(work, struct max77696_charger, ctv_adjust_work.work);
	char buf[32];

	if (atomic_read(&soda_usb_charger_connected)) {
		/* enable battery force relax: set charger termination voltage to 4.1V */
		rc = max77696_charger_cv_prm_set(me, CHARGER_CTV_4P1V);
		if (unlikely(rc)) {
			printk(KERN_ERR "KERNEL: E pmic:charger termination voltage::error set to 4.1V \n");
			LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
				"kernel", "charger", "error set ctv to 4p1v", 1, "");
		} else {
			printk(KERN_INFO "KERNEL: I pmic:charger termination voltage::set to 4.1V \n");
			sprintf(buf, "%dmV", wario_battery_voltage);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
				"kernel", "charger", "ctv4p1v", 1, buf);
			force_green_led = true;
		}
	}
	return;
}

int max77696_charger_ctv_reset (struct max77696_charger* me)
{
	int rc = 0, ctv = 0;

	cancel_delayed_work_sync(&(me->ctv_adjust_work));
		
	/* check charge termination voltage & reset to 4.2V */
	rc = max77696_charger_cv_prm_get(me, &ctv);
	if (unlikely(rc)) 
		printk(KERN_ERR "KERNEL: E pmic:charger termination voltage::error reading\n");
	else {
		if (ctv != CHARGER_CTV_4P2V) {
			/* disable battery force relax: reset charger termination voltage to 4.2V */
			rc = max77696_charger_cv_prm_set(me, CHARGER_CTV_4P2V);
			if (unlikely(rc)) 
				printk(KERN_ERR "KERNEL: E pmic:charger termination voltage::error reset to 4.2V \n");
			else {
				printk(KERN_INFO "KERNEL: I pmic:charger termination voltage::reset to 4.2V \n");
				LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
					"kernel", "charger", "ctv4p2v", 1, "");
			}
		}
	}

	return rc;
}
#endif

static void max77696_charger_wdt_work (struct work_struct *work)
{
	struct max77696_charger *me = __wdt_work_to_max77696_charger(work);

	__lock(me);

	max77696_charger_wdt_ping(me);
	max77696_charger_wdt_start(me);

	__unlock(me);
	return;
}

static void max77696_gauge_vmn_work(struct work_struct *work)
{
	struct max77696_charger *me = container_of(work, struct max77696_charger, gauge_vmn_work.work);

	/* Invoke all cb's for low battery event */
	pmic_event_callback(ENCODE_EVENT((me->gauge_irq - me->chip->irq_base), MAX77696_FG_INT_VMN));
	return;
}

static void max77696_gauge_vmx_work(struct work_struct *work)
{
	struct max77696_charger *me = container_of(work, struct max77696_charger, gauge_vmx_work.work);

	/* Invoke all cb's for over voltage event */
	pmic_event_callback(ENCODE_EVENT((me->gauge_irq - me->chip->irq_base), MAX77696_FG_INT_VMX));
	return;
}

static void max77696_gauge_smn_work(struct work_struct *work)
{
	struct max77696_charger *me = container_of(work, struct max77696_charger, gauge_smn_work.work);

	/* Invoke all cb's for soc min battery event */
	pmic_event_callback(ENCODE_EVENT((me->gauge_irq - me->chip->irq_base), MAX77696_FG_INT_SMN));
	return;
}

static void max77696_gauge_smx_work(struct work_struct *work)
{
	struct max77696_charger *me = container_of(work, struct max77696_charger, gauge_smx_work.work);

	/* Invoke all cb's for soc max battery event */
	pmic_event_callback(ENCODE_EVENT((me->gauge_irq - me->chip->irq_base), MAX77696_FG_INT_SMX));
	return;
}

static void max77696_gauge_tmn_work(struct work_struct *work)
{
	struct max77696_charger *me = container_of(work, struct max77696_charger, gauge_tmn_work.work);

	/* Invoke all cb's for low temp event */
	pmic_event_callback(ENCODE_EVENT((me->gauge_irq - me->chip->irq_base), MAX77696_FG_INT_TMN));
	return;
}

static void max77696_gauge_tmx_work(struct work_struct *work)
{
	struct max77696_charger *me = container_of(work, struct max77696_charger, gauge_tmx_work.work);

	/* Invoke all cb's for crit temp event */
	pmic_event_callback(ENCODE_EVENT((me->gauge_irq - me->chip->irq_base), MAX77696_FG_INT_TMX));
	return;
}

static irqreturn_t max77696_gauge_isr (int irq, void *data)
{
	struct max77696_charger *me = data;
	u8 interrupted;

	/* read INT register to clear bits ASAP */
	max77696_irq_read_fgirq_status(&interrupted);
	dev_dbg(me->dev, "FG_INT %02X\n", interrupted);
#if defined(CONFIG_FALCON) && !defined(DEBUG)
	if(in_falcon()){
		printk(KERN_DEBUG "FG_INT %02X\n", interrupted);
	}
#endif

	if (interrupted & MAX77696_FG_INT_VMN) {
		schedule_delayed_work(&(me->gauge_vmn_work), msecs_to_jiffies(GAUGE_WORK_DELAY));
	}

	if (interrupted & MAX77696_FG_INT_VMX) {
		schedule_delayed_work(&(me->gauge_vmx_work), msecs_to_jiffies(GAUGE_WORK_DELAY));
	}

	if (interrupted & MAX77696_FG_INT_SMN) {
		schedule_delayed_work(&(me->gauge_smn_work), msecs_to_jiffies(GAUGE_WORK_DELAY));
	}

	if (interrupted & MAX77696_FG_INT_SMX) {
		schedule_delayed_work(&(me->gauge_smx_work), msecs_to_jiffies(GAUGE_WORK_DELAY));
	}

	if (interrupted & MAX77696_FG_INT_TMN) {
		schedule_delayed_work(&(me->gauge_tmn_work), msecs_to_jiffies(GAUGE_WORK_DELAY));
	}

	if (interrupted & MAX77696_FG_INT_TMX) {
		schedule_delayed_work(&(me->gauge_tmx_work), msecs_to_jiffies(GAUGE_WORK_DELAY));
	}

	return IRQ_HANDLED;
}

static void max77696_glbl_critbat_work (struct work_struct *work)
{
	/* invoke all subscribed cb's */
	pmic_event_callback(EVENT_TOPS_MLOBAT);
	return;
}

static irqreturn_t max77696_glbl_crit_bat_isr(int irq, void *data)
{
	struct max77696_charger *me = data;
	schedule_delayed_work(&(me->glbl_critbat_work), msecs_to_jiffies(GLBL_CRITBAT_WORK_DELAY));
	return IRQ_HANDLED;
}

static void max77696_charger_chgina_work (struct work_struct *work)
{
	struct max77696_charger *me = __chgina_work_to_max77696_charger(work);

	/* Invoke all cb's for charger detect event */
	pmic_event_callback(ENCODE_EVENT((me->irq - me->chip->irq_base), CHGA_INT_CHGINA));
	if(is_charger_a_connected(me)) {
		pmic_event_callback(EVENT_SW_CHGRIN);
	} else {
		pmic_event_callback(EVENT_SW_CHGROUT);
	}
	return;
}

static void max77696_charger_chgsts_work (struct work_struct *work)
{
	struct max77696_charger *me = __chgsts_work_to_max77696_charger(work);

	/* Invoke all cb's for charger status event */
	pmic_event_callback(ENCODE_EVENT((me->irq - me->chip->irq_base), CHGA_INT_CHG));
	return;
}

static void max77696_charger_thmsts_work (struct work_struct *work)
{
	struct max77696_charger *me = __thmsts_work_to_max77696_charger(work);

	/* Invoke all cb's for charger thermistor status event */
	pmic_event_callback(ENCODE_EVENT((me->irq - me->chip->irq_base), CHGA_INT_THM));
	return;
}

static void max77696_charger_debounce_timeout(struct work_struct *work)
{
	struct max77696_charger *me = container_of(work, struct max77696_charger, charger_debounce_work.work);
	max77696_charger_soft_disable(me, chga_disabled_by_user, false, otg_out_5v);
}

#if defined(CONFIG_MX6SL_WARIO_BASE)
static void max77696_charger_monitor_work(struct work_struct *work)
{
	int rc = 0;
	struct max77696_charger *me = container_of(work, struct max77696_charger,charger_monitor_work.work);

	/* Do not proceed if charger is not connected */
	if (!is_charger_a_connected(me) || !wario_battery_valid) 
		return;

	if( chga_disabled_by_user )
		goto retry;
	
	if (!green_led_set) {
		if (wario_battery_capacity > WARIO_GREEN_LED_THRESHOLD) {
			/* TURN OFF Amber LED - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_OFF);
			if (unlikely(rc)) {
				dev_err(me->dev, "%s: error led ctrl (amber) [%d]\n", __func__, rc);
				goto retry;
			}
			/* Turn ON green led - manual mode */
			rc = max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_ON);
			if (unlikely(rc)) { 
				dev_err(me->dev, "%s: error led ctrl (green) [%d]\n", __func__, rc);
				goto retry;
			}
			green_led_set = true;
		} else {
			/* set LED's to auto mode */
			rc = max77696_led_set_manual_mode(MAX77696_LED_AMBER, false);
			if (unlikely(rc)) {
				dev_err(me->dev, "%s: error led mode ctrl (amber) [%d]\n", __func__, rc);
				goto retry;
			}
			rc = max77696_led_set_manual_mode(MAX77696_LED_GREEN, false);
			if (unlikely(rc)) {
				dev_err(me->dev, "%s: error led mode ctrl (green) [%d]\n", __func__, rc);
				goto retry;
			}
		}
	}

retry:
	/* schedule charger monitor */
	schedule_delayed_work(&(me->charger_monitor_work), msecs_to_jiffies(CHARGER_MON_RUN));
	return;
}
#endif

static irqreturn_t max77696_charger_isr (int irq, void *data)
{
	struct max77696_charger *me = data;

	/* read INT register to clear bits ASAP */
	max77696_charger_reg_read(me, CHG_INT, &(me->interrupted));
	dev_dbg(me->dev, "CHG_INT %02X EN %02X\n", me->interrupted, me->irq_unmask);
#if defined(CONFIG_FALCON) && !defined(DEBUG)
	if(in_falcon()){
		printk(KERN_DEBUG "CHG_INT %02X EN %02X\n", me->interrupted, me->irq_unmask);
	}
#endif

	if (me->interrupted & CHGA_INT_CHGINA) {
		__show_details(me, 0, CHGINA);
		if (likely(!delayed_work_pending(&(me->chgina_work)))) {
			schedule_delayed_work(&(me->chgina_work), msecs_to_jiffies(CHGA_WORK_DELAY));
		}
	}

	if (me->interrupted & CHGA_INT_THM) {
		__show_details(me, 0, THM);
		if (likely(!delayed_work_pending(&(me->thmsts_work)))) {
			schedule_delayed_work(&(me->thmsts_work), msecs_to_jiffies(CHGA_WORK_DELAY));
		}
	}

	if (me->interrupted & CHGA_INT_CHG) {
		__show_details(me, 1, CHG);
		if (likely(!delayed_work_pending(&(me->chgsts_work)))) {
			schedule_delayed_work(&(me->chgsts_work), msecs_to_jiffies(CHGA_WORK_DELAY));
		}
	}

	if (me->interrupted & CHGA_INT_BAT) {
		__show_details(me, 1, BAT);
		/* TODO: add later */
	}

	return IRQ_HANDLED;
}

static int max77696_charger_get_property (struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct max77696_charger *me = __psy_to_max77696_charger(psy);
	int rc = 0;

	__lock(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = me->chg_online;
			break;
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = me->chg_enable;
			break;
		default:
			rc = -EINVAL;
			break;
	}

	__unlock(me);
	return rc;
}

static enum power_supply_property max77696_charger_psy_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static __devinit int max77696_charger_probe (struct platform_device *pdev)
{
	struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77696_charger_platform_data *pdata = pdev->dev.platform_data;
	struct max77696_charger *me;
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
	me->kobj = &(pdev->dev.kobj);

	me->irq = chip->irq_base + MAX77696_ROOTINT_CHGA;
	me->gauge_irq = chip->irq_base + MAX77696_ROOTINT_FG;
	me->critbat_irq = chip->irq_base + MAX77696_ROOTINT_NR_IRQS + MAX77696_TOPSYSINT_BATT_LOW;

	me->psy.name            = MAX77696_PSY_CHG_NAME;
	me->psy.type            = POWER_SUPPLY_TYPE_MAINS;
	me->psy.get_property    = max77696_charger_get_property;
	me->psy.properties      = max77696_charger_psy_props;
	me->psy.num_properties  = ARRAY_SIZE(max77696_charger_psy_props);
	me->psy.supplied_to     = pdata->batteries;
	me->psy.num_supplicants = pdata->num_batteries;

	INIT_DELAYED_WORK(&(me->chgina_work), max77696_charger_chgina_work);
	INIT_DELAYED_WORK(&(me->chgsts_work), max77696_charger_chgsts_work);
	INIT_DELAYED_WORK(&(me->thmsts_work), max77696_charger_thmsts_work);
#if defined(CONFIG_MX6SL_WARIO_BASE)
	INIT_DELAYED_WORK(&(me->charger_monitor_work), max77696_charger_monitor_work);
#endif
	INIT_DELAYED_WORK(&(me->charger_debounce_work), max77696_charger_debounce_timeout);
	INIT_DELAYED_WORK(&(me->wdt_work), max77696_charger_wdt_work);
	INIT_DELAYED_WORK(&(me->gauge_vmn_work), max77696_gauge_vmn_work);
	INIT_DELAYED_WORK(&(me->glbl_critbat_work), max77696_glbl_critbat_work);
	INIT_DELAYED_WORK(&(me->gauge_vmx_work), max77696_gauge_vmx_work);
	INIT_DELAYED_WORK(&(me->gauge_smn_work), max77696_gauge_smn_work);
	INIT_DELAYED_WORK(&(me->gauge_smx_work), max77696_gauge_smx_work);
	INIT_DELAYED_WORK(&(me->gauge_tmn_work), max77696_gauge_tmn_work);
	INIT_DELAYED_WORK(&(me->gauge_tmx_work), max77696_gauge_tmx_work);
	INIT_DELAYED_WORK(&(me->gauge_driftadj_work), max77696_gauge_driftadj_work);
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	INIT_DELAYED_WORK(&(me->ctv_adjust_work), max77696_charger_ctv_adjust_work);
#endif

	/* Initial configurations */
	if (pdata->initial_mode) {
		max77696_charger_mode_set(me, pdata->initial_mode);
	}
	max77696_charger_wdt_period_set(me, pdata->wdt_period_ms);
	max77696_charger_wdt_en_set(me, (pdata->wdt_period_ms > 0));
	max77696_charger_cc_sel_set(me, pdata->cc_uA);		
	max77696_charger_cv_prm_set(me, pdata->cv_prm_mV);
	max77696_charger_cv_jta_set(me, pdata->cv_jta_mV);
	max77696_charger_to_time_set(me, pdata->to_time);
	max77696_charger_to_ith_set(me, pdata->to_ith);
	max77696_charger_fast_chg_timer_set(me, pdata->fast_chg_time);
	max77696_charger_t1_set(me, pdata->t1_C);
	max77696_charger_t2_set(me, pdata->t2_C);
	max77696_charger_t3_set(me, pdata->t3_C);
	max77696_charger_t4_set(me, pdata->t4_C);
	if (pdata->chg_dc_lpm)
		max77696_charger_lpm_set(me, pdata->chg_dc_lpm);

	me->icl_ilim = pdata->icl_ilim;

	/* Disable all charger interrupts */
	max77696_charger_disable_irq(me, 0xFF, 1);

	rc = max77696_charger_sysdev_register();
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to create charger attribute group [%d]\n", rc);
		goto out_err_sysfs;
	}

	rc = power_supply_register(me->dev, &(me->psy));
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register MAIN psy device [%d]\n", rc);
		goto out_err_reg_psy;
	}

	me->charger_notify = pdata->charger_notify;
	/* First time update */
	chg_detect_cb(me, NULL);

	/* Request charger interrupt */
	rc = request_threaded_irq(me->irq, NULL, 
			max77696_charger_isr, IRQF_ONESHOT, DRIVER_NAME, me);
	if (unlikely(rc < 0)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n", me->irq, rc);
		goto out_err_req_chg_irq;
	}

	/* Configure wakeup capable */
	device_set_wakeup_capable(me->dev, 1);
	device_set_wakeup_enable (me->dev, pdata->wakeup_irq);

	/* register and subscribe to chgina events */
	rc = max77696_eventhandler_register(&max77696_chgina_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_chgina_handle.event_id, rc);
		goto out_err_reg_chgina;
	}

	/* register and subscribe to chgsts events */
	rc = max77696_eventhandler_register(&max77696_chgsts_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_chgsts_handle.event_id, rc);
		goto out_err_reg_chgsts;
	}

	/* register and subscribe to thmsts events */
	rc = max77696_eventhandler_register(&max77696_thmsts_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_thmsts_handle.event_id, rc);
		goto out_err_reg_thmsts;
	}
	max77696_irq_disable_fgirq(0xFF, 1);

	/* register to events */
	rc = max77696_eventhandler_register(&max77696_fg_lobat_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_fg_lobat_handle.event_id, rc);
		goto out_err_reg_lobat_handler;	
	}

	rc = max77696_eventhandler_register(&max77696_fg_overvolt_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_fg_overvolt_handle.event_id, rc);
		goto out_err_reg_ov_handler;	
	}

	rc = max77696_eventhandler_register(&max77696_fg_crittemp_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_fg_crittemp_handle.event_id, rc);
		goto out_err_reg_crittemp_handler;	
	}

	rc = max77696_eventhandler_register(&max77696_fg_lotemp_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_fg_lotemp_handle.event_id, rc);
		goto out_err_reg_lotemp_handler;	
	}

	rc = max77696_eventhandler_register(&max77696_fg_maxsoc_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_fg_maxsoc_handle.event_id, rc);
		goto out_err_reg_maxsoc_handler;	
	}

	rc = max77696_eventhandler_register(&max77696_fg_minsoc_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_fg_minsoc_handle.event_id, rc);
		goto out_err_reg_minsoc_handler;	
	}

	rc = max77696_eventhandler_register(&max77696_chgrin_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_chgrin_handle.event_id, rc);
		goto out_err_reg_chgrin_handler;	
	}

	rc = max77696_eventhandler_register(&max77696_chgrout_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_chgrout_handle.event_id, rc);
		goto out_err_reg_chgrout_handler;	
	}

	rc = request_threaded_irq(me->gauge_irq,
			NULL, max77696_gauge_isr, IRQF_ONESHOT, "max77696_gauge", me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n", me->gauge_irq, rc);
		goto out_err_req_lobat_irq;
	}

	rc = request_threaded_irq(me->critbat_irq, NULL,
			max77696_glbl_crit_bat_isr, IRQF_ONESHOT, "max77696_glbl_critbat", me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n", me->critbat_irq, rc);
		goto out_err_req_critbat_irq;
	}

	/* subscribe to chga-ina event */
	chg_detect.param = me;
	chg_detect.func = chg_detect_cb;
	rc = pmic_event_subscribe(EVENT_CHGA_INA, &chg_detect);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (0x%x) [%d]\n", EVENT_CHGA_INA, rc);
		goto out_err_sub_chgina;
	}

	/* subscribe to chga-chg event */
	chg_status.param = me;
	chg_status.func = chg_status_cb;
	rc = pmic_event_subscribe(EVENT_CHGA_CHG, &chg_status);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (0x%x) [%d]\n", EVENT_CHGA_CHG, rc);
		goto out_err_sub_chgsts;
	}

	/* subscribe to chga-thm event */
	thm_status.param = me;
	thm_status.func = thm_status_cb;
	rc = pmic_event_subscribe(EVENT_CHGA_THM, &thm_status);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (0x%x) [%d]\n", EVENT_CHGA_THM, rc);
		goto out_err_sub_thmsts;
	}

	/* subscribe to fg lobat event */
	fg_lobat_evt.param = me;
	fg_lobat_evt.func = max77696_gauge_lobat_cb;
	rc = pmic_event_subscribe(EVENT_FG_VMN, &fg_lobat_evt);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (0x%x) [%d]\n", EVENT_FG_VMN, rc);
		goto out_err_sub_lobat;
	}

	/* subscribe to fg critbat event */
	fg_critbat_evt.param = me;
	fg_critbat_evt.func = max77696_gauge_critbat_cb;
	rc = pmic_event_subscribe(EVENT_TOPS_MLOBAT, &fg_critbat_evt);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (0x%x) [%d]\n", EVENT_TOPS_MLOBAT, rc);
		goto out_err_sub_critbat;
	}

	/* subscribe to fg over voltage event */
	fg_overvolt_evt.param = me;
	fg_overvolt_evt.func = max77696_gauge_overvolt_cb;
	rc = pmic_event_subscribe(EVENT_FG_VMX, &fg_overvolt_evt);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (0x%x) [%d]\n", EVENT_FG_VMX, rc);
		goto out_err_sub_ov;
	}

	/* subscribe to fg temp max (critical) event */
	fg_crittemp_evt.param = me;
	fg_crittemp_evt.func = max77696_gauge_crittemp_cb;
	rc = pmic_event_subscribe(EVENT_FG_TMX, &fg_crittemp_evt);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (0x%x) [%d]\n", EVENT_FG_TMX, rc);
		goto out_err_sub_crittemp;
	}

	/* subscribe to fg temp min event */
	fg_lotemp_evt.param = me;
	fg_lotemp_evt.func = max77696_gauge_lotemp_cb;
	rc = pmic_event_subscribe(EVENT_FG_TMN, &fg_lotemp_evt);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (0x%x) [%d]\n", EVENT_FG_TMN, rc);
		goto out_err_sub_lotemp;
	}

#if defined(CONFIG_POWER_SODA)
	/* For Whisky-Soda Duet arch, 
         * init params for SOC Batt Hi and Low handlers */
        fg_maxsoc_evt.param = me;
	fg_maxsoc_evt.func = max77696_gauge_maxsoc_cb;

	fg_minsoc_evt.param = me;
	fg_minsoc_evt.func = max77696_gauge_minsoc_cb;
#endif

	BUG_ON(chip->chg_ptr);
	chip->chg_ptr = me;
	g_max77696_charger = me;

	pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
	SUBDEVICE_SET_LOADED(charger, chip);
	return 0;

out_err_sub_lotemp:
	pmic_event_unsubscribe(EVENT_FG_TMX, &fg_crittemp_evt);
out_err_sub_crittemp:
	pmic_event_unsubscribe(EVENT_FG_VMX, &fg_overvolt_evt);
out_err_sub_ov:
	pmic_event_unsubscribe(EVENT_TOPS_MLOBAT, &fg_critbat_evt);
out_err_sub_critbat:
	pmic_event_unsubscribe(EVENT_FG_VMN, &fg_lobat_evt);
out_err_sub_lobat:
	pmic_event_unsubscribe(EVENT_CHGA_THM, &thm_status);
out_err_sub_thmsts:
	pmic_event_unsubscribe(EVENT_CHGA_CHG, &chg_status);
out_err_sub_chgsts:
	pmic_event_unsubscribe(EVENT_CHGA_INA, &chg_detect);
out_err_sub_chgina:
	free_irq(me->critbat_irq, me);
out_err_req_critbat_irq:
	free_irq(me->gauge_irq, me);
out_err_req_lobat_irq:
	max77696_eventhandler_unregister(&max77696_chgrout_handle);
out_err_reg_chgrout_handler:
	max77696_eventhandler_unregister(&max77696_chgrin_handle);
out_err_reg_chgrin_handler:
	max77696_eventhandler_unregister(&max77696_fg_minsoc_handle);
out_err_reg_minsoc_handler:
	max77696_eventhandler_unregister(&max77696_fg_maxsoc_handle);
out_err_reg_maxsoc_handler:
	max77696_eventhandler_unregister(&max77696_fg_lotemp_handle);
out_err_reg_lotemp_handler:
	max77696_eventhandler_unregister(&max77696_fg_crittemp_handle);
out_err_reg_crittemp_handler:
	max77696_eventhandler_unregister(&max77696_fg_overvolt_handle);
out_err_reg_ov_handler:
	max77696_eventhandler_unregister(&max77696_fg_lobat_handle);
out_err_reg_lobat_handler:
	max77696_eventhandler_unregister(&max77696_thmsts_handle);
out_err_reg_thmsts:
	max77696_eventhandler_unregister(&max77696_chgsts_handle);
out_err_reg_chgsts:
	max77696_eventhandler_unregister(&max77696_chgina_handle);
out_err_reg_chgina:
	free_irq(me->irq, me);
out_err_req_chg_irq:
	power_supply_unregister(&(me->psy));
out_err_reg_psy:
	max77696_charger_sysdev_unregister();
out_err_sysfs:
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);
	return rc;
}

static __devexit int max77696_charger_remove (struct platform_device *pdev)
{
	struct max77696_charger *me = platform_get_drvdata(pdev);

	g_max77696_charger = NULL;
	me->chip->chg_ptr = NULL;

	/* Unsubscribe - events */
	unsubscribe_socbatt_min();
	unsubscribe_socbatt_max();
	pmic_event_unsubscribe(EVENT_FG_TMN, &fg_lotemp_evt);
	pmic_event_unsubscribe(EVENT_FG_TMX, &fg_crittemp_evt);
	pmic_event_unsubscribe(EVENT_FG_VMX, &fg_overvolt_evt);
	pmic_event_unsubscribe(EVENT_FG_VMN, &fg_lobat_evt);
	pmic_event_unsubscribe(EVENT_TOPS_MLOBAT, &fg_critbat_evt);
	pmic_event_unsubscribe(EVENT_CHGA_THM, &thm_status);
	pmic_event_unsubscribe(EVENT_CHGA_CHG, &chg_status);
	pmic_event_unsubscribe(EVENT_CHGA_INA, &chg_detect);

	cancel_delayed_work_sync(&(me->glbl_critbat_work));
	free_irq(me->critbat_irq, me);

	cancel_delayed_work_sync(&(me->gauge_vmn_work));
	cancel_delayed_work_sync(&(me->gauge_vmx_work));		
	cancel_delayed_work_sync(&(me->gauge_smn_work));
	cancel_delayed_work_sync(&(me->gauge_smx_work));		
	cancel_delayed_work_sync(&(me->gauge_tmn_work));		
	cancel_delayed_work_sync(&(me->gauge_tmx_work));		
	free_irq(me->gauge_irq, me);
	
	/* Unregister - eventhandlers */
	max77696_eventhandler_unregister(&max77696_fg_lotemp_handle);
	max77696_eventhandler_unregister(&max77696_fg_crittemp_handle);
	max77696_eventhandler_unregister(&max77696_fg_overvolt_handle);
	max77696_eventhandler_unregister(&max77696_fg_lobat_handle);
	max77696_eventhandler_unregister(&max77696_fg_minsoc_handle);
	max77696_eventhandler_unregister(&max77696_fg_maxsoc_handle);
	max77696_eventhandler_unregister(&max77696_chgrin_handle);
	max77696_eventhandler_unregister(&max77696_chgrout_handle);
	max77696_eventhandler_unregister(&max77696_thmsts_handle);
	max77696_eventhandler_unregister(&max77696_chgsts_handle);
	max77696_eventhandler_unregister(&max77696_chgina_handle);

	free_irq(me->irq, me);
	cancel_delayed_work_sync(&(me->chgina_work));
	cancel_delayed_work_sync(&(me->chgsts_work));
	cancel_delayed_work_sync(&(me->thmsts_work));
#if defined(CONFIG_MX6SL_WARIO_BASE)
	cancel_delayed_work_sync(&(me->charger_monitor_work));
#endif
	cancel_delayed_work_sync(&(me->gauge_driftadj_work));
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	cancel_delayed_work_sync(&(me->ctv_adjust_work));
#endif
	cancel_delayed_work_sync(&(me->charger_debounce_work));

	max77696_charger_wdt_stop(me);
	max77696_charger_sysdev_unregister();
	power_supply_unregister(&(me->psy));

	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77696_charger_suspend (struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_charger *me = platform_get_drvdata(pdev);
#if defined(CONFIG_MX6SL_WARIO_BASE)
	gpio_wan_ldo_fet_ctrl(0);	/* FET */
#endif

	cancel_delayed_work_sync(&(me->gauge_vmn_work));		
	cancel_delayed_work_sync(&(me->glbl_critbat_work));
#if defined(CONFIG_MX6SL_WARIO_BASE)
	cancel_delayed_work_sync(&(me->charger_monitor_work));
#endif
	cancel_delayed_work_sync(&(me->gauge_vmx_work));		
	cancel_delayed_work_sync(&(me->gauge_smn_work));		
	cancel_delayed_work_sync(&(me->gauge_smx_work));		
	cancel_delayed_work_sync(&(me->gauge_tmn_work));		
	cancel_delayed_work_sync(&(me->gauge_tmx_work));		
	cancel_delayed_work_sync(&(me->gauge_driftadj_work));
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	cancel_delayed_work_sync(&(me->ctv_adjust_work));
#endif

	if (likely(device_may_wakeup(dev))) {
		enable_irq_wake(me->irq);
		enable_irq_wake(me->gauge_irq);
		enable_irq_wake(me->critbat_irq);
	}

	return 0;
}

static int max77696_charger_resume (struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_charger *me = platform_get_drvdata(pdev);
#if defined(CONFIG_MX6SL_WARIO_BASE)
	gpio_wan_ldo_fet_ctrl(1);	/* LDO */
#endif

	if (likely(device_may_wakeup(dev))) {
		disable_irq_wake(me->irq);
		disable_irq_wake(me->gauge_irq);
		disable_irq_wake(me->critbat_irq);
	}
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(max77696_charger_pm, max77696_charger_suspend, max77696_charger_resume);

static struct platform_driver max77696_charger_driver = {
	.driver.name  = DRIVER_NAME,
	.driver.owner = THIS_MODULE,
	.driver.pm    = &max77696_charger_pm,
	.probe        = max77696_charger_probe,
	.remove       = __devexit_p(max77696_charger_remove),
};

static __init int max77696_charger_driver_init (void)
{
	return platform_driver_register(&max77696_charger_driver);
}

static __exit void max77696_charger_driver_exit (void)
{
	platform_driver_unregister(&max77696_charger_driver);
}

late_initcall(max77696_charger_driver_init);
module_exit(max77696_charger_driver_exit);

/*******************************************************************************
 * EXTERNAL FUNCTIONS
 ******************************************************************************/

int max77696_charger_dc_dc_lpm (bool enable)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_lpm_set(me, enable);
	__unlock(me);
	return rc;
}
EXPORT_SYMBOL(max77696_charger_dc_dc_lpm);

/* Set Charger-A ICL  */
int max77696_charger_set_icl (void)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);

	rc = max77696_charger_reg_write(me, CHG_CNFG_09, me->icl_ilim);
	if (unlikely(rc)) {
		dev_err(me->dev, "CHG_CNFG_09 write error [%d]\n", rc);
	}

	__unlock(me);
	return rc;
}
EXPORT_SYMBOL(max77696_charger_set_icl);

int max77696_charger_set_mode (int mode)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc = 0;

	__lock(me);
	rc = max77696_charger_mode_set(me, mode);
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_set_mode);

int max77696_charger_get_mode (int *mode)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_mode_get(me, mode);
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_get_mode);

int max77696_charger_set_pq_en (int enable)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_pqen_set(me, enable);
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_set_pq_en);

int max77696_charger_get_pq_en (int *enable)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_pqen_get(me, enable);
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_get_pq_en);

int max77696_charger_set_cc_level (int uA)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_cc_sel_set(me, uA);
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_set_cc_level);

int max77696_charger_get_cc_level (int *uA)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_cc_sel_get(me, uA);
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_get_cc_level);

int max77696_charger_set_jeita_en (int enable)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_jeita_set(me, enable);
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_set_jeita_en);

int max77696_charger_get_jeita_en (int *enable)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_jeita_get(me, enable);
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_get_jeita_en);

int max77696_charger_set_cv_level (bool jeita, int mV)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	if (jeita) {
		rc = max77696_charger_cv_jta_set(me, mV);
	} else {
		rc = max77696_charger_cv_prm_set(me, mV);
	}
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_set_cv_level);

int max77696_charger_get_cv_level (bool jeita, int *mV)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	if (jeita) {
		rc = max77696_charger_cv_jta_get(me, mV);
	} else {
		rc = max77696_charger_cv_prm_get(me, mV);
	}
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_get_cv_level);

int max77696_charger_set_temp_thres (int t_id, int temp_C)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	if (t_id == 1) {
		rc = max77696_charger_t1_set(me, temp_C);
	} else if (t_id == 2) {
		rc = max77696_charger_t2_set(me, temp_C);
	} else if (t_id == 3) {
		rc = max77696_charger_t3_set(me, temp_C);
	} else if (t_id == 4) {
		rc = max77696_charger_t4_set(me, temp_C);
	} else {
		rc = -EINVAL;
	}
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_set_temp_thres);

int max77696_charger_get_temp_thres (int t_id, int *temp_C)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	if (t_id == 1) {
		rc = max77696_charger_t1_get(me, temp_C);
	} else if (t_id == 2) {
		rc = max77696_charger_t2_get(me, temp_C);
	} else if (t_id == 3) {
		rc = max77696_charger_t3_get(me, temp_C);
	} else if (t_id == 4) {
		rc = max77696_charger_t4_get(me, temp_C);
	} else {
		rc = -EINVAL;
	}
	__unlock(me);

	return rc;
}
EXPORT_SYMBOL(max77696_charger_get_temp_thres);

int max77696_charger_set_wdt_period (int period_ms)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_wdt_period_set(me, period_ms);
	__unlock(me);

	return 0;
}
EXPORT_SYMBOL(max77696_charger_set_wdt_period);

int max77696_charger_get_wdt_period (int *period_ms)
{
	struct max77696_chip *chip = max77696;
	struct max77696_charger *me = chip->chg_ptr;
	int rc;

	__lock(me);
	rc = max77696_charger_wdt_period_get(me, period_ms);
	__unlock(me);

	return 0;
}
EXPORT_SYMBOL(max77696_charger_get_wdt_period);

bool max77696_charger_get_connected (void)
{
    struct max77696_chip *chip = max77696;
    struct max77696_charger *me = chip -> chg_ptr;

    return is_charger_a_connected(me);
}
EXPORT_SYMBOL(max77696_charger_get_connected);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

