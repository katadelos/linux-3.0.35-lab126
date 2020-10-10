/*
 * Copyright 2005-2011 Freescale Semiconductor, Inc. All rights reserved.
 * Copyright (C) 2012 Amazon.com, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * Modified from @file pmic-dev.c for Max77696 PMIC
 * @brief This provides /dev interface to the user program. They make it
 * possible to have user-space programs use or control PMIC. Mainly its
 * useful for notifying PMIC events to user-space programs.
 */

/*
 * Includes
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/circ_buf.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/pmic_external.h>
#include <linux/mfd/max77696.h>
#include <linux/mfd/max77696-events.h>
#include <asm/uaccess.h>

#define PMIC_NAME	"pmic-max77696"
#define CIRC_BUF_MAX MAX77696_MAX_EVENTS

static int pmic_major;
static struct class *pmic_class;
static struct fasync_struct *pmic_dev_queue;

static DEFINE_MUTEX(event_mutex);
static struct circ_buf pmic_events;

static void callbackfn(void *event)
{
	printk(KERN_INFO "\n\n DETECTED PMIC EVENT : %d\n\n",
	       (unsigned int)event);
}

static void user_notify_callback(void *event)
{
	mutex_lock(&event_mutex);
	if (CIRC_SPACE(pmic_events.head, pmic_events.tail, CIRC_BUF_MAX)) {
		pmic_events.buf[pmic_events.head] = (int)event;
		pmic_events.head = (pmic_events.head + 1) & (CIRC_BUF_MAX - 1);
	} else {
		pr_info("Failed to notify event to the user\n");
	}
	mutex_unlock(&event_mutex);

	kill_fasync(&pmic_dev_queue, SIGIO, POLL_IN);
}

/*!
 * This function implements IOCTL controls on a PMIC device.
 *
 * @param        inode       pointer on the node
 * @param        file        pointer on the file
 * @param        cmd         the command
 * @param        arg         the parameter
 * @return       This function returns 0 if successful.
 */
static int pmic_dev_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	register_info reg_info;
	pmic_event_callback_t event_sub;
	u16 event;
	int ret = 0;

	if (_IOC_TYPE(cmd) != 'P')
		return -ENOTTY;
	
	switch (cmd) {
	case PMIC_SUBSCRIBE:
		if (get_user(event, (int __user *)arg)) {
			return -EFAULT;
		}
		event_sub.func = callbackfn;
		event_sub.param = (void *)event;
		ret = pmic_event_subscribe(event, &event_sub);
		pr_debug("subscribe done\n");
		break;

	case PMIC_UNSUBSCRIBE:
		if (get_user(event, (int __user *)arg)) {
			return -EFAULT;
		}
		event_sub.func = callbackfn;
		event_sub.param = (void *)event;
		ret = pmic_event_unsubscribe(event, &event_sub);
		pr_debug("unsubscribe done\n");
		break;

	case PMIC_NOTIFY_USER:
		if (get_user(event, (int __user *)arg)) {
			return -EFAULT;
		}
		event_sub.func = user_notify_callback;
		event_sub.param = (void *)event;
		ret = pmic_event_subscribe(event, &event_sub);
		break;

	case PMIC_GET_NOTIFY:
		mutex_lock(&event_mutex);
		if (CIRC_CNT(pmic_events.head, pmic_events.tail, CIRC_BUF_MAX)) {
			event = (int)pmic_events.buf[pmic_events.tail];
			pmic_events.tail = (pmic_events.tail + 1) & (CIRC_BUF_MAX - 1);
		} else {
			pr_info("No valid notified event\n");
		}
		mutex_unlock(&event_mutex);

		if (put_user(event, (int __user *)arg)) {
			return -EFAULT;
		}
		break;

	default:
		printk(KERN_ERR "%d unsupported ioctl command\n", (int)cmd);
		return -EINVAL;
	}

	return ret;
}

/*!
 * This function implements the open method on a PMIC device.
 *
 * @param        inode       pointer on the node
 * @param        file        pointer on the file
 * @return       This function returns 0.
 */
static int pmic_dev_open(struct inode *inode, struct file *file)
{
	pr_debug("open\n");
	return PMIC_SUCCESS;
}

/*!
 * This function implements the release method on a PMIC device.
 *
 * @param        inode       pointer on the node
 * @param        file        pointer on the file
 *
 * @return       This function returns 0.
 */
static int pmic_dev_free(struct inode *inode, struct file *file)
{
	pr_debug("free\n");
	return PMIC_SUCCESS;
}

static int pmic_dev_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &pmic_dev_queue);
}

/*!
 * This structure defines file operations for a PMIC device.
 */
static struct file_operations pmic_fops = {
	/*!
	 * the owner
	 */
	.owner = THIS_MODULE,
	/*!
	 * the ioctl operation
	 */
	.unlocked_ioctl = pmic_dev_ioctl,
	/*!
	 * the open operation
	 */
	.open = pmic_dev_open,
	/*!
	 * the release operation
	 */
	.release = pmic_dev_free,
	/*!
	 * the release operation
	 */
	.fasync = pmic_dev_fasync,
};

/*!
 * This function implements the init function of the PMIC char device.
 * This function is called when the module is loaded. It registers
 * the character device for PMIC to be used by user-space programs.
 *
 * @return       This function returns 0.
 */
static int __init pmic_dev_init(void)
{
	int ret = 0;
	struct device *pmic_device;

	pmic_major = register_chrdev(0, PMIC_NAME, &pmic_fops);
	if (pmic_major < 0) {
		printk(KERN_ERR "unable to get a major for pmic\n");
		return pmic_major;
	}

	pmic_class = class_create(THIS_MODULE, PMIC_NAME);
	if (IS_ERR(pmic_class)) {
		printk(KERN_ERR "Error creating pmic class.\n");
		ret = PMIC_ERROR;
		goto err;
	}

	pmic_device = device_create(pmic_class, NULL, MKDEV(pmic_major, 0), NULL,
				    PMIC_NAME);
	if (IS_ERR(pmic_device)) {
		printk(KERN_ERR "Error creating pmic class device.\n");
		ret = PMIC_ERROR;
		goto err1;
	}

	pmic_events.buf = kmalloc(CIRC_BUF_MAX * sizeof(char), GFP_KERNEL);
	if (NULL == pmic_events.buf) {
		ret = -ENOMEM;
		goto err2;
	}
	pmic_events.head = pmic_events.tail = 0;

	printk(KERN_INFO "PMIC Character device: successfully loaded\n");
	return ret;
      err2:
	device_destroy(pmic_class, MKDEV(pmic_major, 0));
      err1:
	class_destroy(pmic_class);
      err:
	unregister_chrdev(pmic_major, PMIC_NAME);
	return ret;

}

/*!
 * This function implements the exit function of the PMIC character device.
 * This function is called when the module is unloaded. It unregisters
 * the PMIC character device.
 *
 */
static void __exit pmic_dev_exit(void)
{
	device_destroy(pmic_class, MKDEV(pmic_major, 0));
	class_destroy(pmic_class);

	unregister_chrdev(pmic_major, PMIC_NAME);

	printk(KERN_INFO "PMIC Character device: successfully unloaded\n");
}

/*
 * Module entry points
 */

module_init(pmic_dev_init);
module_exit(pmic_dev_exit);

MODULE_DESCRIPTION("PMIC Protocol /dev entries driver");
MODULE_LICENSE("GPL");
