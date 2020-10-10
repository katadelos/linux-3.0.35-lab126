/*
 * Copyright (C) 2011 Maxim Integrated Products
 * Copyright (c) 2011-2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * ALS(MAX44009) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */
//#define DEBUG
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/sysdev.h>
#include <linux/hwmon.h>
#include <linux/lab126_als.h>

#define MAX44009_ALS_DEV_MINOR 		166
#define DRIVER_VERSION				"1.0"

#define MAX44009_I2C_ADDRESS0       0x94
#define MAX44009_I2C_ADDRESS1       0x96
#define MAX44009_INT_STATUS_ADDR    0x00
#define MAX44009_INT_ENABLE_ADDR    0x01
#define MAX44009_CONFIG_ADDR        0x02
#define MAX44009_LUX_HIGH_ADDR      0x03
#define MAX44009_LUX_LOW_ADDR       0x04
#define MAX44009_THRESH_HIGH_ADDR   0x05
#define MAX44009_THRESH_LOW_ADDR    0x06
#define MAX44009_THRESH_TIM_ADDR    0x07
#define MAX44009_RETRY_DELAY        1
#define MAX44009_MAX_RETRIES        1
#define MAX44009_HYSTERESIS			10
#define THRESHOLD_TIMER_1SEC        10

#define MAX44009_AUTO_MODE
#define MAX44009_ALS "max44009_als"

/* Lux below this threshold will force reporting every second */
#define MAX_LUX_FORCE_REPORTING 30

/*#define MAX44009_INPUT_DEV*/

extern int gpio_max44009_als_int(void);
extern int wario_als_gpio_init(void);
static struct i2c_client *max44009_i2c_client = NULL;
static struct device *max44009_hwmon = NULL;
static u8 max44009_reg_number = 0;
#ifdef MAX44009_INPUT_DEV
static struct input_dev *max44009_idev = NULL;
#endif
static struct miscdevice als_max44009_misc_device;
static int max44009_init_lpm(void);
static int max44009_init_mm(int);
static int max44009_init_gain(void);

static void max44009_als_wqthread(struct work_struct *);
static DECLARE_WORK(max44009_als_wq, max44009_als_wqthread);
#ifdef DEBUG
static int max44009_read_lux(int *);
#endif
static int max44009_read_lux_oneshot(int *);
static int max44009_init_automode(void);
static int max44009_write_reg(u8 reg_num, u8 value);
static int max44009_read_reg(u8 reg_num, u8 *value);
static int max44009_read_lux_regs(u8 *lux);
static int max44009_read_count_oneshot(int *lux_count);

static int Test_lux_count = -1;

struct max44009_thresh_zone {
	u8 upper_thresh;
	u8 lower_thresh;
};

static int automode = 0;

static void max44009_xthresh_uev(int lux){
	char buf[32];
	char *envp[] = {"ALS=xthreshold", buf, NULL};
	snprintf(buf, 31, "LUX=%d", lux);

	kobject_uevent_env(&als_max44009_misc_device.this_device->kobj, KOBJ_CHANGE, envp);
}

static long als_max44009_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int __user *argp = (int __user *)arg;
	int ret = 0;
	int lux = 0;
	int count = 0;
	ALS_REGS als_reg;

	switch (cmd) {
	case ALS_IOCTL_GET_LUX:
		    dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:ALS_IOCTL_GET_LUX", __FUNCTION__, __LINE__);
			ret = -EFAULT;
			if(max44009_read_lux_oneshot(&lux) == 0){
				if (put_user(lux, argp) == 0){
					ret = 0;
					dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d: lux:%d", __FUNCTION__, __LINE__, lux);
				} else {
					printk(KERN_ERR "ALS_IOCTL_GET_LUX: put_user FAILED\n");
				}
			}else{
				printk(KERN_ERR "ALS_IOCTL_GET_LUX: max_44009_read_lux_oneshot FAILED\n");
			}
			break;

	case ALS_IOCTL_GET_COUNT:
		    dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);
			ret = -EFAULT;
			if(max44009_read_count_oneshot(&count) == 0){
				if (put_user(count, argp) == 0){
					ret = 0;
					dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d: lux:%d", __FUNCTION__, __LINE__, lux);
				} else {
					printk(KERN_ERR "ALS_IOCTL_GET_COUNT: put_user FAILED\n");
				}
			}else{
				printk(KERN_ERR "ALS_IOCTL_GET_LUX: max_44009_read_count_oneshot FAILED\n");
			}			
			break;

	case ALS_IOCTL_READ_LUX_REGS:
			dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:ALS_IOCTL_READ_REGS", __FUNCTION__, __LINE__);
			ret = -EFAULT;
			if (copy_from_user(&als_reg, argp, sizeof(ALS_REGS)) == 0) {
				if (max44009_read_lux_regs(&als_reg.value[0]) == 0) {
					if (copy_to_user(argp, &als_reg, sizeof(ALS_REGS)) == 0)	{
						ret = 0;
					} else {
						printk(KERN_ERR "ALS_IOCTL_READ_LUX_REGS: copy_to_user FAILED\n");
					}
				} else {
					printk(KERN_ERR "ALS_IOCTL_READ_LUX_REGS: max_44009_read_lux_regs FAILED\n");
				}
			} else {
				printk(KERN_ERR "ALS_IOCTL_READ_LUX_REGS copy_from_user FAILED\n");
			}
			break;

	case ALS_IOCTL_READ_REG:
			dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:ALS_IOCTL_READ_REG", __FUNCTION__, __LINE__);
			ret = -EFAULT;
			if (copy_from_user(&als_reg, argp, sizeof(ALS_REGS)) == 0) {
				if (max44009_read_reg(als_reg.addr, &als_reg.value[0]) == 0) {
					if (copy_to_user(argp, &als_reg, sizeof(ALS_REGS)) == 0) {
						ret = 0;
					}
				}
			}
			break;

	case ALS_IOCTL_WRITE_REG:
			dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:ALS_IOCTL_WRITE_REG", __FUNCTION__, __LINE__);
			ret = -EFAULT;
			if (copy_from_user(&als_reg, argp, sizeof(ALS_REGS)) == 0) {
				if (max44009_write_reg(als_reg.addr, als_reg.value[0]) == 0)
				{
					ret = 0;
				}
			}
			break;

	case ALS_IOCTL_AUTOMODE_EN:
			dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);

			if(get_user(automode, argp)){
				ret = -EFAULT;
				printk(KERN_ERR "%s:%d get_user failed\n", __func__,__LINE__);
				break;
			}

			if(automode){

				if(max44009_init_automode()){
					ret = -EFAULT;
					printk(KERN_ERR "%s:%d Could not initialize automode\n", __func__,__LINE__);
				}
			}else{
				if(max44009_init_mm(0) ){
					ret = -EFAULT;
					printk(KERN_ERR "%s:%d Could not initialize manual mode\n", __func__,__LINE__);
			    }
			}

			break;
		default:
			printk(KERN_ERR "ALS_IOCTL_READ_LUX_REGS: unknown command: %d\n", cmd);
		        ret = -EINVAL;
			break;
	}
	return ret;
}

static ssize_t als_max44009_misc_write(struct file *file, const char __user *buf,
								size_t count, loff_t *pos)
{
	return 0;
}

static ssize_t als_max44009_misc_read(struct file *file, char __user *buf,
								size_t count, loff_t *pos)
{
	return 0;
}

static const struct file_operations als_max44009_misc_fops =
{
	.owner = THIS_MODULE,
	.read  = als_max44009_misc_read,
	.write = als_max44009_misc_write,
	.unlocked_ioctl = als_max44009_ioctl,
};

static struct miscdevice als_max44009_misc_device =
{
	.minor = MAX44009_ALS_DEV_MINOR,
	.name  = ALS_MISC_DEV_NAME,
	.fops  = &als_max44009_misc_fops,
};

static int max44009_write_reg(u8 reg_num, u8 value)
{
	s32 err;

	err = i2c_smbus_write_byte_data(max44009_i2c_client, reg_num, value);
	if (err < 0) {
		printk(KERN_ERR "max44009: i2c write error\n");
	}

	return err;
}

static int max44009_read_reg(u8 reg_num, u8 *value)
{
	s32 retval;
	u8 reg_val;

	retval = i2c_smbus_read_byte_data(max44009_i2c_client, reg_num);
	if (retval < 0) {
		printk(KERN_ERR "max44009: i2c read error\n");
		return retval;
	}

	reg_val = (u8) (retval & 0xFF);
	/*
	 * The value read back from these regs are inverted.
	 * Need to invert the value before returning it.
	 */
	switch (reg_num)
	{
	case MAX44009_CLOCK_REG_1:
	case MAX44009_CLOCK_REG_2:
	case MAX44009_GAIN_REG_1 :
	case MAX44009_GAIN_REG_2 :
		reg_val = ~reg_val;
		break;
	default:
		break;
	}
	*value = reg_val;
	return 0;
}

/*
 *  max44009_read_spl - Performs a special read of two bytes to a MAX44009 device.
 *  This follows the recommended method of reading the high and low lux values
 *  per the application note AN5033.pdf
 *
 *  This special format for two byte read avoids the stop between bytes.
 *  Otherwise the stop would allow the MAX44009 to update the low lux byte,
 *  and the two bytes could be out of sync:
 *    [S] [Slave Addr + 0(W)] [ACK] [Register 1] [ACK] [Sr] [Slave Addr + 1(R)]
 *    [ACK] [Data 1] [NACK] [Sr] [S] [Slave Addr + 0(W)] [ACK] [Register 2] [ACK]
 *    [Sr] [Slave Addr + 1(R)] [ACK] [Data 2] [NACK] [P]
 *
 */
static int max44009_read_spl(u8 *buffer)
{
	int err;
	int retry = 0;

	struct i2c_msg msgs[] =
	{
		{	/* first message is the register address */
			.addr = max44009_i2c_client->addr,
			.flags = max44009_i2c_client->flags,
			.len = 1,
			.buf = buffer,
		},
		{	/* second message is the read length */
			.addr = max44009_i2c_client->addr,
			.flags = max44009_i2c_client->flags | I2C_M_RD,
			.len = 1,
			.buf = &buffer[0],
		},
		{	/* third message is the next register address */
			.addr = max44009_i2c_client->addr,
			.flags = max44009_i2c_client->flags,
			.len = 1,
			.buf = &buffer[1],
		},
		{	/* fourth message is the next read length */
			.addr = max44009_i2c_client->addr,
			.flags = max44009_i2c_client->flags | I2C_M_RD,
			.len = 1,
			.buf = &(buffer[1]),
		}
	};

	do
	{
	  err = i2c_transfer(max44009_i2c_client->adapter, msgs, 4);
		if (err != 4) {
		    msleep_interruptible(MAX44009_RETRY_DELAY);
			printk(KERN_ERR "%s: I2C SPL read ttransfer error err=%d\n", __FUNCTION__, err);
		}
	} while ((err != 4) && (++retry < MAX44009_MAX_RETRIES));

	if (err != 4) {
		printk(KERN_ERR "%s: read transfer error err=%d\n", __FUNCTION__, err);
		err = -EIO;
	} else
		err = 0;

	dev_dbg(als_max44009_misc_device.this_device, "\n%s:%d: LUX Hi=0x%02x Lo=0x%02x max44009_i2c_client->flags 0x%x \n", __FUNCTION__, __LINE__,
	       buffer[0], buffer[1],  max44009_i2c_client->flags);
	return err;
}

static void max44009_reg_dump(void)
{
	u8 i = 0;
	u8 value = 0xff;
	int ret = 0;

	for (i = 0; i <= MAX44009_MAX_REG; i++) {
		ret = max44009_read_reg(i, &value);
		if (ret) {
			printk(KERN_ERR "%s: ERROR reading MAX44009 registers \n",__FUNCTION__);
			break;
		}
		printk(KERN_DEBUG "MAX44009: reg=0x%x, value=0x%x\n", i, value);
	}
}

/*
 * /sys access to the max44009 registers
 */
static ssize_t max44009_regaddr_store(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t size)
{
	int value = 0;

	if (sscanf(buf, "%x", &value) <= 0) {
	    printk(KERN_ERR "Could not store the max44009 register address\n");
	    return -EINVAL;
	}

	max44009_reg_number = value;
	return size;
}

static ssize_t max44009_regaddr_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	char *curr = buf;

	curr += sprintf(curr, "max44009 register address = 0x%x\n", max44009_reg_number);
	curr += sprintf(curr, "\n");

	return curr - buf;
}
static SYSDEV_ATTR(max44009_regaddr, 0644, max44009_regaddr_show, max44009_regaddr_store);

static ssize_t max44009_regval_store(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t size)
{
	int value = 0;

	if (sscanf(buf, "%x", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	if (max44009_reg_number == 0x03 || max44009_reg_number == 0x04) {
		printk(KERN_ERR "Error trying to update readonly register\n");
		return -EINVAL;
	}

	max44009_write_reg(max44009_reg_number, value);
	return size;
}

static ssize_t max44009_regval_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	unsigned char value = 0xff;
	char *curr = buf;

	if (max44009_reg_number > MAX44009_MAX_REG) {
		/* dump all the regs */
		curr += sprintf(curr, "MAX44009 Register Dump\n");
		curr += sprintf(curr, "\n");
		max44009_reg_dump();
	}
	else {
		max44009_read_reg(max44009_reg_number, &value);
		curr += sprintf(curr, "MAX44009 Register Address : 0x%x\n", max44009_reg_number);
		curr += sprintf(curr, "\n");
		curr += sprintf(curr, " Value: 0x%x\n", value);
		curr += sprintf(curr, "\n");
	}

	return curr - buf;
};
static SYSDEV_ATTR(max44009_regval, 0644, max44009_regval_show, max44009_regval_store);

/**
 * For test purpose, set and report lux count
 * It disables reading lux from the sensor
 */
static ssize_t max44009_lux_count_store(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t size)
{
	int value;

	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	Test_lux_count = value;

	if(max44009_read_lux_oneshot(&value) != 0){
		printk(KERN_ERR "Error reading lux\n");
		return -EIO;
	}
	max44009_xthresh_uev(value);

	return size;
}

static ssize_t max44009_lux_count_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	char *curr = buf;
	int lux_count = -1;

	if (max44009_read_count_oneshot(&lux_count) == 0) {
		curr += sprintf(curr, "%d\n", lux_count);
		curr += sprintf(curr, "\n");
	}
	else {
		curr += sprintf(curr, "read lux count failed\n");
	}

	return curr - buf;
};
static SYSDEV_ATTR(max44009_lux_count, 0644, max44009_lux_count_show, max44009_lux_count_store);


/*
 * /sys access to get the lux value
 */
static ssize_t max44009_lux_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	char *curr = buf;
	int lux = 0;
	dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);
	if(max44009_read_lux_oneshot(&lux) == 0){
		curr += sprintf(curr, "%d\n", lux);
#ifdef DEBUG
		max44009_read_lux(&lux);
		curr += sprintf(curr, "i2c with stop lux %d\n", lux);
		curr += sprintf(curr, "\n");
#endif
	}else{
		curr += sprintf(curr, "read lux count failed\n");
	}
	return curr - buf;
};

static SYSDEV_ATTR(max44009_lux, 0644, max44009_lux_show, NULL);

static ssize_t max44009_lux_auto_enable(struct sys_device *dev,
                        struct sysdev_attribute *attr,
					const char *buf, size_t size){
        int ret = strlen(buf);

	if(buf[0]=='1'){
		  dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);
		  automode = 1;
	    	  if(max44009_init_automode()){
			        ret = -EFAULT;
				printk(KERN_ERR "%s:%d Could not initialize\n", __func__,__LINE__);
		  }
	}else{
	          dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);
		  automode = 0;
	    	  if(max44009_write_reg(MAX44009_INT_ENABLE_ADDR, 0x0)){
                                printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__,MAX44009_INT_ENABLE_ADDR);
				ret = -EFAULT;
		  }
	}
	return ret;
}
static SYSDEV_ATTR(max44009_lux_auto_on, 0644, NULL, max44009_lux_auto_enable);

static struct sysdev_class max44009_ctrl_sysclass = {
    .name   = "max44009_ctrl",
};

static struct sys_device device_max44009_ctrl = {
    .id     = 0,
    .cls    = &max44009_ctrl_sysclass,
};

/*
 *  Updates the integration time settings in the configuration register
 *
 */
int max44009_set_integration_time(u8 time)
{
	int err = 0;
	u8 config = 0;
	printk(KERN_DEBUG "max44009_set_integration_time\n");

	err = max44009_read_reg(MAX44009_CONFIG_ADDR, &config);
	if (err)
		return err;

	if (!(config & 0x40))
		return 0;

	config &= 0xF8;
	config |= (time & 0x7);

	err = max44009_write_reg(MAX44009_CONFIG_ADDR, config);
	if (err) {
		printk("%s: couldn't update config: integ_time\n", __FUNCTION__);
	}
	return err;
}

/*
 *  Enables or disables manual mode on the MAX44009
 */
int max44009_set_manual_mode(bool enable)
{
	int err = 0;
	u8 config = 0;
	bool currently_manual;

	err = max44009_read_reg(MAX44009_CONFIG_ADDR, &config);
	if (err) {
		printk("%s: couldn't read config \n", __FUNCTION__);
		return err;
	}

	currently_manual = (config & 0x40) ? true : false;

	if (currently_manual != enable)
	{
		if (enable)
			config |= 0x40;
		else
			config &= 0xBF;

		err = max44009_write_reg(MAX44009_CONFIG_ADDR, config);
		if (err) {
			printk("%s: couldn't update config: auto/manual mode status \n", __FUNCTION__);
		}
	}
	return err;
}

/*
 *  Enables or disables continuous mode on the MAX44009
 */
int max44009_set_continuous_mode(bool enable)
{
	int err = 0;
	u8 config = 0;
	bool currently_cont;

	err = max44009_read_reg(MAX44009_CONFIG_ADDR, &config);
	if (err) {
		printk("%s: couldn't read config \n", __FUNCTION__);
		return err;
	}

	currently_cont = (config & 0x80) ? true : false;

	if (currently_cont != enable) {
		if (enable)
			config |= 0x80;
		else
			config &= 0x7F;

		err = max44009_write_reg(MAX44009_CONFIG_ADDR, config);
		if (err) {
			printk("%s: couldn't update config: cont mode status \n", __FUNCTION__);
		}
	}
	return err;
}

/*
 *  Sets or clears the current division ratio bit, if necessary
 *
 */
int max44009_set_current_div_ratio(bool enable)
{
	int err = 0;
	u8 config = 0;
	bool currently_cdr;

	err = max44009_read_reg(MAX44009_CONFIG_ADDR, &config);
	if (err) {
		printk("%s: couldn't read config \n", __FUNCTION__);
		return err;
	}

	currently_cdr = (config & 0x08) ? true : false;

	if (currently_cdr != enable)
	{
		if (enable)
			config |= 0x08;
		else
			config &= 0xF7;

		err = max44009_write_reg(MAX44009_CONFIG_ADDR, config);
		if (err) {
			printk("%s: couldn't update config: cdr mode \n", __FUNCTION__);
		}
	}
	return err;
}

/*
 *  Updates threshold timer register to a new value
 */
int max44009_set_thresh_tim(u8 new_tim_val)
{
	int err = 0;

	err = max44009_write_reg(MAX44009_THRESH_TIM_ADDR, new_tim_val);
	if (err) {
		printk("%s: couldn't update config: cdr mode \n", __FUNCTION__);
	}
	return err;
}

/*
 *  Changes the threshold zone based on the current lux reading
 */
int max44009_set_threshold_zone(struct max44009_thresh_zone *new_zone)
{
	int err = 0;

	dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d: max44009 set_threshold_zone upper: 0x%x lower:0x%x\n", __FUNCTION__, __LINE__,
	       new_zone->upper_thresh, new_zone->lower_thresh);

	err = max44009_write_reg(MAX44009_THRESH_HIGH_ADDR, new_zone->upper_thresh) ;
	if (err) {
		printk("%s: couldn't update upper threshold \n", __FUNCTION__);
		goto set_err;
	}

	err = max44009_write_reg(MAX44009_THRESH_LOW_ADDR, new_zone->lower_thresh);
	if (err) {
	  printk(KERN_ERR "%s:%d couldn't update lower threshold \n", __FUNCTION__, __LINE__);
	}

set_err:
	return err;
}

static int max44009_read_lux_oneshot(int *adjusted_lux){
	int lux_count;

	int ret = max44009_read_count_oneshot(&lux_count);
	*adjusted_lux = lux_count * ALS_LUX_PER_COUNT_HI_RES;

	return ret;
}

/*
 * i2c transaction without stop
 * reading 2 registers holding mantissa and exponent.
 * return the lux count
 */
static int max44009_read_count_oneshot(int *lux_count){
	u8 exponent = 0, mantissa = 0;
	u8 lux[2];

	//Testing purpose read lux from sysentry configured value , not from sensor
	if(Test_lux_count != -1){
		*lux_count = Test_lux_count;
		return 0;
	}

	max44009_read_lux_regs(&lux[0]);

	/* calculate the lux value */
	exponent = lux[0] >> 4;
	if (exponent == 0x0F) {
		printk("%s: overload on light sensor!\n", __FUNCTION__);
		/* maximum reading (per datasheet) */
		return -EINVAL;
	}

	mantissa = ((lux[0] & 0x0F) << 4) | (lux[1] & 0x0F);
	/* Count = [2pow(exp)] * mantissa */
	*lux_count = ((int)(1 << exponent)) * ((u32) mantissa);
	return 0;
}

/*
 *  i2c transaction without stop
    reading 2 registers holding mantissa and exponent but do not convert
 */
static int max44009_read_lux_regs(u8 *lux){
	int ret;

        lux[0]=MAX44009_LUX_HIGH_ADDR;
        lux[1]=MAX44009_LUX_LOW_ADDR;
        ret = max44009_read_spl(&lux[0]);

        return ret;
}

#ifdef DEBUG
/*
 *  2 i2c reads with stops
 *  reading 2 registers holding mantissa and exponent
 */
static int max44009_read_lux(int *adjusted_lux)
{
        u8 exponent = 0, mantissa = 0;
        u8 lux_high = 0;
        u8 lux_low = 0;

        /* do a read of the ADC - LUX[HI] register (0x03) */
        if (max44009_read_reg(MAX44009_LUX_HIGH_ADDR, &lux_high) != 0) {
                printk("%s: couldn't read lux(HI) data\n", __FUNCTION__);
                return -EIO;
        }

        /* calculate the lux value */
        exponent = (lux_high >> 4) & 0x0F;
        if (exponent == 0x0F) {
                printk("%s: overload on light sensor!\n", __FUNCTION__);
                /* maximum reading (per datasheet) */
                *adjusted_lux = 188006;
                return -EINVAL;
        }

        /* do a read of the ADC - LUX[LO] register (0x04) */
        if (max44009_read_reg(MAX44009_LUX_LOW_ADDR, &lux_low) != 0) {
                printk("%s: couldn't read lux(LOW) data\n", __FUNCTION__);
                return -EIO;
        }

        mantissa = ((lux_high & 0x0F) << 4) | (lux_low & 0x0F);
        /* LUX = [2pow(exp)] * mantissa * 0.045 */
        *adjusted_lux = ((int)(1 << exponent)) * ((int) mantissa) * ALS_LUX_PER_COUNT_HI_RES;
        /* TODO: add any other adjustments
         *               (e.g. correct for a lens,glass, other filtering, etc)
         */
        return 0;
}


static int max44009_read_lux_low_resolution(int *adjusted_lux)
{
	u8 exponent = 0, mantissa = 0;
	u8 lux_high = 0;

	/* do a read of the ADC - LUX[HI] register (0x03) */
	if (max44009_read_reg(MAX44009_LUX_HIGH_ADDR, &lux_high) != 0) {
		printk("%s: couldn't read lux(HI) data\n", __FUNCTION__);
		return -EIO;
	}

	/* calculate the lux value */
	exponent = (lux_high >> 4) & 0x0F;
	if (exponent == 0x0F) {
		printk("%s: overload on light sensor!\n", __FUNCTION__);
		/* maximum reading (per datasheet) */
		*adjusted_lux = 188006;
		return -EINVAL;
	}

	mantissa = (lux_high & 0x0F);
	/* LUX = [2pow(exp)] * mantissa * 0.72 */
	*adjusted_lux = ((int)(1 << exponent)) * ((int) mantissa) * ALS_LUX_PER_COUNT_LOW_RES;

	/*
	 * printk(KERN_INFO "%s lux=%d\n",__FUNCTION__, *adjusted_lux);
	 */
	/* TODO: add any other adjustments
	 * 		 (e.g. correct for a lens,glass, other filtering, etc)
	 */
	return 0;
}

#endif

/*
 *  Converts a lux value into a threshold mantissa and exponent.
 */
int max44009_lux_to_thresh(int lux)
{
	long result = 0;
	int mantissa = 0;
	int mantissa_round_up;
	int exponent = 0;

	if (lux == 0) {
		/* 0 Has no Mantissa */
	  dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d: max44009 lux_to_thresh lux=0x%04x result=0x%02x\n", __FUNCTION__, __LINE__, lux, 0);
		return 0;
	}

	result = lux / ALS_LUX_PER_COUNT_HI_RES;
	while (result >= 0xFF) {
		result = result >> 1;
		exponent++;
	}

	mantissa_round_up = ((result & 0xf) > 0x7);
	mantissa = (result >> 4) & 0xf;
	mantissa += mantissa_round_up;

	result = mantissa + (exponent << 4);

	dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:max44009 lux_to_thresh lux=%d mantissa 0x%x exponent 0x%x result=0x%x\n", __FUNCTION__, __LINE__, lux, mantissa, exponent, (int)result);

	return (int)result;
}

#ifdef MAX44009_AUTO_MODE
/*
 * Read the ambient light level from MAX44009 device & sets the reading
 * as an input or kobject event on the if the reading is valid
 */
int max44009_report_light_level(void)
{
	int err;
	int lux;
	struct max44009_thresh_zone zone;

	err = max44009_read_lux_oneshot(&lux);
	if (err == 0) {

		dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d: to upper layer lux=%d\n", __FUNCTION__, __LINE__, lux);

		max44009_xthresh_uev(lux);

		if(lux < MAX_LUX_FORCE_REPORTING){
			//sensitive low lux threshold
			//Make sure threshold is always crossed by setting lower > 30
			zone.upper_thresh = 0;
			zone.lower_thresh = 0xff;
		}else{
			/* Reset lux interrupt thresholds adjusted by hysteresis and
			   0.5 lux widener */
			zone.upper_thresh = max44009_lux_to_thresh(lux +
														   ((lux * MAX44009_HYSTERESIS) >> 6));

			if ((lux - ((lux * MAX44009_HYSTERESIS) >> 6)) < 0)
				{
					zone.lower_thresh = max44009_lux_to_thresh(0);
				}
			else
				{
					zone.lower_thresh = max44009_lux_to_thresh(lux - ((lux * MAX44009_HYSTERESIS) >> 6));
				}

		}

		err = max44009_set_threshold_zone(&zone);

#ifdef MAX44009_INPUT_DEV
		/*  Report event to upper layer */
		input_report_abs(max44009_idev, ABS_MISC, lux);
		input_sync(max44009_idev);
#endif
	} else {
		printk(KERN_ERR "%s: problem getting lux reading from MAX44009\n", __FUNCTION__);
	}

	return err;
}
#endif

static void max44009_als_wqthread(struct work_struct *dummy)
{
	int irq = gpio_max44009_als_int();
	u8 intsts = 0;
	int err = 0;

#ifdef MAX44009_AUTO_MODE
	max44009_report_light_level();
#endif

	err = max44009_read_reg(MAX44009_INT_STATUS_ADDR, &intsts);
	if (err) {
		printk(KERN_ERR "%s: couldn't read interrupt sts : %d  \n", __FUNCTION__, err);
	}
	enable_irq(irq);
}

static irqreturn_t max44009_als_irq(int irq, void *device)
{
	disable_irq_nosync(irq);
	schedule_work(&max44009_als_wq);
	return IRQ_HANDLED;
}

/*
 * Initialize max44009 to low power manual mode
 */
static int max44009_init_lpm(void)
{
	int err;

	err = max44009_init_mm(0);
	if (err) {
		printk(KERN_ERR "%s: manual mode failed: %d\n", __FUNCTION__, err);
		return -1;
	}

	return 0;
}

/*
 * Initialize max44009 to manual mode
 */
static int max44009_init_mm(int lpm)
{
	int err = 0;

	dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);

    /* initialize configuration register - cont mode disabled */
	if(lpm)
		err = max44009_write_reg(MAX44009_CONFIG_ADDR, 0x4F);
	else
		err = max44009_write_reg(MAX44009_CONFIG_ADDR, 0x40);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__, MAX44009_CONFIG_ADDR);
		goto failed;
	}

	err = max44009_write_reg(MAX44009_INT_ENABLE_ADDR, 0x0);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__,MAX44009_INT_ENABLE_ADDR);
		goto failed;
	}
    /* initialize upper threshold */
	err = max44009_write_reg(MAX44009_THRESH_HIGH_ADDR, 0xFF);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__, MAX44009_THRESH_HIGH_ADDR);
		goto failed;
	}

    /* initialize lower threshold */
	err = max44009_write_reg(MAX44009_THRESH_LOW_ADDR, 0x0);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__, MAX44009_THRESH_LOW_ADDR);
		goto failed;
	}

	/* initialize threshold timer */
	err = max44009_write_reg(MAX44009_THRESH_TIM_ADDR, 0xFF);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__, MAX44009_THRESH_TIM_ADDR);
		goto failed;
	}
	return 0;

failed:
	printk("%s: couldn't initialize manual mode \n", __FUNCTION__);
	return err;
}


/*
TBD
early_param("ALS_GAIN", parse_gains);

static __init int parse_gains(char* str){

}
*/

static int bootparam_get_gains(u8* g1, u8* g2){
	//TBD
	return -1;
}

/*
 * max44009_init_gain()
 * Switch to customer mode.
 * Duplicate the clock registers from factory mode to customer mode.
 * Initialize the gain G1 and G2 registers
 */
static int max44009_init_gain(void)
{
	int err = 0;
	u8 clock_1, clock_2, ctrl_reg;
        u8 g1, g2;

	if( bootparam_get_gains(&g1, &g2) ){
		g1 = ALS_CAL_DEFAULT_G1_VAL;
		g2 = ALS_CAL_DEFAULT_G2_VAL;
	}

	/*
	 * Read control register.
	 */
	err = max44009_read_reg(MAX44009_CONTROL_REG, &ctrl_reg);
	if (err) {
		printk(KERN_ERR "%s reg %d write %d failed \n", __FUNCTION__, MAX44009_CONTROL_REG, ctrl_reg);
		return err;
	}
	/*
	 * Change to factory mode to read the default clock registers
	 */
	err = max44009_write_reg(MAX44009_CONTROL_REG, ctrl_reg & ~OTPSEL_BIT);
	if (err) {
		printk(KERN_ERR "%s reg %d write %d failed \n", __FUNCTION__, MAX44009_CONTROL_REG, ctrl_reg | OTPSEL_BIT);
		return err;
	}

	/*
	 * Save clock register from factory mode
	 */
	err = max44009_read_reg(MAX44009_CLOCK_REG_1, &clock_1);
	if (err) {
		printk(KERN_ERR "%s reg %d read failed \n", __FUNCTION__, MAX44009_CLOCK_REG_1);
		return err;
	}
	err = max44009_read_reg(MAX44009_CLOCK_REG_2, &clock_2);
	if (err) {
		printk(KERN_ERR "%s reg %d read failed \n", __FUNCTION__, MAX44009_CLOCK_REG_2);
		return err;
	}
	/*
	 * Enable customer mode
	 */
	err = max44009_write_reg(MAX44009_CONTROL_REG, ctrl_reg | OTPSEL_BIT);
	if (err) {
		printk(KERN_ERR "%s reg %d write %d failed \n", __FUNCTION__, MAX44009_CONTROL_REG, ctrl_reg | OTPSEL_BIT);
		return err;
	}
	/*
	 * Duplicate clock registers to customer mode
	 */
	err = max44009_write_reg(MAX44009_CLOCK_REG_1, clock_1);
	if (err) {
		printk(KERN_ERR "%s reg %d write %d failed \n", __FUNCTION__, MAX44009_CLOCK_REG_1, clock_1);
		return err;
	}
	err = max44009_write_reg(MAX44009_CLOCK_REG_2, clock_2);
	if (err) {
		printk(KERN_ERR "%s reg %d write %d failed \n", __FUNCTION__, MAX44009_CLOCK_REG_2, clock_2);
		return err;
	}
	/*
	 * Set the default gain registers.
	 * system should initialize the gain registers again if it is calibrated
	 */
	err = max44009_write_reg(MAX44009_GAIN_REG_1, g1);
	if (err) {
		printk(KERN_ERR "%s reg %d write %d failed \n", __FUNCTION__, MAX44009_GAIN_REG_1, g1);
		return err;
	}
	err = max44009_write_reg(MAX44009_GAIN_REG_2, g2);
	if (err) {
		printk(KERN_ERR "%s reg %d write %d failed \n", __FUNCTION__, MAX44009_GAIN_REG_2, g2);
		return err;
	}
	return err;
}

/*
 * Initialize max44009 to continuous mode with
 * threshold (lower & upper) & interrupt enabled
 */
static int max44009_init_automode(void)
{
	int err = 0;

	/* continues mode disable*/
	err = max44009_write_reg(MAX44009_CONFIG_ADDR, 0x40);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__,MAX44009_CONFIG_ADDR);
		goto failed;
	}

	/* initialize interrupt configuration register */
	err = max44009_write_reg(MAX44009_INT_ENABLE_ADDR, 0x01);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__,MAX44009_INT_ENABLE_ADDR);
		goto failed;
	}

	/* initialize upper threshold */
	/* Sunlight=0xFD; Roomlight=0x88; Hysteresis=0x0 */
	err = max44009_write_reg(MAX44009_THRESH_HIGH_ADDR, 0x0);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__, MAX44009_THRESH_HIGH_ADDR);
		goto failed;
	}

	/* initialize lower threshold */
	/* Shadow=0x58 ; Cover=0x16 ; Hysteresis=0xFF */
	err = max44009_write_reg(MAX44009_THRESH_LOW_ADDR, 0xFF);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__, MAX44009_THRESH_LOW_ADDR);
		goto failed;
	}

	/* initialize threshold timer to 1 sec*/
	err = max44009_write_reg(MAX44009_THRESH_TIM_ADDR, THRESHOLD_TIMER_1SEC);
	if (err) {
		printk(KERN_ERR "%s reg %d write failed \n", __FUNCTION__, MAX44009_THRESH_TIM_ADDR);
		goto failed;
	}
	return 0;

failed:
	printk("%s: couldn't initialize %d\n", __FUNCTION__,err);
	return err;
}

static int __devinit max44009_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	int err = 0;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int irq = 0;
#ifdef MAX44009_INPUT_DEV
	struct input_dev *idev;
#endif
        dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);
	wario_als_gpio_init();
	irq = gpio_max44009_als_int();

	if(!i2c_check_functionality(adapter, I2C_FUNC_I2C |
		I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA)) {
		printk(KERN_ERR "%s: I2C_FUNC not supported\n", __FUNCTION__);
		return -ENODEV;
	}

#ifdef MAX44009_INPUT_DEV
	max44009_idev = input_allocate_device();
	if (!max44009_idev) {
		dev_err(&client->dev, "alloc input device failed!\n");
		return -ENOMEM;
	}
	idev = max44009_idev;
	idev->name = MAX44009_ALS;
	idev->id.bustype = BUS_I2C;
	idev->dev.parent = &client->dev;
	input_set_capability(idev, EV_MSC, MSC_RAW);

	err = input_register_device(idev);
	if (err) {
		dev_err(&client->dev, "register input device failed!\n");
		goto err_reg_device;
	}
#endif

	max44009_hwmon = hwmon_device_register(&client->dev);
	if (IS_ERR(max44009_hwmon)) {
		printk(KERN_ERR "%s: Failed to initialize hw monitor\n", __FUNCTION__);
		err = -EINVAL;
		goto err_hwmon;
	}

	max44009_i2c_client = client;

	err = max44009_init_mm(0);
	if (err) {
		printk(KERN_ERR "%s: Register initialization LPM failed: %d\n", __FUNCTION__, err);
		err = -ENODEV;
		goto err_init;
	}

        err = max44009_init_gain();
        if (err) {
                printk(KERN_ERR "%s: Register initialization gain failed: %d\n", __FUNCTION__, err);
                err = -ENODEV;
                goto err_init;
        }
	err = request_irq(irq, max44009_als_irq,
					IRQF_DISABLED | IRQF_TRIGGER_FALLING, "max44009_int", NULL);
	if (err != 0) {
		printk(KERN_ERR "IRQF_DISABLED: Could not get IRQ %d\n", irq);
		goto err_init;
	}

	if (misc_register(&als_max44009_misc_device)) {
		err = -EBUSY;
		printk(KERN_ERR "%s Couldn't register device %d \n",__FUNCTION__, MAX44009_ALS_DEV_MINOR);
		goto err_irq;
	}

	err = sysdev_class_register(&max44009_ctrl_sysclass);
	if (!err)
		err = sysdev_register(&device_max44009_ctrl);
	if (!err) {
		err = sysdev_create_file(&device_max44009_ctrl, &attr_max44009_regaddr);
		err = sysdev_create_file(&device_max44009_ctrl, &attr_max44009_regval);
		err = sysdev_create_file(&device_max44009_ctrl, &attr_max44009_lux);
		err = sysdev_create_file(&device_max44009_ctrl, &attr_max44009_lux_auto_on);
		err = sysdev_create_file(&device_max44009_ctrl, &attr_max44009_lux_count);
	}

	return 0;

err_irq:
	free_irq(gpio_max44009_als_int(), NULL);
err_init:
	max44009_i2c_client = NULL;
	hwmon_device_unregister(max44009_hwmon);
	max44009_hwmon = NULL;
err_hwmon:
#ifdef MAX44009_INPUT_DEV
	input_unregister_device(max44009_idev);
err_reg_device:
	input_free_device(max44009_idev);
	max44009_idev = NULL;
#endif
	return err;
}

static int __devexit max44009_remove(struct i2c_client *client)
{
	dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);
	sysdev_remove_file(&device_max44009_ctrl, &attr_max44009_regaddr);
	sysdev_remove_file(&device_max44009_ctrl, &attr_max44009_regval);
	sysdev_remove_file(&device_max44009_ctrl, &attr_max44009_lux);
	sysdev_remove_file(&device_max44009_ctrl, &attr_max44009_lux_auto_on);
	sysdev_remove_file(&device_max44009_ctrl, &attr_max44009_lux_count);

	sysdev_unregister(&device_max44009_ctrl);
	sysdev_class_unregister(&max44009_ctrl_sysclass);

	misc_deregister(&als_max44009_misc_device);
	free_irq(gpio_max44009_als_int(), NULL);

	hwmon_device_unregister(max44009_hwmon);
	max44009_hwmon = NULL;
#ifdef MAX44009_INPUT_DEV
	if (max44009_idev) {
		input_unregister_device(max44009_idev);
		input_free_device(max44009_idev);
		max44009_idev = NULL;
	}
#endif
	return 0;
}

static int max44009_suspend(struct i2c_client *client, pm_message_t mesg)
{

	if(max44009_init_lpm())
		printk(KERN_ERR "%s:%d Could not initialize low power mode\n", __func__,__LINE__);

	return 0;
}

static int max44009_resume(struct i2c_client *client)
{
	if(automode){
		if(max44009_init_automode())
			printk(KERN_ERR "%s:%d Could not initialize automode\n", __func__,__LINE__);
	}

	return 0;
}

static const struct i2c_device_id max44009_id[] =  {
	{MAX44009_ALS, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max44009_id);

static struct i2c_driver max44009_i2c_driver = {
	.driver = {
		.name = MAX44009_ALS,
		},
	.probe = max44009_probe,
	.remove = max44009_remove,
#ifdef CONFIG_PM
	.suspend = max44009_suspend,
	.resume = max44009_resume,
#endif
	.id_table = max44009_id,
};

static int __init als_max44009_init(void)
{
	int err = 0;
	dev_dbg(als_max44009_misc_device.this_device , "\n%s:%d:", __FUNCTION__, __LINE__);
	err = i2c_add_driver(&max44009_i2c_driver);
	if (err) {
		printk(KERN_ERR "%s:%d Could not add i2c driver\n", __func__,__LINE__);
		return err;
	}

	return 0;
}


static void __exit als_max44009_exit(void)
{
	i2c_del_driver(&max44009_i2c_driver);
}

module_init(als_max44009_init);
module_exit(als_max44009_exit);

MODULE_DESCRIPTION("MX60 WARIO MAXIM ALS (MAX44009) Driver");
MODULE_AUTHOR("Vidhyananth Venkatasamy");
MODULE_LICENSE("GPL");
