/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef __SX9500_SPECIFICS_H__
#define __SX9500_SPECIFICS_H__

#include <linux/input/smtc/misc/sx9500_platform_data.h>
#include <linux/input/smtc/misc/sx9500_i2c_reg.h>

static int sx9500_get_nirq_state(void)
{
	return !gpio_get_value(GPIO_SX9500_NIRQ);
}

/* Define Registers that need to be initialized to values different than
 * default
 */
static struct smtc_reg_data sx9500_i2c_reg_setup[] = {
	{
		.reg = SX9306_IRQMASK_REG,	/* 0x03 */
		.val = 0x70,			/* Enable CLOSEIRQEN, FARIRQEN, COMPDONEIRQEN */
	},
	{
		.reg = SX9306_PROX_CTRL0_REG,	/* 0x06 */
		.val = 0x18,
	},
	{
		.reg = SX9306_PROX_CTRL1_REG,	/* 0x07 */
		.val = 0x43,			/* small cap range */
	},
	{
		.reg = SX9306_PROX_CTRL2_REG,	/* 0x08 */
		.val = 0x77,			/* x8 digital gain, **83khz**, max precision */
	},
	{
		.reg = SX9306_PROX_CTRL3_REG,	/* 0x09 */
		.val = 0x62,
	},
	{
		.reg = SX9306_PROX_CTRL4_REG,	/* 0x0A */
		.val = 0x96,			/* Set compensation trigger thresholds */
	},
	{
		.reg = SX9306_PROX_CTRL5_REG,	/* 0x0B */
		.val = 0x0F,			/* No debounce, max neg flt, min pos flt */
	},
	{
		.reg = SX9306_PROX_CTRL6_REG,	/* 0x0C */
		.val = 0x01,			/* set PROXTHRESH = 20 counts */
	},
	{
		.reg = SX9306_PROX_CTRL7_REG,	/* 0x0D */
		.val = 0x30,			/* HYST = 0, will make it noisier? */
	},
	{
		.reg = SX9306_PROX_CTRL8_REG,	/* 0x0E */
		.val = 0x00,			/*  */
	},
	{
		.reg = SX9306_SAR_CTRL0_REG,	/* 0x0F */
		.val = 0x00,			/*  */
	},
	{
		.reg = SX9306_SAR_CTRL1_REG,	/* 0x10 */
		.val = 0x00,			/*  */
	},
	{
		.reg = SX9306_SENSOR_SEL_REG,	/* 0x20 */
		.val = 0x03,			/* CS3  */
	},
	{
		.reg = SX9306_IRQSRC_REG,	/* 0x00 */
		.val = 0x10,			/* Enable COMPDONEIRQ irq */
	},
};


static struct _buttonInfo psmtcButtons[] = {
	{
		.keycode = KEY_0,
		.mask = SX9500_TCHCMPSTAT_TCHSTAT0_FLAG,
	},
	{
		.keycode = KEY_1,
		.mask = SX9500_TCHCMPSTAT_TCHSTAT1_FLAG,
	},
	{
		.keycode = KEY_2,
		.mask = SX9500_TCHCMPSTAT_TCHSTAT2_FLAG,
	},
	{
		.keycode = KEY_3,
		.mask = SX9500_TCHCMPSTAT_TCHSTAT3_FLAG,
	},
};

static struct _totalButtonInformation smtcButtonInformation = {
	.buttons = psmtcButtons,
	.buttonSize = ARRAY_SIZE(psmtcButtons),
};

static sx9500_platform_data_t sx9500_config = {
	/* Function pointer to get the NIRQ state (1->NIRQ-low, 0->NIRQ-high) */
	.get_is_nirq_low = sx9500_get_nirq_state,
	/* pointer to an initializer function. Here in case needed in the future */
	//.init_platform_hw = sx9500_init_ts,
	.init_platform_hw = NULL,
	/* pointer to an exit function. Here in case needed in the future */
	//.exit_platform_hw = sx9500_exit_ts,
	.exit_platform_hw = NULL,
	
	.pi2c_reg = sx9500_i2c_reg_setup,
	.i2c_reg_num = ARRAY_SIZE(sx9500_i2c_reg_setup),

	.pbuttonInformation = &smtcButtonInformation,
};

#endif /* __SX9500_SPECIFICS_H__ */
