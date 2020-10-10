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

#include <linux/gpio.h>
#include <linux/power_supply.h>
#include <linux/mfd/max77696.h>
#include <max77696_registers.h>

/* give my hands to FSL bsp ... */
#undef  gpio_to_irq
#define gpio_to_irq    __gpio_to_irq
#undef  gpio_get_value
#define gpio_get_value __gpio_get_value

#define DRIVER_DESC    "MAX77696 Energy Harvester Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_EH_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#ifdef CONFIG_FALCON
extern int in_falcon(void);
#endif

#ifdef VERBOSE
#define dev_noise(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_noise(args...) do { } while (0)
#endif /* VERBOSE */

#define CHGB_PSY_WORK_DELAY            (HZ/10)

/* CHGB Register Read/Write */
#define max77696_eh_reg_read(me, reg, val_ptr) \
        max77696_read((me)->i2c, CHGB_REG(reg), val_ptr)
#define max77696_eh_reg_write(me, reg, val) \
        max77696_write((me)->i2c, CHGB_REG(reg), val)
#define max77696_eh_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, CHGB_REG(reg), dst, len)
#define max77696_eh_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, CHGB_REG(reg), src, len)
#define max77696_eh_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, CHGB_REG(reg), mask, val_ptr)
#define max77696_eh_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, CHGB_REG(reg), mask, val)

/* CHGB Register Single Bit Ops */
#define max77696_eh_reg_get_bit(me, reg, bit, val_ptr) \
        ({\
            int __rc = max77696_eh_reg_read_masked(me, reg,\
                CHGB_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = CHGB_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_eh_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_eh_reg_write_masked(me, reg,\
                CHGB_REG_BITMASK(reg, bit), CHGB_REG_BITSET(reg, bit, val));\
        })

struct max77696_eh {
    struct max77696_chip    *chip;
    struct max77696_i2c     *i2c;
    struct device           *dev;
    struct kobject          *kobj;

    void                     (*charger_notify) (struct power_supply*, bool, bool);
    void                     (*accessory_notify) (struct power_supply *, bool);

    struct power_supply      psy;
    struct delayed_work      psy_work;
    unsigned int             irq;
    u8                       irq_unmask;
    u8                       irq_status;
    u8                       interrupted;
    bool                     pending_psy_notification;
    bool                     chg_online, chg_enable;
    unsigned long            det_interval;
    struct delayed_work      det_work;
    bool                     pending_acc_notification;
    bool                     acc_present;
    unsigned int             acc_det_gpio;
    int                      acc_det_gpio_assert;
    unsigned long            acc_det_debounce;
    struct delayed_work      acc_work;
    struct mutex             lock;
};

#define __psy_to_max77696_eh(psy_ptr) \
        container_of(psy_ptr, struct max77696_eh, psy)
#define __psy_work_to_max77696_eh(psy_work_ptr) \
        container_of(psy_work_ptr, struct max77696_eh, psy_work.work)
#define __det_work_to_max77696_eh(det_work_ptr) \
        container_of(det_work_ptr, struct max77696_eh, det_work.work)
#define __acc_work_to_max77696_eh(acc_work_ptr) \
        container_of(acc_work_ptr, struct max77696_eh, acc_work.work)

#define __get_i2c(chip)                 (&((chip)->rtc_i2c))
#define __lock(me)                      mutex_lock(&((me)->lock))
#define __unlock(me)                    mutex_unlock(&((me)->lock))

#define __search_index_of_approxi(_table, _val) \
        ({\
            int __i;\
            typeof(_val) __diff = (typeof(_val))(abs(_table[0] - _val));\
            for (__i = 1; __i < ARRAY_SIZE(_table); __i++) {\
                typeof(_val) __t = (typeof(_val))(abs(_table[__i] - _val));\
                if (__t > __diff) break;\
                __diff = __t;\
            }\
            (__i - 1);\
        })

#define max77696_eh_write_irq_mask(me) \
        do {\
            int _rc = max77696_eh_reg_write(me,\
                CHGINB_INTM, (me)->irq_unmask);\
            if (unlikely(_rc)) {\
                dev_err((me)->dev, "CHGINB_INTM write error [%d]\n", _rc);\
            }\
        } while (0)

static __inline void max77696_eh_enable_irq (struct max77696_eh* me,
    u8 irq_bits, bool forced)
{
    if (unlikely(!forced && (me->irq_unmask & irq_bits) == irq_bits)) {
        /* already unmasked */
        return;
    }

    /* set enabled flag */
    me->irq_unmask |= irq_bits;
    max77696_eh_write_irq_mask(me);
}

static __inline void max77696_eh_disable_irq (struct max77696_eh* me,
    u8 irq_bits, bool forced)
{
    if (unlikely(!forced && (me->irq_unmask & irq_bits) == 0)) {
        /* already masked */
        return;
    }

    /* clear enabled flag */
    me->irq_unmask &= ~irq_bits;
    max77696_eh_write_irq_mask(me);
}

/* Enable/Disable charger */

static int max77696_eh_chg_en_set (struct max77696_eh* me, int enable)
{
    int rc;

    rc = max77696_eh_reg_set_bit(me, CHGINB_CTL, BCHG_ENB, (u8)(!enable));
    if (unlikely(rc)) {
        dev_err(me->dev, "CHGINB_CTL write error [%d]\n", rc);
        goto out;
    }

out:
    return rc;
}

static int max77696_eh_chg_en_get (struct max77696_eh* me, int *enable)
{
    int rc;
    u8 chg_enb;

    rc = max77696_eh_reg_get_bit(me, CHGINB_CTL, BCHG_ENB, &chg_enb);
    if (unlikely(rc)) {
        dev_err(me->dev, "CHGINB_CTL read error [%d]\n", rc);
        goto out;
    }

    *enable = (int)(!chg_enb);

out:
    return rc;
}

/* Set/read constant current level */

static int max77696_eh_mbcichfc_set (struct max77696_eh* me,
    bool pqsel, int mA)
{
    int rc;
    u8 mbcichfc;

    if (mA > 900) {
        mbcichfc = 0xF;
    } else if (mA <= 200) {
        mbcichfc = 0x0;
    } else {
        mbcichfc = (u8)DIV_ROUND_UP(mA - 200, 50);
    }

    mbcichfc |= ((!pqsel) << 4);

    rc = max77696_eh_reg_set_bit(me, MBCCTRL4, MBCICHFC, mbcichfc);
    if (unlikely(rc)) {
        dev_err(me->dev, "MBCCTRL4 write error [%d]\n", rc);
        goto out;
    }

out:
    return rc;
}

static int max77696_eh_mbcichfc_get (struct max77696_eh* me,
    bool *pqsel, int *mA)
{
    int rc;
    u8 mbcichfc;

    rc = max77696_eh_reg_get_bit(me, MBCCTRL4, MBCICHFC, &mbcichfc);
    if (unlikely(rc)) {
        dev_err(me->dev, "MBCCTRL4 read error [%d]\n", rc);
        goto out;
    }

    *pqsel = (!(mbcichfc & 0x10));
    *mA    = (int)(mbcichfc & 0xF) * 50 + 200;

out:
    return rc;
}

/* Set/read constant voltage level */

static int max77696_eh_mbccv_set (struct max77696_eh* me, int mV)
{
    int rc;
    u8 mbccv;

    if (mV > 4280) {
        mbccv = 0xF;
    } else if (mV <= 4000) {
        mbccv = 0x0;
    } else {
        mbccv = (u8)DIV_ROUND_UP(mV - 4000, 20);
    }

    rc = max77696_eh_reg_set_bit(me, MBCCTRL3, MBCCV, mbccv);
    if (unlikely(rc)) {
        dev_err(me->dev, "MBCCTRL3 write error [%d]\n", rc);
        goto out;
    }

out:
    return rc;
}

static int max77696_eh_mbccv_get (struct max77696_eh* me, int *mV)
{
    int rc;
    u8 mbccv;

    rc = max77696_eh_reg_get_bit(me, MBCCTRL3, MBCCV, &mbccv);
    if (unlikely(rc)) {
        dev_err(me->dev, "MBCCTRL3 read error [%d]\n", rc);
        goto out;
    }

    *mV = ((mbccv >= 0xF)? 4350 : ((int)mbccv * 20 + 4000));

out:
    return rc;
}

/* Enable/Disable accessory */

static int max77696_eh_acc_en_set (struct max77696_eh* me, int enable)
{
    int rc;

    rc = max77696_eh_reg_set_bit(me, CHGINB_CTL, ACC_EN, (u8)(!!enable));
    if (unlikely(rc)) {
        dev_err(me->dev, "CHGINB_CTL write error [%d]\n", rc);
        goto out;
    }

out:
    return rc;
}

static int max77696_eh_acc_en_get (struct max77696_eh* me, int *enable)
{
    int rc;
    u8 acc_en;

    rc = max77696_eh_reg_get_bit(me, CHGINB_CTL, ACC_EN, &acc_en);
    if (unlikely(rc)) {
        dev_err(me->dev, "CHGINB_CTL read error [%d]\n", rc);
        goto out;
    }

    *enable = (int)(!!acc_en);

out:
    return rc;
}

/* Set/Read accessory current */

static int max77696_eh_acc_ilm_set (struct max77696_eh* me, int mA)
{
    int rc;

    rc = max77696_eh_reg_set_bit(me, CHGINB_CTL, ACC_ILM, (u8)(mA > 200));
    if (unlikely(rc)) {
        dev_err(me->dev, "CHGINB_CTL write error [%d]\n", rc);
        goto out;
    }

out:
    return rc;
}

static int max77696_eh_acc_ilm_get (struct max77696_eh* me, int *mA)
{
    int rc;
    u8 acc_ilm;

    rc = max77696_eh_reg_get_bit(me, CHGINB_CTL, ACC_ILM, &acc_ilm);
    if (unlikely(rc)) {
        dev_err(me->dev, "CHGINB_CTL read error [%d]\n", rc);
        goto out;
    }

    *mA = (int)(acc_ilm? 650 : 200);

out:
    return rc;
}

static int max77696_eh_mode_set (struct max77696_eh *me, int mode)
{
    int chg_en, acc_en, rc;

    chg_en = !!(mode & 0x1);
    acc_en = !!(mode & 0x2);

    rc = max77696_eh_chg_en_set(me, chg_en);
    if (unlikely(rc)) {
        goto out;
    }

    rc = max77696_eh_acc_en_set(me, acc_en);
    if (unlikely(rc)) {
        goto out;
    }

out:
    return rc;
}

static int max77696_eh_mode_get (struct max77696_eh *me, int *mode)
{
    int chg_en, acc_en, rc;

    rc = max77696_eh_chg_en_get(me, &chg_en);
    if (unlikely(rc)) {
        goto out;
    }

    rc = max77696_eh_acc_en_get(me, &acc_en);
    if (unlikely(rc)) {
        goto out;
    }

    *mode = (((!!acc_en) << 1) | ((!!chg_en) << 0));

out:
    return rc;
}

#define max77696_eh_det_stop(me) \
        cancel_delayed_work_sync(&((me)->det_work))

#define max77696_eh_det_start(me) \
        if (likely((me)->det_interval > 0)) {\
            if (likely(!delayed_work_pending(&((me)->det_work)))) {\
                schedule_delayed_work(&((me)->det_work), (me)->det_interval);\
            }\
        }

static bool max77696_eh_update_chg_state (struct max77696_eh* me,
    bool notify)
{
    bool online, enable, updated;
    int rc;

    /* read CHGINB_STAT as an interrupt status */
    rc = max77696_eh_reg_read(me, CHGINB_STAT, &(me->irq_status));
    if (unlikely(rc)) {
        dev_err(me->dev, "CHGINB_STAT read error [%d]\n", rc);
        return 0;
    }

    online = (!!CHGB_REG_BITGET(CHGINB_STAT, BCHGPOK, me->irq_status));
    enable = (!!CHGB_REG_BITGET(CHGINB_STAT, BCHGON,  me->irq_status));
    enable = (online && enable);

    /* Detecting only removal of charging source */
    if (likely(online)) {
        max77696_eh_det_start(me);
    }

    updated = (bool)(me->chg_online != online || me->chg_enable != enable);

    if (unlikely(!updated && !me->pending_psy_notification)) {
        return 0;
    }

    me->pending_psy_notification = 1;

    me->chg_online = online;
    me->chg_enable = enable;
    dev_dbg(me->dev, "CHG ONLINE %d ENABLE %d\n", me->chg_online, me->chg_enable);

    if (likely(notify && me->charger_notify)) {
        me->pending_psy_notification = 0;
        me->charger_notify(&(me->psy), me->chg_online, me->chg_enable);
    }

    return updated;
}

static bool max77696_eh_update_acc_state (struct max77696_eh* me,
    bool notify)
{
    bool present, updated;

    if (gpio_get_value(me->acc_det_gpio) == me->acc_det_gpio_assert) {
        present = 1;
    } else {
        present = 0;
    }

    updated = (bool)(me->acc_present != present);

    if (unlikely(!updated && !me->pending_acc_notification)) {
        return 0;
    }

    me->pending_acc_notification = 1;

    me->acc_present = present;
    dev_dbg(me->dev, "ACC PRESENT %d\n", me->acc_present);

    if (likely(notify && me->accessory_notify)) {
        me->pending_acc_notification = 0;
        me->accessory_notify(&(me->psy), me->acc_present);
    }

    return updated;
}

static void max77696_eh_det_work (struct work_struct *work)
{
    struct max77696_eh *me = __det_work_to_max77696_eh(work);

    __lock(me);
    max77696_eh_update_chg_state(me, 1);
    dev_noise(me->dev, "CHGINB_STAT %02X\n", me->irq_status);
    __unlock(me);
}

static void max77696_eh_psy_work (struct work_struct *work)
{
    struct max77696_eh *me = __psy_work_to_max77696_eh(work);

    max77696_eh_det_stop(me);

    __lock(me);
    max77696_eh_update_chg_state(me, 1);
    dev_dbg(me->dev, "CHGINB_STAT %02X\n", me->irq_status);
    __unlock(me);
}

static irqreturn_t max77696_eh_isr (int irq, void *data)
{
    struct max77696_eh *me = data;

    /* read INT register to clear bits ASAP */
    max77696_eh_reg_read(me, CHGINB_INT, &(me->interrupted));
    dev_dbg(me->dev, "CHGINB_INT %02X EN %02X\n",
        me->interrupted, me->irq_unmask);
#if defined(CONFIG_FALCON) && !defined(DEBUG)
    if(in_falcon()){
        printk(KERN_DEBUG "CHGINB_INT %02X EN %02X\n",
	        me->interrupted, me->irq_unmask);
	}
#endif
    me->interrupted &= me->irq_unmask;

    if (me->interrupted & CHGB_INT_BCHGPOK) {
        if (likely(!delayed_work_pending(&(me->psy_work)))) {
            schedule_delayed_work(&(me->psy_work), CHGB_PSY_WORK_DELAY);
        }
    }

    if (me->interrupted & CHGB_INT_ACCFLT) {
        /* TODO: put the customer's code here */
    }

    if (me->interrupted & CHGB_INT_ACCON) {
        /* TODO: put the customer's code here */
    }

    if (me->interrupted & CHGB_INT_ACCLD) {
        /* TODO: put the customer's code here */
    }

    return IRQ_HANDLED;
}

static void max77696_eh_acc_work (struct work_struct *work)
{
    struct max77696_eh *me = __acc_work_to_max77696_eh(work);

    __lock(me);
    max77696_eh_update_acc_state(me, 1);
    __unlock(me);
}

static irqreturn_t max77696_eh_acc_isr (int irq, void *data)
{
    struct max77696_eh *me = data;

    dev_dbg(me->dev, "accessory detection interrupt\n");

    if (likely(!delayed_work_pending(&(me->acc_work)))) {
        schedule_delayed_work(&(me->acc_work), me->acc_det_debounce);
    }

    return IRQ_HANDLED;
}

static int max77696_eh_get_property (struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
    struct max77696_eh *me = __psy_to_max77696_eh(psy);
    int rc = 0;

    __lock(me);

    switch (psp) {
    case POWER_SUPPLY_PROP_PRESENT:
    case POWER_SUPPLY_PROP_ONLINE:
        val->intval = me->chg_online;
        break;

    default:
        rc = -EINVAL;
        break;
    }

    __unlock(me);
    return rc;
}

static enum power_supply_property max77696_eh_psy_props[] = {
    POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

#define MAX77696_EH_ATTR(attr_name, ctrl_name, unit_str) \
static ssize_t max77696_eh_##ctrl_name##_show (struct device *dev,\
    struct device_attribute *devattr, char *buf)\
{\
    struct platform_device *pdev = to_platform_device(dev);\
    struct max77696_eh *me = platform_get_drvdata(pdev);\
    int val = 0, rc;\
    __lock(me);\
    rc = max77696_eh_##ctrl_name##_get(me, &val);\
    if (unlikely(rc)) {\
        goto out;\
    }\
    rc = (int)snprintf(buf, PAGE_SIZE, "%d"unit_str"\n", val);\
out:\
    __unlock(me);\
    return (ssize_t)rc;\
}\
static ssize_t max77696_eh_##ctrl_name##_store (struct device *dev,\
    struct device_attribute *devattr, const char *buf, size_t count)\
{\
    struct platform_device *pdev = to_platform_device(dev);\
    struct max77696_eh *me = platform_get_drvdata(pdev);\
    int val, rc;\
    __lock(me);\
    val = (int)simple_strtol(buf, NULL, 10);\
    rc = max77696_eh_##ctrl_name##_set(me, val);\
    if (unlikely(rc)) {\
        goto out;\
    }\
out:\
    __unlock(me);\
    return (ssize_t)count;\
}\
static DEVICE_ATTR(attr_name, S_IWUSR|S_IRUGO, max77696_eh_##ctrl_name##_show,\
    max77696_eh_##ctrl_name##_store)

MAX77696_EH_ATTR(chg_en,     chg_en,  ""  );
MAX77696_EH_ATTR(cv_param,   mbccv,   "mV");
MAX77696_EH_ATTR(acc_en,     acc_en,  ""  );
MAX77696_EH_ATTR(acc_ilimit, acc_ilm, "mA");

static ssize_t max77696_eh_mbcichfc_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct max77696_eh *me = platform_get_drvdata(pdev);
    bool pqsel;
    int mA, rc;

    __lock(me);

    rc = max77696_eh_mbcichfc_get(me, &pqsel, &mA);
    if (unlikely(rc)) {
        goto out;
    }

    rc = (int)snprintf(buf, PAGE_SIZE, "pq_mode %u, %dmA\n", pqsel, mA);

out:
    __unlock(me);
    return (ssize_t)rc;
}

static ssize_t max77696_eh_mbcichfc_store (struct device *dev,
    struct device_attribute *devattr, const char *buf, size_t count)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct max77696_eh *me = platform_get_drvdata(pdev);
    int opts[3], rc;

    __lock(me);

    /* format: pqsel, mA */
    get_options(buf, 3, opts);

    if (unlikely(opts[0] != 2)) {
        dev_err(me->dev, "missing parameter(s)\n");
        goto out;
    }

    rc = max77696_eh_mbcichfc_set(me, (bool)opts[1], opts[2]);
    if (unlikely(rc)) {
        goto out;
    }

out:
    __unlock(me);
    return (ssize_t)count;
}

static DEVICE_ATTR(cc_param, S_IWUSR|S_IRUGO, max77696_eh_mbcichfc_show,
    max77696_eh_mbcichfc_store);

static struct attribute *max77696_eh_attr[] = {
    &dev_attr_chg_en.attr,
    &dev_attr_acc_en.attr,
    &dev_attr_acc_ilimit.attr,
    &dev_attr_cc_param.attr,
    &dev_attr_cv_param.attr,
    NULL
};

static const struct attribute_group max77696_eh_attr_group = {
    .attrs = max77696_eh_attr,
};

static __devinit int max77696_eh_probe (struct platform_device *pdev)
{
    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_eh_platform_data *pdata = pdev->dev.platform_data;
    struct max77696_eh *me;
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

    me->charger_notify   = pdata->charger_notify;
    me->accessory_notify = pdata->accessory_notify;

    me->irq                 = chip->irq_base + MAX77696_ROOTINT_CHGB;
    me->det_interval        = msecs_to_jiffies(pdata->detect_interval_ms);
    dev_dbg(me->dev, "detection interval = %umsec\n",
        pdata->detect_interval_ms);
    me->acc_det_gpio        = pdata->acc_det_gpio;
    me->acc_det_gpio_assert = pdata->acc_det_gpio_assert;
    dev_dbg(me->dev, "accessory detection gpio = %u active %s\n",
        pdata->acc_det_gpio, (pdata->acc_det_gpio_assert? "high" : "low"));
    me->acc_det_debounce    = msecs_to_jiffies(pdata->acc_det_debounce_ms);
    dev_dbg(me->dev, "accessory detection debouncing = %umsec\n",
        pdata->acc_det_debounce_ms);

    me->psy.name             = MAX77696_PSY_EH_NAME;
    me->psy.type             = POWER_SUPPLY_TYPE_MAINS;
    me->psy.get_property     = max77696_eh_get_property;
    me->psy.properties       = max77696_eh_psy_props;
    me->psy.num_properties   = ARRAY_SIZE(max77696_eh_psy_props);
	me->psy.supplied_to	     = pdata->batteries;
	me->psy.num_supplicants  = pdata->num_batteries;

    INIT_DELAYED_WORK(&(me->det_work), max77696_eh_det_work);
    INIT_DELAYED_WORK(&(me->psy_work), max77696_eh_psy_work);
    INIT_DELAYED_WORK(&(me->acc_work), max77696_eh_acc_work);

    /* Initial configurations */
    max77696_eh_mode_set(me, pdata->initial_mode);
    dev_dbg(me->dev, "initial mode = %u\n", pdata->initial_mode);
    max77696_eh_acc_ilm_set(me, pdata->acc_ilimit);
    dev_dbg(me->dev,
        "accessory current limit = %umA\n", pdata->acc_ilimit);

    /* Disable all EH interrupts */
    max77696_eh_disable_irq(me, 0xFF, 1);

    /* First time charger update */
    max77696_eh_update_chg_state(me, 0);

    /* Initial configurations */

    rc = sysfs_create_group(me->kobj, &max77696_eh_attr_group);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to create attribute group [%d]\n", rc);
        goto out_err_sysfs;
    }

    rc = power_supply_register(me->dev, &(me->psy));
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to register USB psy device [%d]\n", rc);
        goto out_err_reg_psy;
    }

    /* Request EH interrupt */

    rc = request_threaded_irq(me->irq,
        NULL, max77696_eh_isr, IRQF_ONESHOT, DRIVER_NAME, me);

    if (unlikely(rc < 0)) {
        dev_err(me->dev, "failed to request IRQ(%d) [%d]\n", me->irq, rc);
        goto out_err_req_chg_irq;
    }

    BUG_ON(chip->eh_ptr);
    chip->eh_ptr = me;

    /* Enable EH interrupts we need */
    max77696_eh_enable_irq(me, CHGB_INT_BCHGPOK, 0);

    if (likely(me->acc_det_gpio > 0)) {
        rc = gpio_request(me->acc_det_gpio, DRIVER_NAME"-acc");
        if (unlikely(rc < 0)) {
            dev_err(me->dev, "failed to request GPIO(%d) [%d]\n",
                me->acc_det_gpio, rc);
            goto out_err_req_acc_gpio;
        }

        rc = gpio_direction_input(me->acc_det_gpio);
        if (unlikely(rc < 0)) {
            dev_err(me->dev, "failed to configure GPIO(%d) as input [%d]\n",
                me->acc_det_gpio, rc);
            goto out_err_cfg_acc_gpio;
        }

        rc = request_threaded_irq(gpio_to_irq(me->acc_det_gpio), NULL,
            max77696_eh_acc_isr,
            IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
            DRIVER_NAME"-acc",
            me);

        if (unlikely(rc < 0)) {
            dev_err(me->dev, "failed to request IRQ(%d) [%d]\n",
                gpio_to_irq(me->acc_det_gpio), rc);
            goto out_err_req_acc_irq;
        }

        dev_info(me->dev, "accessory detection: GPIO %u IRQ %u\n",
            me->acc_det_gpio, gpio_to_irq(me->acc_det_gpio));

        /* First time accessory update */
        max77696_eh_update_acc_state(me, 0);
    }

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(eh, chip);
    return 0;

out_err_req_acc_irq:
out_err_cfg_acc_gpio:
    gpio_free(me->acc_det_gpio);
out_err_req_acc_gpio:
    free_irq(me->irq, me);
out_err_req_chg_irq:
    power_supply_unregister(&(me->psy));
out_err_reg_psy:
    sysfs_remove_group(me->kobj, &max77696_eh_attr_group);
out_err_sysfs:
    platform_set_drvdata(pdev, NULL);
    mutex_destroy(&(me->lock));
    kfree(me);
    return rc;
}

static __devexit int max77696_eh_remove (struct platform_device *pdev)
{
    struct max77696_eh *me = platform_get_drvdata(pdev);

    me->chip->eh_ptr = NULL;

    free_irq(me->irq, me);
    free_irq(gpio_to_irq(me->acc_det_gpio), me);
    cancel_delayed_work(&(me->psy_work));
    max77696_eh_det_stop(me);
    gpio_free(me->acc_det_gpio);

    sysfs_remove_group(me->kobj, &max77696_eh_attr_group);
    power_supply_unregister(&(me->psy));

    platform_set_drvdata(pdev, NULL);
    mutex_destroy(&(me->lock));
    kfree(me);

    return 0;
}

static struct platform_driver max77696_eh_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .probe        = max77696_eh_probe,
    .remove       = __devexit_p(max77696_eh_remove),
};

static __init int max77696_eh_driver_init (void)
{
	return platform_driver_register(&max77696_eh_driver);
}

static __exit void max77696_eh_driver_exit (void)
{
	platform_driver_unregister(&max77696_eh_driver);
}

late_initcall(max77696_eh_driver_init);
module_exit(max77696_eh_driver_exit);

/*******************************************************************************
 * EXTERNAL FUNCTIONS
 ******************************************************************************/

extern struct max77696_chip* max77696;

int max77696_eh_set_mode (int mode)
{
    struct max77696_chip *chip = max77696;
    struct max77696_eh *me = chip->eh_ptr;
    int rc;

    __lock(me);
    rc = max77696_eh_mode_set(me, mode);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_eh_set_mode);

int max77696_eh_get_mode (int *mode)
{
    struct max77696_chip *chip = max77696;
    struct max77696_eh *me = chip->eh_ptr;
    int rc;

    __lock(me);
    rc = max77696_eh_mode_get(me, mode);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_eh_get_mode);

int max77696_eh_set_accessory_i_limit (int mA)
{
    struct max77696_chip *chip = max77696;
    struct max77696_eh *me = chip->eh_ptr;
    int rc;

    __lock(me);
    rc = max77696_eh_acc_ilm_set(me, mA);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_eh_set_accessory_i_limit);

int max77696_eh_get_accessory_i_limit (int *mA)
{
    struct max77696_chip *chip = max77696;
    struct max77696_eh *me = chip->eh_ptr;
    int rc;

    __lock(me);
    rc = max77696_eh_acc_ilm_get(me, mA);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_eh_get_accessory_i_limit);

int max77696_eh_set_cv_level (int mV)
{
    struct max77696_chip *chip = max77696;
    struct max77696_eh *me = chip->eh_ptr;
    int rc;

    __lock(me);
    rc = max77696_eh_mbccv_set(me, mV);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_eh_set_cv_level);

int max77696_eh_get_cv_level (int *mV)
{
    struct max77696_chip *chip = max77696;
    struct max77696_eh *me = chip->eh_ptr;
    int rc;

    __lock(me);
    rc = max77696_eh_mbccv_get(me, mV);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_eh_get_cv_level);

int max77696_eh_set_cc_level (bool pqsel, int mA)
{
    struct max77696_chip *chip = max77696;
    struct max77696_eh *me = chip->eh_ptr;
    int rc;

    __lock(me);
    rc = max77696_eh_mbcichfc_set(me, pqsel, mA);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_eh_set_cc_level);

int max77696_eh_get_cc_level (bool *pqsel, int *mA)
{
    struct max77696_chip *chip = max77696;
    struct max77696_eh *me = chip->eh_ptr;
    int rc;

    __lock(me);
    rc = max77696_eh_mbcichfc_get(me, pqsel, mA);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_eh_get_cc_level);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

