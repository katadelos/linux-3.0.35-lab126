/*
 * cyttsp4_regs.h
 * Cypress TrueTouch(TM) Standard Product V4 registers.
 * For use with Cypress Txx4xx parts.
 * Supported parts include:
 * TMA4XX
 * TMA1036
 *
 * Copyright (C) 2012 Cypress Semiconductor
 * Copyright (C) 2011 Sony Ericsson Mobile Communications AB.
 *
 * Author: Aleksej Makarov <aleksej.makarov@sonyericsson.com>
 * Modified by: Cypress Semiconductor to add test modes and commands
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
 * Contact Cypress Semiconductor at www.cypress.com <ttdrivers@cypress.com>
 *
 */

#ifndef _CYTTSP4_REGS_H
#define _CYTTSP4_REGS_H

#define CY_FW_FILE_NAME "cyttsp4/cyttsp4_fw.bin"

#define CY_MAX_PRBUF_SIZE           PIPE_BUF
#define CY_PR_TRUNCATED             " truncated..."

#define CY_DEFAULT_CORE_ID          "main_ttsp_core"
#define CY_MAX_NUM_CORE_DEVS        5

#define CY_TMA1036_TCH_REC_SIZE     6
#define CY_TMA4XX_TCH_REC_SIZE      9
#define CY_TMA1036_MAX_TCH          0x0E
#define CY_TMA4XX_MAX_TCH           0x1E

#define GET_HSTMODE(reg)            ((reg & 0x70) >> 4)
#define GET_TOGGLE(reg)             ((reg & 0x80) >> 7)
#define IS_BOOTLOADER(hst_mode, reset_detect)           ((hst_mode) & 0x41 || (reset_detect) != 0 )

#define IS_EXCLUSIVE(dev)           ((dev) != NULL)
#define IS_TMO(t)                   ((t) == 0)

#define CY_REG_BASE                 0x00
#define CY_NUM_REVCTRL              8
#define CY_NUM_TCHREC               10
#define CY_NUM_DDATA                32
#define CY_NUM_MDATA                64

#define CY_CMD_CAT_GET_CFG_ROW_SIZE_CMD_SZ	1
#define CY_CMD_CAT_GET_CFG_ROW_SIZE_RET_SZ	2
/* Read Config Block */
#define CY_CMD_CAT_READ_CFG_BLK_CMD_SZ		6
#define CY_CMD_CAT_READ_CFG_BLK_RET_SZ		7 /* + Data */
#define CY_CMD_CAT_READ_CFG_BLK_RET_HDR_SZ	5
/* Write Config Block */
#define CY_CMD_CAT_WRITE_CFG_BLK_CMD_SZ		8 /* + Data + Security Key */
#define CY_CMD_CAT_WRITE_CFG_BLK_RET_SZ		5
#define CY_CMD_CAT_WRITE_CFG_BLK_CMD_HDR_SZ	6
/* Verify Config Block CRC */
#define CY_CMD_CAT_VERIFY_CFG_BLK_CRC_CMD_SZ	2
#define CY_CMD_CAT_VERIFY_CFG_BLK_CRC_RET_SZ	5
/* Retrieve Data Structure */
#define CY_CMD_CAT_RETRIEVE_DATA_STRUCT_CMD_SZ  6
#define CY_CMD_CAT_RETRIEVE_DATA_STRUCT_RET_SZ  5 /* + Data */
#define HI_BYTE(x)                  (u8)(((x) >> 8) & 0xFF)
#define LO_BYTE(x)                  (u8)((x) & 0xFF)

#define IS_LITTLEENDIAN(reg)        ((reg & 0x01) == 1)

#define CY_CONFIG_LENGTH_INFO_OFFSET 0
#define CY_CONFIG_LENGTH_INFO_SIZE  4
#define CY_CONFIG_LENGTH_OFFSET     0
#define CY_CONFIG_LENGTH_SIZE       2
#define CY_CONFIG_MAXLENGTH_OFFSET  2
#define CY_CONFIG_MAXLENGTH_SIZE    2

#define CY_REG_CAT_CMD              2
#define CY_CMD_COMPLETE_MASK        (1 << 6)
#define CY_CMD_MASK                 0x3F


/* Operational Mode Command Sizes */
/* Run Opens Self-Test */
#define CY_CMD_CAT_RUN_OPENS_ST_CMD_SZ          3
#define CY_CMD_CAT_RUN_OPENS_ST_RET_SZ          3
#define CY_CMD_CAT_GET_OPENS_ST_RES_CMD_SZ	6
#define CY_CMD_CAT_GET_OPENS_ST_RES_RET_SZ	5 /* + Data */

enum cyttsp4_ic_ebid {
	CY_TCH_PARM_EBID,
	CY_MDATA_EBID,
	CY_DDATA_EBID,
};

/* touch record system information offset masks and shifts */
#define CY_BYTE_OFS_MASK            0x1F
#define CY_BOFS_MASK                0xE0
#define CY_BOFS_SHIFT               5

#define CY_REQUEST_EXCLUSIVE_TIMEOUT	500
#define CY_COMMAND_COMPLETE_TIMEOUT	500

/* maximum number of concurrent tracks */
#define CY_NUM_TCH_ID               10

#define CY_ACTIVE_STYLUS_ID         10

/* helpers */
#define GET_NUM_TOUCHES(x)          ((x) & 0x1F)
#define IS_LARGE_AREA(x)            ((x) & 0x20)
#define IS_BAD_PKT(x)               ((x) & 0x20)

#define CY_WATCHDOG_TIMEOUT msecs_to_jiffies(2000)

/* drv_debug commands */
#define CY_DBG_PERF                     6
#define CY_DBG_SUSPEND                  4
#define CY_DBG_RESUME                   5
#define CY_DBG_SOFT_RESET               97
#define CY_DBG_RESET                    98

enum cyttsp4_hst_mode_bits {
	CY_HST_TOGGLE      = (1 << 7),
	CY_HST_MODE_CHANGE = (1 << 3),
	CY_HST_MODE        = (7 << 4),
	CY_HST_OPERATE     = (0 << 4),
	CY_HST_SYSINFO     = (1 << 4),
	CY_HST_CAT         = (2 << 4),
	CY_HST_LOWPOW      = (1 << 2),
	CY_HST_SLEEP       = (1 << 1),
	CY_HST_RESET       = (1 << 0),
};

enum cyttsp_cmd_bits {
	CY_CMD_COMPLETE    = (1 << 6),
};

enum cyttsp4_cmd_cat {
	CY_CMD_CAT_NULL,
	CY_CMD_CAT_RESERVED_1,
	CY_CMD_CAT_GET_CFG_ROW_SZ,
	CY_CMD_CAT_READ_CFG_BLK,
	CY_CMD_CAT_WRITE_CFG_BLK,
	CY_CMD_CAT_RESERVED_2,
	CY_CMD_CAT_LOAD_SELF_TEST_DATA, 
	CY_CMD_CAT_RUN_SELF_TEST,           //0x07, 
	CY_CMD_CAT_GET_SELF_TEST_RESULT,    //0x08,
	CY_CMD_CAT_CALIBRATE_IDACS,         //0x09,
	CY_CMD_CAT_INIT_BASELINES,          //0x0A,
	CY_CMD_CAT_EXEC_PANEL_SCAN,         //0x0B,
	CY_CMD_CAT_RETRIEVE_PANEL_SCAN,     //0x0C,
	CY_CMD_CAT_START_SENSOR_DATA_MODE,  //0x0D,
	CY_CMD_CAT_STOP_SENSOR_DATA_MODE,   //0x0E,
	CY_CMD_CAT_INT_PIN_MODE,            //0x0F,
	CY_CMD_CAT_RETRIEVE_DATA_STRUCTURE, //0x10,
	CY_CMD_CAT_VERIFY_CFG_BLK_CRC,
	CY_CMD_CAT_RESERVED_N,
};

enum cyttsp4_cmd_cat_test_id {
	CY_CAT_TEST_NULL,
	CY_CAT_TEST_BIST,
	CY_CAT_TEST_SHORT,
	CY_CAT_TEST_OPEN,
	CY_CAT_TEST_RESERVED,	
};

enum cyttsp4_cmd_op {
	CY_CMD_OP_NULL,
	CY_CMD_OP_RESERVED_1,
	CY_CMD_OP_GET_PARAM,
	CY_CMD_OP_SET_PARAM,
	CY_CMD_OP_RESERVED_2,
	CY_CMD_OP_GET_CRC,
};

enum cy_cali_mode {
 CY_CALI_SHOW_IDAC		= 0,
 CY_CALI_NORMAL			= 1, 
 CY_CALI_SHOW_IDAC_ONLY = 42,
};

enum cyttsp4_retrieve_data_structure_data_id {
        CY_RDS_DATAID_MUTCAP_SCAN,
        CY_RDS_DATAID_SELFCAP_SCAN,
        CY_RDS_DATAID_BUTTON_SCAN = 3,
};

#define CY_CMD_OP_NULL_CMD_SZ	1
#define CY_CMD_OP_NULL_RET_SZ	0
#define CY_CMD_OP_GET_CRC_CMD_SZ	2
#define CY_CMD_OP_GET_CRC_RET_SZ	3

#define CY_CMD_OP_GET_PARAM_CMD_SZ	2
#define CY_CMD_OP_GET_PARAM_RET_SZ	6
#define CY_CMD_OP_SET_PARAM_CMD_SZ	7
#define CY_CMD_OP_SET_PARAM_RET_SZ	2

#define CY_CMD_OP_GET_NOISE_METRIC_CMD_SZ 1
#define CY_CMD_OP_GET_NOISE_METRIC_RET_SZ 6

enum cyttsp4_op_param{
		CY_OP_PARAM_ACT_DIS       = 0x4A, // touch point distance before reporting new
		CY_OP_PARAM_SCAN_TYPE     = 0x4B, // enuerated scan type
		CY_OP_PARAM_LPM_INV       = 0x4C, // lpm interval in ms,					2 bytes
		CY_OP_PARAM_ACT_INV       = 0x4D, // active interval in ms, 0-250
		CY_OP_PARAM_ACT_TMO       = 0x4E,  // time out from active to lpm in ms,	2 bytes
		CY_OP_PARAM_ACT_LFT_INV   = 0x4F, //?? in ms
		CY_OP_PARAM_ACT_INV_2     = 0x50, //Active Distance 2
		CY_OPT_PARAM_CHGR_STA     = 0x51, //Charger Status
		CY_OPT_PARAM_GRP_XA       = 0x53, //GripX_A, 								2 bytes
		CY_OPT_PARAM_GRP_XB       = 0x54, //GripX_B, 								2 bytes
		CY_OPT_PARAM_GRP_YA       = 0x55, //GripY_A, 								2 bytes
		CY_OPT_PARAM_GRP_YB       = 0x56, //GripY_B, 								2 bytes
		CY_OPT_PARAM_GRP_XEXA     = 0x57, //GripXexc_A, 							2 bytes
		CY_OPT_PARAM_GRP_XEXB     = 0x58, //GripXexc_B, 							2 bytes
		CY_OPT_PARAM_GRP_YEXA     = 0x59, //GripYexc_A, 							2 bytes
		CY_OPT_PARAM_GRP_YEXB     = 0x5A, //GripYexc_B, 							2 bytes
		CY_OPT_PARAM_GRP_1ST_EXC  = 0x5B, //FirstExc
		CY_OPT_PARAM_GRP_EXC_EDG_ORG = 0x5C, //ExcEdgeOrigin
		CY_OPT_PARAM_GRP_INV         = 0x5D, //GripInterval
		CY_OPT_PARAM_GRP_SUP_EN      = 0x5E, //grip suppression global enable		
		CY_OPT_PARAM_SIZE_ORI_EN     = 0x80, //Size and Orientation Enable
};

enum cyttsp4_tt_mode_bits {
	CY_TT_BL     = (1 << 4),
	CY_TT_INVAL  = (1 << 5),
	CY_TT_CNTR   = (3 << 6),
};

enum cyttsp4_bl_status_bits {
	CY_BL_CS_OK    = (1 << 0),
	CY_BL_WDOG     = (1 << 1),
	CY_BL_RUNNING  = (1 << 4),
	CY_BL_BUSY     = (1 << 7),
};

enum cyttsp4_rds_cmd_data_type {
	CY_RDS_MUTUAL_IDAC_CENTER,
	CY_RDS_SELF_IDAC,
	CY_RDS_RESERVED,
	CY_BOTTON_IDAC,
	CY_MUTUAL_IDAC_ALTER,
	CY_MUTUAL_IDAC_ALTER2,
};

/* times */
#define CY_SCAN_PERIOD              40
#define CY_BL_ENTER_TIME            100

enum cyttsp4_mode {
	CY_MODE_UNKNOWN      = 0,
	CY_MODE_BOOTLOADER   = (1 << 1),
	CY_MODE_OPERATIONAL  = (1 << 2),
	CY_MODE_SYSINFO      = (1 << 3),
	CY_MODE_CAT          = (1 << 4),
	CY_MODE_STARTUP      = (1 << 5),
	CY_MODE_LOADER       = (1 << 6),
	CY_MODE_CHANGE_MODE  = (1 << 7),
	CY_MODE_CHANGED      = (1 << 8),
	CY_MODE_CMD_COMPLETE = (1 << 9),
};

enum cyttsp4_int_state {
	CY_INT_NONE,
	CY_INT_IGNORE      = (1 << 0),
	CY_INT_MODE_CHANGE = (1 << 1),
	CY_INT_EXEC_CMD    = (1 << 2),
	CY_INT_AWAKE       = (1 << 3),
};

enum cyttsp4_ic_grpnum {
	CY_IC_GRPNUM_RESERVED,
	CY_IC_GRPNUM_CMD_REGS,
	CY_IC_GRPNUM_TCH_REP,
	CY_IC_GRPNUM_DATA_REC,
	CY_IC_GRPNUM_TEST_REC,
	CY_IC_GRPNUM_PCFG_REC,
	CY_IC_GRPNUM_TCH_PARM_VAL,
	CY_IC_GRPNUM_TCH_PARM_SIZE,
	CY_IC_GRPNUM_RESERVED1,
	CY_IC_GRPNUM_RESERVED2,
	CY_IC_GRPNUM_OPCFG_REC,
	CY_IC_GRPNUM_DDATA_REC,
	CY_IC_GRPNUM_MDATA_REC,
	CY_IC_GRPNUM_TEST_REGS,
	CY_IC_GRPNUM_BTN_KEYS,
	CY_IC_GRPNUM_TTHE_REGS,
	CY_IC_GRPNUM_NUM
};

#define CY_VKEYS_X 720
#define CY_VKEYS_Y 1280

enum cyttsp4_flags {
	CY_FLAG_NONE = 0x00,
	CY_FLAG_HOVER = 0x04,
	CY_FLAG_FLIP = 0x08,
	CY_FLAG_INV_X = 0x10,
	CY_FLAG_INV_Y = 0x20,
	CY_FLAG_VKEYS = 0x40,
};

enum cyttsp4_event_id {
	CY_EV_NO_EVENT,
	CY_EV_TOUCHDOWN,
	CY_EV_MOVE,		/* significant displacement (> act dist) */
	CY_EV_LIFTOFF,		/* record reports last position */
};

enum cyttsp4_object_id {
	CY_OBJ_STANDARD_FINGER,
	CY_OBJ_LARGE_OBJECT,
	CY_OBJ_STYLUS,
	CY_OBJ_HOVER,
};

#define CY_POST_CODEL_WDG_RST           0x01
#define CY_POST_CODEL_CFG_DATA_CRC_FAIL 0x02
#define CY_POST_CODEL_PANEL_TEST_FAIL   0x04

#define CY_TEST_CMD_NULL                0

/* test mode NULL command driver codes; D */
enum cyttsp4_null_test_cmd_code {
	CY_NULL_CMD_NULL,
	CY_NULL_CMD_MODE,
	CY_NULL_CMD_STATUS_SIZE,
	CY_NULL_CMD_HANDSHAKE,
	CY_NULL_CMD_LOW_POWER,
};

enum cyttsp4_test_mode {
	CY_TEST_MODE_NORMAL_OP,		/* Send touch data to OS; normal op */
	CY_TEST_MODE_CAT,		/* Configuration and Test */
	CY_TEST_MODE_SYSINFO,		/* System information mode */
	CY_TEST_MODE_CLOSED_UNIT,	/* Send scan data to sysfs */
};

struct cyttsp4_test_mode_params {
	int cur_mode;
	int cur_cmd;
	size_t cur_status_size;
};

/* GEN4/SOLO Operational interface definitions */
/* TTSP System Information interface definitions */
struct cyttsp4_cydata {
	u8 ttpidh;
	u8 ttpidl;
	u8 fw_ver_major;
	u8 fw_ver_minor;
	u8 revctrl[CY_NUM_REVCTRL];
	u8 blver_major;
	u8 blver_minor;
	u8 jtag_si_id3;
	u8 jtag_si_id2;
	u8 jtag_si_id1;
	u8 jtag_si_id0;
	u8 mfgid_sz;
	u8 *mfg_id;
	u8 cyito_idh;
	u8 cyito_idl;
	u8 cyito_verh;
	u8 cyito_verl;
	u8 ttsp_ver_major;
	u8 ttsp_ver_minor;
	u8 device_info;
} __packed;

struct cyttsp4_test {
	u8 post_codeh;
	u8 post_codel;
} __packed;

struct cyttsp4_pcfg {
	u8 electrodes_x;
	u8 electrodes_y;
	u8 len_xh;
	u8 len_xl;
	u8 len_yh;
	u8 len_yl;
	u8 res_xh;
	u8 res_xl;
	u8 res_yh;
	u8 res_yl;
	u8 max_zh;
	u8 max_zl;
} __packed;

enum cyttsp4_tch_abs {	/* for ordering within the extracted touch data array */
	CY_TCH_X,	/* X */
	CY_TCH_Y,	/* Y */
	CY_TCH_P,	/* P (Z) */
	CY_TCH_T,	/* TOUCH ID */
	CY_TCH_E,	/* EVENT ID */
	CY_TCH_O,	/* OBJECT ID */
	CY_TCH_W,	/* SIZE */
	CY_TCH_MAJ,	/* TOUCH_MAJOR */
	CY_TCH_MIN,	/* TOUCH_MINOR */
	CY_TCH_OR,	/* ORIENTATION */
	CY_TCH_NUM_ABS
};

static const char * const cyttsp4_tch_abs_string[] = {
	[CY_TCH_X]	= "X",
	[CY_TCH_Y]	= "Y",
	[CY_TCH_P]	= "P",
	[CY_TCH_T]	= "T",
	[CY_TCH_E]	= "E",
	[CY_TCH_O]	= "O",
	[CY_TCH_W]	= "W",
	[CY_TCH_MAJ]	= "MAJ",
	[CY_TCH_MIN]	= "MIN",
	[CY_TCH_OR]	= "OR",
	[CY_TCH_NUM_ABS] = "INVALID"
};

#define CY_NUM_TCH_FIELDS       7
#define CY_NUM_EXT_TCH_FIELDS   3

struct cyttsp4_tch_rec_params {
	u8 loc;
	u8 size;
} __packed;

struct cyttsp4_opcfg {
	u8 cmd_ofs;
	u8 rep_ofs;
	u8 rep_szh;
	u8 rep_szl;
	u8 num_btns;
	u8 tt_stat_ofs;
	u8 obj_cfg0;
	u8 max_tchs;
	u8 tch_rec_size;
	struct cyttsp4_tch_rec_params tch_rec_old[CY_NUM_TCH_FIELDS];
	u8 btn_rec_size;/* btn record size (in bytes) */
	u8 btn_diff_ofs;/* btn data loc ,diff counts, (Op-Mode byte ofs) */
	u8 btn_diff_size;/* btn size of diff counts (in bits) */
	struct cyttsp4_tch_rec_params tch_rec_new[CY_NUM_EXT_TCH_FIELDS];
} __packed;

struct cyttsp4_ddata {
	u8 lab126_fw_ver0;
	u8 lab126_fw_ver1;
} __packed;

struct cyttsp4_sysinfo_data {
	u8 hst_mode;
	u8 reserved;
	u8 map_szh;
	u8 map_szl;
	u8 cydata_ofsh;
	u8 cydata_ofsl;
	u8 test_ofsh;
	u8 test_ofsl;
	u8 pcfg_ofsh;
	u8 pcfg_ofsl;
	u8 opcfg_ofsh;
	u8 opcfg_ofsl;
	u8 ddata_ofsh;
	u8 ddata_ofsl;
	u8 mdata_ofsh;
	u8 mdata_ofsl;
} __packed;

struct cyttsp4_sysinfo_ptr {
	struct cyttsp4_cydata *cydata;
	struct cyttsp4_test *test;
	struct cyttsp4_pcfg *pcfg;
	struct cyttsp4_opcfg *opcfg;
	struct cyttsp4_ddata *ddata;
	struct cyttsp4_mdata *mdata;
} __packed;

struct cyttsp4_touch {
	int abs[CY_TCH_NUM_ABS];
};

struct cyttsp4_tch_abs_params {
	size_t ofs;	/* abs byte offset */
	size_t size;	/* size in bits */
	size_t max;	/* max value */
	size_t bofs;	/* bit offset */
};

#define CY_NORMAL_ORIGIN 0	/* upper, left corner */
#define CY_INVERT_ORIGIN 1	/* lower, right corner */

struct cyttsp4_sysinfo_ofs {
	size_t chip_type;
	size_t cmd_ofs;
	size_t rep_ofs;
	size_t rep_sz;
	size_t num_btns;
	size_t num_btn_regs;	/* ceil(num_btns/4) */
	size_t tt_stat_ofs;
	size_t tch_rec_size;
	size_t obj_cfg0;
	size_t max_tchs;
	size_t mode_size;
	size_t data_size;
	size_t map_sz;
	size_t max_x;
	size_t x_origin;	/* left or right corner */
	size_t max_y;
	size_t y_origin;	/* upper or lower corner */
	size_t max_p;
	size_t cydata_ofs;
	size_t test_ofs;
	size_t pcfg_ofs;
	size_t opcfg_ofs;
	size_t ddata_ofs;
	size_t mdata_ofs;
	size_t cydata_size;
	size_t test_size;
	size_t pcfg_size;
	size_t opcfg_size;
	size_t ddata_size;
	size_t mdata_size;
	size_t btn_keys_size;
	struct cyttsp4_tch_abs_params tch_abs[CY_TCH_NUM_ABS];
	size_t btn_rec_size; /* btn record size (in bytes) */
	size_t btn_diff_ofs;/* btn data loc ,diff counts, (Op-Mode byte ofs) */
	size_t btn_diff_size;/* btn size of diff counts (in bits) */
};

/* button to keycode support */
#define CY_NUM_BTN_PER_REG	4
#define CY_NUM_BTN_EVENT_ID	4
#define CY_BITS_PER_BTN		2

enum cyttsp4_btn_state {
	CY_BTN_RELEASED = 0,
	CY_BTN_PRESSED = 1,
	CY_BTN_NUM_STATE
};

struct cyttsp4_btn {
	bool enabled;
	int state;	/* CY_BTN_PRESSED, CY_BTN_RELEASED */
	int key_code;
};

struct cyttsp4_crc {
	u8 ic_tt_cfg_crc[2];
};

#ifdef SHOK_SENSOR_DATA_MODE
enum cyttsp4_monitor_status {
	CY_MNTR_DISABLED,
	CY_MNTR_INITIATED,
	CY_MNTR_STARTED,
};

struct cyttsp4_sensor_monitor {
	enum cyttsp4_monitor_status mntr_status;
	u8 sensor_data[150];		/* operational sensor data */
};
#endif

struct cyttsp4_sysinfo {
	bool ready;
	struct cyttsp4_sysinfo_data si_data;
	struct cyttsp4_sysinfo_ptr si_ptrs;
	struct cyttsp4_sysinfo_ofs si_ofs;
	struct cyttsp4_btn *btn;	/* button states */
	struct cyttsp4_crc crc;
#ifdef SHOK_SENSOR_DATA_MODE
	struct cyttsp4_sensor_monitor monitor;
#endif
	u8 *btn_rec_data;		/* button diff count data */
	u8 *xy_mode;			/* operational mode and status regs */
	u8 *xy_data;			/* operational touch regs */
};

#ifdef DEVELOPMENT_MODE
extern unsigned long long timeofday_microsec(void);
extern void wario_debug_toggle(int);
#endif

enum cyttsp4_self_test_id {
        CY_ST_ID_NULL,
        CY_ST_ID_BIST,
        CY_ST_ID_SHORTS,
        CY_ST_ID_OPENS,
        CY_ST_ID_AUTOSHORTS,
};

enum cyttsp4_self_test_result {
        CY_ST_RESULT_PASS,
        CY_ST_RESULT_FAIL,
        CY_ST_RESULT_HOST_MUST_INTERPRET = 0xFF,
};

enum cyttsp4_calibrate_idacs_sensing_mode {
        CY_CI_SM_MUTCAP_FINE,
        CY_CI_SM_MUTCAP_BUTTON,
        CY_CI_SM_SELFCAP,
};

/* Calibrate IDACs */
#define CY_CMD_CAT_CALIBRATE_IDAC_CMD_SZ        2
#define CY_CMD_CAT_CALIBRATE_IDAC_RET_SZ        1

#define CY_CALIBRATE_COMPLETE_TIMEOUT           10000

#endif /* _CYTTSP4_REGS_H */
