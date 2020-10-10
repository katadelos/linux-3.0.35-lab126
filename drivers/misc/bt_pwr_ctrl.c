/*
 * Copyright 2014-2015 Amazon Technologies, Inc.
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
 * You should have received a copy of the GNU General Public
 * License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */

/*
 * Driver Summary:
 * This driver serves as power control driver for any 3rd party Bluetooth
 * module in Amazon products. The driver power controls the Bluetooth chip
 * and also performs actions corresponding to any specific power optimisat-
 * -ions done for Amazon consumer products.
 * This driver is designed to have minimal dependency on SoC, so that it can
 * be ported easily across different platforms. Atleast thats the intention!
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/serial_core.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <net/bluetooth/bt_pwr_ctrl.h>

/* state variable names and bit positions */
#define BT_PROTO	0x01
#define BT_TXDATA	0x02
#define BT_ASLEEP	0x04
#define BT_DEV_WAKE	0x08
#define BT_SUSPEND	0x10

/* work function */
static void bluesleep_work(struct work_struct *work);

/* work queue */
DECLARE_DELAYED_WORK(sleep_workqueue, bluesleep_work);

/* Macros for handling sleep work */
#define bluesleep_rx_busy() do { schedule_delayed_work(&sleep_workqueue, 0); } while(0)

#define bluesleep_tx_busy() do { schedule_delayed_work(&sleep_workqueue, 0); } while(0)

#define bluesleep_rx_idle() do { schedule_delayed_work(&sleep_workqueue, 0); } while(0)

#define bluesleep_tx_idle() do { schedule_delayed_work(&sleep_workqueue, 0); } while(0)

static int bluesleep_start(void);
static void bluesleep_stop(void);

typedef struct bt_priv_data {
/* dirs within procfs */
struct proc_dir_entry *bt_dir;
struct proc_dir_entry *sleep_dir;

/* BT chip enabled state */
unsigned enable;
/* global state  */
unsigned long state;

/* UART clock enable/disable flag */
unsigned long clk_en;

/* store ptr to platform data */
struct bt_pwr_data *pdata;
unsigned host_wake_irq;
int irq_polarity;
int has_ext_wake;
} bt_priv_data_t;

/* Pin polarities */
#define ACTIVE_LOW	0
#define ACTIVE_HIGH 	1

/** Polarities defined here should match those mentioned in
 ** bsa_server - Projects/bte/main/bt_target.h
 **/
#define BT_WAKE_ASSERT	 ACTIVE_LOW
#define HOST_WAKE_ASSERT ACTIVE_LOW

#define BT_WAKE_DEASSERT   ACTIVE_HIGH
#define HOST_WAKE_DEASSERT ACTIVE_HIGH

/* Lock for state transitions */
static spinlock_t rw_lock;

/* module usage */
static atomic_t open_count = ATOMIC_INIT(1);

/* Transmission timer */
static struct timer_list tx_timer;
#define TX_TIMER_INTERVAL	10

/* use this ptr for all driver specific
 * data within this file */
static bt_priv_data_t *gp_privdata = NULL;

/* tasklet to respond to change in hostwake line */
static struct tasklet_struct hostwake_task;

static int bluesleep_can_sleep(struct bt_pwr_data *pdata)
{
	if ((gpio_get_value(pdata->bt_dev_wake) == BT_WAKE_DEASSERT) &&
		(gpio_get_value(pdata->bt_host_wake) == HOST_WAKE_DEASSERT)) {
		return 1;
	}
	return 0;
}

void bluesleep_sleep_wakeup(void)
{
	if (test_bit(BT_ASLEEP, &gp_privdata->state)) {
		printk(KERN_WARNING "bluesleep: Asleep false\n");
		mod_timer(&tx_timer,
				jiffies + (TX_TIMER_INTERVAL * HZ));
		clear_bit(BT_ASLEEP, &gp_privdata->state);
	}
}

static void bluesleep_work(struct work_struct *work)
{
	if (!test_bit(BT_PROTO, &gp_privdata->state))
		return;

	/* Check if either WAKe GPIOs is enabled (ACTIVE LOW) */
	if (bluesleep_can_sleep(gp_privdata->pdata)) {
		if (test_bit(BT_ASLEEP, &gp_privdata->state)) {
			printk(KERN_WARNING "bluesleep: already asleep\n");
			return;
		}
		/* TODO First check if TX line is empty */
		printk(KERN_WARNING "bluesleep: Asleep true\n");
		set_bit(BT_ASLEEP, &gp_privdata->state);
	} else {
		bluesleep_sleep_wakeup();
	}
}

/**
 * Handles transmission timer expiration.
 * @param data Not used.
 */
static void bluesleep_tx_timer_expire(unsigned long unused)
{
	unsigned long irq_flags;
	spin_lock_irqsave(&rw_lock, irq_flags);
	/* were we silent during the last timeout? */
	if (!test_bit(BT_TXDATA, &gp_privdata->state)) {
		printk(KERN_INFO "Tx has been idle\n");
		gpio_set_value(gp_privdata->pdata->bt_dev_wake, BT_WAKE_DEASSERT);
		bluesleep_tx_idle();
	} else {
		printk(KERN_INFO "Tx data during last period\n");
		mod_timer(&tx_timer, jiffies + (TX_TIMER_INTERVAL*HZ));
	}
	/* clear the incoming data flag */
	clear_bit(BT_TXDATA, &gp_privdata->state);
	spin_unlock_irqrestore(&rw_lock, irq_flags);
}

static void bluesleep_hostwake_task(unsigned long data)
{
	unsigned long irq_flags;
	struct bt_pwr_data *pdata = (struct bt_pwr_data *)data;
	if (!pdata) {
		printk(KERN_ERR "No private data in tasklet\n");
		return;
	}

	spin_lock_irqsave(&rw_lock, irq_flags);

	/* if asserted, rx line is busy and host needs to be/remain
	awake or else rx line is idle and host may sleep */
	if (gpio_get_value(pdata->bt_host_wake)
			== HOST_WAKE_ASSERT) {
		bluesleep_rx_busy();
	} else {
		bluesleep_rx_idle();
	}
	spin_unlock_irqrestore(&rw_lock, irq_flags);
}

/* Handles both Host wake and sleep operations */
static irqreturn_t hostwake_isr(int irqnum, void *data)
{
	/* schedule a tasklet to handle host wake line change */
	tasklet_schedule(&hostwake_task);

	return IRQ_HANDLED;
}

static int bluesleep_start(void)
{
	unsigned long irq_flags;

	printk(KERN_INFO "Bluesleep protocol start\n");

	spin_lock_irqsave(&rw_lock, irq_flags);

	if (test_bit(BT_PROTO, &gp_privdata->state)) {
		spin_unlock_irqrestore(&rw_lock, irq_flags);
		return 0;
	}

	if (!atomic_dec_and_test(&open_count)) {
		atomic_inc(&open_count);
		spin_unlock_irqrestore(&rw_lock, irq_flags);
		return -EBUSY;
	}

	mod_timer(&tx_timer, jiffies + (TX_TIMER_INTERVAL*HZ));

	/* assert BT_DEV_WAKE to remain up to start with */
	gpio_set_value(gp_privdata->pdata->bt_dev_wake, BT_WAKE_ASSERT);
	clear_bit(BT_ASLEEP, &gp_privdata->state);

	enable_irq(gp_privdata->host_wake_irq);
	set_bit(BT_PROTO, &gp_privdata->state);
	spin_unlock_irqrestore(&rw_lock, irq_flags);

	return 0;
}

static void bluesleep_stop(void)
{
	unsigned long irq_flags;

	printk(KERN_INFO "Bluesleep protocol stop\n");

	spin_lock_irqsave(&rw_lock, irq_flags);

	if (!test_bit(BT_PROTO, &gp_privdata->state)) {
		spin_unlock_irqrestore(&rw_lock, irq_flags);
		return;
	}

	clear_bit(BT_PROTO, &gp_privdata->state);
	disable_irq(gp_privdata->host_wake_irq);

	/* Keep BT chip asserted since we are exiting bluesleep protocol */
	gpio_set_value(gp_privdata->pdata->bt_dev_wake, BT_WAKE_ASSERT);
	set_bit(BT_TXDATA, &gp_privdata->state);
	bluesleep_sleep_wakeup();

	del_timer(&tx_timer);

	atomic_inc(&open_count);
	spin_unlock_irqrestore(&rw_lock, irq_flags);
}

static ssize_t
btpwrctrl_enable_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	return sprintf(buf, "%s\n", (gp_privdata->enable ? "enabled" : "disabled"));
}

static ssize_t
btpwrctrl_enable_store(struct device* dev,
	struct device_attribute* attr,
	const char* buf,
	size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bt_pwr_data *pdata = platform_get_drvdata(pdev);

	if(!pdata) {
		printk(KERN_ERR "platform data is null"); 
		return 0; 
	}

	if (sscanf(buf, "%u", (unsigned *)&gp_privdata->enable) <= 0)
		return -EINVAL;

	printk(KERN_INFO "Turning BT chip %s\n", gp_privdata->enable?"ON":"OFF");
	
	gpio_set_value(pdata->bt_rst, !!gp_privdata->enable);
	
	return size;
}
static DEVICE_ATTR(btenable, 0644, btpwrctrl_enable_show, btpwrctrl_enable_store);

/* Read, write proc entries */
/* Note: simple_(read_from)/(write_to)_buffer APIs seems to
 * invoke the proc entries multiple times so use copy_*_user
 * APIs below 
 */
#define BUFSZ 	2
static ssize_t read_proc_btenable(struct file *file,
                          char __user *buffer,
                          size_t len,
                          loff_t *offset)
{
        static bool count_flag = true;
	char procbuf[BUFSZ];

	if(gp_privdata->enable) 
		procbuf[0] = '1';
	else
		procbuf[0] = '0';
	procbuf[1] = '\0';

        if (count_flag == false) {
                count_flag = true;
                return 0;
        }
	if (copy_to_user(buffer, procbuf, BUFSZ))
		return -EFAULT;

        if ((len - BUFSZ) > 0) {
                /* Mark EOF */
                count_flag = false;
        }
        return BUFSZ;
}

static ssize_t write_proc_btenable(struct file *file,
                          const char __user *buffer,
                           size_t len,
                           loff_t *offset)
{
	char procbuf[BUFSZ];
        if (copy_from_user(procbuf, buffer, BUFSZ)) {
                return -EFAULT;
        }

	if (procbuf[0] == '0')
		gp_privdata->enable = 0;
	else 
		gp_privdata->enable = 1;
	gpio_set_value(gp_privdata->pdata->bt_rst, !!gp_privdata->enable);
	printk(KERN_INFO "Turning BT chip %s\n", gp_privdata->enable?"ON":"OFF");
	return len;
}

ssize_t read_proc_hostwake(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	char hostwake;
	static int buffer_count = 1;

	if (buffer_count == 0) {
		buffer_count = 1;
		return 0;
	}

	hostwake = gpio_get_value(gp_privdata->pdata->bt_host_wake) ? '1' : '0';
	if (copy_to_user(buf, &hostwake, 1))
		return -EFAULT;

	if ((count - 1) > 0) {
		/* Mark EOF */
		buffer_count--;
	}
	return 1;
}

ssize_t read_proc_devwake(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	char devwake;
	unsigned long irq_flags;
	static int buffer_count = 1;

	if (buffer_count == 0) {
		buffer_count = 1;
		return 0;
	}

	spin_lock_irqsave(&rw_lock, irq_flags);
	devwake = gpio_get_value(gp_privdata->pdata->bt_dev_wake) ? '1' : '0';
	spin_unlock_irqrestore(&rw_lock, irq_flags);

	if (copy_to_user(buf, &devwake, 1))
		return -EFAULT;

	if ((count - 1) > 0) {
		/* Mark EOF */
		buffer_count--;
	}
	return 1;
}

ssize_t write_proc_devwake(struct file *filp, const char *buf, size_t count,
		loff_t *offp)
{
	char procbuf[BUFSZ];
	unsigned long irq_flags;

	if (copy_from_user(procbuf, buf, BUFSZ)) {
		return -EFAULT;
	}

	spin_lock_irqsave(&rw_lock, irq_flags);
	if (procbuf[0] == '0') {
		/*bt_wake Asserted..wake up the device since tx is busy*/
		gpio_set_value(gp_privdata->pdata->bt_dev_wake, 0);
		set_bit(BT_TXDATA, &gp_privdata->state);
		bluesleep_tx_busy();
	} else if (procbuf[0] == '1') {
		gpio_set_value(gp_privdata->pdata->bt_dev_wake, 1);
		clear_bit(BT_TXDATA, &gp_privdata->state);
		bluesleep_tx_idle();
	} else {
		spin_unlock_irqrestore(&rw_lock, irq_flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&rw_lock, irq_flags);

	return count;
}

ssize_t read_proc_proto(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	char proto;
	unsigned long irq_flags;
	static int buffer_count = 1;

	if (buffer_count == 0) {
		buffer_count = 1;
		return 0;
	}

	spin_lock_irqsave(&rw_lock, irq_flags);
	proto = test_bit(BT_PROTO, &gp_privdata->state) ? '1' : '0';
	spin_unlock_irqrestore(&rw_lock, irq_flags);

	if (copy_to_user(buf, &proto, 1))
		return -EFAULT;

	if ((count - 1) > 0) {
		/* Mark EOF */
		buffer_count--;
	}
	return 1;
}

ssize_t write_proc_proto(struct file *filp, const char *buf, size_t count,
		loff_t *offp)
{
	char proto;

	if (!gp_privdata || !gp_privdata->pdata) {
		printk(KERN_ERR "driver priv and pdata not initialized!");
		return -EINVAL;
	}

	if (count < 1)
		return -EINVAL;

	if (copy_from_user(&proto, buf, 1))
		return -EFAULT;

	if (proto == '0')
		bluesleep_stop();
	else
		bluesleep_start();

	/* claim that we wrote everything */
	return count;
}

ssize_t read_proc_asleep(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	unsigned long irq_flags;
	char asleep;
	static int buffer_count = 1;

	if (buffer_count == 0) {
		buffer_count = 1;
		return 0;
	}

	spin_lock_irqsave(&rw_lock, irq_flags);
	asleep = test_bit(BT_ASLEEP, &gp_privdata->state) ? '1' : '0';
	spin_unlock_irqrestore(&rw_lock, irq_flags);

	if (copy_to_user(buf, &asleep, 1))
		return -EFAULT;

	if ((count - 1) > 0) {
		/* Mark EOF */
		buffer_count--;
	}
	return 1;
}

extern void uart_sleep_and_switch_parent(int k);

ssize_t write_proc_uart_clk(struct file *filp, const char *buf, size_t count,
		loff_t *offp)
{
	int on;
	printk(KERN_ERR "BT: Console UART may be going down after this..");
	if(1 == sscanf(buf, " %d", &on))
		uart_sleep_and_switch_parent(!!on);
	
	/* claim that we wrote everything */
	return count;
}

static const struct file_operations proc_uart_clk_fops = {
write : write_proc_uart_clk
};

static const struct file_operations proc_hostwake_fops = {
read: read_proc_hostwake,
};

static const struct file_operations proc_devwake_fops = {
read: read_proc_devwake,
write : write_proc_devwake
};

static const struct file_operations proc_proto_fops = {
read: read_proc_proto,
write : write_proc_proto
};

static const struct file_operations proc_asleep_fops = {
read: read_proc_asleep,
};

static const struct file_operations proc_btenable_fops = {
read: read_proc_btenable,
write: write_proc_btenable
};

struct bt_proc_entry {
	const char *name;
	mode_t mode;
	const struct file_operations *fops;
	//struct proc_dir_entry *parent_entry;
	struct proc_dir_entry *this_entry;
};

static struct bt_proc_entry proc_entries[] = {
	{ "uart_clk", S_IWUGO, &proc_uart_clk_fops,   NULL },
	{ "hostwake", S_IRUGO, &proc_hostwake_fops,   NULL  },
	{ "btwake",   S_IRUGO | S_IWUGO, &proc_devwake_fops,    NULL  },
	{ "proto",    S_IRUGO | S_IWUGO, &proc_proto_fops,      NULL  },
	{ "asleep",   S_IRUGO, &proc_asleep_fops,     NULL  },
};

static struct bt_pwr_data *btpwrctrl_of_populate_pdata(struct device *dev)
{
	struct device_node *node = dev->of_node;
	struct bt_pwr_data *pdata = dev->platform_data;
	
	if (!node || pdata)
		return pdata;

/* Do this code only for fully DT supported kernels */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "not enough memory\n");
		return NULL;
	}

	pdata->bt_rst = of_get_named_gpio(node, "bt_rst", 0);
	if (!gpio_is_valid(pdata->bt_rst)) {
		dev_err(dev, "invalid gpio %d\n", pdata->bt_rst);
		return NULL;
	}
	printk(KERN_INFO "\nbt_reset pin obtained is %d\n", pdata->bt_rst);

        pdata->bt_dev_wake = of_get_named_gpio(node, "bt_dev_wake", 0);
        if (!gpio_is_valid(pdata->bt_dev_wake)) {
                dev_err(dev, "invalid gpio %d\n", pdata->bt_dev_wake);
                return NULL;
        }
	printk(KERN_INFO "\nbt_dev_wake pin obtained is %d\n", pdata->bt_dev_wake);

        pdata->bt_host_wake = of_get_named_gpio(node, "bt_host_wake", 0);
        if (!gpio_is_valid(pdata->bt_host_wake)) {
                dev_err(dev, "invalid gpio %d\n", pdata->bt_host_wake);
                return NULL;
        }
	printk(KERN_INFO "\nbt_host_wake pin obtained is %d\n", pdata->bt_host_wake);
#endif
	return pdata;
}

static int bt_pwrctrl_probe(struct platform_device *pdev)
{
	int retval=0;
	struct bt_pwr_data *pdata = pdev->dev.platform_data;
	int i, sz;
	gp_privdata = kzalloc(sizeof(bt_priv_data_t), GFP_KERNEL);

	if(!pdata) {
#ifdef CONFIG_OF
		/* try getting from DTs */
		pdata = btpwrctrl_of_populate_pdata(&pdev->dev);
		if (!pdata) {
			dev_err(&pdev->dev, "no pdata found!");
			kfree(gp_privdata);
			return -EINVAL;
		}
#else
		pdata = devm_kzalloc(pdev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(pdev, "not enough memory\n");
			return NULL;
		}
#define MX6SL_WARIO_BT_REG_ON           IMX_GPIO_NR(2, 19)      /* LCD_RESET - BT_DISABLE */
#define MX6SL_WARIO_BT_DEV_WAKE         IMX_GPIO_NR(5,  13)     /* SD1_DAT2 */
#define MX6SL_WARIO_BT_HOST_WAKE        IMX_GPIO_NR(3, 29)      /* KEY_ROW2 - bt_host_wake */

		pdata->bt_rst = MX6SL_WARIO_BT_REG_ON;
		pdata->bt_dev_wake = MX6SL_WARIO_BT_DEV_WAKE;
		pdata->bt_host_wake = MX6SL_WARIO_BT_HOST_WAKE;
#endif
		retval = gpio_request(pdata->bt_rst, "BT_RESET");
		if(retval) {
			printk(KERN_ERR "request BT_RESET pin failed!");
			kfree(gp_privdata);
			return retval;
		}
		gpio_direction_output(pdata->bt_rst,0);

		retval = gpio_request(pdata->bt_dev_wake, "BT_DEV_WAKE");
		if(retval) {
			printk(KERN_ERR "request BT_DEV_WAKE pin failed!");
			goto err1;
		}
		gpio_direction_output(pdata->bt_dev_wake,0);

		retval = gpio_request(pdata->bt_host_wake, "BT_HOST_WAKE");
		if(retval) {
			printk(KERN_ERR "request BT_HOST_WAKE pin failed!");
			goto err2;
		}
		gpio_direction_input(pdata->bt_host_wake);
	}
	else
	{
		printk(KERN_INFO "preset in pdata: bt_rst:%d\n", pdata->bt_rst);
		printk(KERN_INFO "preset in pdata: bt_host_wake:%d\n",pdata->bt_host_wake);
		printk(KERN_INFO "preset in pdata: bt_dev_wake:%d\n",pdata->bt_dev_wake);
	}

	/* when here, all GPIOs have been requested, so do sanity check of
	 * gpios before proceeding further - because, in some versions,
	 * request gpio is done outside of this file 
	 */
	if (!gpio_is_valid(pdata->bt_rst) || !gpio_is_valid(pdata->bt_host_wake) 
		|| !gpio_is_valid(pdata->bt_dev_wake)) {
		printk(KERN_ERR "BT gpios are invalid!");
		goto err3;
	}

	/* convert to IRQ */
	gp_privdata->host_wake_irq = gpio_to_irq(pdata->bt_host_wake);
	if (gp_privdata->host_wake_irq < 0) {
		printk(KERN_ERR "No irq# obtained for BT Host Wake pin\n");
		goto err3;
	} else {
		printk(KERN_ERR "IRQ# for BT HOST WAKE: %d\n",  gp_privdata->host_wake_irq);
	}

	/* request irq */
	retval = devm_request_irq(&pdev->dev, gp_privdata->host_wake_irq, hostwake_isr,
	IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "BT hostwake", NULL);
	if(retval < 0) {
		printk(KERN_ERR "request irq failed with retcode %d\n", retval);
		goto err3;
	}
	/* disable to start with */
	disable_irq(gp_privdata->host_wake_irq);

	/* set private data */
	platform_set_drvdata(pdev, pdata);
	gp_privdata->pdata = pdata;

	/* Create sys entry : btenable */
	if ((retval = device_create_file(&pdev->dev, &dev_attr_btenable) < 0)) {
		printk(KERN_ERR "%s: sys entry creation failed! with retcode %d", __func__, retval);
		goto err4;
	}

	/*Create bluesleep procfs entries*/
	gp_privdata->bt_dir = proc_mkdir("bluetooth", NULL);
	if (gp_privdata->bt_dir == NULL) {
		printk(KERN_ERR "Unable to create /proc/bluetooth directory\n");
		retval = -EFAULT;
		goto err5;
	}

	gp_privdata->sleep_dir = proc_mkdir("sleep", gp_privdata->bt_dir);
	if (gp_privdata->sleep_dir == NULL) {
		printk(KERN_ERR "Unable to create /proc/bluetooth/sleep directory\n");
		retval = -EFAULT;
		goto err6;
	}

	if (!proc_create_data("btenable", 0, gp_privdata->bt_dir,
		&proc_btenable_fops, pdata)) {
        printk(KERN_ERR "Unable to create /proc/bluetooth/btenable entry\n");
        retval = -EFAULT;
		goto err7;
    }

	sz = ARRAY_SIZE(proc_entries);

	for (i = 0; i < sz; i++) {
		proc_entries[i].this_entry = proc_create_data(
			proc_entries[i].name,
			proc_entries[i].mode,
			gp_privdata->sleep_dir,
			proc_entries[i].fops, pdata);
	}

	/* init host wake tasklet */
	tasklet_init(&hostwake_task, bluesleep_hostwake_task,
			(unsigned long)pdata);

	/* Initialize timer */
	init_timer(&tx_timer);
	tx_timer.function = bluesleep_tx_timer_expire;

	/* trigger ext BT wake */
	gpio_set_value(gp_privdata->pdata->bt_dev_wake, BT_WAKE_ASSERT);
	set_bit(BT_DEV_WAKE, &gp_privdata->state);

	printk(KERN_INFO "%s probe successful", __func__);
	return 0;

err7:
	remove_proc_entry("sleep", gp_privdata->bt_dir);
err6:
	remove_proc_entry("bluetooth", 0);
err5:
	device_remove_file(&pdev->dev, &dev_attr_btenable);
err4:
	devm_free_irq(&pdev->dev, gp_privdata->host_wake_irq, NULL);
err3:
	gpio_free(pdata->bt_host_wake);
err2:
	gpio_free(pdata->bt_dev_wake);
err1:
	gpio_free(pdata->bt_rst);
	kfree(gp_privdata);

	return retval;
}

static int bt_pwrctrl_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_btenable);
	remove_proc_entry("asleep", gp_privdata->sleep_dir);
	remove_proc_entry("proto", gp_privdata->sleep_dir);
	remove_proc_entry("btwake", gp_privdata->sleep_dir);
	remove_proc_entry("hostwake", gp_privdata->sleep_dir);
	remove_proc_entry("btenable", gp_privdata->bt_dir);
	remove_proc_entry("sleep", gp_privdata->bt_dir);
	remove_proc_entry("bluetooth", NULL);
	kfree(gp_privdata);
	gp_privdata = NULL;

	return 0;
}

static const struct of_device_id bt_pwr_ctrl_ids[] = {
	{ .compatible = "amzn,bt_pwr_ctrl" },
	{ /* sentinel */ }
};

static struct platform_driver bt_pwrctrl_drv = {
	.driver = {
			.name = "bt_pwr_ctrl",
			.of_match_table = bt_pwr_ctrl_ids,
		   },
	.probe = bt_pwrctrl_probe,
	.remove = bt_pwrctrl_remove,
};

static __init int bt_pwrctrl_init(void)
{
	printk(KERN_INFO "%s called..", __func__);
	return platform_driver_register(&bt_pwrctrl_drv);
}

static void __exit bt_pwrctrl_exit(void)
{
	printk(KERN_INFO "%s called..", __func__);
	platform_driver_unregister(&bt_pwrctrl_drv);
}

module_init(bt_pwrctrl_init);
module_exit(bt_pwrctrl_exit);
MODULE_AUTHOR("Sandeep Marathe msandeep@lab126.com");
MODULE_DESCRIPTION("Amazon Bluetooth Power control driver");
MODULE_LICENSE("GPL v2");
