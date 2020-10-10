/*
 * Copyright (C) 2012 Maxim Integrated Product
 * Jayden Cha <jayden.cha@maxim-ic.com>
 * Copyright (c) 2012-2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <linux/mfd/max77696.h>
#include <linux/mfd/max77696-events.h>
#include <max77696_registers.h>
#include <mach/boardid.h>

#ifdef CONFIG_FALCON
#include <llog.h>
extern int in_falcon(void);
extern int pb_oneshot;
extern void pb_oneshot_unblock_button_events (void);
#endif

#define DRIVER_DESC    "MAX77696 USB Interface Circuit Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_UIC_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#define __psy_work_to_max77696_uic(psy_work_ptr) \
	container_of(psy_work_ptr, struct max77696_uic, psy_work.work)

#define __get_i2c(chip)               (&((chip)->uic_i2c))
#define __lock(me)                    mutex_lock(&((me)->lock))
#define __unlock(me)                  mutex_unlock(&((me)->lock))

/* UIC Register Read/Write */
#define max77696_uic_reg_read(me, reg, val_ptr) \
	max77696_read((me)->i2c, UIC_REG(reg), val_ptr)
#define max77696_uic_reg_write(me, reg, val) \
	max77696_write((me)->i2c, UIC_REG(reg), val)
#define max77696_uic_reg_bulk_read(me, reg, dst, len) \
	max77696_bulk_read((me)->i2c, UIC_REG(reg), dst, len)
#define max77696_uic_reg_bulk_write(me, reg, src, len) \
	max77696_bulk_write((me)->i2c, UIC_REG(reg), src, len)
#define max77696_uic_reg_read_masked(me, reg, mask, val_ptr) \
	max77696_read_masked((me)->i2c, UIC_REG(reg), mask, val_ptr)
#define max77696_uic_reg_write_masked(me, reg, mask, val) \
	max77696_write_masked((me)->i2c, UIC_REG(reg), mask, val)

/* UIC Register Single Bit Ops */
#define max77696_uic_reg_get_bit(me, reg, bit, val_ptr) \
	({\
	 int __rc = max77696_uic_reg_read_masked(me, reg,\
		 UIC_REG_BITMASK(reg, bit), val_ptr);\
	 *(val_ptr) = UIC_REG_BITGET(reg, bit, *(val_ptr));\
	 __rc;\
	 })
#define max77696_uic_reg_set_bit(me, reg, bit, val) \
	({\
	 max77696_uic_reg_write_masked(me, reg,\
		 UIC_REG_BITMASK(reg, bit), UIC_REG_BITSET(reg, bit, val));\
	 })

#define UIC_PSY_WORK_DELAY                  0
#define UIC_ADC_DEBOUNCE_DELAY              200
#define UIC_ADC_DEBOUNCE_DELAY_RESUME       500
#define UIC_BOOTUP_WORK_DELAY               3000		/* in milli seconds */
#define CHARGER_DETECTION_DELAY             1000		/* charger detection delay after forced charger deteciton */

u8 wario_uic_chgtyp = 0;
EXPORT_SYMBOL(wario_uic_chgtyp);
extern int max77696_led_set_manual_mode(unsigned int led_id, bool manual_mode);
extern int max77696_led_ctrl(unsigned int led_id, int led_state);
extern int max77696_charger_set_otg(int enable);
extern void asession_enable(bool detect);
void otg_state_changed_stop_recovery(void);

#ifdef CONFIG_POWER_SODA
extern bool soda_charger_docked(void);
extern void pmic_soda_connects_otg_in_out_handler(u8 in);
extern void soda_otg_vbus_output(int en, int ping);
#endif

void pmic_soda_connects_notify_otg_in(void);
void pmic_soda_connects_notify_otg_out(void);

struct max77696_uic {
	struct max77696_chip *chip;
	struct max77696_i2c  *i2c;
	struct device        *dev;
	struct kobject       *kobj;
	struct power_supply  psy;

	void                  (*uic_notify) (const struct max77696_uic_notify*);

	struct delayed_work   psy_work;
	struct delayed_work   bootup_work;
	struct delayed_work   adc_work;
	struct delayed_work   charger_detect;
	unsigned int          irq;
	u8                    irq_unmask[2];
	u8                    interrupted[2];
	u8                    vb_volt;
	u8                    chg_type;
	u8                    adc_code;
	u8                    i_set;
	bool                  in_otg;

	struct mutex          lock;
	int                   adc_work_delay;
};
static struct max77696_uic* g_max77696_uic;
static pmic_event_callback_t chgtyp_detect;
static int uic_boot_flag = 0;

extern int max77696_charger_set_icl(void);

static const char* max77696_uic_chgtype_string (u8 chg_type)
{
	switch (chg_type) {
		case MAX77696_UIC_CHGTYPE_USB:
			return "USB Cable";
		case MAX77696_UIC_CHGTYPE_CDP:
			return "Charging Downstream Port";
		case MAX77696_UIC_CHGTYPE_DEDICATED_1P5A:
			return "Dedicated Charger";
		case MAX77696_UIC_CHGTYPE_APPLE_0P5AC:
		case MAX77696_UIC_CHGTYPE_APPLE_1P0AC:
		case MAX77696_UIC_CHGTYPE_APPLE_2P0AC:
			return "Apple Charger";
		case MAX77696_UIC_CHGTYPE_OTH_0:
		case MAX77696_UIC_CHGTYPE_OTH_1:
			return "Other Charger";
		case MAX77696_UIC_CHGTYPE_SELFENUM_0P5AC:
			return "Self Enumerated";
	}

	return "(unknown)";
}

static const char* max77696_uic_iset_string (u8 i_set)
{
	switch (i_set) {
		case MAX77696_UIC_ISET_0mA: return "0.0";
		case MAX77696_UIC_ISET_0P1A: return "0.1";
		case MAX77696_UIC_ISET_0P4A: return "0.4";
		case MAX77696_UIC_ISET_0P5A: return "0.5";
		case MAX77696_UIC_ISET_1A: return "1.0";
		case MAX77696_UIC_ISET_1P5A: return "1.5";
		case MAX77696_UIC_ISET_2A: return "2.0";
	}

	return "(unknown)";
}

static int max77696_uic_iset_val_ua(u8 i_set)
{
	switch (i_set) {
		case MAX77696_UIC_ISET_0mA: return 0;
		case MAX77696_UIC_ISET_0P1A: return  100000;
		case MAX77696_UIC_ISET_0P4A: return  400000;
		case MAX77696_UIC_ISET_0P5A: return  500000;
		case MAX77696_UIC_ISET_1A:   return 1000000;
		case MAX77696_UIC_ISET_1P5A: return 1500000;
		case MAX77696_UIC_ISET_2A:   return 2000000;
	}

	return 0;
}


/* UIC Event handling functions */
int max77696_uic_mask1(void *obj, u16 event, bool mask_f)
{
	u8 irq, bit_pos, buf=0x0;
	int rc;
	struct max77696_uic *me = (struct max77696_uic *)obj;

	DECODE_EVENT(event, irq, bit_pos);
	__lock(me);

	rc = max77696_uic_reg_read(me, INTMASK1, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "UIC_INT_MASK1 read error [%d]\n", rc);
		goto out;
	}

	if(mask_f) {
		rc = max77696_uic_reg_write(me, INTMASK1, (buf & ~bit_pos));
		if (unlikely(rc)) {
			dev_err(me->dev, "UIC_INT_MASK1 write error [%d]\n", rc);
			goto out;
		}
	}
	else {
		rc = max77696_uic_reg_write(me, INTMASK1, (buf | bit_pos));
		if (unlikely(rc)) {
			dev_err(me->dev, "UIC_INT_MASK1 write error [%d]\n", rc);
			goto out;
		}
	}
out:
	__unlock(me);
	return rc;
}

static struct max77696_event_handler max77696_chgtyp_handle = {
	.mask_irq = max77696_uic_mask1,
	.event_id = EVENT_UIC_CHGTYP,
};

/* max77696_update_chgtyp:
 * Update charger type & set ICL based on the output from UIC block
 */
static void max77696_update_chgtyp(struct max77696_uic *me)
{
	u8 chg_type = 0, chg_ctrl = 0, status[2];
	int rc = 0;

	chg_type = me->chg_type;

	max77696_uic_reg_bulk_read(me, STATUS1, status, 2);

	me->vb_volt  = UIC_REG_BITGET(STATUS1, VBVOLT, status[0]);
	me->chg_type = UIC_REG_BITGET(STATUS1, CHGTYP, status[0]);
	me->adc_code = UIC_REG_BITGET(STATUS2, ADC,    status[1]);

	//usb charger type, not able to detect soda
	if (me->chg_type) {
		if ((me->adc_code != UIC_ADC_STATUS_INVALID_10) && (me->adc_code != UIC_ADC_STATUS_INVALID_13) &&
				(me->adc_code != UIC_ADC_STATUS_INVALID_16)) {

			/* Enable AUTH500 to avoid charge current issue while vbus droops */
			max77696_uic_reg_read(me, CHGCTRL, &chg_ctrl);
			max77696_uic_reg_write(me, CHGCTRL, (chg_ctrl | UIC_CHGCTRL_AUTH500_M));
			pr_debug("Enable chgctrl AUTH500 \n");

			/* update CHGINA_ICL */
			rc = max77696_charger_set_icl();
			if (!rc)
				me->i_set = MAX77696_UIC_ISET_0P5A;

		}
	} else if (unlikely(!me->vb_volt)) {
		/* This time is when the cable is removed. */
		me->chg_type = 0;
		me->adc_code = 0;
		me->i_set    = 0;
	}

	wario_uic_chgtyp = me->chg_type;

	pr_debug("VBVOLT %X CHGTYP %X ADC %X ISET %X", me->vb_volt, me->chg_type, me->adc_code, me->i_set);
	pr_debug("Detected USB Charger  %s\n", max77696_uic_chgtype_string(me->chg_type));
	pr_debug("Input Current Limit   %sA\n", max77696_uic_iset_string(me->i_set));

	if (likely(me->chg_type != chg_type)) {
		if (likely(me->uic_notify)) {
			struct max77696_uic_notify noti = {
				.vb_volt  = me->vb_volt,
				.chg_type = me->chg_type,
				.adc_code = me->adc_code,
				.i_set    = me->i_set,
			};
			me->uic_notify(&noti);
		}
	}
	power_supply_changed(&(me->psy));
}

static void max77696_uic_bootup_handler (struct work_struct *work)
{
	struct max77696_uic *me = container_of(work, struct max77696_uic, bootup_work.work);
	struct max77696_chip  *chip = me->chip;

	__lock(me);

	if (!IS_SUBDEVICE_LOADED(charger, chip)) {
		schedule_delayed_work(&(me->bootup_work), msecs_to_jiffies(UIC_BOOTUP_WORK_DELAY));
	} else {
		max77696_update_chgtyp(me);
		uic_boot_flag = 0;
	}

	__unlock(me);
	return;
}

static void chgtyp_detect_cb(void *obj, void *param)
{
	struct max77696_uic *me = (struct max77696_uic *)param;
	__lock(me);

	if (!uic_boot_flag) {
		max77696_update_chgtyp(me);
	}
	__unlock(me);
	return;
}

static void max77696_uic_psy_work (struct work_struct *work)
{
	/* Invoke all cb's for uic chgtyp detect event */
	pmic_event_callback(EVENT_UIC_CHGTYP);
	return;
}

static void max77696_uic_charger_detect_work(struct work_struct *work)
{
	struct max77696_uic *me = container_of(work, struct max77696_uic, charger_detect.work);
	u8 status[2];

	__lock(me);
	max77696_uic_reg_bulk_read(me, STATUS1, status, 2);
	me->chg_type = UIC_REG_BITGET(STATUS1, CHGTYP, status[0]);
	wario_uic_chgtyp = me->chg_type;
	__unlock(me);
	return;
}

static void max77696_uic_adc_work(struct work_struct *work)
{
	struct max77696_uic *me = container_of(work, struct max77696_uic, adc_work.work);
	int ret;
	u8 adcval;
	int retry = 5;
	__lock(me);
	/*vbus can be noisy and affect i2c, retry to make sure this packet is going through*/
	do {
		ret = max77696_uic_reg_get_bit(me, STATUS2, ADC, &adcval);
		if (ret) {
			dev_err(me->dev, "uic status (adc val) failed [%d] usb noise ?\n", ret);
			msleep(100);
		}
	}while(ret && retry-- > 0);

	__unlock(me);

	if (unlikely(ret)) {
		dev_err(me->dev, "uic status (adc val) failed [%d]\n", ret);
		
	} else{
#ifdef CONFIG_POWER_SODA		
		pmic_soda_connects_otg_in_out_handler(!adcval);
#else
	/*
	 * use adc_work_delay as a flag to identify if this is the first interrupt
	 * after resume or not 
	 *	UIC_ADC_DEBOUNCE_DELAY or
	 *	UIC_ADC_DEBOUNCE_DElAY_RESUME 
	 */
	if(me->adc_work_delay == UIC_ADC_DEBOUNCE_DELAY) {
		if(adcval == 0 && !me->in_otg)
			pmic_soda_connects_notify_otg_in();
		else if (adcval > 0 && me->in_otg)
			pmic_soda_connects_notify_otg_out();
	}else {
		/*
		 * in_otg state is not reliable after suspend resume.
		 * the connection state might have changed during suspend
		 */
		if(adcval == 0 )
			pmic_soda_connects_notify_otg_in();
		else if (adcval > 0 )
			pmic_soda_connects_notify_otg_out();
	}
#endif
		printk(KERN_DEBUG "%s adcval %d in_otg %d", __func__, adcval, me->in_otg);
	}

}

void pmic_soda_connects_notify_otg_in(void){
	printk(KERN_ERR "%s:%d OTG!\n",__FUNCTION__,__LINE__);
	g_max77696_uic->in_otg = 1;
#ifdef CONFIG_POWER_SODA
	if(false == soda_charger_docked())
		return;
#endif

	if (IS_SUBDEVICE_LOADED(charger, g_max77696_uic->chip)) {
#if defined(CONFIG_MX6SL_WARIO_BASE)
		/* Turn OFF amber led - manual mode */
		max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_OFF);
		/* Turn OFF green led - manual mode */
		max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
#endif

#ifdef CONFIG_POWER_SODA		
		//bypassing powerd and set this pin behind the scene..
		soda_otg_vbus_output(1, 0);
#else
		max77696_charger_set_otg(1);
#endif

		asession_enable(1);
		max77696_charger_set_icl();
	}
	g_max77696_uic->adc_work_delay = UIC_ADC_DEBOUNCE_DELAY;
} 
#ifdef CONFIG_POWER_SODA
EXPORT_SYMBOL(pmic_soda_connects_notify_otg_in);
#endif

void pmic_soda_connects_notify_otg_out(void){
	printk(KERN_ERR"%s:%d\n",__FUNCTION__,__LINE__);
	g_max77696_uic->in_otg = 0;
	if (IS_SUBDEVICE_LOADED(charger, g_max77696_uic->chip)) {
#if defined(CONFIG_MX6SL_WARIO_BASE)
		max77696_led_set_manual_mode(MAX77696_LED_AMBER, false);
		max77696_led_set_manual_mode(MAX77696_LED_GREEN, false);
#endif
		max77696_charger_set_otg(0);
		asession_enable(0);
#ifdef CONFIG_POWER_SODA
		if(false == soda_charger_docked())
			return;

		//toggling boost after OTG is enumerated is a no-opt for hardware. Hardware would still boost anyway. 
		// so the concept of "locking boost", is actually hardware
		// powerd or whoever can toggle boost in software, but the hardware would not listen.

		// this only place this function is sending user space event to notify powerd, 
		// boost control is handed over now. Powerd needs to set boost according to the battery state.

		soda_otg_vbus_output(0, 1);		
#endif
	}		
	g_max77696_uic->adc_work_delay = UIC_ADC_DEBOUNCE_DELAY;
}
#ifdef CONFIG_POWER_SODA
EXPORT_SYMBOL(pmic_soda_connects_notify_otg_out);
#endif

int max77696_uic_force_charger_detection(void)
{
	struct max77696_uic *me = g_max77696_uic;
	int ret = 0;

	if (!me)
		return -ENODEV;
	__lock(me);
	max77696_uic_reg_set_bit(me, CDETCTRL, CHGTYPMAN, 1);
	__unlock(me);
	schedule_delayed_work(&(me->charger_detect), msecs_to_jiffies(CHARGER_DETECTION_DELAY));
	return ret;
}
EXPORT_SYMBOL(max77696_uic_force_charger_detection);

int max77696_uic_is_otg_connected_irq(void){
	return (g_max77696_uic && g_max77696_uic->in_otg);
}
EXPORT_SYMBOL(max77696_uic_is_otg_connected_irq);

int max77696_uic_is_otg_connected(void)
{
	struct max77696_uic *me = g_max77696_uic;
	int ret = 0;
	u8 adcval = 0xff;

	__lock(me);
	
	ret = max77696_uic_reg_get_bit(me, STATUS2, ADC, &adcval);
	if (unlikely(ret)) {
		dev_err(me->dev, "uic status (adc val) failed [%d]\n", ret);
		ret = 0;
	}else {
		ret = (adcval == 0);
	}

	__unlock(me);
	return ret;
}
EXPORT_SYMBOL(max77696_uic_is_otg_connected);

static irqreturn_t max77696_uic_isr (int irq, void *data)
{
	struct max77696_uic *me = data;
	int ret;
	/* read INT register to clear bits ASAP */

	max77696_uic_reg_bulk_read(me, INT1, me->interrupted, 2);

	pr_debug("INT1 %02X INT2 %02X\n", me->interrupted[0], me->interrupted[1]);

	if (me->interrupted[0] & UIC_INT1_CHGTYP_M) {
		schedule_delayed_work(&(me->psy_work), msecs_to_jiffies(UIC_PSY_WORK_DELAY));
	} 

	if(LAB126_BOARDS_ENABLE_USB_OTG) {
		if (me ->interrupted[1] & UIC_INT2_ADC_M) {
			ret = cancel_delayed_work(&(me->adc_work));
			printk(KERN_DEBUG "%s: cancel: %d", __func__, ret);
			schedule_delayed_work(&(me->adc_work), msecs_to_jiffies(me->adc_work_delay));
		}
	}
	return IRQ_HANDLED;
}

bool max77696_uic_is_usbhost (void)
{   
	struct max77696_uic *me = g_max77696_uic;
	u8 chg_type = 0;
	int rc = 0;
	bool ret = false;

	__lock(me);

	rc = max77696_uic_reg_get_bit(me, STATUS1, CHGTYP, &chg_type);
	if (unlikely(rc)) {
		dev_err(me->dev, "uic status (chgtyp) failed [%d]\n", rc);
		goto out;
	}

	if (chg_type == MAX77696_UIC_CHGTYPE_USB || chg_type == MAX77696_UIC_CHGTYPE_SELFENUM_0P5AC ||
		chg_type == MAX77696_UIC_CHGTYPE_CDP) {
		pr_debug("connected to usb host chgtyp=%02X \n", chg_type);
		ret = true;
	}

out:
	__unlock(me);
    return ret;
}
EXPORT_SYMBOL(max77696_uic_is_usbhost);


/* The following lists will be propulated with the UIC device parameters
 * and read/write function pointers
 */

#define MAX77696_UIC_ATTR(name, mode, reg, bits) \
	static ssize_t max77696_uic_##name##_show (struct device *dev,\
			struct device_attribute *devattr, char *buf)\
{\
	struct platform_device *pdev = to_platform_device(dev);\
	struct max77696_uic *me = platform_get_drvdata(pdev);\
	u8 val;\
	int rc;\
	__lock(me);\
	rc = max77696_uic_reg_get_bit(me, reg, bits, &val);\
	if (unlikely(rc)) {\
		dev_err(dev, ""#reg" read error [%d]\n", rc);\
		goto out;\
	}\
	rc = (int)snprintf(buf, PAGE_SIZE, "%u\n", val);\
	out:\
	__unlock(me);\
	return (ssize_t)rc;\
}\
static ssize_t max77696_uic_##name##_store (struct device *dev,\
		struct device_attribute *devattr, const char *buf, size_t count)\
{\
	struct platform_device *pdev = to_platform_device(dev);\
	struct max77696_uic *me = platform_get_drvdata(pdev);\
	u8 val;\
	int rc;\
	__lock(me);\
	val = (u8)simple_strtoul(buf, NULL, 10);\
	rc = max77696_uic_reg_set_bit(me, reg, bits, val);\
	if (unlikely(rc)) {\
		dev_err(dev, ""#reg" write error [%d]\n", rc);\
		goto out;\
	}\
out:\
	__unlock(me);\
	return (ssize_t)count;\
}\
static DEVICE_ATTR(name, mode, max77696_uic_##name##_show,\
		max77696_uic_##name##_store)

MAX77696_UIC_ATTR(chipid,    S_IRUGO,         DEVICEID, CHIPID);
MAX77696_UIC_ATTR(chiprev,   S_IRUGO,         DEVICEID, CHIPREV);

MAX77696_UIC_ATTR(dcdtmr,    S_IRUGO,         STATUS1,  DCDTMR);
MAX77696_UIC_ATTR(chgdetact, S_IRUGO,         STATUS1,  CHGDETACT);
MAX77696_UIC_ATTR(vbvolt,    S_IRUGO,         STATUS1,  VBVOLT);
MAX77696_UIC_ATTR(chgtyp,    S_IRUGO,         STATUS1,  CHGTYP);

MAX77696_UIC_ATTR(adcerror,  S_IRUGO,         STATUS2,  ADCERROR);
MAX77696_UIC_ATTR(enustat,   S_IRUGO,         STATUS2,  ENUSTAT);
MAX77696_UIC_ATTR(adc,       S_IRUGO,         STATUS2,  ADC);

MAX77696_UIC_ATTR(usbswc,    S_IWUSR|S_IRUGO, SYSCTRL1, USBSWC);
MAX77696_UIC_ATTR(low_pow,   S_IWUSR|S_IRUGO, SYSCTRL1, LOW_POW);

MAX77696_UIC_ATTR(adc_deb,   S_IWUSR|S_IRUGO, SYSCTRL2, ADC_DEB);
MAX77696_UIC_ATTR(dcdcpl,    S_IWUSR|S_IRUGO, SYSCTRL2, DCDCPL);
MAX77696_UIC_ATTR(idautoswc, S_IWUSR|S_IRUGO, SYSCTRL2, IDAUTOSWC);
MAX77696_UIC_ATTR(adc_en,    S_IWUSR|S_IRUGO, SYSCTRL2, ADC_EN);

MAX77696_UIC_ATTR(sfen,      S_IWUSR|S_IRUGO, CDETCTRL, SFEN);
MAX77696_UIC_ATTR(cdpdet,    S_IWUSR|S_IRUGO, CDETCTRL, CDPDET);
MAX77696_UIC_ATTR(dchktm,    S_IWUSR|S_IRUGO, CDETCTRL, DCHKTM);
MAX77696_UIC_ATTR(dcd2sct,   S_IWUSR|S_IRUGO, CDETCTRL, DCD2SCT);
MAX77696_UIC_ATTR(dcden,     S_IWUSR|S_IRUGO, CDETCTRL, DCDEN);
MAX77696_UIC_ATTR(chgtypman, S_IWUSR|S_IRUGO, CDETCTRL, CHGTYPMAN);
MAX77696_UIC_ATTR(chgdeten,  S_IWUSR|S_IRUGO, CDETCTRL, CHGDETEN);

static u8 max77696_uic_regaddr = 0;

static ssize_t max77696_uic_regoff_show (struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "0x%x\n", max77696_uic_regaddr);
	return (ssize_t)rc;
}
static ssize_t max77696_uic_regoff_store (struct device *dev,
		struct device_attribute *devattr, const char *buf, size_t count)
{
	unsigned int val = 0;
	if (sscanf(buf, "%x", &val) <= 0) {
		printk(KERN_ERR "Could not store MAX77696_UIC register offset\n");
		return -EINVAL;
	}
	max77696_uic_regaddr = (u8)val;
	return (ssize_t)count;
}
static DEVICE_ATTR(max77696_uic_regoff, S_IWUSR|S_IRUGO, max77696_uic_regoff_show, max77696_uic_regoff_store);

static ssize_t max77696_uic_regval_show (struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct max77696_uic *me = g_max77696_uic;
	u8 val = 0;
	int rc;
	max77696_read((me)->i2c, max77696_uic_regaddr, &val);
	rc = (int)sprintf(buf, "MAX77696_UIC REG_ADDR=0x%x : REG_VAL=0x%x\n", max77696_uic_regaddr,val);
	return (ssize_t)rc;
}

static ssize_t max77696_uic_regval_store (struct device *dev,
		struct device_attribute *devattr, const char *buf, size_t count)
{
	struct max77696_uic *me = g_max77696_uic;
	unsigned int val = 0;
	if (sscanf(buf, "%x", &val) <= 0) {
		printk(KERN_ERR "Could not store MAX77696_UIC register value\n");
		return -EINVAL;
	}
	max77696_write((me)->i2c, max77696_uic_regaddr, (u8)val);
	return (ssize_t)count;
}
static DEVICE_ATTR(max77696_uic_regval, S_IWUSR|S_IRUGO, max77696_uic_regval_show, max77696_uic_regval_store);

static struct attribute *max77696_uic_reg_attr[] = {
	&dev_attr_max77696_uic_regoff.attr,
	&dev_attr_max77696_uic_regval.attr,
	NULL
};

static const struct attribute_group max77696_uic_reg_attr_group = {
	.attrs = max77696_uic_reg_attr,
};

static struct attribute *max77696_uic_attr[] = {
	&dev_attr_chipid.attr,
	&dev_attr_chiprev.attr,
	&dev_attr_dcdtmr.attr,
	&dev_attr_chgdetact.attr,
	&dev_attr_vbvolt.attr,
	&dev_attr_chgtyp.attr,
	&dev_attr_adcerror.attr,
	&dev_attr_enustat.attr,
	&dev_attr_adc.attr,
	&dev_attr_usbswc.attr,
	&dev_attr_low_pow.attr,
	&dev_attr_adc_deb.attr,
	&dev_attr_dcdcpl.attr,
	&dev_attr_idautoswc.attr,
	&dev_attr_adc_en.attr,
	&dev_attr_sfen.attr,
	&dev_attr_cdpdet.attr,
	&dev_attr_dchktm.attr,
	&dev_attr_dcd2sct.attr,
	&dev_attr_dcden.attr,
	&dev_attr_chgtypman.attr,
	&dev_attr_chgdeten.attr,
	NULL
};

static const struct attribute_group max77696_uic_attr_group = {
	.attrs = max77696_uic_attr,
};

static enum power_supply_property max77696_uic_psy_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int max77696_uic_get_property (struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct max77696_uic *me = container_of(psy, struct max77696_uic, psy);
	int rc = 0;

	__lock(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = me->vb_volt != 0;
			break;
		case POWER_SUPPLY_PROP_CURRENT_MAX:
			val->intval = max77696_uic_iset_val_ua(me->i_set);
			break;
		default:
			rc = -EINVAL;
			break;
	}

	__unlock(me);
	return rc;
}

static __devinit int max77696_uic_probe (struct platform_device *pdev)
{
	struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77696_uic_platform_data *pdata = pdev->dev.platform_data;
	struct max77696_uic *me;
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

	me->uic_notify = pdata->uic_notify;

	me->irq  = chip->irq_base + MAX77696_ROOTINT_UIC;

	g_max77696_uic = me;
	uic_boot_flag = 1;

	me->psy.name = DRIVER_NAME;
	me->psy.type = POWER_SUPPLY_TYPE_USB;
	me->psy.properties = max77696_uic_psy_props;
	me->psy.num_properties = ARRAY_SIZE(max77696_uic_psy_props);
	me->psy.get_property = max77696_uic_get_property;

	if ((rc = power_supply_register(me->dev, &(me->psy)))) {
		dev_err(me->dev, "failed to register power supply device (%d)\n", rc);
		goto out_err_reg_psy;
	}

	/* Note: Disable UIC manctrl set during early boot
	 * ICL controlled by CHGA based on the charger type
	 * which is non-sticky */
	max77696_uic_reg_write(me, MANCTRL, 0x0);

	INIT_DELAYED_WORK(&(me->psy_work), max77696_uic_psy_work);
	INIT_DELAYED_WORK(&(me->adc_work), max77696_uic_adc_work);
	INIT_DELAYED_WORK(&(me->bootup_work), max77696_uic_bootup_handler);
	INIT_DELAYED_WORK(&(me->charger_detect), max77696_uic_charger_detect_work);

	rc = sysfs_create_group(me->kobj, &max77696_uic_attr_group);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to create attribute group [%d]\n", rc);
		goto out_err_sysfs;
	}

	/* Disable all UIC interrupts */
	max77696_uic_reg_write(me, INTMASK1, 0x0);
	max77696_uic_reg_write(me, INTMASK2, 0x0);

	rc = request_threaded_irq(me->irq, NULL,
			max77696_uic_isr, IRQF_ONESHOT, DRIVER_NAME, me);
	if (unlikely(rc < 0)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n", me->irq, rc);
		goto out_err_req_irq;
	}

	/* register and subscribe to events */
	rc = max77696_eventhandler_register(&max77696_chgtyp_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_chgtyp_handle.event_id, rc);
		goto out_err_reg_event;
	}

	chgtyp_detect.param = me;
	chgtyp_detect.func = chgtyp_detect_cb;
	rc = pmic_event_subscribe(max77696_chgtyp_handle.event_id, &chgtyp_detect);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed subscribe to event[%d] handle with err [%d]\n", \
				max77696_chgtyp_handle.event_id, rc);
		goto out_err_sub_event;
	}

	/* Enable UIC interrupt */
	max77696_uic_reg_write_masked(me, SYSCTRL1, UIC_SYSCTRL1_INT_EN_M,
			UIC_SYSCTRL1_INT_EN_M);

	schedule_delayed_work(&(me->bootup_work), msecs_to_jiffies(UIC_BOOTUP_WORK_DELAY));	
	pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
	SUBDEVICE_SET_LOADED(uic, chip);

	if(LAB126_BOARDS_ENABLE_USB_OTG) {
		max77696_uic_reg_set_bit(me, INTMASK2, ADC, 1);
		max77696_uic_reg_set_bit(me, SYSCTRL2, ADC_EN, 1);
		max77696_uic_reg_set_bit(me, SYSCTRL2, ADC_DEB, 0x3);
	}
	return 0;

out_err_sub_event:
	max77696_eventhandler_unregister(&max77696_chgtyp_handle);
out_err_reg_event:
	free_irq(me->irq, me);
out_err_req_irq:
	sysfs_remove_group(me->kobj, &max77696_uic_attr_group);
out_err_sysfs:
	power_supply_unregister(&(me->psy));
out_err_reg_psy:
	g_max77696_uic = NULL;
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);

	return rc;
}

static __devexit int max77696_uic_remove (struct platform_device *pdev)
{
	struct max77696_uic *me = platform_get_drvdata(pdev);

	/* unsubscribe and unregister */
	pmic_event_unsubscribe(max77696_chgtyp_handle.event_id, &chgtyp_detect);
	max77696_eventhandler_unregister(&max77696_chgtyp_handle);

	free_irq(me->irq, me);
	cancel_delayed_work(&(me->psy_work));
	cancel_delayed_work_sync(&(me->adc_work));
	cancel_delayed_work(&(me->bootup_work));
	cancel_delayed_work(&(me->charger_detect));
	sysfs_remove_group(me->kobj, &max77696_uic_attr_group);

	power_supply_unregister(&(me->psy));

	g_max77696_uic = NULL;
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);

	return 0;
}

static int max77696_uic_suspend (struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_uic *me = platform_get_drvdata(pdev);
	if(LAB126_BOARDS_ENABLE_USB_OTG) {
		me->adc_work_delay = UIC_ADC_DEBOUNCE_DELAY_RESUME;
		__lock(me);
		max77696_uic_reg_set_bit(me, INTMASK2, ADC, 0);
		max77696_uic_reg_set_bit(me, SYSCTRL2, ADC_EN, 0);
		me->in_otg = 0; //disable ID pin detect
		__unlock(me);
		cancel_delayed_work(&(me->adc_work));
	}
	return 0;
}

static int max77696_uic_resume (struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_uic *me = platform_get_drvdata(pdev);
#ifdef CONFIG_FALCON
	u8 uicwk_status = 0;
	if (in_falcon()) {
		max77696_uic_reg_get_bit(me, INTSTS, UICWK, &uicwk_status);
		if (uicwk_status) {
			if(pb_oneshot == HIBER_SUSP) {
				pb_oneshot = HIBER_CHG_IRQ;
				pb_oneshot_unblock_button_events();
			}
			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY,
			DEVICE_METRIC_TYPE_COUNTER, "kernel", DRIVER_NAME,
			"hibernate_wakeup_UIC", 1, "");
		}
	}
#endif
	if(LAB126_BOARDS_ENABLE_USB_OTG) {
		__lock(me);
		max77696_uic_reg_set_bit(me, INTMASK2, ADC, 1);
		max77696_uic_reg_set_bit(me, SYSCTRL2, ADC_EN, 1);
		__unlock(me);
#if defined(CONFIG_POWER_SODA)
		if(max77696_uic_is_otg_connected() && soda_charger_docked())
			max77696_charger_set_icl();
#endif
	}
	return 0;
}

static SIMPLE_DEV_PM_OPS(max77696_uic_pm, max77696_uic_suspend, max77696_uic_resume);

static struct platform_driver max77696_uic_driver = {
	.driver.name  = DRIVER_NAME,
	.driver.owner = THIS_MODULE,
	.driver.pm    = &max77696_uic_pm,
	.probe        = max77696_uic_probe,
	.remove       = __devexit_p(max77696_uic_remove),
};

static __init int max77696_uic_driver_init (void)
{
	return platform_driver_register(&max77696_uic_driver);
}

static __exit void max77696_uic_driver_exit (void)
{
	platform_driver_unregister(&max77696_uic_driver);
}

module_init(max77696_uic_driver_init);
module_exit(max77696_uic_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

