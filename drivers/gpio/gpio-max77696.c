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
#include <linux/gpio.h>
#include <linux/seq_file.h>
#include <linux/mfd/max77696.h>
#include <max77696_registers.h>

#define DRIVER_DESC    "MAX77696 GPIO Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_GPIO_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#ifdef VERBOSE
#define dev_noise(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_noise(args...) do { } while (0)
#endif /* VERBOSE */

#define GPIO_NGPIO                   MAX77696_GPIO_NR_GPIOS

#define GPIO_DIR_OUT                 GPIOF_DIR_OUT
#define GPIO_DIR_IN                  GPIOF_DIR_IN

struct max77696_gpio {
    struct max77696_chip *chip;
    struct max77696_i2c  *i2c;
    struct device        *dev;

    unsigned int          irq_base;

    struct gpio_chip      gpio;

    unsigned int          top_irq;
    unsigned int          irq_type[GPIO_NGPIO];
    unsigned long         enabled_irq_bitmap[BITS_TO_LONGS(GPIO_NGPIO)];
    unsigned long         dirty_irqcfg_bitmap[BITS_TO_LONGS(GPIO_NGPIO)];
    unsigned long         wakeup_bitmap[BITS_TO_LONGS(GPIO_NGPIO)];
    struct mutex          lock;
};

#define __to_max77696_gpio(gpio_ptr) \
        container_of(gpio_ptr, struct max77696_gpio, gpio)

#define __get_i2c(chip)                (&((chip)->pmic_i2c))
#define __lock(me)                     mutex_lock(&((me)->lock))
#define __unlock(me)                   mutex_unlock(&((me)->lock))

/* GPIO Register Read/Write */
#define max77696_gpio_reg_read(me, reg, val_ptr) \
        max77696_read((me)->i2c, GPIO_REG(reg), val_ptr)
#define max77696_gpio_reg_write(me, reg, val) \
        max77696_write((me)->i2c, GPIO_REG(reg), val)
#define max77696_gpio_reg_bulk_read(me, reg, dst, len) \
        max77696_bulk_read((me)->i2c, GPIO_REG(reg), dst, len)
#define max77696_gpio_reg_bulk_write(me, reg, src, len) \
        max77696_bulk_write((me)->i2c, GPIO_REG(reg), src, len)
#define max77696_gpio_reg_read_masked(me, reg, mask, val_ptr) \
        max77696_read_masked((me)->i2c, GPIO_REG(reg), mask, val_ptr)
#define max77696_gpio_reg_write_masked(me, reg, mask, val) \
        max77696_write_masked((me)->i2c, GPIO_REG(reg), mask, val)

/* GPIO Register Single Bit Ops */
#define max77696_gpio_reg_get_bit(me, reg, bit, val_ptr) \
        ({\
            int __rc = max77696_gpio_reg_read_masked(me, reg,\
                GPIO_REG_BITMASK(reg, bit), val_ptr);\
            *(val_ptr) = GPIO_REG_BITGET(reg, bit, *(val_ptr));\
            __rc;\
        })
#define max77696_gpio_reg_set_bit(me, reg, bit, val) \
        ({\
            max77696_gpio_reg_write_masked(me, reg,\
                GPIO_REG_BITMASK(reg, bit), GPIO_REG_BITSET(reg, bit, val));\
        })

#define max77696_gpio_read_config(me, gpio, mask, val_ptr) \
        ({\
            int _rc = max77696_read_masked((me)->i2c, GPIO_CNFG_GPIO_REG(gpio),\
                mask, val_ptr);\
            if (unlikely(_rc)) {\
                dev_err((me)->dev,\
                    "failed read GPIO%d config [%d]\n", gpio, _rc);\
            }\
            _rc;\
        })

#define max77696_gpio_write_config(me, gpio, mask, val) \
        ({\
            int _rc;\
            dev_noise((me)->dev, "GPIO%d new config - mask %02X val %02X\n",\
                gpio, mask, val);\
            _rc = max77696_write_masked((me)->i2c,\
                GPIO_CNFG_GPIO_REG(gpio), mask, val);\
            if (unlikely(_rc)) {\
                dev_err((me)->dev,\
                    "failed write GPIO%d config [%d]\n", gpio, _rc);\
            }\
            _rc;\
        })

static int max77696_gpio_set_dir (struct max77696_gpio *me,
    unsigned gpio, unsigned dir, unsigned level)
{
    u8 mask, val;
    int rc;

    mask = GPIO_REG_BITMASK(CNFG_GPIO, DIR);
    val  = GPIO_REG_BITSET (CNFG_GPIO, DIR, !!dir);

    if (dir == GPIO_DIR_OUT) {
        mask |= GPIO_REG_BITMASK(CNFG_GPIO, DO);
        val  |= GPIO_REG_BITSET (CNFG_GPIO, DO, !!level);
    }

    rc = max77696_gpio_write_config(me, gpio, mask, val);
    if (unlikely(rc)) {
        dev_err(me->dev, "CNFG_GPIO%d write error [%d]\n", gpio, rc);
    }

    return rc;
}

#define __is_input(cfg_val) (!!GPIO_REG_BITGET(CNFG_GPIO, DIR, cfg_val))

static int max77696_gpio_get_status (struct max77696_gpio *me, unsigned gpio)
{
    u8 val;
    int rc;

    rc = max77696_gpio_read_config(me, gpio, 0xFF, &val);
    if (unlikely(rc)) {
        dev_err(me->dev, "CNFG_GPIO%d read error [%d]\n", gpio, rc);
        goto out;
    }

    if (__is_input(val)) {
        /* When set for GPI (DIR = 1): return DI bit */
        rc = GPIO_REG_BITGET(CNFG_GPIO, DI, val);
    } else {
        /* When set for GPO (DIR = 0): return DO bit */
        rc = GPIO_REG_BITGET(CNFG_GPIO, DO, val);
    }

out:
    return rc;
}

static int max77696_gpio_direction_input (struct gpio_chip *chip,
    unsigned offset)
{
    struct max77696_gpio *me = __to_max77696_gpio(chip);
    int rc;

    __lock(me);
    rc = max77696_gpio_set_dir(me, offset, GPIO_DIR_IN, 0);
    __unlock(me);

    return rc;
}

static int max77696_gpio_get (struct gpio_chip *chip, unsigned offset)
{
    struct max77696_gpio *me = __to_max77696_gpio(chip);
    int rc;

    __lock(me);
    rc = max77696_gpio_get_status(me, offset);
    __unlock(me);

    return rc;
}

static int max77696_gpio_direction_output (struct gpio_chip *chip,
    unsigned offset, int value)
{
    struct max77696_gpio *me = __to_max77696_gpio(chip);
    int rc;

    __lock(me);
    rc = max77696_gpio_set_dir(me, offset, GPIO_DIR_OUT, value);
    __unlock(me);

    return rc;
}

static void max77696_gpio_set (struct gpio_chip *chip,
    unsigned offset, int value)
{
    struct max77696_gpio *me = __to_max77696_gpio(chip);

    __lock(me);
    max77696_gpio_set_dir(me, offset, GPIO_DIR_OUT, value);
    __unlock(me);
}

static int max77696_gpio_to_irq (struct gpio_chip *chip, unsigned offset)
{
    struct max77696_gpio *me = __to_max77696_gpio(chip);

    return (int)(me->irq_base + offset);
}

static void max77696_gpio_dbg_show (struct seq_file *s, struct gpio_chip *chip)
{
    struct max77696_gpio *me = __to_max77696_gpio(chip);
    const char *label, *dir;
    u8 i, cfg, lvl;
    int rc;

    __lock(me);

    for (i = 0; i < GPIO_NGPIO; i++) {
        label = gpiochip_is_requested(chip, i);

        seq_printf(s, " gpio-%-3d (%-20.20s) ",
            chip->base + i, label? label : "--");

        rc = max77696_gpio_read_config(me, i, 0xFF, &cfg);
        if (unlikely(rc)) {
            continue;
        }

        if (__is_input(cfg)) {
            dir = "in";
            lvl = GPIO_REG_BITGET(CNFG_GPIO, DI, cfg);
        } else {
            dir = "out";
            lvl = GPIO_REG_BITGET(CNFG_GPIO, DO, cfg);
        }

        seq_printf(s, "%-5.5s %s 0x%02x\n",
            dir, lvl? "hi" : "lo", cfg);
    }

    __unlock(me);
}

static struct gpio_chip max77696_gpio_chip = {
    .label            = DRIVER_NAME,
    .owner            = THIS_MODULE,
    .direction_input  = max77696_gpio_direction_input,
    .get              = max77696_gpio_get,
    .direction_output = max77696_gpio_direction_output,
    .set              = max77696_gpio_set,
    .to_irq           = max77696_gpio_to_irq,
    .dbg_show         = max77696_gpio_dbg_show,
    .ngpio            = GPIO_NGPIO,
};

static void max77696_gpio_irq_sync_config (struct max77696_gpio *me,
    unsigned int gpio, bool force)
{
    u8 mask, val, refe_val = MAX77696_GPIO_REFEIRQ_NONE;
    int rc;

    if (unlikely(!force && !test_bit(gpio, me->dirty_irqcfg_bitmap))) {
        return;
    }

    /* clear dirty */
    clear_bit(gpio, me->dirty_irqcfg_bitmap);

    /* Setup CNFG register */

    refe_val = MAX77696_GPIO_REFEIRQ_NONE;

    if (likely(test_bit(gpio, me->enabled_irq_bitmap))) {
        if (me->irq_type[gpio] & IRQ_TYPE_EDGE_RISING) {
            refe_val |= MAX77696_GPIO_REFEIRQ_RISING;
        }
        if (me->irq_type[gpio] & IRQ_TYPE_EDGE_FALLING) {
            refe_val |= MAX77696_GPIO_REFEIRQ_FALLING;
        }
    }

    mask = GPIO_REG_BITMASK(CNFG_GPIO, REFE_IRQ);
    val  = GPIO_REG_BITSET (CNFG_GPIO, REFE_IRQ, refe_val);

    rc = max77696_gpio_write_config(me, gpio, mask, val);
    if (unlikely(rc)) {
        dev_err(me->dev, "failed to configure gpio [%d]\n", rc);
    }
}

static void max77696_gpio_irq_mask (struct irq_data *data)
{
    struct max77696_gpio *me = irq_data_get_irq_chip_data(data);
    unsigned int gpio = data->irq - me->irq_base;

    if (unlikely(!test_bit(gpio, me->enabled_irq_bitmap))) {
        /* already masked */
        return;
    }

    /* clear enabled flag */
    clear_bit(gpio, me->enabled_irq_bitmap);

    /* set dirty */
    set_bit(gpio, me->dirty_irqcfg_bitmap);
}

static void max77696_gpio_irq_unmask (struct irq_data *data)
{
    struct max77696_gpio *me = irq_data_get_irq_chip_data(data);
    unsigned int gpio = data->irq - me->irq_base;

    if (unlikely(test_bit(gpio, me->enabled_irq_bitmap))) {
        /* already unmasked */
        return;
    }

    /* set enabled flag */
    set_bit(gpio, me->enabled_irq_bitmap);

    /* set dirty */
    set_bit(gpio, me->dirty_irqcfg_bitmap);
}

static void max77696_gpio_irq_bus_lock (struct irq_data *data)
{
    struct max77696_gpio *me = irq_data_get_irq_chip_data(data);

    __lock(me);
}

/*
 * genirq core code can issue chip->mask/unmask from atomic context.
 * This doesn't work for slow busses where an access needs to sleep.
 * bus_sync_unlock() is therefore called outside the atomic context,
 * syncs the current irq mask state with the slow external controller
 * and unlocks the bus.
 */

static void max77696_gpio_irq_bus_sync_unlock (struct irq_data *data)
{
    struct max77696_gpio *me = irq_data_get_irq_chip_data(data);
    int i;

    disable_irq(me->top_irq);

    for (i = 0; i < GPIO_NGPIO; i++) {
        max77696_gpio_irq_sync_config(me, i, 0);
    }

    if (likely(!bitmap_empty(me->enabled_irq_bitmap, GPIO_NGPIO))) {
        enable_irq(me->top_irq);
    }

    __unlock(me);
}

static int max77696_gpio_irq_set_type (struct irq_data *data, unsigned int type)
{
    struct max77696_gpio *me = irq_data_get_irq_chip_data(data);
    unsigned int gpio = data->irq - me->irq_base;
    int rc = 0;

    if (unlikely(type & ~IRQ_TYPE_EDGE_BOTH)) {
        dev_err(me->dev, "gpio %d: unsupported irq type %d\n", gpio, type);
        rc = -EINVAL;
        goto out;
    }

    if (unlikely(me->irq_type[gpio] == type)) {
        goto out;
    }

    me->irq_type[gpio] = type;
    set_bit(gpio, me->dirty_irqcfg_bitmap);

    if (likely(test_bit(gpio, me->enabled_irq_bitmap))) {
        max77696_gpio_irq_sync_config(me, gpio, 1);
    }

out:
    return rc;
}

static int max77696_gpio_irq_set_wake (struct irq_data *data, unsigned int on)
{
    struct max77696_gpio *me = irq_data_get_irq_chip_data(data);
    unsigned int gpio = data->irq - me->irq_base;

    if (on) {
        if (unlikely(bitmap_empty(me->wakeup_bitmap, GPIO_NGPIO))) {
            enable_irq_wake(me->top_irq);
        }
        set_bit  (gpio, me->wakeup_bitmap);
    } else {
        clear_bit(gpio, me->wakeup_bitmap);
        if (unlikely(bitmap_empty(me->wakeup_bitmap, GPIO_NGPIO))) {
            disable_irq_wake(me->top_irq);
        }
    }

    return 0;
}

static struct irq_chip max77696_gpio_irq_chip = {
    .name                = DRIVER_NAME,
//  .flags               = IRQCHIP_SET_TYPE_MASKED,
    .irq_mask            = max77696_gpio_irq_mask,
    .irq_unmask          = max77696_gpio_irq_unmask,
    .irq_bus_lock        = max77696_gpio_irq_bus_lock,
    .irq_bus_sync_unlock = max77696_gpio_irq_bus_sync_unlock,
    .irq_set_type        = max77696_gpio_irq_set_type,
    .irq_set_wake        = max77696_gpio_irq_set_wake,
};

static irqreturn_t max77696_gpio_isr (int irq, void *data)
{
    struct max77696_gpio *me = data;
    u8 interrupted;
    int i;

    max77696_gpio_reg_read(me, IRQ_LVL2, &interrupted);
    dev_dbg(me->dev, "IRQ_LVL2 %02X\n", interrupted);

    for (i = 0; i < GPIO_NGPIO; i++) {
        if (unlikely(!test_bit(i, me->enabled_irq_bitmap))) {
            /* if the irq is disabled, then ignore a below process */
            continue;
        }

        if (likely(interrupted & (1 << i))) {
            handle_nested_irq(me->irq_base + i);
        }
    }

    return IRQ_HANDLED;
}

static __inline void max77696_gpio_init_cfg (struct max77696_gpio *me,
    int gpio, const struct max77696_gpio_init_data* cfg)
{
    u8 mask, val;

    if (unlikely(cfg->pin_connected == 0)) {
        /* Leave gpio settings as default */
        goto out;
    }

    dev_info(me->dev, "gpio#%d %s %s func-%d %s",
        gpio,
        cfg->pullup_en? "pu" : "--",
        cfg->pulldn_en? "pd" : "--",
        cfg->alter_mode,
        (cfg->direction == MAX77696_GPIO_DIR_OUTPUT)? "out" : "in ");

    /* Setup PUE register */

    mask = (1 << gpio);
    val  = (cfg->pullup_en << gpio);

    max77696_gpio_reg_write_masked(me, PUE_GPIO, mask, val);

    /* Setup PDE register */

    mask = (1 << gpio);
    val  = (cfg->pulldn_en << gpio);

    max77696_gpio_reg_write_masked(me, PDE_GPIO, mask, val);

    /* Setup AME register */

    if (likely(gpio < 4)) {
        mask = (3 << (gpio << 1));
        val  = (cfg->alter_mode << gpio);

        max77696_gpio_reg_write_masked(me, AME_GPIO, mask, val);
    }

    /* Setup CNFG register */

    mask = GPIO_REG_BITMASK(CNFG_GPIO, DIR);
    val  = GPIO_REG_BITSET (CNFG_GPIO, DIR, cfg->direction);

    if (cfg->direction == MAX77696_GPIO_DIR_OUTPUT) {
        mask |= GPIO_REG_BITMASK(CNFG_GPIO, PPDRV);
        val  |= GPIO_REG_BITSET (CNFG_GPIO, PPDRV, cfg->u.output.out_cfg);
        mask |= GPIO_REG_BITMASK(CNFG_GPIO, DO);
        val  |= GPIO_REG_BITSET (CNFG_GPIO, DO, cfg->u.output.drive);
    } else {
        mask |= GPIO_REG_BITMASK(CNFG_GPIO, DBNC);
        val  |= GPIO_REG_BITSET (CNFG_GPIO, DBNC, cfg->u.input.debounce);
        mask |= GPIO_REG_BITMASK(CNFG_GPIO, REFE_IRQ);
        val  |= GPIO_REG_BITSET (CNFG_GPIO, REFE_IRQ, cfg->u.input.refe_irq);

        /* save irq type */
        me->irq_type[gpio] = cfg->u.input.refe_irq;
    }

    max77696_gpio_write_config(me, gpio, mask, val);

out:
    return;
}

static __devinit int max77696_gpio_probe (struct platform_device *pdev)
{
    struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
    struct max77696_gpio_platform_data *pdata = pdev->dev.platform_data;
    struct max77696_gpio *me;
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

    mutex_init(&(me->lock));
    me->chip = chip;
    me->i2c  = __get_i2c(chip);
    me->dev  = &(pdev->dev);

    me->irq_base = pdata->irq_base;

    me->top_irq = chip->irq_base + MAX77696_ROOTINT_GPIO;

    memcpy(&(me->gpio), &max77696_gpio_chip, sizeof(me->gpio));
    me->gpio.dev  = me->dev;
    me->gpio.base = pdata->gpio_base;

    dev_dbg(me->dev, "GPIO base %u IRQ base %u\n",
        pdata->gpio_base, pdata->irq_base);

    rc = gpiochip_add(&(me->gpio));
    if (unlikely(rc < 0)) {
        dev_err(me->dev, "failed to add gpiochip [%d]\n", rc);
        goto out_err_add_gpiochip;
    }

    /* Disable all gpio interrupts */
    for (i = 0; i < GPIO_NGPIO; i++) {
        u8 mask, shift;

        mask  = GPIO_REG_BITMASK (CNFG_GPIO, REFE_IRQ);
        shift = GPIO_REG_BITSHIFT(CNFG_GPIO, REFE_IRQ);

        max77696_gpio_write_config(me, i,
            mask, (u8)(MAX77696_GPIO_REFEIRQ_NONE << shift));
    }

    /* Control the biasing circuitry */
    max77696_gpio_reg_set_bit(me, PUE_GPIO, GPIO_BIAS_EN, !!pdata->bias_en);

    for (i = 0; i < GPIO_NGPIO; i++) {
        max77696_gpio_init_cfg(me, i, &(pdata->init_data[i]));
    }

    for (i = 0; i < GPIO_NGPIO; i++) {
        unsigned int irq = me->irq_base + i;

        irq_set_chip_data       (irq, me);
        irq_set_chip_and_handler(irq, &max77696_gpio_irq_chip, handle_simple_irq);
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
        NULL, max77696_gpio_isr, IRQF_ONESHOT, DRIVER_NAME, me);

    if (unlikely(rc < 0)) {
        dev_err(me->dev,
            "failed to request IRQ(%d) [%d]\n", me->top_irq, rc);
        goto out_err_req_irq;
    }

    pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
    SUBDEVICE_SET_LOADED(gpio, chip);
    return 0;

out_err_req_irq:
    rc = gpiochip_remove(&(me->gpio));
    BUG_ON(rc);
out_err_add_gpiochip:
    platform_set_drvdata(pdev, NULL);
    mutex_destroy(&(me->lock));
    kfree(me);
    return rc;
}

static __devexit int max77696_gpio_remove (struct platform_device *pdev)
{
    struct max77696_gpio *me = platform_get_drvdata(pdev);
    int i, rc;

    for (i = 0; i < GPIO_NGPIO; i++) {
        unsigned int irq = me->irq_base + i;

        irq_set_handler  (irq, NULL);
        irq_set_chip_data(irq, NULL);
    }


    rc = gpiochip_remove(&(me->gpio));
    BUG_ON(rc);

    platform_set_drvdata(pdev, NULL);
    mutex_destroy(&(me->lock));
    kfree(me);

    return 0;
}

static struct platform_driver max77696_gpio_driver = {
    .driver.name  = DRIVER_NAME,
    .driver.owner = THIS_MODULE,
    .probe        = max77696_gpio_probe,
    .remove       = __devexit_p(max77696_gpio_remove),
};

static __init int max77696_gpio_driver_init (void)
{
    return platform_driver_register(&max77696_gpio_driver);
}

static __exit void max77696_gpio_driver_exit (void)
{
    platform_driver_unregister(&max77696_gpio_driver);
}

subsys_initcall(max77696_gpio_driver_init);
module_exit(max77696_gpio_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

