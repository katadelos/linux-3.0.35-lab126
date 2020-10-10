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
#define DEBUG
#define VERBOSE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>

#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/mfd/max77696.h>
#include <max77696_registers.h>
#include <llog.h>

#define DRIVER_DESC    "MAX77696 RTC Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_RTC_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".2"

#ifdef VERBOSE
#define dev_verbose(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_verbose(args...) do { } while (0)
#endif /* VERBOSE */

struct max77696_rtc {
	struct mutex          lock;
	struct max77696_chip *chip;
	struct max77696_i2c  *i2c;
	struct device        *dev;
	struct kobject       *kobj;

	struct rtc_device    *rtc_master;
	struct rtc_device    *rtc[2];
	unsigned int          irq;
	u8                    irq_unmask;
};

#define __get_i2c(chip)               (&((chip)->rtc_i2c))
#define __lock(me)                    mutex_lock(&((me)->lock))
#define __unlock(me)                  mutex_unlock(&((me)->lock))

/* RTC Register Read/Write */
#define max77696_rtc_reg_read(me, reg, val_ptr) \
	max77696_read((me)->i2c, RTC_REG(reg), val_ptr)
#define max77696_rtc_reg_write(me, reg, val) \
	max77696_write((me)->i2c, RTC_REG(reg), val)
#define max77696_rtc_reg_bulk_read(me, reg, dst, len) \
	max77696_bulk_read((me)->i2c, RTC_REG(reg), dst, len)
#define max77696_rtc_reg_bulk_write(me, reg, src, len) \
	max77696_bulk_write((me)->i2c, RTC_REG(reg), src, len)
#define max77696_rtc_reg_read_masked(me, reg, mask, val_ptr) \
	max77696_read_masked((me)->i2c, RTC_REG(reg), mask, val_ptr)
#define max77696_rtc_reg_write_masked(me, reg, mask, val) \
	max77696_write_masked((me)->i2c, RTC_REG(reg), mask, val)

/* RTC Register Single Bit Ops */
#define max77696_rtc_reg_get_bit(me, reg, bit, val_ptr) \
	({\
	 int __rc = max77696_rtc_reg_read_masked(me, reg,\
		 RTC_REG_BITMASK(reg, bit), val_ptr);\
	 *(val_ptr) = RTC_REG_BITGET(reg, bit, *(val_ptr));\
	 __rc;\
	 })
#define max77696_rtc_reg_set_bit(me, reg, bit, val) \
	({\
	 max77696_rtc_reg_write_masked(me, reg,\
		 RTC_REG_BITMASK(reg, bit), RTC_REG_BITSET(reg, bit, val));\
	 })

#define __msleep(msec) msleep_interruptible((unsigned int)(msec))
//#define __msleep(msec) msleep((unsigned int)(msec))
//#define __msleep(msec) mdelay((unsigned int)(msec))

#define RTC_UPDATE_TIME_OUT         1000 /* in milli-seconds */
#define RTC_HRMODE                  RTC_HRMODE_12H

unsigned long suspend_time = 0;
unsigned long last_suspend_time = 0;
EXPORT_SYMBOL(last_suspend_time);
unsigned long total_suspend_time = 0;
EXPORT_SYMBOL(total_suspend_time);

static char *dow_short[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

extern int pb_oneshot;
extern void pb_oneshot_unblock_button_events (void);

#ifdef CONFIG_FALCON
extern int in_falcon(void);
#endif

/* Convert MAX77696 RTC registers --> struct rtc_time */
static void max77696_rtc_format_reg2time (struct max77696_rtc *me,
		const u8 *regs, struct rtc_time *tm)
{
	u8 dow = regs[RTC_DOW] & RTC_DOW_MASK;

	tm->tm_year = (int)(regs[RTC_YEAR] & RTC_YEAR_MASK) + RTC_YEAR_OFFSET;
	tm->tm_mon  = (int)(regs[RTC_MONTH] & RTC_MONTH_MASK) - 1;
	tm->tm_mday = (int)(regs[RTC_DOM] & RTC_DOM_MASK);
	tm->tm_wday = (int)(dow? (__fls(dow)) : 0);
	tm->tm_hour = (int)(regs[RTC_HOUR] & RTC_HOUR_MASK);
	tm->tm_min  = (int)(regs[RTC_MIN] & RTC_MIN_MASK);
	tm->tm_sec  = (int)(regs[RTC_SEC] & RTC_SEC_MASK);

	if (RTC_HRMODE == RTC_HRMODE_12H) {
		tm->tm_hour %= 12;
		tm->tm_hour += ((regs[RTC_HOUR] & RTC_RTCHOUR_AMPM_MASK)? 12 : 0);
	}
}

/* Convert struct rtc_time --> MAX77696 RTC registers */
static void max77696_rtc_format_time2reg (struct max77696_rtc *me,
		const struct rtc_time *tm, u8 *regs)
{
	regs[RTC_YEAR]  = (u8)(tm->tm_year - RTC_YEAR_OFFSET) & RTC_YEAR_MASK;
	regs[RTC_MONTH] = (u8)(tm->tm_mon + 1) & RTC_MONTH_MASK;
	regs[RTC_DOM]   = (u8)(tm->tm_mday) & RTC_DOM_MASK;
	regs[RTC_DOW]   = (u8)(1 << tm->tm_wday) & RTC_DOW_MASK;
	regs[RTC_HOUR]  = (u8)(tm->tm_hour) & RTC_HOUR_MASK;
	regs[RTC_MIN]   = (u8)(tm->tm_min) & RTC_MIN_MASK;
	regs[RTC_SEC]   = (u8)(tm->tm_sec) & RTC_SEC_MASK;

	if (RTC_HRMODE == RTC_HRMODE_12H) {
		if (tm->tm_hour > 12) {
			regs[RTC_HOUR] |= RTC_RTCHOUR_PM;
			regs[RTC_HOUR] -= 12;
		} else if (tm->tm_hour == 12) {
			regs[RTC_HOUR] |= RTC_RTCHOUR_PM;
		} else if (tm->tm_hour == 0) {
			regs[RTC_HOUR] = 12;
		}
	}
}

#define max77696_rtc_write_irq_mask(me) \
	do {\
		u8 _buf = ((~((me)->irq_unmask)) & RTC_RTCINT_ALL);\
		int _rc = max77696_rtc_reg_write(me, RTCINTM, _buf);\
		dev_verbose((me)->dev, "written RTCINTM 0x%02X [%d]\n", _buf, _rc);\
		if (unlikely(_rc)) {\
			dev_err((me)->dev, "RTCINTM write error [%d]\n", _rc);\
		}\
	} while (0)

	static __always_inline
void max77696_rtc_enable_irq (struct max77696_rtc *me, u8 irq_bits)
{
	if (unlikely((me->irq_unmask & irq_bits) == irq_bits)) {
		/* already unmasked */
		return;
	}

	if (unlikely(!me->irq_unmask)) {
		max77696_topsys_enable_rtc_wakeup(1);

		enable_irq(me->irq);
	}

	/* set enabled flag */
	me->irq_unmask |= irq_bits;
	max77696_rtc_write_irq_mask(me);
}

	static __always_inline
void max77696_rtc_disable_irq (struct max77696_rtc *me, u8 irq_bits)
{
	if (unlikely((me->irq_unmask & irq_bits) == 0)) {
		/* already masked */
		return;
	}

	/* clear enabled flag */
	me->irq_unmask &= ~irq_bits;

	if (unlikely(!me->irq_unmask)) {
		max77696_topsys_enable_rtc_wakeup(0);

		disable_irq(me->irq);
	}

	max77696_rtc_write_irq_mask(me);
}

#define max77696_rtc_init_data_mode(me) \
	max77696_rtc_reg_write(me, RTCCNTL, RTC_DATA_MODE)

static int max77696_rtc_sync_read_buffer (struct max77696_rtc *me)
{
	u8 upd_in_prog;
	unsigned long timeout;
	int rc;

	rc = max77696_rtc_reg_set_bit(me, RTCUPDATE0, RBUDR, 1);
	if (unlikely(rc)) {
		dev_err(me->dev, "RTCUPDATE0 write error [%d]\n", rc);
		goto out;
	}

	timeout = jiffies + msecs_to_jiffies(RTC_UPDATE_TIME_OUT);

	do {
		if (unlikely(time_after(jiffies, timeout))) {
			dev_err(me->dev, "rtc update timed out\n");
			rc = -ETIMEDOUT;
			goto out;
		}
		__msleep(1);
		max77696_rtc_reg_get_bit(me, RTCUPDATE0, RBUDR, &upd_in_prog);
	} while (likely(upd_in_prog));

out:
	return rc;
}

static int max77696_rtc_commit_write_buffer (struct max77696_rtc *me)
{
	u8 upd_in_prog;
	unsigned long timeout;
	int rc;

	rc = max77696_rtc_reg_set_bit(me, RTCUPDATE0, UDR, 1);
	if (unlikely(rc)) {
		dev_err(me->dev, "RTCUPDATE0 write error [%d]\n", rc);
		goto out;
	}

	timeout = jiffies + msecs_to_jiffies(RTC_UPDATE_TIME_OUT);

	do {
		if (unlikely(time_after(jiffies, timeout))) {
			dev_err(me->dev, "rtc update timed out\n");
			rc = - -ETIMEDOUT;
			goto out;
		}
		__msleep(1);
		max77696_rtc_reg_get_bit(me, RTCUPDATE0, UDR, &upd_in_prog);
	} while (likely(upd_in_prog));

out:
	return rc;
}

static int max77696_rtc_read_time_buf (struct max77696_rtc *me,
		struct rtc_time *tm)
{
	u8 buf[RTC_REGCNT];
	int rc;

	/* Sync "Read Buffers" */
	rc = max77696_rtc_sync_read_buffer(me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to sync read buffer [%d]\n", rc);
		goto out;
	}

	rc = max77696_rtc_reg_bulk_read(me, RTCSEC, buf, RTC_REGCNT);
	if (unlikely(rc)) {
		dev_err(me->dev, "RTCSEC read error [%d]\n", rc);
		goto out;
	}

	max77696_rtc_format_reg2time(me, buf, tm);

	dev_verbose(me->dev, "read time:\n");
	dev_verbose(me->dev,
			"YEAR %02X MONTH %02X DOM %02X DOW %02X HOUR %02X MIN %02X SEC %02X\n",
			buf[RTC_YEAR], buf[RTC_MONTH], buf[RTC_DOM],
			buf[RTC_DOW],
			buf[RTC_HOUR], buf[RTC_MIN], buf[RTC_SEC]);
	dev_verbose(me->dev, "--> %04d-%02d-%02d %s %02d:%02d:%02d\n",
			tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
			dow_short[tm->tm_wday],
			tm->tm_hour, tm->tm_min, tm->tm_sec);

out:
	return rc;
}

static int max77696_rtc_write_time_buf (struct max77696_rtc *me,
		struct rtc_time *tm)
{
	u8 buf[RTC_REGCNT];
	int rc;

	max77696_rtc_format_time2reg(me, tm, buf);

	dev_verbose(me->dev, "setting time:\n");
	dev_verbose(me->dev, "%04d-%02d-%02d %s %02d:%02d:%02d -->\n",
			tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
			dow_short[tm->tm_wday],
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	dev_verbose(me->dev,
			"YEAR %02X MONTH %02X DOM %02X DOW %02X HOUR %02X MIN %02X SEC %02X\n",
			buf[RTC_YEAR], buf[RTC_MONTH], buf[RTC_DOM],
			buf[RTC_DOW],
			buf[RTC_HOUR], buf[RTC_MIN], buf[RTC_SEC]);

	rc = max77696_rtc_init_data_mode(me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to initialize data mode [%d]\n", rc);
		goto out;
	}

	rc = max77696_rtc_reg_bulk_write(me, RTCSEC, buf, RTC_REGCNT);
	if (unlikely(rc)) {
		dev_err(me->dev, "RTCSEC...RTCDOM write error [%d]\n", rc);
		goto out;
	}

	/* Commit "Write Buffers" */
	rc = max77696_rtc_commit_write_buffer(me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to commit write buffer [%d]\n", rc);
		goto out;
	}

out:
	return rc;
}

static int max77696_rtc_alarm_irq_enable (struct max77696_rtc *me, int alrm_id,
		unsigned int enabled)
{
	u8 alrm_irq_bit = ((alrm_id == 0)? RTC_RTCINT_RTCA1 : RTC_RTCINT_RTCA2);

	if (enabled) {
		max77696_rtc_enable_irq(me, alrm_irq_bit);
	} else {
		max77696_rtc_disable_irq(me, alrm_irq_bit);
	}

	return 0;
}

static int max77696_rtc_read_alarm_buf (struct max77696_rtc *me, int alrm_id,
		struct rtc_wkalrm *alrm)
{
	struct rtc_time *tm = &(alrm->time);
	u8 alrm_buf_reg, buf[RTC_REGCNT];
	int rc;

	alrm_buf_reg = ((alrm_id == 0)? RTC_RTCSECA1_REG : RTC_RTCSECA2_REG);

	rc = max77696_bulk_read(me->i2c, alrm_buf_reg, buf, RTC_REGCNT);
	if (unlikely(rc)) {
		dev_err(me->dev, "RTCSECAx++%d read error [%d]\n", RTC_REGCNT, rc);
		goto out;
	}

	max77696_rtc_format_reg2time(me, buf, tm);

	dev_verbose(me->dev, "read alarm%d:\n", alrm_id);
	dev_verbose(me->dev,
			"YEAR %02X MONTH %02X DOM %02X HOUR %02X MIN %02X SEC %02X\n",
			buf[RTC_YEAR], buf[RTC_MONTH], buf[RTC_DOM],
			buf[RTC_HOUR], buf[RTC_MIN], buf[RTC_SEC]);
	dev_verbose(me->dev, "--> %04d-%02d-%02d %02d:%02d:%02d\n",
			tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);

out:
	return rc;
}

static int max77696_rtc_write_alarm_buf (struct max77696_rtc *me, int alrm_id,
		struct rtc_wkalrm *alrm)
{
	struct rtc_time *tm = &(alrm->time);
	u8 alrm_en_reg, alrm_buf_reg, alrm_irq_bit, alrm_en, buf[RTC_REGCNT];
	int rc = 0;

	if (alrm_id == 0) {
		alrm_en_reg  = RTC_RTCAE1_REG;
		alrm_buf_reg = RTC_RTCSECA1_REG;
		alrm_irq_bit = RTC_RTCINT_RTCA1;
	} else {
		alrm_en_reg  = RTC_RTCAE2_REG;
		alrm_buf_reg = RTC_RTCSECA2_REG;
		alrm_irq_bit = RTC_RTCINT_RTCA2;
	}

	/* Disable RTC alarm interrupt */
	max77696_rtc_disable_irq(me, alrm_irq_bit);

	/* Set new alaram */

	if (likely(alrm->enabled)) {
		max77696_rtc_format_time2reg(me, tm, buf);

		/* we never use DOW alarm */
		alrm_en      = (RTC_RTCAE_ALL & ~RTC_RTCAE_DOW);
		buf[RTC_DOW] = 0x00;

		dev_verbose(me->dev, "setting alarm%d:\n", alrm_id);
		dev_verbose(me->dev, "%04d-%02d-%02d %02d:%02d:%02d -->\n",
				tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec);
		dev_verbose(me->dev,
				"YEAR %02X MONTH %02X DOM %02X HOUR %02X MIN %02X SEC %02X\n",
				buf[RTC_YEAR], buf[RTC_MONTH], buf[RTC_DOM],
				buf[RTC_HOUR], buf[RTC_MIN], buf[RTC_SEC]);

		rc = max77696_rtc_init_data_mode(me);
		if (unlikely(rc)) {
			dev_err(me->dev, "failed to initialize data mode [%d]\n", rc);
			goto out;
		}

		rc = max77696_bulk_write(me->i2c, alrm_buf_reg, buf, RTC_REGCNT);
		if (unlikely(rc)) {
			dev_err(me->dev, "RTCSECAx++%d write error [%d]\n", RTC_REGCNT, rc);
			goto out;
		}

		/* commit new alarm time */
		rc = max77696_rtc_commit_write_buffer(me);
		if (unlikely(rc)) {
			dev_err(me->dev, "%s error commiting write buffers, %d", __func__, rc);
		}

		/* Enable alarm bits */
		rc = max77696_write(me->i2c, alrm_en_reg, alrm_en);
		if (unlikely(rc)) {
			dev_err(me->dev, "RTCAEx write error [%d]\n", rc);
			goto out;
		}

		/* Enable RTC alarm interrupt */
		max77696_rtc_enable_irq(me, alrm_irq_bit);
	}

out:
	return rc;
}

static irqreturn_t max77696_rtc_isr (int irq, void *data)
{
	struct max77696_rtc *me = data;
	u8 rtc_events[2] = { 0, 0 };
	u8 interrupted, ignore;

	__lock(me);

	if(pb_oneshot == HIBER_SUSP) {
		pb_oneshot = HIBER_RTC_IRQ;
		pb_oneshot_unblock_button_events();
	}

	max77696_rtc_reg_read(me, RTCINT, &interrupted);
	dev_dbg(me->dev, "RTCINT %02X EN %02X\n", interrupted, me->irq_unmask);

	/* Do a second read of RTC interrupt to eliminate possible ClearOnRead
	 * failed issue. We intermittently seem to hit one-off spurious alarm
	 * interrupt case (typically getting an interrupt before alarm expires)
	 * causing undesired side-effects - JSEVEN-4256
	 */
	max77696_rtc_reg_read(me, RTCINT, &ignore);
	if(ignore) {
		dev_err(me->dev, "RTC Clear on Read failed! RTCINT %02X\n", ignore);
		LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, 
			"kernel", "rtc", "RTCINT ClearOnRead failed", 1, "");
	}

	interrupted &= me->irq_unmask;

	if (interrupted & RTC_RTCINT_RTC60S) {
		rtc_events[RTC_MASTER] |= RTC_PF;
	}

	if (interrupted & RTC_RTCINT_RTCA1) {
		int rc;
		long delta;
		long gracetime = 3; /* default gracetime is 3 sec */
		struct rtc_time tm;
		unsigned long curr_time;
		struct rtc_wkalrm alarm;
		unsigned long alarm_time;
		/* Check if we have hit a valid alarm0 interrupt and print
		 * to record abnormal cases
		 */
		rc = max77696_rtc_read_time_buf(me, &tm);
		if (unlikely(rc)) {
			dev_err(me->dev, "RTC read time buf failed [%d]", rc);
			goto out;
		}
		rtc_tm_to_time(&tm, &curr_time);

		rc = max77696_rtc_read_alarm_buf(me, 0, &alarm);
		if (unlikely(rc)) {
			dev_err(me->dev, "RTC read alarm buf failed [%d]", rc);
			goto out;
		}
		rtc_tm_to_time(&(alarm.time), &alarm_time);

		delta = curr_time - alarm_time;

#ifdef CONFIG_FALCON
		/* give more gracetime (in secs) for Hibernate case */
		if(in_falcon()) gracetime = 10;
#endif
		/* we consider an alarm valid only if
		 * current time is greater than or equal to the alarm expiry AND
		 * current time less than or equal to (alarm expiry + gracetime) 
		 */
		if(delta >= 0 && delta <= gracetime) {
			dev_verbose(me->dev, "This is a valid RTC Alarm0 int "
			":current time: %lu alarm settime: %lu\n", curr_time, alarm_time);
		} else {
			/* outside of gracetime, we deem to have hit spurious interrupt */
			dev_err(me->dev, "This is a spurious RTC Alarm0 int "
			":current time: %lu Alarm settime: %lu\n", curr_time, alarm_time);
			LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
				"kernel", "rtc", "RTC spurious interrupt", 1, "");
			goto out;
		}

		rtc_events[0] |= RTC_AF;
	}

	if (interrupted & RTC_RTCINT_RTCA2) {
		rtc_events[1] |= RTC_AF;
	}

	if (interrupted & RTC_RTCINT_RTC1S) {
		rtc_events[RTC_MASTER] |= RTC_UF;
	}

	if (likely(rtc_events[0])) {
		rtc_update_irq(me->rtc[0], 1, RTC_IRQF | rtc_events[0]);
	}

	if (likely(rtc_events[1])) {
		rtc_update_irq(me->rtc[1], 1, RTC_IRQF | rtc_events[1]);
	}

out:
	__unlock(me);
	return IRQ_HANDLED;
}

static int max77696_rtc_read_time (struct device *dev, struct rtc_time *tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	int rc;

	__lock(me);

	rc = max77696_rtc_read_time_buf(me, tm);

	__unlock(me);
	return rc;
}

static int max77696_rtc_set_time (struct device *dev, struct rtc_time *tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	int rc;

	__lock(me);

	rc = max77696_rtc_write_time_buf(me, tm);

	__unlock(me);
	return rc;
}

static int max77696_rtc_read_alarm0 (struct device *dev,
		struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	int rc;

	__lock(me);

	rc = max77696_rtc_read_alarm_buf(me, 0, alrm);

	__unlock(me);
	return rc;
}

static int max77696_rtc_set_alarm0 (struct device *dev,
		struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	int rc;

	__lock(me);

	rc = max77696_rtc_write_alarm_buf(me, 0, alrm);

	__unlock(me);
	return rc;
}

static int max77696_rtc_alarm0_irq_enable (struct device *dev,
		unsigned int enabled)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	int rc = 0;

	__lock(me);

	rc = max77696_rtc_alarm_irq_enable(me, 0, enabled);

	__unlock(me);
	return rc;
}

static const struct rtc_class_ops max77696_rtc_ops0 = {
	.read_time        = max77696_rtc_read_time,
	.set_time         = max77696_rtc_set_time,
	.read_alarm       = max77696_rtc_read_alarm0,
	.set_alarm        = max77696_rtc_set_alarm0,
	.alarm_irq_enable = max77696_rtc_alarm0_irq_enable,
};

static int max77696_rtc_read_alarm1 (struct device *dev,
		struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	int rc;

	__lock(me);

	rc = max77696_rtc_read_alarm_buf(me, 1, alrm);

	__unlock(me);
	return rc;
}

static int max77696_rtc_alarm1_irq_enable (struct device *dev,
		unsigned int enabled)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	int rc = 0;

	__lock(me);

	rc = max77696_rtc_alarm_irq_enable(me, 1, enabled);

	__unlock(me);
	return rc;
}

static int max77696_rtc_set_alarm1 (struct device *dev,
		struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	int rc;

	__lock(me);

	rc = max77696_rtc_write_alarm_buf(me, 1, alrm);

	__unlock(me);
	return rc;
}

static const struct rtc_class_ops max77696_rtc_ops1 = {
	.read_time        = max77696_rtc_read_time,
	.set_time         = max77696_rtc_set_time,
	.read_alarm       = max77696_rtc_read_alarm1,
	.set_alarm        = max77696_rtc_set_alarm1,
	.alarm_irq_enable = max77696_rtc_alarm1_irq_enable,
};

static ssize_t max77696_rtc_irq_show (struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	u8 rtcintm;
	int rc;

	__lock(me);

	rc = max77696_rtc_reg_read(me, RTCINTM, &rtcintm);
	if (unlikely(rc)) {
		goto out;
	}

	rc = (int)snprintf(buf, PAGE_SIZE, "RTCINTM 0x%02X\n", rtcintm);

	rc += (int)snprintf(buf+rc, PAGE_SIZE, "Watchdog SoftReset IRQ  %s\n",
			(me->irq_unmask & RTC_RTCINT_WTSR)? "enabled" : "-");
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "RTC 1 second Timer IRQ  %s\n",
			(me->irq_unmask & RTC_RTCINT_RTC1S)? "enabled" : "-");
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "SMPL Event IRQ          %s\n",
			(me->irq_unmask & RTC_RTCINT_SMPL)? "enabled" : "-");
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "RTC Alarm 2 IRQ         %s\n",
			(me->irq_unmask & RTC_RTCINT_RTCA2)? "enabled" : "-");
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "RTC Alarm 1 IRQ         %s\n",
			(me->irq_unmask & RTC_RTCINT_RTCA1)? "enabled" : "-");
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "RTC 1 minute Timer IRQ  %s\n",
			(me->irq_unmask & RTC_RTCINT_RTC60S)? "enabled" : "-");

out:
	__unlock(me);
	return (ssize_t)rc;
}

static DEVICE_ATTR(rtc_irq, S_IRUGO, max77696_rtc_irq_show, NULL);

static ssize_t max77696_rtc_time_show (struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	struct rtc_time tm;
	unsigned long time;
	int rc;

	__lock(me);

	rc = max77696_rtc_read_time_buf(me, &tm);
	if (unlikely(rc)) {
		goto out;
	}

	rtc_tm_to_time(&tm, &time);
	rc  = (int)snprintf(buf, PAGE_SIZE, "%lu (", time);
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "%04d-%02d-%02d %s ",
			tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, dow_short[tm.tm_wday]);
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "%02d:%02d:%02d)\n",
			tm.tm_hour, tm.tm_min, tm.tm_sec);

out:
	__unlock(me);
	return (ssize_t)rc;
}

static ssize_t max77696_rtc_wake_delta_store (struct device *dev,
		struct device_attribute *devattr, const char *buf, size_t count)
{
	int rc;
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);

	unsigned long delta, secs;

	struct rtc_wkalrm alarm;
	struct rtc_time *tm = &(alarm.time);

	delta = simple_strtol(buf, NULL, 10);

	__lock(me);
	rc = max77696_rtc_read_time_buf(me, tm);
	if (rc) {
		printk(KERN_ERR "Could not read current RTC time\n");
		__unlock(me);
		return 0;
	}

	rtc_tm_to_time(tm, &secs);
	secs+=delta;
	rtc_time_to_tm(secs, tm);
	alarm.enabled = true;

	rc = max77696_rtc_write_alarm_buf (me, 0, &alarm);
	if (rc) {
		printk(KERN_ERR "Could not write RTC alarm\n");
		__unlock(me);
		return 0;
	}

	__unlock(me);
	return count;
}
static DEVICE_ATTR(rtc_delta_alarm, S_IWUGO, NULL,  max77696_rtc_wake_delta_store);

static ssize_t max77696_rtc_time_store (struct device *dev,
		struct device_attribute *devattr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	struct rtc_time tm;
	unsigned long curr_time;
	int rc;

	__lock(me);

	rc = max77696_rtc_read_time_buf(me, &tm);
	if (unlikely(rc)) {
		goto out;
	}

	rtc_tm_to_time(&tm, &curr_time);

	if (*buf == '+') {
		rtc_time_to_tm(curr_time + simple_strtoul(buf+1, NULL, 0), &tm);
	} else {
		rtc_time_to_tm(simple_strtoul(buf, NULL, 0), &tm);
	}

	max77696_rtc_write_time_buf(me, &tm);

out:
	__unlock(me);
	return (ssize_t)count;
}

static DEVICE_ATTR(rtc_time, S_IWUSR | S_IRUGO,
		max77696_rtc_time_show, max77696_rtc_time_store);

static ssize_t max77696_rtc_alarm0_show (struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	struct rtc_wkalrm alrm;
	unsigned long time;
	int rc;

	__lock(me);

	rc = max77696_rtc_read_alarm_buf(me, 0, &alrm);
	if (unlikely(rc)) {
		goto out;
	}

	rtc_tm_to_time(&(alrm.time), &time);
	rc  = (int)snprintf(buf, PAGE_SIZE, "%lu (", time);
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "%04d-%02d-%02d    ",
			alrm.time.tm_year+1900, alrm.time.tm_mon+1, alrm.time.tm_mday);
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "%02d:%02d:%02d)\n",
			alrm.time.tm_hour, alrm.time.tm_min, alrm.time.tm_sec);

out:
	__unlock(me);
	return (ssize_t)rc;
}

static DEVICE_ATTR(rtc_alarm0, S_IRUGO, max77696_rtc_alarm0_show, NULL);

static ssize_t max77696_rtc_alarm1_show (struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	struct rtc_wkalrm alrm;
	unsigned long time;
	int rc;

	__lock(me);

	rc = max77696_rtc_read_alarm_buf(me, 1, &alrm);
	if (unlikely(rc)) {
		goto out;
	}

	rtc_tm_to_time(&(alrm.time), &time);
	rc  = (int)snprintf(buf, PAGE_SIZE, "%lu (", time);
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "%04d-%02d-%02d    ",
			alrm.time.tm_year+1900, alrm.time.tm_mon+1, alrm.time.tm_mday);
	rc += (int)snprintf(buf+rc, PAGE_SIZE, "%02d:%02d:%02d)\n",
			alrm.time.tm_hour, alrm.time.tm_min, alrm.time.tm_sec);

out:
	__unlock(me);
	return (ssize_t)rc;
}

static DEVICE_ATTR(rtc_alarm1, S_IRUGO, max77696_rtc_alarm1_show, NULL);

static struct attribute* max77696_rtc_attr[] = {
	&dev_attr_rtc_irq.attr,
	&dev_attr_rtc_time.attr,
	&dev_attr_rtc_alarm0.attr,
	&dev_attr_rtc_alarm1.attr,
	&dev_attr_rtc_delta_alarm.attr,
	NULL
};

static const struct attribute_group max77696_rtc_attr_group = {
	.attrs = max77696_rtc_attr,
};

static __devinit int max77696_rtc_probe (struct platform_device *pdev)
{
	struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77696_rtc_platform_data *pdata = pdev->dev.platform_data;
	struct max77696_rtc *me;
	struct rtc_time tm;
	u8 interrupted, irq_bits;
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

	me->irq = chip->irq_base + MAX77696_ROOTINT_RTC;

	rc = sysfs_create_group(me->kobj, &max77696_rtc_attr_group);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to create attribute group [%d]\n", rc);
		return rc;
	}

	max77696_chip_set_wakeup(me->dev, 1);

	me->rtc[0] = rtc_device_register(DRIVER_NAME".0", me->dev,
			&max77696_rtc_ops0, THIS_MODULE);

	if (unlikely(IS_ERR(me->rtc[0]))) {
		rc = PTR_ERR(me->rtc[0]);
		me->rtc[0] = NULL;

		dev_err(me->dev, "failed to register 1ST rtc device [%d]\n", rc);
		goto out_err;
	}

	me->rtc[1] = rtc_device_register(DRIVER_NAME".1", me->dev,
			&max77696_rtc_ops1, THIS_MODULE);

	if (unlikely(IS_ERR(me->rtc[1]))) {
		rc = PTR_ERR(me->rtc[1]);
		me->rtc[1] = NULL;

		dev_err(me->dev, "failed to register 2ND rtc device [%d]\n", rc);
		goto out_err;
	}

	me->rtc_master = me->rtc[RTC_MASTER];

	/* Disable & Clear all RTC interrupts */
	max77696_rtc_reg_write(me, RTCINTM, RTC_RTCINT_ALL);
	max77696_rtc_reg_write(me, RTCAE1, RTC_RTCAE_NONE);
	max77696_rtc_reg_write(me, RTCAE2, RTC_RTCAE_NONE);
	max77696_rtc_reg_set_bit(me, RTCUPDATE0, FCUR, 1);
	max77696_rtc_reg_read(me, RTCINT, &interrupted);

	rc = request_threaded_irq(me->irq,
			NULL, max77696_rtc_isr, IRQF_ONESHOT, DRIVER_NAME, me);

	if (unlikely(rc < 0)) {
		dev_err(me->dev, "failed to request IRQ(%d) [%d]\n", me->irq, rc);
		goto out_err;
	}

	disable_irq(me->irq);

	dev_dbg(me->dev, "1 minute interrupt: %s\n", pdata->irq_1m ? "on" : "off");
	dev_dbg(me->dev, "1 second interrupt: %s\n", pdata->irq_1s ? "on" : "off");

	irq_bits  = (pdata->irq_1m ? RTC_RTCINT_RTC60S : 0);
	irq_bits |= (pdata->irq_1s ? RTC_RTCINT_RTC1S  : 0);
	max77696_rtc_enable_irq(me, irq_bits);

	max77696_rtc_read_time_buf(me, &tm);
	dev_dbg(me->dev, "current time *** %04d-%02d-%02d %s %02d:%02d:%02d\n",
			tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
			dow_short[tm.tm_wday],
			tm.tm_hour, tm.tm_min, tm.tm_sec);

	pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
	SUBDEVICE_SET_LOADED(rtc, chip);
	return 0;

out_err:
	me->rtc_master = NULL;
	if (likely(me->rtc[1])) {
		rtc_device_unregister(me->rtc[1]);
	}
	if (likely(me->rtc[0])) {
		rtc_device_unregister(me->rtc[0]);
	}
	max77696_chip_set_wakeup(me->dev, 0);
	sysfs_remove_group(me->kobj, &max77696_rtc_attr_group);
	mutex_destroy(&(me->lock));
	platform_set_drvdata(pdev, NULL);
	kfree(me);
	return rc;
}

static __devexit int max77696_rtc_remove (struct platform_device *pdev)
{
	struct max77696_rtc *me = platform_get_drvdata(pdev);

	free_irq(me->irq, me);
	me->rtc_master = NULL;

	rtc_device_unregister(me->rtc[1]);
	rtc_device_unregister(me->rtc[0]);

	sysfs_remove_group(me->kobj, &max77696_rtc_attr_group);

	mutex_destroy(&(me->lock));
	platform_set_drvdata(pdev, NULL);
	kfree(me);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77696_rtc_suspend (struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	struct rtc_time tm;
	int rc;

	__lock(me);

	rc = max77696_rtc_read_time_buf(me, &tm);
	if (unlikely(rc)) {
		goto out;
	}

	rtc_tm_to_time(&tm, &suspend_time);

out:
	__unlock(me);
	return rc;
}

static int max77696_rtc_resume (struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77696_rtc *me = platform_get_drvdata(pdev);
	struct rtc_time tm;
	unsigned long resume_time;
	int rc;
	long delta;
	struct rtc_wkalrm alarm;
	unsigned long alarm_time;
	__lock(me);

	rc = max77696_rtc_read_time_buf(me, &tm);
	if (unlikely(rc)) {
		goto out;
	}

	rtc_tm_to_time(&tm, &resume_time);

	last_suspend_time = resume_time - suspend_time;
	total_suspend_time += last_suspend_time;
	suspend_time = 0;
	max77696_rtc_read_alarm_buf(me, 0, &alarm);
	rtc_tm_to_time(&(alarm.time), &alarm_time);
	delta = alarm_time - resume_time;
	dev_dbg(me->dev, "%s: alarm_time:%ld resume_time%ld",__func__, alarm_time, resume_time);

out:
	__unlock(me);
	return rc;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(max77696_rtc_pm, max77696_rtc_suspend, max77696_rtc_resume);

static struct platform_driver max77696_rtc_driver = {
	.driver.name  = DRIVER_NAME,
	.driver.owner = THIS_MODULE,
	.driver.pm    = &max77696_rtc_pm,
	.probe        = max77696_rtc_probe,
	.remove       = __devexit_p(max77696_rtc_remove),
};

static __init int max77696_rtc_driver_init (void)
{
	return platform_driver_register(&max77696_rtc_driver);
}

static __exit void max77696_rtc_driver_exit (void)
{
	platform_driver_unregister(&max77696_rtc_driver);
}

module_init(max77696_rtc_driver_init);
module_exit(max77696_rtc_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

