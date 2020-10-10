/*
 * Promixity Sensor for Microchip PIC12LF1822
 *
 * Copyright 2009-2012 Amazon Technologies Inc., All rights reserved
 *
 */

#include <linux/types.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/sysdev.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/sysfs.h>
#include <linux/sysdev.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>

extern int gpio_proximity_int(void);
extern int gpio_proximity_detected(void);
extern void gpio_proximity_reset(void);
extern int wario_prox_gpio_init(void);

#define DRIVER_NAME               "wario_prox"
#define PROXIMITY_THRESHOLD       10 /* 10 ms */
#define PROXIMITY_I2C_ADDR        0x0D
#define PROXIMITY_PORT_MINOR      161 /* /dev/proximitysensor */
#define PROXIMITY_MAX_REGISTERS   0x51
#define PROXIMITY_FLASH_DELAY     8
/* Register Set for the PIC12LF1822 */
#define PIC12LF1822_FWVER         0x40
#define PIC12LF1822_RAWL          0x41
#define PIC12LF1822_RAWH          0x42
#define PIC12LF1822_AVGL          0x43
#define PIC12LF1822_AVGH          0x44
#define PIC12LF1822_PROXSTATE     0x45
#define PIC12LF1822_THRES         0x46
#define PIC12LF1822_DBNCDET       0x47
#define PIC12LF1822_WDTCON        0x48
#define PIC12LF1822_LOOPREAD      0x49
#define PIC12LF1822_ZOFFSET       0x4A
#define PIC12LF1822_UPSTEP        0x4B
#define PIC12LF1822_STATUS        0x4C
#define PIC12LF1822_I2CTIMEOUT    0x4D
#define PIC12LF1822_REFL          0x4E
#define PIC12LF1822_REFH          0x4F
#define PIC12LF1822_PROXL         0x50
#define PIC12LF1822_PROXH         0x51

#define PROXIMITY_WDTCON_SUSPEND  0x0
#define PROXIMITY_WDTCON_RESUME   0x13

#define CHECK_BIT(var, pos) ((var) & (1 << (pos)))

static struct miscdevice proximity_misc_device = {
	PROXIMITY_PORT_MINOR,
	"proximitysensor",
	NULL,
};

struct proximity_info {
	struct i2c_client *client;
};

static struct i2c_client *proximity_i2c_client;

static int proximity_read_i2c(u8 *id, u8 reg_num)
{
	s32 error;

	error = i2c_smbus_read_byte_data(proximity_i2c_client, reg_num);
	if (error < 0) {
		return -EIO;
	}

	*id = (error & 0xFF);
	return 0;
}

static int proximity_write_i2c(u8 reg_num, u8 data)
{
	return i2c_smbus_write_byte_data(proximity_i2c_client, reg_num, data);
}

static u8 proximity_reg_number = 0;

static void dump_regs(void)
{
	u8 i = 0;
	u8 value = 0;

	for (i = PIC12LF1822_FWVER; i <= PROXIMITY_MAX_REGISTERS; i++) {
		proximity_read_i2c(&value, i);
		printk("Register 0x%x = Value:0x%x\n", i, value);
	}
}

static ssize_t proximity_reg_store(struct sys_device *dev, struct sysdev_attribute *attr, 
				const char *buf, size_t size)
{
	u16 value = 0;

	if (sscanf(buf, "%hx", &value) <= 0) {
		printk(KERN_ERR "Could not store the codec register value\n");
		return -EINVAL;
	}

	proximity_reg_number = value;
	return size;
}

static ssize_t proximity_reg_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	char *curr = buf;

	curr += sprintf(curr, "Proximity Register Number: %d\n", proximity_reg_number);
	curr += sprintf(curr, "\n");

	return curr - buf;
}

static SYSDEV_ATTR(proximity_reg, 0644, proximity_reg_show, proximity_reg_store);

static ssize_t proximity_register_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	char *curr = buf;
	u8 value = 0;

	if (proximity_reg_number > PROXIMITY_MAX_REGISTERS) {
		curr += sprintf(curr, "Proximity Registers\n");
		curr += sprintf(curr, "\n");
		dump_regs();
	}
	else {
		proximity_read_i2c(&value, proximity_reg_number);
		curr += sprintf(curr, "Proximity Register %d\n", proximity_reg_number);
		curr += sprintf(curr, "\n");
		curr += sprintf(curr, "Value: 0x%x\n", value);
		curr += sprintf(curr, "\n");
	}

	return curr - buf;
}

static ssize_t proximity_register_store(struct sys_device *dev, struct sysdev_attribute *attr, 
					const char *buf, size_t size)
{
	u8 value = 0;

	if (proximity_reg_number > PROXIMITY_MAX_REGISTERS) {
		printk(KERN_ERR "No codec register %d\n", proximity_reg_number);
		return size;
	}

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}
	
	proximity_write_i2c(proximity_reg_number, value);
	return size;
}

static SYSDEV_ATTR(proximity_register, 0644, proximity_register_show, proximity_register_store);

static ssize_t prox_fwver_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_FWVER);
	return sprintf(buf, "%d\n", value);
}

static SYSDEV_ATTR(fwver, 0644, prox_fwver_show, NULL);

static ssize_t prox_raw_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value_LSB, value_MSB;
	u16 value;

	proximity_read_i2c(&value_LSB, PIC12LF1822_RAWL);
	proximity_read_i2c(&value_MSB, PIC12LF1822_RAWH);
	value = (u16)(value_MSB<<8|value_LSB);
	return sprintf(buf, "%d\n", value);
}

static SYSDEV_ATTR(raw, 0644, prox_raw_show, NULL);

static ssize_t prox_avg_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value_LSB, value_MSB;
	u16 value;

	proximity_read_i2c(&value_LSB, PIC12LF1822_AVGL);
	proximity_read_i2c(&value_MSB, PIC12LF1822_AVGH);
	value = (u16)(value_MSB<<8|value_LSB);
	return sprintf(buf, "%d\n", value);
}

static SYSDEV_ATTR(avg, 0644, prox_avg_show, NULL);

static ssize_t prox_proxstate_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_PROXSTATE);
	return sprintf(buf, "%d\n", value);
}

static SYSDEV_ATTR(proxstate, 0644, prox_proxstate_show, NULL);


static ssize_t prox_thres_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{	
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_THRES);
	return sprintf(buf, "%d\n", value);
}

static ssize_t prox_thres_store(struct sys_device *dev, struct sysdev_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;
	
	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(PIC12LF1822_THRES, value);
	return size;
}

static SYSDEV_ATTR(thres, 0644, prox_thres_show, prox_thres_store);

static ssize_t prox_dbncdet_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_DBNCDET);
	return sprintf(buf, "%d\n", value);
}

static ssize_t prox_dbncdet_store(struct sys_device *dev, struct sysdev_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(PIC12LF1822_DBNCDET, value);
	return size;
}

static SYSDEV_ATTR(dbncdet, 0644, prox_dbncdet_show, prox_dbncdet_store);

static ssize_t prox_wdtcon_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_WDTCON);
	return sprintf(buf, "%d\n", value);
}

static ssize_t prox_wdtcon_store(struct sys_device *dev, struct sysdev_attribute *attr, 
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(PIC12LF1822_WDTCON, value);
	return size;
}

static SYSDEV_ATTR(wdtcon, 0644, prox_wdtcon_show, prox_wdtcon_store); 

static ssize_t prox_loopread_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_LOOPREAD);
	return sprintf(buf, "%d\n", value);
}

static ssize_t prox_loopread_store(struct sys_device *dev, struct sysdev_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(PIC12LF1822_LOOPREAD, value);
	return size;
}

static SYSDEV_ATTR(loopread, 0644, prox_loopread_show, prox_loopread_store);

static ssize_t prox_zoffset_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_ZOFFSET);
	return sprintf(buf, "%d\n", value);
}

static ssize_t prox_zoffset_store(struct sys_device *dev, struct sysdev_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}
	
	proximity_write_i2c(PIC12LF1822_ZOFFSET, value);
	return size;
}

static SYSDEV_ATTR(zoffset, 0644, prox_zoffset_show, prox_zoffset_store);

static ssize_t prox_upstep_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_UPSTEP);
	return sprintf(buf, "%d\n", value);
}

static ssize_t prox_upstep_store(struct sys_device *dev, struct sysdev_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(PIC12LF1822_UPSTEP, value);

	return size;
}

static SYSDEV_ATTR(upstep, 0644, prox_upstep_show, prox_upstep_store);

static ssize_t prox_status_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_STATUS);
	return sprintf(buf, "%d\n", value);
}

static ssize_t prox_status_store(struct sys_device *dev, struct sysdev_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(PIC12LF1822_STATUS, value);
	/* if "write to flash" bit (7) is set, a delay of 8ms must occur */
	if (CHECK_BIT(value, 7)) {
		msleep(8);
	}

	return size;
}

static SYSDEV_ATTR(status, 0644, prox_status_show, prox_status_store);

static ssize_t prox_i2ctimeout_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, PIC12LF1822_I2CTIMEOUT);
	return sprintf(buf, "%d\n", value);
}

static ssize_t prox_i2ctimeout_store(struct sys_device *dev, struct sysdev_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(PIC12LF1822_I2CTIMEOUT, value);
	return size;
}

static SYSDEV_ATTR(i2ctimeout, 0644, prox_i2ctimeout_show, prox_i2ctimeout_store);

static ssize_t prox_reset_store(struct sys_device *dev, struct sysdev_attribute *attr,
					const char *buf, size_t size)
{
	printk(KERN_INFO "Resetting proximity sensor\n");
	gpio_proximity_reset();

	return size;
}

static SYSDEV_ATTR(reset, 0644, NULL, prox_reset_store);

static ssize_t prox_ref_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value_LSB, value_MSB;
	u16 value;

	proximity_read_i2c(&value_LSB, PIC12LF1822_REFL);
	proximity_read_i2c(&value_MSB, PIC12LF1822_REFH);
	value = (((u16)value_MSB) << 8) | value_LSB;
	return sprintf(buf, "%d\n", value);
}

static SYSDEV_ATTR(ref, 0644, prox_ref_show, NULL);

static ssize_t prox_prox_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	u8 value_LSB, value_MSB;
	u16 value;

	proximity_read_i2c(&value_LSB, PIC12LF1822_PROXL);
	proximity_read_i2c(&value_MSB, PIC12LF1822_PROXH);
	value = (((u16)value_MSB) << 8) | value_LSB;
	return sprintf(buf, "%d\n", value);
}

static SYSDEV_ATTR(prox, 0644, prox_prox_show, NULL);


static struct sysdev_class proximity_sysclass = {
	.name	= "proximity",
};

static struct sys_device device_proximity = {
	.id	= 0,
	.cls	= &proximity_sysclass,
};

static int proximity_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct proximity_info *info;
	u8 touch_status = 0;
	int error = 0;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		return -ENOMEM;
	}

	/* Reset Proximity */
	gpio_proximity_reset();

	client->addr = PROXIMITY_I2C_ADDR;
	i2c_set_clientdata(client, info);
	info->client = client;
	proximity_i2c_client = info->client;
	proximity_i2c_client->addr = PROXIMITY_I2C_ADDR;
	if (proximity_read_i2c(&touch_status, PIC12LF1822_FWVER) < 0)
		return -ENODEV;

	if (proximity_write_i2c(PIC12LF1822_WDTCON, PROXIMITY_WDTCON_RESUME) < 0)
		return -ENODEV;


	printk(KERN_INFO "Proximity Sensor Found, Firmware Rev: 0x%x\n", touch_status);
	
	/* /sys files */

	error = sysdev_class_register(&proximity_sysclass);
	if (error) goto exit;
	error = sysdev_register(&device_proximity);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_proximity_reg);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_proximity_register);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_fwver);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_raw);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_avg);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_proxstate);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_thres);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_dbncdet);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_wdtcon);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_loopread);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_zoffset);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_upstep);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_status);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_i2ctimeout);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_reset);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_ref);
	if (error) goto exit;
	error = sysdev_create_file(&device_proximity, &attr_prox);
	if (error) goto exit;
	return 0;
exit:
	return -error;
}

static int proximity_remove(struct i2c_client *client)
{
	struct proximity_info *info = i2c_get_clientdata(client);

	i2c_set_clientdata(client, info);

	sysdev_remove_file(&device_proximity, &attr_proximity_reg);
	sysdev_remove_file(&device_proximity, &attr_proximity_register);
	sysdev_remove_file(&device_proximity, &attr_fwver);
	sysdev_remove_file(&device_proximity, &attr_raw);
	sysdev_remove_file(&device_proximity, &attr_avg);
	sysdev_remove_file(&device_proximity, &attr_proxstate);
	sysdev_remove_file(&device_proximity, &attr_thres);
	sysdev_remove_file(&device_proximity, &attr_dbncdet);
	sysdev_remove_file(&device_proximity, &attr_wdtcon);
	sysdev_remove_file(&device_proximity, &attr_loopread);
	sysdev_remove_file(&device_proximity, &attr_zoffset);
	sysdev_remove_file(&device_proximity, &attr_upstep);
	sysdev_remove_file(&device_proximity, &attr_status);
	sysdev_remove_file(&device_proximity, &attr_i2ctimeout);
	sysdev_remove_file(&device_proximity, &attr_reset);
	sysdev_remove_file(&device_proximity, &attr_ref);
	sysdev_remove_file(&device_proximity, &attr_prox);

	sysdev_unregister(&device_proximity);
	sysdev_class_unregister(&proximity_sysclass);
	
	return 0;
}

static void do_proximity_work(struct work_struct *dummy)
{
	int irq = gpio_proximity_int();

	if (!gpio_proximity_detected()) {
		kobject_uevent(&proximity_misc_device.this_device->kobj, KOBJ_ONLINE);
		irq_set_irq_type(irq, IRQF_TRIGGER_RISING);
	}
	else {
		kobject_uevent(&proximity_misc_device.this_device->kobj, KOBJ_OFFLINE);
		irq_set_irq_type(irq, IRQF_TRIGGER_FALLING);
	}

	enable_irq(irq);
}

static DECLARE_DELAYED_WORK(proximity_work, do_proximity_work);

static int pic12lf1822_proximity_suspend(struct i2c_client *client, pm_message_t mesg)
{
	u8 value;
	printk(KERN_INFO "pic12lf1822_proximity_suspend\n");

	cancel_delayed_work_sync(&proximity_work);
	disable_irq(gpio_proximity_int());
	
	// Enter low power mode
	proximity_read_i2c(&value, PIC12LF1822_STATUS);
	value |= 0x01;
	proximity_write_i2c(PIC12LF1822_STATUS, value);
	proximity_write_i2c(PIC12LF1822_WDTCON, PROXIMITY_WDTCON_SUSPEND);

	return 0;
}


static int pic12lf1822_proximity_resume(struct i2c_client *client)
{
	int irq;
	printk(KERN_INFO "pic12lf1822_proximity_resume\n");

	// Re-initialize
	proximity_write_i2c(PIC12LF1822_WDTCON, PROXIMITY_WDTCON_RESUME);
	
	irq = gpio_proximity_int();
	if (!gpio_proximity_detected())
		irq_set_irq_type(irq, IRQF_TRIGGER_RISING);
	else
		irq_set_irq_type(irq, IRQF_TRIGGER_FALLING);

	enable_irq(irq);
	return 0;
}

static const struct i2c_device_id proximity_id[] =  {
	{ DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, proximity_id);

static struct i2c_driver proximity_i2c_driver = {
	.driver = {
			.name = DRIVER_NAME,
		},
	.suspend = pic12lf1822_proximity_suspend,
	.resume = pic12lf1822_proximity_resume,
	.probe = proximity_probe,
	.remove = proximity_remove,
	.id_table = proximity_id,
};

static irqreturn_t proximity_irq(int irq, void *devid)
{
	disable_irq_nosync(irq);

	/* Debounce for 10 ms */
	schedule_delayed_work(&proximity_work, msecs_to_jiffies(PROXIMITY_THRESHOLD));
	
	return IRQ_HANDLED;
}

static int __init proximity_port_init(void)
{
	int err = 0, irq;

	printk(KERN_INFO "Initializing Wario Proximity Sensor\n");
	
	/* Init Proximity GPIO */
	wario_prox_gpio_init();

	irq = gpio_proximity_int();
	if (!gpio_proximity_detected())
		irq_set_irq_type(irq, IRQF_TRIGGER_RISING);
	else
		irq_set_irq_type(irq, IRQF_TRIGGER_FALLING);

	if (misc_register(&proximity_misc_device)) {
		printk (KERN_ERR "proximity_misc_device: Couldn't register device 10, "
				"%d.\n", PROXIMITY_PORT_MINOR);
		return -EBUSY;
	}

	err = i2c_add_driver(&proximity_i2c_driver);
	if (err) {
		printk(KERN_ERR "Proximity: Could not add driver\n");
		free_irq(gpio_proximity_int(), NULL);
	}

        err = request_irq(irq, proximity_irq, 0, "Wario Proximity", NULL);
        if (err != 0) {
                printk(KERN_ERR "IRQF_DISABLED: Could not get IRQ %d\n", irq);
		free_irq(gpio_proximity_int(), NULL);
                return err;
        }

	if (!gpio_proximity_detected())
		kobject_uevent(&proximity_misc_device.this_device->kobj, KOBJ_ONLINE);
	else
		kobject_uevent(&proximity_misc_device.this_device->kobj, KOBJ_OFFLINE);	

	return err;
}

static void proximity_port_cleanup(void)
{
	misc_deregister(&proximity_misc_device);
	free_irq(gpio_proximity_int(), NULL);
	i2c_del_driver(&proximity_i2c_driver);
}

static void __exit proximity_port_remove(void)
{
	proximity_port_cleanup();
}

module_init(proximity_port_init);
module_exit(proximity_port_remove);

MODULE_AUTHOR("Manish Lachwani");
MODULE_DESCRIPTION("Wario Proximity Sensor");
MODULE_LICENSE("GPL");
