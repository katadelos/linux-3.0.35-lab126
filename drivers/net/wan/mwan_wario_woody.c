 /*
 * mwan_wario_woody.c  --  Wario Woody WAN hardware control driver
 *
 * Copyright 2005-2015 Lab126, Inc.  All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <mach/hardware.h>
#include <asm/uaccess.h>
#include <net/mwan.h>
#include <linux/device.h>
#include <linux/pmic_external.h>
//#include <mach/pmic_power.h>
#include <mach/irqs.h>
#include <linux/gpio.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <mach/boardid.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/max77696.h>

#define DEBUG 1

#ifdef DEBUG
#define log_debug(format, arg...) printk("mwan: D %s:" format, __func__, ## arg)
#else
#define log_debug(format, arg...)
#endif

#define log_info(format, arg...) printk("mwan: I %s:" format, __func__, ## arg)
#define log_err(format, arg...) printk("mwan: E %s:" format, __func__, ## arg)

#define VERSION			"2.0.0"

#define PROC_WAN		"wan"
#define PROC_WAN_POWER		"power"
#define PROC_WAN_TYPE		"type"
#define PROC_WAN_DL	    	"dl_mode"
#define PROC_WAN_SAR		"sar_detect"
#define PROC_WAN_FWRDY		"fw_ready"

static struct proc_dir_entry *proc_wan_parent;
static struct proc_dir_entry *proc_wan_power;
static struct proc_dir_entry *proc_wan_type;
static struct proc_dir_entry *proc_wan_dl;
static struct proc_dir_entry *proc_wan_sar;
static struct proc_dir_entry *proc_wan_fwrdy;

static wan_status_t wan_status = WAN_OFF;
static int modem_type = MODEM_TYPE_UNKNOWN;

static int wan_dl_enable_state = 0;
static int wan_sar_detect_state = 1;
static int wan_on_off_state = 0;

#define WAN_STRING_CLASS	"wan"
#define WAN_STRING_DEV		"mwan"

static struct file_operations mwan_ops = {
	.owner = THIS_MODULE,
};

static struct class *wan_class = NULL;
static struct device *wan_dev = NULL;
static int wan_major = 0;

// standard network deregistration time definition:
//   2s -- maximum time required between the start of power down and that of IMSI detach
//   5s -- maximum time required for the IMSI detach
//   5s -- maximum time required between the IMSI detach and power down finish (time
//         required to stop tasks, etc.)
//   3s -- recommended safety margin
#define NETWORK_DEREG_TIME	((12 + 3) * 1000)

// for the whistler module, we use an optimized path for performing deregistration
#define NETWORK_DEREG_TIME_OPT	(3 * 1000)

// minimum allowed delay between TPH notifications
// set this lower but don't remove completely just in case we get spurious GPIO transitions on boot
// 600 ms for PWR ON/OFF and then 5 sec boot until tasks are all initialized
#define WAKE_EVENT_INTERVAL	6

/*
 * Small snippet taken out of the implementation of msecs_to_jiffies
 * to optimize a little bit.
 * HZ is equal to or smaller than 1000, and 1000 is a nice
 * round multiple of HZ, divide with the factor between them,
 * but round upwards:
 */
#if HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)
#define MS_TO_JIFFIES(X)	((unsigned long)((X) + (MSEC_PER_SEC / HZ) - 1) / (MSEC_PER_SEC / HZ))
#else
#define MS_TO_JIFFIES(X)	msecs_to_jiffies(X)
#endif

#define MSEC_2_JIFFIES(X)	\
(__builtin_constant_p(X)	\
? MS_TO_JIFFIES(X):msecs_to_jiffies(X))

static unsigned long modem_off_jiffies = INITIAL_JIFFIES;	// setting this to initial jiffies to avoid getting zero value in there

#define whistler_SUPERCAP_CHARGE_TIME_MS		20
#define whistler_PWR_ON_OFF_HOLD_TIME_MS		600
#define whistler_SUPERCAP_DISCHARGE_TIME_MSEC	1000	// WAN power-down supercap discharge delay for QSC-based modems
#define whistler_BOOT_TIME_SEC			15
#define GPIO_DBOUNCE_TIME_MSEC			30

static wait_queue_head_t whistler_wait_q;
static int whistler_fw_ready_condition = 0;

extern int gpio_wan_fw_ready_irq(void);
extern int gpio_wan_fw_ready(void);
extern void gpio_wan_dl_enable(int enable);
extern void gpio_wan_sar_detect(int detect);

extern int whistler_wan_request_gpio(void);
extern void wan_free_gpio(void);
extern int gpio_wan_usb_wake(void);
extern int gpio_wan_mhi_irq(void);
extern int gpio_wan_host_wake_irq(void);
extern int gpio_wan_host_wake(void);

static void usb_regulator_control(bool on)
{
	struct regulator* usb_reg;
	
	if (on) {
		log_debug("%s:enable regulator\n",  __func__);
    		usb_reg = regulator_get(NULL, "WAN_USB_HOST_PHY");
	        if (!IS_ERR(usb_reg)) 
	   	   regulator_enable(usb_reg); 
     		regulator_put(usb_reg);
	} else  {
    		log_debug("%s:disable regulator\n",  __func__);
    		usb_reg = regulator_get(NULL, "WAN_USB_HOST_PHY");
    		if (!IS_ERR(usb_reg)) 
  		   regulator_disable(usb_reg); 
    		regulator_put(usb_reg); 
	}
}

static inline int get_wan_on_off(void)
{
	return wan_on_off_state;
}

static void set_wan_on_off(int enable)
{
	unsigned long current_jiffies;
	unsigned long time_delta;		// to get rid of the compile warning of C90 style
	unsigned long wait_jiffies;

	extern void gpio_wan_power(int);

	enable = enable != 0;	// (ensure that "enable" is a boolean)

	if (!enable) {
		modem_off_jiffies = jiffies;
	} else {
		current_jiffies = jiffies;
		time_delta = current_jiffies - modem_off_jiffies;
		// allow for supercap discharge so that the modem actually powers off
		if (time_delta < MSEC_2_JIFFIES(whistler_SUPERCAP_DISCHARGE_TIME_MSEC)){
		         // only wait for the remaining time
			 wait_jiffies = (MSEC_2_JIFFIES(whistler_SUPERCAP_DISCHARGE_TIME_MSEC) - time_delta);
			 log_info("%s:wait=%lu jiffies:modem power on delay\n",  __func__, wait_jiffies);
			 while (wait_jiffies) {
				set_current_state(TASK_UNINTERRUPTIBLE);
				wait_jiffies = schedule_timeout(wait_jiffies);
			 }
		}
	}

	log_debug("%s:enable=%d:setting WAN hardware power state\n",  __func__, enable);

	gpio_wan_power(enable);

	wan_on_off_state = enable;
}


static inline int get_wan_dl_enable(void)
{
	return wan_dl_enable_state;
}


static void set_wan_dl_enable(int enable)
{
	extern void gpio_wan_dl_enable(int);

	if (enable != get_wan_dl_enable()) {
		log_debug("%s:enable=%d:setting WAN DL enable state\n",  __func__, enable);
		gpio_wan_dl_enable(enable);
		wan_dl_enable_state = enable;
	}
}

static inline int get_wan_sar_detect(void)
{
	return wan_sar_detect_state;
}


static void set_wan_sar_detect(int detect)
{
	extern void gpio_wan_sar_detect(int);

	if (detect != get_wan_sar_detect()) {
		log_debug("%s=%d:setting WAN SAR detect state\n",  __func__, detect);

		gpio_wan_sar_detect(detect);

		wan_sar_detect_state = detect;
	}
}


static int set_wan_power(wan_status_t new_status)
{
	wan_status_t check_status = new_status == WAN_OFF_KILL ? WAN_OFF : new_status;
	int err = 0;

	if (check_status == wan_status) return 0;

	switch (new_status) {

		case WAN_ON :
			// bring up WAN_ON_OFF
			irq_set_irq_type(gpio_wan_fw_ready_irq(),IRQF_TRIGGER_RISING);
			enable_irq(gpio_wan_fw_ready_irq());
			whistler_fw_ready_condition = 0;
			set_wan_on_off(1);
			// pause following power-on before bringing up the RF_ENABLE line
			//msleep(whistler_SUPERCAP_CHARGE_TIME_MS);
			msleep(whistler_PWR_ON_OFF_HOLD_TIME_MS);
			log_debug("%s:whistler_fw_ready_condition=%d:Waiting for fw ready event\n", __func__, whistler_fw_ready_condition);
			// wait for firmware ready
			if ((err = wait_event_interruptible_timeout(
				whistler_wait_q,
				whistler_fw_ready_condition,
				whistler_BOOT_TIME_SEC*HZ)) <= 0 ) {
				log_err("%s:err=%d:whistler fw ready %s\n", __func__, err,(err)?("received signal"):("timed out"));
				err = -EIO;
			} else
				log_debug("%s:whistler_fw_ready_condition=%d:FW ready\n", __func__, whistler_fw_ready_condition);
			/* enable host wake irq */
			enable_irq(gpio_wan_mhi_irq());
			enable_irq(gpio_wan_host_wake_irq());
			break;

		case WAN_OFF :
		case WAN_OFF_KILL :
			//Set it to a Safe value.
			disable_irq(gpio_wan_mhi_irq());
			disable_irq(gpio_wan_host_wake_irq());
			disable_irq(gpio_wan_fw_ready_irq());
			msleep(whistler_PWR_ON_OFF_HOLD_TIME_MS);
			if (new_status != WAN_OFF_KILL) {
				// wait the necessary deregistration interval
				msleep(NETWORK_DEREG_TIME_OPT);
			}

			// bring down WAN_ON_OFF
			set_wan_on_off(0);
			//set_fw_ready_gpio_state(0); //PB
			new_status = WAN_OFF;
                        whistler_fw_ready_condition = 0;
			break;

		default :
			log_err("%s:request=%d:unknown power request\n",  __func__, new_status);

	}

	wan_status = new_status;
	wan_set_power_status(wan_status);

	return err;
}


static void init_modem_type(int type)
{
	if (modem_type != type) {
		log_info("%s:type=%d:setting modem type\n",  __func__, type);

		modem_type = type;
	}
}


int wan_get_modem_type(void)
{
	return modem_type;
}

EXPORT_SYMBOL(wan_get_modem_type);


static int proc_power_read(
	char *page,
	char **start,
	off_t off,
	int count,
	int *eof,
	void *data)
{
	*eof = 1;

	return sprintf(page, "%d\n", wan_status == WAN_ON ? 1 : 0);
}


static int proc_power_write(
	struct file *file,
	const char __user *buf,
	unsigned long count,
	void *data)
{
	char lbuf[16];
	unsigned char op;

	memset(lbuf, 0, sizeof(lbuf));

	if (copy_from_user(lbuf, buf, 1)) return -EFAULT;

	op = lbuf[0];
	if (op >= '0' && op <= '9') {
		wan_status_t new_wan_status = (wan_status_t)(op - '0');

		switch (new_wan_status) {

			case WAN_OFF :
			case WAN_ON :
				// perform normal on/off power handling
				if( set_wan_power(new_wan_status) != 0 ) return -EIO;
				break;

			case WAN_OFF_KILL :
				set_wan_power(new_wan_status);
				break;

			default :
				log_err("%s:request=%d:unknown power request\n",  __func__, new_wan_status);
				break;
		}

	} else
		log_err("%s:request='%c':unknown power request\n",  __func__, op);

	return count;
}


static int proc_type_read(
	char *page,
	char **start,
	off_t off,
	int count,
	int *eof,
	void *data)
{
	*eof = 1;

	return sprintf(page, "%d\n", modem_type);
}


static int proc_type_write(
	struct file *file,
	const char __user *buf,
	unsigned long count,
	void *data)
{
	char lbuf[16], ch;

	memset(lbuf, 0, sizeof(lbuf));

	if (copy_from_user(lbuf, buf, 1)) return -EFAULT;
	ch = lbuf[0];
	if (ch >= '0' && ch <= '9')
		init_modem_type(ch - '0');
	else
		log_err("%s:type=%c:invalid type\n", __func__, ch);

	return count;
}


static int proc_dl_read(
	char *page,
	char **start,
	off_t off,
	int count,
	int *eof,
	void *data)
{
	*eof = 1;

	return sprintf(page, "%d\n", get_wan_dl_enable());
}


static int proc_dl_write(
	struct file *file,
	const char __user *buf,
	unsigned long count,
	void *data)
{
	char lbuf[16];

	memset(lbuf, 0, sizeof(lbuf));

	if (copy_from_user(lbuf, buf, 1)) return -EFAULT;
	
	set_wan_dl_enable(lbuf[0] != '0');

	return count;
}

static int proc_sar_read(
	char *page,
	char **start,
	off_t off,
	int count,
	int *eof,
	void *data)
{
	*eof = 1;

	return sprintf(page, "%d\n", get_wan_sar_detect());
}


static int proc_sar_write(
	struct file *file,
	const char __user *buf,
	unsigned long count,
	void *data)
{
	char lbuf[16];

	memset(lbuf, 0, sizeof(lbuf));

	if (copy_from_user(lbuf, buf, 1)) return -EFAULT;
	
	set_wan_sar_detect(lbuf[0] != '0');

	return count;
}

static int proc_fwrdy_read(
	char *page,
	char **start,
	off_t off,
	int count,
	int *eof,
	void *data)
{
	*eof = 1;

	return sprintf(page, "%d\n", gpio_wan_fw_ready());
}


static void do_whistler_fw_ready(struct work_struct *dummy)
{
	/* FW ready line is high until power off */
	if (gpio_wan_fw_ready()) {
		if (!whistler_fw_ready_condition) {
			log_debug("%s::whistler fw ready\n", __func__);
			whistler_fw_ready_condition = 1;
			wake_up(&whistler_wait_q);
		}
		irq_set_irq_type(gpio_wan_fw_ready_irq(),IRQF_TRIGGER_FALLING);
	} else if (whistler_fw_ready_condition) {
		/* Detect falling edge. Modem resets ! */
		log_debug("%s::modem reset?\n", __func__);
		whistler_fw_ready_condition =  0;
		if (usb_wake_callback ) {
                        (*usb_wake_callback)(usb_wake_callback_data);
                }
		irq_set_irq_type(gpio_wan_fw_ready_irq(),IRQF_TRIGGER_RISING);
	}
	enable_irq(gpio_wan_fw_ready_irq());
}
static DECLARE_DELAYED_WORK(whistler_fw_ready_work, do_whistler_fw_ready);

static irqreturn_t whistler_fw_ready_handler(int irq, void *devid)
{
	/* debounce time for 30ms */
	disable_irq_nosync(irq);
	schedule_delayed_work(&whistler_fw_ready_work, MSEC_2_JIFFIES(GPIO_DBOUNCE_TIME_MSEC));
	return IRQ_HANDLED;
}

static void do_whistler_host_wake(struct work_struct *dummy)
{
	static unsigned long last_tph_sec = 0;
	unsigned long current_sec = CURRENT_TIME_SEC.tv_sec;

        log_debug("%s::Host wake called..(for TPH)\n", __func__);

	if (gpio_wan_host_wake()) {
                log_debug("%s::Host wake (for TPH)\n", __func__);

                /* limit back-to-back interrupts.*/
                if (current_sec - last_tph_sec >= WAKE_EVENT_INTERVAL) {
                        last_tph_sec = current_sec;
                        kobject_uevent(&wan_dev->kobj, KOBJ_CHANGE);
                        log_info("%s::tph event occurred; notifying system of TPH\n", __func__);
                }
	}

	enable_irq(gpio_wan_host_wake_irq());
}

static DECLARE_DELAYED_WORK(whistler_host_wake_work, do_whistler_host_wake);
static irqreturn_t whistler_host_wake_handler(int irq, void  *devid)
{
	/* debounce time for 30ms */
	disable_irq_nosync(irq);
	schedule_delayed_work(&whistler_host_wake_work, MSEC_2_JIFFIES(GPIO_DBOUNCE_TIME_MSEC));
	return IRQ_HANDLED;
}

static void do_whistler_usb_wake(struct work_struct *dummy)
{
	log_debug("whistler_usb::wakeup USB\n");
	if( usb_wake_callback ) {
		(*usb_wake_callback)(usb_wake_callback_data);
	}
	enable_irq(gpio_wan_mhi_irq());
}

static DECLARE_DELAYED_WORK(whistler_usb_wake_work, do_whistler_usb_wake);
static irqreturn_t whistler_usb_wake_handler(int irq, void  *devid)
{
	disable_irq_nosync(irq);
	schedule_work(&whistler_usb_wake_work);
	return IRQ_HANDLED;
}

static int whistler_suspend(struct platform_device *pdev, pm_message_t state)
{
	log_debug("%s::disable irqs\n", __func__);
	disable_irq(gpio_wan_fw_ready_irq());
	disable_irq(gpio_wan_mhi_irq());
	disable_irq(gpio_wan_host_wake_irq());
        if (wan_on_off_state == 0) usb_regulator_control(0);
	return 0;
}

static int whistler_resume(struct platform_device *pdev)
{
	log_debug("%s::enable irqs\n", __func__);
	if (wan_on_off_state == 0 ) {
		usb_regulator_control(1);
		udelay(600);
	}
	enable_irq(gpio_wan_fw_ready_irq());
	enable_irq(gpio_wan_mhi_irq());
	enable_irq(gpio_wan_host_wake_irq());
	return 0;
}

/* dummy function to prevent error messages */
static void whistler_device_release (struct device *dev)
{
	return;
}

static int __devinit whistler_probe(struct platform_device *pdev)
{
	int ret = -1;
	int error = 0;

	log_debug("%s::Probing whistler device\n", __func__);
	if (whistler_wan_request_gpio()) {
		log_err("%s:failed to request WAN GPIO\n", __func__);
		goto exit0;
        }
	init_waitqueue_head(&whistler_wait_q);
	error = request_irq(gpio_wan_fw_ready_irq(),whistler_fw_ready_handler,IRQF_TRIGGER_RISING,"whistler_fw_ready",NULL);
	if (error < 0) {
		log_err("%s:irq=%d:Could not request IRQ\n",  __func__, gpio_wan_fw_ready_irq());
		goto exit1;
	}

	error = request_irq(gpio_wan_mhi_irq(),whistler_usb_wake_handler,IRQF_TRIGGER_RISING,"whistler_usb_wake",NULL);
	if (error < 0) {
		log_err("whistler_usb_irq_err:irq=%d:Could not request IRQ\n", gpio_wan_mhi_irq());
		goto exit_fw_irq;
	}
        
	error = request_irq(gpio_wan_host_wake_irq(),whistler_host_wake_handler,IRQF_TRIGGER_RISING,"whistler_host_wake",NULL);
	if (error < 0) {
		log_err("%s:irq=%d:Could not request IRQ\n",  __func__, gpio_wan_host_wake());
		goto exit_usb_irq;
	}

	/* disable usb wake irq until WAN is powered up */
	disable_irq(gpio_wan_mhi_irq());
	disable_irq(gpio_wan_host_wake_irq());
	disable_irq(gpio_wan_fw_ready_irq());
	wan_major = register_chrdev(0, WAN_STRING_DEV, &mwan_ops);
	if (wan_major < 0) {
		ret = wan_major;
		log_err("%s:device=" WAN_STRING_DEV ",err=%d:could not register device\n", __func__, ret);
		goto exit_wake_irq;
	}

	wan_class = class_create(THIS_MODULE, WAN_STRING_CLASS);
	if (IS_ERR(wan_class)) {
		ret = PTR_ERR(wan_class);
		log_err("%s:err=%d:could not create class\n", __func__, ret);
		goto exit2;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
	wan_dev = device_create(wan_class, NULL, MKDEV(wan_major, 0), NULL, WAN_STRING_DEV);
#else
	wan_dev = device_create_drvdata(wan_class, NULL, MKDEV(wan_major, 0), NULL, WAN_STRING_DEV);
#endif
	if (IS_ERR(wan_dev)) {
		ret = PTR_ERR(wan_dev);
		log_err("%s:err=%d:could not create class device\n",  __func__,ret);
		goto exit3;
	}

	wan_set_power_status(WAN_OFF);

	ret = 0;
	goto exit0;

exit3:
	class_destroy(wan_class);
	wan_class = NULL;

exit2:
	unregister_chrdev(wan_major, WAN_STRING_DEV);

exit_wake_irq:
	free_irq(gpio_wan_host_wake_irq(),NULL);

exit_usb_irq:
	free_irq(gpio_wan_mhi_irq(),NULL);

exit_fw_irq:
	free_irq(gpio_wan_fw_ready_irq(),NULL);
exit1:
	wan_free_gpio();

exit0:
	return ret;

}

static int __devexit whistler_remove(struct platform_device *pdev)
{
	extern void gpio_wan_exit(void *);

	log_debug("%s::removing whistler device\n", __func__);
	wan_set_power_status(WAN_INVALID);
	if (wan_dev != NULL) {
		device_destroy(wan_class, MKDEV(wan_major, 0));
		wan_dev = NULL;
		class_destroy(wan_class);
		unregister_chrdev(wan_major, WAN_STRING_DEV);
	}
	free_irq(gpio_wan_fw_ready_irq(),NULL);
	free_irq(gpio_wan_mhi_irq(),NULL);
	free_irq(gpio_wan_host_wake_irq(),NULL);

	wan_free_gpio();
	return 0;
}

static struct platform_driver whistler_driver = {
        .driver = {
                   .name = "mwan",
                   .owner  = THIS_MODULE,
                   },
        .suspend = whistler_suspend,
        .resume = whistler_resume,
	.probe = whistler_probe,
	.remove = whistler_remove,
};

static struct platform_device whistler_device = {
	.name = "mwan",
	.id = -1,
	.dev = {
		.release = whistler_device_release,
	       },
};


static int wan_init(void)
{
	log_info("%s\n", __func__);
	if (platform_device_register(&whistler_device) != 0) {
		log_err("%s::can not register whistler device\n", __func__);
		return -1;
	}
	if (platform_driver_register(&whistler_driver) != 0) {
		log_err("%s::can not register whistler driver\n", __func__);
		platform_device_unregister(&whistler_device);
	        return -1;
	}

	return 0;
}


static void wan_exit(void)
{
    platform_driver_unregister(&whistler_driver);
    platform_device_unregister(&whistler_device);
}


static int __init mwan_init(void)
{
	int ret = 0;

	log_info("%s:wario WAN hardware driver " VERSION "\n", __func__);

	// create the "/proc/wan" parent directory
	proc_wan_parent = create_proc_entry(PROC_WAN, S_IFDIR | S_IRUGO | S_IXUGO, NULL);
	if (proc_wan_parent != NULL) {

		// create the "/proc/wan/power" entry
		proc_wan_power = create_proc_entry(PROC_WAN_POWER, S_IWUSR | S_IRUGO, proc_wan_parent);
		if (proc_wan_power != NULL) {
			proc_wan_power->data = NULL;
			proc_wan_power->read_proc = proc_power_read;
			proc_wan_power->write_proc = proc_power_write;
		}

		// create the "/proc/wan/type" entry
		proc_wan_type = create_proc_entry(PROC_WAN_TYPE, S_IWUSR | S_IRUGO, proc_wan_parent);
		if (proc_wan_type != NULL) {
			proc_wan_type->data = NULL;
			proc_wan_type->read_proc = proc_type_read;
			proc_wan_type->write_proc = proc_type_write;
		}

		// create the "/proc/wan/dl_mode" entry
		proc_wan_dl = create_proc_entry(PROC_WAN_DL, S_IWUSR | S_IRUGO, proc_wan_parent);
		if (proc_wan_dl != NULL) {
			proc_wan_dl->data = NULL;
			proc_wan_dl->read_proc = proc_dl_read;
			proc_wan_dl->write_proc = proc_dl_write;
		}
     
		// create the "/proc/wan/sar_detect" entry
		proc_wan_sar = create_proc_entry(PROC_WAN_SAR, S_IWUSR | S_IRUGO, proc_wan_parent);
		if (proc_wan_sar != NULL) {
			proc_wan_sar->data = NULL;
			proc_wan_sar->read_proc = proc_sar_read;
			proc_wan_sar->write_proc = proc_sar_write;
		}

		// create the "/proc/wan/fw_ready" entry
		proc_wan_fwrdy = create_proc_entry(PROC_WAN_FWRDY, S_IRUGO, proc_wan_parent);
		if (proc_wan_fwrdy != NULL) {
			proc_wan_fwrdy->data = NULL;
			proc_wan_fwrdy->read_proc = proc_fwrdy_read;
		}

	} else 
		ret = -1;

	if (ret == 0) ret = wan_init();

	return ret;
}


static void __exit mwan_exit(void)
{
	if (proc_wan_parent != NULL) {
		remove_proc_entry(PROC_WAN_DL, proc_wan_parent);
		remove_proc_entry(PROC_WAN_TYPE, proc_wan_parent);
		remove_proc_entry(PROC_WAN_POWER, proc_wan_parent);
		remove_proc_entry(PROC_WAN_SAR, proc_wan_parent);
		remove_proc_entry(PROC_WAN_FWRDY, proc_wan_parent);
		remove_proc_entry(PROC_WAN, NULL);

		proc_wan_dl = proc_wan_type = proc_wan_power = proc_wan_sar = proc_wan_parent = NULL;
	}

	wan_exit();
}


module_init(mwan_init);
module_exit(mwan_exit);

MODULE_DESCRIPTION("Wario WAN hardware driver");
MODULE_AUTHOR("Lab126");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);

