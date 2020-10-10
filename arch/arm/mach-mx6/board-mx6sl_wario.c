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

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/nodemask.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/i2c.h>
#include <linux/mxcfb.h>
#include <linux/pwm_backlight.h>
#include <linux/memblock.h>
#include <linux/gpio.h>
#include <linux/lab126_hall.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/max77696.h>
#include <linux/mfd/max77696-events.h>
#include <linux/power/soda.h>
#include <linux/mmc/sdhci.h>
#include <linux/frontlight.h>
#include <net/bluetooth/bt_pwr_ctrl.h>
#include <mach/boardid.h>
#include "board-mx6sl_wario.h"
#include <mach/system.h>

extern void emmc_power_gate_pins(int on);
extern void init_emmc_power_gate_pins(void);

#if defined(CONFIG_INPUT_SX9500) || defined(CONFIG_INPUT_SX9500_MODULE)
#include <linux/input/smtc/misc/sx9500-specifics.h>
#endif

#define HW_SUPPORT_EMMC_POWER_GATE  (lab126_board_is(BOARD_ID_MUSCAT_WFO) || \
                                      lab126_board_is(BOARD_ID_MUSCAT_32G_WFO) || \
                                      lab126_board_is(BOARD_ID_MUSCAT_WAN) || \
                                      lab126_board_is(BOARD_ID_WHISKY_WAN) || \
                                      lab126_board_is(BOARD_ID_WHISKY_WFO))


struct clk *extern_audio_root;

extern char *gp_reg_id;
extern char *soc_reg_id;
extern char *pu_reg_id;
extern void mx6_cpu_regulator_init(void);
extern void iomux_config(void);
#if defined(CONFIG_MX6SL_WARIO_BASE)
extern void gpio_wifi_power_enable(int enable);
#endif

extern void wan_free_gpio(void);
extern void gpio_wan_power(int enable);
#if defined(CONFIG_MX6SL_WARIO_WOODY)
extern int whistler_wan_request_gpio(void);
#elif defined(CONFIG_MX6SL_WARIO_BASE)
extern void gpio_wan_ldo_fet_init(void);
extern void wan_request_gpio(void);
#endif
extern u32 enable_ldo_mode;

#if defined(CONFIG_MX6SL_WARIO_WOODY)
extern void brcm_gpio_wifi_power_enable(int enable);
extern void brcm_gpio_bt_power_enable(int enable);
extern void brcm_gpio_wifi_set_normal_mode(int enable);
extern int brcm_gpio_wifi_bt_init(void);
#endif

#ifdef CONFIG_WARIO_SDCARD
static const struct esdhc_platform_data mx6_arm2_sd1_data __initconst = {
	.cd_gpio		= MX6_ARM2_SD1_CD,
	.wp_gpio		= MX6_ARM2_SD1_WP,
	.support_8bit		= 0,
	.keep_power_at_suspend	= 1,
	.delay_line		= 0,
};
#endif

static const struct esdhc_platform_data mx6_arm2_sd2_data __initconst = {
	.always_present		= 1,
	.support_8bit		= 1,
	.support_18v		= 1,
	.delay_line		= 0,
};

static const struct esdhc_platform_data mx6_arm2_sd3_data __initconst = {
	.always_present		= 1,
	.support_18v		= ESDHC_ALWAYS_18V,
	.delay_line		= 0,
	.clk_powerdown_delay	= 1,
};
#if defined(CONFIG_MX6SL_WARIO_WOODY)
/* Broadcom 4343W Bluetooth structures */

/* UART platform data 4343W Bluetooth - Enable RTS/CTS and DMA */
static const struct imxuart_platform_data mx6sl_wario_uart3_data __initconst = {
	.flags      = IMXUART_HAVE_RTSCTS,
	.dma_req_rx = MX6Q_DMA_REQ_UART3_RX,
	.dma_req_tx = MX6Q_DMA_REQ_UART3_TX,
};

/* Board platform data for Bluetooth */
/* GPIOs are filled here -- others will be filled in hci platform init */
struct bt_pwr_data brcm_btpwr_data = {
	.bt_rst       = MX6SL_WARIO_BT_REG_ON,
	.bt_host_wake = MX6SL_WARIO_BT_HOST_WAKE,
	.bt_dev_wake  = MX6SL_WARIO_BT_DEV_WAKE,
	.uart_pdev    = NULL,
};

static struct platform_device wario_bt_pwr_device = {
	.name   = "bt_pwr_ctrl",
	.id	= 0,
	.dev	= {
			.platform_data = &brcm_btpwr_data,
		},
};
#endif
/* This is a hack.  Ideally we would read the value of the WIFI_PWD GPIO
   to see if it is on before suspend and only turn wifi on after resume
   if so.  But gpio_get_value() doesn't work on the 1.8V GPIOs for some
   reason. Set this value manually in wifi_card_enable/disable() for now. */
static int wifi_powered = 0;

static void wario_suspend_enter(void)
{
	if(HW_SUPPORT_EMMC_POWER_GATE)
		emmc_power_gate_pins(0);
}

static void wario_suspend_exit(void)
{
	if(HW_SUPPORT_EMMC_POWER_GATE) 
		emmc_power_gate_pins(1);
}

static const struct pm_platform_data mx6_wario_pm_data __initconst = {
	.name		= "imx_pm",
	.suspend_enter	= wario_suspend_enter,
	.suspend_exit	= wario_suspend_exit,
};

#define mV_to_uV(mV) (mV * 1000)
#define uV_to_mV(uV) (uV / 1000)
#define V_to_uV(V) (mV_to_uV(V * 1000))
#define uV_to_V(uV) (uV_to_mV(uV) / 1000)


#ifdef CONFIG_MFD_MAX77696


#define MAX77696_VREG_CONSUMERS_NAME(_id) \
	max77696_vreg_consumers_##_id
#define MAX77696_VREG_CONSUMERS(_id) \
	static struct regulator_consumer_supply \
	MAX77696_VREG_CONSUMERS_NAME(_id)[]
#define MAX77696_BUCK_INIT(_id, _name, _min_uV, _max_uV,\
                           _apply_uV, _boot_on, _always_on, ops_mask, _initial_mode)\
	.init_data[MAX77696_BUCK_ID_##_id] = {\
		.constraints = {\
			.valid_modes_mask   = REGULATOR_MODE_FAST |\
				              REGULATOR_MODE_NORMAL |\
				              REGULATOR_MODE_IDLE |\
				              REGULATOR_MODE_STANDBY,\
			.valid_ops_mask     = ops_mask,\
			.min_uV             = _min_uV,\
			.max_uV             = _max_uV,\
			.input_uV           = _max_uV,\
			.apply_uV           = _apply_uV,\
			.always_on          = _always_on,\
			.boot_on            = _boot_on,\
			.name               = _name,\
			.initial_mode       = _initial_mode,\
		},\
		.num_consumer_supplies =\
			ARRAY_SIZE(MAX77696_VREG_CONSUMERS_NAME(_id)),\
		.consumer_supplies = MAX77696_VREG_CONSUMERS_NAME(_id),\
		.supply_regulator = NULL,\
	}
#define MAX77696_LDO_INIT(_id, _name, _min_uV, _max_uV,\
                          _apply_uV, _boot_on, _always_on, _supply_regulator, ops_mask)\
	.init_data[MAX77696_LDO_ID_##_id] = {\
		.constraints = {\
			.valid_modes_mask   = REGULATOR_MODE_NORMAL |\
				              REGULATOR_MODE_IDLE |\
				              REGULATOR_MODE_STANDBY,\
			.valid_ops_mask     = ops_mask,\
			.min_uV             = _min_uV,\
			.max_uV             = _max_uV,\
			.input_uV           = _max_uV,\
			.apply_uV           = _apply_uV,\
			.always_on          = _always_on,\
			.boot_on            = _boot_on,\
			.name               = _name,\
		},\
		.num_consumer_supplies =\
			ARRAY_SIZE(MAX77696_VREG_CONSUMERS_NAME(_id)),\
		.consumer_supplies = MAX77696_VREG_CONSUMERS_NAME(_id),\
		.supply_regulator = _supply_regulator,\
	}
#define MAX77696_LSW_INIT(_id, _name, _boot_on, _always_on, _supply_regulator, _active_discharge)\
	.ade[MAX77696_LSW_ID_##_id] = _active_discharge,\
	.init_data[MAX77696_LSW_ID_##_id] = {\
		.constraints = {\
			.valid_modes_mask   = 0,\
			.valid_ops_mask     = REGULATOR_CHANGE_STATUS,\
			.min_uV             = 0,\
			.max_uV             = 0,\
			.input_uV           = 0,\
			.apply_uV           = 0,\
			.always_on          = _always_on,\
			.boot_on            = _boot_on,\
			.name               = _name,\
		},\
		.num_consumer_supplies =\
			ARRAY_SIZE(MAX77696_VREG_CONSUMERS_NAME(_id)),\
		.consumer_supplies = MAX77696_VREG_CONSUMERS_NAME(_id),\
		.supply_regulator = _supply_regulator,\
	}
#define MAX77696_VDDQ_INIT(_id, _name, _min_uV, _max_uV, _apply_uV, _supply_regulator)\
	.init_data = {\
		.constraints = {\
			.valid_modes_mask   = 0,\
			.valid_ops_mask     = REGULATOR_CHANGE_VOLTAGE,\
			.min_uV             = _min_uV,\
			.max_uV             = _max_uV,\
			.input_uV           = _max_uV,\
			.apply_uV           = _apply_uV,\
			.always_on          = 1,\
			.boot_on            = 1,\
			.name               = _name,\
		},\
		.num_consumer_supplies =\
			ARRAY_SIZE(MAX77696_VREG_CONSUMERS_NAME(_id)),\
		.consumer_supplies = MAX77696_VREG_CONSUMERS_NAME(_id),\
		.supply_regulator = _supply_regulator,\
	}


/* BUCK Consumers */
MAX77696_VREG_CONSUMERS(B1) = {
	REGULATOR_SUPPLY("SW1_VDDCORE",             NULL),
};
MAX77696_VREG_CONSUMERS(B1DVS) = {
};
MAX77696_VREG_CONSUMERS(B2) = {
	REGULATOR_SUPPLY("SW2_VDDSOC",              NULL),
};
MAX77696_VREG_CONSUMERS(B2DVS) = {
};
MAX77696_VREG_CONSUMERS(B3) = {
};
MAX77696_VREG_CONSUMERS(B4) = {
};
MAX77696_VREG_CONSUMERS(B5) = {
};
MAX77696_VREG_CONSUMERS(B6) = {
};


/* LDO Consumers */
MAX77696_VREG_CONSUMERS(L1) = {
	REGULATOR_SUPPLY("ZFORCE2_TOUCH",      NULL),
};
MAX77696_VREG_CONSUMERS(L2) = {
	REGULATOR_SUPPLY("USB_GADGET_PHY",      NULL),
	REGULATOR_SUPPLY("WAN_USB_HOST_PHY",    NULL),
};
MAX77696_VREG_CONSUMERS(L3) = {
};
MAX77696_VREG_CONSUMERS(L4) = {
};
MAX77696_VREG_CONSUMERS(L5) = {
};
MAX77696_VREG_CONSUMERS(L6) = {
};
MAX77696_VREG_CONSUMERS(L7) = {
};
MAX77696_VREG_CONSUMERS(L8) = {
};
MAX77696_VREG_CONSUMERS(L9) = {
};
MAX77696_VREG_CONSUMERS(L10) = {
};

/* LSW Consumers */
#if defined(CONFIG_MX6SL_WARIO_BASE)
MAX77696_VREG_CONSUMERS(LSW1) = {
};
MAX77696_VREG_CONSUMERS(LSW2) = {
};
MAX77696_VREG_CONSUMERS(LSW3) = {
};
MAX77696_VREG_CONSUMERS(LSW4) = {
	REGULATOR_SUPPLY("DISP_GATED-old",            NULL),
};
#elif defined(CONFIG_MX6SL_WARIO_WOODY)
MAX77696_VREG_CONSUMERS(LSW1) = {
	REGULATOR_SUPPLY("DISP_GATED",            NULL),
};
MAX77696_VREG_CONSUMERS(LSW2) = {
	REGULATOR_SUPPLY("TOUCH_VDDD",            NULL),
};
MAX77696_VREG_CONSUMERS(LSW3) = {
	REGULATOR_SUPPLY("TOUCH_VDDA",            NULL),
};
MAX77696_VREG_CONSUMERS(LSW4) = {
	REGULATOR_SUPPLY("DISP_GATED-old",            NULL),
};
#else
MAX77696_VREG_CONSUMERS(LSW1) = {
};
MAX77696_VREG_CONSUMERS(LSW2) = {
};
MAX77696_VREG_CONSUMERS(LSW3) = {
};
MAX77696_VREG_CONSUMERS(LSW4) = {
};
#endif

/* VDDQ Consumer */
MAX77696_VREG_CONSUMERS(VDDQ) = {
	REGULATOR_SUPPLY("max77696_vddq",       NULL),
};
/******************************************************************************/
#ifdef CONFIG_BATTERY_MAX77696

/* TODO: will be enabled later */
#if 0
static struct max77696_gauge_reg_data max77696_gauge_init_data[] = {
	{ 0x00, 0x0000, },
};
#endif

static char* max77696_batteries[] = {
	MAX77696_PSY_BATT_NAME,
};

#endif /* CONFIG_BATTERY_MAX77696 */

/* EPD consumer supply info */
static struct regulator_consumer_supply max77696epden_consumers[] = {
	{
		.supply	= "DISPLAY",
	},
};

static struct regulator_consumer_supply max77696vcom_consumers[] = {
	{
		.supply = "VCOM",
	},
};

/* EPD platform data */
static struct regulator_init_data epd_init_pdata[MAX77696_EPD_NR_REGS] = {
	[MAX77696_EPD_ID_EPDEN] = {
		.constraints = {
			.name = "max77696-display",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(max77696epden_consumers),
		.consumer_supplies = max77696epden_consumers,
	},
	[MAX77696_EPD_ID_EPDVCOM] = {
		.constraints = {
			.name = "max77696-vcom",
			.min_uV = mV_to_uV(0),
			.max_uV = mV_to_uV(5000),
			.valid_ops_mask = (REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE),
		},
		.num_consumer_supplies = ARRAY_SIZE(max77696vcom_consumers),
		.consumer_supplies = max77696vcom_consumers,
	},
};

/******************************************************************************/
static struct max77696_platform_data max77696_pdata = {
	.core_irq = gpio_to_irq(MX6_ARM2_FEC_MDIO),
	.core_irq_trigger = IRQF_TRIGGER_LOW,
	.irq_base = MAX77696_IRQ_BASE,
	.core_debug = true,

	.osc_pdata = {
		.load_cap = MAX77696_32K_LOAD_CAP_22PF,
		.op_mode  = MAX77696_32K_MODE_LOW_POWER,
	},

#ifdef CONFIG_GPIO_MAX77696
	.gpio_pdata = {
		.irq_base  = (MAX77696_IRQ_BASE + MAX77696_ROOTINT_NR_IRQS + MAX77696_TOPSYSINT_NR_IRQS),
		.gpio_base = MAX77696_GPIO_BASE,
		.bias_en   = 0,
		.init_data = {
			[0] = {
				.pin_connected = 1,
				.alter_mode    = MAX77696_GPIO_AME_STDGPIO,
				.pullup_en     = 0,
				.pulldn_en     = 0,
				.direction     = MAX77696_GPIO_DIR_INPUT,
				.u.input       = {
					                 .debounce = MAX77696_GPIO_DBNC_0_MSEC,
					                 .refe_irq = MAX77696_GPIO_REFEIRQ_NONE,
				},
			},
                        [1] = {
                                .pin_connected = 1,
                                .alter_mode    = MAX77696_GPIO_AME_MODE_2,
                                .pullup_en     = 0,
                                .pulldn_en     = 0,
                                .direction     = MAX77696_GPIO_DIR_INPUT, /* active high */
                                .u.input       = {
                                                         .debounce = MAX77696_GPIO_DBNC_0_MSEC,
                                                         .refe_irq = MAX77696_GPIO_REFEIRQ_NONE,
                                },
                        },
			[3] = {
				.pin_connected = 1,
				.alter_mode    = MAX77696_GPIO_AME_STDGPIO,
				.pullup_en     = 0,
				.pulldn_en     = 0,
				.direction     = MAX77696_GPIO_DIR_INPUT,
				.u.input       = {
					.debounce = MAX77696_GPIO_DBNC_0_MSEC,
					.refe_irq = MAX77696_GPIO_REFEIRQ_NONE,
				},
			},
		},
	},
#endif /* CONFIG_GPIO_MAX77696 */

#ifdef CONFIG_WATCHDOG_MAX77696
	.wdt_pdata = {
		.timeout_sec = 128,
	},
#endif /* CONFIG_WATCHDOG_MAX77696 */

#ifdef CONFIG_RTC_DRV_MAX77696
	.rtc_pdata = {
		.irq_1m = 0,
		.irq_1s = 0,
	},
#endif /* CONFIG_RTC_DRV_MAX77696 */

#ifdef CONFIG_REGULATOR_MAX77696
	.ldo_pdata = {
		.imon_tf = 0x3, //Keep default value for IMON resistor,

		MAX77696_LDO_INIT(L1,  "max77696_ldo1",   800000, 3950000, 0, 0,      0, NULL, REGULATOR_CHANGE_STATUS),
		MAX77696_LDO_INIT(L2,  "max77696_ldo2",   800000, 3950000, 0, 0,      0, NULL, REGULATOR_CHANGE_STATUS),
		MAX77696_LDO_INIT(L3,  "max77696_ldo3",   800000, 3950000, 0, 0,      0, NULL, 0),
		MAX77696_LDO_INIT(L4,  "max77696_ldo4",   800000, 2375000, 0, 0,      0, NULL, 0),
		MAX77696_LDO_INIT(L5,  "max77696_ldo5",   800000, 2375000, 0, 0,      0, NULL, 0),
		MAX77696_LDO_INIT(L6,  "max77696_ldo6",   800000, 3950000, 0, 0,      0, NULL, 0),
		MAX77696_LDO_INIT(L7,  "max77696_ldo7",   800000, 3950000, 0, 0,      0, NULL, 0),
		MAX77696_LDO_INIT(L8,  "max77696_ldo8",   800000, 2375000, 0, 0,      0, NULL, 0),
		MAX77696_LDO_INIT(L9,  "max77696_ldo9",   800000, 2375000, 0, 0,      0, NULL, 0),
		MAX77696_LDO_INIT(L10, "max77696_ldo10", 2400000, 5550000, 0, 0,      0, NULL, 0),
	},
	.buck_pdata = {
		.support_suspend_ops = 0,
		/*                 id                           min_uV           apply_uV  always_on
		                          name                          max_uV      boot_on   Valid Ops                  Inital Mode
		                   --------------------------------------------------------------------------------------------*/
		MAX77696_BUCK_INIT(B1,    "max77696_buck1",     900000, 1300000, 0, 1,     1, REGULATOR_CHANGE_VOLTAGE, REGULATOR_MODE_IDLE),
		MAX77696_BUCK_INIT(B1DVS, "max77696_buck1dvs",  600000, 1300000, 0, 0,     0,                        0, REGULATOR_MODE_IDLE),
		MAX77696_BUCK_INIT(B2,    "max77696_buck2",     900000, 1300000, 0, 1,     1, REGULATOR_CHANGE_VOLTAGE, REGULATOR_MODE_IDLE),
		MAX77696_BUCK_INIT(B2DVS, "max77696_buck2dvs",  600000, 1300000, 0, 0,     0,                        0, REGULATOR_MODE_IDLE),
		MAX77696_BUCK_INIT(B3,    "max77696_buck3",     600000, 3387500, 0, 0,     0,                        0,                      0),
		MAX77696_BUCK_INIT(B4,    "max77696_buck4",    1200000, 1200000, 1, 1,     1,                        0, REGULATOR_MODE_IDLE),
		MAX77696_BUCK_INIT(B5,    "max77696_buck5",     600000, 3387500, 0, 0,     0,                        0, REGULATOR_MODE_IDLE),
                /*
                 * Workaround for RMAICEWINE-109. Change BUCK6 power mode to "Normal Operation" from "Dynamic Standby".
                 */
		MAX77696_BUCK_INIT(B6,    "max77696_buck6",     600000, 3387500, 0, 0,     0,                        0, REGULATOR_MODE_NORMAL),
        },
#if defined(CONFIG_MX6SL_WARIO_WOODY)
        .lsw_pdata = {
		/*                id    name             boot_on  always_on  supply_regulator active_discharge
		                  -------------------------------------------------------------------------------*/
                MAX77696_LSW_INIT(LSW1, "max77696_lsw1", 0,       0,         NULL,    1),
                MAX77696_LSW_INIT(LSW2, "max77696_lsw2", 0,       0,         NULL,    1),
                MAX77696_LSW_INIT(LSW3, "max77696_lsw3", 0,       0,         NULL,    1),
                MAX77696_LSW_INIT(LSW4, "max77696_lsw4", 0,       0,         NULL,    0),
#else
        .lsw_pdata = {
		/*                id    name             boot_on  always_on  supply_regulator active_discharge
		                  -----------------------------------------------*/
                MAX77696_LSW_INIT(LSW1, "max77696_lsw1", 0,       0,         NULL,    1),
                MAX77696_LSW_INIT(LSW2, "max77696_lsw2", 0,       0,         NULL,    1),
                MAX77696_LSW_INIT(LSW3, "max77696_lsw3", 0,       0,         NULL,    0),
                MAX77696_LSW_INIT(LSW4, "max77696_lsw4", 0,       0,         NULL,    1),
#endif
	},
	.vddq_pdata = {
                #define VDDQIN 1200000
		.vddq_in_uV = VDDQIN,
	},
        .vddq_pdata = {
                .vddq_in_uV = VDDQIN,
                /* VDDQOUT margin = -60% ~ +64% of VDDQIN/2 */
                MAX77696_VDDQ_INIT(VDDQ,                       /* id        */
                                   "max77696_vddq",            /* name      */
                                   (VDDQIN/2)-0.60*(VDDQIN/2), /* min_uV    */
                                   (VDDQIN/2)+0.64*(VDDQIN/2), /* max_uV    */
                                   1,                          /* always_on */
		                   "max77696_buck4"),          /* vddq is supplied by buck 4 on wario boards */
        },
	.epd_pdata = {
		.init_data = epd_init_pdata,
		.pwrgood_gpio = MX6SL_ARM2_EPDC_PWRSTAT,
		.pwrgood_irq = gpio_to_irq(MX6SL_ARM2_EPDC_PWRSTAT),
	},
#endif /* CONFIG_REGULATOR_MAX77696 */

#ifdef CONFIG_LEDS_MAX77696
	.led_pdata = {
		[0] = {
			.info = {
				.name = MAX77696_LEDS_NAME".0"
			},
			.sol_brightness = LED_DEF_BRIGHTNESS,		/* default 0x3F */
			.manual_mode = false,
			.init_state = LED_FLASHD_DEF,
		},
		[1] = {
			.info = {
				.name = MAX77696_LEDS_NAME".1"
			},
			.sol_brightness = LED_DEF_BRIGHTNESS,		/* default 0x3F */
			.manual_mode = false,
			.init_state = LED_FLASHD_DEF,
		},
	},
#endif /* CONFIG_LEDS_MAX77696 */

#ifdef CONFIG_BACKLIGHT_MAX77696
	.bl_pdata = {
		.brightness = WARIO_FL_LEVEL12_MID,
	},
#endif /* CONFIG_BACKLIGHT_MAX77696 */

#ifdef CONFIG_SENSORS_MAX77696
	.adc_pdata = {
		.avg_rate = 32,
		.adc_delay = 0,
		.current_src = 50,
	},
#endif /* CONFIG_SENSORS_MAX77696 */

#ifdef CONFIG_BATTERY_MAX77696
	.gauge_pdata = {
		.v_alert_max                       = 4260,	/* V_ALRT_MAX = 4.26V (4.2V + 60mV tolerance) */
		.v_alert_min                       = 3400,	/* V_ALRM_MIN = 3.4V */
		.t_alert_max                       = 60,
		.t_alert_min                       = 0,
		.s_alert_max                       = 0,
		.s_alert_min                       = 0,
		.enable_alert_on_battery_removal   = 0,
		.enable_alert_on_battery_insertion = 0,
		.enable_current_sense              = 1,
		.enable_por_init                   = 1,
		.charge_full_design                = 790000, /* in uAh */
		.battery_full_capacity             = 95, /* in % */
		.r_sns                             = 10000,		/* SENSE_RESISTOR = 10mOhm */
		.update_interval_ms                = 30000,
		.update_interval_relax_ms          = 3600000,
		.polling_properties                = (MAX77696_GAUGE_POLLING_CAPACITY |
											  MAX77696_GAUGE_POLLING_TEMP |
											  MAX77696_GAUGE_POLLING_VOLTAGE |
											  MAX77696_GAUGE_POLLING_CURRENT_AVG |
											  MAX77696_GAUGE_POLLING_NAC |
											  MAX77696_GAUGE_POLLING_LMD |
											  MAX77696_GAUGE_POLLING_CYCLE |
											  MAX77696_GAUGE_POLLING_NAC_AVG |
											  MAX77696_GAUGE_POLLING_NAC_MIX |
											  MAX77696_GAUGE_POLLING_CAPACITY_AVG |
											  MAX77696_GAUGE_POLLING_CAPACITY_MIX),

#if 0
		.init_data                         = max77696_gauge_init_data,
		.num_init_data                     = ARRAY_SIZE(max77696_gauge_init_data),
#endif
		.battery_online                    = NULL,
		.charger_online                    = max77696_charger_online_def_cb,
		.charger_enable                    = max77696_charger_enable_def_cb,
	},
#endif /* CONFIG_BATTERY_MAX77696 */

#ifdef CONFIG_CHARGER_MAX77696
	.uic_pdata = {
		.uic_notify = max77696_uic_notify_def_cb,
	},
	.chg_pdata = {
#ifdef CONFIG_BATTERY_MAX77696
		.batteries       = max77696_batteries,
		.num_batteries   = ARRAY_SIZE(max77696_batteries),
#endif /* CONFIG_BATTERY_MAX77696 */
		.wdt_period_ms   = 0, /* Disabled */
		.cc_uA           = 466000,/*466mA default */
		.cv_prm_mV       = 4200,  /* 4.2V device default */
		.cv_jta_mV       = 4000,  /* 4.0V device default */
		.fast_chg_time   = 10,    /* Fast Charge timer duration 10hrs */
		.to_time         = 20,	  /* Top-off time 20 minutes */
		.to_ith          = 75000, /* Top-off i threshold 75mA */
		.t1_C            = 0,     /* device default */
		.t2_C            = 15,    /* device default */
		.t3_C            = 44,    /* device default */
		.t4_C            = 54,    /* device default */
		.chg_dc_lpm      = false, /* dc-dc low power mode */
		.icl_ilim        = MAX77696_CHARGER_A_ICL_ILIM_0P5A,	/* default 500mA */
		.wakeup_irq      = 0,
		.charger_notify  = max77696_charger_notify_def_cb,
	},
	.eh_pdata = {
#ifdef CONFIG_BATTERY_MAX77696
		.batteries       = max77696_batteries,
		.num_batteries   = ARRAY_SIZE(max77696_batteries),
#endif /* CONFIG_BATTERY_MAX77696 */
		.charger_notify  = max77696_eh_notify_def_cb,
	},
#endif /* CONFIG_CHARGER_MAX77696 */
#ifdef CONFIG_INPUT_MAX77696_ONKEY
	.onkey_pdata = {
		.wakeup_1sec_delayed_since_onkey_down = 0, /* See EN0DLY in GLBLCNFG1 */
		.wakeup_after_mrwrn                   = 0,
		.wakeup_after_mro                     = 1, /* See MROWK in GLBLCNFG2 */
		.manual_reset_time                    = 12, /* in seconds */
		.onkey_keycode                        = KEY_POWER,
		.hold_1sec_keycode                    = KEY_POWER,
		.mr_warn_keycode                      = KEY_POWER,
	},
#endif /* CONFIG_INPUT_MAX77696_ONKEY */
};

#endif /* CONFIG_MFD_MAX77696 */

#ifdef CONFIG_WARIO_HALL
static struct hall_platform_data mx6sl_wario_hall_platform_data = {
	.hall_gpio = MX6_WARIO_HALL_SNS,
	.hall_irq = gpio_to_irq(MX6_WARIO_HALL_SNS),
	.desc = "hall sensor",
	.wakeup = 1,    /* configure the button as a wake-up source */
};

static struct platform_device mx6sl_wario_hall_device = {
	.name   = "wario_hall",
	.id     = -1,
	.dev    = {
		.platform_data = &mx6sl_wario_hall_platform_data,
	},
};
#endif

#ifdef CONFIG_POWER_SODA
static struct soda_platform_data mx6sl_wario_soda_platform_data = {
	.scl_gpio = MX6_SODA_I2C_SCL,
	.sda_gpio = MX6_SODA_I2C_SDA,
	.i2c_bb_delay = I2C_BB_DELAY_US,
	.soda_sda_dock_irq = gpio_to_irq(MX6_SODA_I2C_SDA),
	.i2c_sda_pu_gpio = MX6_SODA_I2C_SDA_PU,
	.boost_ctrl_gpio = MX6_SODA_BOOST,
	.ext_chg_gpio = MX6_SODA_CHG_DET,
	.ext_chg_irq = gpio_to_irq(MX6_SODA_CHG_DET),
	.vbus_en_gpio = MX6_SODA_VBUS_ENABLE,
	.otg_sw_gpio = MX6_SODA_OTG_SW, //this is going to be changed somewhere down in this file
	.update_interval_ms = 10000,	
};

static struct platform_device mx6sl_wario_soda_device = {
	.name   = "soda",
	.id     = -1,
	.dev    = {
		.platform_data = &mx6sl_wario_soda_platform_data,
	},
};
#endif

static struct spi_board_info panel_flash_device[] __initdata = {
	{
		.modalias = "panel_flash_spi",
		.max_speed_hz = 1000000,        /* max spi SCK clock speed in HZ */
		.bus_num = 0,
		.chip_select = 0,
	}
};

void gpio_init_touch_switch_power(void)
{
	gpio_request(MX6_WARIO_TOUCH_1V8, "touch_1v8");
	gpio_request(MX6_WARIO_TOUCH_3V2, "touch_3v2");
	gpio_direction_output(MX6_WARIO_TOUCH_1V8, 1);
	gpio_direction_output(MX6_WARIO_TOUCH_3V2, 1);
}
EXPORT_SYMBOL(gpio_init_touch_switch_power);

void gpio_touch_switch_power_1v8(int on_off)
{
	gpio_set_value(MX6_WARIO_TOUCH_1V8, !!on_off);
}
EXPORT_SYMBOL(gpio_touch_switch_power_1v8);

void gpio_touch_switch_power_3v2(int on_off)
{
	gpio_set_value(MX6_WARIO_TOUCH_3V2, !!on_off);
}
EXPORT_SYMBOL(gpio_touch_switch_power_3v2);

void gpio_touch_reset_irq_switch(int on_off)
{
	if(on_off) {
		gpio_direction_input(MX6SL_PIN_TOUCH_INTB);
		gpio_direction_output(MX6SL_PIN_TOUCH_RST, 1);
	} else {
		gpio_direction_output(MX6SL_PIN_TOUCH_RST, 0);
		gpio_direction_output(MX6SL_PIN_TOUCH_INTB, 0);
	}
}
EXPORT_SYMBOL(gpio_touch_reset_irq_switch);

static struct imxi2c_platform_data mx6_arm2_i2c0_data = {
	.bitrate = 350000,
};

static struct imxi2c_platform_data mx6_arm2_i2c1_data = {
	.bitrate = 350000,
};

static struct imxi2c_platform_data mx6_arm2_i2c1_data_icewine = {
	.bitrate = 120000,
};

static struct imxi2c_platform_data mx6_arm2_i2c2_data = {
	.bitrate = 350000,
};

static struct i2c_board_info mxc_i2c0_board_info[] __initdata = {
	{
		I2C_BOARD_INFO(MAX77696_NAME, MAX77696_I2C_ADDR),
		.platform_data = &max77696_pdata,
	},
};

static struct i2c_board_info mxc_i2c1_board_info[] __initdata = {
	{
		/* Cypress v4 Touch driver */
		I2C_BOARD_INFO("cyttsp4_i2c_adapter", 0x24),
	},
	{
		/* ZForce v2 Touch driver */
		I2C_BOARD_INFO("zforce2", 0x50),
	},
};

static struct i2c_board_info mxc_i2c2_board_info[] __initdata = {
	{
		/* Wario ALS MAX44009 */
		I2C_BOARD_INFO("max44009_als", 0x4A)
	},
#if defined(CONFIG_INPUT_PROX_PIC12LF1822) ||\
	defined(CONFIG_INPUT_PROX_PIC12LF1822_MODULE)
	{
		/* Wario Proximity */
		I2C_BOARD_INFO("wario_prox", 0x0D)
	},
#endif
#if defined(CONFIG_INPUT_SX9500) || defined(CONFIG_INPUT_SX9500_MODULE)
	{
		/* sx9306 Proximity */
		I2C_BOARD_INFO("sx9500", 0x28),
		.flags         = I2C_CLIENT_WAKE,
		.platform_data = &sx9500_config,
	},
#endif
	{
		/* FSR Button */
		I2C_BOARD_INFO("fsr_keypad", 0x58)
	},
	{
		/* Haptics */
		I2C_BOARD_INFO("drv26xx", 0x59)
	},
};

#ifdef DEVELOPMENT_MODE
static void wario_debug_toggle_pin_init(void)
{
	gpio_request(MX6SL_PIN_DEBUG_TOGGLE, "debug_toggle");
	gpio_direction_output(MX6SL_PIN_DEBUG_TOGGLE, 1);
}

void wario_debug_toggle(int times)
{
	for ( ; times; times--) {
		gpio_set_value(MX6SL_PIN_DEBUG_TOGGLE, 0);
		msleep(1);
		gpio_set_value(MX6SL_PIN_DEBUG_TOGGLE, 1);
		msleep(1);
	}
}
EXPORT_SYMBOL(wario_debug_toggle);
#endif

int wario_pmic_gpio_init(void)
{
	int ret = 0;
	gpio_request(MX6_ARM2_FEC_MDIO, "max77696");
	gpio_direction_input(MX6_ARM2_FEC_MDIO);
	return ret;
}

int wario_pmic_init(void)
{
#ifdef CONFIG_MX6_INTER_LDO_BYPASS
	gp_reg_id = "SW1_VDDCORE";
	soc_reg_id = "SW2_VDDSOC";
	pu_reg_id = NULL; /* unused on Wario */
	enable_ldo_mode = LDO_MODE_BYPASSED;
#endif
	/* initialize the pmic events listptr */
	pmic_event_list_init();

	/* initialize gpio's */
	return wario_pmic_gpio_init();
}

static struct mxc_dvfs_platform_data mx6sl_arm2_dvfscore_data = {
	.reg_id			= "cpu_vddgp",
	.clk1_id		= "cpu_clk",
	.clk2_id		= "gpc_dvfs_clk",
	.gpc_cntr_offset	= MXC_GPC_CNTR_OFFSET,
	.ccm_cdcr_offset	= MXC_CCM_CDCR_OFFSET,
	.ccm_cacrr_offset	= MXC_CCM_CACRR_OFFSET,
	.ccm_cdhipr_offset	= MXC_CCM_CDHIPR_OFFSET,
	.prediv_mask		= 0x1F800,
	.prediv_offset		= 11,
	.prediv_val		= 3,
	.div3ck_mask		= 0xE0000000,
	.div3ck_offset		= 29,
	.div3ck_val		= 2,
	.emac_val		= 0x08,
	.upthr_val		= 25,
	.dnthr_val		= 9,
	.pncthr_val		= 33,
	.upcnt_val		= 10,
	.dncnt_val		= 10,
	.delay_time		= 80,
};

void __init early_console_setup(unsigned long base, struct clk *clk);

static inline void mx6_arm2_init_uart(void)
{
	imx6sl_add_imx_uart(0, NULL); /* DEBUG UART1 */
	/* Enable UART4 for Bourbon */
	if(lab126_board_is(BOARD_ID_BOURBON_WFO) ||
		lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C) ||
		lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2) )
		imx6sl_add_imx_uart(3, NULL); /* UART4 */
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	/* Enable UART3 as Broadcom BT Main interface */
	if (lab126_board_is(BOARD_ID_WHISKY_WFO) ||
		lab126_board_is(BOARD_ID_WHISKY_WAN) || 
		lab126_board_is(BOARD_ID_WOODY))
		imx6sl_add_imx_uart(2, &mx6sl_wario_uart3_data); /* UART3 */
#endif
}

static iomux_v3_cfg_t mx6sl_arm2_epdc_enable_pads[] = {
	/* EPDC */
	MX6SL_PAD_EPDC_D0__EPDC_SDDO_0,
	MX6SL_PAD_EPDC_D1__EPDC_SDDO_1,
	MX6SL_PAD_EPDC_D2__EPDC_SDDO_2,
	MX6SL_PAD_EPDC_D3__EPDC_SDDO_3,
	MX6SL_PAD_EPDC_D4__EPDC_SDDO_4,
	MX6SL_PAD_EPDC_D5__EPDC_SDDO_5,
	MX6SL_PAD_EPDC_D6__EPDC_SDDO_6,
	MX6SL_PAD_EPDC_D7__EPDC_SDDO_7,

	MX6SL_PAD_EPDC_GDCLK__EPDC_GDCLK,
	MX6SL_PAD_EPDC_GDSP__EPDC_GDSP,
	MX6SL_PAD_EPDC_GDOE__EPDC_GDOE,

	MX6SL_PAD_EPDC_SDCLK__EPDC_SDCLK,
	MX6SL_PAD_EPDC_SDOE__EPDC_SDOE,
	MX6SL_PAD_EPDC_SDLE__EPDC_SDLE,
	MX6SL_PAD_EPDC_SDCE0__EPDC_SDCE_0,
};

static iomux_v3_cfg_t mx6sl_arm2_epdc_disable_pads[] = {
	/* EPDC */
	MX6SL_PAD_EPDC_D0__GPIO_1_7,
	MX6SL_PAD_EPDC_D1__GPIO_1_8,
	MX6SL_PAD_EPDC_D2__GPIO_1_9,
	MX6SL_PAD_EPDC_D3__GPIO_1_10,
	MX6SL_PAD_EPDC_D4__GPIO_1_11,
	MX6SL_PAD_EPDC_D5__GPIO_1_12,
	MX6SL_PAD_EPDC_D6__GPIO_1_13,
	MX6SL_PAD_EPDC_D7__GPIO_1_14,

	MX6SL_PAD_EPDC_GDCLK__GPIO_1_31,
	MX6SL_PAD_EPDC_GDSP__GPIO_2_2,
	MX6SL_PAD_EPDC_GDOE__GPIO_2_0,

	MX6SL_PAD_EPDC_SDCLK__GPIO_1_23,
	MX6SL_PAD_EPDC_SDOE__GPIO_1_25,
	MX6SL_PAD_EPDC_SDLE__GPIO_1_24,
	MX6SL_PAD_EPDC_SDCE0__GPIO_1_27,

};

static int epdc_get_pins(void)
{
	int ret = 0;

	/* Claim GPIOs for EPDC pins - used during power up/down */
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_0, "epdc_d0");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_1, "epdc_d1");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_2, "epdc_d2");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_3, "epdc_d3");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_4, "epdc_d4");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_5, "epdc_d5");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_6, "epdc_d6");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_7, "epdc_d7");
	ret |= gpio_request(MX6SL_ARM2_EPDC_GDCLK, "epdc_gdclk");
	ret |= gpio_request(MX6SL_ARM2_EPDC_GDSP, "epdc_gdsp");
	ret |= gpio_request(MX6SL_ARM2_EPDC_GDOE, "epdc_gdoe");
	ret |= gpio_request(MX6SL_ARM2_EPDC_GDRL, "epdc_gdrl");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDCLK, "epdc_sdclk");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDOE, "epdc_sdoe");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDLE, "epdc_sdle");
	ret |= gpio_request(MX6SL_ARM2_EPDC_BDR0, "epdc_bdr0");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDCE0, "epdc_sdce0");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDCE1, "epdc_sdce1");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDCE2, "epdc_sdce2");

	return ret;
}

static void epdc_put_pins(void)
{
	gpio_free(MX6SL_ARM2_EPDC_SDDO_0);
	gpio_free(MX6SL_ARM2_EPDC_SDDO_1);
	gpio_free(MX6SL_ARM2_EPDC_SDDO_2);
	gpio_free(MX6SL_ARM2_EPDC_SDDO_3);
	gpio_free(MX6SL_ARM2_EPDC_SDDO_4);
	gpio_free(MX6SL_ARM2_EPDC_SDDO_5);
	gpio_free(MX6SL_ARM2_EPDC_SDDO_6);
	gpio_free(MX6SL_ARM2_EPDC_SDDO_7);
	gpio_free(MX6SL_ARM2_EPDC_GDCLK);
	gpio_free(MX6SL_ARM2_EPDC_GDSP);
	gpio_free(MX6SL_ARM2_EPDC_GDOE);
	gpio_free(MX6SL_ARM2_EPDC_GDRL);
	gpio_free(MX6SL_ARM2_EPDC_SDCLK);
	gpio_free(MX6SL_ARM2_EPDC_SDOE);
	gpio_free(MX6SL_ARM2_EPDC_SDLE);
	gpio_free(MX6SL_ARM2_EPDC_BDR0);
	gpio_free(MX6SL_ARM2_EPDC_SDCE0);
	gpio_free(MX6SL_ARM2_EPDC_SDCE1);
	gpio_free(MX6SL_ARM2_EPDC_SDCE2);
}

static void epdc_enable_pins(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx6sl_arm2_epdc_enable_pads, \
			ARRAY_SIZE(mx6sl_arm2_epdc_enable_pads));
}

static void epdc_disable_pins(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx6sl_arm2_epdc_disable_pads, \
			ARRAY_SIZE(mx6sl_arm2_epdc_disable_pads));
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_0);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_1);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_2);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_3);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_4);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_5);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_6);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_7);

	gpio_direction_input(MX6SL_ARM2_EPDC_GDCLK);
	gpio_direction_input(MX6SL_ARM2_EPDC_GDSP);
	gpio_direction_output(MX6SL_ARM2_EPDC_GDOE, 0);

	gpio_direction_input(MX6SL_ARM2_EPDC_SDCLK);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDOE);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDLE);

	gpio_direction_output(MX6SL_ARM2_EPDC_SDCE0, 0);
}

/* i.MX6SL waveform data timing data structures for e60_pinot */
/* Created on - Tuesday, November 08, 2011 11:38:37
Warning: this pixel clock is derived from 480 MHz parent! */
static struct fb_videomode e60_pinot_mode = {
	.name         = "E60_V220",
	.refresh      = 85,
	.xres         = 1024,
	.yres         = 758,
	.pixclock     = 40000000,
	.left_margin  = 12,
	.right_margin = 76,
	.upper_margin = 4,
	.lower_margin = 5,
	.hsync_len    = 12,
	.vsync_len    = 2,
	.sync         = 0,
	.vmode        = FB_VMODE_NONINTERLACED,
	.flag         = 0,
};

static struct fb_videomode en060oc1_3ce_225_mode = {
	.name         = "EN060OC1-3CE-225",
	.refresh      = 85,
	.xres         = 1440,
	.yres         = 1080,
	.pixclock     = 120000000,
	.left_margin  = 24,
	.right_margin = 528,
	.upper_margin = 4,
	.lower_margin = 3,
	.hsync_len    = 24,
	.vsync_len    = 2,
	.sync         = 0,
	.vmode        = FB_VMODE_NONINTERLACED,
	.flag         = FLAG_SCAN_X_INVERT,
};

static struct fb_videomode ed060tc1_3ce_mode = {
	.name         = "ED060TC1-3CE",
	.refresh      = 85,
	.xres         = 1448,
	.yres         = 1072,
	.pixclock     = 80000000,
	.left_margin  = 16,
	.right_margin = 104,
	.upper_margin = 4,
	.lower_margin = 4,
	.hsync_len    = 26,
	.vsync_len    = 2,
	.sync         = 0,
	.vmode        = FB_VMODE_NONINTERLACED,
	.flag         = 0,
};

static struct fb_videomode e60_v220_wj_mode = {
	.name         = "E60_V220_WJ",
	.refresh      = 85,
	.xres         = 800,
	.yres         = 600,
	.pixclock     = 32000000,
	.left_margin  = 17,
	.right_margin = 172,
	.upper_margin = 4,
	.lower_margin = 18,
	.hsync_len    = 15,
	.vsync_len    = 4,
	.sync         = 0,
	.vmode        = FB_VMODE_NONINTERLACED,
	.flag         = 0,
};


static struct fb_videomode ed060scp_mode = {
	.name         = "ED060SCP",
	.refresh      = 85,
	.xres         = 800,
	.yres         = 600,
	.pixclock     = 26666667,
	.left_margin  = 8,
	.right_margin = 100,
	.upper_margin = 4,
	.lower_margin = 8,
	.hsync_len    = 4,
	.vsync_len    = 1,
	.sync         = 0,
	.vmode        = FB_VMODE_NONINTERLACED,
	.flag         = 0,
};

//whisky
static struct fb_videomode en060tc1_1ce_mode = {
	.name="20150722_EN060TC1_1CE",
	.refresh      = 85,
	.xres         = 1072,
	.yres         = 1448,
	.pixclock     = 80000000,
	.left_margin  = 20,
	.right_margin = 56,
	.upper_margin = 4,
	.lower_margin = 7,
	.hsync_len    = 32,
	.vsync_len    = 1,
	.sync         = 0,
	.vmode        = FB_VMODE_NONINTERLACED,
	.flag         = FLAG_SCAN_X_INVERT,
};

static struct imx_epdc_fb_mode panel_modes[PANEL_MODE_COUNT] = {
	[PANEL_MODE_E60_PINOT] = {
		.vmode         = &e60_pinot_mode,
		.vscan_holdoff = 4,
		.sdoed_width   = 10,
		.sdoed_delay   = 20,
		.sdoez_width   = 10,
		.sdoez_delay   = 20,
		.gdclk_hp_offs = 524,
		.gdsp_offs     = 25,
		.gdoe_offs     = 0,
		.gdclk_offs    = 19,
		.num_ce        = 1,
		.physical_width  = 122,
		.physical_height = 91,
		.material = EPD_MATERIAL_V320,
	},
	[PANEL_MODE_EN060OC1_3CE_225] = {
		.vmode         = &en060oc1_3ce_225_mode,
		.vscan_holdoff = 4,
		.sdoed_width   = 10,
		.sdoed_delay   = 20,
		.sdoez_width   = 10,
		.sdoez_delay   = 20,
		.gdclk_hp_offs = 1116,
		.gdsp_offs     = 86,
		.gdoe_offs     = 0,
		.gdclk_offs    = 57,
		.num_ce        = 3,
		.physical_width  = 122,
		.physical_height = 91,
		.material = EPD_MATERIAL_V320,
	},
	[PANEL_MODE_ED060TC1_3CE] = {
		.vmode         = &ed060tc1_3ce_mode,
		.vscan_holdoff = 4,
		.sdoed_width   = 10,
		.sdoed_delay   = 20,
		.sdoez_width   = 10,
		.sdoez_delay   = 20,
		.gdclk_hp_offs = 562,
		.gdsp_offs     = 662,
		.gdoe_offs     = 0,
		.gdclk_offs    = 225,
		.num_ce        = 3,
		.physical_width  = 122,
		.physical_height = 91,
		.material = EPD_MATERIAL_V320,
	},
	[PANEL_MODE_ED060SCN] = {
		.vmode         = &e60_v220_wj_mode,
		.vscan_holdoff = 4,
		.sdoed_width   = 10,
		.sdoed_delay   = 20,
		.sdoez_width   = 10,
		.sdoez_delay   = 20,
		.gdclk_hp_offs = 425,
		.gdsp_offs     = 321,
		.gdoe_offs     = 0,
		.gdclk_offs    = 17,
		.num_ce        = 1,
		.physical_width = 122,
		.physical_height = 91,
		.material = EPD_MATERIAL_V220,
	},
	[PANEL_MODE_ED060SCP] = {
		.vmode         = &ed060scp_mode,
		.vscan_holdoff = 4,
		.sdoed_width   = 10,
		.sdoed_delay   = 20,
		.sdoez_width   = 10,
		.sdoez_delay   = 20,
		.gdclk_hp_offs = 419,
		.gdsp_offs     = 263,
		.gdoe_offs     = 0,
		.gdclk_offs    = 5,
		.num_ce        = 1,
		.physical_width = 122,
		.physical_height = 91,
		.material = EPD_MATERIAL_V220,
	},
	//whisky
	[PANEL_MODE_EN060TC1_CARTA_1_2] = {
		.vmode         = &en060tc1_1ce_mode,
		.vscan_holdoff = 4,
		.sdoed_width   = 10,
		.sdoed_delay   = 20,
		.sdoez_width   = 10,
		.sdoez_delay   = 20,
		.gdclk_hp_offs = 465,
		.gdsp_offs     = 415,
		.gdoe_offs     = 0,
		.gdclk_offs    = 91,
		.num_ce        = 1,
		.physical_width = 91,
		.physical_height = 122,
		.material = EPD_MATERIAL_CARTA_1_2,
	},
	/* panel mode param for muscat */
	[PANEL_MODE_ED060TC1_3CE_CARTA_1_2] = {
		.vmode         = &ed060tc1_3ce_mode,
		.vscan_holdoff = 4,
		.sdoed_width   = 10,
		.sdoed_delay   = 20,
		.sdoez_width   = 10,
		.sdoez_delay   = 20,
		.gdclk_hp_offs = 562,
		.gdsp_offs     = 662,
		.gdoe_offs     = 0,
		.gdclk_offs    = 225,
		.num_ce        = 3,
		.physical_width  = 122,
		.physical_height = 91,
		.material = EPD_MATERIAL_CARTA_1_2,
	},
};

static struct imx_epdc_fb_platform_data epdc_data = {
	.epdc_mode = panel_modes,
	.num_modes = ARRAY_SIZE(panel_modes),
	.get_pins = epdc_get_pins,
	.put_pins = epdc_put_pins,
	.enable_pins = epdc_enable_pins,
	.disable_pins = epdc_disable_pins,
};

static void __init mx6_arm2_init_usb(void)
{
	imx_otg_base = MX6_IO_ADDRESS(MX6Q_USB_OTG_BASE_ADDR);

}

static struct platform_pwm_backlight_data mx6_arm2_pwm_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 255,
	.dft_brightness	= 128,
	.pwm_period_ns	= 50000,
};

static int mx6_wario_spi_cs[] = {
	MX6_ARM2_ECSPI1_CS0,
};

static const struct spi_imx_master mx6_wario_spi_data __initconst = {
	.chipselect     = mx6_wario_spi_cs,
	.num_chipselect = ARRAY_SIZE(mx6_wario_spi_cs),
};

#ifdef CONFIG_CYPRESS_CYTTSP4_BUS
extern void __init mx6sl_cyttsp4_init(void);
#endif
struct platform_device *wifi_pdev = NULL;

int wifi_card_enable(void)
{
	struct sdhci_host *sdhost;

	if (!wifi_pdev) {
		printk(KERN_ERR "%s: device not available!\n", __func__);
		return -1;
	}

	wifi_powered = 1;


	sdhost = platform_get_drvdata(wifi_pdev);
	/* Error, it can't be even before we applied power */
	if (sdhost->pwr != 0) {
		printk(KERN_ERR "%s: device power already on!\n", __func__);
		return -1;
	}

	/* Enable card power */
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	if (lab126_board_is(BOARD_ID_WHISKY_WFO) ||
		lab126_board_is(BOARD_ID_WHISKY_WAN) ||
		lab126_board_is(BOARD_ID_WOODY)) {
			brcm_gpio_wifi_power_enable(1);
	}
#endif
#if defined(CONFIG_MX6SL_WARIO_BASE)
		gpio_wifi_power_enable(1);
#endif	
	mdelay(100);

	tasklet_schedule(&sdhost->card_tasklet);

	return 0;
}
EXPORT_SYMBOL(wifi_card_enable);


void wifi_card_disable(void)
{
	struct sdhci_host *sdhost;

	wifi_powered = 0;

	/* Power down the card first */
#if defined(CONFIG_MX6SL_WARIO_WOODY)
		if (lab126_board_is(BOARD_ID_WHISKY_WFO) ||
		lab126_board_is(BOARD_ID_WHISKY_WAN) ||
		lab126_board_is(BOARD_ID_WOODY)) {
			brcm_gpio_wifi_power_enable(0);
	}
#endif
#if defined(CONFIG_MX6SL_WARIO_BASE)
	gpio_wifi_power_enable(0);
#endif
	if (wifi_pdev) {
		int timeout = 10 ;
		/* Host driver will remove the card when a request fails
		because we just turned off power to the card */
		sdhost = platform_get_drvdata(wifi_pdev);
		tasklet_schedule(&sdhost->card_tasklet);

		//Sleep at the start to give a chance for the tasklet.
		msleep(10);
		while(sdhost->pwr != 0 && timeout ) {
			msleep(100);
			timeout--;
		}
		//Even after 10 attempts 100 msec if we fail, the log the error.
		if (!timeout && sdhost->pwr)
			printk(KERN_ERR "%s: wifi module clean unload failed.\n", __func__);
		}

}

EXPORT_SYMBOL(wifi_card_disable);

int haptic_request_pins(void)
{
	int ret;
	ret = gpio_request(MX6SL_ARM2_EPDC_SDSHR,    "haptic_drive");
	ret |= gpio_request(MX6SL_ARM2_EPDC_SDDO_14, "haptic_id");

	gpio_direction_input(MX6SL_ARM2_EPDC_SDDO_14);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDSHR);

	return ret;
}
EXPORT_SYMBOL(haptic_request_pins);

/*return the haptic id pin state*/
int gpio_haptic_ident(void)
{
	int state;
	gpio_direction_output(MX6SL_ARM2_EPDC_SDSHR, 1);
	msleep(1);
	state = gpio_get_value(MX6SL_ARM2_EPDC_SDDO_14);
	gpio_direction_input(MX6SL_ARM2_EPDC_SDSHR);
	return state;
}
EXPORT_SYMBOL(gpio_haptic_ident);

void haptic_drive_pin(u8 enable)
{
	if(enable)
		gpio_direction_output(MX6SL_ARM2_EPDC_SDSHR, 1);
	else
		gpio_direction_input(MX6SL_ARM2_EPDC_SDSHR);
}
EXPORT_SYMBOL(haptic_drive_pin);

static void wan_gpio_init(void)
{
#if defined(CONFIG_MX6SL_WARIO_WOODY)
	if (whistler_wan_request_gpio()) {
		printk(KERN_ERR "%s: failed to request WAN GPIO!\n", __func__);
		return;
	}
#elif defined(CONFIG_MX6SL_WARIO_BASE)
	wan_request_gpio();
	/* Init & enable WAN LDO control by default */
	gpio_wan_ldo_fet_init();
#endif
	gpio_wan_power(0);

	/*free'em, it'll be requested again in wan module*/
	wan_free_gpio();
}

#if defined(CONFIG_MX6SL_WARIO_WOODY)
struct wifi_platform_data {
	int (*set_power)(int val);
	int (*set_reset)(int val);
	int (*set_carddetect)(int val);
	void *(*mem_prealloc)(int section, unsigned long size);
	int (*get_mac_addr)(unsigned char *buf);
	void *(*get_country_code)(char *ccode);
};

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}

static int brcm_wifi_get_mac_addr(unsigned char *buf)
{
	char mac_addr[6];
	int idx;
	if (!buf)
		return -EFAULT;
		
	for (idx = 0; idx < 6; idx++) {
		mac_addr[idx]  = hexval(lab126_mac_address[idx*2])<<4;
		mac_addr[idx] += hexval(lab126_mac_address[idx*2+1]);
	}
	memcpy(buf, mac_addr, 6);

	return 0;
}

static struct resource bcm_wifi_resource[] = {
	[0] = {
	 .name = "bcmdhd_wlan_irq",
	 .start = gpio_to_irq(MX6SL_WARIO_WIFI_WAKE_ON_LAN_B),
	 .end   = gpio_to_irq(MX6SL_WARIO_WIFI_WAKE_ON_LAN_B),
	 .flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct wifi_platform_data brcm_wifi_control = {
	.set_power      = NULL,
	.set_reset      = NULL,
	.set_carddetect = NULL,
	.mem_prealloc	= NULL,
	.get_mac_addr	= brcm_wifi_get_mac_addr,
	.get_country_code = NULL,
};

static struct platform_device bcm_wifi_device = {
	 .name           = "bcmdhd_wlan",
	 .id             = 1,
	 .num_resources  = ARRAY_SIZE(bcm_wifi_resource),
	 .resource       = bcm_wifi_resource,
	 .dev            = {
		 .platform_data = &brcm_wifi_control,
	 },
};
#endif

/*!
 * Board specific initialization.
 * TODO cleanup needed
 */
static void __init mx6_wario_init(void)
{
	lab126_idme_vars_init();

	/* invoke the FSL auto generated code
	 * to do initial iomux config */
	iomux_config();

	wario_pmic_init();

	if (lab126_board_rev_greater_eq(BOARD_ID_ICEWINE_WARIO_P5) ||
		lab126_board_rev_greater_eq(BOARD_ID_ICEWINE_WFO_WARIO_P5) ||
		lab126_board_is(BOARD_ID_ICEWINE_WARIO_512) ||
		lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO_512) ) {
		/*
		 * Charge termination voltage = 4.35V [Icewine post EVT1.1]
		 */
		max77696_pdata.chg_pdata.cv_prm_mV = 4350;

		/* V_ALRT_MAX = 4.4V (4.35V + 50mV tolerance) */
		max77696_pdata.gauge_pdata.v_alert_max = 4400;
	}

	if (lab126_board_is(BOARD_ID_WHISKY_WFO) ||
		lab126_board_is(BOARD_ID_WHISKY_WAN) ) {
		max77696_pdata.chg_pdata.cc_uA= 133000;	
		max77696_pdata.chg_pdata.to_ith = 50000;	
		max77696_pdata.chg_pdata.cv_jta_mV = 4100;	/* JEITA Charge termination voltage = 4.1V */
		max77696_pdata.chg_pdata.fast_chg_time = 6;	/* Fast Charge timer duration = 6hours */
		max77696_pdata.chg_pdata.chg_dc_lpm = true;	/* enable chga dc-dc low power mode */

		/* S_ALRT_MAX = 95% SOC; S_ALRT_MIN = 80% SOC */
		max77696_pdata.gauge_pdata.s_alert_max = SYS_HI_SOC_THRESHOLD;
		max77696_pdata.gauge_pdata.s_alert_min = SYS_LO_SOC_THRESHOLD;

		/* T_ALRT_MAX = 60degC; T_ALRT_MIN = 45degC */
		max77696_pdata.gauge_pdata.t_alert_max = SYS_CRIT_TEMP_THRESHOLD;
		max77696_pdata.gauge_pdata.t_alert_min = SYS_HI_TEMP_THRESHOLD;

		/*
		 * Set LED sign-of-life brightness to 0x1F [WHISKY]
		 */
		max77696_pdata.led_pdata[0].sol_brightness = LED_MED_BRIGHTNESS;
		max77696_pdata.led_pdata[1].sol_brightness = LED_MED_BRIGHTNESS;

		/* Set LED to manual mode */
		max77696_pdata.led_pdata[0].manual_mode = true; 
		max77696_pdata.led_pdata[1].manual_mode = true;

		/* Set LED default state to OFF */
		max77696_pdata.led_pdata[0].init_state = LED_FLASHD_OFF; 
		max77696_pdata.led_pdata[1].init_state = LED_FLASHD_OFF;

		/* set FL default state */
		max77696_pdata.bl_pdata.brightness = DUET_FL_LEVEL12_MID;
	}

	if(lab126_board_is(BOARD_ID_BOURBON_WFO) ||
		lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C) ||
		lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2) ) {
		/*
		 * JEITA Charge termination voltage = 4.1V [BOURBON]
		 */
		max77696_pdata.chg_pdata.cv_jta_mV = 4100;

		/*
		 * Set LED sign-of-life brightness to 0x1F [BOURBON]
		 */
		max77696_pdata.led_pdata[0].sol_brightness = LED_MED_BRIGHTNESS;
		max77696_pdata.led_pdata[1].sol_brightness = LED_MED_BRIGHTNESS;
	}

	if (lab126_board_is(BOARD_ID_WARIO) ||
		(lab126_board_is(BOARD_ID_WOODY)) ) {
		/*
		 * Charge Top-off current threshold = 50mA
		 */
		max77696_pdata.chg_pdata.to_ith = 50000;
		max77696_pdata.chg_pdata.chg_dc_lpm = true;	/* enable chga dc-dc low power mode */
	}

	/* configure PMIC GPIO for boost control instead of SOC */
	if (lab126_board_rev_greater(BOARD_ID_WHISKY_WAN_HVT1) || lab126_board_rev_greater(BOARD_ID_WHISKY_WFO_HVT1) ||
		lab126_board_rev_greater_eq(BOARD_ID_WOODY_2)) {
		max77696_pdata.gpio_pdata.init_data[1].alter_mode = MAX77696_GPIO_AME_STDGPIO;
		max77696_pdata.gpio_pdata.init_data[1].direction = MAX77696_GPIO_DIR_OUTPUT;
		max77696_pdata.gpio_pdata.init_data[1].u.output.out_cfg = MAX77696_GPIO_OUTCFG_PUSHPULL;
		max77696_pdata.gpio_pdata.init_data[1].u.output.drive = MAX77696_GPIO_DO_LO;
		/*
		 * GPIO0 is not used and so should be programmed as it is when it first powers-on: Input, pull-down.
		 * VCCGPIO24 is not powered because we don't use GPIO2, GPIO3 and GPIO4 in the Whisky design. 
		**/	
		max77696_pdata.gpio_pdata.init_data[0].pulldn_en = 1;
#ifdef CONFIG_POWER_SODA
		mx6sl_wario_soda_platform_data.boost_ctrl_gpio = MAX77696_GPIO_1;
#endif
	}

	imx6q_add_imx_snvs_rtc();

	imx6q_add_imx_i2c(0, &mx6_arm2_i2c0_data);
	
	if (lab126_board_is(BOARD_ID_ICEWINE_WARIO_512) || lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO_512) )
		imx6q_add_imx_i2c(1, &mx6_arm2_i2c1_data_icewine);
	else
		imx6q_add_imx_i2c(1, &mx6_arm2_i2c1_data);
		
	i2c_register_board_info(0, mxc_i2c0_board_info,
			ARRAY_SIZE(mxc_i2c0_board_info));
	i2c_register_board_info(1, mxc_i2c1_board_info,
			ARRAY_SIZE(mxc_i2c1_board_info));
	imx6q_add_imx_i2c(2, &mx6_arm2_i2c2_data);
	i2c_register_board_info(2, mxc_i2c2_board_info,
			ARRAY_SIZE(mxc_i2c2_board_info));

	mx6_arm2_init_uart();
#ifdef CONFIG_CYPRESS_CYTTSP4_BUS
	if((!lab126_board_is(BOARD_ID_BOURBON_WFO)) &&
		(!lab126_board_is(BOARD_ID_WARIO_4_256M_CFG_C)) &&
		(!lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2)) )
		mx6sl_cyttsp4_init();
#endif
	imx6q_add_pm_imx(0, &mx6_wario_pm_data);

#ifndef CONFIG_FALCON_WRAPPER
	imx6q_add_sdhci_usdhc_imx(1, &mx6_arm2_sd2_data);
#endif
	/* Make sure the wifi chip is off on boot */
#ifdef CONFIG_MX6SL_WARIO_BASE
	gpio_request(MX6_WARIO_WIFI_PWD, "wifi_pwr");
	gpio_wifi_power_enable(0);
#endif
	wifi_pdev = imx6q_add_sdhci_usdhc_imx(2, &mx6_arm2_sd3_data);

#ifdef CONFIG_WARIO_SDCARD
	imx6q_add_sdhci_usdhc_imx(0, &mx6_arm2_sd1_data);
#endif
	mx6_arm2_init_usb();

	imx6q_add_mxc_pwm(0);
	imx6q_add_mxc_pwm_backlight(0, &mx6_arm2_pwm_backlight_data);

	imx6dl_add_imx_pxp();
	imx6dl_add_imx_pxp_client();
	imx6dl_add_imx_epdc(&epdc_data);
#if defined(CONFIG_FB_MXC_SIPIX_PANEL)
	setup_spdc();
	imx6sl_add_imx_spdc(&spdc_data);
#endif
	imx6q_add_dvfs_core(&mx6sl_arm2_dvfscore_data);
#ifdef CONFIG_WARIO_HALL
	mxc_register_device(&mx6sl_wario_hall_device, &mx6sl_wario_hall_platform_data);
#endif

#ifdef CONFIG_POWER_SODA
	mxc_register_device(&mx6sl_wario_soda_device, &mx6sl_wario_soda_platform_data);
#endif

	imx6q_add_imx2_wdt(0, NULL);
	imx6q_add_busfreq();
	imx6q_add_ecspi(0, &mx6_wario_spi_data);
	spi_register_board_info(panel_flash_device,
			ARRAY_SIZE(panel_flash_device));

	wan_gpio_init();

	/*
	 * uboot already setup the pins, here just request the gpio by name to make kernel happy
	 * */
	if(HW_SUPPORT_EMMC_POWER_GATE)
		init_emmc_power_gate_pins();


#ifdef DEVELOPMENT_MODE
	wario_debug_toggle_pin_init();
#endif

#if defined(CONFIG_MX6SL_WARIO_WOODY)
	if (lab126_board_is(BOARD_ID_WHISKY_WFO) ||
		lab126_board_is(BOARD_ID_WHISKY_WAN) ||
		lab126_board_is(BOARD_ID_WOODY)) {
		/* Initialise and turn-off Broadcom BT */
		if(brcm_gpio_wifi_bt_init()) {
			printk(KERN_DEBUG "BRCM chip Wifi+BT GPIO request failed!!");
			return;
		}
		/* Keep Wifi + BT Disabled */
		brcm_gpio_wifi_power_enable(0);
		brcm_gpio_bt_power_enable(0);
		
		mdelay(100);
		
		/* register wifi platform data */
		platform_device_register(&bcm_wifi_device);
		/* register Bluetooth power ctrl platform device */
		mxc_register_device(&wario_bt_pwr_device, &brcm_btpwr_data);
	}
#endif
}

extern void __iomem *twd_base;
static void __init mx6_timer_init(void)
{
	struct clk *uart_clk;
#ifdef CONFIG_LOCAL_TIMERS
	twd_base = ioremap(LOCAL_TWD_ADDR, SZ_256);
	BUG_ON(!twd_base);
#endif
	mx6sl_clocks_init(32768, 24000000, 0, 0);

	uart_clk = clk_get_sys("imx-uart.0", NULL);
	early_console_setup(UART1_BASE_ADDR, uart_clk);
}

static struct sys_timer mxc_timer = {
	.init   = mx6_timer_init,
};

static void __init mx6_wario_reserve(void)
{
#ifdef CONFIG_FB_MXC_EINK_WORK_BUFFER_RESERVED
	if(!memblock_is_region_memory(CONFIG_FB_MXC_EINK_WORK_BUFFER_ADDR, CONFIG_FB_MXC_EINK_WORK_BUFFER_SIZE)) {
		printk(KERN_ERR "#### Error!! reserved WB area is not in a memory region!!\n");
		return;
	}

	if(memblock_is_region_reserved(CONFIG_FB_MXC_EINK_WORK_BUFFER_ADDR, CONFIG_FB_MXC_EINK_WORK_BUFFER_SIZE)) {
		printk(KERN_ERR "#### Error!! reserved WB area overlaps in-use memory region!!\n");
		return;
	}

	memblock_remove(CONFIG_FB_MXC_EINK_WORK_BUFFER_ADDR, CONFIG_FB_MXC_EINK_WORK_BUFFER_SIZE);
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_RESERVED
}

MACHINE_START(MX6SL_WARIO, "Freescale i.MX 6SoloLite based Wario Board")
.boot_params	= MX6SL_PHYS_OFFSET + 0x100,
	.map_io		= mx6_map_io,
	.init_irq	= mx6_init_irq,
	.init_machine	= mx6_wario_init,
	.timer		= &mxc_timer,
	.reserve	= mx6_wario_reserve,
	MACHINE_END
