/*
 * Copyright 2012-2015 Amazon.com, Inc. All rights reserved.
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * Wario GPIO file for dynamic gpio configuration
 */

#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/fsl_devices.h>
#include <linux/power/soda_device.h>

#include <mach/boardid.h>
#include "board-mx6sl_wario.h"
#include "wario_iomux/include/iomux_define.h"
#include "wario_iomux/include/iomux_register.h"

#ifdef CONFIG_POWER_SODA
#include <linux/power_supply.h>
#endif
extern void usdhc2_iomux_config(void );

#if defined(CONFIG_MX6SL_WARIO_BASE)
static int gpio_wan_ldo_en = 0;
#endif

int gpio_max44009_als_int(void)
{
	return gpio_to_irq(MX6_WARIO_ALS_INT);
}
EXPORT_SYMBOL(gpio_max44009_als_int);

int wario_als_gpio_init(void)
{
	int ret = 0;
	ret = gpio_request(MX6_WARIO_ALS_INT, "als_max44009");
	if(unlikely(ret)) return ret;

	ret = gpio_direction_input(MX6_WARIO_ALS_INT);
	if(unlikely(ret)) goto free_als_gpio;

	return ret;
free_als_gpio:
	gpio_free(MX6_WARIO_ALS_INT);
	return ret;
}
EXPORT_SYMBOL(wario_als_gpio_init);

int gpio_accelerometer_int1(void)
{
	return gpio_to_irq(MX6_WARIO_ACCINT1);
}
EXPORT_SYMBOL(gpio_accelerometer_int1);

int gpio_accelerometer_int2(void)
{
	return gpio_to_irq(MX6_WARIO_ACCINT2);
}
EXPORT_SYMBOL(gpio_accelerometer_int2);

#ifdef CONFIG_WARIO_HALL
int gpio_hallsensor_irq(void)
{
	return gpio_to_irq(MX6_WARIO_HALL_SNS);
}
EXPORT_SYMBOL(gpio_hallsensor_irq);
#endif

/* proximity sensor related */

#if defined(CONFIG_INPUT_SX9500) || defined(CONFIG_INPUT_SX9500_MODULE)
int sx9500_gpio_init(void)
{
	int ret = 0;

	ret = gpio_request(GPIO_SX9500_NIRQ, "SX9500_NIRQ");
	if (ret) {
		printk(KERN_ERR "Could not obtain gpio for SX9500_NIRQ\n");
		return ret;
	}
	
	ret = gpio_direction_input(GPIO_SX9500_NIRQ);
	if (ret) {
		printk(KERN_ERR "Failed to set SX9500_NIRQ GPIO as input\n");
		gpio_free(GPIO_SX9500_NIRQ);
	}

	return ret;
}
EXPORT_SYMBOL(sx9500_gpio_init);

int sx9500_gpio_proximity_int(void)
{
	return gpio_to_irq(GPIO_SX9500_NIRQ);
}
EXPORT_SYMBOL(sx9500_gpio_proximity_int);
#else

int wario_prox_gpio_init(void)
{
	int ret = 0;

	ret = gpio_request(MX6_WARIO_PROX_INT, "prox_int");
	if (ret) return ret;
	ret = gpio_request(MX6_WARIO_PROX_RST, "prox_rst");
	if (ret) {
		gpio_free(MX6_WARIO_PROX_INT);
		return ret;
	}

	ret = gpio_direction_input(MX6_WARIO_PROX_INT);
	if (ret) goto err_exit;
	ret = gpio_direction_output(MX6_WARIO_PROX_RST, 0);
	if (ret) goto err_exit;

	return ret;

err_exit:
	gpio_free(MX6_WARIO_PROX_INT);
	gpio_free(MX6_WARIO_PROX_RST);

	return ret;
}
EXPORT_SYMBOL(wario_prox_gpio_init);

int gpio_proximity_int(void)
{
	return gpio_to_irq(MX6_WARIO_PROX_INT);
}
EXPORT_SYMBOL(gpio_proximity_int);

int gpio_proximity_detected(void)
{
	return gpio_get_value(MX6_WARIO_PROX_INT);
}
EXPORT_SYMBOL(gpio_proximity_detected);

void gpio_proximity_reset(void)
{
	gpio_set_value(MX6_WARIO_PROX_RST, 0);
	msleep(10);
	gpio_set_value(MX6_WARIO_PROX_RST, 1);
	msleep(10);
}
EXPORT_SYMBOL(gpio_proximity_reset);
#endif

void wario_fsr_init_pins(void)
{
	gpio_request(MX6SL_ARM2_EPDC_SDDO_8, "fsr_wake_int");
	gpio_request(MX6SL_ARM2_EPDC_SDDO_9, "fsr_reset");
	gpio_request(MX6SL_ARM2_EPDC_SDDO_10, "fsr_sqze_int");
	gpio_request(MX6SL_ARM2_EPDC_SDDO_11, "fsr_swd_clk");
	gpio_request(MX6SL_ARM2_EPDC_SDDO_12, "fsr_swdio");

	if(lab126_board_rev_greater_eq(BOARD_ID_WARIO_2)) //fsr spare is different on wario
	{
		gpio_request(MX6SL_PIN_KEY_COL1, "fsr_spare");
		gpio_direction_output(MX6SL_PIN_KEY_COL1, 1);
	}
	else if (lab126_board_is(BOARD_ID_ICEWINE_WARIO) ||
			lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO) ||
			lab126_board_is(BOARD_ID_ICEWINE_WARIO_512) ||
			lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO_512))

	{
		gpio_request(MX6SL_ARM2_EPDC_SDDO_13, "fsr_spare");
		gpio_direction_output(MX6SL_ARM2_EPDC_SDDO_13, 1);
	}
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_8);
	gpio_direction_output(MX6SL_ARM2_EPDC_SDDO_9, 1);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_10);
}
EXPORT_SYMBOL(wario_fsr_init_pins);

void fsr_set_pin(int which, int enable)
{
	switch(which)
	{
		case 8:
			gpio_direction_output(MX6SL_ARM2_EPDC_SDDO_8, enable);
			break;
		case 9:
			gpio_set_value(MX6SL_ARM2_EPDC_SDDO_9, enable);
			break;
		case 10:
			gpio_set_value(MX6SL_ARM2_EPDC_SDDO_10, enable);
			break;
		case 11:
			gpio_set_value(MX6SL_ARM2_EPDC_SDDO_11, enable);
			break;
		case 12:
			gpio_set_value(MX6SL_ARM2_EPDC_SDDO_12, enable);
			break;
		case 13:
			gpio_set_value(MX6SL_PIN_KEY_COL1, enable);
			break;
		default:
			break;
	}
}
EXPORT_SYMBOL(fsr_set_pin);

void fsr_wake_pin_pta5(int enable)
{
	if(lab126_board_rev_greater_eq(BOARD_ID_WARIO_2)) //fsr spare is different on wario
	{
		if(enable) {
			gpio_set_value(MX6SL_PIN_KEY_COL1, 0);
		}else{
			gpio_set_value(MX6SL_PIN_KEY_COL1, 1);
		}
	}else
	{
		if(enable) {
			gpio_set_value(MX6SL_ARM2_EPDC_SDDO_13, 0);
		}else{
			gpio_set_value(MX6SL_ARM2_EPDC_SDDO_13, 1);
		}
	}
	udelay(100);
}
EXPORT_SYMBOL(fsr_wake_pin_pta5);

void fsr_pm_pins(int suspend_resume)
{
	if(lab126_board_rev_greater_eq(BOARD_ID_WARIO_2)) //fsr spare is different on wario
	{
		if(suspend_resume == 0) {
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_8);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_9);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_10);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_11);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_12);
			gpio_direction_input(MX6SL_PIN_KEY_COL1);
		}else {
			gpio_direction_output(MX6SL_ARM2_EPDC_SDDO_9, 1);
			gpio_direction_output(MX6SL_PIN_KEY_COL1, 1);
		}
	}else
	{
		if(suspend_resume == 0) {
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_8);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_9);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_10);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_11);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_12);
			gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_13);
		}else {
			gpio_direction_output(MX6SL_ARM2_EPDC_SDDO_9, 1);
			gpio_direction_output(MX6SL_ARM2_EPDC_SDDO_13, 1);
		}
	}
	udelay(100);
}
EXPORT_SYMBOL(fsr_pm_pins);




int fsr_get_irq_state(void)
{
	return gpio_get_value(MX6SL_ARM2_EPDC_SDDO_10);
}
EXPORT_SYMBOL(fsr_get_irq_state);

void gpio_fsr_reset(void)
{
	gpio_set_value(MX6SL_ARM2_EPDC_SDDO_9, 0);
	msleep(1);
	gpio_set_value(MX6SL_ARM2_EPDC_SDDO_9, 1);
	msleep(1);
}
EXPORT_SYMBOL(gpio_fsr_reset);

int gpio_fsr_button_irq(void)
{
	return gpio_to_irq(MX6SL_ARM2_EPDC_SDDO_10);
}
EXPORT_SYMBOL(gpio_fsr_button_irq);

int gpio_fsr_bootloader_irq(void)
{
	return gpio_to_irq(MX6SL_ARM2_EPDC_SDDO_8);
}
EXPORT_SYMBOL(gpio_fsr_bootloader_irq);

int gpio_fsr_logging_irq(void)
{
	if(lab126_board_rev_greater_eq(BOARD_ID_WARIO_2)) //fsr spare is different on wario
		return gpio_to_irq(MX6SL_PIN_KEY_COL1);
	else
		return gpio_to_irq(MX6SL_ARM2_EPDC_SDDO_13);
}
EXPORT_SYMBOL(gpio_fsr_logging_irq);

#ifdef CONFIG_WARIO_HALL
/*
 * hall sensor gpio value hi is not detected, lo is detected
 */
int gpio_hallsensor_detect(void)
{
	if (lab126_board_rev_greater(BOARD_ID_WHISKY_WAN_HVT1) || lab126_board_rev_greater(BOARD_ID_WHISKY_WFO_HVT1) ||
		lab126_board_rev_greater_eq(BOARD_ID_WOODY_2)) {
		return gpio_get_value(MX6_WARIO_HALL_SNS);
	} else {
		return !gpio_get_value(MX6_WARIO_HALL_SNS);
	}
}
EXPORT_SYMBOL(gpio_hallsensor_detect);

void gpio_hallsensor_pullup(int enable)
{
	if (enable > 0) {
		mxc_iomux_v3_setup_pad(MX6SL_HALL_SNS(PU));
	} else {
		mxc_iomux_v3_setup_pad(MX6SL_HALL_SNS(PD));
	}
}
EXPORT_SYMBOL(gpio_hallsensor_pullup);
#endif

#ifdef CONFIG_POWER_SODA
/*
 * soda external charger detect gpio (active low) HI:no external charger; LO: external charger connected
 */
int gpio_soda_ext_chg_detect(unsigned gpio)
{
	return !gpio_get_value(gpio);
}
EXPORT_SYMBOL(gpio_soda_ext_chg_detect);

void gpio_soda_ctrl(unsigned gpio, int enable)
{
	if (enable > 0) {
		gpio_direction_output(gpio, 1);
	} else {
		gpio_direction_input(gpio);
	}
}
EXPORT_SYMBOL(gpio_soda_ctrl);

/*
 * soda dock detect gpio (active low) HI:soda disconnected; LO: soda connected
 */
int gpio_soda_dock_detect(unsigned gpio)
{
	return !gpio_get_value(gpio);
}
EXPORT_SYMBOL(gpio_soda_dock_detect);

int (*sodadev_get_sda_state)(int sda_enable, sda_state_t* state) = 0;
EXPORT_SYMBOL(sodadev_get_sda_state);

void soda_config_sda_line(int i2c_sda_enable)
{
    if (!sodadev_get_sda_state)
        return;

    int ret;
    sda_state_t state = {0};
    while (ret=sodadev_get_sda_state(i2c_sda_enable, &state)) {
        if (ret == 1) {
            __raw_writel(state.sda_val, state.sda_item);
        }
        else if (ret == 2) {
            gpio_direction_output(state.sda_item, 1);
        }
        else if (ret == 5) {
            gpio_direction_input(state.sda_item);
        }
        else {
            msleep(1);  
        }
    }
}
EXPORT_SYMBOL(soda_config_sda_line);

bool soda_i2c_reset(void)
{
	int i;

	gpio_direction_output(MX6_SODA_I2C_SCL, 1);
	gpio_direction_output(MX6_SODA_I2C_SDA, 1);

	udelay(I2C_BB_DELAY_US);

	/* tri-state before reset */
	gpio_direction_input(MX6_SODA_I2C_SCL);		
	gpio_direction_input(MX6_SODA_I2C_SDA);	

	if (!gpio_get_value(MX6_SODA_I2C_SCL)) {
		printk(KERN_ERR "%s: failed - scl held low prior to reset\n",__func__);	
		return false;	/* SCL held low; can't drive it */
	}

	for (i = 0; i < I2C_RESET_CLK_TOGGLE_NUM; i++) {
		gpio_direction_output(MX6_SODA_I2C_SCL, 0);
		udelay((I2C_BB_DELAY_US * 2));
		gpio_direction_output(MX6_SODA_I2C_SCL, 1);
		udelay((I2C_BB_DELAY_US * 2));
	}

	if (!gpio_get_value(MX6_SODA_I2C_SDA)) {
		printk(KERN_ERR "%s: failed - sda stuck low after toggle\n",__func__);	
		return false;	/* After SCL toggle still SDA stuck low */
	}

	/* send stop */
	gpio_direction_output(MX6_SODA_I2C_SCL, 0);
	udelay(I2C_BB_DELAY_US);
	gpio_direction_output(MX6_SODA_I2C_SDA, 0);
	udelay(I2C_BB_DELAY_US);
	gpio_direction_output(MX6_SODA_I2C_SCL, 1);
	udelay(I2C_BB_DELAY_US);
	gpio_direction_output(MX6_SODA_I2C_SDA, 1);
	udelay(I2C_BB_DELAY_US);

	/* tri-state after reset */
	gpio_direction_input(MX6_SODA_I2C_SCL);	
	gpio_direction_input(MX6_SODA_I2C_SDA);

	if (gpio_get_value(MX6_SODA_I2C_SCL) && gpio_get_value(MX6_SODA_I2C_SDA)) {
		return true;
	} else {
		printk(KERN_ERR "%s: failed - scl/sda stuck low after reset\n",__func__);	
		return false;	/* After SCL toggle still SDA stuck low */
	}
}
EXPORT_SYMBOL(soda_i2c_reset);

#endif

/* WiFi Power Enable/Disable */
#if defined(CONFIG_MX6SL_WARIO_BASE)
void gpio_wifi_power_enable(int enable)
{
	gpio_direction_output(MX6_WARIO_WIFI_PWD, 0);
	gpio_set_value(MX6_WARIO_WIFI_PWD, enable);
}
EXPORT_SYMBOL(gpio_wifi_power_enable);
#endif

void gpio_wifi_power_reset(void)
{
#if defined(CONFIG_MX6SL_WARIO_BASE)
	gpio_wifi_power_enable(0);
	/* 20ms  delay is provided by Qc */
	mdelay(20);
	gpio_wifi_power_enable(1);
#elif defined(CONFIG_MX6SL_WARIO_WOODY)
	gpio_set_value(MX6SL_WARIO_WL_REG_ON, 0);
	mdelay(100);
	gpio_set_value(MX6SL_WARIO_WL_REG_ON, 1);
	/* BRCM data sheet "4343W-DS105-R" (1/12/2015)
	 * Section 22: Power-up Sequence and Timing:
	 * Wait at least 150 ms after VDDC and VDDIO are available
	 * before initiating SDIO accesses.
	 */
	mdelay(150);
#endif
}

/* Broadcom 4343W Wifi+BT GPIO functions */
int brcm_gpio_wifi_bt_init(void)
{
	int ret = 0;

	printk(KERN_ERR "\nRequest BRCM Wifi+BT GPIOs..\n");

        ret = gpio_request(MX6SL_WARIO_WL_REG_ON, "WL_REG_ON");
        if(unlikely(ret)) return ret;

        ret = gpio_request(MX6SL_WARIO_BT_REG_ON, "BT_REG_ON");
        if(unlikely(ret)) return ret;

//        ret = gpio_request(MX6SL_WARIO_BT_CLK_REQ, "wifi_set_normal_mode");
//        if(unlikely(ret)) return ret;
        
	ret = gpio_request(MX6SL_WARIO_BT_DEV_WAKE, "bt_dev_wake");
        if(unlikely(ret)) return ret;

        ret = gpio_request(MX6SL_WARIO_BT_HOST_WAKE, "bt_host_wake");
        if(unlikely(ret)) return ret;

	printk(KERN_ERR "\nBRCM Wifi+BT set directions..\n");

	//Enable pins
	gpio_direction_output(MX6SL_WARIO_WL_REG_ON, 0);
	gpio_direction_output(MX6SL_WARIO_BT_REG_ON, 0);

	//set host wake as input and dev wake as output
	gpio_direction_input(MX6SL_WARIO_BT_HOST_WAKE);
	gpio_direction_output(MX6SL_WARIO_BT_DEV_WAKE, 0);
	
	//clk request
//	gpio_direction_input(MX6SL_WARIO_BT_CLK_REQ);
	
	return ret;
}
EXPORT_SYMBOL(brcm_gpio_wifi_bt_init);

void brcm_gpio_wifi_power_enable(int enable)
{
	gpio_set_value(MX6SL_WARIO_WL_REG_ON, enable);
}
EXPORT_SYMBOL(brcm_gpio_wifi_power_enable);

void brcm_gpio_bt_power_enable(int enable)
{
	gpio_set_value(MX6SL_WARIO_BT_REG_ON, enable);
}
EXPORT_SYMBOL(brcm_gpio_bt_power_enable);

void brcm_gpio_wifi_set_normal_mode(int enable)
{ 
/* this is input for BT but don't do it yet, there seems to be some issues
 * still - TODO */
/*	gpio_direction_output(MX6SL_WARIO_BT_CLK_REQ, 0);
/	gpio_set_value(MX6SL_WARIO_BT_CLK_REQ, enable); */
}
EXPORT_SYMBOL(brcm_gpio_wifi_set_normal_mode);

/**************touch**************************/
int gpio_cyttsp_init_pins(void)
{
	int ret = 0;
	//touch pins
	ret = gpio_request(MX6SL_PIN_TOUCH_INTB, "touch_intb");
	if(unlikely(ret)) return ret;

	ret = gpio_request(MX6SL_PIN_TOUCH_RST, "touch_rst");
	if(unlikely(ret)) goto free_intb;

	gpio_direction_input(MX6SL_PIN_TOUCH_INTB);
	gpio_direction_output(MX6SL_PIN_TOUCH_RST, 1);

	return ret;
free_intb:
	gpio_free(MX6SL_PIN_TOUCH_INTB);
	return ret;

}
EXPORT_SYMBOL(gpio_cyttsp_init_pins);

/* zforce2 GPIOs setup */
int gpio_zforce_init_pins(void)
{
	int ret = 0;

	ret = gpio_request(MX6SL_PIN_TOUCH_INTB, "touch_intb");
	if(unlikely(ret)) return ret;

	ret = gpio_request(MX6SL_PIN_TOUCH_RST, "touch_rst");
	if(unlikely(ret)) goto free_intb;

	/* touch BSL programming pins */
	ret = gpio_request(MX6SL_PIN_TOUCH_SWDL, "touch_swdl");
	if(unlikely(ret)) goto free_rst;

	ret = gpio_request(MX6SL_PIN_TOUCH_UART_TX, "touch_uarttx");
	if(unlikely(ret)) goto free_swdl;

	ret = gpio_request(MX6SL_PIN_TOUCH_UART_RX, "touch_uartrx");
	if(unlikely(ret)) goto free_uarttx;

	/* GPIO Interrupt - is set as input once and for all */
	gpio_direction_input(MX6SL_PIN_TOUCH_INTB);

	/* trigger reset - active low */
	gpio_direction_output(MX6SL_PIN_TOUCH_RST, 0);

	return ret;

free_uarttx:
	gpio_free(MX6SL_PIN_TOUCH_UART_TX);
free_swdl:
	gpio_free(MX6SL_PIN_TOUCH_SWDL);
free_rst:
	gpio_free(MX6SL_PIN_TOUCH_RST);
free_intb:
	gpio_free(MX6SL_PIN_TOUCH_INTB);
	return ret;
}
EXPORT_SYMBOL(gpio_zforce_init_pins);

int gpio_zforce_reset_ena(int enable)
{
	if(enable)
		gpio_direction_output(MX6SL_PIN_TOUCH_RST, 0);
	else
		gpio_direction_input(MX6SL_PIN_TOUCH_RST);
	return 0;
}
EXPORT_SYMBOL(gpio_zforce_reset_ena);

void gpio_zforce_set_reset(int val)
{
	gpio_set_value(MX6SL_PIN_TOUCH_RST, val);
}
EXPORT_SYMBOL(gpio_zforce_set_reset);

void gpio_zforce_set_bsl_test(int val)
{
	gpio_set_value(MX6SL_PIN_TOUCH_SWDL, val);
}
EXPORT_SYMBOL(gpio_zforce_set_bsl_test);

void gpio_zforce_bslpins_ena(int enable)
{
	if(enable) {
		gpio_direction_output(MX6SL_PIN_TOUCH_SWDL, 0);
		gpio_direction_input(MX6SL_PIN_TOUCH_UART_RX);
		gpio_direction_output(MX6SL_PIN_TOUCH_UART_TX, 0);
	} else {
		gpio_direction_input(MX6SL_PIN_TOUCH_SWDL);
		gpio_direction_input(MX6SL_PIN_TOUCH_UART_TX);
		gpio_direction_input(MX6SL_PIN_TOUCH_UART_RX);
	}
}
EXPORT_SYMBOL(gpio_zforce_bslpins_ena);

void gpio_zforce_free_pins(void)
{
	gpio_free(MX6SL_PIN_TOUCH_INTB);
	gpio_free(MX6SL_PIN_TOUCH_RST);
	gpio_free(MX6SL_PIN_TOUCH_SWDL);
	gpio_free(MX6SL_PIN_TOUCH_UART_TX);
	gpio_free(MX6SL_PIN_TOUCH_UART_RX);
}
EXPORT_SYMBOL(gpio_zforce_free_pins);

/** Touch Controller IRQ setup for cyttsp and zforce2 **/
void gpio_touchcntrl_request_irq(int enable)
{
	if (enable)
		gpio_direction_input(MX6SL_PIN_TOUCH_INTB);
}
EXPORT_SYMBOL(gpio_touchcntrl_request_irq);

int gpio_touchcntrl_irq(void)
{
	return gpio_to_irq(MX6SL_PIN_TOUCH_INTB);
}
EXPORT_SYMBOL(gpio_touchcntrl_irq);

int gpio_touchcntrl_irq_get_value(void)
{
	return gpio_get_value( MX6SL_PIN_TOUCH_INTB);
}
EXPORT_SYMBOL(gpio_touchcntrl_irq_get_value);
/****/

void gpio_cyttsp_wake_signal(void)
{
	mdelay(100);
	gpio_direction_output(MX6SL_PIN_TOUCH_INTB, 1);
	mdelay(1);
	gpio_set_value(MX6SL_PIN_TOUCH_INTB, 0);
	mdelay(1);
	gpio_set_value(MX6SL_PIN_TOUCH_INTB, 1);
	mdelay(1);
	gpio_set_value(MX6SL_PIN_TOUCH_INTB, 0);
	mdelay(1);
	gpio_set_value(MX6SL_PIN_TOUCH_INTB, 1);
	mdelay(100);
	gpio_direction_input(MX6SL_PIN_TOUCH_INTB);
}
EXPORT_SYMBOL(gpio_cyttsp_wake_signal);

/* power up cypress touch */
int gpio_cyttsp_hw_reset(void)
{

	/* greater than celeste 1.2 and icewine will have new gpio layout */
	gpio_direction_output(MX6SL_PIN_TOUCH_RST, 1);
	gpio_set_value(MX6SL_PIN_TOUCH_RST, 1);
	msleep(20);
	gpio_set_value(MX6SL_PIN_TOUCH_RST, 0);
	msleep(40);
	gpio_set_value(MX6SL_PIN_TOUCH_RST, 1);
	msleep(20);
	gpio_direction_input(MX6SL_PIN_TOUCH_RST);
	printk(KERN_DEBUG "cypress reset");

	return 0;
}
EXPORT_SYMBOL(gpio_cyttsp_hw_reset);


int gpio_setup_wdog(void)
{
	int ret;
	ret = gpio_request(MX6_WARIO_WDOG_B, "wario_wdog");
	if(unlikely(ret)) return ret;

	gpio_set_value(MX6_WARIO_WDOG_B, 1);
	gpio_direction_output(MX6_WARIO_WDOG_B, 1);
	return 0;
}
EXPORT_SYMBOL(gpio_setup_wdog);


// Function to config iomux for instance epdc.
void epdc_iomux_config_lve(void)
{
	// Config epdc.GDCLK to pad EPDC_GDCLK(A12)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_GDCLK(0x020E00D0)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_GDCLK));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_GDCLK(0x020E03C0)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_GDCLK));

	// Config epdc.GDOE to pad EPDC_GDOE(B13)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_GDOE(0x020E00D4)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_GDOE));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_GDOE(0x020E03C4)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_GDOE));

	// Config epdc.GDSP to pad EPDC_GDSP(A11)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_GDSP(0x020E00DC)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_GDSP));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_GDSP(0x020E03CC)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_GDSP));

	// Config epdc.PWRCOM to pad EPDC_PWRCOM(B11)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_PWRCOM(0x020E00E0)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_PWRCOM));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_PWRCOM(0x020E03D0)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_PWRCOM));

	// Config epdc.PWRSTAT to pad EPDC_PWRSTAT(E10)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_PWRSTAT(0x020E00F8)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT5 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_PWRSTAT));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_PWRSTAT(0x020E03E8)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PU & 0x3) << 14 |
			(PUE_PULL & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_ENABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_PWRSTAT));

	// Config epdc.SDCE[0] to pad EPDC_SDCE0(C11)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCE0(0x020E0100)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCE0));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE0(0x020E03F0)
	if (lab126_board_is(BOARD_ID_WHISKY_WFO) ||
		lab126_board_is(BOARD_ID_WHISKY_WAN) ||
		lab126_board_is(BOARD_ID_WOODY)) {
		__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_40OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE0));
	} else {
		__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_SLOW & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCE0));
	}

	// Config epdc.SDCLK to pad EPDC_SDCLK(B10)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCLK(0x020E0110)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_SDCLK));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCLK(0x020E0400)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_48OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDCLK));

	// Config epdc.SDDO[0] to pad EPDC_D0(A18)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_D0(0x020E0090)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D0));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_D0(0x020E0380)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D0));

	// Config epdc.SDDO[1] to pad EPDC_D1(A17)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_D1(0x020E0094)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D1));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_D1(0x020E0384)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D1));

	// Config epdc.SDDO[2] to pad EPDC_D2(B17)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_D2(0x020E00B0)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D2));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_D2(0x020E03A0)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D2));

	// Config epdc.SDDO[3] to pad EPDC_D3(A16)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_D3(0x020E00B4)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D3));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_D3(0x020E03A4)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D3));

	// Config epdc.SDDO[4] to pad EPDC_D4(B16)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_D4(0x020E00B8)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D4));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_D4(0x020E03A8)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D4));

	// Config epdc.SDDO[5] to pad EPDC_D5(A15)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_D5(0x020E00BC)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D5));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_D5(0x020E03AC)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D5));

	// Config epdc.SDDO[6] to pad EPDC_D6(B15)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_D6(0x020E00C0)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D6));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_D6(0x020E03B0)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D6));

	// Config epdc.SDDO[7] to pad EPDC_D7(C15)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_D7(0x020E00C4)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_D7));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_D7(0x020E03B4)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_D7));

	// Config epdc.SDLE to pad EPDC_SDLE(B8)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_SDLE(0x020E0114)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_SDLE));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_SDLE(0x020E0404)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_60OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDLE));

	// Config epdc.SDOE to pad EPDC_SDOE(E7)
	// Mux Register:
	// IOMUXC_SW_MUX_CTL_PAD_EPDC_SDOE(0x020E0118)
	__raw_writel((SION_DISABLED & 0x1) << 4 | (ALT0 & 0x7), (IOMUXC_SW_MUX_CTL_PAD_EPDC_SDOE));
	// Pad Control Register:
	// IOMUXC_SW_PAD_CTL_PAD_EPDC_SDOE(0x020E0408)
	__raw_writel((LVE_ENABLED & 0x1) << 22 | (HYS_ENABLED & 0x1) << 16 | (PUS_100KOHM_PD & 0x3) << 14 |
			(PUE_KEEP & 0x1) << 13 | (PKE_ENABLED & 0x1) << 12 | (ODE_DISABLED & 0x1) << 11 |
			(SPD_100MHZ & 0x3) << 6 | (DSE_48OHM & 0x7) << 3 | (SRE_FAST & 0x1), (IOMUXC_SW_PAD_CTL_PAD_EPDC_SDOE));
}
EXPORT_SYMBOL(epdc_iomux_config_lve);
/********************WAN GPIOs ********************/

#if defined(CONFIG_MX6SL_WARIO_WOODY)
int gpio_wan_mhi_irq(void)
{
	return gpio_to_irq(MX6SL_WARIO_3GM_WAKEUP_MDM);
}
EXPORT_SYMBOL(gpio_wan_mhi_irq);

int gpio_wan_usb_wake(void)
{
	return gpio_get_value(MX6SL_WARIO_3GM_WAKEUP_MDM);
}
EXPORT_SYMBOL(gpio_wan_usb_wake);

void gpio_wan_dl_enable(int enable)
{
        gpio_direction_output(MX6SL_WARIO_3GM_DL_KEY, 0);
        gpio_set_value(MX6SL_WARIO_3GM_DL_KEY, enable);
}
EXPORT_SYMBOL(gpio_wan_dl_enable);

void gpio_wan_sar_detect(int detect)
{
        gpio_direction_output(MX6SL_WARIO_3GM_SAR_DET, 0);
        gpio_set_value(MX6SL_WARIO_3GM_SAR_DET, detect);
}
EXPORT_SYMBOL(gpio_wan_sar_detect);

int whistler_wan_request_gpio(void)
{
	int ret = 0;

	ret = gpio_request(MX6SL_WARIO_3GM_FW_READY, "FW_Ready");
	if (ret) return ret;
	ret = gpio_request(MX6SL_WARIO_3GM_POWER_ON, "Power");
	if (ret) {
		gpio_free(MX6SL_WARIO_3GM_FW_READY);
		return ret;
	}
	gpio_direction_output(MX6SL_WARIO_3GM_POWER_ON, 0);
	ret = gpio_request(MX6SL_WARIO_3GM_DL_KEY, "DLen");
	if (ret) {
		gpio_free(MX6SL_WARIO_3GM_POWER_ON);
		gpio_free(MX6SL_WARIO_3GM_FW_READY);
		return ret;
	}
	ret = gpio_request(MX6SL_WARIO_3GM_SAR_DET, "Sar");
	if (ret) {
		gpio_free(MX6SL_WARIO_3GM_DL_KEY);
		gpio_free(MX6SL_WARIO_3GM_POWER_ON);
		gpio_free(MX6SL_WARIO_3GM_FW_READY);
		return ret;
	}
	ret = gpio_request(MX6SL_WARIO_3GM_WAKEUP_MDM, "USB_Wake");
	if (ret) {
		printk("Failed to request GPIO(4, 26)! error: %d. Ignore!\n", ret);
		ret = 0;
	}
	gpio_direction_input(MX6SL_WARIO_3GM_WAKEUP_MDM);

	return ret;
}
EXPORT_SYMBOL(whistler_wan_request_gpio);

/* Init hall oneshot circuit: 
 * Disable oneshot during boot & normal operation (drive HI) 
 */
void gpio_hall_oneshot_init(void)
{
	if (lab126_board_rev_greater(BOARD_ID_WHISKY_WAN_HVT1) || lab126_board_rev_greater(BOARD_ID_WHISKY_WFO_HVT1) ||
		lab126_board_rev_greater_eq(BOARD_ID_WOODY_2)) {
		gpio_request(MX6SL_ARM2_EPDC_SDCE1, "hall_qboot_enable");
		gpio_direction_output(MX6SL_ARM2_EPDC_SDCE1, 1);
	}
}
EXPORT_SYMBOL(gpio_hall_oneshot_init);

void gpio_hall_oneshot_ctrl(int enable) 
{
	if (enable > 0) {
		gpio_direction_output(MX6SL_ARM2_EPDC_SDCE1, 0);
	} else {
		gpio_direction_output(MX6SL_ARM2_EPDC_SDCE1, 1);	/* default init state during boot */
	}
}
EXPORT_SYMBOL(gpio_hall_oneshot_ctrl);

#elif defined(CONFIG_MX6SL_WARIO_BASE)
void gpio_wan_ldo_fet_init(void)
{
	/* Note: gpio wan ldo/fet ctrl only needed for IW(WAN)EVT1.2 */
	if (lab126_board_is(BOARD_ID_ICEWINE_WARIO_EVT1_2)) {
		gpio_request(MX6SL_ARM2_EPDC_PWRCTRL3, "wan_ldo");
		gpio_direction_output(MX6SL_ARM2_EPDC_PWRCTRL3, 1);
		gpio_wan_ldo_en = 1;
	}
}
EXPORT_SYMBOL(gpio_wan_ldo_fet_init);

/* GPIO (LDO / FET) control to protect WAN module with 4.35V battery */
void gpio_wan_ldo_fet_ctrl(int enable)
{
	/* Note: gpio wan ldo/fet ctrl only needed for IW(WAN)EVT1.2 */
	if (lab126_board_is(BOARD_ID_ICEWINE_WARIO_EVT1_2)) {
		if (enable > 0) {
			if (!gpio_wan_ldo_en) {
				gpio_direction_output(MX6SL_ARM2_EPDC_PWRCTRL3, 1);		/* LDO */
				gpio_wan_ldo_en = 1;
			}
		} else {
			if (gpio_wan_ldo_en) {
				gpio_direction_output(MX6SL_ARM2_EPDC_PWRCTRL3, 0);		/* FET */
				gpio_wan_ldo_en = 0;
			}
		}
	}
}
EXPORT_SYMBOL(gpio_wan_ldo_fet_ctrl);

void gpio_wan_rf_enable(int enable)
{
        gpio_direction_output(MX6_WAN_ON_OFF, 0);
        gpio_set_value(MX6_WAN_ON_OFF, enable);
}
EXPORT_SYMBOL(gpio_wan_rf_enable);

void gpio_wan_usb_enable(int enable)
{
	gpio_direction_output(MX6_WAN_USB_EN, 0);
	gpio_set_value(MX6_WAN_USB_EN, enable);
}
EXPORT_SYMBOL(gpio_wan_usb_enable);

int gpio_wan_mhi_irq(void)
{
	return gpio_to_irq(MX6_WAN_MHI);
}
EXPORT_SYMBOL(gpio_wan_mhi_irq);

int gpio_wan_usb_wake(void)
{
	return gpio_get_value(MX6_WAN_MHI);
}
EXPORT_SYMBOL(gpio_wan_usb_wake);

void wan_request_gpio(void)
{
	gpio_request(MX6_WAN_FW_READY, "FW_Ready");
	gpio_request(MX6_WAN_SHUTDOWN, "Power");
	gpio_request(MX6_WAN_ON_OFF, "Reset");
	gpio_request(MX6_WAN_USB_EN, "USBen");
	return;
}
EXPORT_SYMBOL(wan_request_gpio);

#endif

void wan_free_gpio(void)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	gpio_free(MX6SL_WARIO_3GM_SAR_DET);
	gpio_free(MX6SL_WARIO_3GM_DL_KEY);
	gpio_free(MX6SL_WARIO_3GM_POWER_ON);
	gpio_free(MX6SL_WARIO_3GM_FW_READY);
#elif defined(CONFIG_MX6SL_WARIO_BASE)
	gpio_free(MX6_WAN_USB_EN);
	gpio_free(MX6_WAN_ON_OFF);
	gpio_free(MX6_WAN_SHUTDOWN);
	gpio_free(MX6_WAN_FW_READY);
#endif
}
EXPORT_SYMBOL(wan_free_gpio);

void gpio_wan_power(int enable)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	gpio_set_value(MX6SL_WARIO_3GM_POWER_ON, enable);
#elif defined(CONFIG_MX6SL_WARIO_BASE)
	gpio_direction_output(MX6_WAN_SHUTDOWN, 0);
	gpio_set_value(MX6_WAN_SHUTDOWN, enable);
#endif
}
EXPORT_SYMBOL(gpio_wan_power);

int gpio_wan_fw_ready_irq(void)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY)
        return gpio_to_irq(MX6SL_WARIO_3GM_FW_READY);
#elif defined(CONFIG_MX6SL_WARIO_BASE)
	return gpio_to_irq(MX6_WAN_FW_READY);
#endif
}
EXPORT_SYMBOL(gpio_wan_fw_ready_irq);

int gpio_wan_fw_ready(void)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY)
        return gpio_get_value(MX6SL_WARIO_3GM_FW_READY);
#elif defined(CONFIG_MX6SL_WARIO_BASE)
	return gpio_get_value(MX6_WAN_FW_READY);
#endif
}
EXPORT_SYMBOL(gpio_wan_fw_ready);

int gpio_wan_host_wake_irq(void)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	return gpio_to_irq(MX6SL_WARIO_3GM_WAKEUP_AP);
#elif defined(CONFIG_MX6SL_WARIO_BASE)
        return gpio_to_irq(MX6_WAN_HOST_WAKE);
#endif
}
EXPORT_SYMBOL(gpio_wan_host_wake_irq);

int gpio_wan_host_wake(void)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	return gpio_get_value(MX6SL_WARIO_3GM_WAKEUP_AP);
#elif defined(CONFIG_MX6SL_WARIO_BASE)
        return gpio_get_value(MX6_WAN_HOST_WAKE);
#endif
}
EXPORT_SYMBOL(gpio_wan_host_wake);

/*
//EPDC_SDCE2 - SCL
//EPDC_SDCE3 - SDA
MX6SL_PAD_EPDC_SDCE2__GPIO_1_29
MX6SL_PAD_EPDC_SDCE3__GPIO_1_30
#define MX6SL_ARM2_EPDC_SDCE2           IMX_GPIO_NR(1, 29)
#define MX6SL_ARM2_EPDC_SDCE3           IMX_GPIO_NR(1, 30)
 */
/* gpio_i2c3_scl_toggle:
 *
 */
#define I2C_RESET_CLK_CNT      10
void gpio_i2c3_scl_toggle(void)
{
	int i = 0;
	gpio_direction_output(MX6SL_ARM2_EPDC_SDCE2, 1);
	gpio_set_value(MX6SL_ARM2_EPDC_SDCE2, 1);

	for (i = 0; i < I2C_RESET_CLK_CNT; i++) {
		gpio_set_value(MX6SL_ARM2_EPDC_SDCE2, 0);
		udelay(20);
		gpio_set_value(MX6SL_ARM2_EPDC_SDCE2, 1);
		udelay(20);
	}
} 
EXPORT_SYMBOL(gpio_i2c3_scl_toggle);

int gpio_i2c3_fault(void)
{
	int ret = 0;
	ret = (gpio_get_value(MX6SL_ARM2_EPDC_SDCE2) && 
			!gpio_get_value(MX6SL_ARM2_EPDC_SDCE3));
	return ret;

}
EXPORT_SYMBOL(gpio_i2c3_fault);

void config_i2c3_gpio_input(int enable)
{
	if (enable) {
		gpio_request(MX6SL_ARM2_EPDC_SDCE2, "i2c3_scl");
		gpio_request(MX6SL_ARM2_EPDC_SDCE3, "i2c3_sda");
		mxc_iomux_v3_setup_pad(MX6SL_PAD_EPDC_SDCE2__GPIO_1_29);
		mxc_iomux_v3_setup_pad(MX6SL_PAD_EPDC_SDCE3__GPIO_1_30);
	} else {
		gpio_free(MX6SL_ARM2_EPDC_SDCE2);
		gpio_free(MX6SL_ARM2_EPDC_SDCE3);
		mxc_iomux_v3_setup_pad(MX6SL_PAD_EPDC_SDCE2__I2C3_SCL);
		mxc_iomux_v3_setup_pad(MX6SL_PAD_EPDC_SDCE3__I2C3_SDA);
	}
}
EXPORT_SYMBOL(config_i2c3_gpio_input);


void init_emmc_power_gate_pins(void)
{
	gpio_request(MX6SL_FEC_RXD0_EMMC_3v2, "EMMC_3v2_EN FEC_RXD0");
	gpio_request(MX6SL_FEC_MDC_EMMC_1v8, "EMMC_1v8_EN FEC_MDC");
	
	gpio_direction_output(MX6SL_FEC_RXD0_EMMC_3v2, 1); 
	gpio_direction_output(MX6SL_FEC_MDC_EMMC_1v8, 1); 
	
	gpio_request(MX6SL_SD2_RST, "sd2rst");
	gpio_request(MX6SL_SD2_CLK, "sd2clk");
	gpio_request(MX6SL_SD2_CMD, "sd2cmd");
	gpio_request(MX6SL_SD2_DAT0, "sd2dat0");
	gpio_request(MX6SL_SD2_DAT1, "sd2dat1");
	gpio_request(MX6SL_SD2_DAT2, "sd2dat2");
	gpio_request(MX6SL_SD2_DAT3, "sd2dat3");
	gpio_request(MX6SL_SD2_DAT4, "sd2dat4");
	gpio_request(MX6SL_SD2_DAT5, "sd2dat5");
	gpio_request(MX6SL_SD2_DAT6, "sd2dat6");
	gpio_request(MX6SL_SD2_DAT7, "sd2dat7");
}

EXPORT_SYMBOL(init_emmc_power_gate_pins);

static iomux_v3_cfg_t mx6sl_sd2_power_gated_pads[] = {
	MX6SL_PAD_SD2_RST__GPIO_4_27,
	MX6SL_PAD_SD2_CLK__GPIO_5_5,
	MX6SL_PAD_SD2_CMD__GPIO_5_4,
	MX6SL_PAD_SD2_DAT0__GPIO_5_1,
	MX6SL_PAD_SD2_DAT1__GPIO_4_30,
	MX6SL_PAD_SD2_DAT2__GPIO_5_3,
	MX6SL_PAD_SD2_DAT3__GPIO_4_28,
	MX6SL_PAD_SD2_DAT4__GPIO_5_2,
	MX6SL_PAD_SD2_DAT5__GPIO_4_31,
	MX6SL_PAD_SD2_DAT6__GPIO_4_29,
	MX6SL_PAD_SD2_DAT7__GPIO_5_0,
};

void emmc_power_gate_pins(int on)
{
	if(on) {
		/*
		 * To avoid haardware backfeeding, power sequence 3v2 then 1v8 with 200us delay, 
		 * and provide enough delay for 1v8 to stablize. 
		 * */
		
		gpio_set_value(MX6SL_FEC_RXD0_EMMC_3v2, 1);
		udelay(200);
		gpio_set_value(MX6SL_FEC_MDC_EMMC_1v8, 1);
		udelay(200);
		usdhc2_iomux_config();
	} else {
		
		mxc_iomux_v3_setup_multiple_pads(mx6sl_sd2_power_gated_pads, 
			ARRAY_SIZE(mx6sl_sd2_power_gated_pads));
		
		gpio_direction_output(MX6SL_SD2_RST, 0); 
		gpio_direction_output(MX6SL_SD2_CLK, 0); 
		gpio_direction_output(MX6SL_SD2_CMD, 0); 
		gpio_direction_output(MX6SL_SD2_DAT0, 0); 
		gpio_direction_output(MX6SL_SD2_DAT1, 0); 
		gpio_direction_output(MX6SL_SD2_DAT2, 0); 
		gpio_direction_output(MX6SL_SD2_DAT3, 0); 
		gpio_direction_output(MX6SL_SD2_DAT4, 0); 
		gpio_direction_output(MX6SL_SD2_DAT5, 0); 
		gpio_direction_output(MX6SL_SD2_DAT6, 0); 
		gpio_direction_output(MX6SL_SD2_DAT7, 0); 
		
		gpio_set_value(MX6SL_FEC_MDC_EMMC_1v8, 0);
		mdelay(2); //takes longer to drain
		gpio_set_value(MX6SL_FEC_RXD0_EMMC_3v2, 0);
	}
}
EXPORT_SYMBOL(emmc_power_gate_pins);

#ifdef CONFIG_HAS_MMC_FORCE_OFF
void emmc_force_power_off()
{
	gpio_set_value(MX6SL_FEC_MDC_EMMC_1v8, 0);
	gpio_set_value(MX6SL_FEC_RXD0_EMMC_3v2, 0);
}
EXPORT_SYMBOL(emmc_force_power_off);
#endif
