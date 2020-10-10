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

#include <linux/irq.h>
#include <linux/mfd/max77696.h>
#include <linux/mfd/max77696-events.h>
#include <max77696_registers.h>

#define DRIVER_DESC    "MAX77696 TOPSYS"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_TOPSYS_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#ifdef CONFIG_FALCON
extern int in_falcon(void);
#endif

#define __get_i2c(chip)                  (&((chip)->pmic_i2c))
#define __lock(me)                       mutex_lock(&((me)->lock))
#define __unlock(me)                     mutex_unlock(&((me)->lock))

/* TOPSYS Register Read/Write */
#define max77696_topsys_reg_read(me, reg, val_ptr) \
	max77696_read((me)->i2c, TOPSYS_REG(reg), val_ptr)
#define max77696_topsys_reg_write(me, reg, val) \
	max77696_write((me)->i2c, TOPSYS_REG(reg), val)
#define max77696_topsys_reg_bulk_read(me, reg, dst, len) \
	max77696_bulk_read((me)->i2c, TOPSYS_REG(reg), dst, len)
#define max77696_topsys_reg_bulk_write(me, reg, src, len) \
	max77696_bulk_write((me)->i2c, TOPSYS_REG(reg), src, len)
#define max77696_topsys_reg_read_masked(me, reg, mask, val_ptr) \
	max77696_read_masked((me)->i2c, TOPSYS_REG(reg), mask, val_ptr)
#define max77696_topsys_reg_write_masked(me, reg, mask, val) \
	max77696_write_masked((me)->i2c, TOPSYS_REG(reg), mask, val)

/* TOPSYS Register Single Bit Ops */
#define max77696_topsys_reg_get_bit(me, reg, bit, val_ptr) \
	({\
	 int __rc = max77696_topsys_reg_read_masked(me, reg,\
		 TOPSYS_REG_BITMASK(reg, bit), val_ptr);\
	 *(val_ptr) = TOPSYS_REG_BITGET(reg, bit, *(val_ptr));\
	 __rc;\
	 })
#define max77696_topsys_reg_set_bit(me, reg, bit, val) \
	({\
	 max77696_topsys_reg_write_masked(me, reg,\
		 TOPSYS_REG_BITMASK(reg, bit),\
		 TOPSYS_REG_BITSET(reg, bit, val));\
	 })

#define NR_TOPSYS_IRQ                  MAX77696_TOPSYSINT_NR_IRQS


struct max77696_topsys {
	struct max77696_chip *chip;
	struct max77696_i2c  *i2c;
	struct device        *dev;
	struct kobject       *kobj;

	unsigned int          irq_base;
	unsigned int          top_irq;

	u8                    irq_unmask_new;
	u8                    irq_unmask_curr;
	u8                    irq_wakeup_bitmap;
	struct mutex          lock;
};

static pmic_event_callback_t sw_partialrstrt;
static pmic_event_callback_t sw_fullshtdwn;
static pmic_event_callback_t sw_factoryship;

int max77696_topsys_glbl_mask(void *obj, u16 event, bool mask_f)
{
	u8 irq = 0, bit_pos = 0, buf = 0x0;
	int rc;
	struct max77696_topsys *me = (struct max77696_topsys *)obj;

	DECODE_EVENT(event, irq, bit_pos);
	__lock(me);
	rc = max77696_topsys_reg_read(me, GLBLINTM, & buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "GLBL_INT_MASK read error [%d]\n", rc);
		goto out;
	}

	if(mask_f) {
		rc = max77696_topsys_reg_write(me, GLBLINTM, (buf | bit_pos));
		if (unlikely(rc)) {
			dev_err(me->dev, "GLBL_INT_MASK write error [%d]\n", rc);
			goto out;
		}
	}
	else {
		rc = max77696_topsys_reg_write(me, GLBLINTM, (buf & ~bit_pos));
		if (unlikely(rc)) {
			dev_err(me->dev, "GLBL_INT_MASK write error [%d]\n", rc);
			goto out;
		}
	}
out:
	__unlock(me);
	return rc;
}
EXPORT_SYMBOL(max77696_topsys_glbl_mask);

static struct max77696_event_handler max77696_en0rise_handle = {
	.mask_irq = max77696_topsys_glbl_mask,
	.event_id = EVENT_TOPS_EN0RISE,
};

static struct max77696_event_handler max77696_en0fall_handle = {
	.mask_irq = max77696_topsys_glbl_mask,
	.event_id = EVENT_TOPS_EN0FALL,
};

static struct max77696_event_handler max77696_critbat_handle = {
	.mask_irq = max77696_topsys_glbl_mask,
	.event_id = EVENT_TOPS_MLOBAT,
};

static struct max77696_event_handler max77696_sw_partialrstrt_hdl = {
	.mask_irq = NULL,
	.event_id = EVENT_SW_PARTIALRESTART,
};

static struct max77696_event_handler max77696_sw_fullshtdwn_hdl = {
	.mask_irq = NULL,
	.event_id = EVENT_SW_FULLSHUTDWN,
};

static struct max77696_event_handler max77696_sw_factoryship_hdl = {
	.mask_irq = NULL,
	.event_id = EVENT_SW_FACTORYSHIP,
};

#if 0
static struct max77696_event_handler max77696_mrwrn_handle = {
	.mask_irq = max77696_topsys_glbl_mask,
	.event_id = EVENT_TOPS_MRWARN,
};
#endif

static void max77696_topsys_irq_mask (struct irq_data *data)
{
	struct max77696_topsys *me = irq_data_get_irq_chip_data(data);
	u8 irq_bit = (u8)(1 << (data->irq - me->irq_base + 1));

	if (unlikely(!(me->irq_unmask_new & irq_bit))) {
		return;
	}

	me->irq_unmask_new &= ~irq_bit;

	if (unlikely(me->irq_unmask_new == GLBLINT_IRQ)) {
		me->irq_unmask_new = 0;
	}
}

static void max77696_topsys_irq_unmask (struct irq_data *data)
{
	struct max77696_topsys *me = irq_data_get_irq_chip_data(data);
	u8 irq_bit = (u8)(1 << (data->irq - me->irq_base + 1));

	if (unlikely((me->irq_unmask_new & irq_bit))) {
		return;
	}

	if (unlikely(!me->irq_unmask_new)) {
		me->irq_unmask_new = GLBLINT_IRQ;
	}

	me->irq_unmask_new |= irq_bit;
}

static void max77696_topsys_irq_bus_lock (struct irq_data *data)
{
	struct max77696_topsys *me = irq_data_get_irq_chip_data(data);

	__lock(me);
}

/*
 * genirq core code can issue chip->mask/unmask from atomic context.
 * This doesn't work for slow busses where an access needs to sleep.
 * bus_sync_unlock() is therefore called outside the atomic context,
 * syncs the current irq mask state with the slow external controller
 * and unlocks the bus.
 */

static void max77696_topsys_irq_bus_sync_unlock (struct irq_data *data)
{
	struct max77696_topsys *me = irq_data_get_irq_chip_data(data);
	int rc;

	if (unlikely(me->irq_unmask_new == me->irq_unmask_curr)) {
		goto out;
	}

	rc = max77696_write(me->i2c, GLBLINTM_REG, ~(me->irq_unmask_new));
	if (unlikely(rc)) {
		dev_err(me->dev, "GLBLINTM_REG write error [%d]\n", rc);
		goto out;
	}

	me->irq_unmask_curr = me->irq_unmask_new;

out:
	__unlock(me);
}

static int max77696_topsys_irq_set_type (struct irq_data *data,
		unsigned int type)
{
	struct max77696_topsys *me = irq_data_get_irq_chip_data(data);

	if (unlikely(type & ~(IRQ_TYPE_EDGE_BOTH | IRQ_TYPE_LEVEL_MASK))) {
		dev_err(me->dev, "unsupported irq type %d\n", type);
		return -EINVAL;
	}

	return 0;
}

static int max77696_topsys_irq_set_wake (struct irq_data *data, unsigned int on)
{
	struct max77696_topsys *me = irq_data_get_irq_chip_data(data);
	u8 irq_bit = (u8)(1 << (data->irq - me->irq_base + 1));

	if (on) {
		if (unlikely(!me->irq_wakeup_bitmap)) {
			enable_irq_wake(me->top_irq);
		}
		me->irq_wakeup_bitmap |=  irq_bit;
	} else {
		me->irq_wakeup_bitmap &= ~irq_bit;
		if (unlikely(!me->irq_wakeup_bitmap)) {
			disable_irq_wake(me->top_irq);
		}
	}

	return 0;
}

/* IRQ chip operations
 */
static struct irq_chip max77696_topsys_irq_chip = {
	.name                = DRIVER_NAME,
	//  .flags               = IRQCHIP_SET_TYPE_MASKED,
	.irq_mask            = max77696_topsys_irq_mask,
	.irq_unmask          = max77696_topsys_irq_unmask,
	.irq_bus_lock        = max77696_topsys_irq_bus_lock,
	.irq_bus_sync_unlock = max77696_topsys_irq_bus_sync_unlock,
	.irq_set_type        = max77696_topsys_irq_set_type,
	.irq_set_wake        = max77696_topsys_irq_set_wake,
};

static irqreturn_t max77696_topsys_isr (int irq, void *data)
{
	struct max77696_topsys *me = data;
	u8 interrupted;
	int i;

	max77696_read(me->i2c, GLBLINT_REG, &interrupted);
#if 0 
	pr_info("GLBLINT_REG %02X\n", interrupted);
#endif 
#if defined(CONFIG_FALCON)
	if(in_falcon()){
		printk(KERN_DEBUG "GLBLINT_REG %02X\n", interrupted);
	}
#endif

	for (i = 0; i < NR_TOPSYS_IRQ; i++) {
		u8 irq_bit = (1 << (i + 1));

		if (unlikely(!(me->irq_unmask_new & irq_bit))) {
			/* if the irq is disabled, then ignore a below process */
			continue;
		}

		if (likely(interrupted & irq_bit)) {
			handle_nested_irq(me->irq_base + i);
		}
	}

	return IRQ_HANDLED;
}

static void sw_partialrestart_cb(void *obj, void *param)
{
	struct max77696_topsys *me = (struct max77696_topsys *)obj;
	
	//Power actions only happen on bit transitions from 0 to 1. Write 0 
	//first to esnure that the transition happens - even if the register
	//is in an unexpected state.
	max77696_write(me->i2c, GLBLCNFG0_REG, 0);
	max77696_write(me->i2c, GLBLCNFG0_REG, GLBLCNFG0_PRSTRT | GLBLCNFG0_SFTPDRR);
}

static void sw_fullshutdown_cb(void *obj, void *param)
{
	struct max77696_topsys *me = (struct max77696_topsys *)obj;

	max77696_write(me->i2c, GLBLCNFG0_REG, 0);
	/* Note: SFTPDDR is set to 0 to not reset PMIC registers during full-shutdown
	 *       this is the only way to retain PMIC GPIO state during full-shutdown.
	 */
	max77696_write(me->i2c, GLBLCNFG0_REG, GLBLCNFG0_FSHDN);
}

static void sw_factoryship_cb(void *obj, void *param)
{
	struct max77696_topsys *me = (struct max77696_topsys *)obj;

	max77696_write(me->i2c, GLBLCNFG0_REG, 0);
	max77696_write(me->i2c, GLBLCNFG0_REG, GLBLCNFG0_FSENT | GLBLCNFG0_SFTPDRR);
}

void max77696_chip_poweroff(void)
{
	pmic_event_callback(EVENT_SW_FACTORYSHIP);
}

void max77696_chip_hibernate(void)
{
	pmic_event_callback(EVENT_SW_FULLSHUTDWN);	
}

void max77696_chip_restart(char mode, const char *cmd)
{
	pmic_event_callback(EVENT_SW_PARTIALRESTART);
}

int max77696_topsys_init (struct max77696_chip *chip,
		struct max77696_platform_data* pdata)
{
	struct max77696_topsys *me;
	int i, rc;
	unsigned int irq;
	u8 gintr = 0;

	me = kzalloc(sizeof(*me), GFP_KERNEL);
	if (unlikely(!me)) {
		dev_err(chip->dev, "out of memory (%uB requested)\n", sizeof(*me));
		return -ENOMEM;
	}

	mutex_init(&(me->lock));
	me->chip = chip;
	me->i2c  = __get_i2c(chip);
	me->dev  = chip->dev;
	me->kobj = &(chip->dev->kobj);

	me->irq_base = chip->irq_base + MAX77696_ROOTINT_NR_IRQS;
	me->top_irq  = chip->irq_base + MAX77696_ROOTINT_TOPSYS;

	/* Set PMIC power off */
	pm_power_off = max77696_chip_poweroff;
	pm_restart = max77696_chip_restart;
	pm_power_hibernate = max77696_chip_hibernate;

	/* Set STBY enable */
	max77696_topsys_reg_write_masked(me, GLBLCNFG1, GLBLCNFG1_STBYEN_MASK, 1);

	/* Configure critbat settings (LBYST=200mv & LBDAC=3.2V */
	max77696_topsys_reg_write_masked(me, GLBLCNFG3, (GLBLCNFG3_LBHYST_MASK | GLBLCNFG3_LBDAC_MASK), 
				(GLBLCNFG3_LBDAC_3_2V | GLBLCNFG3_LBHYST_200mV));

	/* Disable all TOPSYS interrupts */
	max77696_write(me->i2c, GLBLINTM_REG, 0xFF);

	/* Clear all TOPSYS interrupts */
	max77696_read(me->i2c, GLBLINT_REG, &gintr);

	/* Register all TOPSYS interrupts */
	for (i = 0; i < NR_TOPSYS_IRQ; i++) {
		irq = me->irq_base + i;

		irq_set_chip_data       (irq, me);
		irq_set_chip_and_handler(irq, &max77696_topsys_irq_chip, handle_level_irq);	
		irq_set_nested_thread   (irq, 1);

#ifdef CONFIG_ARM
		/*
		 * ARM needs us to explicitly flag the IRQ as VALID,
		 * once we do so, it will also set the noprobe.
		 */
		set_irq_flags  (irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	rc = request_threaded_irq(me->top_irq,
			NULL, max77696_topsys_isr, IRQF_ONESHOT, DRIVER_NAME, me);

	if (unlikely(rc < 0)) {
		dev_err(me->dev,
				"failed to request IRQ(%d) [%d]\n", me->top_irq, rc);
		goto out_err_req_irq;
	}

	BUG_ON(chip->topsys_ptr);
	chip->topsys_ptr = me;

	rc = max77696_eventhandler_register(&max77696_en0rise_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_en0rise_handle.event_id, rc);
		goto out_err_reg_en0rise;
	}

	rc = max77696_eventhandler_register(&max77696_en0fall_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_en0fall_handle.event_id, rc);
		goto out_err_reg_en0fall;
	}

	rc = max77696_eventhandler_register(&max77696_critbat_handle, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
				max77696_critbat_handle.event_id, rc);
		goto out_err_reg_critbat;
	}

	rc = max77696_eventhandler_register(&max77696_sw_partialrstrt_hdl, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
			max77696_sw_partialrstrt_hdl.event_id, rc);
		goto out_err_reg_sw_partialrstrt;
	}

	rc = max77696_eventhandler_register(&max77696_sw_fullshtdwn_hdl, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
			max77696_sw_fullshtdwn_hdl.event_id, rc);
		goto out_err_reg_sw_fullshtdwn;
	}

	rc = max77696_eventhandler_register(&max77696_sw_factoryship_hdl, me);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
			max77696_sw_factoryship_hdl.event_id, rc);
		goto out_err_reg_sw_fship;
	}

	sw_partialrstrt.param = me;
	sw_partialrstrt.func = sw_partialrestart_cb;
	rc = pmic_event_subscribe(max77696_sw_partialrstrt_hdl.event_id, &sw_partialrstrt);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
			max77696_sw_fullshtdwn_hdl.event_id, rc);
		goto out_err_subscribe_restart;
	}

	sw_fullshtdwn.param = me;
	sw_fullshtdwn.func = sw_fullshutdown_cb;
	rc = pmic_event_subscribe(max77696_sw_fullshtdwn_hdl.event_id, &sw_fullshtdwn);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
			max77696_sw_fullshtdwn_hdl.event_id, rc);
		goto out_err_subscribe_shutdown;
	}

	sw_factoryship.param = me;
	sw_factoryship.func = sw_factoryship_cb;
	rc = pmic_event_subscribe(max77696_sw_factoryship_hdl.event_id, &sw_factoryship);
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register event[%d] handle with err [%d]\n", \
			max77696_sw_factoryship_hdl.event_id, rc);
		goto out_err_subscribe_fship;
	}

	pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
	return 0;

out_err_subscribe_fship:
	pmic_event_unsubscribe(max77696_sw_fullshtdwn_hdl.event_id, &sw_fullshtdwn);
out_err_subscribe_shutdown:
	pmic_event_unsubscribe(max77696_sw_partialrstrt_hdl.event_id, &sw_partialrstrt);
out_err_subscribe_restart:
	max77696_eventhandler_unregister(&max77696_sw_factoryship_hdl);
out_err_reg_sw_fship:
	max77696_eventhandler_unregister(&max77696_sw_fullshtdwn_hdl);
out_err_reg_sw_fullshtdwn:
	max77696_eventhandler_unregister(&max77696_sw_partialrstrt_hdl);
out_err_reg_sw_partialrstrt:
	max77696_eventhandler_unregister(&max77696_critbat_handle);
out_err_reg_critbat:
	max77696_eventhandler_unregister(&max77696_en0fall_handle);
out_err_reg_en0fall:
	max77696_eventhandler_unregister(&max77696_en0rise_handle);
out_err_reg_en0rise:
	free_irq(me->top_irq, me);
	for (i = 0; i < NR_TOPSYS_IRQ; i++) {
		irq = me->irq_base + i;
		irq_set_handler  (irq, NULL);
		irq_set_chip_data(irq, NULL);
	}
out_err_req_irq:
	mutex_destroy(&(me->lock));
	kfree(me);
	return rc;
}

void max77696_topsys_exit (struct max77696_chip *chip)
{
	struct max77696_topsys *me = chip->topsys_ptr;
	int i;
	unsigned int irq;

	chip->topsys_ptr = NULL;

	max77696_eventhandler_unregister(&max77696_critbat_handle);
	max77696_eventhandler_unregister(&max77696_en0fall_handle);
	max77696_eventhandler_unregister(&max77696_en0rise_handle);

	free_irq(me->top_irq, me);
	for (i = 0; i < NR_TOPSYS_IRQ; i++) {
		irq = me->irq_base + i;
		irq_set_handler  (irq, NULL);
		irq_set_chip_data(irq, NULL);
	}
	mutex_destroy(&(me->lock));
	kfree(me);
}

/*******************************************************************************
 * EXTERNAL FUNCTIONS
 ******************************************************************************/

extern struct max77696_chip* max77696;

#define __max77696_topsys_global_config(_cfg_reg, _cfg_item, _cfg_val) \
	({\
	 struct max77696_chip *__chip = max77696;\
	 struct max77696_topsys *__me = __chip->topsys_ptr;\
	 int __rc;\
	 __lock(__me);\
	 __rc = max77696_topsys_reg_set_bit(__me,\
		 _cfg_reg, _cfg_item, (u8)(_cfg_val));\
	 __unlock(__me);\
	 if (unlikely(__rc)) {\
	 dev_err(__me->dev, ""#_cfg_reg" write error [%d]\n", __rc);\
	 }\
	 __rc;\
	 })

/* Get EN0 status */
bool max77696_topsys_get_glblen0_status(void)
{
	int rc = 0;
	struct max77696_chip *chip = max77696;
	struct max77696_topsys *me = chip->topsys_ptr;
	u8 en0sts = 0;
	bool pressed = false;

	__lock(me);
	rc = max77696_topsys_reg_get_bit(me, GLBLSTAT, EN0_S, &en0sts);
	if (unlikely(rc)) {
		dev_err(me->dev, "TOPSYS EN0_S read write error [%d]\n", rc);
		goto out;
	}
	
	if (en0sts)
		pressed = true;
out:
	__unlock(me);
	return pressed;
}
EXPORT_SYMBOL(max77696_topsys_get_glblen0_status);

/* Global Low-Power Mode */
int max77696_topsys_set_global_lp_mode (bool level)
{
	return __max77696_topsys_global_config(GLBLCNFG1, GLBL_LPM, !!level);
}
EXPORT_SYMBOL(max77696_topsys_set_global_lp_mode);

/* Enable manual reset */
int max77696_topsys_enable_mr (bool enable)
{
	return __max77696_topsys_global_config(GLBLCNFG1, MREN, !!enable);
}
EXPORT_SYMBOL(max77696_topsys_enable_mr);

/* Set manual reset time */
int max77696_topsys_set_mr_time (unsigned int seconds)
{
	u8 mrt;

	if (seconds > 10) {
		mrt = 7;
	} else if (seconds > 8) {
		mrt = 6;
	} else if (seconds > 6) {
		mrt = 5;
	} else if (seconds > 5) {
		mrt = 4;
	} else if (seconds > 4) {
		mrt = 3;
	} else if (seconds > 3) {
		mrt = 2;
	} else if (seconds > 2) {
		mrt = 1;
	} else {
		mrt = 0;
	}

	return __max77696_topsys_global_config(GLBLCNFG1, MRT, mrt);
}
EXPORT_SYMBOL(max77696_topsys_set_mr_time);

/* Enable EN0 delay */
int max77696_topsys_enable_en0_delay (bool enable)
{
	return __max77696_topsys_global_config(GLBLCNFG1, EN0DLY, !!enable);
}
EXPORT_SYMBOL(max77696_topsys_enable_en0_delay);

/* Enable Standby */
int max77696_topsys_enable_standy (bool enable)
{
	return __max77696_topsys_global_config(GLBLCNFG1, STBYEN, !!enable);
}
EXPORT_SYMBOL(max77696_topsys_enable_standy);

/* Enable manual reset wakeup */
int max77696_topsys_enable_mr_wakeup (bool enable)
{
	return __max77696_topsys_global_config(GLBLCNFG2, MROWK, !!enable);
}
EXPORT_SYMBOL(max77696_topsys_enable_mr_wakeup);

/* Enable the system watchdog timer */
int max77696_topsys_enable_wdt (bool enable)
{
	return __max77696_topsys_global_config(GLBLCNFG2, WDTEN, !!enable);
}
EXPORT_SYMBOL(max77696_topsys_enable_wdt);

/* Set the system watchdog timer period */
int max77696_topsys_set_wdt_period (unsigned int seconds)
{
	return __max77696_topsys_global_config(GLBLCNFG2, TWD,
			((seconds > 64)? 3 : ((seconds > 16)? 2 : ((seconds > 2)? 1 : 0))));
}
EXPORT_SYMBOL(max77696_topsys_set_wdt_period);

/* Wakeup on any RTC alarm */
int max77696_topsys_enable_rtc_wakeup (bool enable)
{
	return __max77696_topsys_global_config(GLBLCNFG2, RTCAWK, !!enable);
}
EXPORT_SYMBOL(max77696_topsys_enable_rtc_wakeup);

/* Automatic Wakeup Due to System Watchdog Reset */
int max77696_topsys_enable_wdt_wakeup (bool enable)
{
	return __max77696_topsys_global_config(GLBLCNFG2, WDWK, !!enable);
}
EXPORT_SYMBOL(max77696_topsys_enable_wdt_wakeup);

/* UIC Wakeup Signal is Edge Triggered */
int max77696_topsys_enable_uic_edge_wakeup (bool enable)
{
	return __max77696_topsys_global_config(GLBLCNFG2, UICWK_EDGE, !!enable);
}
EXPORT_SYMBOL(max77696_topsys_enable_uic_edge_wakeup);

/* Set Low-Battery Comparator Hysteresis */
int max77696_topsys_set_lbhyst (unsigned int mV)
{
	u8 lbhyst;

	if (mV > 400) {
		lbhyst = 3;
	} else if (mV > 300) {
		lbhyst = 2;
	} else if (mV > 200) {
		lbhyst = 1;
	} else {
		lbhyst = 0;
	}

	return __max77696_topsys_global_config(GLBLCNFG3, LBDAC, lbhyst);
}
EXPORT_SYMBOL(max77696_topsys_set_lbhyst);

/* Set Low-Battery DAC Falling Threshold */
int max77696_topsys_set_lbdac (unsigned int mV)
{
	u8 lbdac;

	if (mV > 3300) {
		lbdac = 7;
	} else if (mV > 3200) {
		lbdac = 6;
	} else if (mV > 3100) {
		lbdac = 5;
	} else if (mV > 3000) {
		lbdac = 4;
	} else if (mV > 2900) {
		lbdac = 3;
	} else if (mV > 2800) {
		lbdac = 2;
	} else if (mV > 2700) {
		lbdac = 1;
	} else {
		lbdac = 0;
	}

	return __max77696_topsys_global_config(GLBLCNFG3, LBHYST, lbdac);
}
EXPORT_SYMBOL(max77696_topsys_set_lbdac);

/* Clear the system watchdog timer */
int max77696_topsys_clear_wdt (void)
{
	return __max77696_topsys_global_config(GLBLCNFG4, WDTC, 1);
}
EXPORT_SYMBOL(max77696_topsys_clear_wdt);

/* Set nRSTOUT Programmable Delay Timer */
int max77696_topsys_set_rso_delay (unsigned int time_us)
{
	u8 rso_delay;

	if (time_us > 40960) {
		rso_delay = 3;
	} else if (time_us > 10240) {
		rso_delay = 2;
	} else if (time_us >  1280) {
		rso_delay = 1;
	} else {
		rso_delay = 0;
	}

	return __max77696_topsys_global_config(GLBLCNFG5, NRSO_DEL, rso_delay);
}
EXPORT_SYMBOL(max77696_topsys_set_rso_delay);

