/*! \file sx9500.c
 * \brief  SX9500 Driver
 *
 * Driver for the SX9500 
 * Copyright (c) 2011 Semtech Corp
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
//#define DEBUG
#define DRIVER_NAME "sx9500"

#define MAX_WRITE_ARRAY_SIZE 32
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/sysfs.h>
#include <linux/sysdev.h>

#include <linux/input/smtc/misc/sx86xx.h> /* main struct, interrupt,init,pointers */
#include <linux/input/smtc/misc/sx9500_i2c_reg.h>
#include <linux/input/smtc/misc/sx9500_platform_data.h>  /* platform data */

extern int sx9500_gpio_init(void);
extern int sx9500_gpio_proximity_int(void);

#define IDLE	0
#define ACTIVE	1

static struct i2c_client *proximity_i2c_client;
static psx86XX_t s_psx86xx;

/*! \struct sx9500
 * Specialized struct containing input event data, platform data, and
 * last cap state read if needed.
 */
typedef struct sx9500
{
	pbuttonInformation_t pbuttonInformation;
	psx9500_platform_data_t hw; /* specific platform data settings */
} sx9500_t, *psx9500_t;


/*! \fn static int write_register(psx86XX_t this, u8 address, u8 value)
 * \brief Sends a write register to the device
 * \param this Pointer to main parent struct 
 * \param address 8-bit register address
 * \param value   8-bit register value to write to address
 * \return Value from i2c_master_send
 */
static int write_register(psx86XX_t this, u8 address, u8 value)
{
	struct i2c_client *i2c = 0;
	char buffer[2];
	int returnValue = 0;

	buffer[0] = address;
	buffer[1] = value;
	returnValue = -ENOMEM;
	if (this && this->bus) {
		i2c = this->bus;

		returnValue = i2c_master_send(i2c, buffer, 2);
		dev_dbg(&i2c->dev, "write_register Address: 0x%x Value: 0x%x Return: %d\n",
        		address, value, returnValue);
	}

	return returnValue;
}

/*! \fn static int read_register(psx86XX_t this, u8 address, u8 *value) 
* \brief Reads a register's value from the device
* \param this Pointer to main parent struct 
* \param address 8-Bit address to read from
* \param value Pointer to 8-bit value to save register value to 
* \return Value from i2c_smbus_read_byte_data if < 0. else 0
*/
static int read_register(psx86XX_t this, u8 address, u8 *value)
{
	struct i2c_client *i2c = 0;
	s32 returnValue = 0;

	if (this && value && this->bus) {
		i2c = this->bus;
		returnValue = i2c_smbus_read_byte_data(i2c, address);
		dev_dbg(&i2c->dev, "read_register Address: 0x%x Return: 0x%x\n", address, returnValue);
		if (returnValue >= 0) {
			*value = returnValue;
			return 0;
		} else {
			return returnValue;
		}
	}

	return -ENOMEM;
}

#ifdef EXTENDED_I2C_RDWD
/*! \brief Sends a write register range to the device
 * \param this Pointer to main parent struct 
 * \param reg 8-bit register address (base address)
 * \param data pointer to 8-bit register values
 * \param size size of the data pointer
 * \return Value from i2c_master_send
 */
static int write_registerEx(psx86XX_t this, unsigned char reg,
				unsigned char *data, int size)
{
	struct i2c_client *i2c = 0;
	u8 tx[MAX_WRITE_ARRAY_SIZE];
	int ret = 0;

	if (this && (i2c = this->bus) && data && (size <= MAX_WRITE_ARRAY_SIZE))
	{
		dev_dbg(this->pdev, "inside write_registerEx()\n");
		tx[0] = reg;
		dev_dbg(this->pdev, "going to call i2c_master_send(0x%p, 0x%x ",
				(void *)i2c, tx[0]);
		for (ret = 0; ret < size; ret++)
		{
			tx[ret+1] = data[ret];
			dev_dbg(this->pdev, "0x%x, ", tx[ret+1]);
		}
		dev_dbg(this->pdev, "\n");

		ret = i2c_master_send(i2c, tx, size+1 );
		if (ret < 0)
			dev_err(this->pdev, "I2C write error\n");
	}
	dev_dbg(this->pdev, "leaving write_registerEx()\n");

	return ret;
}

/*! \brief Reads a group of registers from the device
* \param this Pointer to main parent struct 
* \param reg 8-Bit address to read from (base address)
* \param data Pointer to 8-bit value array to save registers to 
* \param size size of array
* \return Value from i2c_smbus_read_byte_data if < 0. else 0
*/
static int read_registerEx(psx86XX_t this, unsigned char reg,
				unsigned char *data, int size)
{
	struct i2c_client *i2c = 0;
	int ret = 0;
	u8 tx[] = {
		reg
	};

	if (this && (i2c = this->bus) && data && (size <= MAX_WRITE_ARRAY_SIZE))
	{
		dev_dbg(this->pdev, "inside read_registerEx()\n");
		dev_dbg(this->pdev,
			"going to call i2c_master_send(0x%p,0x%p,1) Reg: 0x%x\n",
			 (void *)i2c, (void *)tx, tx[0]);
		ret = i2c_master_send(i2c, tx, 1);
		if (ret >= 0) {
			dev_dbg(this->pdev, "going to call i2c_master_recv(0x%p,0x%p,%x)\n",
				(void *)i2c, (void *)data, size);
			ret = i2c_master_recv(i2c, data, size);
		}
	}
	if (unlikely(ret < 0))
		dev_err(this->pdev, "I2C read error\n");
	dev_dbg(this->pdev, "leaving read_registerEx()\n");

	return ret;
}
#endif
/*********************************************************************/
/*! \brief Perform a manual offset calibration
* \param this Pointer to main parent struct 
* \return Value return value from the write register
 */
static int manual_offset_calibration(psx86XX_t this)
{
	s32 returnValue = 0;
	returnValue = write_register(this, SX9500_IRQSTAT_REG, 0xFF);
	return returnValue;
}
/*! \brief sysfs show function for manual calibration which currently just
 * returns register value.
 */
static ssize_t manual_offset_calibration_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	u8 reg_value = 0;
	psx86XX_t this = dev_get_drvdata(dev);

	dev_dbg(this->pdev, "Reading IRQSTAT_REG\n");
	read_register(this, SX9500_IRQSTAT_REG, &reg_value);

	return sprintf(buf, "%d\n", reg_value);
}

/*! \brief sysfs store function for manual calibration
 */
static ssize_t manual_offset_calibration_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	psx86XX_t this = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if (val) {
		dev_info( this->pdev, "Performing manual_offset_calibration()\n");
		manual_offset_calibration(this);
	}

	return count;
}

static DEVICE_ATTR(calibrate, 0666, manual_offset_calibration_show,
                                manual_offset_calibration_store);
static struct attribute *sx9500_attributes[] = {
	&dev_attr_calibrate.attr,
	NULL,
};
static struct attribute_group sx9500_attr_group = {
	.attrs = sx9500_attributes,
};
/*********************************************************************/


/*! \fn static int read_regStat(psx86XX_t this)
 * \brief Shortcut to read what caused interrupt.
 * \details This is to keep the drivers a unified
 * function that will read whatever register(s) 
 * provide information on why the interrupt was caused.
 * \param this Pointer to main parent struct 
 * \return If successful, Value of bit(s) that cause interrupt, else 0
 */
static int read_regStat(psx86XX_t this)
{
	u8 data = 0;

	if (this) {
		if (read_register(this, SX9500_IRQSTAT_REG, &data) == 0)
			return (data & 0x00FF);
	}

	return 0;
}

/*! \brief  Initialize I2C config from platform data
 * \param this Pointer to main parent struct 
 */
static void hw_init(psx86XX_t this)
{
	psx9500_t pDevice = 0;
	psx9500_platform_data_t pdata = 0;
	int i = 0;

	/* configure device */
	dev_dbg(this->pdev, "Going to Setup I2C Registers\n");
	if (this && (pDevice = this->pDevice) && (pdata = pDevice->hw))
	{
		while ( i < pdata->i2c_reg_num) {
			/* Write all registers/values contained in i2c_reg */
			dev_dbg(this->pdev, "Going to Write Reg: 0x%x Value: 0x%x\n",
					pdata->pi2c_reg[i].reg, pdata->pi2c_reg[i].val);
//			msleep(3);				
			write_register(this, pdata->pi2c_reg[i].reg, pdata->pi2c_reg[i].val);
			i++;
		}
	} else
		dev_err(this->pdev, "ERROR! platform data 0x%p\n", pDevice->hw);
}
/*********************************************************************/


/*! \fn static int initialize(psx86XX_t this)
 * \brief Performs all initialization needed to configure the device
 * \param this Pointer to main parent struct 
 * \return Last used command's return value (negative if error)
 */
static int initialize(psx86XX_t this)
{
	int count = 0;

	if (this) {
		/* prepare reset by disabling any irq handling */
		this->irq_disabled = 1;
		disable_irq(this->irq);

		/* perform a reset */
		write_register(this, SX9500_SOFTRESET_REG, SX9500_SOFTRESET);
		/* wait until the reset has finished by monitoring NIRQ */
		dev_dbg(this->pdev, "Sent Software Reset. Waiting until device is back from reset to continue.\n");
		/* just sleep for awhile instead of using a loop with reading irq status */

		/* Based on vendor's comments, check NIRQ to detemine if prox chipset is out of reset
		   added 300ms timeout, i.e. max 300ms delay as before, and keep going after timeout */
		while(this->get_nirq_low && !(this->get_nirq_low()) && count < 300) {
			msleep(1);
			count++;
		}
		read_regStat(this);

		if (count >= 300)
			printk(KERN_ERR "Waiting NIRQ low timed out at 300ms!\n");

		dev_dbg(this->pdev, "Device is back from the reset, continuing. NIRQ = %d\n", this->get_nirq_low());
		hw_init(this);
		manual_offset_calibration(this);

		/* re-enable interrupt handling */
		enable_irq(this->irq);
		this->irq_disabled = 0;

		/* make sure no interrupts are pending since enabling irq will only
		 * work on next falling edge */
		read_regStat(this);
		dev_dbg(this->pdev, "Exiting initialize(). NIRQ = %d\n", this->get_nirq_low());

		return 0;
	}

	return -ENOMEM;
}


/*! 
 * \brief Handle what to do when a touch occurs
 * \param this Pointer to main parent struct 
 */
static void touchProcess(psx86XX_t this)
{
	int counter = 0;
	u8 i = 0;
	int numberOfButtons = 0;
	psx9500_t pDevice = NULL;
	struct _buttonInfo *buttons = NULL;
	struct input_dev *input = NULL;
	struct _buttonInfo *pCurrentButton = NULL;

	if (this && (pDevice = this->pDevice))
	{
		dev_dbg(this->pdev, "Inside touchProcess()\n");
		read_register(this, SX9500_TCHCMPSTAT_REG, &i);

		buttons = pDevice->pbuttonInformation->buttons;
		input = pDevice->pbuttonInformation->input;
		numberOfButtons = pDevice->pbuttonInformation->buttonSize;
		
		if (unlikely( (buttons==NULL) || (input==NULL) )) {
			dev_err(this->pdev, "ERROR!! buttons or input NULL!!!\n");
			return;
		}

		for (counter = 0; counter < numberOfButtons; counter++) {
			pCurrentButton = &buttons[counter];
			if (pCurrentButton==NULL) {
				dev_err(this->pdev, "ERROR!! current button at index: %d NULL!!!\n",
					counter);
				return; // ERRORR!!!!
			}
			switch (pCurrentButton->state) {
				case IDLE: /* Button is not being touched! */
					if (((i & pCurrentButton->mask) == pCurrentButton->mask)) {
						/* User pressed button */
						dev_info(this->pdev, "cap button %d touched\n", counter);
						input_report_key(input, pCurrentButton->keycode, 1);
						pCurrentButton->state = ACTIVE;
					} else {
						dev_dbg(this->pdev, "Button %d already released.\n", counter);
					}
					break;
				case ACTIVE: /* Button is being touched! */ 
					if (((i & pCurrentButton->mask) != pCurrentButton->mask)) {
						/* User released button */
						dev_info(this->pdev, "cap button %d released\n", counter);
						input_report_key(input, pCurrentButton->keycode, 0);
						pCurrentButton->state = IDLE;
					} else {
						dev_dbg(this->pdev, "Button %d still touched.\n", counter);
					}
					break;
				default: /* Shouldn't be here, device only allowed ACTIVE or IDLE */
					break;
			};
		}
		input_sync(input);

		dev_dbg(this->pdev, "Leaving touchProcess()\n");
	}
}


static u8 proximity_reg_number = 0;
static u8 proximity_regs[] = {
	SX9306_IRQSRC_REG,
	SX9306_STAT_REG,
	SX9306_IRQMASK_REG,
	SX9306_PROX_CTRL0_REG,
	SX9306_PROX_CTRL1_REG,
	SX9306_PROX_CTRL2_REG,
	SX9306_PROX_CTRL3_REG,
	SX9306_PROX_CTRL4_REG,
	SX9306_PROX_CTRL5_REG,
	SX9306_PROX_CTRL6_REG,
	SX9306_PROX_CTRL7_REG,
	SX9306_PROX_CTRL8_REG,
	SX9306_SAR_CTRL0_REG,
	SX9306_SAR_CTRL1_REG,
	SX9306_SENSOR_SEL_REG,
	SX9306_USE_MSB_REG,
	SX9306_USE_LSB_REG,
	SX9306_AVG_MSB_REG,
	SX9306_AVG_LSB_REG,
	SX9306_DIFF_MSB_REG,
	SX9306_DIFF_LSB_REG,
	SX9306_OFFSET_MSB_REG,
	SX9306_OFFSET_LSB_REG,
	SX9306_SAR_DELTA_REG,
	SX9306_SAR_RATIO_REG,
};
#define NUM_PROXIMITY_REGS (sizeof(proximity_regs)/sizeof(u8))
#define INVALID_REGNUM 0xFF

u8 check_register(u8 reg_num)
{
	u8 i = 0;

	for (i=0; i < NUM_PROXIMITY_REGS; i++)
		if (reg_num == proximity_regs[i])
			return reg_num;

	if (reg_num == SX9306_SOFTRESET_REG)
		return reg_num;
 
	printk(KERN_ERR "Invalid register 0x%02x\n", reg_num);
	return INVALID_REGNUM;
}

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

static int dump_regs(char *buf)
{
	char *curr = buf;
	u8 i = 0;
	u8 value = 0;

	for (i = 0; i < NUM_PROXIMITY_REGS; i++) {
		proximity_read_i2c(&value, proximity_regs[i]);
		curr += sprintf(curr, "Register 0x%02x: 0x%02x\n", proximity_regs[i], value);
	}
	return (curr - buf);
}

static ssize_t proximity_reg_store(struct device *dev, struct device_attribute *attr, 
				const char *buf, size_t size)
{
	u16 value = 0;

	if (sscanf(buf, "%hx", &value) <= 0) {
		printk(KERN_ERR "Could not store the codec register value\n");
		return -EINVAL;
	}

	proximity_reg_number = check_register((u8)value);
	return size;
}

static ssize_t proximity_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char *curr = buf;

	curr += sprintf(curr, "Proximity Register Number: 0x%02x\n", proximity_reg_number);
	curr += sprintf(curr, "\n");

	return curr - buf;
}

static DEVICE_ATTR(proximity_reg, 0644, proximity_reg_show, proximity_reg_store);

static ssize_t proximity_register_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char *curr = buf;
	u8 value = 0;

	if (proximity_reg_number == INVALID_REGNUM) {
		curr += sprintf(curr, "Proximity Registers\n");
		curr += sprintf(curr, "\n");
		curr += dump_regs(curr);
	} else if (proximity_reg_number == SX9306_SOFTRESET_REG) {
		curr += sprintf(curr, "Proximity Register 0x%02x\n", proximity_reg_number);
		curr += sprintf(curr, "Write only!\n");
	} else {
		proximity_read_i2c(&value, proximity_reg_number);
		curr += sprintf(curr, "Value: 0x%02x\n", value);
		curr += sprintf(curr, "\n");
		curr += sprintf(curr, "Proximity Register 0x%02x\n", proximity_reg_number);
		curr += sprintf(curr, "\n");
	}

	return curr - buf;
}

static ssize_t proximity_register_store(struct device *dev, struct device_attribute *attr, 
					const char *buf, size_t size)
{
	u8 value = 0;

	if (proximity_reg_number == INVALID_REGNUM) {
		printk(KERN_ERR "No codec register 0x%02x\n", proximity_reg_number);
		return size;
	}

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}
	
	proximity_write_i2c(proximity_reg_number, value);
	return size;
}

static DEVICE_ATTR(proximity_register, 0644, proximity_register_show, proximity_register_store);

static ssize_t prox_irqsrc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_IRQSRC_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_irqsrc_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_IRQSRC_REG, value);
	return size;
}

static DEVICE_ATTR(irqsrc, 0644, prox_irqsrc_show, prox_irqsrc_store);

static ssize_t prox_stat_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_STAT_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_stat_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_STAT_REG, value);
	return size;
}

static DEVICE_ATTR(stat, 0644, prox_stat_show, prox_stat_store);

static ssize_t prox_irqmask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_IRQMASK_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_irqmask_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_IRQMASK_REG, value);
	return size;
}

static DEVICE_ATTR(irqmask, 0644, prox_irqmask_show, prox_irqmask_store);

static ssize_t prox_ctrl0_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL0_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl0_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL0_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl0, 0644, prox_ctrl0_show, prox_ctrl0_store);

static ssize_t prox_ctrl1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL1_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl1_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL1_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl1, 0644, prox_ctrl1_show, prox_ctrl1_store);

static ssize_t prox_ctrl2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL2_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl2_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL2_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl2, 0644, prox_ctrl2_show, prox_ctrl2_store);

static ssize_t prox_ctrl3_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL3_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl3_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL3_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl3, 0644, prox_ctrl3_show, prox_ctrl3_store);

static ssize_t prox_ctrl4_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL4_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl4_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL4_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl4, 0644, prox_ctrl4_show, prox_ctrl4_store);

static ssize_t prox_ctrl5_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL5_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl5_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL5_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl5, 0644, prox_ctrl5_show, prox_ctrl5_store);

static ssize_t prox_ctrl6_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL6_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl6_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL6_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl6, 0644, prox_ctrl6_show, prox_ctrl6_store);

static ssize_t prox_ctrl7_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL7_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl7_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL7_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl7, 0644, prox_ctrl7_show, prox_ctrl7_store);

static ssize_t prox_ctrl8_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_PROX_CTRL8_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t prox_ctrl8_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_PROX_CTRL8_REG, value);
	return size;
}

static DEVICE_ATTR(prox_ctrl8, 0644, prox_ctrl8_show, prox_ctrl8_store);

static ssize_t sar_ctrl0_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_SAR_CTRL0_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t sar_ctrl0_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_SAR_CTRL0_REG, value);
	return size;
}

static DEVICE_ATTR(sar_ctrl0, 0644, sar_ctrl0_show, sar_ctrl0_store);

static ssize_t sar_ctrl1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_SAR_CTRL1_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t sar_ctrl1_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_SAR_CTRL1_REG, value);
	return size;
}

static DEVICE_ATTR(sar_ctrl1, 0644, sar_ctrl1_show, sar_ctrl1_store);

static ssize_t sensor_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_SENSOR_SEL_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static ssize_t sensor_sel_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u8 value = 0;

	if (sscanf(buf, "%hhx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	proximity_write_i2c(SX9306_SENSOR_SEL_REG, value);
	return size;
}

static DEVICE_ATTR(sensor_sel, 0644, sensor_sel_show, sensor_sel_store);

static ssize_t prox_use_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value_LSB, value_MSB;
	u16 value;

	proximity_read_i2c(&value_LSB, SX9306_USE_LSB_REG);
	proximity_read_i2c(&value_MSB, SX9306_USE_MSB_REG);
	value = (u16)(value_MSB<<8|value_LSB);
	return sprintf(buf, "0x%04x\n", value);
}

static DEVICE_ATTR(prox_use, 0644, prox_use_show, NULL);

static ssize_t prox_avg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value_LSB, value_MSB;
	u16 value;

	proximity_read_i2c(&value_LSB, SX9306_AVG_LSB_REG);
	proximity_read_i2c(&value_MSB, SX9306_AVG_MSB_REG);
	value = (u16)(value_MSB<<8|value_LSB);
	return sprintf(buf, "0x%04x\n", value);
}

static DEVICE_ATTR(prox_avg, 0644, prox_avg_show, NULL);

static ssize_t prox_diff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value_LSB, value_MSB;
	u16 value;

	proximity_read_i2c(&value_LSB, SX9306_DIFF_LSB_REG);
	proximity_read_i2c(&value_MSB, SX9306_DIFF_MSB_REG);
	value = (u16)(value_MSB<<8|value_LSB);
	return sprintf(buf, "0x%04x\n", value);
}

static DEVICE_ATTR(prox_diff, 0644, prox_diff_show, NULL);

static ssize_t prox_offset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value_LSB, value_MSB;
	u16 value;

	proximity_read_i2c(&value_LSB, SX9306_OFFSET_LSB_REG);
	proximity_read_i2c(&value_MSB, SX9306_OFFSET_MSB_REG);
	value = (u16)(value_MSB<<8|value_LSB);
	return sprintf(buf, "0x%04x\n", value);
}

static ssize_t prox_offset_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	u16 value = 0;

	if (sscanf(buf, "%hx", &value) <= 0) {
		printk(KERN_ERR "Error setting the value in the register\n");
		return -EINVAL;
	}

	printk(KERN_INFO "prox offset value: 0x%04x\n", value);
	proximity_write_i2c(SX9306_OFFSET_MSB_REG, (value >> 8));
	proximity_write_i2c(SX9306_OFFSET_LSB_REG, (value & 0x00FF));
	return size;
}

static DEVICE_ATTR(prox_offset, 0644, prox_offset_show, prox_offset_store);

static ssize_t sar_delta_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_SAR_DELTA_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static DEVICE_ATTR(sar_delta, 0644, sar_delta_show, NULL);

static ssize_t sar_ratio_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 value;

	proximity_read_i2c(&value, SX9306_SAR_RATIO_REG);
	return sprintf(buf, "0x%02x\n", value);
}

static DEVICE_ATTR(sar_ratio, 0644, sar_ratio_show, NULL);

static ssize_t prox_softreset_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
	printk(KERN_INFO "Resetting proximity sensor\n");
	proximity_write_i2c(SX9306_SOFTRESET_REG, 0xDE);

	return size;
}

static DEVICE_ATTR(softreset, 0644, NULL, prox_softreset_store);

static struct attribute *sx9306_reg_attributes[] = {
	&dev_attr_proximity_reg.attr,
	&dev_attr_proximity_register.attr,
	&dev_attr_irqsrc.attr,
	&dev_attr_stat.attr,
	&dev_attr_irqmask.attr,
	&dev_attr_prox_ctrl0.attr,
	&dev_attr_prox_ctrl1.attr,
	&dev_attr_prox_ctrl2.attr,
	&dev_attr_prox_ctrl3.attr,
	&dev_attr_prox_ctrl4.attr,
	&dev_attr_prox_ctrl5.attr,
	&dev_attr_prox_ctrl6.attr,
	&dev_attr_prox_ctrl7.attr,
	&dev_attr_prox_ctrl8.attr,
	&dev_attr_sar_ctrl0.attr,
	&dev_attr_sar_ctrl1.attr,
	&dev_attr_sensor_sel.attr,
	&dev_attr_prox_use.attr,
	&dev_attr_prox_avg.attr,
	&dev_attr_prox_diff.attr,
	&dev_attr_prox_offset.attr,
	&dev_attr_sar_delta.attr,
	&dev_attr_sar_ratio.attr,
	&dev_attr_softreset.attr,
	NULL,
};

static struct attribute_group sx9306_reg_attr_group = {
	.attrs = sx9306_reg_attributes,
};

static struct sysdev_class proximity_sysclass = {
	.name	= "proximity",
};

static struct sys_device device_proximity = {
	.id	= 0,
	.cls	= &proximity_sysclass,
};

/*! \fn static int sx9500_probe(struct i2c_client *client, const struct i2c_device_id *id)
 * \brief Probe function
 * \param client pointer to i2c_client
 * \param id pointer to i2c_device_id
 * \return Whether probe was successful
 */
static int sx9500_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int i = 0, error = 0;
	psx86XX_t this = 0;
	psx9500_t pDevice = 0;
	psx9500_platform_data_t pplatData = 0;
	struct input_dev *input = NULL;

	dev_info(&client->dev, "sx9500_probe()\n");

	pplatData = client->dev.platform_data;
	if (!pplatData) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

	this = kzalloc(sizeof(sx86XX_t), GFP_KERNEL); /* create memory for main struct */
	dev_dbg(&client->dev, "\t Initialized Main Memory: 0x%p\n", this);
  
	if (this)
	{
		/* save this pointer */
		s_psx86xx = this;

		/* In case we need to reinitialize data 
		 * (e.q. if suspend reset device) */
		this->init = initialize;
		/* shortcut to read status of interrupt */
		this->refreshStatus = read_regStat;
		/* pointer to function from platform data to get pendown 
		 * (1->NIRQ=0, 0->NIRQ=1) */
		this->get_nirq_low = pplatData->get_is_nirq_low;
		/* save irq in case we need to reference it */
		/* this->irq = client->irq; */
		this->irq = sx9500_gpio_proximity_int();
		/* do we need to create an irq timer after interrupt ? */
		this->useIrqTimer = 0;

		/* Setup function to call on corresponding reg irq source bit */
		if (MAX_NUM_STATUS_BITS >= 8)
		{
			this->statusFunc[0] = 0; /* TXEN_STAT */
			this->statusFunc[1] = 0; /* UNUSED */
			this->statusFunc[2] = 0; /* UNUSED */
			this->statusFunc[3] = 0; /* CONV_STAT */
			this->statusFunc[4] = 0; /* COMP_STAT */
			this->statusFunc[5] = touchProcess; /* RELEASE_STAT */
			this->statusFunc[6] = touchProcess; /* TOUCH_STAT	*/
			this->statusFunc[7] = 0; /* RESET_STAT */
		}

		/* setup i2c communication */
		this->bus = client;
		i2c_set_clientdata(client, this);
		proximity_i2c_client = client;

		/* record device struct */
		this->pdev = &client->dev;
	 
		/* create memory for device specific struct */
		this->pDevice = pDevice = kzalloc(sizeof(sx9500_t), GFP_KERNEL);
		dev_dbg(&client->dev, "\t Initialized Device Specific Memory: 0x%p\n", pDevice);

		if (pDevice)
		{
			/* for accessing items in user data (e.g. calibrate) */
			error = sysfs_create_group(&client->dev.kobj, &sx9500_attr_group);

			/* Check if we hava a platform initialization function to call*/
			if (pplatData->init_platform_hw)
				pplatData->init_platform_hw();

				/* Add Pointer to main platform data struct */
			pDevice->hw = pplatData;
			
			/* Initialize the button information initialized with keycodes */
			pDevice->pbuttonInformation = pplatData->pbuttonInformation;

			/* Create the input device */
			input = input_allocate_device();
			if (!input)
				return -ENOMEM;

			/* Set all the keycodes */
			__set_bit(EV_KEY, input->evbit);
			for (i = 0; i < pDevice->pbuttonInformation->buttonSize; i++) {
				__set_bit(pDevice->pbuttonInformation->buttons[i].keycode, 
						input->keybit);
				pDevice->pbuttonInformation->buttons[i].state = IDLE;
			}
			/* save the input pointer and finish initialization */
			pDevice->pbuttonInformation->input = input;
			input->name = "SX9500 Cap Touch";
			input->id.bustype = BUS_I2C;
//			input->id.product = sx863x->product;
//			input->id.version = sx863x->version;
			if(input_register_device(input))
				return -ENOMEM;
		}
		sx86XX_init(this);

		/* /sys files */
		error = sysdev_class_register(&proximity_sysclass);
		if (error) goto exit;
		error = sysdev_register(&device_proximity);
		if (error) goto exit;
		error = sysfs_create_group(&device_proximity.kobj, &sx9306_reg_attr_group);
		if (error) goto exit; 

		return  0;

exit:
		return -error;
	}

	return -1;
}

/*! \fn static int sx9500_remove(struct i2c_client *client)
 * \brief Called when device is to be removed
 * \param client Pointer to i2c_client struct
 * \return Value from sx86XX_remove()
 */
static int sx9500_remove(struct i2c_client *client)
{
	psx9500_platform_data_t pplatData = 0;
	psx9500_t pDevice = 0;
	psx86XX_t this = i2c_get_clientdata(client);

	if (this && (pDevice = this->pDevice))
	{
		input_unregister_device(pDevice->pbuttonInformation->input);

		sysfs_remove_group(&client->dev.kobj, &sx9500_attr_group);
		pplatData = client->dev.platform_data;
		if (pplatData && pplatData->exit_platform_hw)
			pplatData->exit_platform_hw();
		kfree(this->pDevice);
	}

	sysfs_remove_group(&device_proximity.kobj, &sx9306_reg_attr_group);
	sysdev_unregister(&device_proximity);
	sysdev_class_unregister(&proximity_sysclass);

	return sx86XX_remove(this);
}

#ifdef CONFIG_PM

static int ctrl0_suspended;
/*====================================================*/
/***** Kernel Suspend *****/
static int sx9500_suspend(struct i2c_client *client, pm_message_t mesg)
{
	psx86XX_t this = i2c_get_clientdata(client);

	sx86XX_suspend(this);
	read_register(this, SX9500_CPS_CTRL0_REG, &ctrl0_suspended);
	write_register(this, SX9500_CPS_CTRL0_REG, 0x00);
	return 0;
}
/***** Kernel Resume *****/
static int sx9500_resume(struct i2c_client *client)
{
	psx86XX_t this = i2c_get_clientdata(client);
	sx86XX_resume(this);
	write_register(this, SX9500_CPS_CTRL0_REG, ctrl0_suspended);
	return 0;
}
/*====================================================*/
#endif

static struct i2c_device_id sx9500_idtable[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, sx9500_idtable);

static struct i2c_driver sx9500_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name   = DRIVER_NAME,
	},
	.id_table = sx9500_idtable,
	.probe	  = sx9500_probe,
	.remove	  = __devexit_p(sx9500_remove),
#ifdef CONFIG_PM
	.suspend  = sx9500_suspend,
	.resume   = sx9500_resume,
#endif
};
static int __init sx9500_init(void)
{
	/* Init Proximity GPIO */
	sx9500_gpio_init();
		
	return i2c_add_driver(&sx9500_driver);
}
static void __exit sx9500_exit(void)
{
	i2c_del_driver(&sx9500_driver);
}

module_init(sx9500_init);
module_exit(sx9500_exit);

MODULE_AUTHOR("Semtech Corp. (http://www.semtech.com/)");
MODULE_DESCRIPTION("SX9500 Capacitive Touch Controller Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
