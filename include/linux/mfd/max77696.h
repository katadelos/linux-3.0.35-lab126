/*
 * Copyright (C) 2012 Maxim Integrated Product
 * Jayden Cha <jayden.cha@maxim-ic.com>
 * Copyright 2012-2016 Amazon Technologies, Inc.
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

#ifndef __MAX77696_H__
#define __MAX77696_H__

#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/power_supply.h>
#include <linux/regulator/machine.h>

#define MAX77696_DRIVER_VERSION            "1.0"

#define MAX77696_I2C_ADDR                  (0x78>>1)

#define MAX77696_NAME                      "max77696"
#define MAX77696_CORE_NAME                 MAX77696_NAME"-core"
#define MAX77696_IRQ_NAME                  MAX77696_NAME"-irq"
#define MAX77696_TOPSYS_NAME               MAX77696_NAME"-topsys"
#define MAX77696_GPIO_NAME                 MAX77696_NAME"-gpio"
#define MAX77696_32K_NAME                  MAX77696_NAME"-32k"
#define MAX77696_WDT_NAME                  MAX77696_NAME"-wdt"
#define MAX77696_RTC_NAME                  MAX77696_NAME"-rtc"
#define MAX77696_BUCK_NAME                 MAX77696_NAME"-buck"
#define MAX77696_LDO_NAME                  MAX77696_NAME"-ldo"
#define MAX77696_LSW_NAME                  MAX77696_NAME"-lsw"
#define MAX77696_EPD_NAME                  MAX77696_NAME"-epd"
#define MAX77696_VDDQ_NAME                 MAX77696_NAME"-vddq"
#define MAX77696_LEDS_NAME                 MAX77696_NAME"-leds"
#define MAX77696_BL_NAME                   MAX77696_NAME"-bl"
#define MAX77696_ADC_NAME                  MAX77696_NAME"-adc"
#define MAX77696_UIC_NAME                  MAX77696_NAME"-uic"
#define MAX77696_GAUGE_NAME                MAX77696_NAME"-battery"
#define MAX77696_CHARGER_NAME              MAX77696_NAME"-charger"
#define MAX77696_EH_NAME                   MAX77696_NAME"-eh"
#define MAX77696_ONKEY_NAME                MAX77696_NAME"-onkey"

/* MAX77696 Module ID */
#define MAX77696_CORE_MID                  0
#define MAX77696_IRQ_MID                   MAX77696_CORE_MID
#define MAX77696_TOPSYS_MID                MAX77696_CORE_MID
#define MAX77696_GPIO_MID                  MAX77696_CORE_MID
#define MAX77696_32K_MID                   MAX77696_CORE_MID
#define MAX77696_WDT_MID                   MAX77696_CORE_MID
#define MAX77696_RTC_MID                   1
#define MAX77696_BUCK_MID                  MAX77696_CORE_MID
#define MAX77696_LDO_MID                   MAX77696_CORE_MID
#define MAX77696_LSW_MID                   MAX77696_CORE_MID
#define MAX77696_VDDQ_MID                  MAX77696_CORE_MID
#define MAX77696_LEDS_MID                  MAX77696_CORE_MID
#define MAX77696_BL_MID                    MAX77696_CORE_MID
#define MAX77696_ADC_MID                   MAX77696_CORE_MID
#define MAX77696_UIC_MID                   2
#define MAX77696_GAUGE_MID                 3
#define MAX77696_CHARGER_MID               MAX77696_CORE_MID
#define MAX77696_EH_MID                    MAX77696_RTC_MID
#define MAX77696_ONKEY_MID                 MAX77696_CORE_MID

/* Names of power supplies
 */

/* Fuel Gauge */
#define MAX77696_PSY_BATT_NAME             MAX77696_GAUGE_NAME
/* Main Battery Charger */
#define MAX77696_PSY_CHG_NAME              MAX77696_CHARGER_NAME
/* Energy Harvester */
#define MAX77696_PSY_EH_NAME               MAX77696_EH_NAME

/* Number of interrupts to be reserved for MAX77696 */
#define MAX77696_GPIO_IRQ_OFFSET           \
	(MAX77696_ROOTINT_NR_IRQS + MAX77696_TOPSYSINT_NR_IRQS)
#define MAX77696_NR_IRQS                   \
	(MAX77696_GPIO_IRQ_OFFSET + MAX77696_GPIO_NR_GPIOS)

/* 32kHz OSC load capacitance selection */
#define MAX77696_32K_LOAD_CAP_22PF         0 /* 22pF per node */
#define MAX77696_32K_LOAD_CAP_12PF         1 /* 12pF per node */
#define MAX77696_32K_LOAD_CAP_10PF         2 /* 10pF per node */
#define MAX77696_32K_LOAD_CAP_NONE         3 /* No internal load cap selected */

/* 32kHz OSC operation mode
 *   Whenever the main battery is plugged in and VSYS > VSYSMIN, and the
 *   MODE_SEL bit is set to 1, the crystal driver goes into a high current, high
 *   accuracy state that meets the 15ns cycle by cycle jitter and 45%. 55% duty
 *   cycle spec. As soon as VSYS < VSYSMIN, the oscillator automatically goes
 *   into a low power state where it does not meet the jitter and duty cycle
 *   specification. The assumption is that when VSYS < VSYSMIN, the radio is
 *   off, and tight jitter and duty cycle spec is not required. This prevents
 *   the battery from being too deeply discharge, since the supply current is
 *   reduced.
 */
#define MAX77696_32K_MODE_LOW_POWER        0 /* Low Power */
#define MAX77696_32K_MODE_LOW_JITTER       1 /* Low Jitter */

struct max77696_32k_platform_data {
	int load_cap:2; /* 32kHz OSC load capacitance selection */
	int op_mode:1;  /* 32kHz OSC operation mode             */
};

/* Number of GPIOs */
#define MAX77696_GPIO_NR_GPIOS             5

/* GPIO alternate mode */
#define MAX77696_GPIO_AME_STDGPIO          0 /* Standard GPI or GPO */
#define MAX77696_GPIO_AME_MODE_0           1
#define MAX77696_GPIO_AME_MODE_1           2
#define MAX77696_GPIO_AME_MODE_2           3

/* GPIO direction */
#define MAX77696_GPIO_DIR_OUTPUT           0
#define MAX77696_GPIO_DIR_INPUT            1

/* GPIO output configuration */
#define MAX77696_GPIO_OUTCFG_OPENDRAIN     0
#define MAX77696_GPIO_OUTCFG_PUSHPULL      1

/* GPIO Output drive level */
#define MAX77696_GPIO_DO_LO                0
#define MAX77696_GPIO_DO_HI                1

/* Debounce Configuration */
#define MAX77696_GPIO_DBNC_0_MSEC          0
#define MAX77696_GPIO_DBNC_8_MSEC          1
#define MAX77696_GPIO_DBNC_16_MSEC         2
#define MAX77696_GPIO_DBNC_32_MSEC         3

/* Rising edge and falling edge interrupt configuration */
#define MAX77696_GPIO_REFEIRQ_NONE         0x0
#define MAX77696_GPIO_REFEIRQ_FALLING      0x1
#define MAX77696_GPIO_REFEIRQ_RISING       0x2
#define MAX77696_GPIO_REFEIRQ_BOTH         \
	(MAX77696_GPIO_REFEIRQ_FALLING|MAX77696_GPIO_REFEIRQ_RISING)

struct max77696_gpio_init_data {
	int pin_connected:1;
	int alter_mode:2;    /* GPIO Alternate Mode Enable (see datasheet for details) */
	int pullup_en:1;     /* GPIO Pull-Up Enable */
	int pulldn_en:1;     /* GPIO Pull-Down Enable */
	int direction:1;
	union {
		struct {
			int out_cfg:1;  /* Output configuration */
			int drive:1;    /* Output drive level (0:LOW / 1:HIGH) */
		} output;
		struct {
			int debounce:2; /* Debounce configuration */
			int refe_irq:2; /* Rising and falling edge interrupt configuration */
		} input;
	} u;
};

struct max77696_gpio_platform_data {
	int                            irq_base;
	int                            gpio_base;
	int                            bias_en;  /* GPIO Biasing Circuitry Enable */
	struct max77696_gpio_init_data init_data[MAX77696_GPIO_NR_GPIOS];
};

struct max77696_watchdog_platform_data {
	unsigned int timeout_sec;
	unsigned int ping_interval_sec; /* If watchdog daemon is used,
									 * then set this zero.
									 */
};

struct max77696_rtc_platform_data {
	bool irq_1m; /* RTC 1 minute interrupt */
	bool irq_1s; /* RTC 1 second interrupt */
};

/* BUCK_ Rising Slew Rate Selection */
#define MAX77696_BUCK_RISING_SLEW_RATE_12p5_MV_PER_USEC  0b00
#define MAX77696_BUCK_RISING_SLEW_RATE_25_MV_PER_USEC    0b01
#define MAX77696_BUCK_RISING_SLEW_RATE_50_MV_PER_USEC    0b10
#define MAX77696_BUCK_RISING_SLEW_RATE_NO_CONTROL        0b11

/* BUCK_ Operating Mode Selection */
#define MAX77696_BUCK_OPERATING_MODE_OFF                 0b00
                              /* Disabled. BUCK_ is OFF. */
#define MAX77696_BUCK_OPERATING_MODE_DYNAMIC_STANDBY     0b01
                              /* Dynamic Standby Mode. BUCK_ operates in Normal
                               * Mode when the Standby Bus is Low and operates
                               * in Standby Mode when the Global Low Power Mode
                               * Bus is High.
                               */
#define MAX77696_BUCK_OPERATING_MODE_FORCED_STANDBY      0b10
                              /* Forced Standby Mode. BUCK_ operates in Standby
                               * Mode regardless of the state of the Global Low
                               * Power Mode Bus.
                               */
#define MAX77696_BUCK_OPERATING_MODE_NORMAL              0b11
                              /* Normal Operation. BUCK_ operates in Normal Mode
                               * regardless of the state of the Global Low Power
                               * Mode Bus.
                               */

/* Number of Buck Regulators */
#define MAX77696_BUCK_NR_REGS          MAX77696_BUCK_ID_MAX

enum {
    MAX77696_BUCK_ID_B1 = 0,
    MAX77696_BUCK_ID_B1DVS,
    MAX77696_BUCK_ID_B2,
    MAX77696_BUCK_ID_B2DVS,
    MAX77696_BUCK_ID_B3,
    MAX77696_BUCK_ID_B4,
    MAX77696_BUCK_ID_B5,
    MAX77696_BUCK_ID_B6,
    MAX77696_BUCK_ID_MAX,
};

struct max77696_buck_platform_data {
    bool                       support_suspend_ops;
    struct regulator_init_data init_data[MAX77696_BUCK_NR_REGS];
};

/* LDOx Power Mode */
#define MAX77696_LDO_POWERMODE_OFF                0b00
                              /* Output Disabled. OUTLDOxx is off. */
#define MAX77696_LDO_POWERMODE_LOW_POWER          0b01
                              /* Group Low-Power Mode. OUTLDOxx operates in
                               * normal mode when the global low-power mode
                               * signal is low. When the global low-power mode
                               * signal is high, OUTLDOxx operates in
                               * low-power mode.
                               */
#define MAX77696_LDO_POWERMODE_FORCED_LOW_POWER   0b10
                              /* Low-Power Mode. OUTLDOxx is forced into
                               * low-power mode. The maximum load current is
                               * 5mA and the quiescent supply current is 1.5uA.
                               */
#define MAX77696_LDO_POWERMODE_NORMAL             0b11
                              /* Normal Mode. OUTLDOxx is forced into its normal
                               * operating mode.
                               */

/* LDOx Transconductance Setting */
#define MAX77696_LDO_TRANSCONDUCTANCE_FAST        0b00
#define MAX77696_LDO_TRANSCONDUCTANCE_MEDIUM_FAST 0b01
#define MAX77696_LDO_TRANSCONDUCTANCE_MEDIUM_SLOW 0b10
#define MAX77696_LDO_TRANSCONDUCTANCE_SLOW        0b11

/* LDOx Soft-Start Slew Rate Configuration
 * (Applies to both start-up and output voltage setting changes)
 */
#define MAX77696_LDO_SLEW_RATE_FAST               0 /* 100mV / usec */
#define MAX77696_LDO_SLEW_RATE_SLOW               1 /*   5mV / usec */

/* LDO Current Monitor Operating Mode */
#define MAX77696_LDO_OPERATING_MODE_AUTONOMOUS    0
#define MAX77696_LDO_OPERATING_MODE_MANUAL_OFF    1
#define MAX77696_LDO_OPERATING_MODE_MANUAL_ON     2

/* Number of LDO Regulators */
#define MAX77696_LDO_NR_REGS           MAX77696_LDO_ID_MAX

enum {
    MAX77696_LDO_ID_L1 = 0,
    MAX77696_LDO_ID_L2,
    MAX77696_LDO_ID_L3,
    MAX77696_LDO_ID_L4,
    MAX77696_LDO_ID_L5,
    MAX77696_LDO_ID_L6,
    MAX77696_LDO_ID_L7,
    MAX77696_LDO_ID_L8,
    MAX77696_LDO_ID_L9,
    MAX77696_LDO_ID_L10,
    MAX77696_LDO_ID_MAX,
};

/* LDO Current Monitor Transfer Function. L_IMON_TF adjusts the monitor's
 * transfer function. This adjustment capability is very similar to the "range"
 * function on a digital multi-meter (DMM).
 */
#define MAX77696_LDO_IMON_TF_8000_OHM 0b000
#define MAX77696_LDO_IMON_TF_4000_OHM 0b001
#define MAX77696_LDO_IMON_TF_2000_OHM 0b010
#define MAX77696_LDO_IMON_TF_1000_OHM 0b011 /* device default */
#define MAX77696_LDO_IMON_TF_500_OHM  0b100
#define MAX77696_LDO_IMON_TF_250_OHM  0b101
#define MAX77696_LDO_IMON_TF_125_OHM  0b110
#define MAX77696_LDO_IMON_TF_62p5_OHM 0b111

struct max77696_ldo_platform_data {
    u8                         imon_tf; /* Current Monitor Transfer Function */
    struct regulator_init_data init_data[MAX77696_LDO_NR_REGS];
};

/* Load Switches */
#define MAX77696_LSW_NR_REGS           MAX77696_LSW_ID_MAX

enum {
    MAX77696_LSW_ID_LSW1 = 0,
    MAX77696_LSW_ID_LSW2,
    MAX77696_LSW_ID_LSW3,
    MAX77696_LSW_ID_LSW4,
    MAX77696_LSW_ID_MAX,
};

struct max77696_lsw_platform_data {
    struct regulator_init_data init_data[MAX77696_LSW_NR_REGS];
    u8 ade[MAX77696_LSW_NR_REGS];
};

/* EPD Power Supplies */
#define MAX77696_EPD_NR_REGS           MAX77696_EPD_ID_MAX

/* EPD */
enum {
	MAX77696_EPD_ID_EPDEN = 0,
	MAX77696_EPD_ID_EPDVCOM,
	MAX77696_EPD_ID_EPDVEE,
	MAX77696_EPD_ID_EPDVNEG,
	MAX77696_EPD_ID_EPDVPOS,
	MAX77696_EPD_ID_EPDVDDH,
	MAX77696_EPD_ID_MAX,
};

struct max77696_epd_platform_data {
	struct regulator_init_data *init_data;
	int irq;
	int pwrgood_irq;
	int pwrgood_gpio;
};

/* LPDDR2 Termination Supply */
struct max77696_vddq_platform_data {
	int                        vddq_in_uV;
	struct regulator_init_data init_data;
};

enum {
	MAX77696_LED_GREEN = 0,
	MAX77696_LED_AMBER,

	MAX77696_LED_NR_LEDS,
};

enum {
	MAX77696_LED_OFF = 0,
	MAX77696_LED_ON = 1,
	MAX77696_LED_BLINK = 2,
};

#define LED_MAX_BRIGHTNESS          127
#define LED_DEF_BRIGHTNESS          63
#define LED_MED_BRIGHTNESS          31

#define LED_FLASHD_OFF 0x0 /* off */
#define LED_FLASHP_OFF 0xF /* on indefinitely */

#define LED_FLASHD_ON  0xF /* 640ms */
#define LED_FLASHP_ON  0x0 /* 640ms */

#define LED_FLASHD_DEF 0xD /* 512ms */
#define LED_FLASHP_DEF 0x1 /* 960ms */

#define LED_FLASHD_BLINK  0xA /* 320ms */
#define LED_FLASHP_BLINK  0x0 /* 640ms */

#if defined(CONFIG_POWER_SODA)
/* SOC battery events related defines
 * Applicable only for Whisky dual-battery
 * architecture */
#define SOCBATT_EVTHDL_DELAY 		500 /* 500ms delay for SOC battery Hi
				             * and Low events handler run */
#define IGNORE_SOCBATT_TRIGGER	   	0x11
#define SOCBATT_MINSUBSCRIBE_TRIGGER    0x22
#define SOCBATT_MAXSUBSCRIBE_TRIGGER   	0x33

enum socbattevt_subscription_st {
	SOCBATT_MIN_SUBSCRIBED = 0x0,
	SOCBATT_MAX_SUBSCRIBED,
	UNSUBSCRIBED_ALL,
};

/* Battery events structure for primary SOC Battery
 * (applicable for Whisky/Duet archictecture) */
struct max77696_socbatt_eventdata {
	struct delayed_work socbatt_event_work;
	atomic_t override;
	atomic_t subscribe_state;
};
#endif

struct max77696_led_platform_data {
	struct led_info info;
	bool manual_mode;
	u8 sol_brightness;
	u8 init_state;
};

struct max77696_backlight_platform_data {
	int brightness;
};

/* Analog-to-Digital Converter */
#define MAX77696_ADC_NR_CHS  MAX77696_ADC_CH_MAX

enum {
    MAX77696_ADC_CH_VSYS2 = 0,
    MAX77696_ADC_CH_TDIE,
    MAX77696_ADC_CH_VSYS1,
    MAX77696_ADC_CH_VCHGINA,
    MAX77696_ADC_CH_ICHGINA,
    MAX77696_ADC_CH_IMONL1,
    MAX77696_ADC_CH_IMONL2,
    MAX77696_ADC_CH_IMONL3,
    MAX77696_ADC_CH_IMONL4,
    MAX77696_ADC_CH_IMONL5,
    MAX77696_ADC_CH_IMONL6,
    MAX77696_ADC_CH_IMONL7,
    MAX77696_ADC_CH_IMONL8,
    MAX77696_ADC_CH_IMONL9,
    MAX77696_ADC_CH_IMONL10,
    MAX77696_ADC_CH_IMONB1,
    MAX77696_ADC_CH_IMONB2,
    MAX77696_ADC_CH_IMONB3,
    MAX77696_ADC_CH_IMONB4,
    MAX77696_ADC_CH_IMONB5,
    MAX77696_ADC_CH_IMONB6,
    MAX77696_ADC_CH_AIN0,
    MAX77696_ADC_CH_AIN1,
    MAX77696_ADC_CH_AIN2,
    MAX77696_ADC_CH_AIN3,
    MAX77696_ADC_CH_MAX,
};

struct max77696_adc_platform_data {
	u8  avg_rate;  /* average rate: 1, 2, 16, 32 samples */
	u16 adc_delay; /* up to 16500 nano-seconds */
	u8  current_src; /* current source: 0, 5, 10, 50uA */
};

/* Gas guage */
#define MAX77696_GAUGE_POLLING_CAPACITY             BIT(15)
#define MAX77696_GAUGE_POLLING_TEMP                 BIT(14)
#define MAX77696_GAUGE_POLLING_VOLTAGE              BIT(13)
#define MAX77696_GAUGE_POLLING_CURRENT_AVG          BIT(12)
#define MAX77696_GAUGE_POLLING_NAC                  BIT(11)
#define MAX77696_GAUGE_POLLING_LMD                  BIT(10)
#define MAX77696_GAUGE_POLLING_CYCLE                BIT(9)
#define MAX77696_GAUGE_POLLING_NAC_AVG              BIT(8)
#define MAX77696_GAUGE_POLLING_NAC_MIX              BIT(7)
#define MAX77696_GAUGE_POLLING_CAPACITY_AVG         BIT(6)
#define MAX77696_GAUGE_POLLING_CAPACITY_MIX         BIT(5)
#define MAX77696_GAUGE_CHARACTERIZATION_DATA_SIZE   48

enum {
	MAX77696_GAUGE_LEARNED_FULLCAP = 0,
	MAX77696_GAUGE_LEARNED_CYCLES,
	MAX77696_GAUGE_LEARNED_RCOMP0,
	MAX77696_GAUGE_LEARNED_TEMPCO,
	MAX77696_GAUGE_LEARNED_QRESIDUAL00,
	MAX77696_GAUGE_LEARNED_QRESIDUAL10,
	MAX77696_GAUGE_LEARNED_QRESIDUAL20,
	MAX77696_GAUGE_LEARNED_QRESIDUAL30,
	MAX77696_GAUGE_LEARNED_DQACC,
	MAX77696_GAUGE_LEARNED_DPACC,

	MAX77696_GAUGE_LEARNED_NR_INFOS,
};

/*
 * used for setting a register to a desired value
 * addr : address for a register
 * data : setting value for the register
 */
struct max77696_gauge_reg_data {
	u8  addr;
	u16 data;
};

struct max77696_gauge_led_control {
	bool                manual_mode;
	enum led_brightness brightness;
	bool                flashing;
	struct {
		unsigned long duration_ms;
		unsigned long period_ms;
	} flash_params;
};

struct max77696_gauge_led_trigger {
	bool                              enable;
	struct max77696_gauge_led_control control[MAX77696_LED_NR_LEDS];
};

struct max77696_gauge_platform_data {
	/* The Alert Threshold values
	 *   - Over-/undervoltage       VALRT threshold violation (upper or lower)
	 *   - Over-/undertemperature   TALRT threshold violation (upper or lower)
	 *   - Over/under SOC           SALRT threshold violation (upper or lower)
	 * (the alert will be disabled if max and min are same)
	 */
	int                                v_alert_max, v_alert_min; /* 5100 ~    0 [mV] */
	int                                t_alert_max, t_alert_min; /*  127 ~ -128 [C] */
	int                                s_alert_max, s_alert_min; /*  255 ~    0 [%] */
	bool                               enable_alert_on_battery_removal;
	bool                               enable_alert_on_battery_insertion;
	bool                               enable_current_sense;
	bool                               enable_por_init;

	int                                charge_full_design;    /* in uAh */
	int                                battery_full_capacity; /* in percent */

	/*
	 * R_sns in micro-ohms.
	 * default 10000 (if r_sns = 0) as it is the recommended value by
	 * the datasheet although it can be changed by board designers.
	 */
	unsigned int                       r_sns;

	unsigned int                       update_interval_ms; /* in msec */
	unsigned int                       update_interval_relax_ms; /* in msec */
	/* ORed MAX77696_GAUGE_POLL_... */
	u16                                polling_properties;

	struct max77696_gauge_reg_data    *init_data;
	int                                num_init_data;

	struct max77696_gauge_led_trigger  default_trigger;
	struct max77696_gauge_led_trigger  charging_trigger;
	struct max77696_gauge_led_trigger  charging_full_trigger;

	bool                               (*battery_online) (void);
	bool                               (*charger_online) (void);
	bool                               (*charger_enable) (void);
};

struct max77696_gauge_config_data {
	/* A/D measurement */
	u16	tgain;		/* 0x2C */
	u16	toff;		/* 0x2D */
	u16	cgain;		/* 0x2E */
	u16	coff;		/* 0x2F */

	/* Alert / Status */
	u16	valrt_thresh;	/* 0x01 */
	u16	talrt_thresh;	/* 0x02 */
	u16	soc_alrt_thresh;	/* 0x03 */
	u16	config;		/* 0x01D */
	u16	shdntimer;	/* 0x03F */

	/* App data */
	u16	full_soc_thresh;	/* 0x13 */
	u16	design_cap;	/* 0x18 */
	u16	ichgt_term;	/* 0x1E */

	/* MG3 config */
	u16	at_rate;	/* 0x04 */
	u16	learn_cfg;	/* 0x28 */
	u16	filter_cfg;	/* 0x29 */
	u16	relax_cfg;	/* 0x2A */
	u16	misc_cfg;	/* 0x2B */
	u16	masksoc;	/* 0x32 */

	/* MG3 save and restore */
	u16	fullcap;	/* 0x10 */
	u16	fullcapnom;	/* 0x23 */
	u16	socempty;	/* 0x33 */
	u16	lavg_empty;	/* 0x36 */
	u16	dqacc;		/* 0x45 */
	u16	dpacc;		/* 0x46 */
	u16	qrtbl00;	/* 0x12 */
	u16	qrtbl10;	/* 0x22 */
	u16	qrtbl20;	/* 0x32 */
	u16	qrtbl30;	/* 0x42 */

	/* Cell Data */
	u16	vempty;		/* 0x12 */
	u16	temp_nom;	/* 0x24 */
	u16	temp_lim;	/* 0x25 */
	u16	fctc;		/* 0x37 */
	u16	rcomp0;		/* 0x38 */
	u16	tcompc0;	/* 0x39 */
	u16	empty_tempco;	/* 0x3A */
	u16	kempty0;	/* 0x3B */
	u16 cycles;
	u16 batt_cap;
	u16	cell_char_tbl[MAX77696_GAUGE_CHARACTERIZATION_DATA_SIZE];
} __packed;

/* USB Charger Detection */
#define MAX77696_UIC_CHGTYPE_USB             0b0001 /* USB Cable */
#define MAX77696_UIC_CHGTYPE_CDP             0b0010 /* Charging Downstream Port */
#define MAX77696_UIC_CHGTYPE_DEDICATED_1P5A  0b0011 /* Dedicated Charger: current 1500mA max */
#define MAX77696_UIC_CHGTYPE_APPLE_0P5AC     0b0100 /* Apple Charger: current  500mA max */
#define MAX77696_UIC_CHGTYPE_APPLE_1P0AC     0b0101 /* Apple Charger: current 1500mA max */
#define MAX77696_UIC_CHGTYPE_APPLE_2P0AC     0b0110 /* Apple Charger: current 2000mA max */
#define MAX77696_UIC_CHGTYPE_OTH_0           0b0111 /* Other Charger */
#define MAX77696_UIC_CHGTYPE_SELFENUM_0P5AC  0b1001 /* Self Enumerated: current 500mA max */
#define MAX77696_UIC_CHGTYPE_OTH_1           0b1100 /* Other Charger */

#define MAX77696_UIC_ISET_0mA                0b000
#define MAX77696_UIC_ISET_0P1A               0b001
#define MAX77696_UIC_ISET_0P4A               0b010
#define MAX77696_UIC_ISET_0P5A               0b011
#define MAX77696_UIC_ISET_1A                 0b100
#define MAX77696_UIC_ISET_1P5A               0b101
#define MAX77696_UIC_ISET_2A                 0b110
#define MAX77696_UIC_ISET_RFU                0b111

struct max77696_uic_notify {
	u8 vb_volt;
	u8 chg_type;
	u8 adc_code;
	u8 i_set;
};

struct max77696_uic_platform_data {
	void (*uic_notify) (const struct max77696_uic_notify *noti);
};

#define MAX77696_CHARGER_MODE_OFF     0b0000
/* When the DC to DC is set for buck converter
 * operation, setting this bit will turn on the charger. When the DC to DC is
 * set for boost converter operation, setting this bit will turn on the SYS2 to
 * CHGINA switch for OTG style operation
 */
#define MAX77696_CHARGER_MODE_CHG         0b0101
#define MAX77696_CHARGER_MODE_OTG         0b1010
/* Enable the DC to DC converter as a buck converter */
#define MAX77696_CHARGER_MODE_OFF_ENBUCK  0b0100
/* Enable the DC to DC converter as a boost converter */
#define MAX77696_CHARGER_MODE_OFF_ENBOOST 0b1000

/* CHARGER-A Input current limits */
#define MAX77696_CHARGER_A_ICL_ILIM_0P08A   0x84
#define MAX77696_CHARGER_A_ICL_ILIM_0P16A   0x88
#define MAX77696_CHARGER_A_ICL_ILIM_0P5A    0x99

#define CHARGER_CTV_4P1V            4100
#define CHARGER_CTV_4P2V            4200

struct max77696_charger_platform_data {
	char **batteries;
	int    num_batteries;

	u8     initial_mode;
	u8     icl_ilim;
	int    wdt_period_ms; /* Watchdog Timer Period:   80,000 msec (fixed)
						   * Watchdog Timer Accuracy: -20 ~ +20 %
						   * (To disable wdt, set zero)
						   */

	int    cc_uA, cv_prm_mV, cv_jta_mV;
	int    to_time, to_ith;
	int    t1_C, t2_C, t3_C, t4_C;
	int    fast_chg_time;
	bool   chg_dc_lpm;
	bool   wakeup_irq;

	void   (*charger_notify) (struct power_supply *psy,
			bool online, bool enable);
};

#define MAX77696_EH_MODE_SHUTDOWN   0b00
#define MAX77696_EH_MODE_CHARGER    0b01
#define MAX77696_EH_MODE_ACCESSORY  0b10
#define MAX77696_EH_MODE_AUTODETECT 0b11

struct max77696_eh_platform_data {
	char         **batteries;
	int            num_batteries;

	u8             initial_mode;
	unsigned int   detect_interval_ms;

	unsigned int   acc_det_gpio;
	int            acc_det_gpio_assert; /* HIGH(1) or LOW (0) */
	unsigned int   acc_det_debounce_ms;
	int            acc_ilimit;

	void           (*charger_notify) (struct power_supply *psy,
			bool online, bool enable);
	void           (*accessory_notify) (struct power_supply *psy,
			bool present);
};

struct max77696_onkey_platform_data {
	bool         wakeup_1sec_delayed_since_onkey_down;
	bool         wakeup_after_mro;   /* wakeup after manual reset
									  * 0 = a manual reset results in a power
									  *     down to the shutdown state
									  * 1 = a manual reset results in a power
									  * cycle and the AP active state
									  */
	int          manual_reset_time;  /* 2s ... 12s */
	bool         wakeup_after_mrwrn; /* wakeup after manual reset warning */

	unsigned int onkey_keycode;      /* Keycode for DBEN0 */
	unsigned int hold_1sec_keycode;  /* Keycode for EN0_1SEC */
	unsigned int mr_warn_keycode;    /* Keycode for MRWRN */
};


/*** MAX77696 PLATFORM DATA STRUCTURES ***/
struct max77696_platform_data {
	unsigned int                            core_irq;
	unsigned long                           core_irq_trigger;

	unsigned int                            irq_base;

	/* chip options */
	bool                                    core_debug;
	bool                                    vreg_supports_bucks_suspend_ops;
	bool                                    vreg_supports_dynamic_standby_mode;
	bool                                    vreg_supports_vcom_enable_control;
	bool                                    gauge_supports_led_triggers;

	struct max77696_32k_platform_data       osc_pdata;
	struct max77696_gpio_platform_data      gpio_pdata;
	struct max77696_watchdog_platform_data  wdt_pdata;
	struct max77696_rtc_platform_data       rtc_pdata;
	struct max77696_buck_platform_data      buck_pdata;
	struct max77696_ldo_platform_data       ldo_pdata;
	struct max77696_lsw_platform_data       lsw_pdata;
	struct max77696_epd_platform_data       epd_pdata;
	struct max77696_vddq_platform_data      vddq_pdata;
	struct max77696_led_platform_data       led_pdata[MAX77696_LED_NR_LEDS];
	struct max77696_backlight_platform_data bl_pdata;
	struct max77696_adc_platform_data       adc_pdata;
	struct max77696_gauge_platform_data     gauge_pdata;
	struct max77696_uic_platform_data       uic_pdata;
	struct max77696_charger_platform_data   chg_pdata;
	struct max77696_eh_platform_data        eh_pdata;
	struct max77696_onkey_platform_data     onkey_pdata;
};

struct max77696_i2c {
	struct i2c_client *client;

	int (*read) (struct max77696_i2c *me, u8 addr, u8 *val);
	int (*write) (struct max77696_i2c *me, u8 addr, u8 val);
	int (*bulk_read) (struct max77696_i2c *me, u8 addr, u8 *dst, u16 len);
	int (*bulk_write) (struct max77696_i2c *me, u8 addr, const u8 *src, u16 len);
};

struct max77696_chip {
	struct device                 *dev;
	struct kobject                *kobj;

	struct max77696_i2c            pmic_i2c;
	struct max77696_i2c            rtc_i2c;
	struct max77696_i2c            uic_i2c;
	struct max77696_i2c            gauge_i2c;

	unsigned int                   core_irq;
	unsigned int                   irq_base;
	struct mutex                   irq_lock;

	void                          *irq_ptr;
	void                          *topsys_ptr;
	void                          *led_ptr[MAX77696_LED_NR_LEDS];
	void                          *buck_ptr;
	void                          *ldo_ptr;
	void                          *adc_ptr;
	void                          *gauge_ptr;
	void                          *chg_ptr;
	void                          *eh_ptr;
	void                          *epd_ptr;

	volatile unsigned long        initialized_subdevices;
};

enum {
	max77696_32k_subdevice,

#ifdef CONFIG_GPIO_MAX77696
	max77696_gpio_subdevice,
#endif

#ifdef CONFIG_WATCHDOG_MAX77696
	max77696_wdt_subdevice,
#endif

#ifdef CONFIG_RTC_DRV_MAX77696
	max77696_rtc_subdevice,
#endif

#ifdef CONFIG_REGULATOR_MAX77696
	max77696_buck_subdevice,
	max77696_ldo_subdevice,
	max77696_lsw_subdevice,
	max77696_epd_subdevice,
	max77696_vddq_subdevice,
#endif

#ifdef CONFIG_LEDS_MAX77696
	max77696_led0_subdevice,
	max77696_led1_subdevice,
#endif

#ifdef CONFIG_BACKLIGHT_MAX77696
	max77696_bl_subdevice,
#endif

#ifdef CONFIG_SENSORS_MAX77696
	max77696_adc_subdevice,
#endif

#ifdef CONFIG_BATTERY_MAX77696
	max77696_gauge_subdevice,
#endif

#ifdef CONFIG_CHARGER_MAX77696
	max77696_uic_subdevice,
	max77696_charger_subdevice,
	max77696_eh_subdevice,
#endif

#ifdef CONFIG_INPUT_MAX77696_ONKEY
	max77696_onkey_subdevice,
#endif

	max77696_num_subdevices
};

static __always_inline void max77696_num_subdevices_check(void)
{
	/* Each sub devices' loaded state is reflected in a bit in initialized_subdevices, so the total must be less than
	 * the length of a long. */
	BUILD_BUG_ON(max77696_num_subdevices > BITS_PER_LONG);
}

#define SUBDEVICE_SET_LOADED(devname, chipptr) set_bit(max77696_##devname##_subdevice, &((chipptr)->initialized_subdevices))
#define IS_SUBDEVICE_LOADED(devname, chipptr) test_bit(max77696_##devname##_subdevice, &((chipptr)->initialized_subdevices))

/* ### EXTERNAL APIS ### */

#define pmic_read   max77696_chip_read
#define pmic_write  max77696_chip_write

/* @CORE */
extern int max77696_chip_read (u8 module, u8 addr, u16 *data);
extern int max77696_chip_write (u8 module, u8 addr, u16 data);
extern int max77696_chip_set_wakeup (struct device *dev, bool enable);
extern bool max77696_battery_online_def_cb (void);
extern bool max77696_charger_online_def_cb (void);
extern bool max77696_charger_enable_def_cb (void);
extern void max77696_uic_notify_def_cb (const struct max77696_uic_notify *noti);
extern void max77696_charger_notify_def_cb (struct power_supply *psy,
		bool online, bool enable);
extern void max77696_eh_notify_def_cb (struct power_supply *psy,
		bool online, bool enable);

/* @TOPSYS */
extern int max77696_topsys_set_global_lp_mode (bool level);
extern int max77696_topsys_enable_mr (bool enable);
extern int max77696_topsys_set_mr_time (unsigned int seconds);
extern int max77696_topsys_enable_en0_delay (bool enable);
extern int max77696_topsys_enable_standy (bool enable);
extern int max77696_topsys_enable_mr_wakeup (bool enable);
extern int max77696_topsys_enable_wdt (bool enable);
extern int max77696_topsys_set_wdt_period (unsigned int seconds);
extern int max77696_topsys_enable_rtc_wakeup (bool enable);
extern int max77696_topsys_enable_wdt_wakeup (bool enable);
extern int max77696_topsys_enable_uic_edge_wakeup (bool enable);
extern int max77696_topsys_set_lbhyst (unsigned int mV);
extern int max77696_topsys_set_lbdac (unsigned int mV);
extern int max77696_topsys_clear_wdt (void);
extern int max77696_topsys_set_rso_delay (unsigned int time_us);

/* @IRQ */
extern bool max77696_irq_test_ercflag (u16 flag);
extern void max77696_irq_enable_fgirq (u8 irq_bits, bool forced);
extern void max77696_irq_disable_fgirq (u8 irq_bits, bool forced);
extern int max77696_irq_read_fgirq_status (u8 *status);

/* @LED */
extern void max77696_led_update_changes (unsigned int led_id);
extern void max77696_led_enable_manual_mode (unsigned int led_id,
		bool manual_mode);
extern void max77696_led_set_brightness (unsigned int led_id,
		enum led_brightness value);
extern void max77696_led_set_blink (unsigned int led_id,
		unsigned long duration_ms, unsigned long period_ms);
extern void max77696_led_disable_blink (unsigned int led_id);

/* @LDO */
extern int max77696_ldo_set_bias_enable (bool enable);
extern int max77696_ldo_get_bias_enable (void);

/* @ADC */
extern int max77696_adc_read (u8 channel, u16 *data);
#define max77696_adc_read_buck_current(buck, data_ptr) \
        max77696_adc_read(MAX77696_ADC_CH_IMONB##buck, data_ptr)
#define max77696_adc_read_ldo_current(ldo, data_ptr) \
        max77696_adc_read(MAX77696_ADC_CH_IMONL##ldo, data_ptr)

/* @GAUGE */
extern void max77696_gauge_write_learned (int id, u16 learned);
extern void max77696_gauge_read_learned (int id, u16 *learned);

/* @CHARGER */
extern int max77696_charger_set_mode (int mode);
extern int max77696_charger_get_mode (int *mode);
extern int max77696_charger_set_pq_en (int enable);
extern int max77696_charger_get_pq_en (int *enable);
extern int max77696_charger_set_cc_level (int uA);
extern int max77696_charger_get_cc_level (int *uA);
extern int max77696_charger_set_jeita_en (int enable);
extern int max77696_charger_get_jeita_en (int *enable);
extern int max77696_charger_set_cv_level (bool jeita, int mV);
extern int max77696_charger_get_cv_level (bool jeita, int *mV);
extern int max77696_charger_set_temp_thres (int t_id, int temp_C);
extern int max77696_charger_get_temp_thres (int t_id, int *temp_C);
extern int max77696_charger_set_wdt_period (int period_ms);
extern int max77696_charger_get_wdt_period (int *period_ms);

/* @EH */
extern int max77696_eh_set_mode (int mode);
extern int max77696_eh_get_mode (int *mode);
extern int max77696_eh_set_accessory_i_limit (int mA);
extern int max77696_eh_get_accessory_i_limit (int *mA);
extern int max77696_eh_set_cv_level (int mV);
extern int max77696_eh_get_cv_level (int *mV);
extern int max77696_eh_set_cc_level (bool pqsel, int mA);
extern int max77696_eh_get_cc_level (bool *pqsel, int *mA);

/* ### INTERNAL USE ONLY ### */

#define MAX77696_IRQ_BASE     150
#define MAX77696_GPIO_BASE    250

/* PMIC root interrupt definitions */
enum {
	MAX77696_ROOTINT_TOPSYS    =  0,
	MAX77696_ROOTINT_BUCK,    /*  1 */
	MAX77696_ROOTINT_FG,      /*  2 */
	MAX77696_ROOTINT_GPIO,    /*  3 */
	MAX77696_ROOTINT_RTC,     /*  4 */
	MAX77696_ROOTINT_CHGA,    /*  5 */
	MAX77696_ROOTINT_LDO,     /*  6 */
	MAX77696_ROOTINT_UIC,     /*  7 */
	MAX77696_ROOTINT_ADC,     /*  8 */
	MAX77696_ROOTINT_WLED,    /*  9 */
	MAX77696_ROOTINT_EPD,     /* 10 */
	MAX77696_ROOTINT_CHGB,    /* 11 */

	MAX77696_ROOTINT_NR_IRQS,
	MAX77696_ROOTINT_SOFTEVT, /* NR_IRQS +1, this needs to be out of range,
	                           * otherwise the PMIC irq chip code will consider
	                           * this interrupt as firing for every real interrupt */
};

/* PMIC topsys interrupt definitions */
enum {
	MAX77696_TOPSYSINT_THERM_ALARM_1   = 0,
	MAX77696_TOPSYSINT_THERM_ALARM_0, /* 1 */
	MAX77696_TOPSYSINT_BATT_LOW,      /* 2 */
	MAX77696_TOPSYSINT_MR_WARNING,    /* 3 */
	MAX77696_TOPSYSINT_EN0_1SEC,      /* 4 */
	MAX77696_TOPSYSINT_EN0_FALLING,   /* 5 */
	MAX77696_TOPSYSINT_EN0_RISING,    /* 6 */

	MAX77696_TOPSYSINT_NR_IRQS,
};

enum {
	MAX77696_GPIO_0 = MAX77696_GPIO_BASE,
	MAX77696_GPIO_1,
	MAX77696_GPIO_2,
	MAX77696_GPIO_3,
	MAX77696_GPIO_4,
};

/* PMIC fault interrupt definitions */
#define MAX77696_EPDINTS_VCOMFLTS        BIT(6)
#define MAX77696_EPDINTS_HVINPFLTS       BIT(5)
#define MAX77696_EPDINTS_VEEFLTS         BIT(4)
#define MAX77696_EPDINTS_VNEGFLTS        BIT(3)
#define MAX77696_EPDINTS_VPOSFLTS        BIT(2)
#define MAX77696_EPDINTS_VDDFLTS         BIT(1)
#define MAX77696_EPDINTS_VC5FLTS         BIT(0)

#undef  BITS
#define BITS(_msb, _lsb)                 ((BIT(_msb)-BIT(_lsb))+BIT(_msb))

#define MAX77696_ERCFLAG0_WDPMIC_FSHDN   BIT ( 0) /* PMIC System Watchdog Full Shutdown */
#define MAX77696_ERCFLAG0_WDPMIC_FRSTRT  BIT ( 1) /* PMIC System Watchdog Full Restart */
#define MAX77696_ERCFLAG0_MR_FSHDN       BIT ( 2) /* Manual Reset Full Shutdown */
#define MAX77696_ERCFLAG0_MR_FRSTRT      BIT ( 3) /* Manual Reset Partial Restart */
#define MAX77696_ERCFLAG0_SFT_PSHDN      BIT ( 4) /* Software Partial Shutdown */
#define MAX77696_ERCFLAG0_SFT_PRSTRT     BIT ( 5) /* Software Partial Restart */
#define MAX77696_ERCFLAG0_SFT_FSHDN      BIT ( 6) /* Software Full Shutdown */
#define MAX77696_ERCFLAG0_SFT_FRSTRT     BIT ( 7) /* Software Full Restart */

#define MAX77696_ERCFLAG1_RSTIN_PRSTRT   BIT ( 8) /* Reset Input Partial Restart */
#define MAX77696_ERCFLAG1_TOVLO_FSHDN    BIT ( 9) /* Thermal Overload Full Shutdown */
#define MAX77696_ERCFLAG1_SYS1UVLO_FSHDN BIT (10) /* System 1 Undervoltage Full Shutdown */
#define MAX77696_ERCFLAG1_LBMOK_FSHDN    BIT (11) /* Low-Battery Monitor Not Okay Full Shutdown */

#define MAX77696_FGIRQMASK_SMX           BIT (5)
#define MAX77696_FGIRQMASK_SMN           BIT (4)
#define MAX77696_FGIRQMASK_VMX           BIT (3)
#define MAX77696_FGIRQMASK_VMN           BIT (2)
#define MAX77696_FGIRQMASK_TMX           BIT (1)
#define MAX77696_FGIRQMASK_TMN           BIT (0)

#define MAX77696_FG_INT_SMX              BIT (5)
#define MAX77696_FG_INT_SMN              BIT (4)
#define MAX77696_FG_INT_VMX              BIT (3)
#define MAX77696_FG_INT_VMN              BIT (2)
#define MAX77696_FG_INT_TMX              BIT (1)
#define MAX77696_FG_INT_TMN              BIT (0)

#define SYS_HI_SOC_THRESHOLD             95
#define SYS_LO_SOC_THRESHOLD             80

#define SYS_CRIT_TEMP_THRESHOLD          60
#define SYS_HI_TEMP_THRESHOLD            45

enum pb_oneshot_state{
	NOTHING = 0,
	ALARM_SET,
	HIBER_SUSP,
	HIBER_RTC_IRQ,
	HIBER_CHG_IRQ,
	HALL_IRQ,
};


/*
 *------------------------------------------------------------------------------
 * max77696_read:
 *   (TBD)
 *
 * params:
 *   (TBD)
 *
 * return:
 *   error code
 *------------------------------------------------------------------------------
 */
static __always_inline int max77696_read (struct max77696_i2c *i2c,
		u8 addr, u8 *val)
{
	return i2c->read(i2c, addr, val);
} /* max77696_read */

/*
 *------------------------------------------------------------------------------
 * max77696_write:
 *   (TBD)
 *
 * params:
 *   (TBD)
 *
 * return:
 *   error code
 *------------------------------------------------------------------------------
 */
static __always_inline int max77696_write (struct max77696_i2c *i2c,
		u8 addr, u8 val)
{
	return i2c->write(i2c, addr, val);
} /* max77696_write */

/*
 *------------------------------------------------------------------------------
 * max77696_bulk_read:
 *   (TBD)
 *
 * params:
 *   (TBD)
 *
 * return:
 *   error code
 *------------------------------------------------------------------------------
 */
static __always_inline int max77696_bulk_read (struct max77696_i2c *i2c,
		u8 addr, u8 *dst, u16 len)
{
	return i2c->bulk_read(i2c, addr, dst, len);
} /* max77696_bulk_read */

/*
 *------------------------------------------------------------------------------
 * max77696_bulk_write:
 *   (TBD)
 *
 * params:
 *   (TBD)
 *
 * return:
 *   error code
 *------------------------------------------------------------------------------
 */
static __always_inline int max77696_bulk_write (struct max77696_i2c *i2c,
		u8 addr, const u8 *src, u16 len)
{
	return i2c->bulk_write(i2c, addr, src, len);
} /* max77696_read_masked */

/*
 *------------------------------------------------------------------------------
 * max77696_read_masked:
 *   (TBD)
 *
 * params:
 *   (TBD)
 *
 * return:
 *   error code
 *------------------------------------------------------------------------------
 */
static __always_inline int max77696_read_masked (struct max77696_i2c *i2c,
		u8 addr, u8 mask, u8 *val)
{
	int rc = max77696_read(i2c, addr, val);

	*val &= mask;

	return rc;
} /* max77696_read_masked */

/*
 *------------------------------------------------------------------------------
 * max77696_write_masked:
 *   (TBD)
 *
 * params:
 *   (TBD)
 *
 * return:
 *   error code
 *------------------------------------------------------------------------------
 */
static __always_inline int max77696_write_masked (struct max77696_i2c *i2c,
		u8 addr, u8 mask, u8 val)
{
	int rc;
	u8 buf;

	rc = max77696_read(i2c, addr, &buf);
	if (unlikely(rc)) {
		return rc;
	}

	buf = ((buf & (~mask)) | (val & mask));

	return max77696_write(i2c, addr, buf);
} /* max77696_write_masked */

#endif /* __MAX77696_H__ */

