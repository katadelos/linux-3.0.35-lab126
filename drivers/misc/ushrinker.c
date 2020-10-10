/*
 * Copyright 2013 Amazon Technologies, Inc.
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
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/ratelimit.h>

static int shrinkobjs = 100;
static int shrinkseeks = 10;

static struct miscdevice ushrink_dev =  {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ushrink"
};

static struct ratelimit_state event_limit;

static int ushrink (struct shrinker * sh, struct shrink_control *sc) {
	if (sc->nr_to_scan) {
		if (__ratelimit(&event_limit)) {
			kobject_uevent(&(ushrink_dev.this_device->kobj), KOBJ_CHANGE);
		}
	}

	return shrinkobjs;
}

static struct shrinker ushrinker = {
	.shrink = ushrink,
};

static ssize_t shrinkobjs_show(struct device *dev, struct device_attribute *attr,
                                   char *buf)
{
	return sprintf(buf, "%d\n", shrinkobjs);
}

static ssize_t shrinkobjs_store(struct device *dev, struct device_attribute *attr,
                                    const char *buf, size_t size)
{
	int value = 0;
	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "couldn't parse value\n");
		return -EINVAL;
	}

	shrinkobjs = value;

	return size;
}
static DEVICE_ATTR(shrinkobjs, 0666, shrinkobjs_show, shrinkobjs_store);


static ssize_t shrinkseeks_show(struct device *dev, struct device_attribute *attr,
                                   char *buf)
{
	return sprintf(buf, "%d\n", ushrinker.seeks);
}

static ssize_t shrinkseeks_store(struct device *dev, struct device_attribute *attr,
                                    const char *buf, size_t size)
{
	int value = 0;
	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "couldn't parse value\n");
		return -EINVAL;
	}

	ushrinker.seeks = value;

	return size;
}
static DEVICE_ATTR(shrinkseeks, 0666, shrinkseeks_show, shrinkseeks_store);

static int ushrink_init(void)
{
	int ret;

	ratelimit_state_init(&event_limit, 10 * HZ, 1);
	event_limit.suppress_missed_print = 1;

	if ((ret = misc_register(&ushrink_dev))) {
		printk(KERN_ERR "ushrink unable to register device\n");
		return ret;
	}

	ret = device_create_file(ushrink_dev.this_device, &dev_attr_shrinkobjs);
	if (ret) {
		printk(KERN_ERR "ushrink could not register sysfs entry\n");
		goto err_sysfs_objs;
	}
	ret = device_create_file(ushrink_dev.this_device, &dev_attr_shrinkseeks);
	if (ret) {
		printk(KERN_ERR "ushrink could not register sysfs entry\n");
		goto err_sysfs_seeks;
	}

	ushrinker.seeks = shrinkseeks;
	register_shrinker(&ushrinker);

	return ret;

err_sysfs_seeks:
	device_remove_file(ushrink_dev.this_device, &dev_attr_shrinkobjs);
err_sysfs_objs:
	misc_deregister(&ushrink_dev);
	return ret;
}

static void ushrink_exit(void)
{
	unregister_shrinker(&ushrinker);
	device_remove_file(ushrink_dev.this_device, &dev_attr_shrinkobjs);
	device_remove_file(ushrink_dev.this_device, &dev_attr_shrinkseeks);
	misc_deregister(&ushrink_dev);
}

module_param(shrinkseeks, int, 0);
module_param(shrinkobjs, int, 0);
module_init(ushrink_init);
module_exit(ushrink_exit);

MODULE_AUTHOR("Mitchell Skiba <mskiba@lab126.com>");
MODULE_DESCRIPTION("Shrinker U-Event Stub");
MODULE_ALIAS("ushrink");
MODULE_LICENSE("GPL v2");
