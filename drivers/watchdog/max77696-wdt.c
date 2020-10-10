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
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
//#warning "KERNEL 3.1.0 earilier"
#include "max77696-wdd.h"
#endif /* LINUX_VERSION < 3.1.0 */

#include <linux/watchdog.h>
#include <linux/mfd/max77696.h>

#define DRIVER_DESC    "MAX77696 System Watchdog Timer Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_WDT_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#define WDT_TIMEOUT_MIN         2
#define WDT_TIMEOUT_MAX         128
#define WDT_TIMEOUT_DEF         128 /* in seconds */

#define WDT_CARDRESET_ERCFLAGS  \
        (MAX77696_ERCFLAG0_WDPMIC_FRSTRT | MAX77696_ERCFLAG0_WDPMIC_FSHDN)

struct max77696_wdt {
    struct max77696_chip   *chip;
    struct device          *dev;
    struct kobject         *kobj;
    struct mutex            lock;

    struct watchdog_device  wdd;
    unsigned long           ping_interval;
    struct delayed_work     ping_work;
};

#define __to_max77696_wdt(wdd_ptr) \
        container_of(wdd_ptr, struct max77696_wdt, wdd)
#define __ping_work_to_max77696_wdt(ping_work_ptr) \
        container_of(ping_work_ptr, struct max77696_wdt, ping_work.work)

#define __lock(me)                 mutex_lock(&((me)->lock))
#define __unlock(me)               mutex_unlock(&((me)->lock))

static __always_inline int max77696_wdt_disable (struct max77696_wdt *me)
{
    int rc;

    rc = max77696_topsys_enable_wdt(0);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to disable watchdog timer [%d]\n", rc);
        return rc;
    }

    return 0;
}

static __inline int max77696_wdt_enable (struct max77696_wdt *me,
    unsigned int timeout)
{
    int rc;

    rc = max77696_topsys_enable_wdt(0);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to disable watchdog timer [%d]\n", rc);
        return rc;
    }

    rc = max77696_topsys_set_wdt_period(timeout);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to set watchdog timer period [%d]\n", rc);
        return rc;
    }

    rc = max77696_topsys_enable_wdt(1);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to enable watchdog timer [%d]\n", rc);
        return rc;
    }

    return 0;
}

static __always_inline int max77696_wdt_clear (struct max77696_wdt *me)
{
    int rc;

    rc = max77696_topsys_clear_wdt();
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to clear watchdog timer [%d]\n", rc);
        return rc;
    }

    dev_dbg(me->dev, "watchdog timer cleared\n");
    return 0;
}

#define max77696_wdt_auto_ping_stop(me) \
        cancel_delayed_work_sync(&((me)->ping_work))

#define max77696_wdt_auto_ping_start(me) \
        if (likely((me)->ping_interval > 0)) {\
            if (likely(!delayed_work_pending(&((me)->ping_work)))) {\
                schedule_delayed_work(&((me)->ping_work), (me)->ping_interval);\
            }\
        }

static void max77696_wdt_ping_work (struct work_struct *work)
{
    struct max77696_wdt *me = __ping_work_to_max77696_wdt(work);

    __lock(me);

    max77696_wdt_clear(me);
    max77696_wdt_auto_ping_start(me);

    __unlock(me);
    return;
}

static int max77696_wdt_start (struct watchdog_device *wdd)
{
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc;

    __lock(me);

    rc = max77696_wdt_enable(me, wdd->timeout);
    if (unlikely(rc)) {
        goto out;
    }

    max77696_wdt_auto_ping_start(me);

out:
    __unlock(me);
    return rc;
}

static int max77696_wdt_stop (struct watchdog_device *wdd)
{
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc;

    __lock(me);

    rc = max77696_wdt_disable(me);
    if (unlikely(rc)) {
        goto out;
    }

    max77696_wdt_auto_ping_stop(me);

out:
    __unlock(me);
    return rc;
}

static int max77696_wdt_ping (struct watchdog_device *wdd)
{
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc;

    __lock(me);
    rc = max77696_wdt_clear(me);
    __unlock(me);

    return rc;
}

static int max77696_wdt_set_timeout (struct watchdog_device *wdd,
    unsigned int timeout)
{
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc = 0;

    __lock(me);

    if (unlikely(!test_bit(WDOG_ACTIVE, &(wdd->status)))) {
        /* do nothing if timer not started */
        goto out;
    }

    rc = max77696_wdt_enable(me, timeout);

out:
    __unlock(me);
    return rc;
}

static const struct watchdog_ops max77696_wdt_ops = {
    .owner        = THIS_MODULE,
    .start        = max77696_wdt_start,
    .stop         = max77696_wdt_stop,
    .ping         = max77696_wdt_ping,
    .set_timeout  = max77696_wdt_set_timeout,
};

static const struct watchdog_info max77696_wdt_ident = {
    .identity    = DRIVER_NAME,
    .options     = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
};

static ssize_t max77696_wdt_status_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc;

    __lock(me);
    rc = (int)snprintf(buf, PAGE_SIZE, "boot status %08X\nstatus      %08X\n",
        me->wdd.bootstatus, (unsigned int)me->wdd.status);
    __unlock(me);

    return (ssize_t)rc;
}

static DEVICE_ATTR(wdt_status, S_IRUGO, max77696_wdt_status_show, NULL);

static ssize_t max77696_wdt_state_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc;

    __lock(me);
    rc = (int)snprintf(buf, PAGE_SIZE, "%s\n",
        test_bit(WDOG_ACTIVE, &(me->wdd.status))? "enabled" : "disabled");
    __unlock(me);

    return (ssize_t)rc;
}

static ssize_t max77696_wdt_state_store (struct device *dev,
    struct device_attribute *devattr, const char *buf, size_t count)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc;

    __lock(me);

    if (simple_strtoul(buf, NULL, 10)) {
        if (!test_bit(WDOG_ACTIVE, &(me->wdd.status))) {
            rc = max77696_wdt_enable(me, me->wdd.timeout);
            if (unlikely(rc)) {
                goto out;
            }
            set_bit(WDOG_ACTIVE, &(me->wdd.status));
        }
    } else {
        if (test_bit(WDOG_ACTIVE, &(me->wdd.status))) {
            rc = max77696_wdt_disable(me);
            if (unlikely(rc)) {
                goto out;
            }
            clear_bit(WDOG_ACTIVE, &(me->wdd.status));
        }
    }

out:
    __unlock(me);
    return (ssize_t)count;
}

static DEVICE_ATTR(wdt_state, S_IWUSR | S_IRUGO,
    max77696_wdt_state_show, max77696_wdt_state_store);

static ssize_t max77696_wdt_ping_interval_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc;

    __lock(me);
    rc = (int)snprintf(buf, PAGE_SIZE, "%usec\n",
        (unsigned int)DIV_ROUND_UP(jiffies_to_msecs(me->ping_interval), 1000));
    __unlock(me);

    return (ssize_t)rc;
}

static ssize_t max77696_wdt_ping_interval_store (struct device *dev,
    struct device_attribute *devattr, const char *buf, size_t count)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    unsigned int val;

    __lock(me);

    max77696_wdt_auto_ping_stop(me);

    val               = (unsigned int)simple_strtoul(buf, NULL, 10);
    me->ping_interval = msecs_to_jiffies(val * 1000);

    max77696_wdt_clear(me);
    max77696_wdt_auto_ping_start(me);

    __unlock(me);
    return (ssize_t)count;
}

static DEVICE_ATTR(wdt_ping_interval, S_IWUSR | S_IRUGO,
    max77696_wdt_ping_interval_show, max77696_wdt_ping_interval_store);

static ssize_t max77696_wdt_timeout_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    int rc;

    __lock(me);
    rc = (int)snprintf(buf, PAGE_SIZE, "%usec [%usec ... %usec]\n",
        me->wdd.timeout, me->wdd.min_timeout, me->wdd.max_timeout);
    __unlock(me);

    return (ssize_t)rc;
}

static ssize_t max77696_wdt_timeout_store (struct device *dev,
    struct device_attribute *devattr, const char *buf, size_t count)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);
    struct max77696_wdt *me = __to_max77696_wdt(wdd);
    unsigned int val;

    __lock(me);

    val = (unsigned int)simple_strtoul(buf, NULL, 10);

	if (likely(me->wdd.max_timeout)) {
	    if (unlikely(val < me->wdd.min_timeout || val > me->wdd.max_timeout)) {
	        dev_err(me->dev, "out of range\n");
			goto out;
        }
    }

    me->wdd.timeout = val;

    if (unlikely(!test_bit(WDOG_ACTIVE, &(me->wdd.status)))) {
        /* do nothing if timer not started */
        goto out;
    }

    max77696_wdt_enable(me, me->wdd.timeout);

out:
    __unlock(me);
    return (ssize_t)count;
}

static DEVICE_ATTR(wdt_timeout, S_IWUSR | S_IRUGO,
    max77696_wdt_timeout_show, max77696_wdt_timeout_store);

static struct attribute *max77696_wdt_attr[] = {
    &dev_attr_wdt_status.attr,
    &dev_attr_wdt_state.attr,
    &dev_attr_wdt_ping_interval.attr,
    &dev_attr_wdt_timeout.attr,
    NULL
};

static const struct attribute_group max77696_wdt_attr_group = {
    .attrs = max77696_wdt_attr,
};

static int __devinit max77696_wdt_probe (struct platform_device *pdev)
{
    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_watchdog_platform_data *pdata = pdev->dev.platform_data;
    struct max77696_wdt *me;
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

    platform_set_drvdata(pdev, &(me->wdd));

    me->chip = chip;
//  me->i2c  = __get_i2c(chip);
    me->dev  = &(pdev->dev);
    me->kobj = &(pdev->dev.kobj);
    mutex_init(&(me->lock));

    me->wdd.info        = &max77696_wdt_ident;
    me->wdd.ops         = &max77696_wdt_ops;
    me->wdd.min_timeout = WDT_TIMEOUT_MIN;
    me->wdd.max_timeout = WDT_TIMEOUT_MAX;
    me->wdd.timeout     =
        (pdata->timeout_sec? pdata->timeout_sec : WDT_TIMEOUT_DEF);

    me->ping_interval   = msecs_to_jiffies(pdata->ping_interval_sec * 1000);
    INIT_DELAYED_WORK(&(me->ping_work), max77696_wdt_ping_work);

    /* Translate MAX77696 recorded events to the boot status of watchdog core */
    rc = max77696_irq_test_ercflag(WDT_CARDRESET_ERCFLAGS);
    if (rc) {
        me->wdd.bootstatus |= WDIOF_CARDRESET;
    }

    rc = watchdog_register_device(&(me->wdd));
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to register watchdog device [%d]\n", rc);
        goto out_err_reg_wdd;
    }

    rc = sysfs_create_group(me->kobj, &max77696_wdt_attr_group);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to create attribute group [%d]\n", rc);
        goto out_err_create_sysfs;
    }

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(wdt, chip);
    return 0;

out_err_create_sysfs:
    watchdog_unregister_device(&(me->wdd));
out_err_reg_wdd:
    platform_set_drvdata(pdev, NULL);
    kfree(me);
    return rc;
}

static __devexit int max77696_wdt_remove (struct platform_device *pdev)
{
    struct watchdog_device *wdd = platform_get_drvdata(pdev);
    struct max77696_wdt *me = __to_max77696_wdt(wdd);

    sysfs_remove_group(me->kobj, &max77696_wdt_attr_group);
    watchdog_unregister_device(wdd);
    mutex_destroy(&(me->lock));

    platform_set_drvdata(pdev, NULL);
    kfree(me);

    return 0;
}

static void max77696_wdt_shutdown (struct platform_device *pdev)
{
    struct watchdog_device *wdd = platform_get_drvdata(pdev);

    max77696_wdt_stop(wdd);
}

#ifdef CONFIG_PM_SLEEP
static int max77696_wdt_suspend (struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);

    if (likely(test_bit(WDOG_ACTIVE, &(wdd->status)))) {
        max77696_wdt_stop(wdd);
    }

    return 0;
}

static int max77696_wdt_resume (struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct watchdog_device *wdd = platform_get_drvdata(pdev);

    if (likely(test_bit(WDOG_ACTIVE, &(wdd->status)))) {
        max77696_wdt_start(wdd);
    }

    return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(max77696_wdt_pm,
    max77696_wdt_suspend, max77696_wdt_resume);

static struct platform_driver max77696_wdt_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .driver.pm    = &max77696_wdt_pm,
    .probe        = max77696_wdt_probe,
    .remove       = __devexit_p(max77696_wdt_remove),
    .shutdown     = max77696_wdt_shutdown,
};

static __init int max77696_wdt_driver_init (void)
{
    return platform_driver_register(&max77696_wdt_driver);
}

static __exit void max77696_wdt_driver_exit (void)
{
    platform_driver_unregister(&max77696_wdt_driver);
}

module_init(max77696_wdt_driver_init);
module_exit(max77696_wdt_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

