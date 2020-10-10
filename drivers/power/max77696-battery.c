/*
 * Copyright (C) 2012 Maxim Integrated Product
 * Jayden Cha <jayden.cha@maxim-ic.com>
 * Copyright (c) 2012-2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <linux/sysdev.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/mfd/max77696.h>
#include <mach/boardid.h>
#include <linux/mfd/max77696-events.h>
#include <max77696_registers.h>
#include <llog.h>

#define DRIVER_DESC    "MAX77696 Fuel Gauge Driver"
#define DRIVER_AUTHOR  "Jayden Cha <jayden.cha@maxim-ic.com>"
#define DRIVER_NAME    MAX77696_GAUGE_NAME
#define DRIVER_VERSION MAX77696_DRIVER_VERSION".0"

#ifdef VERBOSE
#define dev_noise(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_noise(args...) do { } while (0)
#endif /* VERBOSE */

#define MODEL_SCALING			0x1
#define VFSOC0_LOCK				0x0000
#define VFSOC0_UNLOCK			0x0080
#define MODEL_UNLOCK1			0X0059
#define MODEL_UNLOCK2			0X00C4
#define MODEL_LOCK1				0X0000
#define MODEL_LOCK2				0X0000
#define DP_ACC_200				0x3200

struct max77696_gauge {
	struct max77696_chip              *chip;
	struct max77696_i2c               *i2c;
	struct device                     *dev;
	struct kobject                    *kobj;

	bool                               enable_current_sense;
	bool                               enable_por_init;
	bool 								por_bit_set;
	int                                charge_full_design;    /* in uAh */
	int                                battery_full_capacity; /* in percent */
	unsigned int                       r_sns;
	unsigned long                      update_interval;
	unsigned long                      update_interval_relax;
	int                                v_alert_max, v_alert_min; /* 5100 ~    0 [mV] */
	int                                t_alert_max, t_alert_min; /*  127 ~ -128 [C] */
	int                                s_alert_max, s_alert_min;
	bool                               alert_on_battery_removal;
	bool                               alert_on_battery_insertion;
	u16                                polling_properties;
	struct max77696_gauge_led_trigger  default_trigger;
	struct max77696_gauge_led_trigger  charging_trigger;
	struct max77696_gauge_led_trigger  charging_full_trigger;
	bool                               (*battery_online) (void);
	bool                               (*charger_online) (void);
	bool                               (*charger_enable) (void);

	bool                               init_complete;
	struct work_struct                 init_work;
	struct power_supply                psy;
	struct delayed_work                psy_work;
	struct delayed_work                lobat_work;
	struct delayed_work                batt_check_work;
	unsigned long                      lobat_interval;
	unsigned int                       irq;
	u16                                irq_unmask;
	unsigned long                      socrep_timestamp;
	int                                socrep;
	unsigned long                      temp_timestamp;
	int                                temp;
	unsigned long                      vcell_timestamp;
	int                                vcell;
	unsigned long                      avgvcell_timestamp;
	int                                avgvcell;
	unsigned long                      avgcurrent_timestamp;
	int                                avgcurrent;
	unsigned long                      availcap_timestamp;
	int                                availcap;
	unsigned long                      nomfullcap_timestamp;
	int                                nomfullcap;
	unsigned long                      cyclecnt_timestamp;
	int                                cyclecnt;
	unsigned long                      socavg_timestamp;
	int                                socav;
	unsigned long                      socmix_timestamp;
	int                                socmix;
	unsigned long                      availcapavg_timestamp;
	int                                availcap_av;
	unsigned long                      availcapmix_timestamp;
	int                                availcap_mix;
	struct mutex                       lock;
};

int wario_battery_voltage = 0;
int wario_battery_current = 0;
int wario_battery_temp_c = 0;
int wario_battery_temp_f = 0;
int wario_battery_capacity = 0;
int wario_battery_capacity_rep = 0;
int wario_battery_capacity_mix = 0;
int wario_battery_nac_mAH = 0;
int wario_battery_nac_mAH_av = 0;
int wario_battery_nac_mAH_mix = 0;
int wario_battery_lmd_mAH = 0;
int wario_battery_cycle_cnt = 0;
int wario_battery_valid = 1;
int max77696_gauge_cycles_ccmode = 0x60;	/* based on CC mode value from MGI data */
int wario_lobat_event = 0;
int wario_critbat_event = 0;
int wario_lobat_condition = 0;
int wario_critbat_condition = 0;
int wario_crittemp_event = 0;
#if defined(CONFIG_MX6SL_WARIO_WOODY)
int wario_hitemp_event = 0;
#endif
int wario_battery_error_flags = 0;
EXPORT_SYMBOL(wario_battery_voltage);
EXPORT_SYMBOL(wario_battery_current);
EXPORT_SYMBOL(wario_battery_temp_c);
EXPORT_SYMBOL(wario_battery_temp_f);
EXPORT_SYMBOL(wario_battery_capacity);
EXPORT_SYMBOL(wario_lobat_event);
EXPORT_SYMBOL(wario_lobat_condition);
EXPORT_SYMBOL(wario_critbat_event);
EXPORT_SYMBOL(wario_critbat_condition);
EXPORT_SYMBOL(wario_battery_error_flags);
EXPORT_SYMBOL(wario_battery_valid);
static int wario_temp_err_cnt = 0;
static int wario_battery_check_disabled = 0;

u16 fg_saved_fullcap = 0;
#if defined(CONFIG_POWER_SODA)
extern struct max77696_socbatt_eventdata socbatt_eventdata;
#endif
extern unsigned long total_suspend_time;
extern unsigned long last_suspend_time;
extern signed int display_temp_c;
extern int max77696_charger_set_mode (int mode);
extern int max77696_led_ctrl(unsigned int led_id, int led_state);
#ifdef CONFIG_FALCON
extern int in_falcon(void);
#endif

#define WARIO_TEMP_C_LO_THRESH              0
#define WARIO_TEMP_C_MID_THRESH             15
#define WARIO_TEMP_C_HI_THRESH              45
#define WARIO_TEMP_C_CRIT_THRESH            60

#define WARIO_ERROR_THRESH                  5
#define BATT_INI_ERROR                      0x0001
#define BATT_ID_ERROR                       0x0002
#define BATT_TEMP_ERROR                     0x0004
#define BATT_VOLT_ERROR                     0x0008

#define LOW_BATT_VOLT_LEVEL                 0
#define CRIT_BATT_VOLT_LEVEL                1
#define SYS_LOW_VOLT_THRESH                 3400    /* 3400 mV */
#define SYS_CRIT_VOLT_THRESH                3200    /* 3200 mV */
#define SYS_LOW_VOLT_THRESH_HYS             10      /* 10mV */
#define SYS_SUSPEND_LOW_VOLT_THRESH         3450    /* 3450 mV */
#define LOBAT_WORK_INTERVAL                 100
#define LOBAT_HYS_WORK_INTERVAL             15000
#define LOBAT_CHECK_INTERVAL                2000
#define LOBAT_CHECK_DELAY                   200

#define BATT_RESUME_INTERVAL                10
#define BATT_ID_CHECK_TEMP_DELTA            15
#define BATT_ID_CHECK_INIT_INTERVAL         2000

#define BATTLEARN_REG_BYTES                 MAX77696_GAUGE_LEARNED_NR_INFOS
uint16_t system_battlearn[BATTLEARN_REG_BYTES];

#if defined(CONFIG_MX6SL_WARIO_WOODY)
static void wario_battery_hitemp_event(struct max77696_gauge *me);
#endif
void max77696_gauge_soclo_event(void);
void max77696_gauge_sochi_event(void);
static int max77696_gauge_check_cdata(struct max77696_gauge *me);
static struct max77696_gauge* g_max77696_fg;
static struct max77696_gauge_config_data* g_max77696_fg_cdata = NULL;
void wario_battery_lobat_event(struct max77696_gauge *me, int crit_level);
static void wario_battery_overheat_event(struct max77696_gauge *me);
static void max77696_gauge_learned_write(struct max77696_gauge *me, int id, u16 learned);
static void max77696_gauge_learned_read(struct max77696_gauge *me, int id, u16 *learned);
static void max77696_gauge_learned_write_all (struct max77696_gauge *me, u16* learned);
static void max77696_gauge_learned_read_all (struct max77696_gauge *me, u16 *learned);

/* Wario Tequila - battery = 898mAh (cap = /0.5mAh = 0x0704)*/
static struct max77696_gauge_config_data gauge_cdata_wario = {
	.tgain =            0xE8C5,		/* -5947 */
	.toff =             0x245A,		/* 9306 */
	.config =           0x2210,
	.filter_cfg =       0xA7A7,		/* temp(12min) & current(90sec) filters increased to minimize changes from transient differs from INI file */
	.relax_cfg =        0x003B,
	.learn_cfg =        0x2606,
	.full_soc_thresh = 	0x5A00,
	.rcomp0 =           0x0041,
	.tcompc0 =          0x1121,
	.ichgt_term =       0x0100,

	.vempty =           0xACDA,
	.qrtbl00 =          0x2186,
	.qrtbl10 =          0x2186,
	.qrtbl20 =          0x0680,
	.qrtbl30 =          0x0501,

	.cycles =           0x0060,
	.fullcap =          0x0704,
	.design_cap =       0x0704,
	.fullcapnom =       0x0704,
	.batt_cap =         0x0704,
	.cell_char_tbl = {
		/* Data from Line31 - Line78 of battery characterization data file */
		/* 0x80 */
		0xA8D0,0xB680,0xB960,0xBB80,0xBBE0,0xBC30,0xBD50,0xBE20,
		0xBE80,0xC090,0xC5A0,0xC730,0xC9D0,0xCC30,0xCE90,0xD110,
		/* 0x90 */
		0x0160,0x0D30,0x0F30,0x3260,0x0CD0,0x1E90,0x2500,0x2FD0,
		0x11F0,0x0D80,0x0A40,0x0A70,0x09C0,0x09F0,0x04E0,0x04E0,
		/* 0xA0 */
		0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,
		0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100
	},
};

/* Icewine - (EVT1.2) battery = 1320mAh (1293mAh INI) (cap = /0.5mAh = 0x0A1A) */
static struct max77696_gauge_config_data gauge_cdata_icewine_435V = {
	.tgain =            0xE8C5,		/* -5947 */
	.toff =             0x245A,		/* 9306 */
	.config =           0x2210,
	.filter_cfg =       0xA7A7,		/* temp(12min) & current(90sec) filters increased to minimize changes from transient differs from INI file */
	.relax_cfg =        0x003B,
	.learn_cfg =        0x2606,
	.full_soc_thresh = 	0x5A00,
	.rcomp0 =           0x0059,
	.tcompc0 =          0x2032,
	.ichgt_term =       0x01C0,

	.vempty =           0xACDA,
	.qrtbl00 =          0x3C00,
	.qrtbl10 =          0x1B00,
	.qrtbl20 =          0x0B01,
	.qrtbl30 =          0x0881,

	.cycles =           0x0060,
	.fullcap =          0x0A1A,
	.design_cap =       0x0A1A,
	.fullcapnom =       0x0A1A,
	.batt_cap =         0x0A1A,
	.cell_char_tbl = {
		/* Data from Line31 - Line78 of battery characterization data file */
		/* 0x80 */
		0xAC80,0xB300,0xB720,0xB870,0xBB60,0xBC70,0xBDB0,0xBF20,
		0xC070,0xC1D0,0xC490,0xC760,0xCCF0,0xD020,0xD360,0xD850,
		/* 0x90 */
		0x00E0,0x0500,0x1180,0x0D10,0x1A00,0x1B00,0x15E0,0x1150,
		0x1080,0x08F0,0x0880,0x0860,0x07F0,0x07C0,0x06E0,0x06E0,
		/* 0xA0 */
		0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,
		0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,
	},
};

/* Icewine - (proto5 interim) battery = 1518mAh  (cap = /0.5mAh = 0x0BDC) */
static struct max77696_gauge_config_data gauge_cdata_icewine_proto_435V = {
	.tgain =            0xE8C5,		/* -5947 */
	.toff =             0x245A,		/* 9306 */
	.config =           0x2210,
	.filter_cfg =       0xA7A7,		/* temp(12min) & current(90sec) filters increased to minimize changes from transient differs from INI file */
	.relax_cfg =        0x003B,
	.learn_cfg =        0x2606,
	.full_soc_thresh = 	0x5A00,
	.rcomp0 =           0x0039,
	.tcompc0 =          0x1A35,
	.ichgt_term =       0x01E0,

	.vempty =           0xACDA,
	.qrtbl00 =          0x5C00,
	.qrtbl10 =          0x218C,
	.qrtbl20 =          0x0498,
	.qrtbl30 =          0x0418,

	.cycles =           0x0060,
	.fullcap =          0x0BDC,
	.design_cap =       0x0BDC,
	.fullcapnom =       0x0BDC,
	.batt_cap =         0x0BDC,
	.cell_char_tbl = {
		/* Data from Line31 - Line78 of battery characterization data file */
		/* 0x80 */
		0xAC80,0xAFC0,0xB3F0,0xB830,0xBA20,0xBC20,0xBCF0,0xBDD0,
		0xBF90,0xC150,0xC450,0xC750,0xCA60,0xCD70,0xD2B0,0xD7F0,
		/* 0x90 */
		0x01F0,0x0710,0x06F0,0x0E30,0x0DC0,0x2060,0x1E00,0x0FF0,
		0x0FF0,0x08F0,0x08F0,0x07F0,0x07F0,0x06D0,0x06F0,0x06F0,
		/* 0xA0 */
		0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,
		0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,
	},
};

/* Icewine - (EVT 1 pre-proto5) battery = 850mAh  (cap = /0.5mAh = 0x06A4) */
static struct max77696_gauge_config_data gauge_cdata_icewine_42V = {
	.tgain =            0xE8C5,		/* -5947 */
	.toff =             0x245A,		/* 9306 */
	.config =           0x2210,
	.filter_cfg =       0xA7A7,		/* temp(12min) & current(90sec) filters increased to minimize changes from transient differs from INI file */
	.relax_cfg =        0x003B,
	.learn_cfg =        0x2606,
	.full_soc_thresh = 	0x5A00,
	.rcomp0 =           0x0041,
	.tcompc0 =          0x1121,
	.ichgt_term =       0x0140,

	.vempty =           0xACDA,
	.qrtbl00 =          0x2186,
	.qrtbl10 =          0x2186,
	.qrtbl20 =          0x0680,
	.qrtbl30 =          0x0501,

	.cycles =           0x0060,
	.fullcap =          0x06A4,
	.design_cap =       0x06A4,
	.fullcapnom =       0x06A4,
	.batt_cap =         0x06A4,
	.cell_char_tbl = {
		/* Data from Line31 - Line78 of battery characterization data file */
		/* 0x80 */
		0xA8D0,0xB680,0xB960,0xBB80,0xBBE0,0xBC30,0xBD50,0xBE20,
		0xBE80,0xC090,0xC5A0,0xC730,0xC9D0,0xCC30,0xCE90,0xD110,
		/* 0x90 */
		0x0160,0x0D30,0x0F30,0x3260,0x0CD0,0x1E90,0x2500,0x2FD0,
		0x11F0,0x0D80,0x0A40,0x0A70,0x09C0,0x09F0,0x04E0,0x04E0,
		/* 0xA0 */
		0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,
		0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100
	},
};

/* pinot - battery = 1400mAh (1408mAh INI) (cap = /0.5mAh = 0x0B00) */
static struct max77696_gauge_config_data gauge_cdata_pinot = {
	.tgain =            0xE8C5,		/* -5947*/
	.toff =             0x245A,		/* 9306 */
	.config =           0x2210,
	.filter_cfg =       0xA7A7,		/* temp(12min) & current(90sec) filters increased to minimize changes from transient differs from INI file */
	.relax_cfg =        0x003B,
	.learn_cfg =        0x2606,
	.full_soc_thresh = 	0x5A00,
	.rcomp0 =           0x005F,
	.tcompc0 =          0x2033,
	.ichgt_term =       0x01C6,

	.vempty =           0xACDA,
	.qrtbl00 =          0x3D00,
	.qrtbl10 =          0x1C00,
	.qrtbl20 =          0x0B81,
	.qrtbl30 =          0x0901,

	.cycles =           0x0060,
	.fullcap =          0x0B00,
	.design_cap =       0x0B00,
	.fullcapnom =       0x0B00,
	.batt_cap =         0x0B00,
	.cell_char_tbl = {
		/* Data from Line31 - Line78 of battery characterization data file */
		/* 0x80 */
		0xAC80,0xB030,0xB750,0xBA30,0xBB00,0xBC40,0xBCC0,0xBD50,
		0xBE70,0xBFA0,0xC100,0xC4E0,0xC6D0,0xC8F0,0xCCD0,0xD0D0,
		/* 0x90 */
		0x01C0,0x0300,0x0F00,0x1640,0x1200,0x2610,0x2420,0x2390,
		0x12F0,0x12C0,0x0BF0,0x0CE0,0x0BF0,0x08F0,0x08D0,0x08D0,
		/* 0xA0 */
		0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,
		0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100,0x0100
	},
};

/* Bourbon - battery = 941mAh (cap = /0.5mAh = 0x075A)*/
static struct max77696_gauge_config_data gauge_cdata_bourbon = {
	.tgain =            0xE81F,		/* -6113 temp range -5 and 65C, error 2.6 degrees at 35C) */
	.toff =             0x252A,		/* 9514 temp range -5 and 65C, error 2.6 degrees at 35C) */
	.config =           0x2210,
	.filter_cfg =       0xA7A7,		/* temp(12min) & current(90sec) filters increased to minimize changes from transient differs from INI file */
	.relax_cfg =        0x003B,
	.learn_cfg =        0x2606,
	.full_soc_thresh = 	0x5A00,		
	.rcomp0 =           0x008B,
	.tcompc0 =          0x3F52,
	.ichgt_term =       0x011C,

	.vempty =           0xAA56,
	.qrtbl00 =          0x2A80,
	.qrtbl10 =          0x1501,
	.qrtbl20 =          0x0A02,
	.qrtbl30 =          0x0803,

	.cycles =           0x0060,
	.fullcap =          0x075A,
	.design_cap =       0x075A,
	.fullcapnom =       0x075A,
	.batt_cap =         0x075A,	
	.cell_char_tbl = {
		/* Data from Line31 - Line78 of battery characterization data file */
		/* 0x80 */
		0xAAD0,0xB6B0,0xB800,0xB8C0,0xBA40,0xBB20,0xBCA0,0xBD20,
		0xBDE0,0xBEB0,0xC020,0xC190,0xC4D0,0xC850,0xCC10,0xCFE0,
		/* 0x90 */
		0x01F0,0x00C0,0x2B00,0x0CF0,0x0AF0,0x19F0,0x3100,0x2060,
		0x24F0,0x0FF0,0x0FF0,0x0BF0,0x0C10,0x0930,0x0900,0x0900,
		/* 0xA0 */
		0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,
		0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080
	},
};

/* Whisky - battery = 247.4mAh (cap = /0.5mAh = 0x01EF)*/
static struct max77696_gauge_config_data gauge_cdata_whisky = {
	.tgain =            0xE81F,		/* -6113 temp range -5 and 65C, error 2.6 degrees at 35C) */
	.toff =             0x252A,		/* 9514 temp range -5 and 65C, error 2.6 degrees at 35C) */
	.config =           0x2210,
	.filter_cfg =       0xA7A7,		/* temp(12min) & current(90sec) filters increased to minimize changes from transient differs from INI file */
	.relax_cfg =        0x003B,
	.learn_cfg =        0x2603,
	.full_soc_thresh = 	0x5F00,		
	.rcomp0 =           0x0030,
	.tcompc0 =          0x1815,
	.ichgt_term =       0x004C,

	.vempty =           0xAA64,
	.qrtbl00 =          0x1282,
	.qrtbl10 =          0x0789,
	.qrtbl20 =          0x0285,
	.qrtbl30 =          0x0207,

	.cycles =           0x0060,
	.fullcap =          0x01EF,
	.design_cap =       0x01EF,
	.fullcapnom =       0x01EF,
	.batt_cap =         0x01EF,	
	.cell_char_tbl = {
		/* Data from Line31 - Line78 of battery characterization data file */
		/* 0x80 */
		0x7DA0,0xADD0,0xB750,0xB8C0,0xBB50,0xBC80,0xBD30,0xBE50,
		0xBF10,0xC050,0xC240,0xC390,0xC680,0xCBD0,0xCE00,0xD140,
		/* 0x90 */
		0x0030,0x0200,0x1650,0x0E30,0x1510,0x2120,0x1D70,0x1960,
		0x12F0,0x0E60,0x0A40,0x0BB0,0x0AC0,0x0890,0x08C0,0x08C0,
		/* 0xA0 */
		0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,
		0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080
	},
};

#define __psy_to_max77696_gauge(psy_ptr) \
	container_of(psy_ptr, struct max77696_gauge, psy)
#define __psy_work_to_max77696_gauge(psy_work_ptr) \
	container_of(psy_work_ptr, struct max77696_gauge, psy_work.work)

#define __get_i2c(chip)                  (&((chip)->gauge_i2c))
#define __lock(me)                       mutex_lock(&((me)->lock))
#define __unlock(me)                     mutex_unlock(&((me)->lock))
#define __is_locked(me)                     mutex_is_locked(&((me)->lock))


/* GAUGE Register Read/Write */
#define max77696_gauge_reg_read(me, reg, val_ptr) \
	max77696_read((me)->i2c, FG_REG(reg), val_ptr)
#define max77696_gauge_reg_write(me, reg, val) \
	max77696_write((me)->i2c, FG_REG(reg), val)
#define max77696_gauge_reg_bulk_read(me, reg, dst, len) \
	max77696_bulk_read((me)->i2c, FG_REG(reg), dst, len)
#define max77696_gauge_reg_bulk_write(me, reg, src, len) \
	max77696_bulk_write((me)->i2c, FG_REG(reg), src, len)
#define max77696_gauge_reg_read_masked(me, reg, mask, val_ptr) \
	max77696_read_masked((me)->i2c, FG_REG(reg), mask, val_ptr)
#define max77696_gauge_reg_write_masked(me, reg, mask, val) \
	max77696_write_masked((me)->i2c, FG_REG(reg), mask, val)

/* GAUGE Register Single Bit Ops */
#define max77696_gauge_reg_get_bit(me, reg, bit, val_ptr) \
	({\
	 int __rc = max77696_gauge_reg_read_masked(me, reg,\
		 FG_REG_BITMASK(reg, bit), val_ptr);\
	 *(val_ptr) = FG_REG_BITGET(reg, bit, *(val_ptr));\
	 __rc;\
	 })
#define max77696_gauge_reg_set_bit(me, reg, bit, val) \
	({\
	 max77696_gauge_reg_write_masked(me, reg,\
		 FG_REG_BITMASK(reg, bit),\
		 FG_REG_BITSET(reg, bit, val));\
	 })

/* GAUGE Register Word I/O */
#define max77696_gauge_reg_read_word(me, reg, val_ptr) \
	({\
	 int __rc = max77696_gauge_reg_bulk_read(me,\
		 reg, (u8*)(val_ptr), 2);\
	 if (unlikely(__rc)) {\
	 dev_err((me)->dev, ""#reg" read error [%d]\n", __rc);\
	 }\
	 *(val_ptr) = __le16_to_cpu(*val_ptr);\
	 __rc;\
	 })
#define max77696_gauge_reg_write_word(me, reg, val) \
	({\
	 u16 __buf = __cpu_to_le16(val);\
	 int __rc = max77696_gauge_reg_bulk_write(me,\
		 reg, (u8*)(&__buf), 2);\
	 if (unlikely(__rc)) {\
	 dev_err((me)->dev, ""#reg" write error [%d]\n", __rc);\
	 }\
	 __rc;\
	 })

/* GAUGE Register Word Maksed I/O */
#define max77696_gauge_reg_read_masked_word(me, reg, mask, val_ptr) \
	({\
	 int __rc = max77696_gauge_reg_bulk_read(me,\
		 reg, (u8*)(val_ptr), 2);\
	 if (unlikely(__rc)) {\
	 dev_err((me)->dev, ""#reg" read error [%d]\n", __rc);\
	 }\
	 *(val_ptr) = (__le16_to_cpu(*val_ptr) & (mask));\
	 __rc;\
	 })
#define max77696_gauge_reg_write_masked_word(me, reg, mask, val) \
	({\
	 u16 __buf = __cpu_to_le16(val);\
	 int __rc = max77696_gauge_reg_bulk_read(me,\
		 reg, (u8*)(&__buf), 2);\
	 if (unlikely(__rc)) {\
	 dev_err((me)->dev, ""#reg" read error [%d]\n", __rc);\
	 }\
	 __buf = __cpu_to_le16((__buf & (~(mask))) | ((val) & (mask)));\
	 __rc = max77696_gauge_reg_bulk_write(me, reg, (u8*)(&__buf), 2);\
	 if (unlikely(__rc)) {\
	 dev_err((me)->dev, ""#reg" write error [%d]\n", __rc);\
	 }\
	 __rc;\
	 })

/* macros for conversion from 2-byte register value to integer */
#define __u16_to_intval(val) \
	((int)(val))
#define __s16_to_intval(val) \
	(((val) & 0x8000)? -((int)((0x7fff & ~(val)) + 1)) : ((int)(val)))

#define max77696_gauge_reg_write_word_verify(me, reg, val) \
	max77696_gauge_verify_reg_write_word(me, FG_REG(reg), val)

void max77696_gauge_read_learned_all ( u16 *learned);
void max77696_gauge_write_learned_all ( u16* learned);

static int max77696_gauge_verify_reg_write_word(struct max77696_gauge *me,
		u8 reg, u16 value)
{
	int retries = 4;
	int rc = 0;
	u16 rd_val, wr_val = value;

	do {
		rc = max77696_bulk_write(me->i2c, reg, (u8*)(&wr_val), 2);
		max77696_bulk_read(me->i2c, reg, (u8*)(&rd_val), 2);
		if (rd_val != wr_val) {
			rc = -EIO;
			retries--;
		}
	} while (retries && rd_val != wr_val);

	if (rc < 0)
		dev_err(me->dev, "MG config: %s:error=[%d] \n",__func__,rc);

	return rc;
}

static void max77696_fg_reg_dump(struct max77696_gauge *me)
{
	int i = 0;
	u16 val = 0;

	for (i = 0; i <= 0x4F; i++) {
		max77696_bulk_read(me->i2c, i, (u8*)(&val), 2);
		printk("0x%04x, ",val);
	}

	for (i = 0xE0; i <= 0xFF; i++) {
		max77696_bulk_read(me->i2c, i, (u8*)(&val), 2);
		printk("0x%04x, ", val);
	}
	printk(KERN_INFO " \n");
}

static ssize_t max77696_fg_voltage_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_voltage);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_voltage, S_IRUGO, max77696_fg_voltage_show, NULL);

static ssize_t max77696_fg_current_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_current);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_current, S_IRUGO, max77696_fg_current_show, NULL);

static ssize_t max77696_fg_capacity_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_capacity);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_capacity, S_IRUGO, max77696_fg_capacity_show, NULL);

static ssize_t max77696_fg_capacity_rep_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_capacity_rep);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_capacity_rep, S_IRUGO, max77696_fg_capacity_rep_show, NULL);

static ssize_t max77696_fg_capacity_mix_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_capacity_mix);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_capacity_mix, S_IRUGO, max77696_fg_capacity_mix_show, NULL);

static ssize_t max77696_fg_temp_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_temp_f);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_temperature, S_IRUGO, max77696_fg_temp_show, NULL);

static ssize_t max77696_fg_nac_mAH_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_nac_mAH);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_mAH, S_IRUGO, max77696_fg_nac_mAH_show, NULL);

static ssize_t max77696_fg_nac_av_mAH_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_nac_mAH_av);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_mAH_av, S_IRUGO, max77696_fg_nac_av_mAH_show, NULL);

static ssize_t max77696_fg_nac_mix_mAH_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_nac_mAH_mix);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_mAH_mix, S_IRUGO, max77696_fg_nac_mix_mAH_show, NULL);

static ssize_t max77696_fg_lmd_mAH_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_lmd_mAH);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_lmd_mAH, S_IRUGO, max77696_fg_lmd_mAH_show, NULL);

static ssize_t max77696_fg_cycle_count_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_battery_cycle_cnt);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(battery_cycle, S_IRUGO, max77696_fg_cycle_count_show, NULL);

static ssize_t max77696_fg_reg_dump_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	struct max77696_gauge *me = g_max77696_fg;
	int rc;
	/* regdump all FG registers */
	rc = (int)sprintf(buf, "\n");
	max77696_fg_reg_dump(me);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(fg_reg_dump, S_IRUGO, max77696_fg_reg_dump_show, NULL);

static ssize_t max77696_batterror_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", wario_battery_error_flags);
}
static SYSDEV_ATTR(battery_error, S_IRUGO, max77696_batterror_show, NULL);

static ssize_t max77696_battery_id_valid_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", wario_battery_valid);
}
static SYSDEV_ATTR(battery_id_valid, S_IRUGO, max77696_battery_id_valid_show, NULL);

static ssize_t max77696_battlearn_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	max77696_gauge_read_learned_all(system_battlearn);
	memcpy(buf, system_battlearn, sizeof(system_battlearn));

	return sizeof(system_battlearn);
}

static ssize_t max77696_battlearn_store (struct sys_device *dev,
		struct sysdev_attribute *devattr, const char *buf, size_t count)
{
	/*
	 * Powerd is invoking this sysentry
	 * 	if(POR bit is set)
	 * 	{
	 * 		if  write len is ok
	 * 			copy the buffer from emmc->powerd->battery driver->pmic registers
	 * 		else
	 * 			new battery -- does nothing, assume the new battlearn is going to wipe out the old setting anyway.
	 *
	 * }
	 * */
	struct max77696_gauge *me = g_max77696_fg;

	if(me->por_bit_set){
		if(count == 20) {
			memcpy(system_battlearn, buf, count);
			max77696_gauge_write_learned_all(system_battlearn);
		}
		else {
			printk(KERN_INFO "KERNEL: I pmic:fg newbatt::replaced new battery\n");
		}
		me->por_bit_set = 0;
	}
	return (ssize_t)count;
}

static SYSDEV_ATTR(fg_batt_learn, S_IWUSR|S_IRUGO, max77696_battlearn_show, max77696_battlearn_store);

static ssize_t max77696_fg_cdata_check_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	struct max77696_gauge *me = g_max77696_fg;
	int rc;

	if (max77696_gauge_check_cdata(me)) {
		rc = (int)sprintf(buf, "ModelGauge Data Verification *** Data Invalid ***\n");
	} else {
		rc = (int)sprintf(buf, "ModelGauge Data Verification *** Data Valid ***\n");
	}
	return (ssize_t)rc;
}
static SYSDEV_ATTR(fg_cdata_check, S_IRUGO, max77696_fg_cdata_check_show, NULL);

static ssize_t fg_lobat_condition_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	int rc;
	rc = (int)sprintf(buf, "%d\n", wario_lobat_condition);
	return (ssize_t)rc;
}
static SYSDEV_ATTR(fg_lobat_condition, S_IRUGO, fg_lobat_condition_show, NULL);

static ssize_t max77696_fg_por_ctrl_show (struct sys_device *dev,
		struct sysdev_attribute *devattr, char *buf)
{
	struct max77696_gauge *me = g_max77696_fg;
	int rc = 0;
	u16 val = 0;

	max77696_gauge_reg_read_word(me, STATUS, &val);
	if (val & FG_STATUS_POR) {
		rc = (int)sprintf(buf, "1\n");
	} else {
		rc = (int)sprintf(buf, "0\n");
	}
	return (ssize_t)rc;
}

static ssize_t max77696_fg_por_ctrl_store(struct sys_device *dev,
		struct sysdev_attribute *devattr, const char *buf, size_t count)
{
	struct max77696_gauge *me = g_max77696_fg;
	int value = 0;
	u16 sts = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		return -EINVAL;
	}

	if (value > 0) {
		__lock(me);\
		/* Set the POR bit */
		max77696_gauge_reg_read_word(me, STATUS, &sts);
		max77696_gauge_reg_write_word(me, STATUS, (sts | FG_STATUS_POR));
		printk(KERN_INFO "KERNEL: I pmic:fg por::sw controlled por - reboot your kindle\n");
		__unlock(me);\
	} else {
		return -EINVAL;
	}

	return (ssize_t)count;
}
static SYSDEV_ATTR(fg_por_ctrl, S_IWUSR|S_IRUGO, max77696_fg_por_ctrl_show, max77696_fg_por_ctrl_store);

#ifdef DEVELOPMENT_MODE
static ssize_t
battery_store_send_lobat_uevent(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t count)
{
	int value;
	struct max77696_gauge *me = g_max77696_fg;

	if (sscanf(buf, "%d", &value) <= 0) {
		return -EINVAL;
	}

	if (value == 1) {
		wario_battery_lobat_event(me, 0);
	} else if (value == 2) {
		wario_battery_lobat_event(me, 1);
	} else {
		return -EINVAL;
	}

	return count;
}
static SYSDEV_ATTR(send_lobat_uevent, S_IWUSR, NULL, battery_store_send_lobat_uevent);

static ssize_t
battery_store_send_overheat_uevent(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t count)
{
	int value;
	struct max77696_gauge *me = g_max77696_fg;

	if (sscanf(buf, "%d", &value) <= 0) {
		return -EINVAL;
	}
	if (value > 0) {
		wario_battery_overheat_event(me);
	} else {
		return -EINVAL;
	}
	return count;
}
static SYSDEV_ATTR(send_overheat_uevent, S_IWUSR, NULL, battery_store_send_overheat_uevent);

static ssize_t
battery_send_soclo_uevent(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t count)
{
	int value;

	if (sscanf(buf, "%d", &value) <= 0) {
		return -EINVAL;
	}
	if (value > 0) {
		max77696_gauge_soclo_event();
	} else {
		return -EINVAL;
	}
	return count;
}
static SYSDEV_ATTR(send_soclo_uevent, S_IWUSR, NULL, battery_send_soclo_uevent);

static ssize_t
battery_send_sochi_uevent(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t count)
{
	int value;

	if (sscanf(buf, "%d", &value) <= 0) {
		return -EINVAL;
	}
	if (value > 0) {
		max77696_gauge_sochi_event();
	} else {
		return -EINVAL;
	}
	return count;
}
static SYSDEV_ATTR(send_sochi_uevent, S_IWUSR, NULL, battery_send_sochi_uevent);
#endif

#define MAX77696_GAUGE_ATTR_LEARNED(_name, _id) \
	static ssize_t max77696_##_name##_show (struct sys_device *dev,\
			struct sysdev_attribute *devattr, char *buf)\
{\
	struct max77696_gauge *me = g_max77696_fg; \
	u16 val;\
	int rc;\
	__lock(me);\
	max77696_gauge_learned_read(me, _id, &val);\
	rc = (int)snprintf(buf, PAGE_SIZE, "%u (0x%04X)\n", val, val);\
	__unlock(me);\
	return (ssize_t)rc;\
}\
static ssize_t max77696_##_name##_store (struct sys_device *dev,\
		struct sysdev_attribute *devattr, const char *buf, size_t count)\
{\
	struct max77696_gauge *me = g_max77696_fg; \
	u16 val;\
	__lock(me);\
	val = (u16)simple_strtoul(buf, NULL, 10);\
	max77696_gauge_learned_write(me, _id, val);\
	__unlock(me);\
	return (ssize_t)count;\
}\
static SYSDEV_ATTR(_name, S_IWUSR | S_IRUGO,\
		max77696_##_name##_show, max77696_##_name##_store)

MAX77696_GAUGE_ATTR_LEARNED(fg_learn_fullcap,     MAX77696_GAUGE_LEARNED_FULLCAP);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_cycles,      MAX77696_GAUGE_LEARNED_CYCLES);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_rcomp0,      MAX77696_GAUGE_LEARNED_RCOMP0);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_tempco,      MAX77696_GAUGE_LEARNED_TEMPCO);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_qresidual00, MAX77696_GAUGE_LEARNED_QRESIDUAL00);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_qresidual10, MAX77696_GAUGE_LEARNED_QRESIDUAL10);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_qresidual20, MAX77696_GAUGE_LEARNED_QRESIDUAL20);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_qresidual30, MAX77696_GAUGE_LEARNED_QRESIDUAL30);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_dqacc,       MAX77696_GAUGE_LEARNED_DQACC);
MAX77696_GAUGE_ATTR_LEARNED(fg_learn_dpacc,       MAX77696_GAUGE_LEARNED_DPACC);

static struct sysdev_class wario_battery_sysclass = {
	.name = "wario_battery",
};

static struct sys_device wario_battery_device = {
	.id = 0,
	.cls = &wario_battery_sysclass,
};

static int max77696_gauge_sysdev_register(void)
{
    int rc = 0;
	rc = sysdev_class_register(&wario_battery_sysclass);
	if (!rc) {
		rc = sysdev_register(&wario_battery_device);
		if (!rc) {
			sysdev_create_file(&wario_battery_device, &attr_battery_voltage);
			sysdev_create_file(&wario_battery_device, &attr_battery_current);
			sysdev_create_file(&wario_battery_device, &attr_battery_temperature);
			sysdev_create_file(&wario_battery_device, &attr_battery_capacity);
			sysdev_create_file(&wario_battery_device, &attr_battery_capacity_rep);
			sysdev_create_file(&wario_battery_device, &attr_battery_capacity_mix);
			sysdev_create_file(&wario_battery_device, &attr_battery_mAH);
			sysdev_create_file(&wario_battery_device, &attr_battery_mAH_av);
			sysdev_create_file(&wario_battery_device, &attr_battery_mAH_mix);
			sysdev_create_file(&wario_battery_device, &attr_battery_lmd_mAH);
			sysdev_create_file(&wario_battery_device, &attr_battery_cycle);
			sysdev_create_file(&wario_battery_device, &attr_battery_error);
			sysdev_create_file(&wario_battery_device, &attr_battery_id_valid);
			sysdev_create_file(&wario_battery_device, &attr_fg_reg_dump);
			sysdev_create_file(&wario_battery_device, &attr_fg_cdata_check);
			sysdev_create_file(&wario_battery_device, &attr_fg_lobat_condition);
			sysdev_create_file(&wario_battery_device, &attr_fg_batt_learn);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_fullcap);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_cycles);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_rcomp0);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_tempco);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_qresidual00);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_qresidual10);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_qresidual20);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_qresidual30);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_dqacc);
			sysdev_create_file(&wario_battery_device, &attr_fg_learn_dpacc);
			sysdev_create_file(&wario_battery_device, &attr_fg_por_ctrl);
#ifdef DEVELOPMENT_MODE
			sysdev_create_file(&wario_battery_device, &attr_send_lobat_uevent);
			sysdev_create_file(&wario_battery_device, &attr_send_overheat_uevent);
			sysdev_create_file(&wario_battery_device, &attr_send_soclo_uevent);
			sysdev_create_file(&wario_battery_device, &attr_send_sochi_uevent);
#endif
		} else {
			sysdev_class_unregister(&wario_battery_sysclass);
		}
	}
	return rc;
}

static void max77696_gauge_sysdev_unregister(void)
{
	sysdev_remove_file(&wario_battery_device, &attr_battery_voltage);
	sysdev_remove_file(&wario_battery_device, &attr_battery_current);
	sysdev_remove_file(&wario_battery_device, &attr_battery_temperature);
	sysdev_remove_file(&wario_battery_device, &attr_battery_capacity);
	sysdev_remove_file(&wario_battery_device, &attr_battery_capacity_rep);
	sysdev_remove_file(&wario_battery_device, &attr_battery_capacity_mix);
	sysdev_remove_file(&wario_battery_device, &attr_battery_mAH);
	sysdev_remove_file(&wario_battery_device, &attr_battery_mAH_av);
	sysdev_remove_file(&wario_battery_device, &attr_battery_mAH_mix);
	sysdev_remove_file(&wario_battery_device, &attr_battery_lmd_mAH);
	sysdev_remove_file(&wario_battery_device, &attr_battery_cycle);
	sysdev_remove_file(&wario_battery_device, &attr_battery_error);
	sysdev_remove_file(&wario_battery_device, &attr_battery_id_valid);
	sysdev_remove_file(&wario_battery_device, &attr_fg_reg_dump);
	sysdev_remove_file(&wario_battery_device, &attr_fg_cdata_check);
	sysdev_remove_file(&wario_battery_device, &attr_fg_lobat_condition);
	sysdev_remove_file(&wario_battery_device, &attr_fg_batt_learn);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_fullcap);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_cycles);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_rcomp0);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_tempco);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_qresidual00);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_qresidual10);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_qresidual20);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_qresidual30);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_dqacc);
	sysdev_remove_file(&wario_battery_device, &attr_fg_learn_dpacc);
	sysdev_remove_file(&wario_battery_device, &attr_fg_por_ctrl);
#ifdef DEVELOPMENT_MODE
	sysdev_remove_file(&wario_battery_device, &attr_send_lobat_uevent);
	sysdev_remove_file(&wario_battery_device, &attr_send_overheat_uevent);
	sysdev_remove_file(&wario_battery_device, &attr_send_soclo_uevent);
	sysdev_remove_file(&wario_battery_device, &attr_send_sochi_uevent);
#endif
    sysdev_unregister(&wario_battery_device);
    sysdev_class_unregister(&wario_battery_sysclass);
    return;
}

#define FG_INT_BITS_MASK   \
	(FG_CONFIG_BER | FG_CONFIG_BEI | FG_CONFIG_AEN | FG_CONFIG_ALSH | FG_CONFIG_VS)
#define FG_INT_BITS_FILTER \
	(FG_INT_BITS_MASK & ~FG_CONFIG_ALSH)

#define max77696_gauge_write_irq_mask(me) \
	do {\
		u16 _mask = FG_INT_BITS_MASK;\
		u16 _val  = ((me)->irq_unmask & FG_INT_BITS_FILTER);\
		int _rc   = max77696_gauge_reg_write_masked_word(me,\
				CONFIG, _mask, _val);\
		if (unlikely(_rc)) {\
			dev_err((me)->dev, "CONFIG write error [%d]\n", _rc);\
		}\
	} while (0)

static __inline void max77696_gauge_enable_irq (struct max77696_gauge* me,
		u16 alert_bits, bool forced)
{
	if (unlikely(!forced && (me->irq_unmask & alert_bits) == alert_bits)) {
		/* already unmasked */
		return;
	}

	/* set enabled flag */
	me->irq_unmask |= alert_bits;
	max77696_gauge_write_irq_mask(me);
}

static __inline void max77696_gauge_disable_irq (struct max77696_gauge* me,
		u16 alert_bits, bool forced)
{
	if (unlikely(!forced && (me->irq_unmask & alert_bits) == 0)) {
		/* already masked */
		return;
	}

	/* clear enabled flag */
	me->irq_unmask &= ~alert_bits;
	max77696_gauge_write_irq_mask(me);
}

static const u8 max77696_gauge_learned_regs[GAUGE_LEARNED_NR_REGS] = {
	FG_FULLCAP_REG,
	FG_CYCLES_REG,
	FG_RCOMP0_REG,
	FG_TEMPCO_REG,
	FG_QRESIDUAL00_REG,
	FG_QRESIDUAL10_REG,
	FG_QRESIDUAL20_REG,
	FG_QRESIDUAL30_REG,
	FG_DQACC_REG,
	FG_DPACC_REG,
};

static void max77696_gauge_learned_write (struct max77696_gauge *me, int id,
		u16 learned)
{
	u16 buf = __cpu_to_le16(learned);
	int rc;

	rc = max77696_bulk_write(me->i2c,
			max77696_gauge_learned_regs[id], (u8*)(&buf), 2);

	if (unlikely(rc)) {
		dev_err(me->dev, "failed to write learned-%d [%d]\n", id, rc);
	}
}

static void max77696_gauge_learned_read (struct max77696_gauge *me, int id,
		u16 *learned)
{
	u16 buf = 0;
	int rc;

	rc = max77696_bulk_read(me->i2c,
			max77696_gauge_learned_regs[id], (u8*)(&buf), 2);

	if (unlikely(rc)) {
		dev_err(me->dev, "failed to read learned-%d [%d]\n", id, rc);
		return;
	}

	*learned = __le16_to_cpu(buf);
}

static void max77696_gauge_learned_write_all (struct max77696_gauge *me, u16* learned)
{
	u16 buf ;
	int rc, id;

	/* Note: FULLCAP(0), DQACC(8), DPACC(9) removed from the learn parmeters
	 * based on the Current drift issue - SW workaround
	 */
	for (id = 1; id < (GAUGE_LEARNED_NR_REGS - 2); id++) {
		buf = __cpu_to_le16(learned[id]);
		__lock(me);
		rc = max77696_bulk_write(me->i2c,
			max77696_gauge_learned_regs[id], (u8*)(&buf), 2);
		__unlock(me);
		if (unlikely(rc)) {
			dev_err(me->dev, "failed to write learned-%d [%d]\n", id, rc);
		}
	}
}

static void max77696_gauge_learned_read_all (struct max77696_gauge *me, u16 *learned)
{
	u16 buf = 0;
	int rc, id;

	/* Note: FULLCAP(0), DQACC(8), DPACC(9) removed from the learn parmeters
	 * based on the Current drift issue - SW workaround
	 */
	for (id = 1; id < (GAUGE_LEARNED_NR_REGS - 2); id++) {
		rc = max77696_bulk_read(me->i2c,
				max77696_gauge_learned_regs[id], (u8*)(&buf), 2);

		if (unlikely(rc)) {
			dev_err(me->dev, "failed to read learned-%d [%d]\n", id, rc);
			return;
		}
		learned[id] = __le16_to_cpu(buf);
	}
}

#define __is_timestamp_expired(me, timestamp) \
	time_before(jiffies, (me)->timestamp + (me)->update_interval)
#define __is_timestamp_relax_expired(me, timestamp) \
	time_before(jiffies, (me)->timestamp + (me)->update_interval_relax)
#define __reset_timestamp(me, timestamp) \
	((me)->timestamp = jiffies)

static bool max77696_gauge_update_socrep (struct max77696_gauge *me, bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, socrep = me->socrep;

	if (unlikely(!force && __is_timestamp_expired(me, socrep_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, SOCREP, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "SOCREP read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, socrep_timestamp);

	me->socrep = (int)(__u16_to_intval(buf) >> 8);
	wario_battery_capacity_rep = me->socrep;

	updated    = (bool)(me->socrep != socrep);
	dev_noise(me->dev, "(%u) SOCREP   %3d%%\n", updated, me->socrep);

out:
	return updated;
}

static bool max77696_gauge_update_socmix (struct max77696_gauge *me, bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, socmix = me->socmix;

	if (unlikely(!force && __is_timestamp_expired(me, socmix_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, SOCMIX, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "SOCMIX read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, socmix_timestamp);

	me->socmix = (int)(__u16_to_intval(buf) >> 8);
	wario_battery_capacity_mix = me->socmix;
	updated    = (bool)(me->socmix != socmix);
	dev_noise(me->dev, "(%u) SOCMIX   %3d%%\n", updated, me->socmix);

out:
	return updated;
}

static bool max77696_gauge_update_socavg (struct max77696_gauge *me, bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, socav = me->socav;

	if (unlikely(!force && __is_timestamp_expired(me, socavg_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, SOCAV, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "SOCAV read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, socavg_timestamp);

	me->socav = (int)(__u16_to_intval(buf) >> 8);
	wario_battery_capacity = me->socav;
	updated    = (bool)(me->socav != socav);
	dev_noise(me->dev, "(%u) SOCAV   %3d%%\n", updated, me->socav);

out:
	return updated;
}

static bool max77696_gauge_update_temp (struct max77696_gauge *me, bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, temp = me->temp;
	int celsius, fahrenheit;

	if (unlikely(!force && __is_timestamp_expired(me, temp_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, TEMP, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "TEMP read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, temp_timestamp);

	celsius = __s16_to_intval(buf);
	/* Ignore LSB for 1 deg celsius resolution */
	celsius = (celsius >> 8);
	fahrenheit = ((celsius * 9) / 5) + 32;
	me->temp = fahrenheit;
	wario_battery_temp_f = me->temp;
	wario_battery_temp_c = celsius;

	updated  = (bool)(me->temp != temp);
	dev_noise(me->dev, "(%u) TEMP     %3dC\n", updated, me->temp);

out:
	return updated;
}

static bool max77696_gauge_update_vcell (struct max77696_gauge *me, bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, vcell = me->vcell;

	if (unlikely(!force && __is_timestamp_expired(me, vcell_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, VCELL, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "VCELL read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, vcell_timestamp);

	me->vcell = (((__u16_to_intval(buf) >> 3) * 625) / 1000);
	wario_battery_voltage = me->vcell;
	updated   = (bool)(me->vcell != vcell);
	dev_noise(me->dev, "(%u) VCELL    %3dmV\n", updated, me->vcell);

out:
	return updated;
}

static bool max77696_gauge_update_avgvcell (struct max77696_gauge *me,
		bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, avgvcell = me->avgvcell;

	if (unlikely(!force && __is_timestamp_expired(me, avgvcell_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, AVGVCELL, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "AVGVCELL read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, avgvcell_timestamp);

	me->avgvcell = (((__u16_to_intval(buf) >> 3) * 625) / 1000);
	updated = (bool)(me->avgvcell != avgvcell);
	dev_noise(me->dev, "(%u) AVGVCELL %3dmV\n", updated, me->avgvcell);

out:
	return updated;
}

static bool max77696_gauge_update_avgcurrent (struct max77696_gauge *me,
		bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, avgcurrent = me->avgcurrent;

	if (unlikely(!force && __is_timestamp_expired(me, avgcurrent_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, AVGCURRENT, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "AVGCURRENT read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, avgcurrent_timestamp);

	me->avgcurrent  = __s16_to_intval(buf);
	/* current in uA ; r_sns = 10000 uOhms */
	me->avgcurrent *= (1562500 / me->r_sns);
	/* current in mA */
	me->avgcurrent /= 1000;

	wario_battery_current = me->avgcurrent;
	updated = (bool)(me->avgcurrent != avgcurrent);
	dev_noise(me->dev, "(%u) AVGVCELL %3dmV\n", updated, me->avgcurrent);

out:
	return updated;
}

static bool max77696_gauge_update_availcap_rep(struct max77696_gauge *me,
		bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, availcap = me->availcap;

	if (unlikely(!force && __is_timestamp_expired(me, availcap_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, REMCAPREP, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "REMCAPREP read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, availcap_timestamp);

	me->availcap = __u16_to_intval(buf);
	/* Units of LSB = 5.0uVh/r_sns */
	/* NAC in uAH ; r_sns = 10000 uOhms */
	me->availcap *= ( 5000 / (me->r_sns / 1000));
	/* NAC in mAH */
	me->availcap /= 1000;

	wario_battery_nac_mAH = me->availcap;
	updated = (bool)(me->availcap != availcap);
	dev_noise(me->dev, "(%u) NAC %3dmAH\n", updated, me->availcap);

out:
	return updated;
}

static bool max77696_gauge_update_availcap_mix(struct max77696_gauge *me,
		bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, availcap_mix = me->availcap_mix;

	if (unlikely(!force && __is_timestamp_expired(me, availcapmix_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, REMCAPMIX, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "REMCAPMIX read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, availcapmix_timestamp);

	me->availcap_mix = __u16_to_intval(buf);
	/* Units of LSB = 5.0uVh/r_sns */
	/* NAC in uAH ; r_sns = 10000 uOhms */
	me->availcap_mix *= ( 5000 / (me->r_sns / 1000));
	/* NAC(MIX) in mAH */
	me->availcap_mix /= 1000;

	wario_battery_nac_mAH_mix = me->availcap_mix;
	updated = (bool)(me->availcap_mix != availcap_mix);
	dev_noise(me->dev, "(%u) NAC(MIX) %3dmAH\n", updated, me->availcap_mix);

out:
	return updated;
}

static bool max77696_gauge_update_availcap_avg(struct max77696_gauge *me,
		bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, availcap_av = me->availcap_av;

	if (unlikely(!force && __is_timestamp_expired(me, availcapavg_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, REMCAPAV, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "REMCAPAV read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, availcapavg_timestamp);

	me->availcap_av = __u16_to_intval(buf);
	/* Units of LSB = 5.0uVh/r_sns */
	/* NAC in uAH ; r_sns = 10000 uOhms */
	me->availcap_av *= ( 5000 / (me->r_sns / 1000));
	/* NAC(AVG) in mAH */
	me->availcap_av /= 1000;

	wario_battery_nac_mAH_av = me->availcap_av;
	updated = (bool)(me->availcap_av != availcap_av);
	dev_noise(me->dev, "(%u) NAC(AVG) %3dmAH\n", updated, me->availcap_av);

out:
	return updated;
}

static bool max77696_gauge_update_nomfullcap(struct max77696_gauge *me,
		bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, nomfullcap = me->nomfullcap;

	if (unlikely(!force && __is_timestamp_relax_expired(me, nomfullcap_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, FULLCAPNOM, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "FULLCAPNOM read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, nomfullcap_timestamp);

	me->nomfullcap = __u16_to_intval(buf);
	/* Units of LSB = 5.0uVh/r_sns */
	/* LMD in uAH ; r_sns = 10000 uOhms */
	me->nomfullcap *= ( 5000 / (me->r_sns / 1000));
	/* LMD in mAH */
	me->nomfullcap /= 1000;

	wario_battery_lmd_mAH = me->nomfullcap;
	updated = (bool)(me->nomfullcap != nomfullcap);
	dev_noise(me->dev, "(%u) LMD %3dmAH\n", updated, me->nomfullcap);

out:
	return updated;
}

static bool max77696_gauge_update_cycle_count(struct max77696_gauge *me,
		bool force)
{
	bool updated = 0;
	u16 buf;
	int rc, cyclecnt = me->cyclecnt;

	if (unlikely(!force && __is_timestamp_relax_expired(me, cyclecnt_timestamp))) {
		goto out;
	}

	rc = max77696_gauge_reg_read_word(me, CYCLES, &buf);
	if (unlikely(rc)) {
		dev_err(me->dev, "CYCLES read error [%d]\n", rc);
		goto out;
	}

	__reset_timestamp(me, cyclecnt_timestamp);

	me->cyclecnt = ((__u16_to_intval(buf)) - max77696_gauge_cycles_ccmode);
	me->cyclecnt /= 100;

	wario_battery_cycle_cnt = me->cyclecnt;
	updated = (bool)(me->cyclecnt != cyclecnt);
	dev_noise(me->dev, "(%u) CYCLECNT %d\n", updated, me->cyclecnt);

out:
	return updated;
}

static void wario_battery_overheat_event(struct max77696_gauge *me)
{
	char *envp[] = { "BATTERY=overheat", NULL };
	printk(KERN_CRIT "KERNEL: E pmic:fg battery temp::overheat event temp=%dC\n",
			wario_battery_temp_c);
	kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
	return;
}

void max77696_gauge_overheat(void)
{
	struct max77696_gauge *me = g_max77696_fg;
	if (unlikely(me == NULL)) {
		return;
	}
	if (!wario_crittemp_event) {
		wario_battery_overheat_event(me);
		wario_crittemp_event = 1;
	}
	return;
}
EXPORT_SYMBOL(max77696_gauge_overheat);

#if defined(CONFIG_MX6SL_WARIO_WOODY)
static void wario_battery_hitemp_event(struct max77696_gauge *me)
{
	char *envp[] = { "BATTERY=temp_hi", NULL };
	printk(KERN_CRIT "KERNEL: E pmic:fg battery temp::hi event temp=%dC\n",
			wario_battery_temp_c);
	kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
	return;
}

void max77696_gauge_temphi(void)
{
	struct max77696_gauge *me = g_max77696_fg;
	if (unlikely(me == NULL)) {
		return;
	}
	if (!wario_hitemp_event) {
		wario_battery_hitemp_event(me);
		wario_hitemp_event = 1;
	}
	return;
}
EXPORT_SYMBOL(max77696_gauge_temphi);
#endif


/*
 * Post a low battery or a critical battery event to the userspace
 */
void wario_battery_lobat_event(struct max77696_gauge *me, int crit_level)
{
	if (!crit_level) {
		if (!wario_lobat_event) {
			char *envp[] = { "BATTERY=low", NULL };
			printk(KERN_CRIT "KERNEL: I pmic:fg battery valrtmin::lowbat event\n");
			kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
			wario_lobat_event = 1;
		}
	} else {
		if (!wario_critbat_event) {
			char *envp[] = { "BATTERY=critical", NULL };
			printk(KERN_CRIT "KERNEL: I pmic:fg battery mbattlow::critbat event\n");
			kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
			wario_critbat_event = 1;
		}
	}
}

static void max77696_gauge_lobat_work(struct work_struct *work)
{
	struct max77696_gauge *me = container_of(work, struct max77696_gauge, lobat_work.work);
	int i = 0;
#if defined(CONFIG_MX6SL_WARIO_BASE)
	int batt_voltage = 0;
	bool charger_conn = max77696_charger_online_def_cb();

	for (i = 0; i < (LOBAT_CHECK_INTERVAL / LOBAT_CHECK_DELAY); i++) {
		max77696_gauge_update_vcell(me, 1);
		pr_debug("%s: voltage = %dmV \n", __func__, me->vcell);
		if (me->vcell > (SYS_LOW_VOLT_THRESH )) {
			break;
		}
		msleep(LOBAT_CHECK_DELAY);
	}
	batt_voltage = me->vcell;

	if (!charger_conn) {
		if ( (batt_voltage <= SYS_CRIT_VOLT_THRESH) || wario_critbat_condition) {
			wario_battery_lobat_event(me, CRIT_BATT_VOLT_LEVEL);
		} else if (batt_voltage <= SYS_LOW_VOLT_THRESH) {
			wario_battery_lobat_event(me, LOW_BATT_VOLT_LEVEL);
		} else {
			schedule_delayed_work(&(me->lobat_work), me->lobat_interval);
		}
	}
#elif defined(CONFIG_MX6SL_WARIO_WOODY)
	
	if (max77696_charger_online_def_cb()) {
		return; /* charger plugged in */
	}

	for (i = 0; i < (LOBAT_CHECK_INTERVAL / LOBAT_CHECK_DELAY); i++) {
		max77696_gauge_update_vcell(me, 1);
		pr_debug("%s: voltage = %dmV \n", __func__, me->vcell);
		if (me->vcell > (SYS_LOW_VOLT_THRESH + SYS_LOW_VOLT_THRESH_HYS)) {
			break;
		}
		msleep(LOBAT_CHECK_DELAY);
	}

	if (!max77696_charger_online_def_cb()) {
		if ( (me->vcell <= SYS_CRIT_VOLT_THRESH)) {
			wario_battery_lobat_event(me, CRIT_BATT_VOLT_LEVEL);
		} else if (me->vcell <= SYS_LOW_VOLT_THRESH) {
			wario_battery_lobat_event(me, LOW_BATT_VOLT_LEVEL);
		} else if (me->vcell <= (SYS_LOW_VOLT_THRESH + SYS_LOW_VOLT_THRESH_HYS)) {
			pr_debug("%s: schedule lobat check voltage = %dmV \n", __func__, me->vcell);
			schedule_delayed_work(&(me->lobat_work), msecs_to_jiffies(LOBAT_HYS_WORK_INTERVAL));
		} else {
			pr_debug("%s: skipping lobat check (recovered from voltage droop) voltage = %dmV \n", __func__, me->vcell);
		}
	}
#endif
	return;
}

void max77696_gauge_lobat(void)
{
	struct max77696_gauge *me = g_max77696_fg;
	schedule_delayed_work(&(me->lobat_work), me->lobat_interval);
	return;
}
EXPORT_SYMBOL(max77696_gauge_lobat);

void max77696_gauge_sochi_event(void)
{
	struct max77696_gauge *me = g_max77696_fg;
	char *envp[] = { "BATTERY=soc_hi", NULL };
	kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
	printk(KERN_CRIT "KERNEL: I pmic:fg battery salrtmax::soc_hi event\n");
	printk(KERN_INFO "%s Wario battery temp:%dC, volt:%dmV, current:%dmA, capacity(soc_av):%d%%, capacity(soc_rep):%d%% mAH:%dmAh\n",
		__func__, wario_battery_temp_c, wario_battery_voltage, wario_battery_current,
		wario_battery_capacity, wario_battery_capacity_rep, wario_battery_nac_mAH);
#if defined(CONFIG_POWER_SODA)
	/* since we reached battery Hi, subscribe to min/Low event */
	atomic_set(&socbatt_eventdata.override, SOCBATT_MINSUBSCRIBE_TRIGGER);
	schedule_delayed_work(&socbatt_eventdata.socbatt_event_work,
		msecs_to_jiffies(SOCBATT_EVTHDL_DELAY));
#endif
}
EXPORT_SYMBOL(max77696_gauge_sochi_event);

void max77696_gauge_soclo_event(void)
{
	struct max77696_gauge *me = g_max77696_fg;
	char *envp[] = { "BATTERY=soc_lo", NULL };
	kobject_uevent_env(me->kobj, KOBJ_CHANGE, envp);
	printk(KERN_CRIT "KERNEL: I pmic:fg battery salrtmin::soc_lo event\n");
	printk(KERN_INFO "%s Wario battery temp:%dC, volt:%dmV, current:%dmA, capacity(soc_av):%d%%, capacity(soc_rep):%d%% mAH:%dmAh\n",
		__func__, wario_battery_temp_c, wario_battery_voltage, wario_battery_current,
		wario_battery_capacity, wario_battery_capacity_rep, wario_battery_nac_mAH);
#if defined(CONFIG_POWER_SODA)        
	/* since we reached battery Low, subscribe to Hi/max event */
	atomic_set(&socbatt_eventdata.override, SOCBATT_MAXSUBSCRIBE_TRIGGER);
	schedule_delayed_work(&socbatt_eventdata.socbatt_event_work,
		msecs_to_jiffies(SOCBATT_EVTHDL_DELAY));
#endif
}
EXPORT_SYMBOL(max77696_gauge_soclo_event);

/* Modify CAP_REP based on CAP_AV */
int max77696_gauge_repcap_adj(struct max77696_gauge *me)
{
	int rc = 0;
	u16 av_cap = 0;

	rc = max77696_gauge_reg_read_word(me, REMCAPAV, &av_cap);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg repcap_adj::REMCAPAV failed\n");
		return -EIO;
	}

	rc = max77696_gauge_reg_write_word_verify(me, REMCAPREP, av_cap);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg repcap_adj::REMCAPREP failed\n");
		return -EIO;
	}

	return 0;
}

/* Modify gauge parameters (CAP_REP, FULL_CAP) to adjust for the drift - (j42-5579) */
int max77696_gauge_driftadj_handler(void)
{
	struct max77696_gauge *me = g_max77696_fg;
	int rc = 0;
	u16 av_cap = 0, full_cap = 0;

	rc = max77696_gauge_repcap_adj(me);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg driftadj::REPCAP(adj with AVCAP) failed\n");
		return -EIO;
	}

	rc = max77696_gauge_reg_read_word(me, REMCAPAV, &av_cap);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg driftadj::REMCAPAV failed\n");
		return -EIO;
	}

	rc = max77696_gauge_reg_read_word(me, FULLCAP, &full_cap);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg driftadj::FULLCAP failed\n");
		return -EIO;
	}

	if (full_cap > av_cap) {
		rc = max77696_gauge_reg_write_word_verify(me, FULLCAP, av_cap);
		if (unlikely(rc)) {
			printk(KERN_ERR "KERNEL: E pmic:fg driftadj:FULLCAP(adj with AVCAP) failed\n");
			return -EIO;
		}
	}

	return 0;
}
EXPORT_SYMBOL(max77696_gauge_driftadj_handler);

/*
 *	wario_check_battery:
 * 		This function validates battery by comparing temperature
 *		readings between FG & DISPLAY thermistor
 */
static int wario_check_battery(void)
{
    int temp_delta = 0;
	temp_delta = abs(wario_battery_temp_c - display_temp_c);

	if ((temp_delta < BATT_ID_CHECK_TEMP_DELTA) ||
		(strncmp(system_bootmode, "diags", 5) == 0) ||
		(lab126_board_is(BOARD_ID_WARIO) || lab126_board_is(BOARD_ID_WOODY)) ||
		wario_battery_check_disabled) {
		return 1;
	}
	return 0;
}

static void wario_battery_check_handler(struct work_struct *work)
{
	struct max77696_gauge *me = container_of(work, struct max77696_gauge, lobat_work.work);
	int rc = 0;
	wario_battery_valid = wario_check_battery();

	printk(KERN_INFO "KERNEL: I pmic:fg battery id check::wario_battery_valid=%d\n",wario_battery_valid);

	if (!wario_battery_valid) {
		LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
				"kernel", "fg", "wario battery invalid", 1, "");

		/* Turn OFF charging */
		rc = max77696_charger_set_mode(MAX77696_CHARGER_MODE_OFF);
        if (unlikely(rc)) {
            dev_err(me->dev, "%s: Unable to turn-off chga [%d]\n", __func__, rc);
            goto out;
        }

		/* Turn OFF AMBER LED (charging indicator) - manual mode */
        rc = max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_OFF);
        if (unlikely(rc)) {
            dev_err(me->dev, "%s: error led ctrl (amber) [%d]\n", __func__, rc);
        }

#if defined(CONFIG_MX6SL_WARIO_WOODY)
		/* Turn OFF GREEN LED - manual mode */
		rc = max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
		if (unlikely(rc)) {
		    dev_err(me->dev, "%s: error led ctrl (amber) [%d]\n", __func__, rc);
		}
#endif
	}

out:
	return;
}

static void max77696_gauge_init_alert (struct max77696_gauge *me)
{
	u16 alert_val, alert_en = 0;
	int alert_max, alert_min;

	/* To prevent false interrupts, the threshold registers should be
	 * initialized before setting the Aen bit.
	 */
	if (likely(me->v_alert_max > me->v_alert_min)) {
		alert_en |= (FG_CONFIG_AEN | FG_CONFIG_VS);

		alert_max = min(me->v_alert_max, 5100); /* 255 * 20 */
		alert_min = max(me->v_alert_min,    0);

		dev_info(me->dev,
				"Voltage alert     %dmV ... %dmV\n", alert_min, alert_max);

		alert_val  = ((u16)DIV_ROUND_UP(alert_max, 20) << 8);
		alert_val |= ((u16)DIV_ROUND_UP(alert_min, 20) << 0);
	} else {
		dev_info(me->dev, "Voltage alert     (disabled)\n");
		alert_val = 0xFF00;
	}

	max77696_gauge_reg_write_word(me, VALRT_TH, __cpu_to_le16(alert_val));

	if (likely(me->t_alert_max > me->t_alert_min)) {
		alert_en |= FG_CONFIG_AEN;

		alert_max = min(me->t_alert_max,  127);
		alert_min = max(me->t_alert_min, -128);

		dev_dbg(me->dev,
				"Temperture alert  %dC ... %dC\n", alert_min, alert_max);

		alert_val  = ((s16)alert_max << 8);
		alert_val |= ((s16)alert_min << 0);
	} else {
		dev_info(me->dev, "Temperture alert  (disabled)\n");
		alert_val = 0x7F80;
	}

	max77696_gauge_reg_write_word(me, TALRT_TH, __cpu_to_le16(alert_val));

	if (likely(me->s_alert_max > me->s_alert_min)) {
		alert_en |= (FG_CONFIG_AEN | FG_CONFIG_SS);

		alert_max = min(me->s_alert_max, 255);
		alert_min = max(me->s_alert_min,   0);

		dev_dbg(me->dev,
				"SOC alert         %d%% ... %d%%\n", alert_min, alert_max);

		alert_val  = ((u16)alert_max << 8);
		alert_val |= ((u16)alert_min << 0);
	} else {
		dev_info(me->dev, "SOC alert         (disabled)\n");
		alert_val = 0xFF00;
	}

	max77696_gauge_reg_write_word(me, SALRT_TH, __cpu_to_le16(alert_val));

	alert_en |= ((me->alert_on_battery_removal  )? FG_CONFIG_BER : 0);
	alert_en |= ((me->alert_on_battery_insertion)? FG_CONFIG_BEI : 0);

	/* Enable fuel gauge interrupts we need */
	max77696_gauge_enable_irq(me, alert_en, 0);
}

static int max77696_gauge_load_custom_model(struct max77696_gauge *me,
		struct max77696_gauge_config_data *cdata)
{
	int tbl_size = ARRAY_SIZE(cdata->cell_char_tbl);
	u16 *tmp_data = NULL;
	u8 regaddr = FG_MCHAR_TBL_REG;
	int rc = 0;
	int i;

	tmp_data = kcalloc(tbl_size, sizeof(*tmp_data), GFP_KERNEL);
	if (!tmp_data) {
		dev_err(me->dev, "MG config: couldn't allocate memory \n");
		return -ENOMEM;
	}

	/* Unlock Model Access */
	max77696_gauge_reg_write_word_verify(me, MLOCK1, MODEL_UNLOCK1);
	max77696_gauge_reg_write_word_verify(me, MLOCK2, MODEL_UNLOCK2);

	/* Write the Custom Model */
	for (i = 0; i < tbl_size; i++)
		max77696_bulk_write((me)->i2c, (regaddr + i), (u8*)(cdata->cell_char_tbl + i), 2);

	/* Read the Custom Model */
	for (i = 0; i < tbl_size; i++)
		max77696_bulk_read((me)->i2c, (regaddr + i), (u8*)(tmp_data + i), 2);

	/* Verify the Custom Model */
	if (memcmp(cdata->cell_char_tbl, tmp_data, tbl_size)) {
		dev_err(me->dev, "MG config: %s compare failed\n", __func__);
		for (i = 0; i < tbl_size; i++)
			dev_info(me->dev, "0x%x, 0x%x", cdata->cell_char_tbl[i],tmp_data[i]);
		dev_info(me->dev, "\n");
		rc = -EIO;
		goto out;
	}

	/* Lock Model Access */
	max77696_gauge_reg_write_word_verify(me, MLOCK1, MODEL_LOCK1);
	max77696_gauge_reg_write_word_verify(me, MLOCK2, MODEL_LOCK2);

	/* Verify Lock Model */
	memset(tmp_data, 0, tbl_size);
	for (i = 0; i < tbl_size; i++) {
		max77696_bulk_read((me)->i2c, (regaddr + i), (u8*)(tmp_data + i), 2);
		if (tmp_data[i]) {
			rc = -EIO;
		}
	}

out:
	kfree(tmp_data);
	return rc;
}

static int max77696_gauge_verify_custom_model(struct max77696_gauge *me,
		struct max77696_gauge_config_data *cdata)
{
	int tbl_size = ARRAY_SIZE(cdata->cell_char_tbl);
	u16 *tmp_data = NULL;
	u8 regaddr = FG_MCHAR_TBL_REG;
	int rc = 0;
	int i;

	tmp_data = kcalloc(tbl_size, sizeof(*tmp_data), GFP_KERNEL);
	if (!tmp_data) {
		dev_err(me->dev, "MG config: couldn't allocate memory \n");
		return -ENOMEM;
	}

	/* Unlock Model Access */
	max77696_gauge_reg_write_word_verify(me, MLOCK1, MODEL_UNLOCK1);
	max77696_gauge_reg_write_word_verify(me, MLOCK2, MODEL_UNLOCK2);

	/* Read the Custom Model */
	for (i = 0; i < tbl_size; i++)
		max77696_bulk_read((me)->i2c, (regaddr + i), (u8*)(tmp_data + i), 2);

	/* Verify the Custom Model */
	if (memcmp(cdata->cell_char_tbl, tmp_data, tbl_size)) {
		printk(KERN_ERR "MG config: %s compare failed\n", __func__);
		for (i = 0; i < tbl_size; i++)
			printk(KERN_INFO "0x%x, 0x%x", cdata->cell_char_tbl[i],tmp_data[i]);
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::custom model verify failed\n");
		rc = -EIO;
	}

	/* Lock Model Access */
	max77696_gauge_reg_write_word_verify(me, MLOCK1, MODEL_LOCK1);
	max77696_gauge_reg_write_word_verify(me, MLOCK2, MODEL_LOCK2);

	/* Verify Lock Model */
	memset(tmp_data, 0, tbl_size);
	for (i = 0; i < tbl_size; i++) {
		max77696_bulk_read((me)->i2c, (regaddr + i), (u8*)(tmp_data + i), 2);
		if (tmp_data[i]) {
			rc = -EIO;
			printk(KERN_ERR "KERNEL: E pmic:fg mg check::custom model lock failed\n");
			break;
		}
	}

	kfree(tmp_data);
	return rc;
}

static int max77696_gauge_check_cdata(struct max77696_gauge *me)
{
	struct max77696_gauge_config_data *cdata = g_max77696_fg_cdata;
	u16 val = 0;
	int rc = 0;

	if (cdata == NULL) {
		rc = -EINVAL;
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::characterization data not found\n");
		goto out;
	}

	max77696_gauge_reg_read_word(me, FILTERCFG, &val);
	if (val != cdata->filter_cfg) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::FILTERCFG invalid "
			"read val: %x cdata: %x\n", val, cdata->filter_cfg);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, RELAXCFG, &val);
	if (val != cdata->relax_cfg) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::RELAXCFG invalid "
			"read val: %x cdata: %x\n", val, cdata->relax_cfg);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, FULLSOCTHR, &val);
	if (val != cdata->full_soc_thresh) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::FULLSOCTHR invalid "
			"read val: %x cdata: %x\n", val, cdata->full_soc_thresh);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, TEMPCO, &val);
	if (val != cdata->tcompc0) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::TEMPCO invalid "
			"read val: %x cdata: %x\n", val, cdata->tcompc0);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, ICHGTERM, &val);
	if (val != cdata->ichgt_term) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::ICHGTERM invalid "
			"read val: %x cdata: %x\n", val, cdata->ichgt_term);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, TGAIN, &val);
	if (val != cdata->tgain) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::TGAIN invalid "
			"read val: %x cdata: %x\n", val, cdata->tgain);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, TOFF, &val);
	if (val != cdata->toff) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::TOFF invalid "
			"read val: %x cdata: %x\n", val, cdata->toff);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, V_EMPTY, &val);
	if (val != cdata->vempty) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::V_EMPTY invalid "
			"read val: %x cdata: %x\n", val, cdata->vempty);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, QRESIDUAL00, &val);
	if (val != cdata->qrtbl00) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::QRESIDUAL00 invalid "
			"read val: %x cdata: %x\n", val, cdata->qrtbl00);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, QRESIDUAL10, &val);
	if (val != cdata->qrtbl10) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::QRESIDUAL10 invalid "
			"read val: %x cdata: %x\n", val, cdata->qrtbl10);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, QRESIDUAL20, &val);
	if (val != cdata->qrtbl20) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::QRESIDUAL20 invalid "
			"read val: %x cdata: %x\n", val, cdata->qrtbl20);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, QRESIDUAL30, &val);
	if (val != cdata->qrtbl30) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::QRESIDUAL30 invalid "
			"read val: %x cdata: %x\n", val, cdata->qrtbl30);
		rc = -EINVAL;
		goto out;
	}

	max77696_gauge_reg_read_word(me, DESIGNCAP, &val);
	if (val != cdata->design_cap) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::DESIGNCAP invalid "
			"read val: %x cdata: %x\n", val, cdata->design_cap);
		rc = -EINVAL;
		goto out;
	}

	/* Verify Custom Model */
	rc = max77696_gauge_verify_custom_model(me, cdata);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg mg check::custom model invalid\n");
		rc = -EINVAL;
		goto out;
	}

out:
	return rc;
}

static int max77696_gauge_init_config(struct max77696_gauge *me)
{
	struct max77696_gauge_config_data *cdata = g_max77696_fg_cdata;
	u16 vfsoc = 0, rep_cap = 0, dq_acc = 0, sts = 0, full_cap = 0, rep_cap_chk = 0;
	u32 rem_cap = 0;
	int rc = 0;

	if (cdata == NULL) {
		rc = -EINVAL;
		printk(KERN_ERR "KERNEL: E pmic:fg init config::characterization data not found\n");
		goto out;
	}

	/* Initialize Configuration */
	max77696_gauge_reg_write_word(me, CONFIG, cdata->config);
	max77696_gauge_reg_write_word(me, FILTERCFG, cdata->filter_cfg);
	max77696_gauge_reg_write_word(me, RELAXCFG, cdata->relax_cfg);
	max77696_gauge_reg_write_word(me, LEARNCFG, cdata->learn_cfg);
	max77696_gauge_reg_write_word(me, FULLSOCTHR, cdata->full_soc_thresh);

	/* Load Custom Model */
	rc = max77696_gauge_load_custom_model(me, cdata);
	if (rc) {
		rc = -EIO;
		printk(KERN_ERR "KERNEL: E pmic:fg init config::custom model load failed\n");
		goto out;
	}

	/* Write Custom Parameters */
	rc = max77696_gauge_reg_write_word_verify(me, RCOMP0, cdata->rcomp0);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::RCOMP0 failed\n");
		rc = -EIO;
		goto out;
	}

	max77696_gauge_reg_write_word_verify(me, TEMPCO, cdata->tcompc0);
	max77696_gauge_reg_write_word(me, ICHGTERM, cdata->ichgt_term);
	max77696_gauge_reg_write_word(me, TGAIN, cdata->tgain);
	max77696_gauge_reg_write_word(me, TOFF, cdata->toff);
	max77696_gauge_reg_write_word_verify(me, V_EMPTY, cdata->vempty);
	max77696_gauge_reg_write_word_verify(me, QRESIDUAL00, cdata->qrtbl00);
	max77696_gauge_reg_write_word_verify(me, QRESIDUAL10, cdata->qrtbl10);
	max77696_gauge_reg_write_word_verify(me, QRESIDUAL20, cdata->qrtbl20);
	max77696_gauge_reg_write_word_verify(me, QRESIDUAL30, cdata->qrtbl30);

	/* Update Full Capacity Parameters */
	rc = max77696_gauge_reg_write_word_verify(me, FULLCAP, cdata->fullcap);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::FULLCAP failed\n");
		rc = -EIO;
		goto out;
	}
	max77696_gauge_reg_write_word(me, DESIGNCAP, cdata->design_cap);
	rc = max77696_gauge_reg_write_word_verify(me, FULLCAPNOM, cdata->fullcapnom);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::FULLCAPNOM failed\n");
		rc = -EIO;
		goto out;
	}

	/* delay must be atleast 350mS to allow VFSOC
	 * to be calculated from the new configuration
	 */
	msleep(350);

	/* Write VFSOC value to VFSOC0 */
	max77696_gauge_reg_read_word(me, VFSOC, &vfsoc);
	rc = max77696_gauge_reg_write_word_verify(me, VFSOC_EN, VFSOC0_UNLOCK);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::VFSOC0_UNLOCK failed\n");
		rc = -EIO;
		goto out;
	}
	max77696_gauge_reg_write_word(me, VFSOC0, vfsoc);
	rc = max77696_gauge_reg_write_word_verify(me, VFSOC_EN, VFSOC0_LOCK);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::VFSOC0_LOCK failed\n");
		rc = -EIO;
		goto out;
	}

	/* Advance to Coulomb-Counter Mode */
	rc = max77696_gauge_reg_write_word_verify(me, CYCLES, cdata->cycles);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::CYCLES failed\n");
		rc = -EIO;
		goto out;
	}

	/* Load New Capacity Parameters */
	rem_cap = ((vfsoc >> 8) * cdata->fullcap) / 100;
	rc = max77696_gauge_reg_write_word_verify(me, REMCAPMIX, (u16)rem_cap);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::REMCAPMIX failed\n");
		rc = -EIO;
		goto out;
	}

	rep_cap = ((u16)rem_cap) * ((cdata->batt_cap/cdata->fullcap) / MODEL_SCALING);
	rc = max77696_gauge_reg_write_word_verify(me, REMCAPREP, rep_cap);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::REMCAPREP failed\n");
		rc = -EIO;
		goto out;
	}

	msleep(10);
	max77696_gauge_reg_read_word(me, REMCAPREP, &rep_cap_chk);
	if (rep_cap_chk != rep_cap) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::REMCAPREP check failed\n");
		rc = max77696_gauge_reg_write_word_verify(me, REMCAPREP, rep_cap);
		if (rc) {
			printk(KERN_ERR "KERNEL: E pmic:fg init config::REMCAPREP failed\n");
			rc = -EIO;
			goto out;
		}
	}

	/* Write dQ_acc to 200% of Capacity and dP_acc to 200% */
	dq_acc = (cdata->fullcap/4);
	rc = max77696_gauge_reg_write_word_verify(me, DQACC, dq_acc);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::DQACC failed\n");
		rc = -EIO;
		goto out;
	}
	rc = max77696_gauge_reg_write_word_verify(me, DPACC, DP_ACC_200);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::DPACC failed\n");
		rc = -EIO;
		goto out;
	}
	rc = max77696_gauge_reg_write_word_verify(me, FULLCAP, cdata->batt_cap);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::FULLCAP(new) failed\n");
		rc = -EIO;
		goto out;
	}

	msleep(10);
	max77696_gauge_reg_read_word(me, FULLCAP, &full_cap);
	if (full_cap != cdata->batt_cap) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::FULLCAP check failed\n");
		rc = max77696_gauge_reg_write_word_verify(me, FULLCAP, cdata->batt_cap);
		if (rc) {
			printk(KERN_ERR "KERNEL: E pmic:fg init config::FULLCAP(new) retry failed\n");
			rc = -EIO;
			goto out;
		}
	}

	max77696_gauge_reg_write_word(me, DESIGNCAP, cdata->fullcap);
	rc = max77696_gauge_reg_write_word_verify(me, FULLCAPNOM, cdata->fullcap);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::FULLCAPNOM(new) failed\n");
		rc = -EIO;
		goto out;
	}

	/* Update SOC register with new SOC */
	max77696_gauge_reg_write_word(me, SOCREP, vfsoc);

	/* Verify Model gauge data - sanity check */
	rc = max77696_gauge_check_cdata(me);
	if (rc) {
		printk(KERN_ERR "KERNEL: E pmic:fg init config::characterization data check failed\n");
		rc = -EINVAL;
		goto out;
	}

	/* Init complete, Clear the POR bit */
	max77696_gauge_reg_read_word(me, STATUS, &sts);
	max77696_gauge_reg_write_word(me, STATUS, sts & ~FG_STATUS_POR);
out:
	return rc;
}

static void max77696_gauge_init_work (struct work_struct *work)
{
	struct max77696_gauge *me = container_of(work, struct max77696_gauge, init_work);
	int rc;

	/* Initialize registers according to values from the platform data */

	if (me->enable_por_init) {
		rc = max77696_gauge_init_config(me);
		if (rc)	{
			wario_battery_error_flags |= BATT_INI_ERROR;
			printk(KERN_ERR "KERNEL: E pmic:fg battery init::mg config init failed\n");
			return;
		}
		printk(KERN_INFO "KERNEL: I pmic:fg battery init::mg config init successful\n");
	}

	me->init_complete = true;
	max77696_gauge_update_availcap_rep(me, 1);
	max77696_gauge_update_nomfullcap(me, 1);
	max77696_gauge_update_cycle_count(me, 1);

	max77696_gauge_init_alert(me);
	if (likely(me->update_interval && me->polling_properties)) {
		schedule_delayed_work(&(me->psy_work), me->update_interval);
	}
}

static void max77696_gauge_psy_work (struct work_struct *work)
{
	struct max77696_gauge *me = __psy_work_to_max77696_gauge(work);
	bool updated = 0;

	__lock(me);

	if (unlikely(!me->update_interval || !me->polling_properties)) {
		goto out;
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_CAPACITY_AVG) {
		updated |= max77696_gauge_update_socavg(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_CAPACITY) {
		updated |= max77696_gauge_update_socrep(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_TEMP) {
		updated |= max77696_gauge_update_temp(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_VOLTAGE) {
		updated |= max77696_gauge_update_vcell(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_CURRENT_AVG) {
		updated |= max77696_gauge_update_avgcurrent(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_NAC) {
		updated |= max77696_gauge_update_availcap_rep(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_LMD) {
		updated |= max77696_gauge_update_nomfullcap(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_CYCLE) {
		updated |= max77696_gauge_update_cycle_count(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_CAPACITY_MIX) {
		updated |= max77696_gauge_update_socmix(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_NAC_AVG) {
		updated |= max77696_gauge_update_availcap_avg(me, 1);
	}

	if (me->polling_properties & MAX77696_GAUGE_POLLING_NAC_MIX) {
		updated |= max77696_gauge_update_availcap_mix(me, 1);
	}

	if (likely(updated)) {
		power_supply_changed(&(me->psy));
	}

	if ((wario_battery_temp_c < WARIO_TEMP_C_CRIT_THRESH) && wario_crittemp_event)
		wario_crittemp_event = 0;

#if defined(CONFIG_MX6SL_WARIO_WOODY)
	if ((wario_battery_temp_c < WARIO_TEMP_C_HI_THRESH) && wario_hitemp_event)
		wario_hitemp_event = 0;
#endif

	if ((wario_battery_temp_c <= WARIO_TEMP_C_LO_THRESH) ||
		(wario_battery_temp_c >= WARIO_TEMP_C_CRIT_THRESH)) {
		wario_temp_err_cnt++;
	} else {
		wario_temp_err_cnt = 0;
		wario_battery_error_flags &= ~BATT_TEMP_ERROR;
	}

	if (wario_temp_err_cnt > WARIO_ERROR_THRESH) {
		wario_battery_error_flags |= BATT_TEMP_ERROR;
		wario_temp_err_cnt = 0;
		printk(KERN_CRIT "KERNEL: E pmic:fg battery temp::out of normal operating range temp=%dC\n",
							wario_battery_temp_c);
	}

	pr_debug("Wario battery temp:%dC, volt:%dmV, current:%dmA, capacity:%d%%, mAH:%dmAh\n",
		wario_battery_temp_c, wario_battery_voltage, wario_battery_current,
		wario_battery_capacity, wario_battery_nac_mAH);

	schedule_delayed_work(&(me->psy_work), me->update_interval);

out:
	__unlock(me);
	return;
}

static void max77696_gauge_external_power_changed (struct power_supply *psy)
{
	struct max77696_gauge *me = __psy_to_max77696_gauge(psy);

	__lock(me);

	dev_dbg(me->dev, "external power changed\n");
	power_supply_changed(&(me->psy));

	__unlock(me);
}

#if defined(CONFIG_MX6SL_WARIO_BASE)
static void max77696_gauge_trigger_leds (struct max77696_gauge *me, int status)
{
	struct max77696_platform_data *chip_pdata = me->chip->dev->platform_data;
	struct max77696_gauge_led_trigger* led_trig = NULL;
	int i;

	if (unlikely(!chip_pdata->gauge_supports_led_triggers)) {
		return;
	}

	switch (status) {
		case POWER_SUPPLY_STATUS_FULL:
			led_trig = &(me->charging_full_trigger);
			break;

		case POWER_SUPPLY_STATUS_CHARGING:
			led_trig = &(me->charging_trigger);
			break;

		default:
			led_trig = &(me->default_trigger);
			break;
	}

	if (unlikely(!led_trig || !led_trig->enable)) {
		return;
	}

	for (i = 0; i < MAX77696_LED_NR_LEDS; i++) {
		struct max77696_gauge_led_control *led_ctrl = &(led_trig->control[i]);

		max77696_led_enable_manual_mode(i, led_ctrl->manual_mode);
		max77696_led_set_brightness(i, led_ctrl->brightness);
		if (led_ctrl->flashing) {
			max77696_led_set_blink(i,
					led_ctrl->flash_params.duration_ms,
					led_ctrl->flash_params.period_ms);
		} else {
			max77696_led_disable_blink(i);
		}
		max77696_led_update_changes(i);

	}
}
#endif

static int max77696_gauge_get_prop_status (struct max77696_gauge *me)
{
	int rc;

	max77696_gauge_update_socrep(me, 0);

	if (unlikely(!me->charger_online || !me->charger_enable)) {
		rc = POWER_SUPPLY_STATUS_UNKNOWN;
		goto out;
	}

	if (unlikely(!me->charger_online())) {
		rc = POWER_SUPPLY_STATUS_DISCHARGING;
		goto out;
	}

	if (unlikely(me->socrep >= me->battery_full_capacity)) {
		rc = POWER_SUPPLY_STATUS_FULL;
		goto out;
	}

	rc = (me->charger_enable()?
			POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_NOT_CHARGING);

out:
#if defined(CONFIG_MX6SL_WARIO_BASE)
	max77696_gauge_trigger_leds(me, rc);
#endif
	return rc;
}

static int max77696_gauge_get_prop_present (struct max77696_gauge *me)
{
	u16 buf;
	int rc;

	if (likely(me->battery_online)) {
		rc = (int)me->battery_online();
		goto out;
	}
	max77696_gauge_reg_read_word(me, STATUS, &buf);
	rc = (!(buf & FG_STATUS_BST));

out:
	return rc;
}

static int max77696_gauge_get_prop_cycle_count (struct max77696_gauge *me)
{
	max77696_gauge_update_cycle_count(me, 0);
	return me->cyclecnt;
}

static int max77696_gauge_get_prop_voltage_now (struct max77696_gauge *me)
{
	max77696_gauge_update_vcell(me, 0);
	return me->vcell * 1000; /* convert unit from mV to uV */
}

static int max77696_gauge_get_prop_voltage_avg (struct max77696_gauge *me)
{
	max77696_gauge_update_avgvcell(me, 0);
	return me->avgvcell * 1000; /* convert unit from mV to uV */
}

static int max77696_gauge_get_prop_charge_full_design (struct max77696_gauge *me)
{
	return me->charge_full_design;
}

/* Last Measured Discharge (mAh) */
static int max77696_gauge_get_prop_charge_full (struct max77696_gauge *me)
{
	max77696_gauge_update_nomfullcap(me, 0);
	return me->nomfullcap;
}

/* Nominal Available Charge (mAh) */
static int max77696_gauge_get_prop_charge_now (struct max77696_gauge *me)
{
	max77696_gauge_update_availcap_rep(me, 0);
	return me->availcap;
}

static int max77696_gauge_get_prop_capacity (struct max77696_gauge *me)
{
	max77696_gauge_update_socrep(me, 0);
	return me->socrep;
}

static int max77696_gauge_get_prop_temp (struct max77696_gauge *me)
{
	max77696_gauge_update_temp(me, 0);
	return me->temp; /* fahrenheit */
}

static int max77696_gauge_get_prop_current_now (struct max77696_gauge *me)
{
	u16 buf;
	int rc;

	max77696_gauge_reg_read_word(me, CURRENT, &buf);
	rc   = __s16_to_intval(buf);
	/* current in uA ; r_sns = 10000 uOhms */
	rc *= (1562500 / me->r_sns);
	/* current in mA */
	rc /= 1000;

	return rc;
}

static int max77696_gauge_get_prop_current_avg (struct max77696_gauge *me)
{
	max77696_gauge_update_avgcurrent(me, 0);
	return me->avgcurrent;
}

static int max77696_gauge_get_property (struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct max77696_gauge *me = __psy_to_max77696_gauge(psy);
	int rc = 0;

	__lock(me);
	if (!me->init_complete) {
		rc = -ENODATA;
		goto out;
	}

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = max77696_gauge_get_prop_status(me);
			break;

		case POWER_SUPPLY_PROP_PRESENT:
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = max77696_gauge_get_prop_present(me);
			break;

		case POWER_SUPPLY_PROP_CYCLE_COUNT:
			val->intval = max77696_gauge_get_prop_cycle_count(me);
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = max77696_gauge_get_prop_voltage_now(me) / 1000;
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_AVG:
			val->intval = max77696_gauge_get_prop_voltage_avg(me) / 1000;
			break;

		case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
			val->intval = max77696_gauge_get_prop_charge_full_design(me);
			break;

		case POWER_SUPPLY_PROP_CHARGE_FULL: /* Last Measured Discharge */
			val->intval = max77696_gauge_get_prop_charge_full(me);
			break;

		case POWER_SUPPLY_PROP_CHARGE_NOW: /* Nominal Available Charge */
			val->intval = max77696_gauge_get_prop_charge_now(me);
			break;

		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = max77696_gauge_get_prop_capacity(me);
			break;

		case POWER_SUPPLY_PROP_TEMP:
			val->intval = max77696_gauge_get_prop_temp(me);
			break;

		case POWER_SUPPLY_PROP_CURRENT_NOW:
			if (unlikely(!me->enable_current_sense)) {
				rc = -EINVAL;
				goto out;
			}
			val->intval = max77696_gauge_get_prop_current_now(me);
			break;

		case POWER_SUPPLY_PROP_CURRENT_AVG:
			if (unlikely(!me->enable_current_sense)) {
				rc = -EINVAL;
				goto out;
			}
			val->intval = max77696_gauge_get_prop_current_avg(me);
			break;

		default:
			rc = -EINVAL;
			break;
	}

out:
	__unlock(me);
	return rc;
}

static enum power_supply_property max77696_gauge_psy_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
};

static __always_inline
void __show_led_trigger_config (struct max77696_gauge *me, const char* name,
		struct max77696_gauge_led_trigger* led_trig)
{
#ifdef DEBUG
	int i;

	if (!led_trig->enable) {
		dev_dbg(me->dev, "%s trigger: (disabled)\n", name);
		return;
	}

	dev_dbg(me->dev, "%s trigger:\n", name);

	for (i = 0; i < MAX77696_LED_NR_LEDS; i++) {
		struct max77696_gauge_led_control *led_ctrl = &(led_trig->control[i]);

		dev_dbg(me->dev, "  LED[%u]\n", i);
		dev_dbg(me->dev, "    mode           %s\n",
				(led_ctrl->manual_mode? "manual" : "autonomous"));
		dev_dbg(me->dev, "    brightness     %d\n", led_ctrl->brightness);
		if (led_ctrl->flashing) {
			dev_dbg(me->dev, "    flashing       enable\n");
			dev_dbg(me->dev, "    flash duration %lumsec\n",
					led_ctrl->flash_params.duration_ms);
			dev_dbg(me->dev, "    flash period   %lumsec\n",
					led_ctrl->flash_params.period_ms);
		} else {
			dev_dbg(me->dev, "    flashing       disable\n");
		}
	}
#endif /* DEBUG */
}

static int __init wario_battery_check_disable(char *str)
{
#ifdef DEVELOPMENT_MODE
	wario_battery_check_disabled = 1;
#endif
	return 1;
}
__setup("no_battery_check", wario_battery_check_disable);


static __devinit int max77696_gauge_probe (struct platform_device *pdev)
{
	struct max77696_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77696_gauge_platform_data *pdata = pdev->dev.platform_data;
	struct max77696_gauge *me;
	int i, rc;
	u16 val;

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
	me->kobj = &(pdev->dev.kobj);

	me->enable_current_sense  = pdata->enable_current_sense;
	me->charge_full_design    = pdata->charge_full_design;
	me->battery_full_capacity = pdata->battery_full_capacity;
	me->r_sns                 = ((pdata->r_sns == 0)?
			GAUGE_DEFAULT_SNS_RESISTOR : pdata->r_sns);
	me->update_interval       = msecs_to_jiffies(pdata->update_interval_ms);
	me->update_interval_relax = msecs_to_jiffies(pdata->update_interval_relax_ms);
	me->polling_properties    = pdata->polling_properties;
	me->battery_online        = pdata->battery_online;
	me->charger_online        = pdata->charger_online;
	me->charger_enable        = pdata->charger_enable;
	me->enable_por_init       = pdata->enable_por_init;
	me->por_bit_set           = 0;
	me->v_alert_max           = pdata->v_alert_max;
	me->v_alert_min           = pdata->v_alert_min;
	me->t_alert_max           = pdata->t_alert_max;
	me->t_alert_min           = pdata->t_alert_min;
	me->s_alert_max           = pdata->s_alert_max;
	me->s_alert_min           = pdata->s_alert_min;
	me->alert_on_battery_removal = pdata->enable_alert_on_battery_removal;
	me->alert_on_battery_insertion = pdata->enable_alert_on_battery_insertion;
	me->lobat_interval        = msecs_to_jiffies(LOBAT_WORK_INTERVAL);

	memcpy(&(me->default_trigger), &(pdata->default_trigger),
			sizeof(me->default_trigger));
	__show_led_trigger_config(me,
			"default", &(me->default_trigger));

	memcpy(&(me->charging_trigger), &(pdata->charging_trigger),
			sizeof(me->charging_trigger));
	__show_led_trigger_config(me,
			"charging", &(me->charging_trigger));

	memcpy(&(me->charging_full_trigger), &(pdata->charging_full_trigger),
			sizeof(me->charging_full_trigger));
	__show_led_trigger_config(me,
			"charging-full", &(me->charging_full_trigger));

	me->irq = chip->irq_base + MAX77696_ROOTINT_FG;

	BUG_ON(chip->gauge_ptr);
	chip->gauge_ptr = me;

	max77696_gauge_update_socrep(me, 1);
	max77696_gauge_update_socavg(me, 1);
	max77696_gauge_update_vcell(me, 1);
	max77696_gauge_update_avgcurrent(me, 1);
	max77696_gauge_update_temp(me, 1);
    max77696_gauge_update_avgvcell(me, 1);

	me->psy.name                   = MAX77696_PSY_BATT_NAME;
	me->psy.type                   = POWER_SUPPLY_TYPE_BATTERY;
	me->psy.external_power_changed = max77696_gauge_external_power_changed;
	me->psy.get_property           = max77696_gauge_get_property;
	me->psy.properties             = max77696_gauge_psy_props;
	me->psy.num_properties         = ARRAY_SIZE(max77696_gauge_psy_props);

	g_max77696_fg = me;

	if (lab126_board_is(BOARD_ID_WARIO) ||
			lab126_board_is(BOARD_ID_WOODY)) {
		g_max77696_fg_cdata = &gauge_cdata_wario;
	} else if (lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_P5) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_P5) ||
		lab126_board_is(BOARD_ID_ICEWINE_WARIO_512) ||
		lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO_512))  {
		g_max77696_fg_cdata = &gauge_cdata_icewine_435V;
	} else if (lab126_board_is(BOARD_ID_ICEWINE_WARIO_P5) ||
		lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO_P5))  {
		g_max77696_fg_cdata = &gauge_cdata_icewine_proto_435V;
	} else if (lab126_board_is(BOARD_ID_ICEWINE_WARIO) ||
		lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO))  {
		g_max77696_fg_cdata = &gauge_cdata_icewine_42V;
	} else if (lab126_board_is(BOARD_ID_PINOT) || 
		lab126_board_is(BOARD_ID_PINOT_2GB) ||
		lab126_board_is(BOARD_ID_PINOT_WFO_2GB) ||
		lab126_board_is(BOARD_ID_PINOT_WFO) ||
		lab126_board_is(BOARD_ID_MUSCAT_WAN) ||
		lab126_board_is(BOARD_ID_MUSCAT_WFO) ||
		lab126_board_is(BOARD_ID_MUSCAT_32G_WFO)  ) {
		g_max77696_fg_cdata = &gauge_cdata_pinot;
	} else if (lab126_board_is(BOARD_ID_BOURBON_WFO) ||
		lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2)) {
		g_max77696_fg_cdata = &gauge_cdata_bourbon;
	} else if (lab126_board_is(BOARD_ID_WHISKY_WFO) || 
		lab126_board_is(BOARD_ID_WHISKY_WAN)) {
		g_max77696_fg_cdata = &gauge_cdata_whisky;
	} else {
		rc = -EINVAL;
		dev_err(me->dev, "MG characterization data not found, check boardid [%d]\n", rc);
		goto out_err_cdata;
	}

	INIT_DELAYED_WORK(&(me->psy_work), max77696_gauge_psy_work);
	INIT_DELAYED_WORK(&(me->lobat_work), max77696_gauge_lobat_work);
	INIT_DELAYED_WORK(&(me->batt_check_work), wario_battery_check_handler);

	/* When current is not measured,
	 * CURRENT_NOW and CURRENT_AVG properties should be invisible.
	 */
	if (!me->enable_current_sense) {
		me->psy.num_properties -= 2;
	}

	rc = power_supply_register(me->dev, &(me->psy));
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to register psy device [%d]\n", rc);
		goto out_err_reg_psy;
	}

	/* Initialize registers according to values from the platform data */
	if (likely(pdata->init_data)) {
		for (i = 0; i < pdata->num_init_data; i++) {
			u16 buf = __cpu_to_le16(pdata->init_data[i].data);
			max77696_bulk_write(me->i2c,
					pdata->init_data[i].addr, (u8*)(&buf), 2);
		}
	}

	if (likely(!me->enable_current_sense)) {
		max77696_gauge_reg_write_word(me, CGAIN,    0x0000);
		max77696_gauge_reg_write_word(me, MISCCFG,  0x0003);
		max77696_gauge_reg_write_word(me, LEARNCFG, 0x0007);
	} else {
		if (lab126_board_is(BOARD_ID_WHISKY_WFO) || 
			lab126_board_is(BOARD_ID_WHISKY_WAN)) 
			max77696_gauge_reg_write_word(me, MISCCFG,  0x0A11);	/* 10mA correction; SOC alerts based on AV */
		else 
			max77696_gauge_reg_write_word(me, MISCCFG,  0x0A10);	/* 10mA correction */
	}

	max77696_gauge_disable_irq(me,
			FG_CONFIG_BER | FG_CONFIG_BEI | FG_CONFIG_AEN, 1);

	max77696_gauge_reg_read_word(me, STATUS, &val);
	if (val & FG_STATUS_POR) {
		me->por_bit_set = 1;
		INIT_WORK(&me->init_work, max77696_gauge_init_work);
		schedule_work(&me->init_work);
	} else {
		me->init_complete = true;
		max77696_gauge_update_availcap_rep(me, 1);
		max77696_gauge_update_nomfullcap(me, 1);
		max77696_gauge_update_cycle_count(me, 1);
		max77696_gauge_init_alert(me);
		if (likely(me->update_interval && me->polling_properties)) {
			schedule_delayed_work(&(me->psy_work), me->update_interval);
		}
	}

	schedule_delayed_work(&(me->batt_check_work), msecs_to_jiffies(BATT_ID_CHECK_INIT_INTERVAL));

	rc = max77696_gauge_sysdev_register();
	if (unlikely(rc)) {
		dev_err(me->dev, "failed to create gauge attribute group [%d]\n", rc);
		goto out_err;
	}

	pr_info(DRIVER_DESC" "DRIVER_VERSION" Installed\n");
	SUBDEVICE_SET_LOADED(gauge, chip);
	return 0;

out_err:
	cancel_work_sync(&me->init_work);
	cancel_delayed_work_sync(&(me->batt_check_work));
	cancel_delayed_work_sync(&(me->lobat_work));
	cancel_delayed_work_sync(&(me->psy_work));
	power_supply_unregister(&(me->psy));
out_err_reg_psy:
	g_max77696_fg_cdata = NULL;
out_err_cdata:
	g_max77696_fg = NULL;
	mutex_destroy(&(me->lock));
	kfree(me);
	platform_set_drvdata(pdev, NULL);
	return rc;
}

static __devexit int max77696_gauge_remove (struct platform_device *pdev)
{
	struct max77696_gauge *me = platform_get_drvdata(pdev);

	max77696_gauge_sysdev_unregister();
	me->chip->gauge_ptr = NULL;

	cancel_work_sync(&me->init_work);
	cancel_delayed_work_sync(&(me->batt_check_work));
	cancel_delayed_work_sync(&(me->lobat_work));
	cancel_delayed_work_sync(&(me->psy_work));

	power_supply_unregister(&(me->psy));

	g_max77696_fg = NULL;
	g_max77696_fg_cdata = NULL;
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&(me->lock));
	kfree(me);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77696_gauge_suspend(struct device *dev)
{
	struct max77696_gauge *me = dev_get_drvdata(dev);
	struct max77696_gauge_config_data *cdata = g_max77696_fg_cdata;
	int rc = 0;
	u16 dq_acc;

	if (cdata == NULL) {
		printk(KERN_ERR "KERNEL: E pmic:fg suspend::characterization data not found\n");
		return -EINVAL;
	}

	/* Don't suspend on low voltage
	 * since the PMIC's LOBAT/CRITBAT will shut us down soon
	 */
	if ( (wario_battery_voltage > 0) && (wario_battery_voltage <= SYS_SUSPEND_LOW_VOLT_THRESH))
		return -EBUSY;

	printk(KERN_INFO "KERNEL: I pmic:fg suspend battinfo:current=%dmA, volt=%04dmV, capav=%03d%%, mAh=%dmAh:\n",
			wario_battery_current, wario_battery_voltage, wario_battery_capacity, wario_battery_nac_mAH);

	rc = max77696_gauge_reg_read_word(me, FULLCAP, &fg_saved_fullcap);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg suspend::FULLCAP failed\n");
		return -EIO;
	}

	/* Write dQ_acc to 200% of Capacity and dP_acc to 200% */
	dq_acc = (cdata->fullcap/4);
	rc = max77696_gauge_reg_write_word_verify(me, DQACC, dq_acc);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg suspend::DQACC failed\n");
		return -EIO;
	}

	rc = max77696_gauge_reg_write_word_verify(me, DPACC, DP_ACC_200);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg suspend::DPACC failed\n");
		return -EIO;
	}

	rc = max77696_gauge_repcap_adj(me);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg suspend::REPCAP(adj with AVCAP) failed\n");
		return -EIO;
	}

	cancel_delayed_work_sync(&(me->lobat_work));
	cancel_delayed_work_sync(&(me->psy_work));

#ifdef CONFIG_FALCON
	/* restrict FG sanity checks & restore logic to hibernate suspend/resume */
	if(in_falcon()) {
		int i = 0;
		/* Save cell learned info before suspend/hibernate */
	   	printk(KERN_INFO "Saving learned params as we seem to loose it!sometimes!");
		memset(system_battlearn, 0x0, sizeof(system_battlearn));
		for(i = 0; i < MAX77696_GAUGE_LEARNED_NR_INFOS; i++) {
			max77696_gauge_read_learned(i, &system_battlearn[i]);
		}
	}
#endif
	return 0;
}

static int max77696_gauge_resume(struct device *dev)
{
	struct max77696_gauge *me = dev_get_drvdata(dev);
	struct max77696_gauge_config_data *cdata = g_max77696_fg_cdata;
	int rc = 0;
	u16 dq_acc;

	if (cdata == NULL) {
		printk(KERN_ERR "KERNEL: E pmic:fg resume::characterization data not found\n");
		return -EINVAL;
	}

#ifdef CONFIG_FALCON
	/* restrict FG sanity checks & restore logic to hibernate suspend/resume */
	if(in_falcon()) {
		uint16_t readlearned[BATTLEARN_REG_BYTES];
		int i = 0;
		u16 val;

		/* restore battery characterization if we have an unintended por bit set */
		max77696_gauge_reg_read_word(me, STATUS, &val);
		if (val & FG_STATUS_POR) {

			printk(KERN_CRIT "KERNEL: I pmic:fg resume::this should not happen! unless POR "
				"was set during a hibernate/suspend!, we'll try restoring battery "
				"characterization");

			rc = max77696_gauge_init_config(me);
			if (rc) {
				wario_battery_error_flags |= BATT_INI_ERROR;
				printk(KERN_ERR "KERNEL: E pmic:fg resume::mg config init failed\n");
				/* better to bail out here instead of proceeding with
				 * messed up battery */
				return rc;
			}
			printk(KERN_INFO "KERNEL: I pmic:fg resume::mg config init successful\n");
		}

		/* restore learned params if there is a mismatch */
		memset(readlearned, 0, sizeof(readlearned));
		for (i = 0; i < BATTLEARN_REG_BYTES; i++) {
			max77696_gauge_read_learned(i, &readlearned[i]);
		}

		for (i = 0; i < BATTLEARN_REG_BYTES; i++) {
			if (readlearned[i] != system_battlearn[i]) {
				printk(KERN_ERR "KERNEL: E pmic:fg resume::Learned param "
					"changed during FSHDN! read and saved values [%d]: "
					"0x%x 0x%x", i, readlearned[i], system_battlearn[i]);
	
				printk(KERN_INFO "KERNEL: I pmic:fg resume::restoring it to saved...");
				max77696_gauge_write_learned(i, system_battlearn[i]);
			}
		}
	}
#endif

	max77696_gauge_update_avgcurrent(me, 1);
	max77696_gauge_update_vcell(me, 1);	
	max77696_gauge_update_socavg(me, 1);
	max77696_gauge_update_availcap_rep(me, 1);
	printk(KERN_INFO "KERNEL: I pmic:fg resume battinfo:current=%dmA, volt=%04dmV, capav=%03d%%, mAh=%dmAh:\n",
			wario_battery_current, wario_battery_voltage, wario_battery_capacity, wario_battery_nac_mAH);

	/* Write dQ_acc to 200% of Capacity and dP_acc to 200% */
	dq_acc = (cdata->fullcap/4);
	rc = max77696_gauge_reg_write_word_verify(me, DQACC, dq_acc);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg resume::DQACC failed\n");
		return -EIO;
	}

	rc = max77696_gauge_reg_write_word_verify(me, DPACC, DP_ACC_200);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg resume::DPACC failed\n");
		return -EIO;
	}

	rc = max77696_gauge_reg_write_word_verify(me, FULLCAP, fg_saved_fullcap);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg resume::FULLCAP(saved) failed\n");
		return -EIO;
	}

	rc = max77696_gauge_repcap_adj(me);
	if (unlikely(rc)) {
		printk(KERN_ERR "KERNEL: E pmic:fg resume::REPCAP(adj with AVCAP) failed\n");
		return -EIO;
	}

	if (likely(me->init_complete)) {
		if (likely(me->polling_properties)) {
			schedule_delayed_work(&(me->psy_work), msecs_to_jiffies(BATT_RESUME_INTERVAL));
		}
	}
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(max77696_gauge_pm, max77696_gauge_suspend, max77696_gauge_resume);

static struct platform_driver max77696_gauge_driver = {
	.driver.name  = DRIVER_NAME,
	.driver.owner = THIS_MODULE,
	.driver.pm    = &max77696_gauge_pm,
	.probe        = max77696_gauge_probe,
	.remove       = __devexit_p(max77696_gauge_remove),
};

static __init int max77696_gauge_driver_init (void)
{
	return platform_driver_register(&max77696_gauge_driver);
}

static __exit void max77696_gauge_driver_exit (void)
{
	platform_driver_unregister(&max77696_gauge_driver);
}

module_init(max77696_gauge_driver_init);
module_exit(max77696_gauge_driver_exit);

/*******************************************************************************
 * EXTERNAL FUNCTIONS
 ******************************************************************************/

extern struct max77696_chip* max77696;

void max77696_gauge_write_learned (int id, u16 learned)
{
	struct max77696_chip *chip = max77696;
	struct max77696_gauge *me = chip->gauge_ptr;

	__lock(me);
	max77696_gauge_learned_write(me, id, learned);
	__unlock(me);
}
EXPORT_SYMBOL(max77696_gauge_write_learned);

void max77696_gauge_read_learned (int id, u16 *learned)
{
	struct max77696_chip *chip = max77696;
	struct max77696_gauge *me = chip->gauge_ptr;

	__lock(me);
	max77696_gauge_learned_read(me, id, learned);
	__unlock(me);
}
EXPORT_SYMBOL(max77696_gauge_read_learned);

void max77696_gauge_write_learned_all ( u16* learned)
{
	struct max77696_chip *chip = max77696;
	struct max77696_gauge *me = chip->gauge_ptr;

	max77696_gauge_learned_write_all(me, learned);

}
EXPORT_SYMBOL(max77696_gauge_write_learned_all);

void max77696_gauge_read_learned_all ( u16 *learned)
{
	struct max77696_chip *chip = max77696;
	struct max77696_gauge *me = chip->gauge_ptr;
	max77696_gauge_learned_read_all(me, learned);
}
EXPORT_SYMBOL(max77696_gauge_read_learned_all);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);

