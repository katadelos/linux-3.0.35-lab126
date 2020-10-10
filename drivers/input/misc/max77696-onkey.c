/*
 * Copyright 2012 Maxim Integrated Product
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
#include <linux/semaphore.h>

#include <linux/input.h>
#include <linux/mfd/max77696.h>
#include <linux/mfd/max77696-events.h>

#define DRIVER_DESC    "MAX77696 ONKEY Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_ONKEY_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#define KEY_DOWN_VALUE 1
#define KEY_UP_VALUE   0

enum {
	ONKEY_KEY_EN0 = 0,
	ONKEY_KEY_1SEC,
	ONKEY_KEY_MRWRN,

	ONKEY_NR_KEYS,
};

struct max77696_onkey {
	struct max77696_chip *chip;
	struct device        *dev;
	struct kobject       *kobj;

	/* filled from pdata */
	unsigned int          keycode[ONKEY_NR_KEYS];

	struct input_dev     *input;
	unsigned int          dben0_irq;
	unsigned int          irq[ONKEY_NR_KEYS]; /* key_up interrupts */
	bool                  pressed[ONKEY_NR_KEYS];
	struct delayed_work   down_work;
	struct delayed_work   up_work;
	struct delayed_work   onkey_longpress_work;
	struct delayed_work   onkey_led_work;
};

#define __work_to_max77696_onkey(work_ptr) \
	container_of(work_ptr, struct max77696_onkey, down_work.work)

#define ONKEY_SKIP_INTERVAL 		500	/* in milli-seconds */
#define ONKEY_LED_BLINK_INTERVAL	2000	/* in milli-seconds */
#define ONKEY_LONGPRESS_INTERVAL	4000	/* in milli-seconds */
static pmic_event_callback_t onkey_down;
static pmic_event_callback_t onkey_up;
static struct mutex onkey_lock;
static struct delayed_work onkey_skip_work;
static bool wario_onkey_enabled = 0;
bool wario_onkey_press_skip = 0;
EXPORT_SYMBOL(wario_onkey_press_skip);

#if defined(CONFIG_MX6SL_WARIO_WOODY)
static struct max77696_event_handler onkey_press_sw_handle = {
	.mask_irq = NULL,
	.event_id = EVENT_SW_ONKEY_PRESS,
};
#endif

#define PB_SKIP_ONESHOT 1
/*
 * used to postpone handling of extra PB events generated by hall oneshot circuit
 * until the true wakeup source is identified.
 * When entering hibernate, button events are blocked.  They are unblocked
 * when a call to pb_oneshot_unblock_button_events is made by a driver that
 * claims responsibility for the wakeup.
 * pb_oneshot_unblock_button_events is called in the complete pm callback, so that if no other
 * drivers claim responsibility for the power button event, we can assume it is real.
 */
static DEFINE_SEMAPHORE(onkey_cb_sem);

/*
 * allows other drivers to indicate that they are responsible for the wakeup which
 * triggered the pb oneshot to generate button events, which should be ignored.
 */
int pb_oneshot;
EXPORT_SYMBOL(pb_oneshot);

// called by external drivers to indicate that events can continue after
// true wakeup source has been identified.
void pb_oneshot_unblock_button_events (void) {
#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	up(&onkey_cb_sem);
	return;
#endif
}
EXPORT_SYMBOL(pb_oneshot_unblock_button_events);

// Called before the pb oneshot circuit is activated to prevent extra pb events
static void pb_oneshot_block_button_events (void) {
#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	// init sema to 0 (in the unlikely event that more than one
	// device released this during the last resume cycle)
	sema_init(&onkey_cb_sem, 0);
	pb_oneshot = HIBER_SUSP;
#endif
	return;
}

#ifdef CONFIG_FALCON
extern int in_falcon(void);

/* JSEVENONE-2014: When we see power button events coming out of hibernate and the cover
 * is closed, emit a metric that can be subtracted from analysis of UNKNOWN wakeups
 * If the device was woken from hibernate due to a power button press
 * but power button interrupts were masked out because cover is closed, userspace
 * would be woken for an UNKNOWN reason. Since we can't pass these events to userspace
 * when the cover is closed, use a dummy event handler to keep power button interrupts
 * unmasked and eat the extra interrupts generated. Remove these dummy event handlers in
 * normal suspend, so that spurious power button presses don't unnecessarily wake the
 * device.
 */
#include <llog.h>
extern int gpio_hallsensor_detect(void);
static bool log_hibernate_pb_metrics = 0;
static struct delayed_work dummy_onkey_metrics_cancel_work;
static void dummy_onkey_metrics_cancel_work_cb (struct work_struct *work)
{
	log_hibernate_pb_metrics = 0;
	return;
}
static pmic_event_callback_t onkey_dummy;
static pmic_event_callback_t onkey_dummy2;
static void onkey_dummy_cb2 (void *obj, void *param)
{
	// do nothing for EN0RISE
	return;
}
static void onkey_dummy_cb (void *obj, void *param)
{
	if (log_hibernate_pb_metrics && gpio_hallsensor_detect() && pb_oneshot == HIBER_SUSP) {
		LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY,
		DEVICE_METRIC_TYPE_COUNTER, "kernel", DRIVER_NAME,
		"hibernate_wakeup_PB", 1, "");
	}
	return;
}
#else
static int in_falcon (void) {
	return 0;
}
#endif // CONFIG_FALCON

static bool wario_onkey_sense = 0;
static bool wario_onkey_long_press = 0;
static bool wario_onkey_offline_event = 0;
extern bool max77696_charger_get_connected(void);
extern int max77696_led_ctrl(unsigned int led_id, int led_state);
extern bool max77696_topsys_get_glblen0_status(void);

static void wario_onkey_longpress_work (struct work_struct *work)
{
	struct max77696_onkey *me = container_of(work, struct max77696_onkey, onkey_longpress_work.work);
	bool onkey_pressed = false;

	onkey_pressed = max77696_topsys_get_glblen0_status();
	if (onkey_pressed) {
		kobject_uevent(me->kobj, KOBJ_OFFLINE);
		printk(KERN_INFO "KERNEL: I pmic:onkey::wario button offline event\n");		
		wario_onkey_offline_event = 1;	
	}

	return;
}

static void wario_onkey_led_blink_work (struct work_struct *work)
{
	struct max77696_onkey *me = container_of(work, struct max77696_onkey, onkey_led_work.work);

	if (wario_onkey_long_press) {
		if (wario_onkey_sense && !max77696_charger_get_connected()) {	
			/* Blink Green LED */
			max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
			msleep(200);
			max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_ON);
			msleep(200);
			max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
			msleep(200);
			max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_ON);
			msleep(200);
			max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
		} else {
			/* Turn OFF Green LED */
			max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
		}	
		wario_onkey_long_press = 0;
		return;
	}

	if (wario_onkey_sense) {
		wario_onkey_long_press = 1;
		schedule_delayed_work(&(me->onkey_led_work), msecs_to_jiffies(ONKEY_LED_BLINK_INTERVAL));
	} else {
		/* Turn OFF Green LED */
		max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
	}
	return;
}

static void wario_onkey_skip_work(struct work_struct *work)
{
	wario_onkey_press_skip = 0;
	pb_oneshot = NOTHING;
	return;
}

int wario_onkey_down_ctrl(int enable)
{
	mutex_lock(&onkey_lock);
	if (enable && !wario_onkey_enabled) {
		cancel_delayed_work_sync(&onkey_skip_work);
		pmic_event_subscribe(EVENT_TOPS_EN0FALL, &onkey_up);
		pmic_event_subscribe(EVENT_TOPS_EN0RISE, &onkey_down);	
		schedule_delayed_work(&onkey_skip_work, msecs_to_jiffies(ONKEY_SKIP_INTERVAL));
		wario_onkey_enabled = 1;
	} else if (!enable && wario_onkey_enabled) {
		cancel_delayed_work_sync(&onkey_skip_work);
		pmic_event_unsubscribe(EVENT_TOPS_EN0FALL, &onkey_up);	
		pmic_event_unsubscribe(EVENT_TOPS_EN0RISE, &onkey_down);
		wario_onkey_press_skip = 1;
		wario_onkey_enabled = 0;
	} 
	mutex_unlock(&onkey_lock);
	return 0;
}

static ssize_t onkey_down_ctrl_store(struct device *dev,
		struct device_attribute *devattr, const char *buf, size_t count)
{
	int value = 0;
	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Could not update onkey_ctrl \n");
		return -EINVAL;
	}
	if (value > 0) {
		wario_onkey_down_ctrl(1);
	} else {
		wario_onkey_down_ctrl(0);
	}
	return count;
}

static ssize_t onkey_down_ctrl_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", wario_onkey_enabled);
}
static DEVICE_ATTR(onkey_down_ctrl, (S_IRUSR | S_IWUSR), onkey_down_ctrl_show, onkey_down_ctrl_store);

static struct attribute *onkey_attr[] = {
	&dev_attr_onkey_down_ctrl.attr,
	NULL
};

static const struct attribute_group onkey_attr_group = {
	.attrs = onkey_attr,
};

/* callbacks */
static void onkey_up_cb(void *obj, void *param)
{
	struct max77696_onkey *me = (struct max77696_onkey*)param;
	int i;
#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	if(pb_oneshot == HIBER_RTC_IRQ || pb_oneshot == HALL_IRQ ||
			pb_oneshot == HIBER_CHG_IRQ) {
		printk(KERN_INFO "pmic:skip onkey for pb_oneshot %s %d", __func__, pb_oneshot);
		pb_oneshot = NOTHING;
		return;
	}
#endif
	pb_oneshot = NOTHING;

	if (wario_onkey_press_skip) {
		printk(KERN_INFO "KERNEL: I pmic:onkey::wario button rel skipped\n");
		return;
	}
	wario_onkey_sense = 0;
	cancel_delayed_work_sync(&(me->onkey_longpress_work));

	if (wario_onkey_offline_event) {
		wario_onkey_offline_event = 0;
	} else {
		kobject_uevent(me->kobj, KOBJ_ONLINE); 
		printk(KERN_INFO "KERNEL: I pmic:onkey::wario button online event\n");		
	}

	for (i = ONKEY_NR_KEYS-1; i >= 0; i--) {
		if (likely(me->keycode[i] < KEY_MAX && likely(me->pressed[i]))) {
			me->pressed[i] = 0; 
			input_report_key(me->input, me->keycode[i], KEY_UP_VALUE);
			input_sync(me->input);
		}
	}
}

static void onkey_down_cb(void *obj, void *param)
{
	struct max77696_onkey *me = (struct max77696_onkey*)param;
	struct max77696_chip  *chip = me->chip;
	int i;
	bool onkey_pressed = false;
#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	if(pb_oneshot == HIBER_RTC_IRQ || pb_oneshot == HALL_IRQ ||
			pb_oneshot == HIBER_CHG_IRQ) {
		printk(KERN_INFO "pmic:skip onkey for pb_oneshot %s, %d", __func__, pb_oneshot);
		return;
	}
#endif

	if (wario_onkey_press_skip || !IS_SUBDEVICE_LOADED(led0, chip) 
	    || !IS_SUBDEVICE_LOADED(led1, chip) || !IS_SUBDEVICE_LOADED(charger, chip)) {

		printk(KERN_INFO "KERNEL: I pmic:onkey::wario button press skipped\n");
		return;
	}
	wario_onkey_sense = 1;

#if defined(CONFIG_MX6SL_WARIO_BASE)
	if (!max77696_charger_get_connected()) {	
      		/* Turn ON Green LED */
		max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_ON);
		schedule_delayed_work(&(me->onkey_led_work), msecs_to_jiffies(ONKEY_LED_BLINK_INTERVAL));
	}
#endif

	onkey_pressed = max77696_topsys_get_glblen0_status();
	if (onkey_pressed) {
		schedule_delayed_work(&(me->onkey_longpress_work), msecs_to_jiffies(ONKEY_LONGPRESS_INTERVAL));
	}

	for (i = 0; i < ONKEY_NR_KEYS; i++) {
		if (likely(me->keycode[i] < KEY_MAX)) {
			me->pressed[i] = 1;
			input_report_key(me->input, me->keycode[i], KEY_DOWN_VALUE);
			input_sync(me->input);
		}
	}
}
/* end of callbacks */

static void max77696_onkey_down_work (struct work_struct *work)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	// wait until it's OK to process events
	while (down_interruptible(&onkey_cb_sem)) {
		printk(KERN_WARNING "%s interrupted waiting for onkey_cb_sem", __func__);
	}
#endif

	/* invoke all subscribed cb's */
	pmic_event_callback(EVENT_TOPS_EN0RISE);
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	pmic_event_callback(EVENT_SW_ONKEY_PRESS);
#endif

#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	up(&onkey_cb_sem);
#endif

	return;
}

static irqreturn_t max77696_onkey_down_isr (int irq, void *data)
{
	struct max77696_onkey *me = data;

	schedule_delayed_work(&(me->down_work), 0);

	return IRQ_HANDLED;
}

static irqreturn_t max77696_onkey_mrwrn_isr (int irq, void *data)
{
	struct max77696_onkey *me = data;

	printk(KERN_EMERG "bcut: C def:pcut:wario pending 15 second battery cut:\n");

	if (likely(me->keycode[ONKEY_KEY_MRWRN] < KEY_MAX)) {
		me->pressed[ONKEY_KEY_MRWRN] = 0;
		input_report_key(me->input, me->keycode[ONKEY_KEY_MRWRN], KEY_UP_VALUE);
		input_sync(me->input);
	}

	return IRQ_HANDLED;
}

#if 0
/* TODO: Enable later on if required */
static irqreturn_t max77696_onkey_1sec_isr (int irq, void *data)
{
	struct max77696_onkey *me = data;

	printk(KERN_INFO "PBUTTON - 1SEC PRESS \n");
	if (likely(me->keycode[ONKEY_KEY_1SEC] < KEY_MAX)) {
		me->pressed[ONKEY_KEY_1SEC] = 0;
		input_report_key(me->input, me->keycode[ONKEY_KEY_1SEC], KEY_UP_VALUE);
		input_sync(me->input);
	}

	return IRQ_HANDLED;
}
#endif

static void max77696_onkey_up_work (struct work_struct *work)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	// wait until it's OK to process events
	while (down_interruptible(&onkey_cb_sem)) {
		printk(KERN_WARNING "%s interrupted waiting for onkey_cb_sem", __func__);
	}
#endif
	/* invoke all subscribed cb's */
	pmic_event_callback(EVENT_TOPS_EN0FALL);

#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	up(&onkey_cb_sem);
#endif
	return;
}

static irqreturn_t max77696_onkey_up_isr (int irq, void *data)
{
	struct max77696_onkey *me = data;

	schedule_delayed_work(&(me->up_work), 0);

	return IRQ_HANDLED;
}

static __devinit int max77696_onkey_probe (struct platform_device *pdev)
{
	struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77696_onkey_platform_data *pdata = pdev->dev.platform_data;
	struct max77696_onkey *me;
	unsigned int irq_base;
	int i, rc;

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

	me->chip = chip;
	me->dev  = &(pdev->dev);
	me->kobj = &(pdev->dev.kobj);

	mutex_init(&onkey_lock);
	me->keycode[ONKEY_KEY_EN0  ] = pdata->onkey_keycode;
	me->keycode[ONKEY_KEY_1SEC ] = pdata->hold_1sec_keycode;
	me->keycode[ONKEY_KEY_MRWRN] = pdata->mr_warn_keycode;

	irq_base = chip->irq_base + MAX77696_ROOTINT_NR_IRQS;

	me->dben0_irq = irq_base + MAX77696_TOPSYSINT_EN0_RISING;		
	me->irq[ONKEY_KEY_MRWRN] = irq_base + MAX77696_TOPSYSINT_MR_WARNING;
	me->irq[ONKEY_KEY_EN0  ] = irq_base + MAX77696_TOPSYSINT_EN0_FALLING;
#if 0
	/* TODO: Enable later on if required */
	me->irq[ONKEY_KEY_1SEC ] = irq_base + MAX77696_TOPSYSINT_EN0_1SEC;
#endif

	INIT_DELAYED_WORK(&(me->down_work), max77696_onkey_down_work);
	INIT_DELAYED_WORK(&(me->up_work), max77696_onkey_up_work);

	me->input = input_allocate_device();
	if (unlikely(!me->input)) {
		rc = -ENOMEM;
		dev_err(me->dev, "failed to allocate input device [%d]\n", rc);
		goto out_err_alloc_input;
	}

	for (i = 0; i < ONKEY_NR_KEYS; i++) {
		if (likely(me->keycode[i] < KEY_MAX)) {
			input_set_capability(me->input, EV_KEY, me->keycode[i]);
		}
	}

	me->input->name       = DRIVER_NAME;
	me->input->phys       = DRIVER_NAME"/input0";
	me->input->dev.parent = me->dev;

	rc = input_register_device(me->input);
	if (unlikely(rc)) {
		dev_dbg(me->dev, "failed to register input device [%d]\n", rc);
		goto out_err_reg_input;
	}

	/* Initial configurations */
	max77696_topsys_enable_en0_delay(pdata->wakeup_1sec_delayed_since_onkey_down);
	max77696_topsys_enable_mr_wakeup(pdata->wakeup_after_mro);
	max77696_topsys_set_mr_time(pdata->manual_reset_time);
	max77696_topsys_enable_mr(pdata->manual_reset_time > 0);

	rc = request_threaded_irq(me->dben0_irq, NULL,
			max77696_onkey_down_isr, IRQF_ONESHOT, DRIVER_NAME"_press", me);
	if (unlikely(rc < 0)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n",
				me->dben0_irq, rc);
		goto out_err_req_en0_down_irq;
	}

	rc = request_threaded_irq(me->irq[ONKEY_KEY_MRWRN], NULL,
			max77696_onkey_mrwrn_isr, IRQF_ONESHOT, DRIVER_NAME"_mr", me);
	if (unlikely(rc < 0)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n",
				me->irq[ONKEY_KEY_MRWRN], rc);
		goto out_err_req_mr_wrn_irq;
	}

#if 0
	/* TODO: Enable later on if required */

	rc = request_threaded_irq(me->irq[ONKEY_KEY_1SEC], NULL,
			max77696_onkey_1sec_isr, IRQF_ONESHOT, DRIVER_NAME"_1s", me);
	if (unlikely(rc < 0)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n",
				me->irq[ONKEY_KEY_1SEC], rc);
		goto out_err_req_en0_1sec_irq;
	}
#endif

	rc = request_threaded_irq(me->irq[ONKEY_KEY_EN0], NULL,
			max77696_onkey_up_isr, IRQF_ONESHOT, DRIVER_NAME"_rel", me);
	if (unlikely(rc < 0)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n",
				me->irq[ONKEY_KEY_EN0], rc);
		goto out_err_req_en0_up_irq;
	}

	INIT_DELAYED_WORK(&onkey_skip_work, wario_onkey_skip_work);
	INIT_DELAYED_WORK(&(me->onkey_longpress_work), wario_onkey_longpress_work);
	INIT_DELAYED_WORK(&(me->onkey_led_work), wario_onkey_led_blink_work);

	onkey_down.param = me;
	onkey_down.func = onkey_down_cb;
	rc = pmic_event_subscribe(EVENT_TOPS_EN0RISE, &onkey_down);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (%lu) [%d]\n", EVENT_TOPS_EN0RISE, rc);
		goto out_err_sub_rise;
	}

	onkey_up.param = me;
	onkey_up.func = onkey_up_cb;
	rc = pmic_event_subscribe(EVENT_TOPS_EN0FALL, &onkey_up);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to subscribe PMIC event (%lu) [%d]\n", EVENT_TOPS_EN0FALL, rc);
		goto out_err_sub_fall;
	}

#ifdef CONFIG_FALCON
	onkey_dummy.param = me;
	onkey_dummy.func = onkey_dummy_cb;
	onkey_dummy2.param = me;
	onkey_dummy2.func = onkey_dummy_cb2;
	INIT_DELAYED_WORK(&(dummy_onkey_metrics_cancel_work), dummy_onkey_metrics_cancel_work_cb);

	// This ensures that EN0FALL is always unmasked and cleared before going in to hibernate
	pmic_event_subscribe(EVENT_TOPS_EN0FALL, &onkey_dummy);
	pmic_event_subscribe(EVENT_TOPS_EN0RISE, &onkey_dummy2);
#endif // CONFIG_FALCON

	rc = sysfs_create_group(me->kobj, &onkey_attr_group);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to create attribute group [%d]\n", rc);
		goto out_err_sysfs;
	}

#if defined(CONFIG_MX6SL_WARIO_WOODY)
	rc = max77696_eventhandler_register(&onkey_press_sw_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				onkey_press_sw_handle.event_id, rc);
		goto out_err_reg_onkey_press_sw_handler;
	}
#endif

	wario_onkey_enabled = 1;

	pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
	SUBDEVICE_SET_LOADED(onkey, chip);
	return 0;

#if defined(CONFIG_MX6SL_WARIO_WOODY)
out_err_reg_onkey_press_sw_handler:
	sysfs_remove_group(me->kobj, &onkey_attr_group);
#endif
out_err_sysfs:
	pmic_event_unsubscribe(EVENT_TOPS_EN0FALL, &onkey_up);
out_err_sub_fall:
	pmic_event_unsubscribe(EVENT_TOPS_EN0RISE, &onkey_down);
out_err_sub_rise:
	free_irq(me->irq[ONKEY_KEY_EN0], me);
out_err_req_en0_up_irq:
#if 0
	free_irq(me->irq[ONKEY_KEY_1SEC], me);
out_err_req_en0_1sec_irq:
#endif
	free_irq(me->irq[ONKEY_KEY_MRWRN], me);	
out_err_req_mr_wrn_irq:
	free_irq(me->dben0_irq, me);
out_err_req_en0_down_irq:
	input_unregister_device(me->input);
	me->input = NULL;
out_err_reg_input:
	input_free_device(me->input);
out_err_alloc_input:
	platform_set_drvdata(pdev, NULL);
	kfree(me);
	return rc;
}

static __devexit int max77696_onkey_remove (struct platform_device *pdev)
{
	return 0;
}

static void max77696_onkey_shutdown (struct platform_device *pdev)
{
	struct max77696_onkey *me = platform_get_drvdata(pdev);
	int i;

#if defined(CONFIG_MX6SL_WARIO_WOODY)
	max77696_eventhandler_unregister(&onkey_press_sw_handle);
#endif
	sysfs_remove_group(me->kobj, &onkey_attr_group);
	free_irq(me->dben0_irq, me);

	for (i = 0; i < ONKEY_NR_KEYS; i++) {
		if (likely(me->irq[i] > 0)) {
			free_irq(me->irq[i], me);
		}
	}
	input_unregister_device(me->input);

	cancel_delayed_work_sync(&(me->onkey_led_work));	
	cancel_delayed_work_sync(&(me->onkey_longpress_work));
	cancel_delayed_work_sync(&onkey_skip_work);
	pmic_event_unsubscribe(EVENT_TOPS_EN0RISE, &onkey_down);
	pmic_event_unsubscribe(EVENT_TOPS_EN0FALL, &onkey_up);
	platform_set_drvdata(pdev, NULL);
	kfree(me);
}

#ifdef CONFIG_PM_SLEEP
static int max77696_onkey_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_onkey *me = platform_get_drvdata(pdev);

#if defined(CONFIG_MX6SL_WARIO_WOODY) && defined(PB_SKIP_ONESHOT)
	if (in_falcon()) {
		pb_oneshot_block_button_events();
	}
#endif
#ifdef CONFIG_FALCON
	if (in_falcon()) {
		log_hibernate_pb_metrics = 1;
	} else {
		pmic_event_unsubscribe(EVENT_TOPS_EN0FALL, &onkey_dummy);
		pmic_event_unsubscribe(EVENT_TOPS_EN0RISE, &onkey_dummy2);
	}
#endif // CONFIG_FALCON

	cancel_delayed_work_sync(&(me->onkey_led_work));
	cancel_delayed_work_sync(&(me->onkey_longpress_work));
	cancel_delayed_work_sync(&onkey_skip_work);
	return 0;
}

static int max77696_onkey_resume(struct device *dev)
{
#ifdef CONFIG_FALCON
	if (in_falcon()) {
		schedule_delayed_work(&dummy_onkey_metrics_cancel_work, msecs_to_jiffies(ONKEY_SKIP_INTERVAL));
	} else {
		pmic_event_subscribe(EVENT_TOPS_EN0FALL, &onkey_dummy);
		pmic_event_subscribe(EVENT_TOPS_EN0RISE, &onkey_dummy2);
	}
#endif // CONFIG_FALCON
	return 0;
}

static void max77696_onkey_complete(struct device *dev) {
	pb_oneshot_unblock_button_events();
	return;
}

#endif /* CONFIG_PM_SLEEP */

const struct dev_pm_ops max77696_onkey_pm = {
	.suspend = max77696_onkey_suspend,
	.resume = max77696_onkey_resume,
	.complete = max77696_onkey_complete,
};


static struct platform_driver max77696_onkey_driver = {
	.driver.name  = DRIVER_NAME,
	.driver.owner = THIS_MODULE,
	.driver.pm    = &max77696_onkey_pm,
	.probe        = max77696_onkey_probe,
	.remove       = __devexit_p(max77696_onkey_remove),
	.shutdown     = max77696_onkey_shutdown,
};

static __init int max77696_onkey_driver_init (void)
{
	return platform_driver_register(&max77696_onkey_driver);
}

static __exit void max77696_onkey_driver_exit (void)
{
	platform_driver_unregister(&max77696_onkey_driver);
}

module_init(max77696_onkey_driver_init);
module_exit(max77696_onkey_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);
