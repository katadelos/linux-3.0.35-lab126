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
#include <max77696_registers.h>

#include <llog.h>

#define DRIVER_DESC    "MAX77696 Top Level Interrupt"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_IRQ_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#ifdef CONFIG_FALCON
extern int in_falcon(void);
#endif

#ifdef VERBOSE
#define dev_noise(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_noise(args...) do { } while (0)
#endif /* VERBOSE */

/* Top level IRQ groups */
struct max77696_irq_root_group {
    u8 status_reg;  /* INT status register address */
    u8 mask_reg;    /* INT mask register address   */
    u8 valid_bits;  /* INT register's valid bits   */
};

static const struct max77696_irq_root_group max77696_irq_root_groups[] = {
    [0] = {
        .status_reg = INTTOP1_REG,
        .mask_reg   = INTTOP1M_REG,
        .valid_bits = BITS(7,0),
    },
    [1] = {
        .status_reg = INTTOP2_REG,
        .mask_reg   = INTTOP2M_REG,
        .valid_bits = BITS(7,4)
    },
};

#define NR_ROOT_GRP  ARRAY_SIZE(max77696_irq_root_groups)

#define __get_i2c(chip)     (&((chip)->pmic_i2c))
#define __lock(me)          mutex_lock(&((me)->lock))
#define __unlock(me)        mutex_unlock(&((me)->lock))

/* Top level IRQ definitions */
struct max77696_irq_root_irq {
    u8   group_id;   /* 0 .. NR_ROOT_GRP - 1 */
    u8   bit_mask;   /* status and mask register have same bit masks */
};

#define __grp_of_int(root_irq) \
        (max77696_irq_root_irqs[root_irq].group_id)
#define __mask_of_int(root_irq) \
        (max77696_irq_root_irqs[root_irq].bit_mask)

static const struct max77696_irq_root_irq max77696_irq_root_irqs[] = {
    #define ROOTIRQ_DEF(_id, _group_id, _bit_mask) \
            [_id] = {\
                .group_id = _group_id,\
                .bit_mask = _bit_mask,\
            }
    /* Interrupt Root Group #0 */
    ROOTIRQ_DEF(MAX77696_ROOTINT_TOPSYS, 0,  TOPSYSINT_M),
    ROOTIRQ_DEF(MAX77696_ROOTINT_BUCK,   0,  BUCKINT_M  ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_FG,     0,  FGINT_M    ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_GPIO,   0,  GPIOINT_M  ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_RTC,    0,  RTCINT_M   ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_CHGA,   0,  CHGAINT_M  ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_LDO,    0,  LDOINT_M   ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_UIC,    0,  UICINT_M   ),
    /* Interrupt Root Group #1 */
    ROOTIRQ_DEF(MAX77696_ROOTINT_ADC,    1,  ADCINT_M   ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_WLED,   1,  WLEDINT_M  ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_EPD,    1,  EPDINT_M   ),
    ROOTIRQ_DEF(MAX77696_ROOTINT_CHGB,   1,  CHGBINT_M  ),
};

#define NR_ROOT_IRQ  MAX77696_ROOTINT_NR_IRQS

struct max77696_irq {
    struct max77696_chip *chip;
    struct max77696_i2c  *i2c;
    struct device        *dev;
    struct kobject       *kobj;

    u16                   ercflag;
    u8                    fgirq_unmask;

    u8                    grp_status[NR_ROOT_GRP];
    u8                    grp_unmask_new[NR_ROOT_GRP];  /* unmask value buffer writting */
    u8                    grp_unmask_curr[NR_ROOT_GRP]; /* backup of current unmask value */

    unsigned long         wakeup_bitmap[BITS_TO_LONGS(NR_ROOT_IRQ)];
    struct mutex          lock;
};

static u16  max77696_irq_ercflag;
static bool max77696_irq_ercflag_set = 0;

static __init int max77696_irq_setup_ercflag (char *str)
{
    /* ercflag0, ercflag1 */
    int ercflag[3];

    get_options(str, 3, ercflag);

    max77696_irq_ercflag     = ((u16)ercflag[2] << 8) | ercflag[1];
    max77696_irq_ercflag_set = 1;

    return 1;
}
__setup("max77696_ercflag=", max77696_irq_setup_ercflag);

static __always_inline
int max77696_irq_write_root_mask (struct max77696_irq *me, int grp, bool force)
{
    bool ignore
        = (!force && me->grp_unmask_curr[grp] == me->grp_unmask_new[grp]);
    int rc;

    if (unlikely(ignore)) {
        rc = 0;
        goto out;
    }

    rc = max77696_write_masked(me->i2c, max77696_irq_root_groups[grp].mask_reg,
        max77696_irq_root_groups[grp].valid_bits, ~(me->grp_unmask_new[grp]));

    if (unlikely(rc)) {
        dev_err(me->dev, "INTTOP%dM write error %d\n", grp, rc);
        goto out;
    }

    me->grp_unmask_curr[grp] = me->grp_unmask_new[grp];

out:
    return rc;
}

#define max77696_irq_set_root_mask(me, grp, __mask) \
        ((me)->grp_unmask_new[grp] &= (u8)(~(__mask)))
#define max77696_irq_clr_root_mask(me, grp, __mask) \
        ((me)->grp_unmask_new[grp] |= (u8)( (__mask)))

static __always_inline
int max77696_irq_read_root_status (struct max77696_irq *me, int grp)
{
    int rc;

    rc = max77696_read_masked(me->i2c, max77696_irq_root_groups[grp].status_reg,
        max77696_irq_root_groups[grp].valid_bits, &(me->grp_status[grp]));

    if (unlikely(rc)) {
        dev_err(me->dev, "INTTOP%d read error %d\n", grp, rc);
        me->grp_status[grp] = 0x00;
    }

    return rc;
}

#define max77696_irq_is_root_irq_disabled(me, root_irq) \
        (!((me)->grp_unmask_new[__grp_of_int(root_irq)] &\
           __mask_of_int(root_irq)))

#define max77696_irq_is_root_irq_enabled(me, root_irq) \
        (!max77696_irq_is_root_irq_disabled(me, root_irq))

#define max77696_irq_disable_root_irq(me, root_irq) \
        do {\
            dev_dbg((me)->dev, "root_irq %d: disabled\n", root_irq);\
            max77696_irq_set_root_mask(me,\
                __grp_of_int(root_irq), __mask_of_int(root_irq));\
        } while (0)

#define max77696_irq_enable_root_irq(me, root_irq) \
        do {\
            dev_dbg((me)->dev, "root_irq %d: enabled\n", root_irq);\
            max77696_irq_clr_root_mask(me,\
                __grp_of_int(root_irq), __mask_of_int(root_irq));\
        } while (0)

#define max77696_irq_is_root_irq_generated(me, root_irq) \
        (!!((me)->grp_status[__grp_of_int(root_irq)] &\
            __mask_of_int(root_irq)))

static __always_inline
bool max77696_irq_is_root_irq_empty (struct max77696_irq *me)
{
    int i;

    for (i = 0; i < NR_ROOT_GRP; i++) {
        if (me->grp_unmask_new[i] & max77696_irq_root_groups[i].valid_bits) {
            return 0;
        }
    }

    return 1; /* no root_irq is enabled */
}

static void max77696_irq_mask (struct irq_data *data)
{
    struct max77696_irq *me = irq_data_get_irq_chip_data(data);
    unsigned int irq = data->irq - me->chip->irq_base;

    if (unlikely(max77696_irq_is_root_irq_disabled(me, irq))) {
        /* already disabled */
        return;
    }

    max77696_irq_disable_root_irq(me, irq);

    /* disable topsys_irq if no root_irq enabled */
    if (unlikely(max77696_irq_is_root_irq_empty(me))) {
        max77696_irq_disable_root_irq(me, MAX77696_ROOTINT_TOPSYS);
    }
}

static void max77696_irq_unmask (struct irq_data *data)
{
    struct max77696_irq *me = irq_data_get_irq_chip_data(data);
    unsigned int irq = data->irq - me->chip->irq_base;

    if (unlikely(max77696_irq_is_root_irq_enabled(me, irq))) {
        /* already enabled */
        return;
    }

    /* enable topsys_irq if root_irq will be enabled first time */
    if (unlikely(max77696_irq_is_root_irq_empty(me))) {
        max77696_irq_enable_root_irq(me, MAX77696_ROOTINT_TOPSYS);
    }

    max77696_irq_enable_root_irq(me, irq);
}

static void max77696_irq_bus_lock (struct irq_data *data)
{
	struct max77696_irq *me = irq_data_get_irq_chip_data(data);

	__lock(me);
}

/*
 * genirq core code can issue chip->mask/unmask from atomic context.
 * This doesn't work for slow busses where an access needs to sleep.
 * bus_sync_unlock() is therefore called outside the atomic context,
 * syncs the current irq mask state with the slow external controller
 * and unlocks the bus.
 */

static void max77696_irq_bus_sync_unlock (struct irq_data *data)
{
	struct max77696_irq *me = irq_data_get_irq_chip_data(data);
	int i;

    for (i = 0; i < NR_ROOT_GRP; i++) {
        max77696_irq_write_root_mask(me, i, 0);
    }

	__unlock(me);
}

static int max77696_irq_set_type (struct irq_data *data, unsigned int type)
{
    struct max77696_irq *me = irq_data_get_irq_chip_data(data);

	if (unlikely(type & ~(IRQ_TYPE_EDGE_BOTH | IRQ_TYPE_LEVEL_MASK))) {
		dev_err(me->dev, "unsupported irq type %d\n", type);
		return -EINVAL;
    }

    return 0;
}

static int max77696_irq_set_wake (struct irq_data *data, unsigned int on)
{
    struct max77696_irq *me = irq_data_get_irq_chip_data(data);
    unsigned int irq = data->irq - me->chip->irq_base;

    if (on) {
        if (unlikely(bitmap_empty(me->wakeup_bitmap, NR_ROOT_IRQ))) {
            device_set_wakeup_enable(me->dev, 1);
        }
        set_bit  (irq, me->wakeup_bitmap);
    } else {
        clear_bit(irq, me->wakeup_bitmap);
        if (unlikely(bitmap_empty(me->wakeup_bitmap, NR_ROOT_IRQ))) {
            device_set_wakeup_enable(me->dev, 0);
        }
    }

    return 0;
}

/* IRQ chip operations
 */
static struct irq_chip max77696_irq_chip = {
    .name                = DRIVER_NAME,
//  .flags               = IRQCHIP_SET_TYPE_MASKED,
    .irq_mask            = max77696_irq_mask,
    .irq_unmask          = max77696_irq_unmask,
    .irq_bus_lock        = max77696_irq_bus_lock,
    .irq_bus_sync_unlock = max77696_irq_bus_sync_unlock,
    .irq_set_type        = max77696_irq_set_type,
    .irq_set_wake        = max77696_irq_set_wake,
};

static irqreturn_t max77696_irq_thread (int irq, void *data)
{
    struct max77696_irq *me = data;
    int i;

    /* Read TOP IRQ status */
    for (i = 0; i < NR_ROOT_GRP; i++) {
        max77696_irq_read_root_status(me, i);
    }

#ifdef DEBUG
    for (i = 0; i < NR_ROOT_GRP; i++) {
        dev_dbg(me->dev, "top_int status[%d] %02X\n", i, me->grp_status[i]);
    }
#endif /* DEBUG */

#if defined(CONFIG_FALCON) && !defined(DEBUG)
	if(in_falcon()){
		for (i = 0; i < NR_ROOT_GRP; i++) {
			printk(KERN_DEBUG "top_int status[%d] %02X\n", i, me->grp_status[i]);
		}
	}
#endif

    /* Check IRQ bits */
    for (i = 0; i < NR_ROOT_IRQ; i++) {
        if (unlikely(max77696_irq_is_root_irq_disabled(me, i))) {
            /* if the irq is disabled, then ignore a below process */
            continue;
        }

        if (likely(max77696_irq_is_root_irq_generated(me, i))) {
            dev_dbg(me->dev, "handle_nested_irq(%d)\n", me->chip->irq_base + i);
            handle_nested_irq(me->chip->irq_base + i);
        }
    }

	return IRQ_HANDLED;
}

static const char *ercflag_desc[][3] = {
    { "WDPMIC_FSHDN",   "PMIC System Watchdog Full Shutdown"         , "boot: C def:rst:reset=hard"},
    { "WDPMIC_FRSTRT",  "PMIC System Watchdog Full Restart"          , "boot: C def:rst:reset=hard"},
    { "MR_FSHDN",       "Manual Reset Full Shutdown"                 , "boot: C def:rst:reset=hard"},
    { "MR_FRSTRT",      "Manual Reset Full Restart"                  , "boot: C def:bcut:batterycut=1"},
    { "SFT_PSHDN",      "Software Partial Shutdown"                  , "boot: C def:rst:reset=hard"},
    { "SFT_PRSTRT",     "Software Partial Restart"                   , ""},
    { "SFT_FSHDN",      "Software Full Shutdown"                     , "boot: C def:rst:reset=hard"},
    { "SFT_FRSTRT",     "Software Full Restart"                      , "boot: C def:rst:reset=hard"},
    { "LBMOK_FSHDN",    "Low-Battery Monitor Not Okay Full Shutdown" , "boot: C def:rst:reset=hard"},
    { "SYS1UVLO_FSHDN", "System 1 Undervoltage Full Shutdown"        , "boot: C def:rst:reset=hard"},
    { "TOVLO_FSHDN",    "Thermal Overload Full Shutdown"             , "boot: C def:rst:reset=hard"},
    { "RSTIN_PRSTRT",   "nRSTIN Partial Restart - likely watchdog"   , "boot: C def:rst:reset=watchdog"},
};

static ssize_t max77696_irq_ercflag_show (struct device *dev,
    struct device_attribute *devattr, char *buf)
{
    struct max77696_chip *chip = dev_get_drvdata(dev);
    struct max77696_irq *me = chip->irq_ptr;
    int i, ofst;
    bool printed;

    __lock(me);

    ofst  = (int)snprintf(buf,      PAGE_SIZE, "Event flags: 0x%04X\n",
        me->ercflag);

    printed = false;

    for (i = 0; i<ARRAY_SIZE(ercflag_desc); i++) {
        if (me->ercflag & (1 << i)) {
            ofst += (int)snprintf(buf+ofst, PAGE_SIZE,
                "  %-14s %s\n", ercflag_desc[i][0], ercflag_desc[i][1]);

            printed = true;
        }
    }

    if (!printed) {
        ofst += (int)snprintf(buf+ofst, PAGE_SIZE,
            "  %-14s %s\n", "None", "First Boot, Dead Battery or Hardware Fault");
    }

    __unlock(me);
    return (ssize_t)ofst;
}
static DEVICE_ATTR(ercflag, S_IRUGO, max77696_irq_ercflag_show, NULL);

static void max77696_irq_ercflag_logprint(struct max77696_irq *me) {
    int i;
    bool printed = false;

    printk("Reboot Reason - Metric Framework Reporting\n");
    for (i = 0; i < ARRAY_SIZE(ercflag_desc); i++) {
        if (me->ercflag & (1<<i) ) {
            LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY,  DEVICE_METRIC_TYPE_COUNTER, "event_recorder", "ercflags", ercflag_desc[i][0], 1, "");
            printed = true;
        }
    }
    if (!printed) {
        LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER, "event_recorder", "ercflags", "ercflag_empty", 1, "");
    }

    printed = false;
    printk("Reboot Reason - Legacy Metric Reporting\n");
    for (i = 0; i < ARRAY_SIZE(ercflag_desc); i++) {
        if (me->ercflag & (1<<i) ) {
            printk(KERN_ERR "%s\n", ercflag_desc[i][2]);
            printed = true;
        }
    }
    if (!printed) {
        printk(KERN_ERR "No Legacy reboot reason code to print\n");
    }
}

static struct attribute *max77696_irq_attr[] = {
    &dev_attr_ercflag.attr,
    NULL
};

static const struct attribute_group max77696_irq_attr_group = {
    .attrs = max77696_irq_attr,
};

/* Initialize and reqgister IRQ channel on driver probe
 */
int max77696_irq_init (struct max77696_chip *chip,
    struct max77696_platform_data* pdata)
{
    struct max77696_irq *me;
    u8 ercflag[2];
    int i, rc;

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

    /* Disable all root interrupts */
    for (i = 0; i < NR_ROOT_GRP; i++) {
        max77696_irq_set_root_mask  (me, i, 0xFF);
        max77696_irq_write_root_mask(me, i, 1);
    }

    /* Read event recorder */
    max77696_bulk_read(me->i2c, ERCFLAG0_REG, ercflag, 2);
    me->ercflag = (max77696_irq_ercflag_set?
        max77696_irq_ercflag : ((u16)ercflag[1] << 8) | ercflag[0]);
    max77696_irq_ercflag_logprint(me);

    rc = sysfs_create_group(me->kobj, &max77696_irq_attr_group);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to create attribute group [%d]\n", rc);
        goto out_err_sysfs;
    }

    /* Register all root interrupts */

    for (i = 0; i < NR_ROOT_IRQ; i++) {
        unsigned int irq = chip->irq_base + i;

        irq_set_chip_data       (irq, me);
        irq_set_chip_and_handler(irq, &max77696_irq_chip, handle_simple_irq);
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

    if (unlikely(!chip->core_irq)) {
        dev_warn(chip->dev, "INTERRUPT DISABLED\n");
        goto out;
    }

    rc = request_threaded_irq(chip->core_irq, NULL, max77696_irq_thread,
        IRQF_ONESHOT|pdata->core_irq_trigger, DRIVER_NAME, me);

    if (unlikely(rc < 0)) {
        dev_err(chip->dev,
            "failed to request core IRQ(%d) [%d]\n", chip->core_irq, rc);
        goto out_err_req_irq;
    }

out:
    BUG_ON(chip->irq_ptr);
    chip->irq_ptr = me;

    return 0;

out_err_req_irq:
    sysfs_remove_group(me->kobj, &max77696_irq_attr_group);
out_err_sysfs:
    mutex_destroy(&(me->lock));
    kfree(me);
    return rc;
}

/* Free IRQ channel on driver exit
 */
void max77696_irq_exit (struct max77696_chip *chip)
{
    struct max77696_irq *me = chip->irq_ptr;
    int i;

    chip->irq_ptr = NULL;

    if (likely(chip->core_irq)) {
      free_irq(chip->core_irq, NULL);
    }

    for (i = 0; i < NR_ROOT_IRQ; i++) {
        unsigned int irq = chip->irq_base + i;

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

bool max77696_irq_test_ercflag (u16 flag)
{
    struct max77696_chip *chip = max77696;
    struct max77696_irq *me = chip->irq_ptr;
    bool rc;

    __lock(me);
    rc = ((me->ercflag & flag) == flag);
    __unlock(me);

    return rc;
}
EXPORT_SYMBOL(max77696_irq_test_ercflag);

#define max77696_irq_write_fgirq_mask(me) \
        do {\
            int _rc = max77696_write((me)->i2c, FGINTM_REG,\
                ~((me)->fgirq_unmask));\
            if (unlikely(_rc)) {\
                dev_err((me)->dev, "FGINTM write error [%d]\n", _rc);\
            }\
        } while (0)

void max77696_irq_enable_fgirq (u8 irq_bits, bool forced)
{
    struct max77696_chip *chip = max77696;
    struct max77696_irq *me = chip->irq_ptr;

    __lock(me);

    if (unlikely(!forced && (me->fgirq_unmask & irq_bits) == irq_bits)) {
        /* already unmasked */
        goto out;
    }

    /* set enabled flag */
    me->fgirq_unmask |= irq_bits;
    max77696_irq_write_fgirq_mask(me);

out:
    __unlock(me);
    return;
}
EXPORT_SYMBOL(max77696_irq_enable_fgirq);

void max77696_irq_disable_fgirq (u8 irq_bits, bool forced)
{
    struct max77696_chip *chip = max77696;
    struct max77696_irq *me = chip->irq_ptr;

    __lock(me);

    if (unlikely(!forced && (me->fgirq_unmask & irq_bits) == 0)) {
        /* already masked */
        goto out;
    }

    /* clear enabled flag */
    me->fgirq_unmask &= ~irq_bits;
    max77696_irq_write_fgirq_mask(me);

out:
    __unlock(me);
    return;
}
EXPORT_SYMBOL(max77696_irq_disable_fgirq);

int max77696_irq_read_fgirq_status (u8 *status)
{
    struct max77696_chip *chip = max77696;
    struct max77696_irq *me = chip->irq_ptr;
    u8 interrupted;
    int rc;

    __lock(me);

    rc = max77696_read(me->i2c, FGINT_REG, &interrupted);
    if (unlikely(rc)) {
        dev_err(me->dev, "FGINT read error [%d]\n", rc);
        goto out;
    }

    *status = (interrupted & me->fgirq_unmask);

out:
    __unlock(me);
    return rc;
}
EXPORT_SYMBOL(max77696_irq_read_fgirq_status);
