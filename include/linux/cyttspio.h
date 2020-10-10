#ifndef __CYTTSP_IO_H__
#define __CYTTSP_IO_H__


#define CY_SENSORS_ROW 18
#define CY_SENSORS_COL 14

/* raw data for all sensors */
#define CY_TEST_MODE_RAW_COUNTS		0x40
/* difference counts for all sensors */
#define CY_TEST_MODE_DIFF_COUNTS	0x50
/* interleaved local and global IDAC values */
#define CY_TEST_MODE_IDAC_SETTINGS	0x60
/* interleaved baseline and raw data for all sensors*/
#define CY_TEST_MODE_INT_RC_BASE	0x70


/*
#ifndef u8 
typedef unsigned char u8
#endif
*/
/*
 * 
 * referer to page 84 in cyttsp reference rev J for test mode format
 * 
 */

/* two pages with mutual scan default*/
#define TEST_MODE_EVEN_PAGE_MUTUAL 0x00
#define TEST_MODE_ODD_PAGE_MUTUAL  0x40

#define TEST_MODE_EVEN_PAGE_SELF 0x01
#define TEST_MODE_ODD_PAGE_SELF  0x41

struct tm_hst_mode{
	
	u8 soft_reset : 1;
	u8 deep_sleep : 1;	
	u8 low_pow : 1;	
	u8 pad_1 : 1;	
	u8 device_mode : 3;
	u8 data_bit_toggle : 1;
	
};

struct tm_tt_mode{
	u8 scan_type : 2;
	u8 pad_2 : 2;
	u8 bl_flag : 1;
	u8 buffer_invalid : 1;	
	u8 new_data_ct : 2;
};

struct tm_tt_stat{
	
	u8 num_touch : 4;
	u8 object_detected : 3;
	u8 pad_3 : 1;
};

struct cyttsp_test_mode_data 
{
	struct tm_hst_mode reg_hst_mode;
	struct tm_tt_mode reg_tt_mode;
	struct tm_tt_stat reg_tt_stat;
	
	u8 regs[CY_SENSORS_ROW*CY_SENSORS_COL];
	
};

struct cyttsp_test_mode_pages
{
	struct cyttsp_test_mode_data tm_data[2];
};

#define TO_U8(v) *(u8*)&((v))

#define CY_MAGIC_NUMBER			21
#define CY_IOCTL_TEST_MODE_DUMP		_IOW(CY_MAGIC_NUMBER, 0x01, struct cyttsp_test_mode_pages)


#endif
