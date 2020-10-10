/* 
 * include/linux/cyttsp.h
 *
 * Copyright (C) 2009, 2010 Cypress Semiconductor, Inc.
 *
 * Copyright 2011 Amazon.com, Inc. All rights reserved.
 * Vidhyananth Venkatasamy (venkatas@lab126.com)
 *
 * Cypress TrueTouch(TM) Standard Product touchscreen driver.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include <linux/input.h>

#ifndef _CYTTSP_H_
#define _CYTTSP_H_

#include <linux/input.h>

#define CY_DRIVER_NAME "cyttsp"
#define CY_SPI_NAME "cyttsp-spi"
#define CY_I2C_NAME "cyttsp-i2c"
/* Scan Type selection for finger and/or stylus activation */
#define CY_SCN_TYP_DFLT 0x01 /* finger only mutual scan */
#define CY_SCN_TYP_MUT_SELF 0x03 /*self scan and mutual scan*/
#define CY_SCN_TYP_SELF 0x02 /*self scan*/
#define CY_SCN_TYP_MUTUAL 0x01 /*mutual scan*/

#define CYTTSP_I2C_ADDRESS 0x24

/* Active Power state scanning/processing refresh interval */
#define CY_ACT_INTRVL_DFLT 0x10 /* 16x1 ms */
/* touch timeout for the Active power */
#define CY_TCH_TMOUT_DFLT 0x64 /* 100x10 ms */
/* Low Power state scanning/processing refresh interval */
#define CY_LP_INTRVL_DFLT 0x03 /* 3x10 ms */
/*
 *defines for Gen2 (Txx2xx); Gen3 (Txx3xx)
 * use these defines to set cyttsp_platform_data.gen in board config file
 */
enum cyttsp_gen {
	CY_GEN2,
	CY_GEN3,
};
/*
 * Active distance in pixels for a gesture to be reported
 * if set to 0, then all gesture movements are reported
 * Valid range is 0 - 15
 */
#define CY_ACT_DIST_DFLT 8
#define CY_ACT_DIST CY_ACT_DIST_DFLT
/* max retries for read/write ops */
#define CY_NUM_RETRY 26

enum cyttsp_gest {
	CY_GEST_GRP_NONE = 0,
	CY_GEST_GRP1 =	0x10,
	CY_GEST_GRP2 = 0x20,
	CY_GEST_GRP3 = 0x40,
	CY_GEST_GRP4 = 0x80,
};

enum cyttsp_powerstate {
	CY_IDLE_STATE,
	CY_ACTIVE_STATE,
	CY_LOW_PWR_STATE,
	CY_SLEEP_STATE,
};

enum {
	REG_HST_MODE = 0x0,
	REG_MFG_STAT,
	REG_MFG_CMD,
	REG_MFG_REG0,
	REG_MFG_REG1,
	REG_SYS_SCN_TYP = 0x1c,
};

struct cyttsp_platform_data {
	u32 maxx;
	u32 maxy;
	u32 flags;
	enum cyttsp_gen gen;
	unsigned use_st:1;
	unsigned use_mt:1;
	unsigned use_trk_id:1;
	unsigned use_hndshk:1;
	unsigned use_timer:1;
	unsigned use_sleep:1;
	unsigned use_gestures:1;
	unsigned use_load_file:1;
	unsigned use_force_fw_update:1;
	unsigned use_virtual_keys:1;
	unsigned use_lpm:1;
	enum cyttsp_powerstate power_state;
	u8 gest_set;
	u8 scn_typ;     /* finger and/or stylus scanning */
	u8 act_intrvl;  /* Active refresh interval; ms */
	u8 tch_tmout;   /* Active touch timeout; ms */
	u8 lp_intrvl;   /* Low power refresh interval; ms */
	int (*wakeup)(void);
	int (*init)(int on_off);
	void (*mt_sync)(struct input_dev *);
	char *name;
	s16 irq_gpio;
	
};

#define DBG(x)  
#define DBG2(x)  
#define DBG3(x)  

/* rely on kernel input.h to define Multi-Touch capability */
#ifndef ABS_MT_TRACKING_ID
/* define only if not defined already by system; */
/* value based on linux kernel 2.6.30.10 */
#define ABS_MT_TRACKING_ID (ABS_MT_BLOB_ID + 1)
#endif /* ABS_MT_TRACKING_ID */

#define	TOUCHSCREEN_TIMEOUT (msecs_to_jiffies(28))
/* Bootloader File 0 offset */
#define CY_BL_FILE0       0x00
/* Bootloader command directive */
#define CY_BL_CMD         0xFF
/* Bootloader Enter Loader mode */
#define CY_BL_ENTER       0x38
/* Bootloader Write a Block */
#define CY_BL_WRITE_BLK   0x39
/* Bootloader Terminate Loader mode */
#define CY_BL_TERMINATE   0x3B
/* Bootloader Exit and Verify Checksum command */
#define CY_BL_EXIT        0xA5
/* Bootloader default keys */
#define CY_BL_KEY0 0
#define CY_BL_KEY1 1
#define CY_BL_KEY2 2
#define CY_BL_KEY3 3
#define CY_BL_KEY4 4
#define CY_BL_KEY5 5
#define CY_BL_KEY6 6
#define CY_BL_KEY7 7

#define CY_DIFF(m, n)          		((m) != (n))
#define GET_NUM_TOUCHES(x)     		((x) & 0x0F)
#define GET_TOUCH1_ID(x)       		(((x) & 0xF0) >> 4)
#define GET_TOUCH2_ID(x)       		((x) & 0x0F)
#define GET_TOUCH3_ID(x)       		(((x) & 0xF0) >> 4)
#define GET_TOUCH4_ID(x)       		((x) & 0x0F)
#define IS_LARGE_AREA(x)       		(((x) & 0x10) >> 4)
#define IS_BAD_PKT(x)          		((x) & 0x20)
#define FLIP_DATA_FLAG         		0x01
#define REVERSE_X_FLAG         		0x02
#define REVERSE_Y_FLAG         		0x04
#define FLIP_DATA(flags)       		((flags) & FLIP_DATA_FLAG)
#define REVERSE_X(flags)       		((flags) & REVERSE_X_FLAG)
#define REVERSE_Y(flags)       		((flags) & REVERSE_Y_FLAG)
#define FLIP_XY(x, y)     	   		{typeof(x) tmp; tmp = (x); (x) = (y); (y) = tmp; }
#define INVERT_X(x, xmax)      		((xmax) - (x))
#define INVERT_Y(y, ymax)      		((ymax) - (y))
#define SET_HSTMODE(reg, mode) 		((reg) & (mode))
#define GET_HSTMODE(reg)       		((reg & 0x70) >> 4)
#define GET_BOOTLOADERMODE(reg)		((reg & 0x10) >> 4)

/* maximum number of concurrent ST track IDs */
#define CY_NUM_ST_TCH_ID            2
/* maximum number of concurrent MT track IDs */
#define CY_NUM_MT_TCH_ID            4
/* maximum number of track IDs */
#define CY_NUM_TRK_ID               16

#define CY_NTCH             		0 /* lift off */
#define CY_TCH              		1 /* touch down */
#define CY_ST_FNGR1_IDX     		0
#define CY_ST_FNGR2_IDX     		1
#define CY_MT_TCH1_IDX      		0
#define CY_MT_TCH2_IDX      		1
#define CY_MT_TCH3_IDX      		2
#define CY_MT_TCH4_IDX      		3
#define CY_XPOS             		0
#define CY_YPOS             		1
#define CY_IGNR_TCH         		(-1)
#define CY_SMALL_TOOL_WIDTH 		10
#define CY_LARGE_TOOL_WIDTH 		255
#define CY_REG_BASE         		0x00
#define CY_REG_GEST_SET     		0x1E
#define CY_REG_SCN_TYP      		0x1C
#define CY_REG_ACT_INTRVL   		0x1D
#define CY_REG_TCH_TMOUT    		(CY_REG_ACT_INTRVL+1)
#define CY_REG_LP_INTRVL    		(CY_REG_TCH_TMOUT+1)
#define CY_SOFT_RESET       		(1 << 0)
#define CY_DEEP_SLEEP       		(1 << 1)
#define CY_LOW_POWER        		(1 << 2)
#define CY_MAXZ             		255
#define CY_OK               		0
#define CY_INIT             		1
#define CY_DELAY_DFLT       		10 /* ms */
#define CY_DELAY_SYSINFO    		20 /* ms */
#define CY_DELAY_BL         		300
#define CY_DELAY_DNLOAD     		100 /* ms */
#define CY_HNDSHK_BIT       		0x80

#define CY_REG_VENDOR_ID			0x1D
#define CYTTSP_CANDO_PANEL			0x00
#define CYTTSP_TPK_PANEL			0x01
#define CYTTSP_UNKNOWN_PANEL		0xFF

/* device mode bits */
#define CY_OPERATE_MODE     		0x00
#define CY_SYSINFO_MODE     		0x10

/* raw data for all sensors */
#define CY_TEST_MODE_RAW_COUNTS		0x40
/* difference counts for all sensors */
#define CY_TEST_MODE_DIFF_COUNTS	0x50
/* interleaved local and global IDAC values */
#define CY_TEST_MODE_IDAC_SETTINGS	0x60
/* interleaved baseline and raw data for all sensors*/
#define CY_TEST_MODE_INT_RC_BASE	0x70

#define CY_TT_MODE_REG 0x01

#define CY_NUM_TEST_MODE_REGS 255

/* power mode select bits */		
#define CY_SOFT_RESET_MODE  		0x01 /* return to Bootloader mode */
#define CY_DEEP_SLEEP_MODE  		0x02
#define CY_LOW_POWER_MODE   		0x04
#define CY_NUM_KEY          		8
#define CY_PROC_NAME                "touch" // Proc name
#define CY_PROC_CMD_LEN             50   // Proc max command length

struct cyttsp_bus_ops {
	s32 (*write)(void *handle, u8 addr, u8 length, const void *values);
	s32 (*read)(void *handle, u8 addr, u8 length, void *values);
	s32 (*ext)(void *handle, void *values);
};

/* TrueTouch Standard Product Gen3 (Txx3xx) interface definition */
struct cyttsp_xydata {
	u8 hst_mode;
	u8 tt_mode;
	u8 tt_stat;
	u16 x1 __attribute__ ((packed));
	u16 y1 __attribute__ ((packed));
	u8 z1;
	u8 touch12_id;
	u16 x2 __attribute__ ((packed));
	u16 y2 __attribute__ ((packed));
	u8 z2;
};

struct cyttsp_xydata_gen2 {
	u8 hst_mode;
	u8 tt_mode;
	u8 tt_stat;
	u16 x1 __attribute__ ((packed));
	u16 y1 __attribute__ ((packed));
	u8 z1;
	u8 evnt_idx;
	u16 x2 __attribute__ ((packed));
	u16 y2 __attribute__ ((packed));
	u8 tt_undef1;
	u8 gest_cnt;
	u8 gest_id;
	u8 tt_undef[14];
	u8 gest_set;
	u8 tt_reserved;
};

/* TrueTouch Standard Product Gen2 (Txx2xx) interface definition */
enum cyttsp_gen2_std {
	CY_GEN2_NOTOUCH = 0x03, /* Both touches removed */
	CY_GEN2_GHOST =   0x02, /* ghost */
	CY_GEN2_2TOUCH =  0x03, /* 2 touch; no ghost */
	CY_GEN2_1TOUCH =  0x01, /* 1 touch only */
	CY_GEN2_TOUCH2 =  0x01, /* 1st touch removed; 2nd touch remains */
};

/* TTSP System Information interface definition */
struct cyttsp_sysinfo_data {
	u8 hst_mode;
	u8 mfg_stat;
	u8 mfg_cmd;
	u8 cid[3];
	u8 tt_undef1;
	u8 uid[8];
	u8 bl_verh;
	u8 bl_verl;
	u8 tts_verh;
	u8 tts_verl;
	u8 app_idh;
	u8 app_idl;
	u8 app_verh;
	u8 app_verl;
	u8 tt_undef[5];
	u8 scn_typ;	/* Gen3 only: scan type [0:Mutual, 1:Self] */
	u8 act_intrvl;
	u8 tch_tmout;
	u8 lp_intrvl;
};

/* TTSP Bootloader Register Map interface definition */
#define CY_BL_CHKSUM_OK 0x01
struct cyttsp_bootloader_data {
	u8 bl_file;
	u8 bl_status;
	u8 bl_error;
	u8 blver_hi;
	u8 blver_lo;
	u8 bld_blver_hi;
	u8 bld_blver_lo;
	u8 ttspver_hi;
	u8 ttspver_lo;
	u8 appid_hi;
	u8 appid_lo;
	u8 appver_hi;
	u8 appver_lo;
	u8 cid_0;
	u8 cid_1;
	u8 cid_2;
};

#define cyttsp_wake_data cyttsp_xydata

struct cyttsp {
	struct device *pdev;
	int irq;
	struct input_dev *input;
	struct work_struct work;
	struct timer_list timer;
	struct mutex mutex;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	char phys[32];
	struct cyttsp_platform_data *platform_data;
	struct cyttsp_bootloader_data bl_data;
	struct cyttsp_sysinfo_data sysinfo_data;
	u8 num_prv_st_tch;
	u16 act_trk[CY_NUM_TRK_ID];
	u16 prv_mt_tch[CY_NUM_MT_TCH_ID];
	u16 prv_st_tch[CY_NUM_ST_TCH_ID];
	u16 prv_mt_pos[CY_NUM_TRK_ID][2];
	struct cyttsp_bus_ops *bus_ops;
	unsigned fw_loader_mode:1;
	unsigned suspended:1;
	struct timer_list to_timer;
	bool to_timeout;
	bool bl_ready;
	u8 reg_id;
	bool active_irq_enabled;
	u8 device_mode;
	u8 scn_typ;
};

struct cyttsp_track_data {
	u8 prv_tch;
	u8 cur_tch;
	u16 tmp_trk[CY_NUM_MT_TCH_ID];
	u16 snd_trk[CY_NUM_MT_TCH_ID];
	u16 cur_trk[CY_NUM_TRK_ID];
	u16 cur_st_tch[CY_NUM_ST_TCH_ID];
	u16 cur_mt_tch[CY_NUM_MT_TCH_ID];
	/* if NOT CY_USE_TRACKING_ID then only */
	/* uses CY_NUM_MT_TCH_ID positions */
	u16 cur_mt_pos[CY_NUM_TRK_ID][2];
	/* if NOT CY_USE_TRACKING_ID then only */
	/* uses CY_NUM_MT_TCH_ID positions */
	u8 cur_mt_z[CY_NUM_TRK_ID];
	u8 tool_width;
	u16 st_x1;
	u16 st_y1;
	u8 st_z1;
	u16 st_x2;
	u16 st_y2;
	u8 st_z2;
};

static const u8 bl_cmd[] = {
	CY_BL_FILE0, CY_BL_CMD, CY_BL_EXIT,
	CY_BL_KEY0, CY_BL_KEY1, CY_BL_KEY2,
	CY_BL_KEY3, CY_BL_KEY4, CY_BL_KEY5,
	CY_BL_KEY6, CY_BL_KEY7
};

#define LOCK(m) do { \
	DBG(printk(KERN_INFO "%s: lock\n", __func__);) \
	mutex_lock(&(m)); \
} while (0);

#define UNLOCK(m) do { \
	DBG(printk(KERN_INFO "%s: unlock\n", __func__);) \
	mutex_unlock(&(m)); \
} while (0);

static void print_data_block(const char *func, u8 command,
			u8 length, void *data, int tries)
{
	char buf[1024];
	unsigned buf_len = sizeof(buf);
	char *p = buf;
	int i;
	int l;

	l = snprintf(p, buf_len, "cmd 0x%x: ", command);
	buf_len -= l;
	p += l;
	for (i = 0; i < length && buf_len; i++, p += l, buf_len -= l)
		l = snprintf(p, buf_len, "%02x ", *((char *)data + i));
	printk(KERN_DEBUG "%d %s: %s\n", tries, func, buf);
}

enum CY_TE_REGS{
	TE_REG_HST_MODE = 0x0,
	TE_REG_TT_MODE,
	TE_REG_TT_STAT,
};

enum CY_DEVICE_MODE{
	CY_OP_MODE = 0x0,
	CY_SI_MODE,
	CY_TE_MODE,
	CY_BL_MODE,
};

/* Debug Macros */

// Set this to 1 for DEBUG output
#define _CYDEBUG 0 
#if _CYDEBUG > 0
#    define DEBUG_INFO(fmt, args...) printk(KERN_INFO "[CYTTSP] " fmt, ## args)
#else
#    define DEBUG_INFO(fmt, args...)
#endif
#    define DEBUG_ERR(fmt, args...)  printk(KERN_ERR "[CYTTSP] ERROR: " fmt, ## args)

/* Errors Messages */
#define CY_ERR_I2C_ADD              "Could not add I2C driver\n"

#define CY_DEV_MINOR                160  // Device minor

const u8 NORMAL_CALIBRATE[3] =  {0x20, 0x0, 0x0};
const u8 OPEN_TEST_CALIBRATE[3] = {0x20, 0xC3, 0x0};

#endif /* _CYTTSP_H_ */

