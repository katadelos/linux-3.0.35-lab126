/*
 * Copyright (c) 2012-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along
 * with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef __BOARD_MX6SL_WARIO_H__
#define __BOARD_MX6SL_WARIO_H__

#include <mach/mx6.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/mxc_dvfs.h>
#include <mach/memory.h>
#include <mach/iomux-mx6sl.h>
#include <mach/imx-uart.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "usb.h"
#include "devices-imx6q.h"
#include "crm_regs.h"
#include "cpu_op-mx6.h"

/* Wario GPIO pins */
#define MX6_ARM2_CAP_TCH_INT    IMX_GPIO_NR(2, 10)      /* EPDC_PWRCTRL3 */
#define MX6_WARIO_WDOG_B        IMX_GPIO_NR(3, 18)      /* WDOG_B */
#define MX6_ARM2_SD3_CD         IMX_GPIO_NR(3, 22)      /* REF_CLK_32K */
#define MX6_ARM2_PWM_BL_CTRL    IMX_GPIO_NR(3, 23)      /* PWM1 */
#define MX6_WARIO_HALL_SNS      IMX_GPIO_NR(4, 7)       /* KEY_ROW7 */
#define MX6SL_PIN_TOUCH_INTB    IMX_GPIO_NR(4, 3)       /* KEY_ROW5 - touch irq*/



#if defined(CONFIG_MX6SL_WARIO_BASE)

#define MX6_WARIO_WIFI_PWD      IMX_GPIO_NR(3, 29)      /* KEY_ROW2 */

#elif defined(CONFIG_MX6SL_WARIO_WOODY)

/* Wario pins related to BRCM 4343W Wifi+BT module */
#define MX6SL_WARIO_WL_REG_ON		IMX_GPIO_NR(2, 15)	/* LCD_CLK - BT_RESET_B */
#define MX6SL_WARIO_WL_GPIO_1           IMX_GPIO_NR(5,  8)      /* SD1_DAT1 - WIFI_GPIO_1 */
#define MX6SL_WARIO_BT_REG_ON		IMX_GPIO_NR(2, 19)	/* LCD_RESET - BT_DISABLE */
#define MX6SL_WARIO_BT_CLK_REQ          IMX_GPIO_NR(5,  6)	/* SD1_DAT3 */
#define MX6SL_WARIO_BT_DEV_WAKE         IMX_GPIO_NR(5,  13)	/* SD1_DAT2 */
#define MX6SL_WARIO_WIFI_WAKE_ON_LAN_B  IMX_GPIO_NR(3, 31)	/* KEY_ROW3 */
#define MX6SL_WARIO_BT_HOST_WAKE        IMX_GPIO_NR(3, 29)	/* KEY_ROW2 - bt_host_wake */

/* pins related to SODA interface */
#define MX6_SODA_I2C_SCL        IMX_GPIO_NR(2, 7)			/* EPDC_PWRCTRL0 */
#define MX6_SODA_I2C_SDA        IMX_GPIO_NR(2, 8)			/* EPDC_PWRCTRL1 */
#define MX6_SODA_OTG_SW         IMX_GPIO_NR(2, 9)                       /* EPDC_PWRCTRL2 */
#define MX6_SODA_I2C_SDA_PU     IMX_GPIO_NR(2, 14)			/* EPDC_PWRWAKEUP */
#define MX6_SODA_BOOST          IMX_GPIO_NR(2, 12)                      /* EPDC_PWRINT */
#define MX6_SODA_CHG_DET        IMX_GPIO_NR(5, 15)		        /* SD1_CLK */
#define MX6_SODA_VBUS_ENABLE    IMX_GPIO_NR(4, 16)			/* FEC_TXD1 */

#define I2C_RESET_CLK_TOGGLE_NUM         11
#define I2C_BB_DELAY_US                  20 /* 50 KHz */

#endif

#define MX6_WARIO_ACCINT1       IMX_GPIO_NR(4, 4)       /* KEY_COL6 */
#define MX6_WARIO_ACCINT2       IMX_GPIO_NR(4, 6)       /* KEY_COL7 */
#define MX6_ARM2_ECSPI1_CS0     IMX_GPIO_NR(4, 11)      /* ECSPI1_SS0 */
#define MX6_WARIO_PROX_INT      IMX_GPIO_NR(4, 12)      /* ECSPI2_SCLK */
#define MX6_WARIO_PROX_RST      IMX_GPIO_NR(4, 13)      /* ECSPI2_MOSI */
#define MX6_WARIO_ALS_INT       IMX_GPIO_NR(4, 14)      /* ECSPI2_MISO */
#define MX6_ARM2_FEC_MDIO       IMX_GPIO_NR(4, 20)  	/* ****** PMIC IRQ */
#define MX6_ARM2_FEC_PWR_EN     IMX_GPIO_NR(4, 21)      /* FEC_TX_CLK */
#define MX6_ARM2_SD2_WP         IMX_GPIO_NR(4, 29)      /* SD2_DAT6 */
#define MX6_ARM2_SD2_CD         IMX_GPIO_NR(5, 0)       /* SD2_DAT7 */
#define MX6_ARM2_SD1_CD         IMX_GPIO_NR(5, 7)       /* SD1_DAT5 */
#define MX6_ARM2_SD1_WP         IMX_GPIO_NR(5, 10)      /* SD1_DAT7 */
#define MX6SL_PIN_DEBUG_TOGGLE  IMX_GPIO_NR(4, 25)      /* debug toggle */
#define MX6SL_PIN_KEY_COL1      IMX_GPIO_NR(3, 26)      /* FSR spare gpio*/ 

/* load switch pins for emmc power gate*/
#define MX6SL_FEC_RXD0_EMMC_3v2    IMX_GPIO_NR(4, 17)
#define MX6SL_FEC_MDC_EMMC_1v8     IMX_GPIO_NR(4, 23)
#define MX6SL_SD2_RST              IMX_GPIO_NR(4, 27)
#define MX6SL_SD2_CLK              IMX_GPIO_NR(5, 5)
#define MX6SL_SD2_CMD              IMX_GPIO_NR(5,4)
#define MX6SL_SD2_DAT0             IMX_GPIO_NR(5,1)
#define MX6SL_SD2_DAT1             IMX_GPIO_NR(4,30)
#define MX6SL_SD2_DAT2             IMX_GPIO_NR(5,3)
#define MX6SL_SD2_DAT3             IMX_GPIO_NR(4,28)
#define MX6SL_SD2_DAT4             IMX_GPIO_NR(5,2)
#define MX6SL_SD2_DAT5             IMX_GPIO_NR(4,31)
#define MX6SL_SD2_DAT6             IMX_GPIO_NR(4,29)
#define MX6SL_SD2_DAT7             IMX_GPIO_NR(5,0)

/* Touch pins */
#define MX6SL_PIN_TOUCH_RST     IMX_GPIO_NR(4, 5)        /* KEY_ROW6 - touch reset */
#define MX6SL_PIN_TOUCH_SWDL    IMX_GPIO_NR(3, 24)       /* KEY_COL0 - SWDIO touch host side 
							  * program pin, used on Zforce2 */
#define MX6SL_PIN_TOUCH_UART_TX IMX_GPIO_NR(5, 9)        /* SD1_DAT5 - BSL_RX on Zforce2 */
#define MX6SL_PIN_TOUCH_UART_RX IMX_GPIO_NR(5, 12)       /* SD1_DAT4 - BSL_TX on ZForce2 */

/* Wario pins related to BRCM 4343W Wifi+BT module */
#define MX6SL_WARIO_WL_REG_ON		IMX_GPIO_NR(2, 15)	/* LCD_CLK - BT_RESET_B */
#define MX6SL_WARIO_BT_REG_ON		IMX_GPIO_NR(2, 19)	/* LCD_RESET - BT_DISABLE */
#define MX6SL_WARIO_BT_CLK_REQ   	IMX_GPIO_NR(5,  6)	/* SD1_DAT3 */
#define MX6SL_WARIO_BT_DEV_WAKE   	IMX_GPIO_NR(5,  13)	/* SD1_DAT2 */
#define MX6SL_WARIO_WIFI_WAKE_ON_LAN_B 	IMX_GPIO_NR(3, 31)	/* KEY_ROW3 */
#define MX6SL_WARIO_BT_HOST_WAKE      	IMX_GPIO_NR(3, 29)	/* KEY_ROW2 - bt_host_wake */
/* GPIO PINs related to modem */

#if defined(CONFIG_MX6SL_WARIO_WOODY)
#define MX6SL_WARIO_3GM_POWER_ON        IMX_GPIO_NR(2, 10)    /* EPDC_PWRCTRL3 - 3GM_POWER_ON */
#define MX6SL_WARIO_3GM_FW_READY        IMX_GPIO_NR(3, 30)    /* KEY_COL3 */
#define MX6SL_WARIO_3GM_DL_KEY          IMX_GPIO_NR(4, 2)     /* KEY_COL5 */
#define MX6SL_WARIO_3GM_WAKEUP_MDM      IMX_GPIO_NR(4, 26)    /* FEC_REF_CLK */
#define MX6SL_WARIO_3GM_WAKEUP_AP       IMX_GPIO_NR(4, 1)     /* KEY_ROW4 */
#define MX6SL_WARIO_3GM_SAR_DET         IMX_GPIO_NR(3, 28)    /* KEY_COL2 */
#elif defined(CONFIG_MX6SL_WARIO_BASE)
#define MX6_WAN_GPIO1                   IMX_GPIO_NR(3, 28)    /* KEY_COL2 */
#define MX6_WAN_FW_READY                IMX_GPIO_NR(3, 30)    /* KEY_COL3 */
#define MX6_WAN_SHUTDOWN                IMX_GPIO_NR(4, 0)     /* KEY_COL4 */
#define MX6_WAN_ON_OFF                  IMX_GPIO_NR(4, 2)     /* KEY_COL5 */
#define MX6_WAN_HMI                     IMX_GPIO_NR(4, 16)    /* FEC_TXD1 */
#define MX6_WAN_USB_EN                  IMX_GPIO_NR(4, 22)    /* FEC_TX_EN */
#define MX6_WAN_MHI                     IMX_GPIO_NR(4, 26)    /* FEC_REF_CLK */
#define MX6_WAN_HOST_WAKE               IMX_GPIO_NR(4, 1)     /* KEY_ROW4 */
#endif

#define MX6_WARIO_TOUCH_3V2     IMX_GPIO_NR(5, 7)       /*SD1_DAT6*/
#define MX6_WARIO_TOUCH_1V8     IMX_GPIO_NR(5, 10)      /*SD1_DAT7*/

/* EPDC GPIO pins */
#define MX6SL_ARM2_EPDC_SDDO_0          IMX_GPIO_NR(1, 7)
#define MX6SL_ARM2_EPDC_SDDO_1          IMX_GPIO_NR(1, 8)
#define MX6SL_ARM2_EPDC_SDDO_2          IMX_GPIO_NR(1, 9)
#define MX6SL_ARM2_EPDC_SDDO_3          IMX_GPIO_NR(1, 10)
#define MX6SL_ARM2_EPDC_SDDO_4          IMX_GPIO_NR(1, 11)
#define MX6SL_ARM2_EPDC_SDDO_5          IMX_GPIO_NR(1, 12)
#define MX6SL_ARM2_EPDC_SDDO_6          IMX_GPIO_NR(1, 13)
#define MX6SL_ARM2_EPDC_SDDO_7          IMX_GPIO_NR(1, 14)
#define MX6SL_ARM2_EPDC_SDDO_8          IMX_GPIO_NR(1, 15)
#define MX6SL_ARM2_EPDC_SDDO_9          IMX_GPIO_NR(1, 16)
#define MX6SL_ARM2_EPDC_SDDO_10         IMX_GPIO_NR(1, 17)
#define MX6SL_ARM2_EPDC_SDDO_11         IMX_GPIO_NR(1, 18)
#define MX6SL_ARM2_EPDC_SDDO_12         IMX_GPIO_NR(1, 19)
#define MX6SL_ARM2_EPDC_SDDO_13         IMX_GPIO_NR(1, 20)
#define MX6SL_ARM2_EPDC_SDDO_14         IMX_GPIO_NR(1, 21)
#define MX6SL_ARM2_EPDC_SDDO_15         IMX_GPIO_NR(1, 22)
#define MX6SL_ARM2_EPDC_GDCLK           IMX_GPIO_NR(1, 31)
#define MX6SL_ARM2_EPDC_GDSP            IMX_GPIO_NR(2, 2)
#define MX6SL_ARM2_EPDC_GDOE            IMX_GPIO_NR(2, 0)
#define MX6SL_ARM2_EPDC_GDRL            IMX_GPIO_NR(2, 1)
#define MX6SL_ARM2_EPDC_SDCLK           IMX_GPIO_NR(1, 23)
#define MX6SL_ARM2_EPDC_SDOE            IMX_GPIO_NR(1, 25)
#define MX6SL_ARM2_EPDC_SDLE            IMX_GPIO_NR(1, 24)
#define MX6SL_ARM2_EPDC_SDSHR           IMX_GPIO_NR(1, 26)
#define MX6SL_ARM2_EPDC_PWRCOM          IMX_GPIO_NR(2, 11)
#define MX6SL_ARM2_EPDC_PWRSTAT         IMX_GPIO_NR(2, 13)
#define MX6SL_ARM2_EPDC_PWRCTRL0        IMX_GPIO_NR(2, 7)
#define MX6SL_ARM2_EPDC_PWRCTRL1        IMX_GPIO_NR(2, 8)
#define MX6SL_ARM2_EPDC_PWRCTRL2        IMX_GPIO_NR(2, 9)
#define MX6SL_ARM2_EPDC_PWRCTRL3        IMX_GPIO_NR(2, 10)
#define MX6SL_ARM2_EPDC_BDR0            IMX_GPIO_NR(2, 5)
#define MX6SL_ARM2_EPDC_BDR1            IMX_GPIO_NR(2, 6)
#define MX6SL_ARM2_EPDC_SDCE0           IMX_GPIO_NR(1, 27)
#define MX6SL_ARM2_EPDC_SDCE1           IMX_GPIO_NR(1, 28)
#define MX6SL_ARM2_EPDC_SDCE2           IMX_GPIO_NR(1, 29)
#define MX6SL_ARM2_EPDC_SDCE3           IMX_GPIO_NR(1, 30)
#define MX6SL_ARM2_EPDC_PMIC_WAKE       IMX_GPIO_NR(2, 14) /* EPDC_PWRWAKEUP */
#define MX6SL_ARM2_EPDC_PMIC_INT        IMX_GPIO_NR(2, 12) /* EPDC_PWRINT */
#define MX6SL_ARM2_EPDC_VCOM            IMX_GPIO_NR(2, 3)


/*
 * hall gpio pullup used only for VNI setup 
 */
#define  HALL_SNS_PU_PAD_CTRL \
          (PAD_CTL_HYS | PAD_CTL_PUS_100K_UP | \
	   PAD_CTL_PUE | PAD_CTL_SPEED_MED | PAD_CTL_DSE_40ohm | \
	   PAD_CTL_SRE_SLOW)

#define  HALL_SNS_PD_PAD_CTRL \
        (PAD_CTL_HYS | PAD_CTL_PUS_100K_DOWN | \
	 PAD_CTL_PUE | PAD_CTL_SPEED_MED | PAD_CTL_DSE_40ohm | \
	 PAD_CTL_SRE_SLOW)

#define MX6SL_HALL_SNS(x)	IOMUX_PAD(0x04B0, 0x01A8, 0x5, 0x0000, 0, HALL_SNS_##x##_PAD_CTRL)

#if defined(CONFIG_INPUT_SX9500) || defined(CONFIG_INPUT_SX9500_MODULE)
/* IO Used for NIRQ */
#define GPIO_SX9500_NIRQ MX6_WARIO_PROX_INT
#endif

#endif /* __BOARD_MX6SL_WARIO_H__ */
