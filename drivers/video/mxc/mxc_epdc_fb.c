/*
 * Copyright (C) 2010-2012 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
/*
 * Based on STMP378X LCDIF
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

//#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/cpufreq.h>
#include <linux/firmware.h>
#include <linux/kthread.h>
#include <linux/dmaengine.h>
#include <linux/pxp_dma.h>
#include <linux/mxcfb.h>
#include <linux/mxcfb_epdc_kernel.h>
#include <linux/gpio.h>
#include <linux/regulator/driver.h>
#include <linux/fsl_devices.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include <mach/epdc.h>
#include <mach/dma.h>
#include <asm/cacheflush.h>
#include <linux/mfd/max77696-events.h>
#include <llog.h>

#include "epdc_regs.h"
#include <asm/cacheflush.h>

#ifdef CONFIG_FALCON
#include <linux/falconmem.h>
#endif

#ifdef CONFIG_MX6SL_WARIO_WOODY   /* only for whisky device */
#define FIX_CURRENT_RESAMPLE   /* to fix dithered image induced high current device shutdown */
#endif

#ifdef CONFIG_FB_MXC_EINK_REAGL_MODULE
#define CONFIG_FB_MXC_EINK_REAGL
#endif // CONFIG_FB_MXC_EINK_REAGL_MODULE

#ifdef CONFIG_FB_MXC_EINK_REAGL
#include <linux/mxcfb_eink.h>
#endif // CONFIG_FB_MXC_EINK_REAGL

#define DRIVER_NAME                "imx_epdc_fb"

#define MAX_INIT_RETRIES           3

#define NUM_SCREENS_MIN	2

#define EPDC_V1_NUM_LUTS           16
#define EPDC_V1_MAX_NUM_UPDATES    20
#define EPDC_V2_NUM_LUTS           64
#define EPDC_V2_MAX_NUM_UPDATES    64
#define EPDC_MAX_NUM_BUFFERS       2
#define INVALID_LUT                (-1)
#define DRY_RUN_NO_LUT             100

#define DEFAULT_TEMP_INDEX         0
#define DEFAULT_TEMP               20 /* room temp in deg Celsius */

#define INIT_UPDATE_MARKER         0x12345678
#define PAN_UPDATE_MARKER          0x12345679

#define POWER_STATE_OFF            0
#define POWER_STATE_ON             1
#define POWER_STATE_GOING_UP       2

/* Maximum update buffer image width due to v2.0 and v2.1 errata ERR005313. */
#define EPDC_V2_MAX_UPDATE_WIDTH   2047
#define EPDC_V2_ROTATION_ALIGNMENT 8
#define PWRUP_TIMEOUT              250
#define MERGE_OK                   0
#define MERGE_FAIL                 1
#define MERGE_BLOCK                2

#define FW_STR_LEN                 64

#define EPDC_VERSION_GET_MAJOR(code)             (((code) & EPDC_VERSION_MAJOR_MASK) >> EPDC_VERSION_MAJOR_OFFSET)
#define EPDC_VERSION_GET_MINOR(code)             (((code) & EPDC_VERSION_MINOR_MASK) >> EPDC_VERSION_MINOR_OFFSET)
#define EPDC_VERSION_GET_STEP(code)              (((code) & EPDC_VERSION_STEP_MASK)  >> EPDC_VERSION_STEP_OFFSET)
#define EPDC_VERSION_CODE(major, minor, step) \
	((((major) & (EPDC_VERSION_MAJOR_MASK >> EPDC_VERSION_MAJOR_OFFSET)) << EPDC_VERSION_MAJOR_OFFSET) | \
	 (((minor) & (EPDC_VERSION_MINOR_MASK >> EPDC_VERSION_MINOR_OFFSET)) << EPDC_VERSION_MINOR_OFFSET) | \
	 (((step)  & (EPDC_VERSION_STEP_MASK  >> EPDC_VERSION_STEP_OFFSET))  << EPDC_VERSION_STEP_OFFSET)) \

#define EPDC_VERSION_2_0_0         EPDC_VERSION_CODE(2, 0, 0)
#define EPDC_VERSION_2_1_0         EPDC_VERSION_CODE(2, 1, 0)

#define LUT_MASK(x) (1ULL << (x))

static unsigned long default_bpp = 8;
#ifdef CONFIG_SENSORS_MAX77696
extern signed int display_temp_c;
#else
#error "define display_temp_c!"
#endif

extern bool wfm_using_builtin;

struct update_marker_data {
	struct list_head full_list;
	struct list_head upd_list;
	u32 update_marker;
	struct completion update_completion;
	struct completion submit_completion;
	int lut_num;
	u64 lut_mask;
	bool collision_test;
	bool waiting;
	bool submitted;
	long long start_time;
};

struct update_desc_list {
	struct list_head list;
	struct mxcfb_update_data upd_data; /* Update parameters */
	u32 epdc_offs;                     /* Added to buffer ptr to resolve alignment */
	u32 epdc_stride;                   /* Depends on rotation & whether we skip PxP */
	struct list_head upd_marker_list;  /* List of markers for this update */
	u32 update_order;                  /* Numeric ordering value for update */
	bool is_reagl;                     /* Does the update require REAGL processing? */
	bool wb_pause;                     /* Boolean to identify whether the EPDC update
	                                      requires pause for REAGL processing */
};

/* This structure represents a list node containing both
 * a memory region allocated as an output buffer for the PxP
 * update processing task, and the update description (mode, region, etc.) */
struct update_data_list {
	struct list_head list;
	dma_addr_t phys_addr;      /* Pointer to phys address of processed Y buf */
	void *virt_addr;
	struct update_desc_list *update_desc;
	int lut_num;               /* Assigned before update is processed into working buffer */
	u64 collision_mask;        /* Set when update creates collision */
	                           /* Mask of the LUTs the update collides with */
};

struct waveform_data_header {
#if defined(CONFIG_LAB126)
	unsigned int checksum:32;
	unsigned int file_length:32;
	unsigned int serial_number:32;
	unsigned int run_type:8;
	unsigned int fpl_platform:8;
	unsigned int fpl_lot:16;
	unsigned int mode_version:8;
	unsigned int wf_version:8;
	unsigned int wf_subversion:8;
	unsigned int wf_type:8;
	unsigned int panel_size:8;
	unsigned int amepd_part_number:8;
	unsigned int wf_revision:8;
	unsigned int frame_rate:8;
	unsigned int reserved1_0:8;
	unsigned int vcom_shifted:8;
	unsigned int reserved1_1:16;
#else
	unsigned int wi0;
	unsigned int wi1;
	unsigned int wi2;
	unsigned int wi3;
	unsigned int wi4;
	unsigned int wi5;
	unsigned int wi6;
#endif
	unsigned int xwia:24;
	unsigned int cs1:8;

	unsigned int wmta:24;
	unsigned int fvsn:8;
	unsigned int luts:8;
	unsigned int mc:8;
	unsigned int trc:8;
	unsigned int advanced_wfm_flags:8;
	unsigned int eb:8;
	unsigned int sb:8;
	unsigned int reserved0_1:8;
	unsigned int reserved0_2:8;
	unsigned int reserved0_3:8;
	unsigned int reserved0_4:8;
	unsigned int reserved0_5:8;
	unsigned int cs2:8;
};

#if defined(FIX_CURRENT_RESAMPLE)
struct sw_mitigation {
	__u32 pixel_thres; /* percentage of pixel changed each line in % */
	__u32 count_thres; /* line exceed above threshold in %*/
	__u32 scaled_width;   /* resample width */
	__u32 scaled_height; /* resample height */
	__u32 scan_lines;     /* lines to be scanned */
	__u32 str_thres; /* average scanned line pixel value changed threshold in % */
	__u32 scaled_factor; /* used to calculate scaled width and height in %o*/
};
#endif

struct mxc_epdc_fb_data {
	struct fb_info info;
	struct fb_var_screeninfo epdc_fb_var; /* Internal copy of screeninfo
	                                         so we can sync changes to it */
	u32 pseudo_palette[16];
	char fw_str[FW_STR_LEN];
	struct list_head list;
	struct imx_epdc_fb_mode *cur_mode;
	struct imx_epdc_fb_platform_data *pdata;
	int blank;
	u32 max_pix_size;
	ssize_t map_size;
	dma_addr_t phys_start;
#if defined(FIX_CURRENT_RESAMPLE)
	void *virt_start;
#endif
	u32 fb_offset;
	int default_bpp;
	int native_width;
	int native_height;
	int physical_width;
	int physical_height;
	int num_screens;
	int epdc_irq;
	struct device *dev;
	int power_state;
	int wait_for_powerdown;
	struct completion powerdown_compl;
	struct completion powerup_compl;
	struct clk *epdc_clk_axi;
	struct clk *epdc_clk_pix;
	struct regulator *display_regulator;
	struct regulator *vcom_regulator;
	int rev;

	/* FB elements related to EPDC updates */
	int num_luts;
	int max_num_updates;
	volatile bool in_init;
	bool hw_ready;
	bool hw_initializing;
	bool waiting_for_idle;
	u32 auto_mode;
	u32 upd_scheme;
	struct list_head upd_pending_list;
	struct list_head upd_buf_queue;
	struct list_head upd_buf_free_list;
	struct list_head upd_buf_collision_list;
	struct update_data_list *cur_update;
	struct mutex queue_mutex;

	int trt_entries;
	int temp_index;
	u8 *temp_range_bounds;
	int buf_pix_fmt;
	struct mxcfb_waveform_modes wv_modes;
	bool wv_modes_update;
	u32 *waveform_buffer_virt;
	u32 waveform_buffer_phys;
	u32 waveform_buffer_size;
//	u32 *working_buffer_virt;
//	u32 working_buffer_phys;

	u32 *working_buffer_A_virt;
	u32 working_buffer_A_phys;
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
	u32 *working_buffer_B_virt;
	u32 working_buffer_B_phys;
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B

	u32 working_buffer_size;
	dma_addr_t *phys_addr_updbuf;
	void **virt_addr_updbuf;
	u32 upd_buffer_num;
	u32 max_num_buffers;
#if !defined(CONFIG_LAB126)
	dma_addr_t phys_addr_copybuf;	/* Phys address of copied update data */
	void *virt_addr_copybuf;	/* Used for PxP SW workaround */
#endif
	u32 order_cnt;
	struct list_head full_marker_list;
	u32 *lut_update_order;		/* Array size = number of luts */
	volatile u64 epdc_colliding_luts;
	u64 luts_complete_wb;
	struct completion updates_done;
	struct delayed_work epdc_done_work;
	struct workqueue_struct *epdc_submit_workqueue;
	struct work_struct epdc_submit_work;

	bool waiting_for_wb;
	bool waiting_for_lut;
	bool waiting_for_lut15;
	struct completion update_res_free;
	struct completion wb_free;
	struct completion lut15_free;
	struct completion eof_event;
	int eof_sync_period;
	struct mutex power_mutex;
	bool powering_down;
	bool updates_active;
	int pwrdown_delay;
	unsigned long tce_prevent;
	bool restrict_width; /* work around rev >=2.0 width and
				stride restriction  */

	/* FB elements related to gen2 waveform data */
	u8 *waveform_vcd_buffer;
	u32 waveform_mc;
	u32 waveform_trc;

	/* FB elements related to PxP DMA */
	struct completion pxp_tx_cmpl;
	struct pxp_channel *pxp_chan;
	struct pxp_config_data pxp_conf;
	struct dma_async_tx_descriptor *txd;
	dma_cookie_t cookie;
	struct scatterlist sg[2];
	struct mutex pxp_mutex; /* protects access to PxP */

#if defined(CONFIG_LAB126)
	struct waveform_data_header *wv_header;
	unsigned long wv_header_size;
	unsigned int vcom_uV;
#endif

#if defined(FIX_CURRENT_RESAMPLE)
	struct sw_mitigation sw_algo;
#endif
	int *dither_err_dist;
	u32 waveform_type;
	int temp_override;
	// Variables used for voltage control sys entry
	int vc_waveform_sys;
	int vc_temperature_sys;
#ifdef CONFIG_FB_MXC_EINK_REAGL
	int which_reagl;
#endif // CONFIG_FB_MXC_EINK_REAGL
};

#ifdef CONFIG_FB_MXC_EINK_REAGL

enum reagl_algo_enum {
	REAGL_ALGO_LAB126_FAST = 0,
	REAGL_ALGO_LAB126,
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
	REAGL_ALGO_FREESCALE,
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B
	MAX_REAGL_ALGOS
};

struct reagl_algo {
	reagl_function_t *func;
	uint16_t *buffer_in;
	uint16_t *buffer_out;
	u32 buffer_tce;
};

static struct reagl_algo reagl_algos[MAX_REAGL_ALGOS];

#endif // CONFIG_FB_MXC_EINK_REAGL

struct mxcfb_waveform_data_file {
	struct waveform_data_header wdh;
	u32 *data;	/* Temperature Range Table + Waveform Data */
};

/*
 * EPDC Voltage Control data handler
 */
struct epd_vc_data {
	unsigned version:16;
	unsigned vpos:16;
	unsigned vneg:16;
	unsigned vddh:16;
	unsigned vee:16;
	unsigned vcom_offset:16;
	unsigned unused_0:16;
	unsigned unused_1:8;
	u8  cs:8;
};

/*
 * Structure to hold Voltage control information in uV
 */
struct epd_vc_data_volts {
	int vpos_v;
	int vneg_v;
	int vddh_v;
	int vee_v ;
	int vcom_v;
};

#if defined(CONFIG_LAB126)

#include "mxc_epdc_fb_lab126.c"
#define WFM_TEMP_BUF_LEN   (1024*1024*10)
static int mxc_epdc_debugging = 0;
static int mxc_epdc_paused = 0;
static int use_cmap = 0;

// Total system memory (mm.h)
extern unsigned long num_physpages;

// Table to allocate framebuffer memory based on total memory and resolution
struct fb_res_mem {
	unsigned int total_system_mem_mb;
	int x;
	int y;
	unsigned int fb_mem_mb;
};

struct fb_res_mem resolution_memory_map[] =
{
	{ 256, 800 , 600 ,  3},
	{ 256, 1024, 758 ,  3},
	{ 512, 1448, 1072,  6},
	{ 508, 1072, 1448,  6}, //whisky, taking 4M away reserved for working buffer A
};

#endif /* CONFIG_LAB126 */

void __iomem *epdc_base;

struct mxc_epdc_fb_data *g_fb_data;
static pmic_event_callback_t g_pmic_evt_pwrok;
static pmic_event_callback_t g_pmic_evt_fault;

/* forward declaration */
static void epdc_force_powerdown(struct mxc_epdc_fb_data *fb_data);
static int mxc_epdc_fb_get_temp_index(struct mxc_epdc_fb_data *fb_data,
						int temp);
static void mxc_epdc_fb_flush_updates(struct mxc_epdc_fb_data *fb_data);
static int mxc_epdc_fb_blank(int blank, struct fb_info *info);
static int mxc_epdc_fb_init_hw(struct fb_info *info);
static int pxp_process_update(struct mxc_epdc_fb_data *fb_data,
			      u32 src_width, u32 src_height,
			      struct mxcfb_rect *update_region);
static int pxp_complete_update(struct mxc_epdc_fb_data *fb_data, u32 *hist_stat);
#if defined(FIX_CURRENT_RESAMPLE)
static int pxp_legacy_process_resample(struct mxcfb_update_data *upd_data, struct mxc_epdc_fb_data *fb_data,
			      u32 scaled_width, u32 scaled_height, u32 down_up);
#endif

static int draw_mode0(struct mxc_epdc_fb_data *fb_data);
static bool is_free_list_full(struct mxc_epdc_fb_data *fb_data);

static void do_dithering_processing_Y1_v1_0(
		unsigned char *update_region_ptr,
		struct mxcfb_rect *update_region,
		unsigned long update_region_stride,
		int *err_dist, int reaglWfm);
static void do_dithering_processing_Y2_v1_0(
		unsigned char *update_region_ptr,
		struct mxcfb_rect *update_region,
		unsigned long update_region_stride,
		int *err_dist, int reaglWfm);
static void do_dithering_processing_Y4_v1_0(
		unsigned char *update_region_ptr,
		struct mxcfb_rect *update_region,
		unsigned long update_region_stride,
		int *err_dist, int reaglWfm);

static void dump_epdc_reg(void)
{
	printk(KERN_INFO "\n\n");
	printk(KERN_INFO "EPDC_CTRL 0x%x\n", __raw_readl(EPDC_CTRL));
	printk(KERN_INFO "EPDC_WVADDR 0x%x\n", __raw_readl(EPDC_WVADDR));
	printk(KERN_INFO "EPDC_WB_ADDR 0x%x\n", __raw_readl(EPDC_WB_ADDR));
	printk(KERN_INFO "EPDC_RES 0x%x\n", __raw_readl(EPDC_RES));
	printk(KERN_INFO "EPDC_FORMAT 0x%x\n", __raw_readl(EPDC_FORMAT));
	printk(KERN_INFO "EPDC_FIFOCTRL 0x%x\n", __raw_readl(EPDC_FIFOCTRL));
	printk(KERN_INFO "EPDC_UPD_ADDR 0x%x\n", __raw_readl(EPDC_UPD_ADDR));
	printk(KERN_INFO "EPDC_UPD_STRIDE 0x%x\n", __raw_readl(EPDC_UPD_STRIDE));
	printk(KERN_INFO "EPDC_UPD_FIXED 0x%x\n", __raw_readl(EPDC_UPD_FIXED));
	printk(KERN_INFO "EPDC_UPD_CORD 0x%x\n", __raw_readl(EPDC_UPD_CORD));
	printk(KERN_INFO "EPDC_UPD_SIZE 0x%x\n", __raw_readl(EPDC_UPD_SIZE));
	printk(KERN_INFO "EPDC_UPD_CTRL 0x%x\n", __raw_readl(EPDC_UPD_CTRL));
	printk(KERN_INFO "EPDC_TEMP 0x%x\n", __raw_readl(EPDC_TEMP));
	printk(KERN_INFO "EPDC_AUTOWV_LUT 0x%x\n", __raw_readl(EPDC_AUTOWV_LUT));
	printk(KERN_INFO "EPDC_TCE_CTRL 0x%x\n", __raw_readl(EPDC_TCE_CTRL));
	printk(KERN_INFO "EPDC_TCE_SDCFG 0x%x\n", __raw_readl(EPDC_TCE_SDCFG));
	printk(KERN_INFO "EPDC_TCE_GDCFG 0x%x\n", __raw_readl(EPDC_TCE_GDCFG));
	printk(KERN_INFO "EPDC_TCE_HSCAN1 0x%x\n", __raw_readl(EPDC_TCE_HSCAN1));
	printk(KERN_INFO "EPDC_TCE_HSCAN2 0x%x\n", __raw_readl(EPDC_TCE_HSCAN2));
	printk(KERN_INFO "EPDC_TCE_VSCAN 0x%x\n", __raw_readl(EPDC_TCE_VSCAN));
	printk(KERN_INFO "EPDC_TCE_OE 0x%x\n", __raw_readl(EPDC_TCE_OE));
	printk(KERN_INFO "EPDC_TCE_POLARITY 0x%x\n", __raw_readl(EPDC_TCE_POLARITY));
	printk(KERN_INFO "EPDC_TCE_TIMING1 0x%x\n", __raw_readl(EPDC_TCE_TIMING1));
	printk(KERN_INFO "EPDC_TCE_TIMING2 0x%x\n", __raw_readl(EPDC_TCE_TIMING2));
	printk(KERN_INFO "EPDC_TCE_TIMING3 0x%x\n", __raw_readl(EPDC_TCE_TIMING3));
	printk(KERN_INFO "EPDC_PIGEON_CTRL0 0x%x\n", __raw_readl(EPDC_PIGEON_CTRL0));
	printk(KERN_INFO "EPDC_PIGEON_CTRL1 0x%x\n", __raw_readl(EPDC_PIGEON_CTRL1));
	printk(KERN_INFO "EPDC_IRQ_MASK1 0x%x\n", __raw_readl(EPDC_IRQ_MASK1));
	printk(KERN_INFO "EPDC_IRQ_MASK2 0x%x\n", __raw_readl(EPDC_IRQ_MASK2));
	printk(KERN_INFO "EPDC_IRQ1 0x%x\n", __raw_readl(EPDC_IRQ1));
	printk(KERN_INFO "EPDC_IRQ2 0x%x\n", __raw_readl(EPDC_IRQ2));
	printk(KERN_INFO "EPDC_IRQ_MASK 0x%x\n", __raw_readl(EPDC_IRQ_MASK));
	printk(KERN_INFO "EPDC_IRQ 0x%x\n", __raw_readl(EPDC_IRQ));
	printk(KERN_INFO "EPDC_STATUS_LUTS 0x%x\n", __raw_readl(EPDC_STATUS_LUTS));
	printk(KERN_INFO "EPDC_STATUS_LUTS2 0x%x\n", __raw_readl(EPDC_STATUS_LUTS2));
	printk(KERN_INFO "EPDC_STATUS_NEXTLUT 0x%x\n", __raw_readl(EPDC_STATUS_NEXTLUT));
	printk(KERN_INFO "EPDC_STATUS_COL1 0x%x\n", __raw_readl(EPDC_STATUS_COL));
	printk(KERN_INFO "EPDC_STATUS_COL2 0x%x\n", __raw_readl(EPDC_STATUS_COL2));
	printk(KERN_INFO "EPDC_STATUS 0x%x\n", __raw_readl(EPDC_STATUS));
	printk(KERN_INFO "EPDC_UPD_COL_CORD 0x%x\n", __raw_readl(EPDC_UPD_COL_CORD));
	printk(KERN_INFO "EPDC_UPD_COL_SIZE 0x%x\n", __raw_readl(EPDC_UPD_COL_SIZE));
	printk(KERN_INFO "EPDC_DEBUG 0x%x\n", __raw_readl(EPDC_DEBUG));
	printk(KERN_INFO "EPDC_DEBUG_LUT 0x%x\n", __raw_readl(EPDC_DEBUG_LUT));
	printk(KERN_INFO "EPDC_HIST1_PARAM 0x%x\n", __raw_readl(EPDC_HIST1_PARAM));
	printk(KERN_INFO "EPDC_HIST2_PARAM 0x%x\n", __raw_readl(EPDC_HIST2_PARAM));
	printk(KERN_INFO "EPDC_HIST4_PARAM 0x%x\n", __raw_readl(EPDC_HIST4_PARAM));
	printk(KERN_INFO "EPDC_HIST8_PARAM0 0x%x\n", __raw_readl(EPDC_HIST8_PARAM0));
	printk(KERN_INFO "EPDC_HIST8_PARAM1 0x%x\n", __raw_readl(EPDC_HIST8_PARAM1));
	printk(KERN_INFO "EPDC_HIST16_PARAM0 0x%x\n", __raw_readl(EPDC_HIST16_PARAM0));
	printk(KERN_INFO "EPDC_HIST16_PARAM1 0x%x\n", __raw_readl(EPDC_HIST16_PARAM1));
	printk(KERN_INFO "EPDC_HIST16_PARAM2 0x%x\n", __raw_readl(EPDC_HIST16_PARAM2));
	printk(KERN_INFO "EPDC_HIST16_PARAM3 0x%x\n", __raw_readl(EPDC_HIST16_PARAM3));
	printk(KERN_INFO "EPDC_GPIO 0x%x\n", __raw_readl(EPDC_GPIO));
	printk(KERN_INFO "EPDC_VERSION 0x%x\n", __raw_readl(EPDC_VERSION));
	printk(KERN_INFO "\n\n");
}

#ifdef DEBUG
static void dump_pxp_config(struct mxc_epdc_fb_data *fb_data,
			    struct pxp_config_data *pxp_conf)
{
	dev_info(fb_data->dev, "S0 fmt 0x%x",
		pxp_conf->s0_param.pixel_fmt);
	dev_info(fb_data->dev, "S0 width 0x%x",
		pxp_conf->s0_param.width);
	dev_info(fb_data->dev, "S0 height 0x%x",
		pxp_conf->s0_param.height);
	dev_info(fb_data->dev, "S0 ckey 0x%x",
		pxp_conf->s0_param.color_key);
	dev_info(fb_data->dev, "S0 ckey en 0x%x",
		pxp_conf->s0_param.color_key_enable);

	dev_info(fb_data->dev, "OL0 combine en 0x%x",
		pxp_conf->ol_param[0].combine_enable);
	dev_info(fb_data->dev, "OL0 fmt 0x%x",
		pxp_conf->ol_param[0].pixel_fmt);
	dev_info(fb_data->dev, "OL0 width 0x%x",
		pxp_conf->ol_param[0].width);
	dev_info(fb_data->dev, "OL0 height 0x%x",
		pxp_conf->ol_param[0].height);
	dev_info(fb_data->dev, "OL0 ckey 0x%x",
		pxp_conf->ol_param[0].color_key);
	dev_info(fb_data->dev, "OL0 ckey en 0x%x",
		pxp_conf->ol_param[0].color_key_enable);
	dev_info(fb_data->dev, "OL0 alpha 0x%x",
		pxp_conf->ol_param[0].global_alpha);
	dev_info(fb_data->dev, "OL0 alpha en 0x%x",
		pxp_conf->ol_param[0].global_alpha_enable);
	dev_info(fb_data->dev, "OL0 local alpha en 0x%x",
		pxp_conf->ol_param[0].local_alpha_enable);

	dev_info(fb_data->dev, "Out fmt 0x%x",
		pxp_conf->out_param.pixel_fmt);
	dev_info(fb_data->dev, "Out width 0x%x",
		pxp_conf->out_param.width);
	dev_info(fb_data->dev, "Out height 0x%x",
		pxp_conf->out_param.height);

	dev_info(fb_data->dev,
		"drect left 0x%x right 0x%x width 0x%x height 0x%x",
		pxp_conf->proc_data.drect.left, pxp_conf->proc_data.drect.top,
		pxp_conf->proc_data.drect.width,
		pxp_conf->proc_data.drect.height);
	dev_info(fb_data->dev,
		"srect left 0x%x right 0x%x width 0x%x height 0x%x",
		pxp_conf->proc_data.srect.left, pxp_conf->proc_data.srect.top,
		pxp_conf->proc_data.srect.width,
		pxp_conf->proc_data.srect.height);
	dev_info(fb_data->dev, "Scaling en 0x%x", pxp_conf->proc_data.scaling);
	dev_info(fb_data->dev, "HFlip en 0x%x", pxp_conf->proc_data.hflip);
	dev_info(fb_data->dev, "VFlip en 0x%x", pxp_conf->proc_data.vflip);
	dev_info(fb_data->dev, "Rotation 0x%x", pxp_conf->proc_data.rotate);
	dev_info(fb_data->dev, "BG Color 0x%x", pxp_conf->proc_data.bgcolor);
}

static void dump_update_data(struct device *dev,
			     struct update_data_list *upd_data_list)
{
	dev_info(dev,
		"X = %d, Y = %d, Width = %d, Height = %d, WaveMode = %d, "
		"LUT = %d, Coll Mask = 0x%llx, order = %d\n",
		upd_data_list->update_desc->upd_data.update_region.left,
		upd_data_list->update_desc->upd_data.update_region.top,
		upd_data_list->update_desc->upd_data.update_region.width,
		upd_data_list->update_desc->upd_data.update_region.height,
		upd_data_list->update_desc->upd_data.waveform_mode,
		upd_data_list->lut_num,
		upd_data_list->collision_mask,
		upd_data_list->update_desc->update_order);
}

static void dump_collision_list(struct mxc_epdc_fb_data *fb_data)
{
	struct update_data_list *plist;

	dev_info(fb_data->dev, "Collision List:\n");
	if (list_empty(&fb_data->upd_buf_collision_list))
		dev_info(fb_data->dev, "Empty");
	list_for_each_entry(plist, &fb_data->upd_buf_collision_list, list) {
		dev_info(fb_data->dev, "Virt Addr = 0x%x, Phys Addr = 0x%x ",
			(u32)plist->virt_addr, plist->phys_addr);
		dump_update_data(fb_data->dev, plist);
	}
}

static void dump_free_list(struct mxc_epdc_fb_data *fb_data)
{
	struct update_data_list *plist;

	dev_info(fb_data->dev, "Free List:\n");
	if (list_empty(&fb_data->upd_buf_free_list))
		dev_info(fb_data->dev, "Empty");
	list_for_each_entry(plist, &fb_data->upd_buf_free_list, list)
		dev_info(fb_data->dev, "Virt Addr = 0x%x, Phys Addr = 0x%x ",
			(u32)plist->virt_addr, plist->phys_addr);
}

static void dump_queue(struct mxc_epdc_fb_data *fb_data)
{
	struct update_data_list *plist;

	dev_info(fb_data->dev, "Queue:\n");
	if (list_empty(&fb_data->upd_buf_queue))
		dev_info(fb_data->dev, "Empty");
	list_for_each_entry(plist, &fb_data->upd_buf_queue, list) {
		dev_info(fb_data->dev, "Virt Addr = 0x%x, Phys Addr = 0x%x ",
			(u32)plist->virt_addr, plist->phys_addr);
		dump_update_data(fb_data->dev, plist);
	}
}

static void dump_desc_data(struct device *dev,
			     struct update_desc_list *upd_desc_list)
{
	dev_info(dev,
	         "X = %d, Y = %d, Width = %d, Height = %d, WaveMode = %d, order = %d\n",
	         upd_desc_list->upd_data.update_region.left,
	         upd_desc_list->upd_data.update_region.top,
	         upd_desc_list->upd_data.update_region.width,
	         upd_desc_list->upd_data.update_region.height,
	         upd_desc_list->upd_data.waveform_mode,
	         upd_desc_list->update_order);
}

static void dump_pending_list(struct mxc_epdc_fb_data *fb_data)
{
	struct update_desc_list *plist;

	dev_info(fb_data->dev, "Queue:\n");
	if (list_empty(&fb_data->upd_pending_list))
		dev_info(fb_data->dev, "Empty");
	list_for_each_entry(plist, &fb_data->upd_pending_list, list)
		dump_desc_data(fb_data->dev, plist);
}

static void dump_all_updates(struct mxc_epdc_fb_data *fb_data)
{
	dump_free_list(fb_data);
	dump_queue(fb_data);
	dump_collision_list(fb_data);
	dev_info(fb_data->dev, "Current update being processed:\n");
	if (fb_data->cur_update == NULL)
		dev_info(fb_data->dev, "No current update\n");
	else
		dump_update_data(fb_data->dev, fb_data->cur_update);
}
#else
static inline void dump_pxp_config(struct mxc_epdc_fb_data *fb_data,
				   struct pxp_config_data *pxp_conf) {}
static inline void dump_update_data(struct device *dev,
			     struct update_data_list *upd_data_list) {}
static inline void dump_collision_list(struct mxc_epdc_fb_data *fb_data) {}
static inline void dump_free_list(struct mxc_epdc_fb_data *fb_data) {}
static inline void dump_queue(struct mxc_epdc_fb_data *fb_data) {}
static inline void dump_all_updates(struct mxc_epdc_fb_data *fb_data) {}
#endif

static long long timeofday_msec(void)
{
	struct timeval tv;

	do_gettimeofday(&tv);
	return (long long)tv.tv_sec*1000 + tv.tv_usec/1000;
}

/********************************************************
 * Start Low-Level EPDC Functions
 ********************************************************/

static inline void epdc_lut_complete_intr(int rev, u32 lut_num, bool enable)
{
	if (rev < EPDC_VERSION_2_0_0) {
		if (enable)
			__raw_writel(1 << lut_num, EPDC_IRQ_MASK_SET);
		else
			__raw_writel(1 << lut_num, EPDC_IRQ_MASK_CLEAR);
	} else {
		if (enable) {
			if (lut_num < 32)
				__raw_writel(1 << lut_num, EPDC_IRQ_MASK1_SET);
			else
				__raw_writel(1 << (lut_num - 32),
					EPDC_IRQ_MASK2_SET);
		} else {
			if (lut_num < 32)
				__raw_writel(1 << lut_num,
					EPDC_IRQ_MASK1_CLEAR);
			else
				__raw_writel(1 << (lut_num - 32),
					EPDC_IRQ_MASK2_CLEAR);
		}
	}
}

static inline void epdc_working_buf_intr(bool enable)
{
	if (enable)
		__raw_writel(EPDC_IRQ_WB_CMPLT_IRQ, EPDC_IRQ_MASK_SET);
	else
		__raw_writel(EPDC_IRQ_WB_CMPLT_IRQ, EPDC_IRQ_MASK_CLEAR);
}

static inline void epdc_clear_working_buf_irq(void)
{
	__raw_writel(EPDC_IRQ_WB_CMPLT_IRQ | EPDC_IRQ_LUT_COL_IRQ,
		     EPDC_IRQ_CLEAR);
}

static inline void epdc_upd_done_intr(bool enable)
{
	if (enable)
		__raw_writel(EPDC_IRQ_UPD_DONE_IRQ, EPDC_IRQ_MASK_SET);
	else {
		__raw_writel(EPDC_IRQ_UPD_DONE_IRQ, EPDC_IRQ_MASK_CLEAR);
		__raw_writel(EPDC_IRQ_UPD_DONE_IRQ, EPDC_IRQ_CLEAR);
	}
}

static inline void epdc_eof_intr(bool enable)
{
	if (enable)
		__raw_writel(EPDC_IRQ_FRAME_END_IRQ, EPDC_IRQ_MASK_SET);
	else
		__raw_writel(EPDC_IRQ_FRAME_END_IRQ, EPDC_IRQ_MASK_CLEAR);
}

static inline void epdc_clear_eof_irq(void)
{
	__raw_writel(EPDC_IRQ_FRAME_END_IRQ, EPDC_IRQ_CLEAR);
}

static inline bool epdc_signal_eof(void)
{
	return (__raw_readl(EPDC_IRQ_MASK) & __raw_readl(EPDC_IRQ)
		& EPDC_IRQ_FRAME_END_IRQ) ? true : false;
}

static inline void epdc_set_temp(u32 temp)
{
	__raw_writel(temp, EPDC_TEMP);
}

static inline void epdc_set_screen_res(u32 width, u32 height)
{
	u32 val = (height << EPDC_RES_VERTICAL_OFFSET) | width;
	__raw_writel(val, EPDC_RES);
}

static inline void epdc_set_update_addr(u32 addr)
{
	__raw_writel(addr, EPDC_UPD_ADDR);
}

static inline void epdc_set_update_coord(u32 x, u32 y)
{
	u32 val = (y << EPDC_UPD_CORD_YCORD_OFFSET) | x;
	__raw_writel(val, EPDC_UPD_CORD);
}

static inline void epdc_set_update_dimensions(u32 width, u32 height)
{
	u32 val = (height << EPDC_UPD_SIZE_HEIGHT_OFFSET) | width;
	__raw_writel(val, EPDC_UPD_SIZE);
}

static void epdc_set_update_waveform(struct mxcfb_waveform_modes *wv_modes)
{
	u32 val;

	/* Configure the auto-waveform look-up table based on waveform modes */

	/* Entry 1 = DU, 2 = GC4, 3 = GC8, etc. */
	val = (wv_modes->mode_du << EPDC_AUTOWV_LUT_DATA_OFFSET) |
		(0 << EPDC_AUTOWV_LUT_ADDR_OFFSET);
	__raw_writel(val, EPDC_AUTOWV_LUT);
	val = (wv_modes->mode_du << EPDC_AUTOWV_LUT_DATA_OFFSET) |
		(1 << EPDC_AUTOWV_LUT_ADDR_OFFSET);
	__raw_writel(val, EPDC_AUTOWV_LUT);
	val = (wv_modes->mode_gc4 << EPDC_AUTOWV_LUT_DATA_OFFSET) |
		(2 << EPDC_AUTOWV_LUT_ADDR_OFFSET);
	__raw_writel(val, EPDC_AUTOWV_LUT);
	val = (wv_modes->mode_gc8 << EPDC_AUTOWV_LUT_DATA_OFFSET) |
		(3 << EPDC_AUTOWV_LUT_ADDR_OFFSET);
	__raw_writel(val, EPDC_AUTOWV_LUT);
	val = (wv_modes->mode_gc16 << EPDC_AUTOWV_LUT_DATA_OFFSET) |
		(4 << EPDC_AUTOWV_LUT_ADDR_OFFSET);
	__raw_writel(val, EPDC_AUTOWV_LUT);
	val = (wv_modes->mode_gc32 << EPDC_AUTOWV_LUT_DATA_OFFSET) |
		(5 << EPDC_AUTOWV_LUT_ADDR_OFFSET);
	__raw_writel(val, EPDC_AUTOWV_LUT);
}

static void epdc_set_update_stride(u32 stride)
{
	__raw_writel(stride, EPDC_UPD_STRIDE);
}

static void epdc_submit_update(u32 lut_num, u32 waveform_mode, u32 update_mode,
                               bool use_dry_run, bool update_pause,
                               bool use_test_mode, u32 np_val)
{
	u32 reg_val = 0;

	if (use_test_mode) {
		reg_val |=
		    ((np_val << EPDC_UPD_FIXED_FIXNP_OFFSET) &
		     EPDC_UPD_FIXED_FIXNP_MASK) | EPDC_UPD_FIXED_FIXNP_EN;

		__raw_writel(reg_val, EPDC_UPD_FIXED);

		reg_val = EPDC_UPD_CTRL_USE_FIXED;
		reg_val |= EPDC_UPD_CTRL_NO_LUT_CANCEL;
	} else {
		__raw_writel(reg_val, EPDC_UPD_FIXED);
	}

	if (waveform_mode == WAVEFORM_MODE_AUTO)
		reg_val |= EPDC_UPD_CTRL_AUTOWV;
	else
		reg_val |= ((waveform_mode <<
				EPDC_UPD_CTRL_WAVEFORM_MODE_OFFSET) &
				EPDC_UPD_CTRL_WAVEFORM_MODE_MASK);

	if (update_pause)
		reg_val |= EPDC_UPD_CTRL_AUTOWV_PAUSE;

	reg_val |= (use_dry_run ? EPDC_UPD_CTRL_DRY_RUN : 0) |
	    ((lut_num << EPDC_UPD_CTRL_LUT_SEL_OFFSET) &
	     EPDC_UPD_CTRL_LUT_SEL_MASK) |
	    update_mode;

	__raw_writel(reg_val, EPDC_UPD_CTRL);
}

static void epdc_submit_paused_update(int waveform)
{
	u32 reg_val = __raw_readl(EPDC_UPD_CTRL);
	if (waveform != WAVEFORM_MODE_AUTO)
	{
		/* We've decided on a waveform */
		reg_val &= ~EPDC_UPD_CTRL_WAVEFORM_MODE_MASK;
		reg_val |= ((waveform << EPDC_UPD_CTRL_WAVEFORM_MODE_OFFSET) &
				EPDC_UPD_CTRL_WAVEFORM_MODE_MASK);
	}
	reg_val &= ~EPDC_UPD_CTRL_AUTOWV_PAUSE;
	__raw_writel(reg_val, EPDC_UPD_CTRL);
}

static inline bool epdc_is_lut_complete(int rev, u32 lut_num)
{
	u32 val;
	bool is_compl;
	if (rev < EPDC_VERSION_2_0_0) {
		val = __raw_readl(EPDC_IRQ);
		is_compl = val & (1 << lut_num) ? true : false;
	} else if (lut_num < 32) {
		val = __raw_readl(EPDC_IRQ1);
		is_compl = val & (1 << lut_num) ? true : false;
	} else {
		val = __raw_readl(EPDC_IRQ2);
		is_compl = val & (1 << (lut_num - 32)) ? true : false;
	}

	return is_compl;
}

static inline void epdc_clear_lut_complete_irq(int rev, u32 lut_num)
{
	if (rev < EPDC_VERSION_2_0_0)
		__raw_writel(1 << lut_num, EPDC_IRQ_CLEAR);
	else if (lut_num < 32)
		__raw_writel(1 << lut_num, EPDC_IRQ1_CLEAR);
	else
		__raw_writel(1 << (lut_num - 32), EPDC_IRQ2_CLEAR);
}

static inline bool epdc_is_lut_active(u32 lut_num)
{
	u32 val;
	bool is_active;

	if (lut_num < 32) {
		val = __raw_readl(EPDC_STATUS_LUTS);
		is_active = val & (1 << lut_num) ? true : false;
	} else {
		val = __raw_readl(EPDC_STATUS_LUTS2);
		is_active = val & (1 << (lut_num - 32)) ? true : false;
	}

	return is_active;
}

static inline bool epdc_any_luts_active(int rev)
{
	bool any_active;

	if (rev < EPDC_VERSION_2_0_0)
		any_active = __raw_readl(EPDC_STATUS_LUTS) ? true : false;
	else
		any_active = (__raw_readl(EPDC_STATUS_LUTS) |
			__raw_readl(EPDC_STATUS_LUTS2))	? true : false;

	return any_active;
}

static inline bool epdc_any_luts_available(void)
{
	bool luts_available =
	    (__raw_readl(EPDC_STATUS_NEXTLUT) &
	     EPDC_STATUS_NEXTLUT_NEXT_LUT_VALID) ? true : false;
	return luts_available;
}

static inline int epdc_get_next_lut(void)
{
	u32 val =
	    __raw_readl(EPDC_STATUS_NEXTLUT) &
	    EPDC_STATUS_NEXTLUT_NEXT_LUT_MASK;
	return val;
}

static int epdc_choose_next_lut(int rev, int *next_lut)
{
	u64 luts_status, unprocessed_luts, used_luts;
	/* Available LUTs are reduced to 16 in 5-bit waveform mode */
	bool format_p5n = ((__raw_readl(EPDC_FORMAT) & EPDC_FORMAT_BUF_PIXEL_FORMAT_MASK) ==
	                   EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N);

	luts_status = __raw_readl(EPDC_STATUS_LUTS);
	if ((rev < EPDC_VERSION_2_0_0) || format_p5n)
		luts_status &= 0xFFFF;
	else
		luts_status |= ((u64)__raw_readl(EPDC_STATUS_LUTS2) << 32);

	if (rev < EPDC_VERSION_2_0_0) {
		unprocessed_luts = __raw_readl(EPDC_IRQ) & 0xFFFF;
	} else {
		unprocessed_luts = __raw_readl(EPDC_IRQ1) |
			((u64)__raw_readl(EPDC_IRQ2) << 32);
		if (format_p5n)
			unprocessed_luts &= 0xFFFF;
	}

	/*
	 * Note on unprocessed_luts: There is a race condition
	 * where a LUT completes, but has not been processed by
	 * IRQ handler workqueue, and then a new update request
	 * attempts to use that LUT.  We prevent that here by
	 * ensuring that the LUT we choose doesn't have its IRQ
	 * bit set (indicating it has completed but not yet been
	 * processed).
	 */
	used_luts = luts_status | unprocessed_luts;

	/*
	 * Selecting a LUT to minimize incidence of TCE Underrun Error
	 * --------------------------------------------------------
	 * We want to find the lowest order LUT that is of greater
	 * order than all other active LUTs.  If highest order LUT
	 * is active, then we want to choose the lowest order
	 * available LUT.
	 *
	 * NOTE: For EPDC version 2.0 and later, TCE Underrun error
	 *       bug is fixed, so it doesn't matter which LUT is used.
	 */

	if ((rev < EPDC_VERSION_2_0_0) || format_p5n) {
		*next_lut = fls64(used_luts);
		if (*next_lut > 15)
			*next_lut = ffz(used_luts);
	} else {
		if ((u32)used_luts != ~0UL)
			*next_lut = ffz((u32)used_luts);
		else if ((u32)(used_luts >> 32) != ~0UL)
			*next_lut = ffz((u32)(used_luts >> 32)) + 32;
		else
			*next_lut = INVALID_LUT;
	}

	if (used_luts & 0x8000)
		return 1;
	else
		return 0;
}

static inline bool epdc_is_working_buffer_busy(void)
{
	u32 val = __raw_readl(EPDC_STATUS);
	bool is_busy = (val & EPDC_STATUS_WB_BUSY) ? true : false;

	return is_busy;
}

static inline bool epdc_is_working_buffer_complete(void)
{
	u32 val = __raw_readl(EPDC_IRQ);
	bool is_compl = (val & EPDC_IRQ_WB_CMPLT_IRQ) ? true : false;

	return is_compl;
}

static inline bool epdc_is_lut_cancelled(void)
{
	u32 val = __raw_readl(EPDC_STATUS);
	bool is_void = (val & EPDC_STATUS_UPD_VOID) ? true : false;

	return is_void;
}

static inline bool epdc_is_collision(void)
{
	u32 val = __raw_readl(EPDC_IRQ);
	return (val & EPDC_IRQ_LUT_COL_IRQ) ? true : false;
}

static inline u64 epdc_get_colliding_luts(int rev)
{
	u64 val = __raw_readl(EPDC_STATUS_COL);
	if (rev >= EPDC_VERSION_2_0_0)
		val |= (u64)__raw_readl(EPDC_STATUS_COL2) << 32;
	return val;
}

static inline u8 epdc_get_histogram_np(void)
{
	u32 val = (__raw_readl(EPDC_STATUS) & EPDC_STATUS_HISTOGRAM_NP_MASK);
	return (val >> EPDC_STATUS_HISTOGRAM_NP_OFFSET);
}

static inline u8 epdc_get_waveform_mode(void)
{
	u32 val = (__raw_readl(EPDC_UPD_CTRL) & EPDC_UPD_CTRL_WAVEFORM_MODE_MASK);
	return (val >> EPDC_UPD_CTRL_WAVEFORM_MODE_OFFSET);
}

static void epdc_set_horizontal_timing(u32 horiz_start, u32 horiz_end,
				       u32 hsync_width, u32 hsync_line_length)
{
	u32 reg_val =
	    ((hsync_width << EPDC_TCE_HSCAN1_LINE_SYNC_WIDTH_OFFSET) &
	     EPDC_TCE_HSCAN1_LINE_SYNC_WIDTH_MASK)
	    | ((hsync_line_length << EPDC_TCE_HSCAN1_LINE_SYNC_OFFSET) &
	       EPDC_TCE_HSCAN1_LINE_SYNC_MASK);
	__raw_writel(reg_val, EPDC_TCE_HSCAN1);

	reg_val =
	    ((horiz_start << EPDC_TCE_HSCAN2_LINE_BEGIN_OFFSET) &
	     EPDC_TCE_HSCAN2_LINE_BEGIN_MASK)
	    | ((horiz_end << EPDC_TCE_HSCAN2_LINE_END_OFFSET) &
	       EPDC_TCE_HSCAN2_LINE_END_MASK);
	__raw_writel(reg_val, EPDC_TCE_HSCAN2);
}

static void epdc_set_vertical_timing(u32 vert_start, u32 vert_end,
				     u32 vsync_width)
{
	u32 reg_val =
	    ((vert_start << EPDC_TCE_VSCAN_FRAME_BEGIN_OFFSET) &
	     EPDC_TCE_VSCAN_FRAME_BEGIN_MASK)
	    | ((vert_end << EPDC_TCE_VSCAN_FRAME_END_OFFSET) &
	       EPDC_TCE_VSCAN_FRAME_END_MASK)
	    | ((vsync_width << EPDC_TCE_VSCAN_FRAME_SYNC_OFFSET) &
	       EPDC_TCE_VSCAN_FRAME_SYNC_MASK);
	__raw_writel(reg_val, EPDC_TCE_VSCAN);
}

static void epdc_set_hist_params(struct mxc_epdc_fb_data *fb_data)
{
#define HIST_PARAM(value, pos, shift) \
	(((value << shift) << EPDC_HIST_PARAM_VALUE ## pos ## _OFFSET) & EPDC_HIST_PARAM_VALUE ## pos ## _MASK)

	int shift = (fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N) ? 1 : 0;
	__raw_writel(HIST_PARAM(0x00, 0,  shift), EPDC_HIST1_PARAM);

	__raw_writel(HIST_PARAM(0x00, 0,  shift) |
	             HIST_PARAM(0x0F, 1,  shift), EPDC_HIST2_PARAM);

	__raw_writel(HIST_PARAM(0x00, 0,  shift) |
	             HIST_PARAM(0x05, 1,  shift) |
	             HIST_PARAM(0x0A, 2,  shift) |
	             HIST_PARAM(0x0F, 3,  shift), EPDC_HIST4_PARAM);

	__raw_writel(HIST_PARAM(0x00, 0,  shift) |
	             HIST_PARAM(0x02, 1,  shift) |
	             HIST_PARAM(0x04, 2,  shift) |
	             HIST_PARAM(0x06, 3,  shift), EPDC_HIST8_PARAM0);
	__raw_writel(HIST_PARAM(0x09, 4,  shift) |
	             HIST_PARAM(0x0B, 5,  shift) |
	             HIST_PARAM(0x0D, 6,  shift) |
	             HIST_PARAM(0x0F, 7,  shift), EPDC_HIST8_PARAM1);

	__raw_writel(HIST_PARAM(0x00, 0,  shift) |
	             HIST_PARAM(0x01, 1,  shift) |
	             HIST_PARAM(0x02, 2,  shift) |
	             HIST_PARAM(0x03, 3,  shift), EPDC_HIST16_PARAM0);
	__raw_writel(HIST_PARAM(0x04, 4,  shift) |
	             HIST_PARAM(0x05, 5,  shift) |
	             HIST_PARAM(0x06, 6,  shift) |
	             HIST_PARAM(0x07, 7,  shift), EPDC_HIST16_PARAM1);
	__raw_writel(HIST_PARAM(0x08, 8,  shift) |
	             HIST_PARAM(0x09, 9,  shift) |
	             HIST_PARAM(0x0A, 10, shift) |
	             HIST_PARAM(0x0B, 11, shift), EPDC_HIST16_PARAM2);
	__raw_writel(HIST_PARAM(0x0C, 12, shift) |
	             HIST_PARAM(0x0D, 13, shift) |
	             HIST_PARAM(0x0E, 14, shift) |
	             HIST_PARAM(0x0F, 15, shift), EPDC_HIST16_PARAM3);

#undef HIST_PARAM
}

static void epdc_init_settings(struct mxc_epdc_fb_data *fb_data)
{
	struct imx_epdc_fb_mode *epdc_mode = fb_data->cur_mode;
	struct fb_var_screeninfo *screeninfo = &fb_data->epdc_fb_var;
	u32 reg_val;
	int num_ce;
	int i;

	/* Enable clocks to access EPDC regs */
	clk_enable(fb_data->epdc_clk_axi);
	clk_enable(fb_data->epdc_clk_pix);

	/* Reset */
	__raw_writel(EPDC_CTRL_SFTRST, EPDC_CTRL_SET);
	while (!(__raw_readl(EPDC_CTRL) & EPDC_CTRL_CLKGATE))
		;
	__raw_writel(EPDC_CTRL_SFTRST, EPDC_CTRL_CLEAR);

	/* Enable clock gating (clear to enable) */
	__raw_writel(EPDC_CTRL_CLKGATE, EPDC_CTRL_CLEAR);
	while (__raw_readl(EPDC_CTRL) & (EPDC_CTRL_SFTRST | EPDC_CTRL_CLKGATE))
		;

	/* EPDC_CTRL */
	reg_val = __raw_readl(EPDC_CTRL);
	reg_val &= ~EPDC_CTRL_UPD_DATA_SWIZZLE_MASK;
	reg_val |= EPDC_CTRL_UPD_DATA_SWIZZLE_NO_SWAP;
	reg_val &= ~EPDC_CTRL_LUT_DATA_SWIZZLE_MASK;
	reg_val |= EPDC_CTRL_LUT_DATA_SWIZZLE_NO_SWAP;
	__raw_writel(reg_val, EPDC_CTRL_SET);

	/* EPDC_FORMAT - 2bit TFT and buf_pix_fmt Buf pixel format */
	reg_val = EPDC_FORMAT_TFT_PIXEL_FORMAT_2BIT
	    | fb_data->buf_pix_fmt
	    | ((0x0 << EPDC_FORMAT_DEFAULT_TFT_PIXEL_OFFSET) &
	       EPDC_FORMAT_DEFAULT_TFT_PIXEL_MASK);
	__raw_writel(reg_val, EPDC_FORMAT);

	/*
	 * Set histogram params
	 */
	epdc_set_hist_params(fb_data);

	/* EPDC_FIFOCTRL (disabled) */
	reg_val =
	    ((100 << EPDC_FIFOCTRL_FIFO_INIT_LEVEL_OFFSET) &
	     EPDC_FIFOCTRL_FIFO_INIT_LEVEL_MASK)
	    | ((200 << EPDC_FIFOCTRL_FIFO_H_LEVEL_OFFSET) &
	       EPDC_FIFOCTRL_FIFO_H_LEVEL_MASK)
	    | ((100 << EPDC_FIFOCTRL_FIFO_L_LEVEL_OFFSET) &
	       EPDC_FIFOCTRL_FIFO_L_LEVEL_MASK);
	__raw_writel(reg_val, EPDC_FIFOCTRL);

	/* EPDC_TEMP - Use default temp to get index */
	epdc_set_temp(mxc_epdc_fb_get_temp_index(fb_data, DEFAULT_TEMP));

	/* EPDC_RES */
	epdc_set_screen_res(epdc_mode->vmode->xres, epdc_mode->vmode->yres);

	/* EPDC_AUTOWV_LUT */
	/* Initialize all auto-waveform look-up values to 2 - GC16 */
	for (i = 0; i < 8; i++)
		__raw_writel((2 << EPDC_AUTOWV_LUT_DATA_OFFSET) |
			(i << EPDC_AUTOWV_LUT_ADDR_OFFSET), EPDC_AUTOWV_LUT);

	/*
	 * EPDC_TCE_CTRL
	 * VSCAN_HOLDOFF = 4
	 * VCOM_MODE = MANUAL
	 * VCOM_VAL = 0
	 * DDR_MODE = DISABLED
	 * LVDS_MODE_CE = DISABLED
	 * LVDS_MODE = DISABLED
	 * DUAL_SCAN = DISABLED
	 * SDDO_WIDTH = 8bit
	 * PIXELS_PER_SDCLK = 4
	 */
	reg_val =
	    ((epdc_mode->vscan_holdoff << EPDC_TCE_CTRL_VSCAN_HOLDOFF_OFFSET) &
	     EPDC_TCE_CTRL_VSCAN_HOLDOFF_MASK)
	    | EPDC_TCE_CTRL_PIXELS_PER_SDCLK_4;
	if (epdc_mode->vmode->flag & FLAG_SCAN_X_INVERT)
		reg_val |= EPDC_TCE_CTRL_SCAN_DIR_0_UP;
	__raw_writel(reg_val, EPDC_TCE_CTRL);

	/* EPDC_TCE_HSCAN */
	epdc_set_horizontal_timing(screeninfo->left_margin,
				   screeninfo->right_margin,
				   screeninfo->hsync_len,
				   screeninfo->hsync_len);

	/* EPDC_TCE_VSCAN */
	epdc_set_vertical_timing(screeninfo->upper_margin,
				 screeninfo->lower_margin,
				 screeninfo->vsync_len);

	/* EPDC_TCE_OE */
	reg_val =
	    ((epdc_mode->sdoed_width << EPDC_TCE_OE_SDOED_WIDTH_OFFSET) &
	     EPDC_TCE_OE_SDOED_WIDTH_MASK)
	    | ((epdc_mode->sdoed_delay << EPDC_TCE_OE_SDOED_DLY_OFFSET) &
	       EPDC_TCE_OE_SDOED_DLY_MASK)
	    | ((epdc_mode->sdoez_width << EPDC_TCE_OE_SDOEZ_WIDTH_OFFSET) &
	       EPDC_TCE_OE_SDOEZ_WIDTH_MASK)
	    | ((epdc_mode->sdoez_delay << EPDC_TCE_OE_SDOEZ_DLY_OFFSET) &
	       EPDC_TCE_OE_SDOEZ_DLY_MASK);
	__raw_writel(reg_val, EPDC_TCE_OE);

	/* EPDC_TCE_TIMING1 */
	__raw_writel(0x0, EPDC_TCE_TIMING1);

	/* EPDC_TCE_TIMING2 */
	reg_val =
	    ((epdc_mode->gdclk_hp_offs << EPDC_TCE_TIMING2_GDCLK_HP_OFFSET) &
	     EPDC_TCE_TIMING2_GDCLK_HP_MASK)
	    | ((epdc_mode->gdsp_offs << EPDC_TCE_TIMING2_GDSP_OFFSET_OFFSET) &
	       EPDC_TCE_TIMING2_GDSP_OFFSET_MASK);
	__raw_writel(reg_val, EPDC_TCE_TIMING2);

	/* EPDC_TCE_TIMING3 */
	reg_val =
	    ((epdc_mode->gdoe_offs << EPDC_TCE_TIMING3_GDOE_OFFSET_OFFSET) &
	     EPDC_TCE_TIMING3_GDOE_OFFSET_MASK)
	    | ((epdc_mode->gdclk_offs << EPDC_TCE_TIMING3_GDCLK_OFFSET_OFFSET) &
	       EPDC_TCE_TIMING3_GDCLK_OFFSET_MASK);
	__raw_writel(reg_val, EPDC_TCE_TIMING3);

	/*
	 * EPDC_TCE_SDCFG
	 * SDCLK_HOLD = 1
	 * SDSHR = 1
	 * NUM_CE = 1
	 * SDDO_REFORMAT = FLIP_PIXELS
	 * SDDO_INVERT = DISABLED
	 * PIXELS_PER_CE = display horizontal resolution
	 */
	num_ce = epdc_mode->num_ce;
	if (num_ce == 0)
		num_ce = 1;
	reg_val = EPDC_TCE_SDCFG_SDCLK_HOLD | EPDC_TCE_SDCFG_SDSHR
	    | ((num_ce << EPDC_TCE_SDCFG_NUM_CE_OFFSET) &
	       EPDC_TCE_SDCFG_NUM_CE_MASK)
	    | EPDC_TCE_SDCFG_SDDO_REFORMAT_FLIP_PIXELS
	    | ((epdc_mode->vmode->xres/num_ce << EPDC_TCE_SDCFG_PIXELS_PER_CE_OFFSET) &
	       EPDC_TCE_SDCFG_PIXELS_PER_CE_MASK);
	__raw_writel(reg_val, EPDC_TCE_SDCFG);

	/*
	 * EPDC_TCE_GDCFG
	 * GDRL = 1
	 * GDOE_MODE = 0;
	 * GDSP_MODE = 0;
	 */
	reg_val = EPDC_TCE_SDCFG_GDRL;
	__raw_writel(reg_val, EPDC_TCE_GDCFG);

	/*
	 * EPDC_TCE_POLARITY
	 * SDCE_POL = ACTIVE LOW
	 * SDLE_POL = ACTIVE HIGH
	 * SDOE_POL = ACTIVE HIGH
	 * GDOE_POL = ACTIVE HIGH
	 * GDSP_POL = ACTIVE LOW
	 */
	reg_val = EPDC_TCE_POLARITY_SDLE_POL_ACTIVE_HIGH
	    | EPDC_TCE_POLARITY_SDOE_POL_ACTIVE_HIGH
	    | EPDC_TCE_POLARITY_GDOE_POL_ACTIVE_HIGH;
	__raw_writel(reg_val, EPDC_TCE_POLARITY);

	/* EPDC_IRQ_MASK */
	__raw_writel(EPDC_IRQ_TCE_UNDERRUN_IRQ, EPDC_IRQ_MASK);

	/*
	 * EPDC_GPIO
	 * PWRCOM = ?
	 * PWRCTRL = ?
	 * BDR = ?
	 */
	reg_val = ((0 << EPDC_GPIO_PWRCTRL_OFFSET) & EPDC_GPIO_PWRCTRL_MASK)
	    | ((0 << EPDC_GPIO_BDR_OFFSET) & EPDC_GPIO_BDR_MASK);
	__raw_writel(reg_val, EPDC_GPIO);

	__raw_writel(fb_data->waveform_buffer_phys, EPDC_WVADDR);
	__raw_writel(fb_data->working_buffer_A_phys, EPDC_WB_ADDR);
	__raw_writel(fb_data->working_buffer_A_phys, EPDC_WB_ADDR_TCE);

	/* Disable clock */
	clk_disable(fb_data->epdc_clk_pix);
	clk_disable(fb_data->epdc_clk_axi);
}

static inline __u32 get_waveform_by_type(struct mxc_epdc_fb_data *fb_data, __u32 waveform)
{
	switch(waveform)
	{
		case(WAVEFORM_MODE_INIT):
			return fb_data->wv_modes.mode_init;
		case(WAVEFORM_MODE_AUTO):
			return WAVEFORM_MODE_AUTO;
		case(WAVEFORM_MODE_DU):
			return fb_data->wv_modes.mode_du;
		case(WAVEFORM_MODE_GC16):
			return fb_data->wv_modes.mode_gc16;
		case(WAVEFORM_MODE_GC16_FAST):
			return fb_data->wv_modes.mode_gc16_fast;
		case (WAVEFORM_MODE_A2):
			return fb_data->wv_modes.mode_a2;
		case(WAVEFORM_MODE_GL16):
			return fb_data->wv_modes.mode_gl16;
		case(WAVEFORM_MODE_GL16_FAST):
			return fb_data->wv_modes.mode_gl16_fast;
		case(WAVEFORM_MODE_DU4):
			return fb_data->wv_modes.mode_du4;
		case(WAVEFORM_MODE_REAGLD):
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
			return fb_data->wv_modes.mode_reagld;
#else
			// Fallthrough
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B
		case(WAVEFORM_MODE_REAGL):
			return fb_data->wv_modes.mode_reagl;
		case(WAVEFORM_MODE_GL16_INV):
			return fb_data->wv_modes.mode_gl16_inv;
		case(WAVEFORM_MODE_GL4):
			return fb_data->wv_modes.mode_gl4;
		default:
			return WAVEFORM_MODE_AUTO;
	}
}

static int epdc_powerup(struct mxc_epdc_fb_data *fb_data)
{
	int ret = 0;

	/*
	 * If power down request is pending, clear
	 * powering_down to cancel the request.
	 */
	fb_data->powering_down = false;
	fb_data->updates_active = true;

	if (fb_data->power_state == POWER_STATE_ON  || fb_data->power_state == POWER_STATE_GOING_UP) {
		return 1;
	}

	dev_dbg(fb_data->dev, "EPDC Powerup: %d\n", fb_data->power_state);
	fb_data->power_state = POWER_STATE_GOING_UP;

	/* Enable power to the EPD panel */
	ret = regulator_enable(fb_data->display_regulator);
	if (IS_ERR((void *)ret)) {
		dev_err(fb_data->dev, "Unable to enable Display regulator."
			"err = 0x%x\n", ret);
		fb_data->power_state = POWER_STATE_OFF;
		return 0;
	}

	/* Enable pins used by EPDC */
	if (fb_data->pdata->enable_pins)
		fb_data->pdata->enable_pins();

	/* Enable clocks to EPDC */
	clk_enable(fb_data->epdc_clk_axi);
	clk_enable(fb_data->epdc_clk_pix);

	__raw_writel(EPDC_CTRL_CLKGATE, EPDC_CTRL_CLEAR);

	dev_dbg(fb_data->dev, "EPDC Powerup out state: %d\n", fb_data->power_state);

	return 1;
}

static int epdc_powerup_wait_for_enabled(struct mxc_epdc_fb_data *fb_data)
{
	int ret;
	if (wait_for_completion_timeout(&fb_data->powerup_compl, msecs_to_jiffies(PWRUP_TIMEOUT)))
	{
		dev_dbg(fb_data->dev, "%s: Power UP completion received\n", __FUNCTION__);
		return 1;
	}

	ret = regulator_is_enabled(fb_data->display_regulator);
	if (ret < 0) {
		dev_err(fb_data->dev, "Unable to get regulator_is_enabled"
			"err = 0x%x\n", ret);
	} else if (ret > 0) {
		dev_err(fb_data->dev, "%s: Power UP timed out but actually enabled\n", __FUNCTION__);
		complete(&fb_data->powerup_compl);
		return 1;
	}

	LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
	                   "kernel", DRIVER_NAME, "powerup_timeout", 1, "");

	mutex_lock(&fb_data->power_mutex);
	epdc_force_powerdown(fb_data);
	mutex_unlock(&fb_data->power_mutex);
	return 0;
}

/*
 * Sets VCOM based on VCI VCOM offset data for a given waveform and temperature range
 */
static void do_vcom_offset(struct mxc_epdc_fb_data *fb_data, int waveform_mode, int waveform_temp_range)
{
	int vcom_uV;
	struct epd_vc_data_volts vcdv;
	int ret;

	if (fb_data->wv_header->vcom_shifted)
		return;

	if (fetch_epdc_pmic_voltages(&vcdv, fb_data, waveform_mode, waveform_temp_range) < 0)
		vcdv.vcom_v = 0;

	vcom_uV = fb_data->vcom_uV + vcdv.vcom_v;
	dev_dbg(fb_data->dev, "Setting VCOM to %duV (base: %duV offset: %duV)\n", -vcom_uV, fb_data->vcom_uV, vcdv.vcom_v);
	ret = regulator_set_voltage(fb_data->vcom_regulator, -vcom_uV, -vcom_uV);
	if (ret)
		dev_err(fb_data->dev, "unable to set VCOM = %dmV (err = %d)\n", uV_to_mV(vcom_uV), ret);
	else
		dev_dbg(fb_data->dev, "VCOM = %dmV\n", uV_to_mV(regulator_get_voltage(fb_data->vcom_regulator)));
}

static int epdc_powerup_vcom(struct mxc_epdc_fb_data *fb_data, int waveform_mode, int temp)
{
	int ret = 0;

	if (fb_data->power_state != POWER_STATE_GOING_UP)
		return 0;

	do_vcom_offset(fb_data, waveform_mode, temp);

	ret = regulator_enable(fb_data->vcom_regulator);
	if (IS_ERR((void *)ret)) {
		dev_err(fb_data->dev, "Unable to enable VCOM regulator."
		        "err = 0x%x\n", ret);
		regulator_disable(fb_data->display_regulator);
		fb_data->power_state = POWER_STATE_OFF;
		return 0;
	}

	fb_data->power_state = POWER_STATE_ON;
	dev_dbg(fb_data->dev, "%s: Power state: %d\n", __FUNCTION__,  fb_data->power_state);
	return 1;
}

/* This callback runs as a workqueue */
static void pmic_pwrenable_cb(void *obj, void *param)
{
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)param;

	// Completion should only be triggered if we've just powered up
	mutex_lock(&fb_data->power_mutex);
	if (fb_data->power_state == POWER_STATE_GOING_UP)
	{
		// trigger powerup complete now
		dev_dbg(fb_data->dev, "Triggering Power UP completion\n");
		complete(&fb_data->powerup_compl);
	}
	mutex_unlock(&fb_data->power_mutex);
}

static void pmic_fault_cb(void *obj, void *param)
{
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)param;
	int ret;

	mutex_lock(&fb_data->power_mutex);
	printk(KERN_ERR "Regulator Fault Occurred! Retrying powerup...\n");
	if (fb_data->power_state == POWER_STATE_GOING_UP)
	{
		// Disable power to the EPD panel
		regulator_disable(fb_data->display_regulator);

		msleep(1);

		// Enable power to the EPD panel
		ret = regulator_enable(fb_data->display_regulator);
		if (ret < 0)
			dev_err(fb_data->dev, "Unable to enable display regulator. err = 0x%x\n", ret);
	}
	mutex_unlock(&fb_data->power_mutex);
}

static void epdc_powerdown(struct mxc_epdc_fb_data *fb_data)
{
	/* If powering_down has been cleared, a powerup
	 * request is pre-empting this powerdown request.
	 */
	if (!fb_data->powering_down
		|| (fb_data->power_state == POWER_STATE_OFF)
		|| (fb_data->updates_active)) {
		return;
	}

	/* Reset counter to reduce chance of overflow */
	fb_data->order_cnt = 0;

	dev_dbg(fb_data->dev, "EPDC Powerdown. Power state: %d\n", fb_data->power_state);

	/* Disable power to the EPD panel */
	if (fb_data->power_state == POWER_STATE_ON)
		regulator_disable(fb_data->vcom_regulator);
	regulator_disable(fb_data->display_regulator);

	/* Disable clocks to EPDC */
	__raw_writel(EPDC_CTRL_CLKGATE, EPDC_CTRL_SET);
	clk_disable(fb_data->epdc_clk_pix);
	clk_disable(fb_data->epdc_clk_axi);

	/* Disable pins used by EPDC (to prevent leakage current) */
	if (fb_data->pdata->disable_pins)
		fb_data->pdata->disable_pins();

	fb_data->power_state = POWER_STATE_OFF;
	fb_data->powering_down = false;
	init_completion(&fb_data->powerup_compl);
	dev_dbg(fb_data->dev, "%s: Power state: %d\n", __FUNCTION__, fb_data->power_state);

	if (fb_data->wait_for_powerdown) {
		fb_data->wait_for_powerdown = false;
		complete(&fb_data->powerdown_compl);
	}
}

static void epdc_force_powerdown(struct mxc_epdc_fb_data *fb_data)
{
	dev_dbg(fb_data->dev, "EPDC Forced Powerdown\n");

	/* Disable power to the EPD panel */
	switch (fb_data->power_state) {
	case POWER_STATE_OFF:
		return;
	case POWER_STATE_ON:
		regulator_disable(fb_data->vcom_regulator);
		// Fallthrough
	case POWER_STATE_GOING_UP:
		regulator_disable(fb_data->display_regulator);
	}

	/* Disable clocks to EPDC */
	__raw_writel(EPDC_CTRL_CLKGATE, EPDC_CTRL_SET);
	clk_disable(fb_data->epdc_clk_pix);
	clk_disable(fb_data->epdc_clk_axi);

	/* Disable pins used by EPDC (to prevent leakage current) */
	if (fb_data->pdata->disable_pins)
		fb_data->pdata->disable_pins();

	fb_data->power_state = POWER_STATE_OFF;
	fb_data->powering_down = false;

	if (fb_data->wait_for_powerdown) {
		fb_data->wait_for_powerdown = false;
		complete(&fb_data->powerdown_compl);
	}
}

static int epdc_init_sequence(struct mxc_epdc_fb_data *fb_data)
{
	int attempts = MAX_INIT_RETRIES;
	int ret;

	fb_data->in_init = true;

	do {
		/* Initialize EPDC, passing pointer to EPDC registers */
		epdc_init_settings(fb_data);
		mutex_lock(&fb_data->power_mutex);
		if (!epdc_powerup(fb_data))
		{
			dev_err(fb_data->dev, "EPDC powerup timeout/error.\n");
			ret = -EHWFAULT;
			goto cleanup;
		}

		mutex_unlock(&fb_data->power_mutex);
		if (!epdc_powerup_wait_for_enabled(fb_data))
		{
			dev_err(fb_data->dev, "EPDC powerup timeout/error.\n");
			mutex_lock(&fb_data->power_mutex);
			ret = -EHWFAULT;
			goto cleanup;
		}
		mutex_lock(&fb_data->power_mutex);
		epdc_powerup_vcom(fb_data, WAVEFORM_MODE_INIT, fb_data->temp_index);
		ret = draw_mode0(fb_data);
		if (ret) {
			LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
			                   "kernel", DRIVER_NAME, "mode0_failure", 1, "");
		}

	cleanup:
		/* Force power down event */
		epdc_force_powerdown(fb_data);
		fb_data->updates_active = false;
		mutex_unlock(&fb_data->power_mutex);
	} while (--attempts && ret);

	return ret;
}

static int mxc_epdc_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	u32 len;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	if (offset < info->fix.smem_len) {
		/* mapping framebuffer memory */
		len = info->fix.smem_len - offset;
		vma->vm_pgoff = (info->fix.smem_start + offset) >> PAGE_SHIFT;
	} else
		return -EINVAL;

	len = PAGE_ALIGN(len);
	if (vma->vm_end - vma->vm_start > len)
		return -EINVAL;

	/* make buffers bufferable */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	vma->vm_flags |= VM_IO | VM_RESERVED;

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		dev_dbg(info->device, "mmap remap_pfn_range failed\n");
		return -ENOBUFS;
	}

	return 0;
}

static inline u_int _chan_to_field(u_int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int mxc_epdc_fb_setcolreg(u_int regno, u_int red, u_int green,
				 u_int blue, u_int transp, struct fb_info *info)
{
	unsigned int val;
	int ret = 1;

	/*
	 * If greyscale is true, then we convert the RGB value
	 * to greyscale no matter what visual we are using.
	 */
	/* LAB126 */
	/*if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
				      7471 * blue) >> 16;*/
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/*
		 * 16-bit True Colour.  We encode the RGB value
		 * according to the RGB bitfield information.
		 */
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val = _chan_to_field(red, &info->var.red);
			val |= _chan_to_field(green, &info->var.green);
			val |= _chan_to_field(blue, &info->var.blue);

			pal[regno] = val;
			ret = 0;
		}
		break;

	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		break;
	}

	return ret;
}

static int mxc_epdc_fb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	int count, index, r;
	u16 *red, *green, *blue, *transp;
	u16 trans = 0xffff;
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;
	int i;

	if (info->fix.visual == FB_VISUAL_STATIC_PSEUDOCOLOR) {
		/* Only support an 8-bit, 256 entry lookup */
		if (cmap->len != 256)
			return 1;

		mxc_epdc_fb_flush_updates(fb_data);

		mutex_lock(&fb_data->pxp_mutex);
		/*
		 * Store colormap in pxp_conf structure for later transmit
		 * to PxP during update process to convert gray pixels.
		 *
		 * Since red=blue=green for pseudocolor visuals, we can
		 * just use red values.
		 */
		for (i = 0; i < 256; i++)
			fb_data->pxp_conf.proc_data.lut_map[i] = cmap->red[i] & 0xFF;

		fb_data->pxp_conf.proc_data.lut_map_updated = true;

		use_cmap = 1;
		mutex_unlock(&fb_data->pxp_mutex);
	} else {
		red     = cmap->red;
		green   = cmap->green;
		blue    = cmap->blue;
		transp  = cmap->transp;
		index   = cmap->start;

		for (count = 0; count < cmap->len; count++) {
			if (transp)
				trans = *transp++;
			r = mxc_epdc_fb_setcolreg(index++, *red++, *green++, *blue++,
						trans, info);
			if (r != 0)
				return r;
		}
	}

	return 0;
}

static void adjust_coordinates(u32 xres, u32 yres, u32 rotation,
	struct mxcfb_rect *update_region, struct mxcfb_rect *adj_update_region)
{
	u32 temp;

	/* If adj_update_region == NULL, pass result back in update_region */
	/* If adj_update_region == valid, use it to pass back result */
	if (adj_update_region)
		switch (rotation) {
		case FB_ROTATE_UR:
			adj_update_region->top = update_region->top;
			adj_update_region->left = update_region->left;
			adj_update_region->width = update_region->width;
			adj_update_region->height = update_region->height;
			break;
		case FB_ROTATE_CW:
			adj_update_region->top = update_region->left;
			adj_update_region->left = yres -
				(update_region->top + update_region->height);
			adj_update_region->width = update_region->height;
			adj_update_region->height = update_region->width;
			break;
		case FB_ROTATE_UD:
			adj_update_region->width = update_region->width;
			adj_update_region->height = update_region->height;
			adj_update_region->top = yres -
				(update_region->top + update_region->height);
			adj_update_region->left = xres -
				(update_region->left + update_region->width);
			break;
		case FB_ROTATE_CCW:
			adj_update_region->left = update_region->top;
			adj_update_region->top = xres -
				(update_region->left + update_region->width);
			adj_update_region->width = update_region->height;
			adj_update_region->height = update_region->width;
			break;
		}
	else
		switch (rotation) {
		case FB_ROTATE_UR:
			/* No adjustment needed */
			break;
		case FB_ROTATE_CW:
			temp = update_region->top;
			update_region->top = update_region->left;
			update_region->left = yres -
				(temp + update_region->height);
			temp = update_region->width;
			update_region->width = update_region->height;
			update_region->height = temp;
			break;
		case FB_ROTATE_UD:
			update_region->top = yres -
				(update_region->top + update_region->height);
			update_region->left = xres -
				(update_region->left + update_region->width);
			break;
		case FB_ROTATE_CCW:
			temp = update_region->left;
			update_region->left = update_region->top;
			update_region->top = xres -
				(temp + update_region->width);
			temp = update_region->width;
			update_region->width = update_region->height;
			update_region->height = temp;
			break;
		}
}

/*
 * Set fixed framebuffer parameters based on variable settings.
 *
 * @param       info     framebuffer information pointer
 */
static int mxc_epdc_fb_set_fix(struct fb_info *info)
{
	struct fb_fix_screeninfo *fix = &info->fix;
	struct fb_var_screeninfo *var = &info->var;

	fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->accel = FB_ACCEL_NONE;
	if (var->grayscale)
		fix->visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep = 1;
	fix->ypanstep = 1;

	return 0;
}

/*
 * This routine actually sets the video mode. It's in here where we
 * the hardware state info->par and fix which can be affected by the
 * change in par. For this driver it doesn't do much.
 *
 */
static int mxc_epdc_fb_set_par(struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;
	struct pxp_config_data *pxp_conf = &fb_data->pxp_conf;
	struct pxp_proc_data *proc_data = &pxp_conf->proc_data;
	struct fb_var_screeninfo *screeninfo = &fb_data->info.var;
	int ret;
	__u32 xoffset_old, yoffset_old;

	/*
	 * Can't change the FB parameters until current updates have completed.
	 * This function returns when all active updates are done.
	 */
	mxc_epdc_fb_flush_updates(fb_data);

	mutex_lock(&fb_data->queue_mutex);
	/*
	 * Set all screeninfo except for xoffset/yoffset
	 * Subsequent call to pan_display will handle those.
	 */
	xoffset_old = fb_data->epdc_fb_var.xoffset;
	yoffset_old = fb_data->epdc_fb_var.yoffset;
	fb_data->epdc_fb_var = *screeninfo;
	fb_data->epdc_fb_var.xoffset = xoffset_old;
	fb_data->epdc_fb_var.yoffset = yoffset_old;
	mutex_unlock(&fb_data->queue_mutex);

	mutex_lock(&fb_data->pxp_mutex);

	/*
	 * Update PxP config data (used to process FB regions for updates)
	 * based on FB info and processing tasks required
	 */

	/* Initialize non-channel-specific PxP parameters */
	proc_data->drect.left = proc_data->srect.left = 0;
	proc_data->drect.top = proc_data->srect.top = 0;
	proc_data->drect.width = proc_data->srect.width = screeninfo->xres;
	proc_data->drect.height = proc_data->srect.height = screeninfo->yres;
	proc_data->scaling = 0;
	proc_data->hflip = 0;
	proc_data->vflip = 0;
	proc_data->rotate = screeninfo->rotate;
	proc_data->bgcolor = 0;
	proc_data->overlay_state = 0;
	proc_data->lut_transform = PXP_LUT_NONE;

	/*
	 * configure S0 channel parameters
	 * Parameters should match FB format/width/height
	 */
	if (screeninfo->grayscale)
		pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_GREY;
	else {
		switch (screeninfo->bits_per_pixel) {
		case 16:
			pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_RGB565;
			break;
		case 24:
			pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_RGB24;
			break;
		case 32:
			pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_RGB32;
			break;
		default:
			pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_RGB565;
			break;
		}
	}
	pxp_conf->s0_param.width = screeninfo->xres_virtual;
	pxp_conf->s0_param.height = screeninfo->yres;
	pxp_conf->s0_param.color_key = -1;
	pxp_conf->s0_param.color_key_enable = false;

	/*
	 * Initialize Output channel parameters
	 * Output is Y-only greyscale
	 * Output width/height will vary based on update region size
	 */
	pxp_conf->out_param.width = screeninfo->xres;
	pxp_conf->out_param.height = screeninfo->yres;
	pxp_conf->out_param.pixel_fmt = PXP_PIX_FMT_GREY;

	mutex_unlock(&fb_data->pxp_mutex);

	/*
	 * If HW not yet initialized, check to see if we are being sent
	 * an initialization request.
	 */
	if (!fb_data->hw_ready && !fb_data->hw_initializing) {
		struct fb_videomode mode;
		u32 xres_temp;

		fb_var_to_videomode(&mode, screeninfo);

		/* When comparing requested fb mode,
		   we need to use unrotated dimensions */
		if ((screeninfo->rotate == FB_ROTATE_CW) ||
			(screeninfo->rotate == FB_ROTATE_CCW)) {
			xres_temp = mode.xres;
			mode.xres = mode.yres;
			mode.yres = xres_temp;
		}

		/* Match videomode against epdc modes */
		fb_data->cur_mode = panel_choose_fbmode(fb_data);
		if (!fb_data->cur_mode) {
			dev_err(fb_data->dev,
				"Failed to match requested video mode\n");
			return EINVAL;
		}

		/* Found a match - Grab timing params */
		screeninfo->left_margin = mode.left_margin;
		screeninfo->right_margin = mode.right_margin;
		screeninfo->upper_margin = mode.upper_margin;
		screeninfo->lower_margin = mode.lower_margin;
		screeninfo->hsync_len = mode.hsync_len;
		screeninfo->vsync_len = mode.vsync_len;

		fb_data->hw_initializing = true;

		/* Initialize EPDC settings and init panel */
		ret = mxc_epdc_fb_init_hw((struct fb_info *)fb_data);
		if (ret) {
			dev_err(fb_data->dev,
				"Failed to load panel waveform and initialize\n");
			return ret;
		}
	}

	/*
	 * EOF sync delay (in us) should be equal to the vscan holdoff time
	 * VSCAN_HOLDOFF time = (VSCAN_HOLDOFF value + 1) * Vertical lines
	 * Add 25us for additional margin
	 */
	fb_data->eof_sync_period = (fb_data->cur_mode->vscan_holdoff + 1) *
		1000000/(fb_data->cur_mode->vmode->refresh *
		(fb_data->cur_mode->vmode->upper_margin +
		fb_data->cur_mode->vmode->yres +
		fb_data->cur_mode->vmode->lower_margin +
		fb_data->cur_mode->vmode->vsync_len)) + 25;

	mxc_epdc_fb_set_fix(info);

	return 0;
}

static int mxc_epdc_fb_check_var(struct fb_var_screeninfo *var,
				 struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;

	if (var->xres_virtual < var->xoffset + var->xres)
		var->xres_virtual = var->xoffset + var->xres;
	if (var->yres_virtual < var->yoffset + var->yres)
		var->yres_virtual = var->yoffset + var->yres;

	if ((var->bits_per_pixel != 32) && (var->bits_per_pixel != 24) &&
	    (var->bits_per_pixel != 16) && (var->bits_per_pixel != 8))
		var->bits_per_pixel = default_bpp;

	switch (var->bits_per_pixel) {
	case 8:
		if (var->grayscale != 0) {
			/*
			 * For 8-bit grayscale, R, G, and B offset are equal.
			 *
			 */
			var->red.length = 8;
			var->red.offset = 0;
			var->red.msb_right = 0;

			var->green.length = 8;
			var->green.offset = 0;
			var->green.msb_right = 0;

			var->blue.length = 8;
			var->blue.offset = 0;
			var->blue.msb_right = 0;

			var->transp.length = 0;
			var->transp.offset = 0;
			var->transp.msb_right = 0;
		} else {
			var->red.length = 3;
			var->red.offset = 5;
			var->red.msb_right = 0;

			var->green.length = 3;
			var->green.offset = 2;
			var->green.msb_right = 0;

			var->blue.length = 2;
			var->blue.offset = 0;
			var->blue.msb_right = 0;

			var->transp.length = 0;
			var->transp.offset = 0;
			var->transp.msb_right = 0;
		}
		break;
	case 16:
		var->red.length = 5;
		var->red.offset = 11;
		var->red.msb_right = 0;

		var->green.length = 6;
		var->green.offset = 5;
		var->green.msb_right = 0;

		var->blue.length = 5;
		var->blue.offset = 0;
		var->blue.msb_right = 0;

		var->transp.length = 0;
		var->transp.offset = 0;
		var->transp.msb_right = 0;
		break;
	case 24:
		var->red.length = 8;
		var->red.offset = 16;
		var->red.msb_right = 0;

		var->green.length = 8;
		var->green.offset = 8;
		var->green.msb_right = 0;

		var->blue.length = 8;
		var->blue.offset = 0;
		var->blue.msb_right = 0;

		var->transp.length = 0;
		var->transp.offset = 0;
		var->transp.msb_right = 0;
		break;
	case 32:
		var->red.length = 8;
		var->red.offset = 16;
		var->red.msb_right = 0;

		var->green.length = 8;
		var->green.offset = 8;
		var->green.msb_right = 0;

		var->blue.length = 8;
		var->blue.offset = 0;
		var->blue.msb_right = 0;

		var->transp.length = 8;
		var->transp.offset = 24;
		var->transp.msb_right = 0;
		break;
	}

	switch (var->rotate) {
	case FB_ROTATE_UR:
	case FB_ROTATE_UD:
		var->xres = fb_data->native_width;
		var->yres = fb_data->native_height;
		var->width = fb_data->physical_width;
		var->height = fb_data->physical_height;
		break;
	case FB_ROTATE_CW:
	case FB_ROTATE_CCW:
		var->xres = fb_data->native_height;
		var->yres = fb_data->native_width;
		var->width = fb_data->physical_height;
		var->height = fb_data->physical_width;
		break;
	default:
		/* Invalid rotation value */
		var->rotate = 0;
		dev_dbg(fb_data->dev, "Invalid rotation request\n");
		return -EINVAL;
	}

	var->xres_virtual = ALIGN(var->xres, 32);
	var->yres_virtual = ALIGN(var->yres, 128) * fb_data->num_screens;

	return 0;
}

void mxc_epdc_fb_set_waveform_modes(struct mxcfb_waveform_modes *modes,
	struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;

	mutex_lock(&fb_data->queue_mutex);

	memcpy(&fb_data->wv_modes, modes, sizeof(struct mxcfb_waveform_modes));

	/* Set flag to ensure that new waveform modes
	 * are programmed into EPDC before next update */
	fb_data->wv_modes_update = true;

	mutex_unlock(&fb_data->queue_mutex);
}
EXPORT_SYMBOL(mxc_epdc_fb_set_waveform_modes);

static int mxc_epdc_fb_get_temp_index(struct mxc_epdc_fb_data *fb_data, int temp)
{
  int i;
  int index = -1;

  if (fb_data->trt_entries == 0) {
    dev_err(fb_data->dev,
      "No TRT exists...using default temp index\n");
    return DEFAULT_TEMP_INDEX;
  }

  /* Search temperature ranges for a match */
  for (i = 0; i < fb_data->trt_entries - 1; i++) {
    if ((temp >= fb_data->temp_range_bounds[i])) {
      index = i;
      if ((temp < fb_data->temp_range_bounds[i+1])) {
        break;
      }
    }
  }

  if (index < 0) {
    dev_dbg(fb_data->dev,
      "No TRT index match...using default temp index\n");
    return DEFAULT_TEMP_INDEX;
  }

  dev_dbg(fb_data->dev, "Using temperature index %d\n", index);

  return index;
}

int mxc_epdc_fb_set_temperature(int temperature, struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;

	/* Store temp index. Used later when configuring updates. */
	mutex_lock(&fb_data->queue_mutex);
	fb_data->temp_index = mxc_epdc_fb_get_temp_index(fb_data, temperature);
	mutex_unlock(&fb_data->queue_mutex);

	return 0;
}
EXPORT_SYMBOL(mxc_epdc_fb_set_temperature);

int mxc_epdc_fb_set_auto_update(u32 auto_mode, struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;

	dev_dbg(fb_data->dev, "Setting auto update mode to %d\n", auto_mode);

	if ((auto_mode == AUTO_UPDATE_MODE_AUTOMATIC_MODE)
		|| (auto_mode == AUTO_UPDATE_MODE_REGION_MODE))
		fb_data->auto_mode = auto_mode;
	else {
		dev_err(fb_data->dev, "Invalid auto update mode parameter.\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mxc_epdc_fb_set_auto_update);

int mxc_epdc_fb_set_upd_scheme(u32 upd_scheme, struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;

	dev_dbg(fb_data->dev, "Setting optimization level to %d\n", upd_scheme);

	/*
	 * Can't change the scheme until current updates have completed.
	 * This function returns when all active updates are done.
	 */
	mxc_epdc_fb_flush_updates(fb_data);

	if ((upd_scheme == UPDATE_SCHEME_SNAPSHOT)
		|| (upd_scheme == UPDATE_SCHEME_QUEUE)
		|| (upd_scheme == UPDATE_SCHEME_QUEUE_AND_MERGE))
		fb_data->upd_scheme = upd_scheme;
	else {
		dev_err(fb_data->dev, "Invalid update scheme specified.\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mxc_epdc_fb_set_upd_scheme);

static int epdc_process_update(struct update_data_list *upd_data_list,
                               struct mxc_epdc_fb_data *fb_data)
{
	struct mxcfb_rect *src_upd_region; /* Region of src buffer for update */
	struct mxcfb_rect pxp_upd_region;
	u32 src_width, src_height;
	u32 bytes_per_pixel;
	u32 post_rotation_xcoord, post_rotation_ycoord, width_pxp_blocks;
	u32 pxp_input_offs, pxp_output_offs, pxp_output_shift;
	u32 hist_stat = 0;
	struct update_desc_list *upd_desc_list = upd_data_list->update_desc;
	struct mxcfb_update_data *upd_data = &upd_desc_list->upd_data;

	int ret;

	/*
	 * Gotta do a whole bunch of buffer ptr manipulation to
	 * work around HW restrictions for PxP & EPDC
	 * Note: Applies to pre-2.0 versions of EPDC/PxP
	 */

	/*
	 * Are we using FB or an alternate (overlay)
	 * buffer for source of update?
	 */
	if (upd_data->flags & EPDC_FLAG_USE_ALT_BUFFER) {
		src_width = upd_data->alt_buffer_data.width;
		src_height = upd_data->alt_buffer_data.height;
		src_upd_region = &upd_data->alt_buffer_data.alt_update_region;
	} else {
		src_width = fb_data->epdc_fb_var.xres_virtual;
		src_height = fb_data->epdc_fb_var.yres;
		src_upd_region = &upd_data->update_region;
	}

	mutex_lock(&fb_data->pxp_mutex);

	bytes_per_pixel = fb_data->epdc_fb_var.bits_per_pixel / 8;
	pxp_input_offs = (src_upd_region->top * src_width + src_upd_region->left) * bytes_per_pixel;

	pxp_upd_region.left = 0;
	pxp_upd_region.top = 0;

	/*
	 * For version 2.0 and later of EPDC & PxP, if no rotation, we don't
	 * need to align width & height (rotation always requires 8-pixel
	 * width & height alignment, per PxP limitations)
	 */
	if ((fb_data->epdc_fb_var.rotate == FB_ROTATE_UR) && (fb_data->rev >= EPDC_VERSION_2_0_0)) {
		pxp_upd_region.width = src_upd_region->width;
		pxp_upd_region.height = src_upd_region->height;
	} else {
		/* Update region dimensions to meet 8x8 pixel requirement */
		pxp_upd_region.width = ALIGN(src_upd_region->width + pxp_upd_region.left, 8);
		pxp_upd_region.height = ALIGN(src_upd_region->height, 8);
	}

	switch (fb_data->epdc_fb_var.rotate) {
	case FB_ROTATE_UR:
	default:
		post_rotation_xcoord = pxp_upd_region.left;
		post_rotation_ycoord = pxp_upd_region.top;
		width_pxp_blocks = pxp_upd_region.width;
		break;
	case FB_ROTATE_CW:
		width_pxp_blocks = pxp_upd_region.height;
		post_rotation_xcoord = width_pxp_blocks - src_upd_region->height;
		post_rotation_ycoord = pxp_upd_region.left;
		break;
	case FB_ROTATE_UD:
		width_pxp_blocks = pxp_upd_region.width;
		post_rotation_xcoord = width_pxp_blocks - src_upd_region->width - pxp_upd_region.left;
		post_rotation_ycoord = pxp_upd_region.height - src_upd_region->height - pxp_upd_region.top;
		break;
	case FB_ROTATE_CCW:
		width_pxp_blocks = pxp_upd_region.height;
		post_rotation_xcoord = pxp_upd_region.top;
		post_rotation_ycoord = pxp_upd_region.width - src_upd_region->width - pxp_upd_region.left;
		break;
	}

	pxp_output_shift = 0;
	pxp_output_offs = post_rotation_ycoord * width_pxp_blocks + post_rotation_xcoord;
	upd_desc_list->epdc_offs = pxp_output_offs;

	upd_desc_list->epdc_stride = width_pxp_blocks;

	if (upd_data->flags & EPDC_FLAG_USE_ALT_BUFFER) {
		sg_dma_address(&fb_data->sg[0]) =
			upd_data->alt_buffer_data.phys_addr + pxp_input_offs;
	} else {
		sg_dma_address(&fb_data->sg[0]) =
			fb_data->info.fix.smem_start + fb_data->fb_offset + pxp_input_offs;
		sg_set_page(&fb_data->sg[0],
		            virt_to_page(fb_data->info.screen_base),
		            fb_data->info.fix.smem_len,
		            offset_in_page(fb_data->info.screen_base));
	}

	/* Update sg[1] to point to output of PxP proc task */
	sg_dma_address(&fb_data->sg[1]) = upd_data_list->phys_addr + pxp_output_shift;
	sg_set_page(&fb_data->sg[1],
	            virt_to_page(upd_data_list->virt_addr),
	            fb_data->max_pix_size,
	            offset_in_page(upd_data_list->virt_addr));

	/*
	 * Set PxP LUT transform type based on update flags.
	 */
	fb_data->pxp_conf.proc_data.lut_transform = 0;
	if (upd_data->flags & EPDC_FLAG_ENABLE_INVERSION)
		fb_data->pxp_conf.proc_data.lut_transform |= PXP_LUT_INVERT;

	if (upd_data->flags & EPDC_FLAG_FORCE_MONOCHROME)
		fb_data->pxp_conf.proc_data.lut_transform |= PXP_LUT_BLACK_WHITE;

	if ((upd_data->flags & EPDC_FLAG_USE_CMAP) || use_cmap)
		fb_data->pxp_conf.proc_data.lut_transform |= PXP_LUT_USE_CMAP;

	if ((upd_data->flags & EPDC_FLAG_FORCE_Y2))
		fb_data->pxp_conf.proc_data.lut_transform |= PXP_LUT_Y2;

	/*
	 * Toggle inversion processing if 8-bit
	 * inverted is the current pixel format.
	 */
	if (fb_data->epdc_fb_var.grayscale == GRAYSCALE_8BIT_INVERTED)
		fb_data->pxp_conf.proc_data.lut_transform ^= PXP_LUT_INVERT;

	/* Enable PxP LUT option to configure pixel data for REAGL processing */
	if (fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N)
	{
		if ((upd_desc_list->upd_data.flags & EPDC_FLAG_USE_DITHERING_Y1) ||
				(upd_desc_list->upd_data.flags & EPDC_FLAG_USE_DITHERING_Y2) ||
				(upd_desc_list->upd_data.flags & EPDC_FLAG_USE_DITHERING_Y4))
			fb_data->pxp_conf.proc_data.lut_transform &= ~PXP_LUT_REAGL;
		else
			fb_data->pxp_conf.proc_data.lut_transform |= PXP_LUT_REAGL;
	}

	/* This is a blocking call, so upon return PxP tx should be done */
	ret = pxp_process_update(fb_data, src_width, src_height, &pxp_upd_region);
	if (ret) {
		dev_err(fb_data->dev, "Unable to submit PxP update task.\n");
		mutex_unlock(&fb_data->pxp_mutex);
		return ret;
	}

	/* This is a blocking call, so upon return PxP tx should be done */
	ret = pxp_complete_update(fb_data, &hist_stat);
	if (ret) {
		dev_err(fb_data->dev, "Unable to complete PxP update task.\n");
		mutex_unlock(&fb_data->pxp_mutex);
		return ret;
	}

	mutex_unlock(&fb_data->pxp_mutex);

	/* Update waveform mode from PxP histogram results */
	if ((fb_data->rev <= EPDC_VERSION_2_0_0) && (upd_data->waveform_mode == WAVEFORM_MODE_AUTO)) {
		if (hist_stat & 0x1)
			upd_data->waveform_mode = upd_data->hist_bw_waveform_mode;
		else
			upd_data->waveform_mode = upd_data->hist_gray_waveform_mode;

		dev_dbg(fb_data->dev, "hist_stat = 0x%x, new waveform = 0x%x\n",
		        hist_stat, upd_data->waveform_mode);

		if (mxc_epdc_debugging)
			printk(KERN_INFO "mxc_epdc_fb: [%d] Waveform used is 0x%x (%s)\n",
					upd_data->update_marker, upd_data->waveform_mode, wfm_name_for_mode(fb_data, upd_data->waveform_mode));
	}

	return 0;
}

static void merge_hist_waveforms(struct mxcfb_update_data *a, struct mxcfb_update_data *b)
{
	// Do the Black and White waveforms first
	__u32 *awf = &a->hist_bw_waveform_mode;
	__u32 *bwf = &b->hist_bw_waveform_mode;

	if (*awf != *bwf) {
		if (!(*awf) || !(*bwf)) {
			// If one is not set give priority to the one set
			*awf = (!(*awf)) ? (*bwf) : (*awf);
		} else {
			// If there are not the same, one is DU.
			*awf = g_fb_data->wv_modes.mode_du;
		}
	}

	// Now do the gray waveforms
	awf = &a->hist_gray_waveform_mode;
	bwf = &b->hist_gray_waveform_mode;

	if (*awf != *bwf) {
		if (!(*awf) || !(*bwf)) {
			// If one is not set give priority to the one set
			*awf = (!*(awf)) ? (*bwf) : (*awf);
		} else {
			if (*awf == g_fb_data->wv_modes.mode_gc16 ||
			    *bwf == g_fb_data->wv_modes.mode_gc16) {
				// Priority to GC16
				*awf = g_fb_data->wv_modes.mode_gc16;
			} else if (g_fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N &&
					(*awf == g_fb_data->wv_modes.mode_reagld ||
					 *bwf == g_fb_data->wv_modes.mode_reagld)) {
				// Next priority to REAGLD
				*awf = g_fb_data->wv_modes.mode_reagld;
			} else if (g_fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N &&
					(*awf == g_fb_data->wv_modes.mode_reagl ||
					 *bwf == g_fb_data->wv_modes.mode_reagl)) {
				// Next priority to REAGL
				*awf = g_fb_data->wv_modes.mode_reagl;
			} else if (*awf == g_fb_data->wv_modes.mode_gl16 ||
			           *bwf == g_fb_data->wv_modes.mode_gl16) {
				// Next priority to GL16
				*awf = g_fb_data->wv_modes.mode_gl16;
			} else {
				// All other gray wvs are equal. Just use the same one
			}
		}
	}
}

static int epdc_submit_merge(struct update_desc_list *upd_desc_list,
				struct update_desc_list *update_to_merge,
				struct mxc_epdc_fb_data *fb_data)
{
	struct update_marker_data *next_marker;
	struct update_marker_data *temp;
	struct mxcfb_update_data *a, *b;
	struct mxcfb_rect *arect, *brect;
	struct mxcfb_rect combine;
	bool use_flags = false;
	int lut_mask = 0;

	a = &upd_desc_list->upd_data;
	b = &update_to_merge->upd_data;
	arect = &upd_desc_list->upd_data.update_region;
	brect = &update_to_merge->upd_data.update_region;

	/* Do not merge a dry-run collision test update */
	if ((a->flags & EPDC_FLAG_TEST_COLLISION) ||
		(b->flags & EPDC_FLAG_TEST_COLLISION))
		return MERGE_BLOCK;

	/*
	 * Updates with different flags must be executed sequentially.
	 * Halt the merge process to ensure this.
	 */
	if (a->flags != b->flags) {
		/*
		 * Special exception: if update regions are identical,
		 * we may be able to merge them.
		 */
		if ((arect->left != brect->left) ||
			(arect->top != brect->top) ||
			(arect->width != brect->width) ||
			(arect->height != brect->height))
			return MERGE_BLOCK;

		use_flags = true;
	}

	if (a->update_mode != b->update_mode)
		a->update_mode = UPDATE_MODE_FULL;

	/*if (a->waveform_mode != b->waveform_mode)
		a->waveform_mode = WAVEFORM_MODE_AUTO;*/

	if (arect->left > (brect->left + brect->width) ||
		brect->left > (arect->left + arect->width) ||
		arect->top > (brect->top + brect->height) ||
		brect->top > (arect->top + arect->height))
		return MERGE_FAIL;

	combine.left = arect->left < brect->left ? arect->left : brect->left;
	combine.top = arect->top < brect->top ? arect->top : brect->top;
	combine.width = (arect->left + arect->width) >
		(brect->left + brect->width) ?
		(arect->left + arect->width - combine.left) :
			(brect->left + brect->width - combine.left);
	combine.height = (arect->top + arect->height) >
		(brect->top + brect->height) ?
		(arect->top + arect->height - combine.top) :
			(brect->top + brect->height - combine.top);

	/* Don't merge if combined width exceeds max width */
	if (fb_data->restrict_width) {
		u32 max_width = EPDC_V2_MAX_UPDATE_WIDTH;
		u32 combined_width = combine.width;
		if (fb_data->epdc_fb_var.rotate != FB_ROTATE_UR)
			max_width -= EPDC_V2_ROTATION_ALIGNMENT;
		if ((fb_data->epdc_fb_var.rotate == FB_ROTATE_CW) ||
				(fb_data->epdc_fb_var.rotate == FB_ROTATE_CCW))
			combined_width = combine.height;
		if (combined_width > max_width)
			return MERGE_FAIL;
	}

	if (mxc_epdc_debugging)
		printk(KERN_INFO "mxc_epdc_fb: Going to merge update %d and %d. Waveforms: %d and %d\n",
		       a->update_marker, b->update_marker, a->waveform_mode, b->waveform_mode);

	/* Lab126: Merge code giving priority to highest fidelity waveforms */
	if (a->waveform_mode == b->waveform_mode) {
		// Merge prefered histogram waveforms in case of AUTO
	} else if (a->waveform_mode == g_fb_data->wv_modes.mode_gc16 ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_gc16) {
		// First priority to GC16 updates
		a->waveform_mode = g_fb_data->wv_modes.mode_gc16;
	} else if (a->waveform_mode == g_fb_data->wv_modes.mode_gc16_fast ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_gc16_fast) {
		// Next priority GC16 fast
		a->waveform_mode = g_fb_data->wv_modes.mode_gc16_fast;
	} else if (g_fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N &&
	           (a->waveform_mode == g_fb_data->wv_modes.mode_reagld ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_reagld)) {
		// Next priority to REAGLD
		a->waveform_mode = g_fb_data->wv_modes.mode_reagld;
	} else if (g_fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N &&
	           (a->waveform_mode == g_fb_data->wv_modes.mode_reagl ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_reagl)) {
		// Next priority to REAGL
		a->waveform_mode = g_fb_data->wv_modes.mode_reagl;
	} else if (a->waveform_mode == g_fb_data->wv_modes.mode_gl16 ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_gl16) {
		// Next priority to GL16 updates
		a->waveform_mode = g_fb_data->wv_modes.mode_gl16;
	} else if (a->waveform_mode == g_fb_data->wv_modes.mode_gc16_fast ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_gc16_fast) {
		// Next priority GC16 fast
		a->waveform_mode = g_fb_data->wv_modes.mode_gc16_fast;
	} else if (a->waveform_mode == g_fb_data->wv_modes.mode_gl16_fast ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_gl16_fast) {
		// Next priority GL16 fast
		a->waveform_mode = g_fb_data->wv_modes.mode_gl16_fast;
	} else if (a->waveform_mode == g_fb_data->wv_modes.mode_du4 ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_du4) {
		// Next priority DU4
		a->waveform_mode = g_fb_data->wv_modes.mode_du4;
	} else if (a->waveform_mode == g_fb_data->wv_modes.mode_du ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_du) {
		// Next priority DU
		a->waveform_mode = g_fb_data->wv_modes.mode_du;
	} else if (a->waveform_mode == g_fb_data->wv_modes.mode_a2 ||
	           b->waveform_mode == g_fb_data->wv_modes.mode_a2) {
		// Next priority A2
		a->waveform_mode = g_fb_data->wv_modes.mode_a2;
	} else if (a->waveform_mode == WAVEFORM_MODE_AUTO) {
		a->waveform_mode = WAVEFORM_MODE_AUTO;
	} else {
		a->waveform_mode = b->waveform_mode;
	}

	/* If we have a REAGL or REAGLD waveform, set the flags */
	if (g_fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N &&
			(a->waveform_mode == g_fb_data->wv_modes.mode_reagl ||
			 a->waveform_mode == g_fb_data->wv_modes.mode_reagld)) {
		upd_desc_list->is_reagl = true;
		upd_desc_list->wb_pause = true;
	}

	if (mxc_epdc_debugging)
		printk(KERN_INFO "mxc_epdc_fb: Merging update %d and %d. New waveform: 0x%x (%s)\n",
		       a->update_marker, b->update_marker, a->waveform_mode, wfm_name_for_mode(fb_data, a->waveform_mode));

	// Lab126: We want to merge prefered hist waveforms in case of collision
	merge_hist_waveforms(a, b);

	if (a->update_mode == UPDATE_MODE_FULL || b->update_mode == UPDATE_MODE_FULL) {
		// If any of the two updates are FULL, make it a FULL update
		a->update_mode = UPDATE_MODE_FULL;
	}

	*arect = combine;

	/* Use flags of the later update */
	if (use_flags)
		a->flags = b->flags;

	/* Merge markers */
	list_splice_tail(&update_to_merge->upd_marker_list,
		&upd_desc_list->upd_marker_list);

	/* Merge active LUTS for these markers */
	list_for_each_entry_safe(next_marker, temp,
			&upd_desc_list->upd_marker_list, upd_list)
		lut_mask |= next_marker->lut_mask;

	list_for_each_entry_safe(next_marker, temp,
			&upd_desc_list->upd_marker_list, upd_list)
		next_marker->lut_mask = lut_mask;

	/* Merged update should take on the earliest order */
	upd_desc_list->update_order =
		(upd_desc_list->update_order > update_to_merge->update_order) ?
		upd_desc_list->update_order : update_to_merge->update_order;

	return MERGE_OK;
}

static void epdc_submit_work_func(struct work_struct *work)
{
	int temp_index;
	struct update_data_list *next_update, *temp_update;
	struct update_desc_list *next_desc, *temp_desc;
	struct update_marker_data *next_marker, *temp_marker;
	struct mxc_epdc_fb_data *fb_data =
		container_of(work, struct mxc_epdc_fb_data, epdc_submit_work);
	struct update_data_list *upd_data_list = NULL;
	struct mxcfb_rect adj_update_region, *upd_region;
	bool end_merge = false;
	bool is_transform;
	u32 update_addr;
	int ret;

	/* Protect access to buffer queues and to update HW */
	mutex_lock(&fb_data->queue_mutex);

	/*
	 * Are any of our collision updates able to go now?
	 * Go through all updates in the collision list and check to see
	 * if the collision mask has been fully cleared
	 */
	list_for_each_entry_safe(next_update, temp_update,
				&fb_data->upd_buf_collision_list, list) {

		if (next_update->collision_mask != 0)
			continue;

		dev_dbg(fb_data->dev, "A collision update is ready to go!\n");

		if (!(g_fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N &&
					(next_update->update_desc->upd_data.waveform_mode == g_fb_data->wv_modes.mode_reagl ||
					next_update->update_desc->upd_data.waveform_mode == g_fb_data->wv_modes.mode_reagld))) {
			/* Force waveform mode to auto for resubmitted collisions */
			next_update->update_desc->upd_data.waveform_mode = WAVEFORM_MODE_AUTO;
			next_update->update_desc->is_reagl = false;
		} else {
			next_update->update_desc->is_reagl = true;
		}


		/*
		 * We have a collision cleared, so select it for resubmission.
		 * If an update is already selected, attempt to merge.
		 */
		if (!upd_data_list) {
			upd_data_list = next_update;
			list_del_init(&next_update->list);
			if (fb_data->upd_scheme == UPDATE_SCHEME_QUEUE)
				/* If not merging, we have our update */
				break;
		} else {
			switch (epdc_submit_merge(upd_data_list->update_desc,
						next_update->update_desc,
						fb_data)) {
			case MERGE_OK:
				dev_dbg(fb_data->dev,
					"Update merged [collision]\n");
				list_del_init(&next_update->update_desc->list);
				kfree(next_update->update_desc);
				next_update->update_desc = NULL;
				list_del_init(&next_update->list);
				/* Add to free buffer list */
				list_add_tail(&next_update->list,
					 &fb_data->upd_buf_free_list);
				break;
			case MERGE_FAIL:
				dev_dbg(fb_data->dev,
					"Update not merged [collision]\n");
				break;
			case MERGE_BLOCK:
				dev_dbg(fb_data->dev,
					"Merge blocked [collision]\n");
				end_merge = true;
				break;
			}

			if (end_merge) {
				end_merge = false;
				break;
			}
		}
	}

	/*
	 * Skip pending update list only if we found a collision
	 * update and we are not merging
	 */
	if (!((fb_data->upd_scheme == UPDATE_SCHEME_QUEUE) &&
		upd_data_list)) {
		/*
		 * If we didn't find a collision update ready to go, we
		 * need to get a free buffer and match it to a pending update.
		 */

		/*
		 * Can't proceed if there are no free buffers (and we don't
		 * already have a collision update selected)
		*/
		if (!upd_data_list &&
			list_empty(&fb_data->upd_buf_free_list)) {
			dev_dbg(fb_data->dev, "No free buffers!\n");
			mutex_unlock(&fb_data->queue_mutex);
			return;
		}

		list_for_each_entry_safe(next_desc, temp_desc,
				&fb_data->upd_pending_list, list) {

			dev_dbg(fb_data->dev, "Found a pending update!\n");

			if (!upd_data_list) {
				if (list_empty(&fb_data->upd_buf_free_list))
					break;
				upd_data_list =
					list_entry(fb_data->upd_buf_free_list.next,
						struct update_data_list, list);
				list_del_init(&upd_data_list->list);
				upd_data_list->update_desc = next_desc;
				list_del_init(&next_desc->list);
				if (fb_data->upd_scheme == UPDATE_SCHEME_QUEUE)
					/* If not merging, we have an update */
					break;
			} else {
				switch (epdc_submit_merge(upd_data_list->update_desc,
						next_desc, fb_data)) {
				case MERGE_OK:
					dev_dbg(fb_data->dev,
						"Update merged [queue]\n");
					list_del_init(&next_desc->list);
					kfree(next_desc);
					break;
				case MERGE_FAIL:
					dev_dbg(fb_data->dev,
						"Update not merged [queue]\n");
					break;
				case MERGE_BLOCK:
					dev_dbg(fb_data->dev,
						"Merge blocked [queue]\n");
					end_merge = true;
					break;
				}

				if (end_merge)
					break;
			}
		}
	}

	/* Is update list empty? */
	if (!upd_data_list) {
		mutex_unlock(&fb_data->queue_mutex);
		return;
	}

	/*
	 * If there is no REAGL support, fall back to a GC16.
	 */
#ifndef CONFIG_FB_MXC_EINK_REAGL
	{
		struct update_desc_list *update = upd_data_list->update_desc;
		if (update->upd_data.waveform_mode == fb_data->wv_modes.mode_reagl ||
				update->upd_data.waveform_mode == fb_data->wv_modes.mode_reagld) {
			update->upd_data.waveform_mode = fb_data->wv_modes.mode_gc16;
		}
	}
#endif // CONFIG_FB_MXC_EINK_REAGL

	/*
	 * AUTO with a prefered waveform of REAGL or REAGLD is not supported. In that
	 * case, force the waveform
	 */
	if (fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N) {
		struct update_desc_list *update = upd_data_list->update_desc;
		if (update->upd_data.waveform_mode == WAVEFORM_MODE_AUTO) {
			// Submitting an AUTO waveform. Check that REAGL or REAGLD is not prefered.
			if (update->upd_data.hist_gray_waveform_mode == fb_data->wv_modes.mode_reagl ||
					update->upd_data.hist_gray_waveform_mode == fb_data->wv_modes.mode_reagld) {
				update->upd_data.waveform_mode = update->upd_data.hist_gray_waveform_mode;
			} else if (update->upd_data.hist_bw_waveform_mode == fb_data->wv_modes.mode_reagl ||
					update->upd_data.hist_bw_waveform_mode == fb_data->wv_modes.mode_reagld) {
				update->upd_data.waveform_mode = update->upd_data.hist_bw_waveform_mode;
			}
		}

		if (update->upd_data.waveform_mode == fb_data->wv_modes.mode_reagl ||
				update->upd_data.waveform_mode == fb_data->wv_modes.mode_reagld) {
			// Reset the flags on REAGL and REAGLD
			update->is_reagl = true;
		}
	}

	/* Pause is mandatory in this scheme. The UPD_DONE interrupt work will
	 * complete the power on sequence by enabling VCOM if this is a valid
	 * update.
	 */
	upd_data_list->update_desc->wb_pause = true;

	/*
	 * If no processing required, skip update processing
	 * No processing means:
	 *   - FB unrotated
	 *   - FB pixel format = 8-bit grayscale
	 *   - No look-up transformations (inversion, posterization, etc.)
	 *   - No REAGL processing, which requires a look-up transformation
	 *     to convert from 4 bit to 5 bit lookup mode
	 *
	 * Note: A bug with EPDC stride prevents us from skipping
	 * PxP in versions 2.0 and earlier of EPDC.
	 */
	is_transform = (upd_data_list->update_desc->upd_data.flags &
		(EPDC_FLAG_ENABLE_INVERSION | EPDC_FLAG_USE_DITHERING_Y1 |
		EPDC_FLAG_USE_DITHERING_Y2 |
		EPDC_FLAG_USE_DITHERING_Y4 | EPDC_FLAG_FORCE_MONOCHROME |
		EPDC_FLAG_USE_CMAP | EPDC_FLAG_FORCE_Y2)) || use_cmap || (fb_data->buf_pix_fmt ==
		EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N)  ? true : false;

	/* To fix race condition when powerdown is scheduled with pending interrupts */
	cancel_delayed_work_sync(&fb_data->epdc_done_work);

	mutex_lock(&fb_data->power_mutex);
	/* If needed, enable EPDC HW while ePxP is processing */
	if (!epdc_powerup(fb_data)) {
		dev_err(fb_data->dev, "EPDC powerup error.\n");
		/* Protect access to buffer queues and to update HW */
		list_del_init(&upd_data_list->update_desc->list);
		kfree(upd_data_list->update_desc);
		upd_data_list->update_desc = NULL;
		/* Add to free buffer list */
		list_add_tail(&upd_data_list->list,
				&fb_data->upd_buf_free_list);
		/* Release buffer queues */
		mutex_unlock(&fb_data->queue_mutex);
		mutex_unlock(&fb_data->power_mutex);
		return;
	}
	mutex_unlock(&fb_data->power_mutex);

	if ((fb_data->epdc_fb_var.rotate == FB_ROTATE_UR) &&
		(fb_data->epdc_fb_var.grayscale == GRAYSCALE_8BIT) &&
		!is_transform && (fb_data->rev > EPDC_VERSION_2_0_0) &&
		!fb_data->restrict_width) {

		/*
		 * Set update buffer pointer to the start of
		 * the update region in the frame buffer.
		 */
		upd_region = &upd_data_list->update_desc->upd_data.update_region;
		update_addr = fb_data->info.fix.smem_start +
			((upd_region->top * fb_data->info.var.xres_virtual) +
			upd_region->left) * fb_data->info.var.bits_per_pixel/8;
		upd_data_list->update_desc->epdc_stride =
					fb_data->info.var.xres_virtual *
					fb_data->info.var.bits_per_pixel/8;
	} else {
		/* Select from PxP output buffers */
		upd_data_list->phys_addr =
			fb_data->phys_addr_updbuf[fb_data->upd_buffer_num];
		upd_data_list->virt_addr =
			fb_data->virt_addr_updbuf[fb_data->upd_buffer_num];
		fb_data->upd_buffer_num++;
		if (fb_data->upd_buffer_num > fb_data->max_num_buffers-1)
			fb_data->upd_buffer_num = 0;

		/* Release buffer queues */
		mutex_unlock(&fb_data->queue_mutex);

		/* Perform PXP processing */
		if (epdc_process_update(upd_data_list, fb_data)) {
			dev_err(fb_data->dev, "PXP processing error.\n");
			/* Protect access to buffer queues and to update HW */
			mutex_lock(&fb_data->queue_mutex);
			list_del_init(&upd_data_list->update_desc->list);
			kfree(upd_data_list->update_desc);
			upd_data_list->update_desc = NULL;
			/* Add to free buffer list */
			list_add_tail(&upd_data_list->list,
				&fb_data->upd_buf_free_list);
			/* Release buffer queues */
			mutex_unlock(&fb_data->queue_mutex);
			return;
		}

		/* Protect access to buffer queues and to update HW */
		mutex_lock(&fb_data->queue_mutex);

		update_addr = upd_data_list->phys_addr
				+ upd_data_list->update_desc->epdc_offs;
	}

	/* Get rotation-adjusted coordinates */
	adjust_coordinates(fb_data->epdc_fb_var.xres,
		fb_data->epdc_fb_var.yres, fb_data->epdc_fb_var.rotate,
		&upd_data_list->update_desc->upd_data.update_region,
		&adj_update_region);


	/*
	 * Dithering Processing (Version 1.0 - for i.MX508 and i.MX6SL)
	 */
	if (upd_data_list->update_desc->upd_data.flags & EPDC_FLAG_USE_DITHERING_Y1) {
		/* Dithering Y8 -> Y1 */
		do_dithering_processing_Y1_v1_0(
				(uint8_t *)(upd_data_list->virt_addr + upd_data_list->update_desc->epdc_offs),
				&adj_update_region,
				(fb_data->rev < EPDC_VERSION_2_0_0) ? ALIGN(adj_update_region.width, 8) : adj_update_region.width,
				fb_data->dither_err_dist, (fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N) ? 1 : 0);

	} else if (upd_data_list->update_desc->upd_data.flags & EPDC_FLAG_USE_DITHERING_Y2) {
		/* Dithering Y8 -> Y2 */
		do_dithering_processing_Y2_v1_0(
				(uint8_t *)(upd_data_list->virt_addr + upd_data_list->update_desc->epdc_offs),
				&adj_update_region,
				(fb_data->rev < EPDC_VERSION_2_0_0) ? ALIGN(adj_update_region.width, 8) : adj_update_region.width,
				fb_data->dither_err_dist, (fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N) ? 1 : 0);
	}else if (upd_data_list->update_desc->upd_data.flags & EPDC_FLAG_USE_DITHERING_Y4) {
		/* Dithering Y8 -> Y4 */
		do_dithering_processing_Y4_v1_0(
				(uint8_t *)(upd_data_list->virt_addr + upd_data_list->update_desc->epdc_offs),
				&adj_update_region,
				(fb_data->rev < EPDC_VERSION_2_0_0) ? ALIGN(adj_update_region.width, 8) : adj_update_region.width,
				fb_data->dither_err_dist, (fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N) ? 1 : 0);
	}

	/*
	 * Is the working buffer idle?
	 * If the working buffer is busy, we must wait for the resource
	 * to become free. The IST will signal this event.
	 */
	if (fb_data->cur_update != NULL) {
		dev_dbg(fb_data->dev, "working buf busy!\n");
		/* Initialize event signalling an update resource is free */
		init_completion(&fb_data->wb_free);

		fb_data->waiting_for_wb = true;

		/* Leave spinlock while waiting for WB to complete */
		mutex_unlock(&fb_data->queue_mutex);
		wait_for_completion(&fb_data->wb_free);
		mutex_lock(&fb_data->queue_mutex);
	}

	/*
	 * If there are no LUTs available,
	 * then we must wait for the resource to become free.
	 * The IST will signal this event.
	 *
	 * If LUT15 is in use (for pre-EPDC v2.0 hardware):
	 *   - Wait for LUT15 to complete is if TCE underrun prevent is enabled
	 *   - If we go ahead with update, sync update submission with EOF
	 */
	ret = epdc_choose_next_lut(fb_data->rev, &upd_data_list->lut_num);
	if (((fb_data->rev >= EPDC_VERSION_2_0_0 && fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N && upd_data_list->lut_num > 15) ||
				(ret && fb_data->tce_prevent && fb_data->rev < EPDC_VERSION_2_0_0))) {

		dev_dbg(fb_data->dev, "no luts available!\n");

		/* Initialize event signalling an update resource is free */
		init_completion(&fb_data->update_res_free);

		fb_data->waiting_for_lut = true;

		/* Leave spinlock while waiting for LUT to free up */
		mutex_unlock(&fb_data->queue_mutex);
		wait_for_completion(&fb_data->update_res_free);
		mutex_lock(&fb_data->queue_mutex);
		ret = epdc_choose_next_lut(fb_data->rev, &upd_data_list->lut_num);
	} else if (ret && (fb_data->rev < EPDC_VERSION_2_0_0)) {
		/* Synchronize update submission time to reduce
		   chances of TCE underrun */
		init_completion(&fb_data->eof_event);

		epdc_eof_intr(true);

		/* Leave spinlock while waiting for EOF event */
		mutex_unlock(&fb_data->queue_mutex);
		ret = wait_for_completion_timeout(&fb_data->eof_event,
			msecs_to_jiffies(1000));
		if (!ret) {
			dev_err(fb_data->dev, "Missed EOF event!\n");
			epdc_eof_intr(false);
		}
		udelay(fb_data->eof_sync_period);
		mutex_lock(&fb_data->queue_mutex);

	}

	/* LUTs are available, so we get one here */
	fb_data->cur_update = upd_data_list;

	/* Reset mask for LUTS that have completed during WB processing */
	fb_data->luts_complete_wb = 0;

	/* If we are just testing for collision, we don't assign a LUT,
	 * so we don't need to update LUT-related resources. */
	if (!(upd_data_list->update_desc->upd_data.flags
		& EPDC_FLAG_TEST_COLLISION)) {
		/* Associate LUT with update marker */
		list_for_each_entry_safe(next_marker, temp_marker,
			&upd_data_list->update_desc->upd_marker_list, upd_list)
		{
			next_marker->lut_num = fb_data->cur_update->lut_num;
			/* Set this LUT as active for each marker */
			next_marker->lut_mask |= LUT_MASK(fb_data->cur_update->lut_num);
			dev_dbg(fb_data->dev, "Submit. Marker: %d __ LUT: %d __ LUT_MASK: 0x%llx\n",
					next_marker->update_marker,
					next_marker->lut_num,
					next_marker->lut_mask);
		}

		/* Mark LUT with order */
		fb_data->lut_update_order[upd_data_list->lut_num] =
			upd_data_list->update_desc->update_order;

		epdc_lut_complete_intr(fb_data->rev, upd_data_list->lut_num,
					true);
	}

	/*
	 * If this update requires a pause after WB processing,
	 * we will get UPD_DONE interrupt but not WB_CMPLT.
	 * For all other updates, we need an interrupt for WB_CMPLT.
	 */
	if (upd_data_list->update_desc->wb_pause) {
		epdc_working_buf_intr(false);
		epdc_clear_working_buf_irq();
		epdc_upd_done_intr(true);
	} else {
		epdc_upd_done_intr(false);
		epdc_working_buf_intr(true);
	}

	/* Program EPDC update to process buffer */
	if (fb_data->temp_override != TEMP_USE_AUTO)
		upd_data_list->update_desc->upd_data.temp = fb_data->temp_override;

	if (upd_data_list->update_desc->upd_data.temp == TEMP_USE_AUTO) {
		temp_index = mxc_epdc_fb_get_temp_index(fb_data, display_temp_c);
		epdc_set_temp(temp_index);
	} else if (upd_data_list->update_desc->upd_data.temp != TEMP_USE_AMBIENT) {
		temp_index = mxc_epdc_fb_get_temp_index(fb_data,
			upd_data_list->update_desc->upd_data.temp);
		epdc_set_temp(temp_index);
	} else {
		epdc_set_temp(fb_data->temp_index);
	}

	epdc_set_update_addr(update_addr);
	epdc_set_update_coord(adj_update_region.left, adj_update_region.top);
	epdc_set_update_dimensions(adj_update_region.width,
				   adj_update_region.height);
	if (fb_data->rev > EPDC_VERSION_2_0_0)
		epdc_set_update_stride(upd_data_list->update_desc->epdc_stride);
	if (fb_data->wv_modes_update &&
		(upd_data_list->update_desc->upd_data.waveform_mode
			== WAVEFORM_MODE_AUTO)) {
		epdc_set_update_waveform(&fb_data->wv_modes);
		fb_data->wv_modes_update = false;
	}

	if (upd_data_list->update_desc->wb_pause && upd_data_list->update_desc->is_reagl) {
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
		if (upd_data_list->update_desc->upd_data.waveform_mode == fb_data->wv_modes.mode_reagld)
			__raw_writel(fb_data->working_buffer_B_phys, EPDC_WB_ADDR_TCE);
		else
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B
#ifdef CONFIG_FB_MXC_EINK_REAGL
			__raw_writel(reagl_algos[fb_data->which_reagl].buffer_tce, EPDC_WB_ADDR_TCE);
#else
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
			__raw_writel(fb_data->working_buffer_B_phys, EPDC_WB_ADDR_TCE);
#endif
#endif // CONFIG_FB_MXC_EINK_REAGL
	} else {
		__raw_writel(fb_data->working_buffer_A_phys, EPDC_WB_ADDR_TCE);
	}


	if (mxc_epdc_debugging) {
		int temp;
		printk(KERN_INFO "mxc_epdc_fb: [%d] Sending update. waveform:0x%x (%s) mode:0x%x update region top=%d, left=%d, width=%d, height=%d\n",
				fb_data->cur_update->update_desc->upd_data.update_marker,
				upd_data_list->update_desc->upd_data.waveform_mode,
				wfm_name_for_mode(fb_data, upd_data_list->update_desc->upd_data.waveform_mode),
				upd_data_list->update_desc->upd_data.update_mode,
				upd_data_list->update_desc->upd_data.update_region.top,
				upd_data_list->update_desc->upd_data.update_region.left,
				upd_data_list->update_desc->upd_data.update_region.width,
				upd_data_list->update_desc->upd_data.update_region.height);

		if (upd_data_list->update_desc->upd_data.temp == TEMP_USE_AUTO)
			temp = display_temp_c;
		else if (upd_data_list->update_desc->upd_data.temp != TEMP_USE_AMBIENT)
			temp = upd_data_list->update_desc->upd_data.temp;
		else
			temp = DEFAULT_TEMP;

		printk(KERN_INFO "mxc_epdc_fb: [%d] Sending update in LUT: %d __ Temperature : %d\n",
				fb_data->cur_update->update_desc->upd_data.update_marker, fb_data->cur_update->lut_num, temp);
	}

	dev_dbg(fb_data->dev, "Sending update on LUT: %d\n", upd_data_list->lut_num);
	epdc_submit_update(upd_data_list->lut_num,
			   upd_data_list->update_desc->upd_data.waveform_mode,
			   upd_data_list->update_desc->upd_data.update_mode,
			   (upd_data_list->update_desc->upd_data.flags
				& EPDC_FLAG_TEST_COLLISION) ? true : false,
			   true, false, 0);

	/* Release buffer queues */
	mutex_unlock(&fb_data->queue_mutex);
}

static void convert_gc16_to_gl16(__u32 *waveform_mode)
{
	if (*waveform_mode == WAVEFORM_MODE_GC16_FAST)
		*waveform_mode = WAVEFORM_MODE_GL16_FAST;
	else if (*waveform_mode == WAVEFORM_MODE_GC16)
		*waveform_mode = WAVEFORM_MODE_GL16;
}

#if defined(FIX_CURRENT_RESAMPLE)

#include <linux/cpufreq.h>
#define ONE_GRAY_LEVEL 15  /* in 8-bit framebuffer unit */
#define TRANSITION_THRESHOLD 35  /* 35% */
#define STRENGTH_THREASHOLD 10   /* 10% */
#define SCAN_LINE_TOTAL 10 /* total number of line to be scanned; evenly divided in update region, scanline is one less */
#define SCAN_LINE_THRESHOLD 4 /* pass or fail threshold */
#define SCALED_FACTOR 375 /* 0.375 */
static int mxc_epdc_pixel_pre_process(struct mxcfb_update_data *upd_data, struct mxc_epdc_fb_data *fb_data)
{
	int ret;
	unsigned char *pixel_start, pixel0,pixel1;
	uint32_t top, left,width, height, stride;
	uint32_t row,col, scan_stride,norm_length;
	uint32_t transition[32], transition_total, strength,scan_line;
	uint32_t resample, count, bitmask,rescale_ratio;
	uint32_t s_avg, n_avg,avg;
	uint32_t scaled_width, scaled_height;

	cpufreq_override(1);
	/* If needed, enable EPDC HW while ePxP is processing */
	if ((fb_data->power_state == POWER_STATE_OFF)
		|| fb_data->powering_down) {
		epdc_powerup(fb_data);
	}

	if (fb_data->sw_algo.scan_lines == 0)
		return 0;

	dev_dbg(fb_data->dev,"%s: enter\n",__func__);

	top = upd_data->update_region.top;
	left = upd_data->update_region.left;
	width = upd_data->update_region.width;
	height = upd_data->update_region.height;
	stride = fb_data->info.var.xres_virtual;
	if (fb_data->info.var.rotate & 1) {
		scan_stride = width;
		norm_length = height;
	}
	else {
		scan_stride = height;
		norm_length = width;
	}
	scan_stride = ((scan_stride / fb_data->sw_algo.scan_lines) & (-2) ) + 1; /* make it a odd number to alternate even/odd line */

	if ( ( (width <<1) < fb_data->info.var.xres) && ( (height<<1) < fb_data->info.var.yres))
		return 0;

	strength = 0;
	if (fb_data->info.var.rotate & 1) { /* landscape mode */
		for (col=0,scan_line=0;col<(width-scan_stride);col+=scan_stride,scan_line++) { /* skip the first line on the screen */
			transition[scan_line] = 0;
			pixel_start = fb_data->virt_start + top*stride + left + col;
			for (row=0;row<height;row+=2)
			{
				uint32_t diff, tran;
				pixel0 = pixel_start[row*stride];
				pixel1 = pixel_start[(row+1)*stride];
				diff = abs((int32_t)pixel0 - (uint32_t)pixel1);
				strength+= diff;
				tran = diff > ONE_GRAY_LEVEL ? 1:0;
				transition[scan_line] += tran;
			}
			dev_dbg(fb_data->dev,"landscape transition[%d]=%d strength=%d\n",scan_line,transition[scan_line],strength);
		}
	}
	else {  /* portrait mode */
#if defined(CONFIG_KERNEL_MODE_NEON)
		if (width < 32*2) /* NEON works on 32 pixels in one shot, it's ok to skip lines less than 64 pixels wide */
			return 0;
		kernel_neon_begin();
		for (row=scan_stride,scan_line=0;row<height;row+=scan_stride,scan_line++) { /* skip the first line on the screen */
			transition[scan_line] = 0;
			pixel_start = fb_data->virt_start + (top+row)*stride + left;
			do_pixel_line((void*)pixel_start, &strength,&transition[scan_line], width );
			dev_dbg(fb_data->dev,"portrait transition neon [%d]=%d strength=%d\n",scan_line,transition[scan_line],strength);
		}
		kernel_neon_end();
#else
		for (row=scan_stride,scan_line=0;row<height;row+=scan_stride,scan_line++) { /* skip the first line on the screen */
			transition[scan_line] = 0;
			pixel_start = fb_data->virt_start + (top+row)*stride + left;
			for (col=0;col<width;col+=2)
			{
				uint32_t diff, tran;
				pixel0 = pixel_start[col];
				pixel1 = pixel_start[col+1];
				diff = abs((int32_t)pixel0 - (int32_t)pixel1);
				strength+= diff;
				tran = diff > ONE_GRAY_LEVEL ? 1:0;
				transition[scan_line] += tran;
			}
			dev_dbg(fb_data->dev,"portrait transition[%d]=%d strength=%d\n",scan_line,transition[scan_line],strength);
		}
#endif
	}

	transition_total = 0;
	count=0;bitmask=0;
	for (row=0;row<scan_line;row++) {
		if  ( (transition[row]*100) >=  (fb_data->sw_algo.pixel_thres*norm_length) ) {
			/* normalize to 50 in [0 15 0 15 ......] pattern */
			bitmask+= 1<<row;
			count++;
		}
		transition_total += transition[row];
	}

	resample = 0;
	if (count>=fb_data->sw_algo.count_thres)
		resample = 1;
	else if (count> (fb_data->sw_algo.count_thres/2)) {
		col = 0b11;/* check if there is any 2 sequential lines */
		for (row=0;row<fb_data->sw_algo.scan_lines -1;row++) {
			if (( (bitmask & col) >> row) == 0b11){
				resample =1;
				break;
			}
			col <<=1;
		}
	}
	else if ( (strength*100) >=fb_data->sw_algo.str_thres*norm_length*scan_line*255)
		/* normalize to 50 in [0 255 0 255 ......] pattern */
		resample = 1;

	/* let's work out a resize factor */
	if (resample) {
		/* average strenght and transition_total */
		s_avg = (500*strength) /255 /scan_line/norm_length;
		if (s_avg>100)
			s_avg = 100;

		n_avg = transition_total*100/scan_line/norm_length;;
		dev_dbg(fb_data->dev,"%s: score n_avg= %d s_avg=%d\n",__func__,n_avg,s_avg);
		avg = (s_avg + n_avg) * fb_data->sw_algo.scaled_factor; /* ??? normalized to [0 50] */
		if (avg > 50*1000)
			avg = 50*1000;
		rescale_ratio = 100 - avg/1000;
		dev_dbg(fb_data->dev,"%s: resacle_ratio = %d\n",__func__,rescale_ratio);

		if (fb_data->sw_algo.scaled_width == 0 && fb_data->sw_algo.scaled_height == 0 ) {
			if (rescale_ratio >60) {
				scaled_width = (fb_data->info.var.xres_virtual * rescale_ratio /100 ) & ((uint32_t) -2);
				scaled_height = (height *rescale_ratio /100 ) & ((uint32_t) -2);
			}
			else {
				scaled_width = fb_data->info.var.xres_virtual >> 1;
				scaled_height = height  >> 1;
			}
		}
		else {
			scaled_width = fb_data->sw_algo.scaled_width;
			scaled_height = fb_data->sw_algo.scaled_height;
		}
	}

	dev_dbg(fb_data->dev,"%s: scan_stride =%d  strength=%d transition=%d count=%d bitmask=0x%x\n",__func__,
		scan_stride,strength, transition_total, count,bitmask);
	if ( (upd_data->waveform_mode== WAVEFORM_MODE_DU) || (upd_data->waveform_mode == WAVEFORM_MODE_A2)) {
		if (rescale_ratio > 60)
		return 0; /* when in du/a2 mode, only catch the most serious offensive */;
	}
	if (resample && (scaled_width!=0) && (scaled_height!=0)) {
		u32 hist_stat;
		/* down sampling */
		mutex_lock(&fb_data->pxp_mutex);
		dev_dbg(fb_data->dev, "doing down sampling stuff -> [%d %d]\n", scaled_width, scaled_height);
		ret = pxp_legacy_process_resample(upd_data,fb_data,scaled_width,scaled_height,1);
		if (ret) {
			dev_err(fb_data->dev, "Unable to submit PxP down sample task.\n");
			mutex_unlock(&fb_data->pxp_mutex);
			return ret;
		}

		/* This is a blocking call, so upon return PxP tx should be done */
		ret = pxp_complete_update(fb_data, &hist_stat);
		if (ret) {
			dev_err(fb_data->dev, "Unable to complete PxP down sample task.\n");
			mutex_unlock(&fb_data->pxp_mutex);
			return ret;
		}

		/* up sampling */
		dev_dbg(fb_data->dev, "doing up sampling stuff\n");
		ret = pxp_legacy_process_resample(upd_data,fb_data, scaled_width, scaled_height,0);
		if (ret) {
			dev_err(fb_data->dev, "Unable to submit PxP up sample task.\n");
			mutex_unlock(&fb_data->pxp_mutex);
			return ret;
		}

		/* This is a blocking call, so upon return PxP tx should be done */
		ret = pxp_complete_update(fb_data, &hist_stat);
		if (ret) {
			dev_err(fb_data->dev, "Unable to complete PxP up sample task.\n");
			mutex_unlock(&fb_data->pxp_mutex);
			return ret;
		}
		dev_dbg(fb_data->dev, "done resampling\n");
		mutex_unlock(&fb_data->pxp_mutex);

		/* debug purpose */
		printk(KERN_INFO"mxc_epdc_fb:SW mitigation count=%d strength=%d res %d %d %d\n",
			count, strength, rescale_ratio, scaled_width,scaled_height);
#if defined(show_resample)
		if (mxc_epdc_debugging) {
			char disp[32]={0};

			sprintf(disp,"count = %d",count);
			paint_string(fb_data, left+width/2,top+height/2,disp);
			sprintf(disp,"strength=%d", strength);
			paint_string(fb_data, left+width/2, top+32+height/2, disp);
			sprintf(disp,"n_avg=%d s_avg=%d", n_avg, s_avg);
			paint_string(fb_data, left+width/2, top+64+height/2, disp);
			sprintf(disp,"scale factor .%d",fb_data->sw_algo.scaled_factor);
			paint_string(fb_data, left+width/2, top+96+height/2, disp);
			sprintf(disp,"res ratio %d",rescale_ratio);
			paint_string(fb_data, left+width/2, top+128+height/2, disp);
			sprintf(disp,"res w=%d h=%d",scaled_width,scaled_height);
			paint_string(fb_data, left+width/2, top+160+height/2, disp);
		}
#endif
	}

	return 0;
}

#endif

static int mxc_epdc_fb_send_single_update(struct mxcfb_update_data *upd_data,
				   struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info : g_fb_data;
	struct update_data_list *upd_data_list = NULL;
	struct mxcfb_rect *screen_upd_region; /* Region on screen to update */
	int temp_index;
	int ret;
	struct update_desc_list *upd_desc;
	struct update_marker_data *marker_data, *next_marker, *temp_marker;
	bool is_reagl = false;
	bool wb_pause = false;

	if (mxc_epdc_paused) {
		dev_err(fb_data->dev, "Updates paused ... not sending to epdc\n");
		return -EPERM;
	}

	/* Has EPDC HW been initialized? */
	if (!fb_data->hw_ready) {
		/* Throw message if we are not mid-initialization */
		if (!fb_data->hw_initializing)
			dev_err(fb_data->dev, "Display HW not properly "
				"initialized. Aborting update.\n");
		return -EPERM;
	}

	/* Check validity of update params */
	if ((upd_data->update_mode != UPDATE_MODE_PARTIAL) &&
		(upd_data->update_mode != UPDATE_MODE_FULL)) {
		dev_err(fb_data->dev,
			"Update mode 0x%x is invalid.  Aborting update.\n",
			upd_data->update_mode);
		return -EINVAL;
	}
	if ((upd_data->waveform_mode > 255) &&
		(upd_data->waveform_mode != WAVEFORM_MODE_AUTO)) {
		dev_err(fb_data->dev,
			"Update waveform mode 0x%x is invalid."
			"  Aborting update.\n",
			upd_data->waveform_mode);
		return -EINVAL;
	}
	if ((upd_data->update_region.left + upd_data->update_region.width > fb_data->epdc_fb_var.xres) ||
		(upd_data->update_region.top + upd_data->update_region.height > fb_data->epdc_fb_var.yres)) {
		dev_err(fb_data->dev,
		        "Update region is outside bounds of framebuffer [l:%d,t:%d,w:%d,h:%d]. Aborting update.\n",
		        upd_data->update_region.left, upd_data->update_region.top,
		        upd_data->update_region.width, upd_data->update_region.height);
		return -EINVAL;
	}
	if (upd_data->flags & EPDC_FLAG_USE_ALT_BUFFER) {
		if ((upd_data->update_region.width !=
			upd_data->alt_buffer_data.alt_update_region.width) ||
			(upd_data->update_region.height !=
			upd_data->alt_buffer_data.alt_update_region.height)) {
			dev_err(fb_data->dev,
				"Alternate update region dimensions must "
				"match screen update region dimensions.\n");
			return -EINVAL;
		}
		/* Validate physical address parameter */
		if ((upd_data->alt_buffer_data.phys_addr <
			fb_data->info.fix.smem_start) ||
			(upd_data->alt_buffer_data.phys_addr >
			fb_data->info.fix.smem_start + fb_data->map_size)) {
			dev_err(fb_data->dev,
				"Invalid physical address for alternate "
				"buffer.  Aborting update...\n");
			return -EINVAL;
		}
	}

	/* Conditions for using REAGL waveform:
	 *   1) REAGL waveform mode is specified
	 *   2) Waveform format is 5-bit
	 *   3) Scheme is Queue or Queue & Merge
	 */
	/* LAB126: This needs to be done before waveform mapping */
	is_reagl =
		((upd_data->waveform_mode == WAVEFORM_MODE_REAGL ||
			upd_data->waveform_mode == WAVEFORM_MODE_REAGLD) &&
		(fb_data->buf_pix_fmt == EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N)) ? 1 : 0;
	/* Warn against REAGL updates w/o REAGL waveform */
	if ((upd_data->waveform_mode == WAVEFORM_MODE_REAGL ||
				upd_data->waveform_mode == WAVEFORM_MODE_REAGLD) &&
		(fb_data->buf_pix_fmt != EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N))
		dev_err(fb_data->dev, "REAGL waveform update specified "
				"without valid REAGL waveform file.\n");
	wb_pause = true;

	if (mxc_epdc_debugging)
	{
		printk(KERN_INFO "mxc_epdc_fb: [%d] Requested waveforms: mode: 0x%x __ BW: 0x%x __ Gray : 0x%x\n",
				upd_data->update_marker,
				upd_data->waveform_mode,
				upd_data->hist_bw_waveform_mode,
				upd_data->hist_gray_waveform_mode);
	}

	/*
	 * On the i.MX6SL, convert all GC16 partial updates to GL16 partial.
	 * The silicon is not aware that pixel states 29, 30, and 31 are
	 * identical in the REAGL waveforms. As a result, pixels transitioning
	 * from 29 to 30 or 31 to 30 are included in the partial update,
	 * which results in a flashing transition when using GC16.
	 */
	if ((fb_data->rev == EPDC_VERSION_2_0_0 || fb_data->rev == EPDC_VERSION_2_1_0) &&
			(upd_data->update_mode == UPDATE_MODE_PARTIAL) &&
			(fb_data->waveform_type & WAVEFORM_TYPE_5BIT) &&
			((upd_data->waveform_mode == WAVEFORM_MODE_GC16_FAST) ||
			 (upd_data->waveform_mode == WAVEFORM_MODE_GC16) ||
			 (upd_data->waveform_mode == WAVEFORM_MODE_AUTO)))
	{
		convert_gc16_to_gl16(&upd_data->hist_bw_waveform_mode);
		convert_gc16_to_gl16(&upd_data->hist_gray_waveform_mode);
		convert_gc16_to_gl16(&upd_data->waveform_mode);
	}

	// Lab126: Convert to valid waveforms
	upd_data->waveform_mode = get_waveform_by_type(fb_data, upd_data->waveform_mode);
	upd_data->hist_bw_waveform_mode = get_waveform_by_type(fb_data, upd_data->hist_bw_waveform_mode);
	upd_data->hist_gray_waveform_mode = get_waveform_by_type(fb_data, upd_data->hist_gray_waveform_mode);

	// Lab126: Check validity of prefered waveforms and override if needed
	if (upd_data->hist_bw_waveform_mode == WAVEFORM_MODE_INIT ||
	    upd_data->hist_bw_waveform_mode == WAVEFORM_MODE_AUTO)
		upd_data->hist_bw_waveform_mode = fb_data->wv_modes.mode_du;
	if (upd_data->hist_gray_waveform_mode == WAVEFORM_MODE_INIT ||
	    upd_data->hist_gray_waveform_mode == WAVEFORM_MODE_AUTO)
		upd_data->hist_gray_waveform_mode = fb_data->wv_modes.mode_gc16;

	if (mxc_epdc_debugging)
	{
		printk(KERN_INFO "mxc_epdc_fb: [%d] Converted waveforms: mode: 0x%x (%s) __ BW: 0x%x (%s) __ Gray : 0x%x (%s)\n",
		       upd_data->update_marker,
		       upd_data->waveform_mode,           wfm_name_for_mode(fb_data, upd_data->waveform_mode),
		       upd_data->hist_bw_waveform_mode,   wfm_name_for_mode(fb_data, upd_data->hist_bw_waveform_mode),
		       upd_data->hist_gray_waveform_mode, wfm_name_for_mode(fb_data, upd_data->hist_gray_waveform_mode));
	}

	mutex_lock(&fb_data->queue_mutex);

	/*
	 * If we are waiting to go into suspend, or the FB is blanked,
	 * we do not accept new updates
	 */
	if ((fb_data->waiting_for_idle) ||
		(fb_data->blank != FB_BLANK_UNBLANK)) {
		dev_err(fb_data->dev, "EPDC not active."
			"Update request abort.\n");
		mutex_unlock(&fb_data->queue_mutex);
		return -EPERM;
	}

#if defined(FIX_CURRENT_RESAMPLE)
	dev_dbg(fb_data->dev,"mxc_epdc_fb: do pre_pixel_processing\n");
	if (!is_reagl) {
		ret = mxc_epdc_pixel_pre_process(upd_data, fb_data);
		if (ret!=0)
			dev_err(fb_data->dev,"mxc_epdc_pixel_pre_process err=%d\n",ret);
	}
#endif
	if (fb_data->upd_scheme == UPDATE_SCHEME_SNAPSHOT) {
		int count = 0;
		struct update_data_list *plist;

		/*
		 * If next update is a FULL mode update, then we must
		 * ensure that all pending & active updates are complete
		 * before submitting the update.  Otherwise, the FULL
		 * mode update may cause an endless collision loop with
		 * other updates.  Block here until updates are flushed.
		 */
		if (upd_data->update_mode == UPDATE_MODE_FULL) {
			mutex_unlock(&fb_data->queue_mutex);
			mxc_epdc_fb_flush_updates(fb_data);
			mutex_lock(&fb_data->queue_mutex);
		}

		/* Count buffers in free buffer list */
		list_for_each_entry(plist, &fb_data->upd_buf_free_list, list)
			count++;

		/* Use count to determine if we have enough
		 * free buffers to handle this update request */
		if (count + fb_data->max_num_buffers
			<= fb_data->max_num_updates) {
			dev_err(fb_data->dev,
				"No free intermediate buffers available.\n");
			mutex_unlock(&fb_data->queue_mutex);
			return -ENOMEM;
		}

		/* Grab first available buffer and delete from the free list */
		upd_data_list =
		    list_entry(fb_data->upd_buf_free_list.next,
			       struct update_data_list, list);

		list_del_init(&upd_data_list->list);
	}

	/*
	 * Create new update data structure, fill it with new update
	 * data and add it to the list of pending updates
	 */
	upd_desc = kzalloc(sizeof(struct update_desc_list), GFP_KERNEL);
	if (!upd_desc) {
		dev_err(fb_data->dev,
			"Insufficient system memory for update! Aborting.\n");
		if (fb_data->upd_scheme == UPDATE_SCHEME_SNAPSHOT) {
			list_add(&upd_data_list->list,
				&fb_data->upd_buf_free_list);
		}
		mutex_unlock(&fb_data->queue_mutex);
		return -EPERM;
	}

	upd_desc->is_reagl = is_reagl;
	upd_desc->wb_pause = wb_pause;

	/* Initialize per-update marker list */
	INIT_LIST_HEAD(&upd_desc->upd_marker_list);
	upd_desc->upd_data = *upd_data;
	upd_desc->update_order = fb_data->order_cnt++;

	list_add_tail(&upd_desc->list, &fb_data->upd_pending_list);

	/* If marker specified, associate it with a completion */
	if (upd_data->update_marker != 0) {
		/* Allocate new update marker and set it up */
		marker_data = kzalloc(sizeof(struct update_marker_data),
				GFP_KERNEL);
		if (!marker_data) {
			dev_err(fb_data->dev, "No memory for marker!\n");
			mutex_unlock(&fb_data->queue_mutex);
			return -ENOMEM;
		}
		list_add_tail(&marker_data->upd_list,
			&upd_desc->upd_marker_list);
		marker_data->update_marker = upd_data->update_marker;
		if (upd_desc->upd_data.flags & EPDC_FLAG_TEST_COLLISION)
			marker_data->lut_num = DRY_RUN_NO_LUT;
		else
			marker_data->lut_num = INVALID_LUT;
		marker_data->lut_mask = 0;
		marker_data->submitted = false;
		init_completion(&marker_data->update_completion);
		init_completion(&marker_data->submit_completion);
		/* Add marker to master marker list */
		list_add_tail(&marker_data->full_list,
			&fb_data->full_marker_list);


		if (mxc_epdc_debugging) {
			marker_data->start_time = timeofday_msec();
			printk(KERN_INFO "mxc_epdc_fb: [%d] update start marker=%d, start time=%lld\n",
			       marker_data->update_marker, marker_data->update_marker, marker_data->start_time);
			printk(KERN_INFO "mxc_epdc_fb: [%d] waveform=0x%x (%s) mode=0x%x temp:%d update region top=%d, left=%d, width=%d, height=%d flags=0x%x\n",
			       marker_data->update_marker, upd_data->waveform_mode, wfm_name_for_mode(fb_data, upd_data->waveform_mode),
			       upd_data->update_mode, upd_data->temp, upd_data->update_region.top, upd_data->update_region.left,
			       upd_data->update_region.width, upd_data->update_region.height,
						 upd_data->flags);
		}
	}

	/* Queued update scheme processing */
	if (fb_data->upd_scheme != UPDATE_SCHEME_SNAPSHOT) {
		/* Signal workqueue to handle new update */
		queue_work(fb_data->epdc_submit_workqueue,
			&fb_data->epdc_submit_work);

		mutex_unlock(&fb_data->queue_mutex);
		return 0;
	}

	/* Snapshot update scheme processing */

	/* Set descriptor for current update, delete from pending list */
	upd_data_list->update_desc = upd_desc;
	list_del_init(&upd_desc->list);

	fb_data->updates_active = true;
	mutex_unlock(&fb_data->queue_mutex);

	/*
	 * Hold on to original screen update region, which we
	 * will ultimately use when telling EPDC where to update on panel
	 */
	screen_upd_region = &upd_desc->upd_data.update_region;

	/* Select from PxP output buffers */
	upd_data_list->phys_addr =
		fb_data->phys_addr_updbuf[fb_data->upd_buffer_num];
	upd_data_list->virt_addr =
		fb_data->virt_addr_updbuf[fb_data->upd_buffer_num];
	fb_data->upd_buffer_num++;
	if (fb_data->upd_buffer_num > fb_data->max_num_buffers-1)
		fb_data->upd_buffer_num = 0;

	epdc_process_update(upd_data_list, fb_data);

	/* Pass selected waveform mode back to user */
	upd_data->waveform_mode = upd_desc->upd_data.waveform_mode;

	/* Get rotation-adjusted coordinates */
	adjust_coordinates(fb_data->epdc_fb_var.xres,
		fb_data->epdc_fb_var.yres, fb_data->epdc_fb_var.rotate,
		&upd_desc->upd_data.update_region, NULL);

	/* Grab lock for queue manipulation and update submission */
	mutex_lock(&fb_data->queue_mutex);

	/*
	 * Is the working buffer idle?
	 * If either the working buffer is busy, or there are no LUTs available,
	 * then we return and let the ISR handle the update later
	 */
	if ((fb_data->cur_update != NULL) || !epdc_any_luts_available()) {
		/* Add processed Y buffer to update list */
		list_add_tail(&upd_data_list->list, &fb_data->upd_buf_queue);

		/* Return and allow the update to be submitted by the ISR. */
		mutex_unlock(&fb_data->queue_mutex);
		return 0;
	}

	/* LUTs are available, so we get one here */
	ret = epdc_choose_next_lut(fb_data->rev, &upd_data_list->lut_num);
	if (ret && fb_data->tce_prevent && (fb_data->rev < EPDC_VERSION_2_0_0)) {
		dev_dbg(fb_data->dev, "Must wait for LUT15\n");
		/* Add processed Y buffer to update list */
		list_add_tail(&upd_data_list->list, &fb_data->upd_buf_queue);

		/* Return and allow the update to be submitted by the ISR. */
		mutex_unlock(&fb_data->queue_mutex);
		return 0;
	}

	if (!(upd_data_list->update_desc->upd_data.flags
		& EPDC_FLAG_TEST_COLLISION)) {

		/* Save current update */
		fb_data->cur_update = upd_data_list;

		/* Reset mask for LUTS that have completed during WB processing */
		fb_data->luts_complete_wb = 0;

		/* Associate LUT with update marker */
		list_for_each_entry_safe(next_marker, temp_marker,
			&upd_data_list->update_desc->upd_marker_list, upd_list)
			next_marker->lut_num = upd_data_list->lut_num;

		/* Mark LUT as containing new update */
		fb_data->lut_update_order[upd_data_list->lut_num] =
			upd_desc->update_order;

		epdc_lut_complete_intr(fb_data->rev, upd_data_list->lut_num,
					true);
	}

	/* Clear status and Enable LUT complete and WB complete IRQs */
	epdc_working_buf_intr(true);

	/* Program EPDC update to process buffer */
	epdc_set_update_addr(upd_data_list->phys_addr + upd_desc->epdc_offs);
	epdc_set_update_coord(screen_upd_region->left, screen_upd_region->top);
	epdc_set_update_dimensions(screen_upd_region->width,
		screen_upd_region->height);
	if (fb_data->rev > EPDC_VERSION_2_0_0)
		epdc_set_update_stride(upd_desc->epdc_stride);
	if (upd_desc->upd_data.temp == TEMP_USE_AUTO) {
		temp_index = mxc_epdc_fb_get_temp_index(fb_data, display_temp_c);
		epdc_set_temp(temp_index);
	} else if (upd_desc->upd_data.temp != TEMP_USE_AMBIENT) {
		temp_index = mxc_epdc_fb_get_temp_index(fb_data, upd_desc->upd_data.temp);
		epdc_set_temp(temp_index);
	} else {
		epdc_set_temp(fb_data->temp_index);
	}
	if (fb_data->wv_modes_update &&
		(upd_desc->upd_data.waveform_mode == WAVEFORM_MODE_AUTO)) {
		epdc_set_update_waveform(&fb_data->wv_modes);
		fb_data->wv_modes_update = false;
	}

	epdc_submit_update(upd_data_list->lut_num,
			   upd_desc->upd_data.waveform_mode,
			   upd_desc->upd_data.update_mode,
			   (upd_desc->upd_data.flags
				& EPDC_FLAG_TEST_COLLISION) ? true : false,
			   upd_data_list->update_desc->wb_pause, false, 0);


	mutex_unlock(&fb_data->queue_mutex);
	return 0;
}

int mxc_epdc_fb_send_update(struct mxcfb_update_data *upd_data,
				   struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;

	if (!fb_data->restrict_width) {
		/* No width restriction, send entire update region */
		return mxc_epdc_fb_send_single_update(upd_data, info);
	} else {
		int ret;
		__u32 width, left;
		__u32 marker;
		__u32 *region_width, *region_left;
		u32 max_upd_width = EPDC_V2_MAX_UPDATE_WIDTH;

		/* Further restrict max width due to pxp rotation
		  * alignment requirement
		  */
		if (fb_data->epdc_fb_var.rotate != FB_ROTATE_UR)
			max_upd_width -= EPDC_V2_ROTATION_ALIGNMENT;

		/* Select split of width or height based on rotation */
		if ((fb_data->epdc_fb_var.rotate == FB_ROTATE_UR) ||
			(fb_data->epdc_fb_var.rotate == FB_ROTATE_UD)) {
			region_width = &upd_data->update_region.width;
			region_left = &upd_data->update_region.left;
		} else {
			region_width = &upd_data->update_region.height;
			region_left = &upd_data->update_region.top;
		}

		if (*region_width <= max_upd_width)
			return mxc_epdc_fb_send_single_update(upd_data,	info);

		width = *region_width;
		left = *region_left;
		marker = upd_data->update_marker;
		upd_data->update_marker = 0;

		do {
			*region_width = max_upd_width;
			*region_left = left;
			ret = mxc_epdc_fb_send_single_update(upd_data, info);
			if (ret)
				return ret;
			width -= max_upd_width;
			left += max_upd_width;
		} while (width > max_upd_width);

		*region_width = width;
		*region_left = left;
		upd_data->update_marker = marker;
		return mxc_epdc_fb_send_single_update(upd_data, info);
	}
}
EXPORT_SYMBOL(mxc_epdc_fb_send_update);

int mxc_epdc_fb_wait_update_complete(u32 update_marker,
						struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;
	struct update_marker_data *next_marker;
	struct update_marker_data *temp;
	bool marker_found = false;
	int ret = 0;

	/* 0 is an invalid update_marker value */
	if (update_marker == 0)
		return -EINVAL;

	/*
	 * Find completion associated with update_marker requested.
	 * Note: If update completed already, marker will have been
	 * cleared, it won't be found, and function will just return.
	 */

	/* Grab queue lock to protect access to marker list */
	mutex_lock(&fb_data->queue_mutex);

	list_for_each_entry_safe(next_marker, temp,
		&fb_data->full_marker_list, full_list) {
		if (next_marker->update_marker == update_marker) {
			dev_dbg(fb_data->dev, "Waiting for marker %d\n",
				update_marker);
			next_marker->waiting = true;
			marker_found = true;
			break;
		}
	}

	mutex_unlock(&fb_data->queue_mutex);

	/*
	 * If marker not found, it has either been signalled already
	 * or the update request failed.  In either case, just return.
	 */
	if (!marker_found)
		return ret;

	ret = wait_for_completion_timeout(&next_marker->update_completion,
						msecs_to_jiffies(5000));
	if (!ret) {
		dev_err(fb_data->dev,
			"Timed out waiting for update completion. Marker : %d\n", update_marker);
		return -ETIMEDOUT;
	}

	//marker_data->collision_test = next_marker->collision_test;

	/* Free update marker object */
	kfree(next_marker);

	return ret;
}
EXPORT_SYMBOL(mxc_epdc_fb_wait_update_complete);


int mxc_epdc_fb_wait_update_submission(u32 update_marker,
						struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;
	struct update_marker_data *next_marker;
	struct update_marker_data *temp;
	bool marker_found = false;
	int ret = 0;

	/* 0 is an invalid update_marker value */
	if (update_marker == 0)
		return -EINVAL;

	/*
	 * Find completion associated with update_marker requested.
	 * Note: If update completed already, marker will have been
	 * cleared, it won't be found, and function will just return.
	 */

	/* Grab queue lock to protect access to marker list */
	mutex_lock(&fb_data->queue_mutex);

	list_for_each_entry_safe(next_marker, temp,
		&fb_data->full_marker_list, full_list) {
		if (next_marker->update_marker == update_marker) {
			dev_dbg(fb_data->dev, "Waiting for marker %d\n",
				update_marker);
			marker_found = true;
			break;
		}
	}

	mutex_unlock(&fb_data->queue_mutex);

	/*
	 * If marker not found, it has either been signalled already
	 * or the update request failed.  In either case, just return.
	 */
	if (!marker_found || next_marker->submitted)
		return ret;

	ret = wait_for_completion_timeout(&next_marker->submit_completion,
						msecs_to_jiffies(5000));
	if (!ret) {
		dev_err(fb_data->dev,
			"Timed out waiting for update submission. Marker : %d\n", update_marker);
		return -ETIMEDOUT;
	}

	return ret;
}
EXPORT_SYMBOL(mxc_epdc_fb_wait_update_submission);


int mxc_epdc_fb_set_pwrdown_delay(u32 pwrdown_delay,
					    struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;

	fb_data->pwrdown_delay = pwrdown_delay;

	return 0;
}
EXPORT_SYMBOL(mxc_epdc_fb_set_pwrdown_delay);

int mxc_epdc_get_pwrdown_delay(struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = info ?
		(struct mxc_epdc_fb_data *)info:g_fb_data;

	return fb_data->pwrdown_delay;
}
EXPORT_SYMBOL(mxc_epdc_get_pwrdown_delay);

static int mxc_epdc_fb_ioctl(struct fb_info *info, unsigned int cmd,
			     unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret = -EINVAL;

	switch (cmd) {
	case MXCFB_SET_WAVEFORM_MODES:
		{
			struct mxcfb_waveform_modes modes;
			if (!copy_from_user(&modes, argp, sizeof(modes))) {
				mxc_epdc_fb_set_waveform_modes(&modes, info);
				ret = 0;
			}
			break;
		}
	case MXCFB_SET_TEMPERATURE:
		{
			int temperature;
			if (!get_user(temperature, (int32_t __user *) arg))
				ret = mxc_epdc_fb_set_temperature(temperature,
					info);
			break;
		}
	case MXCFB_SET_AUTO_UPDATE_MODE:
		{
			u32 auto_mode = 0;
			if (!get_user(auto_mode, (__u32 __user *) arg))
				ret = mxc_epdc_fb_set_auto_update(auto_mode,
					info);
			break;
		}
	case MXCFB_SET_UPDATE_SCHEME:
		{
			u32 upd_scheme = 0;
			if (!get_user(upd_scheme, (__u32 __user *) arg))
				ret = mxc_epdc_fb_set_upd_scheme(upd_scheme,
					info);
			break;
		}
	case MXCFB_SEND_UPDATE:
		{
			struct mxcfb_update_data upd_data;

			if (!copy_from_user(&upd_data, argp, sizeof(upd_data))) {
				ret = mxc_epdc_fb_send_update(&upd_data, info);
				if (ret == 0 && copy_to_user(argp, &upd_data, sizeof(upd_data)))
					ret = -EFAULT;
			} else {
				ret = -EFAULT;
			}

			break;
		}

	case MXCFB_WAIT_FOR_UPDATE_COMPLETE:
		{
			u32 update_marker = 0;
			if (!get_user(update_marker, (__u32 __user *) arg))
				ret =
					mxc_epdc_fb_wait_update_complete(update_marker,
							info);
			break;
		}
	case MXCFB_WAIT_FOR_UPDATE_SUBMISSION:
		{
			u32 update_marker = 0;
			if (!get_user(update_marker, (__u32 __user *) arg))
				ret =
					mxc_epdc_fb_wait_update_submission(update_marker,
							info);
			break;
		}
	case MXCFB_SET_PWRDOWN_DELAY:
		{
			int delay = 0;
			if (!get_user(delay, (__u32 __user *) arg))
				ret =
					mxc_epdc_fb_set_pwrdown_delay(delay, info);
			break;
		}
	case MXCFB_GET_PWRDOWN_DELAY:
		{
			int pwrdown_delay = mxc_epdc_get_pwrdown_delay(info);
			if (put_user(pwrdown_delay,
				(int __user *)argp))
				ret = -EFAULT;
			ret = 0;
			break;
		}
	case MXCFB_GET_WORK_BUFFER:
		{
			/* copy the epdc working buffer to the user space */
			struct mxc_epdc_fb_data *fb_data = info ? (struct mxc_epdc_fb_data *)info:g_fb_data;
			flush_cache_all();
			outer_flush_all();
			if (copy_to_user((void __user *)arg, (const void *) fb_data->working_buffer_A_virt, fb_data->working_buffer_size))
				ret = -EFAULT;
			else
				ret = 0;
			flush_cache_all();
			outer_flush_all();
			break;
		}
	case MXCFB_GET_TEMPERATURE:
		{
			struct mxc_epdc_fb_data *fb_data = info ? (struct mxc_epdc_fb_data *)info:g_fb_data;
			int temp;

			if (fb_data->temp_override != TEMP_USE_AUTO)
				temp = fb_data->temp_override;
			else
				temp = display_temp_c;
			ret = 0;
			if (put_user(temp, (int __user *)argp))
				ret = -EFAULT;
			break;
		}
	case MXCFB_SET_PAUSE:
		{
			mxc_epdc_paused = 1;
			ret = 0;
			break;
		}
	case MXCFB_GET_PAUSE:
		{
			if (put_user(mxc_epdc_paused,
						(int __user *)argp))
				ret = -EFAULT;
			ret = 0;
			break;
		}
	case MXCFB_SET_RESUME:
		{
			mxc_epdc_paused = 0;
			ret = 0;
			break;
		}
	case MXCFB_GET_WAVEFORM_TYPE:
		{
			struct mxc_epdc_fb_data *fb_data = info ? (struct mxc_epdc_fb_data *)info:g_fb_data;
			ret = 0;
			if (put_user(fb_data->waveform_type, (int __user *)argp))
				ret = -EFAULT;
			break;
		}
	case MXCFB_GET_MATERIAL_TYPE:
		{
			struct mxc_epdc_fb_data *fb_data = info ? (struct mxc_epdc_fb_data *)info:g_fb_data;
			if (fb_data->cur_mode)
			{
				ret = 0;
				if (put_user(fb_data->cur_mode->material, (int __user *)argp))
					ret = -EFAULT;
			}
			break;
		}
	default:
		break;
	}
	return ret;
}

static void mxc_epdc_fb_update_pages(struct mxc_epdc_fb_data *fb_data,
				     u16 y1, u16 y2)
{
	struct mxcfb_update_data update;

	/* Do partial screen update, Update full horizontal lines */
	update.update_region.left = 0;
	update.update_region.width = fb_data->epdc_fb_var.xres;
	update.update_region.top = y1;
	update.update_region.height = y2 - y1;
	update.waveform_mode = WAVEFORM_MODE_AUTO;
	update.update_mode = UPDATE_MODE_FULL;
	update.update_marker = 0;
	update.temp = TEMP_USE_AUTO;
	update.flags = 0;

	mxc_epdc_fb_send_update(&update, &fb_data->info);
}

/* this is called back from the deferred io workqueue */
static void mxc_epdc_fb_deferred_io(struct fb_info *info,
				    struct list_head *pagelist)
{
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;
	struct page *page;
	unsigned long beg, end;
	int y1, y2, miny, maxy;

	if (fb_data->auto_mode != AUTO_UPDATE_MODE_AUTOMATIC_MODE)
		return;

	miny = INT_MAX;
	maxy = 0;
	list_for_each_entry(page, pagelist, lru) {
		beg = page->index << PAGE_SHIFT;
		end = beg + PAGE_SIZE - 1;
		y1 = beg / info->fix.line_length;
		y2 = end / info->fix.line_length;
		if (y2 >= fb_data->epdc_fb_var.yres)
			y2 = fb_data->epdc_fb_var.yres - 1;
		if (miny > y1)
			miny = y1;
		if (maxy < y2)
			maxy = y2;
	}

	mxc_epdc_fb_update_pages(fb_data, miny, maxy);
}

void mxc_epdc_fb_flush_updates(struct mxc_epdc_fb_data *fb_data)
{
	int ret;

	if (fb_data->in_init)
		return;

	/* Grab queue lock to prevent any new updates from being submitted */
	mutex_lock(&fb_data->queue_mutex);
	mutex_lock(&fb_data->power_mutex);

	/*
	 * 3 places to check for updates that are active or pending:
	 *   1) Updates in the pending list
	 *   2) Update buffers in use (e.g., PxP processing)
	 *   3) Active updates to panel - We can key off of EPDC
	 *      power state to know if we have active updates.
	 */
	if (!list_empty(&fb_data->upd_pending_list) ||
			!list_empty(&fb_data->upd_buf_collision_list) ||
			!is_free_list_full(fb_data) ||
			(fb_data->updates_active == true)) {
		/* Initialize event signalling updates are done */
		init_completion(&fb_data->updates_done);
		fb_data->waiting_for_idle = true;

		mutex_unlock(&fb_data->power_mutex);
		mutex_unlock(&fb_data->queue_mutex);
		/* Wait for any currently active updates to complete */
		ret = wait_for_completion_timeout(&fb_data->updates_done,
						msecs_to_jiffies(8000));
		if (!ret)
			dev_err(fb_data->dev,
				"Flush updates timeout! ret = 0x%x\n", ret);

		mutex_lock(&fb_data->queue_mutex);
		mutex_lock(&fb_data->power_mutex);
		fb_data->waiting_for_idle = false;
	}

	mutex_unlock(&fb_data->power_mutex);
	mutex_unlock(&fb_data->queue_mutex);
}

static int mxc_epdc_fb_blank(int blank, struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;
	int ret;

	dev_dbg(fb_data->dev, "blank = %d\n", blank);

	if (fb_data->blank == blank)
		return 0;

	switch (blank) {
	case FB_BLANK_POWERDOWN:
		mxc_epdc_fb_flush_updates(fb_data);
		/* Wait for powerdown */
		mutex_lock(&fb_data->power_mutex);
		if ((fb_data->power_state != POWER_STATE_OFF) &&
			(fb_data->pwrdown_delay == FB_POWERDOWN_DISABLE)) {

			/* Powerdown disabled, so we disable EPDC manually */
			int count = 0;
			int sleep_ms = 10;

			mutex_unlock(&fb_data->power_mutex);

			/* If any active updates, wait for them to complete */
			while (fb_data->updates_active) {
				/* Timeout after 1 sec */
				if ((count * sleep_ms) > 1000)
					break;
				msleep(sleep_ms);
				count++;
			}

			mutex_lock(&fb_data->power_mutex);
			epdc_force_powerdown(fb_data);
			mutex_unlock(&fb_data->power_mutex);
		} else if (fb_data->power_state != POWER_STATE_OFF) {
			fb_data->wait_for_powerdown = true;
			init_completion(&fb_data->powerdown_compl);
			mutex_unlock(&fb_data->power_mutex);
			ret = wait_for_completion_timeout(&fb_data->powerdown_compl,
				msecs_to_jiffies(5000));
			if (!ret) {
				dev_err(fb_data->dev, "No powerdown received! Forcing powerdown\n");
				mutex_lock(&fb_data->power_mutex);
				epdc_force_powerdown(fb_data);
				mutex_unlock(&fb_data->power_mutex);
			}
		} else
			mutex_unlock(&fb_data->power_mutex);
		// Fallthrough
	case FB_BLANK_UNBLANK:
		fb_data->blank = blank;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		mxc_epdc_fb_flush_updates(fb_data);
		break;
	}
	return 0;
}

static int mxc_epdc_fb_pan_display(struct fb_var_screeninfo *var,
				   struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;
	u_int y_bottom;

	dev_dbg(info->device, "%s: var->yoffset %d, info->var.yoffset %d\n",
		 __func__, var->yoffset, info->var.yoffset);
	/* check if var is valid; also, xpan is not supported */
	if (!var || (var->xoffset != info->var.xoffset) ||
	    (var->yoffset + var->yres > var->yres_virtual)) {
		dev_dbg(info->device, "x panning not supported\n");
		return -EINVAL;
	}

	if ((fb_data->epdc_fb_var.xoffset == var->xoffset) &&
		(fb_data->epdc_fb_var.yoffset == var->yoffset))
		return 0;	/* No change, do nothing */

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += var->yres;

	if (y_bottom > info->var.yres_virtual)
		return -EINVAL;

	mutex_lock(&fb_data->queue_mutex);

	fb_data->fb_offset = (var->yoffset * var->xres_virtual + var->xoffset)
		* (var->bits_per_pixel) / 8;

	fb_data->epdc_fb_var.xoffset = var->xoffset;
	fb_data->epdc_fb_var.yoffset = var->yoffset;

	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;

	mutex_unlock(&fb_data->queue_mutex);

	return 0;
}

static struct fb_ops mxc_epdc_fb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = mxc_epdc_fb_check_var,
	.fb_set_par = mxc_epdc_fb_set_par,
	.fb_setcmap = mxc_epdc_fb_setcmap,
	.fb_setcolreg = mxc_epdc_fb_setcolreg,
	.fb_pan_display = mxc_epdc_fb_pan_display,
	.fb_ioctl = mxc_epdc_fb_ioctl,
	.fb_mmap = mxc_epdc_fb_mmap,
	.fb_blank = mxc_epdc_fb_blank,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
};

static struct fb_deferred_io mxc_epdc_fb_defio = {
	.delay = HZ,
	.deferred_io = mxc_epdc_fb_deferred_io,
};

static void epdc_done_work_func(struct work_struct *work)
{
	struct mxc_epdc_fb_data *fb_data =
		container_of(work, struct mxc_epdc_fb_data, epdc_done_work.work);
	mutex_lock(&fb_data->power_mutex);
	epdc_powerdown(fb_data);
	mutex_unlock(&fb_data->power_mutex);
}

static void do_paused_update(struct mxc_epdc_fb_data *fb_data, int is_cancelled)
{
	struct update_data_list *upd_data_list = fb_data->cur_update;
	struct mxcfb_rect adj_update_region;
	int waveform_mode = upd_data_list->update_desc->upd_data.waveform_mode;
	int is_reagl = upd_data_list->update_desc->is_reagl;
	int temp_index;

	if (fb_data->buf_pix_fmt != EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N)
		is_reagl = false;

	if (upd_data_list->update_desc->upd_data.temp == TEMP_USE_AUTO) {
		temp_index = mxc_epdc_fb_get_temp_index(fb_data, display_temp_c);
	} else if (upd_data_list->update_desc->upd_data.temp != TEMP_USE_AMBIENT) {
		temp_index = mxc_epdc_fb_get_temp_index(fb_data, upd_data_list->update_desc->upd_data.temp);
	} else {
		temp_index = fb_data->temp_index;
	}

	dev_dbg(fb_data->dev, "In paused update %d\n", is_cancelled);

	/* Disable UPD_DONE interrupt */
	epdc_upd_done_intr(false);
	fb_data->cur_update->update_desc->wb_pause = false;
	/* Enable working buffer interrupt */
	epdc_working_buf_intr(true);

	/* If the update is VOID we're done */
	if (is_cancelled)
		goto out;

	/* Get rotation-adjusted coordinates in case of REAGL */
	if (is_reagl && (waveform_mode == fb_data->wv_modes.mode_reagl ||
	    waveform_mode == fb_data->wv_modes.mode_reagld)) {
		adjust_coordinates(fb_data->epdc_fb_var.xres,
				fb_data->epdc_fb_var.yres, fb_data->epdc_fb_var.rotate,
				&upd_data_list->update_desc->upd_data.update_region,
				&adj_update_region);
	}

	/*
	 * We are paused between WB Processing and TCE scan. Perform REAGL
	 * or REAGL-D processing and then allow the update to complete.
	 */
#ifdef CONFIG_FB_MXC_EINK_REAGL
#if defined(CONFIG_LAB126) && !defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
	if (is_reagl && (waveform_mode == fb_data->wv_modes.mode_reagl ||
	    waveform_mode == fb_data->wv_modes.mode_reagld))
#else
	if (is_reagl && waveform_mode == fb_data->wv_modes.mode_reagl)
#endif // !CONFIG_FB_MXC_EINK_WORK_BUFFER_B
	{
		dev_dbg(fb_data->dev, "Doing REAGL Processing : %lld\n", timeofday_msec());
		/*
		 * REAGL processing API with Dual WB feature (EPDC v2.0+)
		 *   do_reagl_processing(
		 *		unsigned short *working_buffer_ptr_in,
		 *		unsigned short *working_buffer_ptr_out,
		 *		struct mxcfb_rect *update_region,
		 *		long working_buffer_width,
		 *		long working_buffer_height);
		 */

		waveform_mode = fb_data->wv_modes.mode_reagl;
		reagl_algos[fb_data->which_reagl].func(
				reagl_algos[fb_data->which_reagl].buffer_in,
				reagl_algos[fb_data->which_reagl].buffer_out,
				&adj_update_region,
				fb_data->native_width,
				fb_data->native_height);

		dev_dbg(fb_data->dev, "Done REAGL Processing: %lld\n", timeofday_msec());
	}
	else
#if !defined(CONFIG_LAB126) || (defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B))
	if (is_reagl && waveform_mode == fb_data->wv_modes.mode_reagld)
	{
		dev_dbg(fb_data->dev, "Doing REAGLD Processing\n");
		do_reagld_processing_v2_0_1((uint16_t *)fb_data->working_buffer_A_virt,
				(uint16_t *)fb_data->working_buffer_B_virt,
				&adj_update_region,
				fb_data->native_width,
				fb_data->native_height);
	}
	else
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B
#endif // CONFIG_FB_MXC_EINK_REAGL
	if (waveform_mode == WAVEFORM_MODE_AUTO) {
		int hist = epdc_get_histogram_np();
		if (hist & 0x3) {
			/* This is a black and white update */
			waveform_mode = upd_data_list->update_desc->upd_data.hist_bw_waveform_mode;
		}
		else {
			/* This is a gray update */
			waveform_mode = upd_data_list->update_desc->upd_data.hist_gray_waveform_mode;
		}
		if (mxc_epdc_debugging) {
			printk(KERN_INFO "mxc_epdc_fb: [%d] Histogram result=%u, waveform used=0x%x (%s)\n",
					fb_data->cur_update->update_desc->upd_data.update_marker,
					hist,
					waveform_mode,
					wfm_name_for_mode(fb_data, waveform_mode));
		}
	}

	/* We have a valid update. Check if we need to power on VCOM */
	mutex_lock(&fb_data->power_mutex);
	if (fb_data->power_state == POWER_STATE_GOING_UP) {
		dev_dbg(fb_data->dev, "%s: Waiting for Power up completion before VCOM. Power state: %d\n", __FUNCTION__, fb_data->power_state);
		mutex_unlock(&fb_data->power_mutex);
		if (!epdc_powerup_wait_for_enabled(fb_data)) {
			mutex_lock(&fb_data->power_mutex);
			dev_err(fb_data->dev, "Powerup timeout!!\n");
			fb_data->power_state = POWER_STATE_OFF;
			mutex_unlock(&fb_data->power_mutex);
			return;
		}
		mutex_lock(&fb_data->power_mutex);
		dev_dbg(fb_data->dev, "Powering up VCOM\n");
		epdc_powerup_vcom(fb_data, waveform_mode, temp_index);
	}
	mutex_unlock(&fb_data->power_mutex);

	/* Submit the update. WB_CMPLT will follow */
	epdc_submit_paused_update(waveform_mode);

out:
	dev_dbg(fb_data->dev, "Done Paused update\n");
}

static bool is_free_list_full(struct mxc_epdc_fb_data *fb_data)
{
	int count = 0;
	struct update_data_list *plist;

	/* Count buffers in free buffer list */
	list_for_each_entry(plist, &fb_data->upd_buf_free_list, list)
		count++;

	/* Check to see if all buffers are in this list */
	if (count == fb_data->max_num_updates)
		return true;
	else
		return false;
}

static irqreturn_t mxc_epdc_irq_quick_check_handler(int irq, void *dev_id)
{
	struct mxc_epdc_fb_data *fb_data = dev_id;
	u32 ints_fired, luts1_ints_fired, luts2_ints_fired;

	/*
	 * If we just completed one-time panel init, bypass
	 * queue handling, clear interrupt and return
	 */
	if (fb_data->in_init) {
		if (epdc_is_working_buffer_complete()) {
			epdc_working_buf_intr(false);
			epdc_clear_working_buf_irq();
			dev_dbg(fb_data->dev, "Cleared WB for init update\n");
		}

		if (epdc_is_lut_complete(fb_data->rev, 0)) {
			epdc_lut_complete_intr(fb_data->rev, 0, false);
			epdc_clear_lut_complete_irq(fb_data->rev, 0);
			fb_data->in_init = false;
			dev_dbg(fb_data->dev, "Cleared LUT complete for init update\n");
		}

		return IRQ_HANDLED;
	}

	ints_fired = __raw_readl(EPDC_IRQ_MASK) & __raw_readl(EPDC_IRQ);
	if (fb_data->rev < EPDC_VERSION_2_0_0) {
		luts1_ints_fired = 0;
		luts2_ints_fired = 0;
	} else {
		luts1_ints_fired = __raw_readl(EPDC_IRQ_MASK1) & __raw_readl(EPDC_IRQ1);
		luts2_ints_fired = __raw_readl(EPDC_IRQ_MASK2) & __raw_readl(EPDC_IRQ2);
	}

	if (!(ints_fired || luts1_ints_fired || luts2_ints_fired))
		return IRQ_HANDLED;

	if (__raw_readl(EPDC_IRQ) & EPDC_IRQ_TCE_UNDERRUN_IRQ) {
		dev_err(fb_data->dev,
			"TCE underrun! Will continue to update panel\n");
		/* Clear TCE underrun IRQ */
		__raw_writel(EPDC_IRQ_TCE_UNDERRUN_IRQ, EPDC_IRQ_CLEAR);
	}

	/* Check if we are waiting on EOF to sync a new update submission */
	if (epdc_signal_eof()) {
		epdc_eof_intr(false);
		epdc_clear_eof_irq();
		complete(&fb_data->eof_event);
	}

	/*
	 * Workaround for EPDC v2.0/v2.1 errata: Must read collision status
	 * before clearing IRQ, or else collision status for bits 16:63
	 * will be automatically cleared.  So we read it here, and there is
	 * no conflict with using it in epdc_intr_work_func since the
	 * working buffer processing flow is strictly sequential (i.e.,
	 * only one WB processing done at a time, so the data grabbed
	 * here should be up-to-date and accurate when the WB processing
	 * completes.  Also, note that there is no impact to other versions
	 * of EPDC by reading LUT status here.
	 */
	if (fb_data->cur_update != NULL)
		fb_data->epdc_colliding_luts = epdc_get_colliding_luts(fb_data->rev);

	/* Clear the interrupt mask for any interrupts signalled */
	__raw_writel(ints_fired, EPDC_IRQ_MASK_CLEAR);
	__raw_writel(luts1_ints_fired, EPDC_IRQ_MASK1_CLEAR);
	__raw_writel(luts2_ints_fired, EPDC_IRQ_MASK2_CLEAR);

	dev_dbg(fb_data->dev, "EPDC interrupts fired = 0x%x, "
		"LUTS1 fired = 0x%x, LUTS2 fired = 0x%x\n",
		ints_fired, luts1_ints_fired, luts2_ints_fired);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t mxc_epdc_irq_handler(int irq, void *data)
{
	struct mxc_epdc_fb_data *fb_data = data;
	struct update_data_list *collision_update;
	struct mxcfb_rect *next_upd_region;
	struct update_marker_data *next_marker;
	struct update_marker_data *temp;
	int temp_index;
	u64 temp_mask;
	u32 lut;
	bool ignore_collision = false;
	int i;
	bool wb_lut_done = false;
	bool free_update = true;
	int next_lut, epdc_next_lut_15;
	u32 epdc_luts_active, epdc_wb_busy, epdc_upd_done, epdc_luts_avail, epdc_lut_cancelled;
	u32 epdc_collision;
	u64 epdc_irq_stat;
	bool epdc_waiting_on_wb;
	u32 coll_coord, coll_size;
	struct mxcfb_rect coll_region;
	bool update_paused = false;
	bool wb_pause = false;
	bool do_collision = true;

	/* Protect access to buffer queues and to update HW */
	mutex_lock(&fb_data->queue_mutex);

	/* Capture EPDC status one time to limit exposure to race conditions */
	epdc_luts_active = epdc_any_luts_active(fb_data->rev);
	epdc_wb_busy = epdc_is_working_buffer_busy();
	epdc_upd_done = __raw_readl(EPDC_IRQ) & EPDC_IRQ_UPD_DONE_IRQ;
	epdc_lut_cancelled = epdc_is_lut_cancelled();
	epdc_luts_avail = epdc_any_luts_available();
	epdc_collision = epdc_is_collision();

	if (fb_data->rev < EPDC_VERSION_2_0_0)
		epdc_irq_stat = __raw_readl(EPDC_IRQ);
	else
		epdc_irq_stat = (u64)__raw_readl(EPDC_IRQ1) |
			((u64)__raw_readl(EPDC_IRQ2) << 32);
	epdc_waiting_on_wb = (fb_data->cur_update != NULL) ? true : false;
	if (fb_data->cur_update && fb_data->cur_update->update_desc->wb_pause)
		wb_pause = true;
	else
		wb_pause = false;

	if (!wb_pause)
		epdc_upd_done = 0;

	/* Free any LUTs that have completed */
	if (epdc_irq_stat) {
		for (i = 0; i < fb_data->num_luts; i++) {
			if ((epdc_irq_stat & LUT_MASK(i)) == 0)
				continue;

			dev_dbg(fb_data->dev, "LUT %d completed\n", i);

			/* Disable IRQ for completed LUT */
			epdc_lut_complete_intr(fb_data->rev, i, false);

			epdc_clear_lut_complete_irq(fb_data->rev, i);

			fb_data->luts_complete_wb |= LUT_MASK(i);

			fb_data->lut_update_order[i] = 0;

			/* Signal completion if submit workqueue needs a LUT */
			if (fb_data->waiting_for_lut) {
				complete(&fb_data->update_res_free);
				fb_data->waiting_for_lut = false;
			}

			/* Signal completion if LUT15 free and is needed */
			if (fb_data->waiting_for_lut15 && (i == 15)) {
				complete(&fb_data->lut15_free);
				fb_data->waiting_for_lut15 = false;
			}

			/* Detect race condition where WB and its LUT complete
				 (i.e. full update completes) in one swoop */
			if (epdc_waiting_on_wb &&
					(i == fb_data->cur_update->lut_num))
				wb_lut_done = true;

			/* Signal completion if anyone waiting on this LUT */
			if (!wb_lut_done)
			{
				list_for_each_entry_safe(next_marker, temp,
						&fb_data->full_marker_list,
						full_list) {

					// Clear the complete LUT from LUT dependencies for this marker
					next_marker->lut_mask &= ~LUT_MASK(i);

					/* This marker is waiting on other LUTs to complete */
					if (next_marker->lut_num == INVALID_LUT || next_marker->lut_mask)
						continue;

					/* Found marker to signal - remove from list */
					list_del_init(&next_marker->full_list);

					/* Signal completion of update */
					dev_dbg(fb_data->dev, "Signaling marker %d\n", next_marker->update_marker);

					if (mxc_epdc_debugging && next_marker->update_marker) {
						long long end_time = timeofday_msec();
						printk(KERN_INFO "mxc_epdc_fb: [%d] update end marker=%u, end time=%lld, time taken=%lld ms\n",
								next_marker->update_marker, next_marker->update_marker, end_time, end_time - next_marker->start_time);
					}

					if (next_marker->waiting)
						complete(&next_marker->update_completion);
					else
						kfree(next_marker);
				}
			}
		}
	}

	/*
	 * Go through all updates in the collision list and
	 * unmask any updates that were colliding with
	 * the completed LUTs.
	 */
	if (epdc_irq_stat) {
		list_for_each_entry(collision_update, &fb_data->upd_buf_collision_list, list)
			collision_update->collision_mask &= ~fb_data->luts_complete_wb;
	}

	/* Check to see if all updates have completed */
	if (list_empty(&fb_data->upd_pending_list) &&
			list_empty(&fb_data->upd_buf_collision_list) &&
			is_free_list_full(fb_data) &&
			!epdc_waiting_on_wb &&
			!epdc_luts_active &&
			!wb_pause) {

		mutex_lock(&fb_data->power_mutex);
		fb_data->updates_active = false;

		if (fb_data->pwrdown_delay != FB_POWERDOWN_DISABLE) {
			/*
			 * Set variable to prevent overlapping
			 * enable/disable requests
			 */
			fb_data->powering_down = true;
			mutex_unlock(&fb_data->power_mutex);
			cancel_delayed_work_sync(&fb_data->epdc_done_work);
			mutex_lock(&fb_data->power_mutex);
			schedule_delayed_work(&fb_data->epdc_done_work, msecs_to_jiffies(fb_data->pwrdown_delay));

		}

		if (fb_data->waiting_for_idle)
			complete(&fb_data->updates_done);
		mutex_unlock(&fb_data->power_mutex);
	}

	/*
	 * Is Working Buffer busy (for normal update case)?
	 * Is UPD_DONE complete (for paused update case)?
	 */
	if ((!wb_pause && epdc_wb_busy) || (wb_pause && !epdc_upd_done)) {
		/* Can't submit another update until WB is done */
		mutex_unlock(&fb_data->queue_mutex);
		return IRQ_HANDLED;
	}

	/*
	 * Were we waiting on working buffer?
	 * If so, update queues and check for collisions
	 */
	if (epdc_waiting_on_wb) {
		/* Signal completion if submit workqueue was waiting on WB */
		if (fb_data->waiting_for_wb && !wb_pause) {
			dev_dbg(fb_data->dev, "Working buffer complete\n");
			complete(&fb_data->wb_free);
			fb_data->waiting_for_wb = false;
		}

		/*
		 * Handle 3 possible events exclusive of each other, in
		 * the following priority order:
		 *   1) This is a collision test update
		 *   2) The LUT was cancelled (no pixels changing)
		 *   3) The update is being paused after WB processing
		 */
		if (fb_data->cur_update->update_desc->upd_data.flags
			& EPDC_FLAG_TEST_COLLISION) {
			/* This was a dry run to test for collision */

			/* Signal marker */
			list_for_each_entry_safe(next_marker, temp,
				&fb_data->full_marker_list,
				full_list) {
				if (next_marker->lut_num != DRY_RUN_NO_LUT)
					continue;

				if (epdc_collision)
					next_marker->collision_test = true;
				else
					next_marker->collision_test = false;

				dev_dbg(fb_data->dev,
					"In IRQ, collision_test = %d\n",
					next_marker->collision_test);

				/* Found marker to signal - remove from list */
				list_del_init(&next_marker->full_list);

				if (mxc_epdc_debugging && next_marker->update_marker) {
					long long end_time = timeofday_msec();
					printk(KERN_INFO "mxc_epdc_fb: [%d] update end marker=%u, end time=%lld, time taken=%lld ms\n",
								next_marker->update_marker, next_marker->update_marker, end_time, end_time - next_marker->start_time);
				}

				/* Signal completion of update */
				dev_dbg(fb_data->dev, "Signaling marker "
					"for dry-run - %d\n",
					next_marker->update_marker);
				if (next_marker->waiting)
					complete(&next_marker->update_completion);
				else
					kfree(next_marker);
			}
		} else if (wb_pause) {
			/*
			 * If the update is canceled the UPD_DONE interrupt will be cleared and the update
			 * will not be submitted.
			 * Then there are three cases to handle:
			 * 1. Regardless of the cancel flag, if collision is set handle the collision below
			 * 2. If no collision and update is cancelled, clear and free the update
			 * 3. Set update_paused and wait for the WB_CMPLT interrupt
			 */

			do_paused_update(fb_data, epdc_lut_cancelled);

			if (epdc_collision) {
				// The pause update has collided.
				// The code below will resubmit the update
				dev_dbg(fb_data->dev, "Paused + Collision. LUT: %d\n", fb_data->cur_update->lut_num);
				do_collision = true;
				if (fb_data->waiting_for_wb) {
					dev_dbg(fb_data->dev, "Working buffer complete\n");
					complete(&fb_data->wb_free);
					fb_data->waiting_for_wb = false;
				}
			} else if (epdc_lut_cancelled) {
				dev_dbg(fb_data->dev, "Paused + Cancel. LUT: %d\n", fb_data->cur_update->lut_num);
				epdc_lut_complete_intr(fb_data->rev,
						fb_data->cur_update->lut_num, false);
				epdc_clear_lut_complete_irq(fb_data->rev,
						fb_data->cur_update->lut_num);

				fb_data->lut_update_order[fb_data->cur_update->lut_num] = 0;
				free_update = true;
				wb_lut_done = true;
				if (fb_data->waiting_for_wb) {
					dev_dbg(fb_data->dev, "Working buffer complete\n");
					complete(&fb_data->wb_free);
					fb_data->waiting_for_wb = false;
				}
			}
			else {
				update_paused = true;
			}
		}

		if (epdc_collision && (!wb_pause || do_collision)) {

			int active_lut_count = 0;

			/* Real update (no dry-run), collision occurred */

			/* Check list of colliding LUTs, and add to our collision mask */
			fb_data->cur_update->collision_mask =
			    fb_data->epdc_colliding_luts;

			/* Clear collisions that completed since WB began */
			fb_data->cur_update->collision_mask &=
				~fb_data->luts_complete_wb;

			dev_dbg(fb_data->dev, "Collision mask = 0x%llx\n",
			       fb_data->epdc_colliding_luts);

			/* For EPDC 2.0 and later, minimum collision bounds
			   are provided by HW.  Recompute new bounds here. */
			if ((fb_data->upd_scheme != UPDATE_SCHEME_SNAPSHOT)
				&& (fb_data->rev >= EPDC_VERSION_2_0_0)) {
				u32 xres, yres, rotate;
				struct mxcfb_rect *cur_upd_rect =
					&fb_data->cur_update->update_desc->upd_data.update_region;

				// Get collision region coords from EPDC
				coll_coord = __raw_readl(EPDC_UPD_COL_CORD);
				coll_size = __raw_readl(EPDC_UPD_COL_SIZE);
				coll_region.left =
					(coll_coord & EPDC_UPD_COL_CORD_XCORD_MASK)
						>> EPDC_UPD_COL_CORD_XCORD_OFFSET;
				coll_region.top =
					(coll_coord & EPDC_UPD_COL_CORD_YCORD_MASK)
						>> EPDC_UPD_COL_CORD_YCORD_OFFSET;
				coll_region.width =
					(coll_size & EPDC_UPD_COL_SIZE_WIDTH_MASK)
						>> EPDC_UPD_COL_SIZE_WIDTH_OFFSET;
				coll_region.height =
					(coll_size & EPDC_UPD_COL_SIZE_HEIGHT_MASK)
						>> EPDC_UPD_COL_SIZE_HEIGHT_OFFSET;
				dev_dbg(fb_data->dev, "Coll region: l = %d, "
					"t = %d, w = %d, h = %d\n",
					coll_region.left, coll_region.top,
					coll_region.width, coll_region.height);

				// Convert coords back to orig orientation
				switch (fb_data->epdc_fb_var.rotate) {
				case FB_ROTATE_CW:
					xres = fb_data->epdc_fb_var.yres;
					yres = fb_data->epdc_fb_var.xres;
					rotate = FB_ROTATE_CCW;
					break;
				case FB_ROTATE_UD:
					xres = fb_data->epdc_fb_var.xres;
					yres = fb_data->epdc_fb_var.yres;
					rotate = FB_ROTATE_UD;
					break;
				case FB_ROTATE_CCW:
					xres = fb_data->epdc_fb_var.yres;
					yres = fb_data->epdc_fb_var.xres;
					rotate = FB_ROTATE_CW;
					break;
				default:
					xres = fb_data->epdc_fb_var.xres;
					yres = fb_data->epdc_fb_var.yres;
					rotate = FB_ROTATE_UR;
					break;
				}
				adjust_coordinates(xres, yres, rotate,
						&coll_region, cur_upd_rect);

				dev_dbg(fb_data->dev, "mxc_epdc_fb: Collision region: l = %d, t = %d, w = %d, h = %d LUT: %d\n",
						cur_upd_rect->left, cur_upd_rect->top,
						cur_upd_rect->width,
						cur_upd_rect->height,
						fb_data->cur_update->lut_num);
			}

			/*
			 * If we collide with newer updates, then
			 * we don't need to re-submit the update. The
			 * idea is that the newer updates should take
			 * precedence anyways, so we don't want to
			 * overwrite them.
			 */
			for (temp_mask = fb_data->cur_update->collision_mask, lut = 0;
				temp_mask != 0;
				lut++, temp_mask = temp_mask >> 1) {
				if (!(temp_mask & 0x1))
					continue;

				if (fb_data->lut_update_order[lut] >=
					fb_data->cur_update->update_desc->update_order) {
					dev_dbg(fb_data->dev, "Ignoring collision with newer update. LUT: %d\n", fb_data->cur_update->lut_num);
					ignore_collision = true;
					break;
				}
			}

			if (epdc_lut_cancelled) {
				/*
				 * Note: The update may be cancelled (void) if all
				 * pixels collided. In that case we handle it as a
				 * collision, not a cancel.
				 */

				/* Clear LUT status (might be set if no AUTOWV used) */

				/*
				 * Disable and clear IRQ for the LUT used.
				 * Even though LUT is cancelled in HW, the LUT
				 * complete bit may be set if AUTOWV not used.
				 */
				epdc_lut_complete_intr(fb_data->rev,
						fb_data->cur_update->lut_num, false);
				epdc_clear_lut_complete_irq(fb_data->rev,
						fb_data->cur_update->lut_num);

				fb_data->lut_update_order[fb_data->cur_update->lut_num] = 0;

				// Signal completion if submit workqueue needs a LUT
				if (fb_data->waiting_for_lut) {
					complete(&fb_data->update_res_free);
					fb_data->waiting_for_lut = false;
				}
				// Since this LUT is canceled we need to clear this LUT from all
				// markers that have it as a dependency
				list_for_each_entry_safe(next_marker, temp,
						&fb_data->cur_update->update_desc->upd_marker_list,
						upd_list) {
					dev_dbg(fb_data->dev, "Collision + Canceled. LUT: %d __ LUT_MASK: 0x%llx\n", fb_data->cur_update->lut_num, next_marker->lut_mask);
					next_marker->lut_mask &= ~LUT_MASK(fb_data->cur_update->lut_num);
					if (next_marker->lut_mask)
						active_lut_count++;
				}
			}

			if (!ignore_collision) {
				free_update = false;

				dev_dbg(fb_data->dev, "Collision + !Ignore. LUT: %d\n", fb_data->cur_update->lut_num);

				/*
				 * If update has markers, clear the LUTs to
				 * avoid signalling that they have completed.
				 */
				list_for_each_entry_safe(next_marker, temp,
					&fb_data->cur_update->update_desc->upd_marker_list,
					upd_list)
				{
					next_marker->lut_num = INVALID_LUT;
				}
				/* Move to collision list */
				list_add_tail(&fb_data->cur_update->list, &fb_data->upd_buf_collision_list);
			}
			else if (epdc_lut_cancelled) {
				// If Update is canceled we need to make sure there are no active LUTS pending for this update
				// before deleteing it.
				dev_dbg(fb_data->dev, "Collision + Ignore + Canceled. LUT: %d __ Active_LUT_COUNT: %d\n", fb_data->cur_update->lut_num, active_lut_count);
				update_paused = false;
				if (active_lut_count)
				{
					// Still LUTS pending on this update
					wb_lut_done = false;
				}
				else
				{
					// Update can be freed, we're all done.
					wb_lut_done = true;
				}
				dev_dbg(fb_data->dev, "Collision + Ignore + Canceled. LUT: %d __ Free_update: %d __ wb_lut_done: %d __ update_paused: %d\n", fb_data->cur_update->lut_num,
						free_update, wb_lut_done, update_paused);
			}
		}
		else if (epdc_lut_cancelled && !wb_pause) {
			/*
			* Note: The update may be cancelled (void) if all
			* pixels collided. In that case we handle it as a
			* collision, not a cancel.
			*/

			/* Clear LUT status (might be set if no AUTOWV used) */

			/*
			 * Disable and clear IRQ for the LUT used.
			 * Even though LUT is cancelled in HW, the LUT
			 * complete bit may be set if AUTOWV not used.
			 */
			dev_dbg(fb_data->dev, "Lut is canceled : %d\n", fb_data->cur_update->lut_num);
			epdc_lut_complete_intr(fb_data->rev,
					fb_data->cur_update->lut_num, false);
			epdc_clear_lut_complete_irq(fb_data->rev,
					fb_data->cur_update->lut_num);

			fb_data->lut_update_order[fb_data->cur_update->lut_num] = 0;

			/* Signal completion if submit workqueue needs a LUT */
			if (fb_data->waiting_for_lut) {
				complete(&fb_data->update_res_free);
				fb_data->waiting_for_lut = false;
			}
		}

		/*
		 * The update has made it to the working buffer.
		 * Signal anyone waiting on the submission
		 */
		if (!update_paused) {
			if (!list_empty(&fb_data->cur_update->update_desc->upd_marker_list)) {
				list_for_each_entry_safe(next_marker, temp,
						&fb_data->cur_update->update_desc->upd_marker_list, upd_list)
				{
					if (next_marker->update_marker != 0)
					{
						next_marker->submitted = true;
						complete(&next_marker->submit_completion);
					}
				}
			}
		}

		/* Do we need to free the current update descriptor? */
		if (free_update && !update_paused) {
			/* Handle condition where WB & LUT are both complete */
			if (wb_lut_done) {
				list_for_each_entry_safe(next_marker, temp,
					&fb_data->cur_update->update_desc->upd_marker_list,
					upd_list) {

					/* Del from per-update & full list */
					list_del_init(&next_marker->upd_list);
					list_del_init(&next_marker->full_list);

					if (mxc_epdc_debugging && next_marker->update_marker) {
						long long end_time = timeofday_msec();
						printk(KERN_INFO "mxc_epdc_fb: [%d] update end marker=%u, end time=%lld, time taken=%lld ms\n",
								next_marker->update_marker, next_marker->update_marker, end_time, end_time - next_marker->start_time);
					}

					/* Signal completion of update */
					dev_dbg(fb_data->dev, "Signaling marker (wb) %d\n", next_marker->update_marker);
					if (next_marker->waiting)
						complete(&next_marker->update_completion);
					else
						kfree(next_marker);
				}
			}

			/* Free marker list and update descriptor */
			kfree(fb_data->cur_update->update_desc);

			/* Add to free buffer list */
			list_add_tail(&fb_data->cur_update->list,
				 &fb_data->upd_buf_free_list);
		}

		if (!update_paused) {
			/* Clear current update */
			fb_data->cur_update = NULL;
			fb_data->luts_complete_wb = 0;


			/* Clear IRQ for working buffer */
			epdc_upd_done_intr(false);
			epdc_working_buf_intr(false);
			epdc_clear_working_buf_irq();
		}
	}

	/* Check to see if all updates have completed */
	/* This can only happen here if the LUT was cancelled. */
	/* In all other scenarios we will get a LUT completion */
	/* which in turn will call power-down once all LUTS have completed */
	if (list_empty(&fb_data->upd_pending_list) &&
			list_empty(&fb_data->upd_buf_collision_list) &&
			is_free_list_full(fb_data) &&
			!epdc_luts_active &&
			!update_paused &&
			!fb_data->cur_update) {

		mutex_lock(&fb_data->power_mutex);
		fb_data->updates_active = false;

		if (fb_data->pwrdown_delay != FB_POWERDOWN_DISABLE) {
			/*
			 * Set variable to prevent overlapping
			 * enable/disable requests
			 */
			fb_data->powering_down = true;
			/* Schedule EPDC disable */
			mutex_unlock(&fb_data->power_mutex);
			cancel_delayed_work_sync(&fb_data->epdc_done_work);
			mutex_lock(&fb_data->power_mutex);
			schedule_delayed_work(&fb_data->epdc_done_work, msecs_to_jiffies(fb_data->pwrdown_delay));
		}

		if (fb_data->waiting_for_idle)
			complete(&fb_data->updates_done);
		mutex_unlock(&fb_data->power_mutex);
	}

	if (fb_data->upd_scheme != UPDATE_SCHEME_SNAPSHOT) {
		/* Queued update scheme processing */

		/* Schedule task to submit collision and pending update */
		mutex_lock(&fb_data->power_mutex);
		if (!fb_data->powering_down) {
			queue_work(fb_data->epdc_submit_workqueue,
				&fb_data->epdc_submit_work);
		}
		mutex_unlock(&fb_data->power_mutex);

		/* Release buffer queues */
		mutex_unlock(&fb_data->queue_mutex);

		return IRQ_HANDLED;
	}

	/* Snapshot update scheme processing */

	/* Check to see if any LUTs are free */
	if (!epdc_luts_avail) {
		dev_dbg(fb_data->dev, "No luts available.\n");
		mutex_unlock(&fb_data->queue_mutex);
		return IRQ_HANDLED;
	}

	epdc_next_lut_15 = epdc_choose_next_lut(fb_data->rev, &next_lut);
	/* Check to see if there is a valid LUT to use */
	if (epdc_next_lut_15 && fb_data->tce_prevent && (fb_data->rev < EPDC_VERSION_2_0_0)) {
		dev_dbg(fb_data->dev, "Must wait for LUT15\n");
		mutex_unlock(&fb_data->queue_mutex);
		return IRQ_HANDLED;
	}

	/*
	 * Are any of our collision updates able to go now?
	 * Go through all updates in the collision list and check to see
	 * if the collision mask has been fully cleared
	 */
	list_for_each_entry(collision_update,
			    &fb_data->upd_buf_collision_list, list) {

		if (collision_update->collision_mask != 0)
			continue;

		dev_dbg(fb_data->dev, "A collision update is ready to go!\n");
		/*
		 * We have a collision cleared, so select it
		 * and we will retry the update
		 */
		fb_data->cur_update = collision_update;
		list_del_init(&fb_data->cur_update->list);
		break;
	}

	/*
	 * If we didn't find a collision update ready to go,
	 * we try to grab one from the update queue
	 */
	if (fb_data->cur_update == NULL) {
		/* Is update list empty? */
		if (list_empty(&fb_data->upd_buf_queue)) {
			dev_dbg(fb_data->dev, "No pending updates.\n");

			/* No updates pending, so we are done */
			mutex_unlock(&fb_data->queue_mutex);
			return IRQ_HANDLED;
		} else {
			dev_dbg(fb_data->dev, "Found a pending update!\n");

			/* Process next item in update list */
			fb_data->cur_update =
			    list_entry(fb_data->upd_buf_queue.next,
				       struct update_data_list, list);
			list_del_init(&fb_data->cur_update->list);
		}
	}

	/* Use LUT selected above */
	fb_data->cur_update->lut_num = next_lut;

	/* Associate LUT with update markers */
	list_for_each_entry_safe(next_marker, temp,
		&fb_data->cur_update->update_desc->upd_marker_list, upd_list)
		next_marker->lut_num = fb_data->cur_update->lut_num;

	/* Mark LUT as containing new update */
	fb_data->lut_update_order[fb_data->cur_update->lut_num] =
		fb_data->cur_update->update_desc->update_order;

	/* Enable Collision and WB complete IRQs */
	epdc_working_buf_intr(true);
	epdc_lut_complete_intr(fb_data->rev, fb_data->cur_update->lut_num, true);

	/* Program EPDC update to process buffer */
	next_upd_region =
		&fb_data->cur_update->update_desc->upd_data.update_region;
	if (fb_data->cur_update->update_desc->upd_data.temp == TEMP_USE_AUTO) {
		temp_index = mxc_epdc_fb_get_temp_index(fb_data, display_temp_c);
	} else if (fb_data->cur_update->update_desc->upd_data.temp != TEMP_USE_AMBIENT) {
		temp_index = mxc_epdc_fb_get_temp_index(fb_data,
			fb_data->cur_update->update_desc->upd_data.temp);
	} else {
		temp_index = fb_data->temp_index;
	}
	epdc_set_temp(temp_index);
	epdc_set_update_addr(fb_data->cur_update->phys_addr +
				fb_data->cur_update->update_desc->epdc_offs);
	epdc_set_update_coord(next_upd_region->left, next_upd_region->top);
	epdc_set_update_dimensions(next_upd_region->width,
				   next_upd_region->height);
	if (fb_data->rev > EPDC_VERSION_2_0_0)
		epdc_set_update_stride(fb_data->cur_update->update_desc->epdc_stride);
	if (fb_data->wv_modes_update &&
		(fb_data->cur_update->update_desc->upd_data.waveform_mode
			== WAVEFORM_MODE_AUTO)) {
		epdc_set_update_waveform(&fb_data->wv_modes);
		fb_data->wv_modes_update = false;
	}

	epdc_submit_update(fb_data->cur_update->lut_num,
			   fb_data->cur_update->update_desc->upd_data.waveform_mode,
			   fb_data->cur_update->update_desc->upd_data.update_mode,
			   false, fb_data->cur_update->update_desc->wb_pause,
			   false, 0);

	/* Release buffer queues */
	mutex_unlock(&fb_data->queue_mutex);

	return IRQ_HANDLED;
}

static int draw_mode0(struct mxc_epdc_fb_data *fb_data)
{
	u32 *upd_buf_ptr;
	int i;
	struct fb_var_screeninfo *screeninfo = &fb_data->epdc_fb_var;
	u32 xres, yres;

	upd_buf_ptr = (u32 *)fb_data->info.screen_base;

	epdc_upd_done_intr(false);
	epdc_working_buf_intr(true);
	epdc_lut_complete_intr(fb_data->rev, 0, true);

	/* Use unrotated (native) width/height */
	if ((screeninfo->rotate == FB_ROTATE_CW) ||
		(screeninfo->rotate == FB_ROTATE_CCW)) {
		xres = screeninfo->yres;
		yres = screeninfo->xres;
	} else {
		xres = screeninfo->xres;
		yres = screeninfo->yres;
	}

	/* Program EPDC update to process buffer */
	epdc_set_update_addr(fb_data->phys_start);
	epdc_set_update_coord(0, 0);
	epdc_set_update_dimensions(xres, yres);
	if (fb_data->rev > EPDC_VERSION_2_0_0)
		epdc_set_update_stride(0);
	epdc_submit_update(0, fb_data->wv_modes.mode_init, UPDATE_MODE_FULL,
		false, false, true, 0x00);

	dev_dbg(fb_data->dev, "Mode0 update - Waiting for LUT to complete...\n");

	/* Will timeout after ~4-5 seconds */

	for (i = 0; i < 40; i++) {
		if (!epdc_is_lut_active(0)) {
			dev_dbg(fb_data->dev, "Mode0 init complete\n");
			return 0;
		}
		msleep(100);
	}

	dev_err(fb_data->dev, "Mode0 init failed!\n");

	return -EHWFAULT;
}

static int do_epdc_init_sequence(struct mxc_epdc_fb_data *fb_data)
{
	int ret = 0;
	u32 xres, yres;
	struct mxcfb_update_data update;
	struct mxcfb_update_marker_data upd_marker_data;
	struct fb_var_screeninfo *screeninfo = &fb_data->epdc_fb_var;

	clk_enable(fb_data->epdc_clk_axi);
	clk_enable(fb_data->epdc_clk_pix);

	ret = epdc_init_sequence(fb_data);
	if (ret < 0)
	{
		clk_disable(fb_data->epdc_clk_pix);
		clk_disable(fb_data->epdc_clk_axi);
		return ret;
	}

	/* Disable clocks */
	clk_disable(fb_data->epdc_clk_pix);
	clk_disable(fb_data->epdc_clk_axi);

	fb_data->hw_ready = true;
	fb_data->hw_initializing = false;

	/* Use unrotated (native) width/height */
	if ((screeninfo->rotate == FB_ROTATE_CW) ||
			(screeninfo->rotate == FB_ROTATE_CCW)) {
		xres = screeninfo->yres;
		yres = screeninfo->xres;
	} else {
		xres = screeninfo->xres;
		yres = screeninfo->yres;
	}

	update.update_region.left = 0;
	update.update_region.width = xres;
	update.update_region.top = 0;
	update.update_region.height = yres;
	update.update_mode = UPDATE_MODE_FULL;
	update.waveform_mode = WAVEFORM_MODE_AUTO;
	update.update_marker = INIT_UPDATE_MARKER;
	update.temp = TEMP_USE_AUTO;
	update.flags = 0;

	upd_marker_data.update_marker = update.update_marker;

	mxc_epdc_fb_send_update(&update, &fb_data->info);
	/* Block on initial update */
	ret = mxc_epdc_fb_wait_update_complete(update.update_marker,
			&fb_data->info);
	if (ret < 0)
		dev_err(fb_data->dev,
				"Wait for initial update complete failed."
				" Error = 0x%x", ret);

	return ret;
}

static int mxc_epdc_fb_process_fw(struct mxc_epdc_fb_data *fb_data, const struct firmware *fw)
{
	int ret = 0;
	struct mxcfb_waveform_data_file *wv_file;
	int wv_data_offs;
	int i;
	struct clk *epdc_parent;
	unsigned long rounded_parent_rate, epdc_pix_rate,
			rounded_pix_clk, target_pix_clk;
	size_t temp_wfm_buf_len = 0;
	unsigned char *temp_wfm_buf;

	temp_wfm_buf = vmalloc(WFM_TEMP_BUF_LEN);
	if (temp_wfm_buf == NULL) {
		dev_err(fb_data->dev, "Can't allocate mem for temp buffer!\n");
		ret = -ENOMEM;
		goto error;
	}

	ret = gunzip(temp_wfm_buf, WFM_TEMP_BUF_LEN, &temp_wfm_buf_len, fw->data, fw->size);
	if (ret < 0) {
		dev_err(fb_data->dev, "Can't unzip waveform!\n");
		goto error;
	}

	wv_file = (struct mxcfb_waveform_data_file *)temp_wfm_buf;

	fb_data->temp_range_bounds = NULL;
	fb_data->waveform_buffer_virt = NULL;
	fb_data->wv_header = NULL;

	/* Get size and allocate temperature range table */
	fb_data->trt_entries = wv_file->wdh.trc + 1;
	fb_data->temp_range_bounds = kzalloc(fb_data->trt_entries, GFP_KERNEL);
	if (fb_data->temp_range_bounds == NULL) {
		dev_err(fb_data->dev, "Can't allocate mem for temp ranges!\n");
		ret = -ENOMEM;
		goto error;
	}

	for (i = 0; i < fb_data->trt_entries; i++)
		dev_dbg(fb_data->dev, "trt entry #%d = 0x%x\n", i, *((u8 *)&wv_file->data + i));

	/* Copy TRT data */
	memcpy(fb_data->temp_range_bounds, &wv_file->data, fb_data->trt_entries);

	/* Set default temperature index using TRT and room temp */
	fb_data->temp_index = mxc_epdc_fb_get_temp_index(fb_data, DEFAULT_TEMP);

	/* Get offset and size for waveform data */
	wv_data_offs = sizeof(wv_file->wdh) + fb_data->trt_entries + 1;
	fb_data->waveform_buffer_size = temp_wfm_buf_len - wv_data_offs;

	/* Allocate memory for waveform data */
	fb_data->waveform_buffer_virt = dma_alloc_coherent(fb_data->dev,
						fb_data->waveform_buffer_size,
						&fb_data->waveform_buffer_phys,
						GFP_DMA);
	if (fb_data->waveform_buffer_virt == NULL) {
		dev_err(fb_data->dev, "Can't allocate mem for waveform!\n");
		ret = -ENOMEM;
		goto error;
	}

	memcpy(fb_data->waveform_buffer_virt, (u8 *)(temp_wfm_buf) + wv_data_offs,
		fb_data->waveform_buffer_size);

	/* Read field to determine if 4-bit or 5-bit mode */
	if ((wv_file->wdh.luts & 0xC) == 0x4) {
		fb_data->buf_pix_fmt = EPDC_FORMAT_BUF_PIXEL_FORMAT_P5N;
		fb_data->waveform_type |= WAVEFORM_TYPE_5BIT;
	} else {
		fb_data->buf_pix_fmt = EPDC_FORMAT_BUF_PIXEL_FORMAT_P4N;
		fb_data->waveform_type |= WAVEFORM_TYPE_4BIT;
	}

#if defined(CONFIG_LAB126)
	fb_data->wv_header = kmalloc(wv_data_offs, GFP_KERNEL);
	if (fb_data->wv_header == NULL) {
		dev_err(fb_data->dev, "Can't allocate mem for waveform header!\n");
		ret = -ENOMEM;
		goto error;
	}
	fb_data->wv_header_size = wv_data_offs;
	memcpy(fb_data->wv_header, temp_wfm_buf, wv_data_offs);
#endif

	// 2nd gen waveform firmware file */
	fb_data->waveform_vcd_buffer = NULL;
	eink_get_wfm_voltage_control(fb_data->wv_header, (u8*)fb_data->waveform_buffer_virt);

	/* Enable clocks to access EPDC regs */
	clk_enable(fb_data->epdc_clk_axi);

	target_pix_clk = fb_data->cur_mode->vmode->pixclock;
	/* Enable pix clk for EPDC */
	clk_enable(fb_data->epdc_clk_pix);
	rounded_pix_clk = clk_round_rate(fb_data->epdc_clk_pix, target_pix_clk);

	if (((rounded_pix_clk >= target_pix_clk + target_pix_clk/100) ||
		(rounded_pix_clk <= target_pix_clk - target_pix_clk/100))) {
		/* Can't get close enough without changing parent clk */
		epdc_parent = clk_get_parent(fb_data->epdc_clk_pix);
		rounded_parent_rate = clk_round_rate(epdc_parent, target_pix_clk);

		epdc_pix_rate = target_pix_clk;
		while (epdc_pix_rate < rounded_parent_rate)
			epdc_pix_rate *= 2;
		clk_set_rate(epdc_parent, epdc_pix_rate);

		rounded_pix_clk = clk_round_rate(fb_data->epdc_clk_pix, target_pix_clk);
		if (((rounded_pix_clk >= target_pix_clk + target_pix_clk/100) ||
			(rounded_pix_clk <= target_pix_clk - target_pix_clk/100)))
			/* Still can't get a good clock, provide warning */
			dev_err(fb_data->dev, "Unable to get an accurate EPDC pix clk"
				"desired = %lu, actual = %lu\n", target_pix_clk,
				rounded_pix_clk);
	}

	clk_set_rate(fb_data->epdc_clk_pix, rounded_pix_clk);

	/* Disable clocks */
	clk_disable(fb_data->epdc_clk_pix);
	clk_disable(fb_data->epdc_clk_axi);

	ret = 0;
	goto cleanup;

error:
	if (fb_data->temp_range_bounds) {
		kfree(fb_data->temp_range_bounds);
		fb_data->temp_range_bounds = NULL;
	}

	if (fb_data->waveform_buffer_virt) {
		dma_free_writecombine(fb_data->dev,
		                      fb_data->waveform_buffer_size,
		                      fb_data->waveform_buffer_virt,
		                      fb_data->waveform_buffer_phys);
		fb_data->waveform_buffer_virt = NULL;
	}

	if (fb_data->wv_header) {
		kfree(fb_data->wv_header);
		fb_data->wv_header = NULL;
	}

cleanup:
	release_firmware(fw);

	if (temp_wfm_buf) {
		vfree(temp_wfm_buf);
		temp_wfm_buf = NULL;
	}

	return ret;
}

static int mxc_epdc_fb_init_hw(struct fb_info *info)
{
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;
	const struct firmware *fw;
	int ret;

	/*
	 * Create fw search string based on ID string in selected videomode.
	 * Format is "imx/epdc_[panel string].fw"
	 */
	if (fb_data->cur_mode) {
		char *prefix  = "imx/epdc_";
		char *postfix = ".fw";
		strcpy(fb_data->fw_str, prefix);
		strncat(fb_data->fw_str,
		        fb_data->cur_mode->vmode->name,
		        FW_STR_LEN - strlen(prefix) - strlen(postfix) - 1);
		strcat(fb_data->fw_str, postfix);
	} else {
		strcpy(fb_data->fw_str, "imx/epdc.fw");
	}

	wfm_using_builtin = false;
	if ((ret = request_firmware(&fw, fb_data->fw_str, fb_data->dev)) == 0 &&
	    (ret = mxc_epdc_fb_process_fw(fb_data, fw)) == 0)
		return 0;

	/*
	 * If there was a hardware fault, don't attempt to load the fallback
	 * waveform (it won't help). Fail to probe the driver and let the caller
	 * handle the error.
	 */
	if (ret == -EHWFAULT)
		return ret;

	wfm_using_builtin = true;
	dev_err(fb_data->dev, "Failed to load fw! (err %d) Trying fallback fw\n", ret);
	ret = request_firmware(&fw, "imx/default.fw.gz", fb_data->dev);
	if (ret) {
		dev_err(fb_data->dev, "Failed to load default fw!\n");
		return ret;
	}
	return mxc_epdc_fb_process_fw(fb_data, fw);
}

static ssize_t store_update(struct device *device,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct mxcfb_update_data update;
	struct fb_info *info = dev_get_drvdata(device);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	if (strncmp(buf, "direct", 6) == 0)
		update.waveform_mode = fb_data->wv_modes.mode_du;
	else if (strncmp(buf, "gc16", 4) == 0)
		update.waveform_mode = fb_data->wv_modes.mode_gc16;
	else if (strncmp(buf, "gc4", 3) == 0)
		update.waveform_mode = fb_data->wv_modes.mode_gc4;

	/* Now, request full screen update */
	update.update_region.left = 0;
	update.update_region.width = fb_data->epdc_fb_var.xres;
	update.update_region.top = 0;
	update.update_region.height = fb_data->epdc_fb_var.yres;
	update.update_mode = UPDATE_MODE_FULL;
	update.temp = TEMP_USE_AUTO;
	update.update_marker = 0;
	update.flags = 0;

	mxc_epdc_fb_send_update(&update, info);

	return count;
}

static struct device_attribute fb_attrs[] = {
	__ATTR(update, S_IRUGO|S_IWUSR, NULL, store_update),
};

static ssize_t mxc_epdc_pwrdown_show(struct device *dev, struct device_attribute *attr,
                                     char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	return sprintf(buf, "%d\n", fb_data->pwrdown_delay);
}

static ssize_t mxc_epdc_pwrdown_store(struct device *dev, struct device_attribute *attr,
                                      const char *buf, size_t size)
{
	int value = 0;
	struct fb_info *info = dev_get_drvdata(dev);

	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Error in epdc power state value\n");
		return -EINVAL;
	}

	mxc_epdc_fb_set_pwrdown_delay(value, info);

	return size;
}
static DEVICE_ATTR(mxc_epdc_pwrdown, 0666, mxc_epdc_pwrdown_show, mxc_epdc_pwrdown_store);

static ssize_t mxc_epdc_temperature_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	return sprintf(buf, "%d\n", fb_data->temp_override);
}

static ssize_t mxc_epdc_temperature_store(struct device *dev, struct device_attribute *attr,
		                                      const char *buf, size_t size)
{
	int value = TEMP_USE_AUTO;
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Error in epdc temperature value\n");
		return -EINVAL;
	}

	fb_data->temp_override = value;
	return size;
}
static DEVICE_ATTR(mxc_epdc_temperature, 0666, mxc_epdc_temperature_show, mxc_epdc_temperature_store);

static ssize_t mxc_epdc_voltcontrol_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;
	struct epd_vc_data_volts vcdv;
	int waveform_idx;
	int temp_idx;

	if (!fb_data->waveform_vcd_buffer)
	{
		printk(KERN_ERR "No voltage control information\n");
		return 0;
	}

	waveform_idx = get_waveform_by_type(fb_data, fb_data->vc_waveform_sys);
	if (waveform_idx == WAVEFORM_MODE_AUTO)
		waveform_idx = fb_data->wv_modes.mode_init;
	temp_idx = mxc_epdc_fb_get_temp_index(fb_data, fb_data->vc_temperature_sys);

	printk(KERN_ERR "Fetching voltage control for waveform : %d (idx: %d) and temperature : %d (idx: %d)\n",
			fb_data->vc_waveform_sys, waveform_idx, fb_data->vc_temperature_sys, temp_idx);
	fetch_epdc_pmic_voltages(&vcdv, fb_data, waveform_idx, temp_idx);

	return sprintf(buf, "VCOM_BIT_0x19: %d\nVoltage control:\n\
			VPOS: %+duV\n\
			VNEG: %+duV\n\
			VDDH: %+duV\n\
			VEE : %+duV\n\
			VCOM_OFFSET: %+duV\n", fb_data->wv_header->vcom_shifted, vcdv.vpos_v, vcdv.vneg_v, vcdv.vddh_v, vcdv.vee_v, vcdv.vcom_v);
}

static ssize_t mxc_epdc_voltcontrol_store(struct device *dev, struct device_attribute *attr,
		                                          const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;
	int waveform;
	int temperature;

	if (!fb_data->waveform_vcd_buffer)
	{
		printk(KERN_ERR "No voltage control information\n");
		return 0;
	}
	if (sscanf(buf, "%d %d", &waveform, &temperature) <= 0) {
		printk(KERN_ERR "Error in waveform / temperature value\n");
		return -EINVAL;
	}
	fb_data->vc_waveform_sys = waveform;
	fb_data->vc_temperature_sys = temperature;
	printk(KERN_ERR "Setting waveform to %d and temp to %d\n", waveform, temperature);
	return size;
}

static DEVICE_ATTR(mxc_epdc_voltcontrol, 0666, mxc_epdc_voltcontrol_show, mxc_epdc_voltcontrol_store);

#ifdef CONFIG_FB_MXC_EINK_REAGL
static ssize_t mxc_epdc_reagl_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	return sprintf(buf, "%d\n", fb_data->which_reagl);
}

static ssize_t mxc_epdc_reagl_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int value;
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Error setting reagl algo\n");
		return -EINVAL;
	}

	if (value >= MAX_REAGL_ALGOS) {
		printk(KERN_ERR "Max value is %d\n", MAX_REAGL_ALGOS - 1);
		return -EINVAL;
	}

	fb_data->which_reagl = value;
	return size;
}
static DEVICE_ATTR(mxc_epdc_reagl, 0666, mxc_epdc_reagl_show, mxc_epdc_reagl_store);
#endif // CONFIG_FB_MXC_EINK_REAGL

static ssize_t mxc_epdc_wvaddr_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	clk_enable(fb_data->epdc_clk_axi);
	sprintf(buf,"0x%08X", __raw_readl(EPDC_WVADDR));
	clk_disable(fb_data->epdc_clk_axi);
	return strlen(buf);
}
static DEVICE_ATTR(mxc_epdc_wvaddr, 0666, mxc_epdc_wvaddr_show, NULL);

static ssize_t mxc_epdc_regs_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	clk_enable(fb_data->epdc_clk_axi);
	dump_epdc_reg();
	clk_disable(fb_data->epdc_clk_axi);
	return 0;
}
static DEVICE_ATTR(mxc_epdc_regs, 0666, mxc_epdc_regs_show, NULL);

static ssize_t mxc_epdc_powerup_show(struct device *dev, struct device_attribute *attr,
                                     char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	return sprintf(buf, "%d\n", fb_data->power_state);
}

static ssize_t mxc_epdc_powerup_store(struct device *dev, struct device_attribute *attr,
                                      const char *buf, size_t size)
{
	int value = 0;
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Error in epdc power state value\n");
		return -EINVAL;
	}

	if (value == 0 && (fb_data->power_state != POWER_STATE_OFF)) {
		mutex_lock(&fb_data->power_mutex);
		epdc_force_powerdown(fb_data);
		mutex_unlock(&fb_data->power_mutex);
	} else if (value && (fb_data->power_state == POWER_STATE_OFF)) {
		mutex_lock(&fb_data->power_mutex);

		if (!epdc_powerup(fb_data))
		{
			dev_err(fb_data->dev, "EPDC powerup error\n");
			mutex_unlock(&fb_data->power_mutex);
			return size;
		}
		mutex_unlock(&fb_data->power_mutex);
		if (!epdc_powerup_wait_for_enabled(fb_data)) {
			dev_err(fb_data->dev, "EPDC powerup timeout/error.\n");
			return size;
		}
		mutex_lock(&fb_data->power_mutex);
		epdc_powerup_vcom(fb_data, WAVEFORM_MODE_INIT, fb_data->temp_index);
		mutex_unlock(&fb_data->power_mutex);
	}

	return size;
}
static DEVICE_ATTR(mxc_epdc_powerup, 0666, mxc_epdc_powerup_show, mxc_epdc_powerup_store);

static ssize_t mxc_epdc_update_store(struct device *device, struct device_attribute *attr,
                                     const char *buf, size_t count)
{
	struct mxcfb_update_data update;
	struct fb_info *info = dev_get_drvdata(device);

	char wf_buf[64];
	int top, left, width, height, mode;
	int params = 0;

	params = sscanf(buf, "%63s %d %d %d %d %d", wf_buf, &mode, &top, &left, &width, &height);
	if (params <= 0)
		return -EINVAL;

	update.flags = 0;

	if (strncmp(wf_buf, "auto", 4) == 0)
		update.waveform_mode = WAVEFORM_MODE_AUTO;
	else if (strncmp(wf_buf, "au", 2) == 0)
		update.waveform_mode = WAVEFORM_MODE_A2;
	else if (strncmp(wf_buf, "du4", 3) == 0)
		update.waveform_mode = WAVEFORM_MODE_DU4;
	else if (strncmp(wf_buf, "du", 2) == 0)
		update.waveform_mode = WAVEFORM_MODE_DU;
	else if (strncmp(wf_buf, "gc16f", 5) == 0)
		update.waveform_mode = WAVEFORM_MODE_GC16_FAST;
	else if (strncmp(wf_buf, "gc16", 4) == 0)
		update.waveform_mode = WAVEFORM_MODE_GC16;
	else if (strncmp(wf_buf, "gc4", 3) == 0)
		update.waveform_mode = WAVEFORM_MODE_GC4;
	else if (strncmp(wf_buf, "gl4", 3) == 0)
		update.waveform_mode = WAVEFORM_MODE_GL4;
	else if (strncmp(wf_buf, "gl16inv", 7) == 0)
		update.waveform_mode = WAVEFORM_MODE_GL16_INV;
	else if (strncmp(wf_buf, "gl16f", 5) == 0)
		update.waveform_mode = WAVEFORM_MODE_GL16_FAST;
	else if (strncmp(wf_buf, "gl16", 4) == 0)
		update.waveform_mode = WAVEFORM_MODE_GL16;
	else if (strncmp(wf_buf, "reagld", 6) == 0)
		update.waveform_mode = WAVEFORM_MODE_REAGLD;
	else if (strncmp(wf_buf, "reagl", 5) == 0)
		update.waveform_mode = WAVEFORM_MODE_REAGL;
	else
		update.waveform_mode = WAVEFORM_MODE_AUTO;

	update.update_mode          = (params >= 2) ? mode   : UPDATE_MODE_FULL;
	update.update_region.top    = (params >= 3) ? top    : 0;
	update.update_region.left   = (params >= 4) ? left   : 0;
	update.update_region.width  = (params >= 5) ? width  : (info->var.xres - update.update_region.left);
	update.update_region.height = (params >= 6) ? height : (info->var.yres - update.update_region.top);

	update.hist_bw_waveform_mode = 0;
	update.hist_gray_waveform_mode = 0;
	update.temp = TEMP_USE_AUTO;
	update.update_marker = 1;

	mxc_epdc_fb_send_update(&update, info);

	return count;
}

static ssize_t mxc_epdc_waveform_modes_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct mxc_epdc_fb_data *fb_data = (struct mxc_epdc_fb_data *)info;

	return sprintf(buf, "mode_version:0x%x\n\
			init:0x%x\n\
			du:0x%x\n\
			du4:0x%x\n\
			gc16f:0x%x\n\
			gc16:0x%x\n\
			gc4:0x%x\n\
			gl4:0x%x\n\
			gl16inv:0x%x\n\
			gl16f:0x%x\n\
			gl16:0x%x\n\
			reagld:0x%x\n\
			reagl:0x%x\n",
			fb_data->wv_header->mode_version,
			fb_data->wv_modes.mode_init,
			fb_data->wv_modes.mode_du,
			fb_data->wv_modes.mode_du4,
			fb_data->wv_modes.mode_gc16_fast,
			fb_data->wv_modes.mode_gc16,
			fb_data->wv_modes.mode_gc4,
			fb_data->wv_modes.mode_gl4,
			fb_data->wv_modes.mode_gl16_inv,
			fb_data->wv_modes.mode_gl16_fast,
			fb_data->wv_modes.mode_gl16,
			fb_data->wv_modes.mode_reagld,
			fb_data->wv_modes.mode_reagl
			);
}
static DEVICE_ATTR(mxc_epdc_waveform_modes, 0666, mxc_epdc_waveform_modes_show, NULL);

static ssize_t mxc_epdc_update_show(struct device *dev, struct device_attribute *attr,
                                    char *buf)
{
	return sprintf(buf, "1\n");
}
static DEVICE_ATTR(mxc_epdc_update, 0666, mxc_epdc_update_show, mxc_epdc_update_store);

static ssize_t mxc_epdc_debug_show(struct device *dev, struct device_attribute *attr,
                                   char *buf)
{
	return sprintf(buf, "%d\n", mxc_epdc_debugging);
}

static ssize_t mxc_epdc_debug_store(struct device *dev, struct device_attribute *attr,
                                    const char *buf, size_t size)
{
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Error in epdc debug value\n");
		return -EINVAL;
	}

	mxc_epdc_debugging = value;

	return size;
}

static DEVICE_ATTR(mxc_epdc_debug, 0666, mxc_epdc_debug_show, mxc_epdc_debug_store);

int __devinit mxc_epdc_fb_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct mxc_epdc_fb_data *fb_data;
	struct resource *res;
	struct fb_info *info;
	char *options, *opt;
	char name[] = "mxcepdcfb";
	struct fb_videomode *vmode;
	int xres_virt, yres_virt, buf_size;
	int xres_virt_rot, yres_virt_rot, pix_size_rot;
	struct fb_var_screeninfo *var_info;
	struct fb_fix_screeninfo *fix_info;
	struct pxp_config_data *pxp_conf;
	struct pxp_proc_data *proc_data;
	struct scatterlist *sg;
	struct update_data_list *upd_list;
	struct update_data_list *plist, *temp_list;
	int i;
	unsigned long x_mem_size = 0;
	u32 val;

	fb_data = (struct mxc_epdc_fb_data *)framebuffer_alloc(
			sizeof(struct mxc_epdc_fb_data), &pdev->dev);
	if (fb_data == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* Get platform data and check validity */
	fb_data->pdata = pdev->dev.platform_data;
	if ((fb_data->pdata == NULL) || (fb_data->pdata->num_modes < 1)
		|| (fb_data->pdata->epdc_mode == NULL)
		|| (fb_data->pdata->epdc_mode->vmode == NULL)) {
		ret = -EINVAL;
		goto out_fbdata;
	}

	if (fb_get_options(name, &options)) {
		ret = -ENODEV;
		goto out_fbdata;
	}

	fb_data->tce_prevent = 0;

	if (options)
		while ((opt = strsep(&options, ",")) != NULL) {
			if (!*opt)
				continue;

			if (!strncmp(opt, "bpp=", 4))
				fb_data->default_bpp =
					simple_strtoul(opt + 4, NULL, 0);
			else if (!strncmp(opt, "x_mem=", 6))
				x_mem_size = memparse(opt + 6, NULL);
			else if (!strncmp(opt, "tce_prevent", 11))
				fb_data->tce_prevent = 1;
		}

	fb_data->dev = &pdev->dev;

	if (!fb_data->default_bpp)
		fb_data->default_bpp = default_bpp;

	mxc_epdc_panel_init(fb_data);

	fb_data->cur_mode = panel_choose_fbmode(fb_data);
	if (!fb_data->cur_mode) {
		dev_err(fb_data->dev, "Failed to match requested video mode\n");
		ret = -EINVAL;
		goto out_panel_close;
	}
	vmode = fb_data->cur_mode->vmode;

	platform_set_drvdata(pdev, fb_data);
	info = &fb_data->info;

	/* Allocate color map for the FB */
	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto out_drv_data;

	dev_dbg(&pdev->dev, "resolution %dx%d, bpp %d\n",
		vmode->xres, vmode->yres, fb_data->default_bpp);

	/*
	 * GPU alignment restrictions dictate framebuffer parameters:
	 * - 32-byte alignment for buffer width
	 * - 128-byte alignment for buffer height
	 * => 4K buffer alignment for buffer start
	 */
	xres_virt = ALIGN(vmode->xres, 32);
	yres_virt = ALIGN(vmode->yres, 128);
	fb_data->max_pix_size = PAGE_ALIGN(xres_virt * yres_virt);

	/*
	 * Have to check to see if aligned buffer size when rotated
	 * is bigger than when not rotated, and use the max
	 */
	xres_virt_rot = ALIGN(vmode->yres, 32);
	yres_virt_rot = ALIGN(vmode->xres, 128);
	pix_size_rot = PAGE_ALIGN(xres_virt_rot * yres_virt_rot);
	fb_data->max_pix_size = (fb_data->max_pix_size > pix_size_rot) ?
				fb_data->max_pix_size : pix_size_rot;

	buf_size = fb_data->max_pix_size * fb_data->default_bpp/8;

#ifdef CONFIG_LAB126
	// Dynamically set x_mem_size based on available physical memory and resolution
	if (num_physpages > 0)
	{
		int i;
		unsigned long ddr_size;

		// Convert to MB
		ddr_size = (num_physpages >> (20 - PAGE_SHIFT));
		dev_dbg(&pdev->dev, "Kernel memory reported %ld\n", ddr_size);
		for (i = 0; i < ARRAY_SIZE(resolution_memory_map); i++)
		{
			// Search for a match
			if (ddr_size == resolution_memory_map[i].total_system_mem_mb &&
					vmode->xres == resolution_memory_map[i].x &&
					vmode->yres == resolution_memory_map[i].y)
			{
				// Convert to bytes
				x_mem_size = (unsigned long)resolution_memory_map[i].fb_mem_mb << 20;
				dev_dbg(&pdev->dev, "Memory override detected TotalMem:%ldM X:%d Y:%d FBMem:%dM\n", ddr_size, vmode->xres, vmode->yres, resolution_memory_map[i].fb_mem_mb);
				break;
			}
		}
	}
#endif

	/* Compute the number of screens needed based on X memory requested */
	if (x_mem_size > 0) {
		fb_data->num_screens = DIV_ROUND_UP(x_mem_size, buf_size);
		if (fb_data->num_screens < NUM_SCREENS_MIN)
			fb_data->num_screens = NUM_SCREENS_MIN;
		else if (buf_size * fb_data->num_screens > SZ_16M)
			fb_data->num_screens = SZ_16M / buf_size;
	} else {
		fb_data->num_screens = NUM_SCREENS_MIN;
	}

	dev_dbg(&pdev->dev, "Allocating memory for %d screens\n", fb_data->num_screens);
	fb_data->map_size = buf_size * fb_data->num_screens;
	dev_dbg(&pdev->dev, "memory to allocate: %d\n", fb_data->map_size);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		ret = -ENODEV;
		goto out_cmap;
	}

	epdc_base = ioremap(res->start, SZ_4K);
	if (epdc_base == NULL) {
		ret = -ENOMEM;
		goto out_cmap;
	}

	/* Allocate FB memory */
	info->screen_base = dma_alloc_writecombine(&pdev->dev,
						  fb_data->map_size,
						  &fb_data->phys_start,
						  GFP_DMA);

	if (info->screen_base == NULL) {
		ret = -ENOMEM;
		goto out_mapregs;
	}
	dev_dbg(&pdev->dev, "allocated at %p:0x%x\n", info->screen_base,
		fb_data->phys_start);

#if defined(FIX_CURRENT_RESAMPLE)
	fb_data->virt_start = info->screen_base;
#endif
	var_info = &info->var;
	var_info->activate = FB_ACTIVATE_TEST;
	var_info->bits_per_pixel = fb_data->default_bpp;
	var_info->xres = vmode->xres;
	var_info->yres = vmode->yres;
	var_info->xres_virtual = xres_virt;
	/* Additional screens allow for panning  and buffer flipping */
	var_info->yres_virtual = yres_virt * fb_data->num_screens;

	var_info->pixclock = vmode->pixclock;
	var_info->left_margin = vmode->left_margin;
	var_info->right_margin = vmode->right_margin;
	var_info->upper_margin = vmode->upper_margin;
	var_info->lower_margin = vmode->lower_margin;
	var_info->hsync_len = vmode->hsync_len;
	var_info->vsync_len = vmode->vsync_len;
	var_info->vmode = vmode->vmode;
	var_info->width = fb_data->cur_mode->physical_width;
	var_info->height = fb_data->cur_mode->physical_height;

	switch (fb_data->default_bpp) {
	case 32:
	case 24:
		var_info->red.offset = 16;
		var_info->red.length = 8;
		var_info->green.offset = 8;
		var_info->green.length = 8;
		var_info->blue.offset = 0;
		var_info->blue.length = 8;
		break;

	case 16:
		var_info->red.offset = 11;
		var_info->red.length = 5;
		var_info->green.offset = 5;
		var_info->green.length = 6;
		var_info->blue.offset = 0;
		var_info->blue.length = 5;
		break;

	case 8:
		/*
		 * For 8-bit grayscale, R, G, and B offset are equal.
		 *
		 */
		var_info->grayscale = GRAYSCALE_8BIT;

		var_info->red.length = 8;
		var_info->red.offset = 0;
		var_info->red.msb_right = 0;
		var_info->green.length = 8;
		var_info->green.offset = 0;
		var_info->green.msb_right = 0;
		var_info->blue.length = 8;
		var_info->blue.offset = 0;
		var_info->blue.msb_right = 0;
		break;

	default:
		dev_err(&pdev->dev, "unsupported bitwidth %d\n",
			fb_data->default_bpp);
		ret = -EINVAL;
		goto out_dma_fb;
	}

	fix_info = &info->fix;

	strcpy(fix_info->id, "mxc_epdc_fb");
	fix_info->type = FB_TYPE_PACKED_PIXELS;
	fix_info->visual = FB_VISUAL_TRUECOLOR;
	fix_info->xpanstep = 0;
	fix_info->ypanstep = 0;
	fix_info->ywrapstep = 0;
	fix_info->accel = FB_ACCEL_NONE;
	fix_info->smem_start = fb_data->phys_start;
	fix_info->smem_len = fb_data->map_size;
	fix_info->ypanstep = 0;

	fb_data->native_width = vmode->xres;
	fb_data->native_height = vmode->yres;
	fb_data->physical_width = fb_data->cur_mode->physical_width;
	fb_data->physical_height = fb_data->cur_mode->physical_height;

	info->fbops = &mxc_epdc_fb_ops;
	info->var.activate = FB_ACTIVATE_NOW;
	info->pseudo_palette = fb_data->pseudo_palette;
	info->screen_size = info->fix.smem_len;
	info->flags = FBINFO_FLAG_DEFAULT;

	mxc_epdc_fb_set_fix(info);

	fb_data->auto_mode = AUTO_UPDATE_MODE_REGION_MODE;
	fb_data->upd_scheme = UPDATE_SCHEME_QUEUE_AND_MERGE;

	/* Initialize our internal copy of the screeninfo */
	fb_data->epdc_fb_var = *var_info;
	fb_data->fb_offset = 0;
	fb_data->eof_sync_period = 0;

	fb_data->epdc_clk_axi = clk_get(fb_data->dev, "epdc_axi");
	if (IS_ERR(fb_data->epdc_clk_axi)) {
		dev_err(&pdev->dev, "Unable to get EPDC AXI clk."
			"err = 0x%x\n", (int)fb_data->epdc_clk_axi);
		ret = -ENODEV;
		goto out_dma_fb;
	}
	fb_data->epdc_clk_pix = clk_get(fb_data->dev, "epdc_pix");
	if (IS_ERR(fb_data->epdc_clk_pix)) {
		dev_err(&pdev->dev, "Unable to get EPDC pix clk."
			"err = 0x%x\n", (int)fb_data->epdc_clk_pix);
		ret = -ENODEV;
		goto out_dma_fb;
	}

	clk_enable(fb_data->epdc_clk_axi);
	val = __raw_readl(EPDC_VERSION);
	clk_disable(fb_data->epdc_clk_axi);
	fb_data->rev = val;
	dev_dbg(&pdev->dev, "EPDC version = %d.%d.%d\n",
	        EPDC_VERSION_GET_MAJOR(fb_data->rev),
	        EPDC_VERSION_GET_MINOR(fb_data->rev),
	        EPDC_VERSION_GET_STEP(fb_data->rev));

	if (fb_data->rev < EPDC_VERSION_2_0_0) {
		fb_data->num_luts = EPDC_V1_NUM_LUTS;
		fb_data->max_num_updates = EPDC_V1_MAX_NUM_UPDATES;
	} else {
		fb_data->num_luts = EPDC_V2_NUM_LUTS;
		fb_data->max_num_updates = EPDC_V2_MAX_NUM_UPDATES;
		if (vmode->xres > EPDC_V2_MAX_UPDATE_WIDTH)
			fb_data->restrict_width = true;
	}
	fb_data->max_num_buffers = EPDC_MAX_NUM_BUFFERS;

	/*
	 * Initialize lists for pending updates,
	 * active update requests, update collisions,
	 * and freely available updates.
	 */
	INIT_LIST_HEAD(&fb_data->upd_pending_list);
	INIT_LIST_HEAD(&fb_data->upd_buf_queue);
	INIT_LIST_HEAD(&fb_data->upd_buf_free_list);
	INIT_LIST_HEAD(&fb_data->upd_buf_collision_list);

	/* Allocate update buffers and add them to the list */
	for (i = 0; i < fb_data->max_num_updates; i++) {
		upd_list = kzalloc(sizeof(*upd_list), GFP_KERNEL);
		if (upd_list == NULL) {
			ret = -ENOMEM;
			goto out_upd_lists;
		}

		/* Add newly allocated buffer to free list */
		list_add(&upd_list->list, &fb_data->upd_buf_free_list);
	}

	fb_data->virt_addr_updbuf =
		kzalloc(sizeof(void *) * fb_data->max_num_buffers, GFP_KERNEL);
	fb_data->phys_addr_updbuf =
		kzalloc(sizeof(dma_addr_t) * fb_data->max_num_buffers,
			GFP_KERNEL);
	for (i = 0; i < fb_data->max_num_buffers; i++) {
		/*
		 * Allocate memory for PxP output buffer.
		 * Each update buffer is 1 byte per pixel, and can
		 * be as big as the full-screen frame buffer
		 */
		fb_data->virt_addr_updbuf[i] =
				kmalloc(fb_data->max_pix_size, GFP_KERNEL);
		fb_data->phys_addr_updbuf[i] =
				virt_to_phys(fb_data->virt_addr_updbuf[i]);

#ifdef CONFIG_FALCON
		falcon_add_preload_kernel_range((u32)fb_data->virt_addr_updbuf[i], fb_data->max_pix_size);
#endif
		if (fb_data->virt_addr_updbuf[i] == NULL) {
			ret = -ENOMEM;
			goto out_upd_buffers;
		}

		dev_dbg(fb_data->info.device, "allocated %d bytes @ 0x%08X\n",
			fb_data->max_pix_size, fb_data->phys_addr_updbuf[i]);
	}

	/* Counter indicating which update buffer should be used next. */
	fb_data->upd_buffer_num = 0;

	/*
	 * Allocate memory for PxP SW workaround buffer
	 * These buffers are used to hold copy of the update region,
	 * before sending it to PxP for processing.
	 */
#if !defined(CONFIG_LAB126)
	fb_data->virt_addr_copybuf =
	    dma_alloc_coherent(fb_data->info.device, fb_data->max_pix_size*2,
			       &fb_data->phys_addr_copybuf, GFP_DMA);
	if (fb_data->virt_addr_copybuf == NULL) {
		ret = -ENOMEM;
		goto out_upd_buffers;
	}
#endif

	fb_data->working_buffer_size = vmode->yres * vmode->xres * 2;
	/* Allocate EPDC working buffer A (main WB Processing buffer) */
#ifdef CONFIG_FB_MXC_EINK_WORK_BUFFER_RESERVED
	fb_data->working_buffer_A_phys = CONFIG_FB_MXC_EINK_WORK_BUFFER_ADDR;
	fb_data->working_buffer_A_virt = ioremap_cached(fb_data->working_buffer_A_phys, CONFIG_FB_MXC_EINK_WORK_BUFFER_SIZE);
	if(CONFIG_FB_MXC_EINK_WORK_BUFFER_SIZE < fb_data->working_buffer_size) {
		dev_err(&pdev->dev, "Reserved Working buffer is too small\n");
		ret = -ENOMEM;
		goto out_copybuffer;
	}
#else
	fb_data->working_buffer_A_virt =
		kmalloc(fb_data->working_buffer_size, GFP_KERNEL);
	fb_data->working_buffer_A_phys =
		virt_to_phys(fb_data->working_buffer_A_virt);
	if (fb_data->working_buffer_A_virt == NULL) {
		dev_err(&pdev->dev, "Can't allocate mem for working buf A!\n");
		ret = -ENOMEM;
 		goto out_copybuffer;
 	}
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_RESERVED

#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
	/* Allocate EPDC working buffer B (post-waveform algorithm) */
	fb_data->working_buffer_B_virt =
		kmalloc(fb_data->working_buffer_size, GFP_KERNEL);
	fb_data->working_buffer_B_phys =
		virt_to_phys(fb_data->working_buffer_B_virt);
	if (fb_data->working_buffer_B_virt == NULL) {
		dev_err(&pdev->dev, "Can't allocate mem for working buf B!\n");
		ret = -ENOMEM;
		goto out_dma_work_buf_A;
	}
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B

#ifdef CONFIG_FB_MXC_EINK_REAGL
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
	/* Initialize REAGL buffers. Only needed if REAGL-D is enabled */
	if (reagl_init(fb_data->native_width) < 0)
		goto out_dma_work_buf_B;
#endif

	reagl_algos[REAGL_ALGO_LAB126_FAST] = (struct reagl_algo) {
		.func       = do_reagl_processing_v_4_lab126,
		.buffer_in  = (uint16_t *)fb_data->working_buffer_A_virt,
		.buffer_out = (uint16_t *)fb_data->working_buffer_A_virt,
		.buffer_tce = fb_data->working_buffer_A_phys
	};
	reagl_algos[REAGL_ALGO_LAB126] = (struct reagl_algo) {
		.func       = do_reagl_processing_v_2_lab126,
		.buffer_in  = (uint16_t *)fb_data->working_buffer_A_virt,
		.buffer_out = (uint16_t *)fb_data->working_buffer_A_virt,
		.buffer_tce = fb_data->working_buffer_A_phys
	};
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
	reagl_algos[REAGL_ALGO_FREESCALE] = (struct reagl_algo) {
		.func       = do_reagl_processing_v2_2_1,
		.buffer_in  = (uint16_t *)fb_data->working_buffer_A_virt,
		.buffer_out = (uint16_t *)fb_data->working_buffer_B_virt,
		.buffer_tce = fb_data->working_buffer_B_phys
	};
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B
#endif // CONFIG_FB_MXC_EINK_REAGL

	/* Initialize EPDC pins */
	if (fb_data->pdata->get_pins)
		fb_data->pdata->get_pins();
	if(fb_data->pdata->disable_pins)
		fb_data->pdata->disable_pins();

	fb_data->in_init = false;

	fb_data->hw_ready = false;
	fb_data->hw_initializing = false;

	/*
	 * Set default waveform mode values.
	 * Should be overwritten via ioctl.
	 */

	fb_data->wv_modes.mode_init = WAVEFORM_MODE_INIT;
	fb_data->wv_modes.mode_du = WAVEFORM_MODE_DU;
	fb_data->wv_modes.mode_a2 = WAVEFORM_MODE_DU;
	fb_data->wv_modes.mode_gc4 = WAVEFORM_MODE_GC16;
	fb_data->wv_modes.mode_gc8 = WAVEFORM_MODE_GC16;
	fb_data->wv_modes.mode_gc16 = WAVEFORM_MODE_GC16;
	fb_data->wv_modes.mode_gc16_fast = WAVEFORM_MODE_GC16;
	fb_data->wv_modes.mode_gc32 = WAVEFORM_MODE_GC16;
	fb_data->wv_modes.mode_gl16 = WAVEFORM_MODE_GC16;
	fb_data->wv_modes.mode_gl16_fast = WAVEFORM_MODE_GC16;
	fb_data->wv_modes.mode_du4 = WAVEFORM_MODE_GC16;

	/*
	 * reagl_flow
	 */
	fb_data->wv_modes.mode_reagl = 3;

	fb_data->wv_modes_update = true;

	fb_data->temp_override = TEMP_USE_AUTO;

#ifdef CONFIG_FB_MXC_EINK_REAGL
	fb_data->which_reagl = REAGL_ALGO_LAB126_FAST;
#endif // CONFIG_FB_MXC_EINK_REAGL

	/* Initialize marker list */
	INIT_LIST_HEAD(&fb_data->full_marker_list);

	/* Initialize all LUTs to inactive */
	fb_data->lut_update_order =
		kzalloc(fb_data->num_luts * sizeof(u32 *), GFP_KERNEL);
	for (i = 0; i < fb_data->num_luts; i++)
		fb_data->lut_update_order[i] = 0;

	INIT_DELAYED_WORK(&fb_data->epdc_done_work, epdc_done_work_func);
	fb_data->epdc_submit_workqueue = alloc_workqueue("EPDC Submit",
					WQ_MEM_RECLAIM  |
					WQ_CPU_INTENSIVE | WQ_HIGHPRI, 1);
	INIT_WORK(&fb_data->epdc_submit_work, epdc_submit_work_func);

	/* Retrieve EPDC IRQ num */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot get IRQ resource\n");
		ret = -ENODEV;
		goto out_reagl;
	}
	fb_data->epdc_irq = res->start;

	/* Register IRQ handler */
	ret = request_threaded_irq(fb_data->epdc_irq, mxc_epdc_irq_quick_check_handler, mxc_epdc_irq_handler, 0,
			"fb_dma", fb_data);
	if (ret) {
		dev_err(&pdev->dev, "request_irq (%d) failed with error %d\n",
			fb_data->epdc_irq, ret);
		ret = -ENODEV;
		goto out_reagl;
	}

	info->fbdefio = &mxc_epdc_fb_defio;
#ifdef CONFIG_FB_MXC_EINK_AUTO_UPDATE_MODE
	fb_deferred_io_init(info);
#endif

	/* get pmic regulators */
	fb_data->display_regulator = regulator_get(NULL, "DISPLAY");
	if (IS_ERR(fb_data->display_regulator)) {
		dev_err(&pdev->dev, "Unable to get display PMIC regulator."
			"err = 0x%x\n", (int)fb_data->display_regulator);
		ret = -ENODEV;
		goto out_irq;
	}
	fb_data->vcom_regulator = regulator_get(NULL, "VCOM");
	if (IS_ERR(fb_data->vcom_regulator)) {
		regulator_put(fb_data->display_regulator);
		dev_err(&pdev->dev, "Unable to get VCom regulator."
			"err = 0x%x\n", (int)fb_data->vcom_regulator);
		ret = -ENODEV;
		goto out_regulator1;
	}

	init_completion(&fb_data->powerup_compl);

#if defined(CONFIG_LAB126)
	/* subscribe to pwrgood event */
	g_pmic_evt_pwrok.param = fb_data;
	g_pmic_evt_pwrok.func = pmic_pwrenable_cb;
	ret = pmic_event_subscribe(EVENT_EPD_POK, &g_pmic_evt_pwrok);
	if (ret) {
		printk(KERN_ERR "%s: pmic_event_subscribe failed (%d)\n", __func__, ret);
		ret = -EFAULT;
		goto out_regulator2;
	}

	/* subscribe to power fault event */
	g_pmic_evt_fault.param = fb_data;
	g_pmic_evt_fault.func = pmic_fault_cb;
	ret = pmic_event_subscribe(EVENT_EPD_FAULT, &g_pmic_evt_fault);
	if (ret) {
		printk(KERN_ERR "%s: pmic_event_subscribe failed (%d)\n", __func__, ret);
		ret = -EFAULT;
		goto out_pwrok_evt;
	}
#endif


#if defined(CONFIG_LAB126)
	if (device_create_file(info->dev, &fb_attrs[0]))
		dev_err(&pdev->dev, "Unable to create file from fb_attrs\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_powerup) < 0)
		dev_err(&pdev->dev, "Unable to create mxc_epdc_powerup file\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_debug) < 0)
		dev_err(&pdev->dev, "Unable to create mxc_epdc_debug file\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_update) < 0)
		dev_err(&pdev->dev, "Unable to create mxc_epdc_update file\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_pwrdown) < 0)
		dev_err(&pdev->dev, "Unable to create  mxc_epdc_pwrdown file\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_regs) < 0)
		dev_err(&pdev->dev, "Unable to create  mxc_epdc_regs file\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_temperature) < 0)
		dev_err(&pdev->dev, "Unable to create  mxc_epdc_temperature file\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_voltcontrol) < 0)
		dev_err(&pdev->dev, "Unable to create  mxc_epdc_voltcontrol file\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_waveform_modes) < 0)
		dev_err(&pdev->dev, "Unable to create mxc_epdc_waveform_modes file\n");

	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_wvaddr) < 0)
		dev_err(&pdev->dev, "Unable to create mxc_epdc_wvaddr file\n");

#ifdef CONFIG_FB_MXC_EINK_REAGL
	if (device_create_file(&pdev->dev, &dev_attr_mxc_epdc_reagl) < 0)
		dev_err(&pdev->dev, "Unable to create  mxc_epdc_reagl file\n");
#endif // CONFIG_FB_MXC_EINK_REAGL
#endif

	fb_data->cur_update = NULL;

	mutex_init(&fb_data->queue_mutex);
	mutex_init(&fb_data->pxp_mutex);
	mutex_init(&fb_data->power_mutex);

	/*
	 * Fill out PxP config data structure based on FB info and
	 * processing tasks required
	 */
	pxp_conf = &fb_data->pxp_conf;
	proc_data = &pxp_conf->proc_data;

	/* Initialize non-channel-specific PxP parameters */
	proc_data->drect.left = proc_data->srect.left = 0;
	proc_data->drect.top = proc_data->srect.top = 0;
	proc_data->drect.width = proc_data->srect.width = fb_data->info.var.xres;
	proc_data->drect.height = proc_data->srect.height = fb_data->info.var.yres;
	proc_data->scaling = 0;
	proc_data->hflip = 0;
	proc_data->vflip = 0;
	proc_data->rotate = 0;
	proc_data->bgcolor = 0;
	proc_data->overlay_state = 0;
	proc_data->lut_transform = PXP_LUT_NONE;
	proc_data->lut_map = NULL;

	/*
	 * We initially configure PxP for RGB->YUV conversion,
	 * and only write out Y component of the result.
	 */

	/*
	 * Initialize S0 channel parameters
	 * Parameters should match FB format/width/height
	 */
	pxp_conf->s0_param.pixel_fmt = PXP_PIX_FMT_RGB565;
	pxp_conf->s0_param.width = fb_data->info.var.xres_virtual;
	pxp_conf->s0_param.height = fb_data->info.var.yres;
	pxp_conf->s0_param.color_key = -1;
	pxp_conf->s0_param.color_key_enable = false;

	/*
	 * Initialize OL0 channel parameters
	 * No overlay will be used for PxP operation
	 */
	for (i = 0; i < 8; i++) {
		pxp_conf->ol_param[i].combine_enable = false;
		pxp_conf->ol_param[i].width = 0;
		pxp_conf->ol_param[i].height = 0;
		pxp_conf->ol_param[i].pixel_fmt = PXP_PIX_FMT_RGB565;
		pxp_conf->ol_param[i].color_key_enable = false;
		pxp_conf->ol_param[i].color_key = -1;
		pxp_conf->ol_param[i].global_alpha_enable = false;
		pxp_conf->ol_param[i].global_alpha = 0;
		pxp_conf->ol_param[i].local_alpha_enable = false;
	}

	/*
	 * Initialize Output channel parameters
	 * Output is Y-only greyscale
	 * Output width/height will vary based on update region size
	 */
	pxp_conf->out_param.width = fb_data->info.var.xres;
	pxp_conf->out_param.height = fb_data->info.var.yres;
	pxp_conf->out_param.stride = pxp_conf->out_param.width;
	pxp_conf->out_param.pixel_fmt = PXP_PIX_FMT_GREY;

	/* Initialize color map for conversion of 8-bit gray pixels */
	fb_data->pxp_conf.proc_data.lut_map = kmalloc(256, GFP_KERNEL);
	if (fb_data->pxp_conf.proc_data.lut_map == NULL) {
		dev_err(&pdev->dev, "Can't allocate mem for lut map!\n");
		ret = -ENOMEM;
		goto out_sysfs;
	}

	{
		unsigned short channel_map[256];
		struct fb_cmap linear_8bpp_cmap = {
			.len    = 256,
			.start  = 0,
			.red    = channel_map,
			.green  = channel_map,
			.blue   = channel_map,
			.transp = NULL
		};

		for (i = 0; i < 256; i++)
			channel_map[i] = (i << 8 | i);

		mxc_epdc_fb_setcmap(&linear_8bpp_cmap, &fb_data->info);
		fb_copy_cmap(&linear_8bpp_cmap, &info->cmap);
	}

	fb_data->pxp_conf.proc_data.lut_map_updated = true;

	/*
	 * Ensure this is set to NULL here...we will initialize pxp_chan
	 * later in our thread.
	 */
	fb_data->pxp_chan = NULL;

	/* Initialize Scatter-gather list containing 2 buffer addresses. */
	sg = fb_data->sg;
	sg_init_table(sg, 2);

	/*
	 * For use in PxP transfers:
	 * sg[0] holds the FB buffer pointer
	 * sg[1] holds the Output buffer pointer (configured before TX request)
	 */
	sg_dma_address(&sg[0]) = info->fix.smem_start;
	sg_set_page(&sg[0], virt_to_page(info->screen_base),
		    info->fix.smem_len, offset_in_page(info->screen_base));

	fb_data->order_cnt = 0;
	fb_data->waiting_for_wb = false;
	fb_data->waiting_for_lut = false;
	fb_data->waiting_for_lut15 = false;
	fb_data->waiting_for_idle = false;
	fb_data->blank = FB_BLANK_UNBLANK;
	fb_data->power_state = POWER_STATE_OFF;
	fb_data->powering_down = false;
	fb_data->wait_for_powerdown = false;
	fb_data->updates_active = false;
	fb_data->pwrdown_delay = 0;

	/* Register FB */
	ret = register_framebuffer(info);
	if (ret) {
		dev_err(&pdev->dev,
			"register_framebuffer failed with error %d\n", ret);
		goto out_lutmap;
	}

	g_fb_data = fb_data;

	if (!fb_data->hw_ready) {
		dev_dbg(&pdev->dev, "initializing hw\n");

		ret = mxc_epdc_fb_init_hw((struct fb_info *)fb_data);
		if (ret) {
			dev_err(&pdev->dev, "Failed to initialize HW!\n");
			goto out_framebuffer;
		}
	}

#if defined(CONFIG_LAB126)
	ret = mxc_epdc_waveform_init(fb_data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize waveform with error %d\n", ret);
		goto out_framebuffer;
	}
#endif

	ret = do_epdc_init_sequence(fb_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to do initialization sequence %d\n", ret);
		goto out_framebuffer;
	}

	ret = mxc_epdc_do_panel_init(fb_data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to do panel init : %d\n", ret);
		goto out_framebuffer;
	}

	/* Find the largest possible dimension and allocate three lines of dithering error distribution.*/
	{
		int max_res_virt = yres_virt > xres_virt ? yres_virt : xres_virt;
		dev_dbg(&pdev->dev, "Dithering. Error distribution allocating %d\n", max_res_virt);
		fb_data->dither_err_dist = kzalloc((max_res_virt + 4) * 3	* sizeof(int), GFP_KERNEL);
		if (!fb_data->dither_err_dist)
		{
			dev_err(&pdev->dev, "Failed to allocated dither memory\n");
			goto out_framebuffer;
		}
	}


#if defined(FIX_CURRENT_RESAMPLE)
	fb_data->sw_algo.scan_lines = SCAN_LINE_TOTAL;
	fb_data->sw_algo.count_thres = SCAN_LINE_THRESHOLD;
	fb_data->sw_algo.pixel_thres = TRANSITION_THRESHOLD;
	fb_data->sw_algo.str_thres = STRENGTH_THREASHOLD;
	fb_data->sw_algo.scaled_width = 0;
	fb_data->sw_algo.scaled_height = 0;
	fb_data->sw_algo.scaled_factor = SCALED_FACTOR;
#endif
	goto out;

out_framebuffer:
	unregister_framebuffer(info);

out_lutmap:
	kfree(fb_data->pxp_conf.proc_data.lut_map);

#if defined(CONFIG_LAB126)
out_sysfs:
	device_remove_file(fb_data->info.dev, &fb_attrs[0]);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_powerup);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_debug);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_update);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_pwrdown);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_regs);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_temperature);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_voltcontrol);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_waveform_modes);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_wvaddr);
#ifdef CONFIG_FB_MXC_EINK_REAGL
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_reagl);
#endif // CONFIG_FB_MXC_EINK_REAGL
#endif

#if defined(CONFIG_LAB126)
	/* unsubscribe pmic events */
	g_pmic_evt_fault.param = fb_data;
	g_pmic_evt_fault.func = pmic_fault_cb;
	pmic_event_unsubscribe(EVENT_EPD_FAULT, &g_pmic_evt_fault);

out_pwrok_evt:
	g_pmic_evt_pwrok.param = fb_data;
	g_pmic_evt_pwrok.func = pmic_pwrenable_cb;
	pmic_event_unsubscribe(EVENT_EPD_POK, &g_pmic_evt_pwrok);
#endif

out_regulator2:
	regulator_put(fb_data->vcom_regulator);
out_regulator1:
	regulator_put(fb_data->display_regulator);
out_irq:
	free_irq(fb_data->epdc_irq, fb_data);
out_reagl:
#ifdef CONFIG_FB_MXC_EINK_REAGL
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
	// This is only needed if REAGL-D is enabled
	reagl_free();
#endif
#endif // CONFIG_FB_MXC_EINK_REAGL
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
out_dma_work_buf_B:
	kfree(fb_data->working_buffer_B_virt);
out_dma_work_buf_A:
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B
#ifndef CONFIG_FB_MXC_EINK_WORK_BUFFER_RESERVED
	kfree(fb_data->working_buffer_A_virt);
#else
	iounmap(fb_data->working_buffer_A_virt);
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_RESERVED
	if (fb_data->pdata->put_pins)
		fb_data->pdata->put_pins();
out_copybuffer:
#if !defined(CONFIG_LAB126)
	dma_free_writecombine(&pdev->dev, fb_data->max_pix_size*2,
			      fb_data->virt_addr_copybuf,
			      fb_data->phys_addr_copybuf);
#endif
out_upd_buffers:
	for (i = 0; i < fb_data->max_num_buffers; i++)
		if (fb_data->virt_addr_updbuf[i] != NULL)
			kfree(fb_data->virt_addr_updbuf[i]);
	if (fb_data->virt_addr_updbuf != NULL)
		kfree(fb_data->virt_addr_updbuf);
	if (fb_data->phys_addr_updbuf != NULL)
		kfree(fb_data->phys_addr_updbuf);
out_upd_lists:
	list_for_each_entry_safe(plist, temp_list, &fb_data->upd_buf_free_list,
			list) {
		list_del(&plist->list);
		kfree(plist);
	}
out_dma_fb:
	dma_free_writecombine(&pdev->dev, fb_data->map_size, info->screen_base,
			      fb_data->phys_start);

out_mapregs:
	iounmap(epdc_base);
out_cmap:
	fb_dealloc_cmap(&info->cmap);
out_drv_data:
	platform_set_drvdata(pdev, NULL);
out_panel_close:
	mxc_epdc_waveform_done(fb_data);
out_fbdata:
	kfree(fb_data);
out:
	return ret;
}

static int mxc_epdc_fb_remove(struct platform_device *pdev)
{
	struct update_data_list *plist, *temp_list;
	struct mxc_epdc_fb_data *fb_data = platform_get_drvdata(pdev);
	int i;

	mxc_epdc_fb_blank(FB_BLANK_POWERDOWN, &fb_data->info);

#if defined(CONFIG_LAB126)
	device_remove_file(fb_data->info.dev, &fb_attrs[0]);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_powerup);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_debug);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_update);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_pwrdown);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_regs);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_temperature);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_voltcontrol);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_waveform_modes);
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_wvaddr);
#ifdef CONFIG_FB_MXC_EINK_REAGL
	device_remove_file(&pdev->dev, &dev_attr_mxc_epdc_reagl);
#endif // CONFIG_FB_MXC_EINK_REAGL
#endif

	flush_workqueue(fb_data->epdc_submit_workqueue);
	destroy_workqueue(fb_data->epdc_submit_workqueue);
	flush_delayed_work_sync(&fb_data->epdc_done_work);

	regulator_put(fb_data->display_regulator);
	regulator_put(fb_data->vcom_regulator);

#if defined(CONFIG_LAB126)
	/* unsubscribe pmic events */
	g_pmic_evt_pwrok.param = fb_data;
	g_pmic_evt_pwrok.func = pmic_pwrenable_cb;
	pmic_event_unsubscribe(EVENT_EPD_POK, &g_pmic_evt_pwrok);

	g_pmic_evt_pwrok.param = fb_data;
	g_pmic_evt_pwrok.func = pmic_fault_cb;
	pmic_event_unsubscribe(EVENT_EPD_FAULT, &g_pmic_evt_fault);
#endif

	unregister_framebuffer(&fb_data->info);

#if defined(CONFIG_LAB126)
	mxc_epdc_waveform_done(fb_data);

	if (fb_data->wv_header) {
		kfree(fb_data->wv_header);
		fb_data->wv_header = NULL;
	}

#ifdef CONFIG_FB_MXC_EINK_REAGL
	reagl_free();
#endif // CONFIG_FB_MXC_EINK_REAGL
#endif

	if (fb_data->temp_range_bounds) {
		kfree(fb_data->temp_range_bounds);
		fb_data->temp_range_bounds = NULL;
	}

	free_irq(fb_data->epdc_irq, fb_data);

	for (i = 0; i < fb_data->max_num_buffers; i++)
		if (fb_data->virt_addr_updbuf[i] != NULL)
			kfree(fb_data->virt_addr_updbuf[i]);

	if (fb_data->virt_addr_updbuf != NULL)
		kfree(fb_data->virt_addr_updbuf);
	if (fb_data->phys_addr_updbuf != NULL)
		kfree(fb_data->phys_addr_updbuf);
#ifndef CONFIG_FB_MXC_EINK_WORK_BUFFER_RESERVED
	kfree(fb_data->working_buffer_A_virt);
#else
	iounmap(fb_data->working_buffer_A_virt);
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_RESERVED
#if defined(CONFIG_LAB126) && defined(CONFIG_FB_MXC_EINK_WORK_BUFFER_B)
	kfree(fb_data->working_buffer_B_virt);
#endif // CONFIG_FB_MXC_EINK_WORK_BUFFER_B

	if (fb_data->waveform_buffer_virt != NULL)
		dma_free_writecombine(&pdev->dev, fb_data->waveform_buffer_size,
				fb_data->waveform_buffer_virt,
				fb_data->waveform_buffer_phys);
#if !defined(CONFIG_LAB126)
	if (fb_data->virt_addr_copybuf != NULL)
		dma_free_writecombine(&pdev->dev, fb_data->max_pix_size*2,
				fb_data->virt_addr_copybuf,
				fb_data->phys_addr_copybuf);
#endif
	list_for_each_entry_safe(plist, temp_list, &fb_data->upd_buf_free_list,
			list) {
		list_del(&plist->list);
		kfree(plist);
	}

#ifdef CONFIG_FB_MXC_EINK_AUTO_UPDATE_MODE
	fb_deferred_io_cleanup(&fb_data->info);
#endif

	dma_free_writecombine(&pdev->dev, fb_data->map_size, fb_data->info.screen_base,
			      fb_data->phys_start);

	if (fb_data->pdata->put_pins)
		fb_data->pdata->put_pins();

	/* Release PxP-related resources */
	if (fb_data->pxp_chan != NULL)
		dma_release_channel(&fb_data->pxp_chan->dma_chan);

	if (fb_data->pxp_conf.proc_data.lut_map != NULL)
		kfree(fb_data->pxp_conf.proc_data.lut_map);

	if (fb_data->waveform_vcd_buffer != NULL)
		kfree(fb_data->waveform_vcd_buffer);

	if (fb_data->dither_err_dist != NULL)
		kfree(fb_data->dither_err_dist);

	iounmap(epdc_base);

	fb_dealloc_cmap(&fb_data->info.cmap);

	framebuffer_release(&fb_data->info);
	return 0;
}

#ifdef CONFIG_PM
static int mxc_epdc_fb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mxc_epdc_fb_data *data = platform_get_drvdata(pdev);
	return mxc_epdc_fb_blank(FB_BLANK_POWERDOWN, &data->info);
}

static int mxc_epdc_fb_resume(struct platform_device *pdev)
{
	struct mxc_epdc_fb_data *data = platform_get_drvdata(pdev);

	epdc_init_settings(data);
	mxc_epdc_fb_blank(FB_BLANK_UNBLANK, &data->info);
	data->updates_active = false;

	return 0;
}

static void mxc_epdc_fb_shutdown(struct platform_device *pdev)
{
	struct mxc_epdc_fb_data *data = platform_get_drvdata(pdev);
	mxc_epdc_fb_blank(FB_BLANK_POWERDOWN, &data->info);
}

#else
#define mxc_epdc_fb_suspend    NULL
#define mxc_epdc_fb_resume     NULL
#define mxc_epdc_fb_shutdown   NULL
#endif

static struct platform_driver mxc_epdc_fb_driver = {
	.probe = mxc_epdc_fb_probe,
	.remove = mxc_epdc_fb_remove,
	.suspend = mxc_epdc_fb_suspend,
	.resume = mxc_epdc_fb_resume,
	.shutdown = mxc_epdc_fb_shutdown,
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   },
};

/* Callback function triggered after PxP receives an EOF interrupt */
static void pxp_dma_done(void *arg)
{
	struct pxp_tx_desc *tx_desc = to_tx_desc(arg);
	struct dma_chan *chan = tx_desc->txd.chan;
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);
	struct mxc_epdc_fb_data *fb_data = pxp_chan->client;

	/* This call will signal wait_for_completion_timeout() in send_buffer_to_pxp */
	complete(&fb_data->pxp_tx_cmpl);
}

static bool chan_filter(struct dma_chan *chan, void *arg)
{
	if (imx_dma_is_pxp(chan))
		return true;
	else
		return false;
}

/* Function to request PXP DMA channel */
static int pxp_chan_init(struct mxc_epdc_fb_data *fb_data)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;

	/*
	 * Request a free channel
	 */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);
	chan = dma_request_channel(mask, chan_filter, NULL);
	if (!chan) {
		dev_err(fb_data->dev, "Unsuccessfully received channel!!!!\n");
		return -EBUSY;
	}

	fb_data->pxp_chan = to_pxp_channel(chan);
	fb_data->pxp_chan->client = fb_data;

	init_completion(&fb_data->pxp_tx_cmpl);

	return 0;
}

/*
 * Function to call PxP DMA driver and send our latest FB update region
 * through the PxP and out to an intermediate buffer.
 * Note: This is a blocking call, so upon return the PxP tx should be complete.
 */
static int pxp_process_update(struct mxc_epdc_fb_data *fb_data,
			      u32 src_width, u32 src_height,
			      struct mxcfb_rect *update_region)
{
	dma_cookie_t cookie;
	struct scatterlist *sg = fb_data->sg;
	struct dma_chan *dma_chan;
	struct pxp_tx_desc *desc;
	struct dma_async_tx_descriptor *txd;
	struct pxp_config_data *pxp_conf = &fb_data->pxp_conf;
	struct pxp_proc_data *proc_data = &fb_data->pxp_conf.proc_data;
	int i, ret;
	int length;

	dev_dbg(fb_data->dev, "Starting PxP Send Buffer\n");

	/* First, check to see that we have acquired a PxP Channel object */
	if (fb_data->pxp_chan == NULL) {
		/*
		 * PxP Channel has not yet been created and initialized,
		 * so let's go ahead and try
		 */
		ret = pxp_chan_init(fb_data);
		if (ret) {
			/*
			 * PxP channel init failed, and we can't use the
			 * PxP until the PxP DMA driver has loaded, so we abort
			 */
			dev_err(fb_data->dev, "PxP chan init failed\n");
			return -ENODEV;
		}
	}

	/*
	 * Init completion, so that we
	 * can be properly informed of the completion
	 * of the PxP task when it is done.
	 */
	init_completion(&fb_data->pxp_tx_cmpl);

	dma_chan = &fb_data->pxp_chan->dma_chan;

	txd = dma_chan->device->device_prep_slave_sg(dma_chan, sg, 2,
						     DMA_TO_DEVICE,
						     DMA_PREP_INTERRUPT);
	if (!txd) {
		dev_err(fb_data->info.device,
			"Error preparing a DMA transaction descriptor.\n");
		return -EIO;
	}

	txd->callback_param = txd;
	txd->callback = pxp_dma_done;

	/*
	 * Configure PxP for processing of new update region
	 * The rest of our config params were set up in
	 * probe() and should not need to be changed.
	 */
	pxp_conf->s0_param.width = src_width;
	pxp_conf->s0_param.height = src_height;
	proc_data->srect.top = update_region->top;
	proc_data->srect.left = update_region->left;
	proc_data->srect.width = update_region->width;
	proc_data->srect.height = update_region->height;

	/*
	 * Because only YUV/YCbCr image can be scaled, configure
	 * drect equivalent to srect, as such do not perform scaling.
	 */
	proc_data->drect.top = 0;
	proc_data->drect.left = 0;
	proc_data->drect.width = proc_data->srect.width;
	proc_data->drect.height = proc_data->srect.height;

	/* PXP expects rotation in terms of degrees */
	proc_data->rotate = fb_data->epdc_fb_var.rotate * 90;
	if (proc_data->rotate > 270)
		proc_data->rotate = 0;

	pxp_conf->out_param.width = update_region->width;
	pxp_conf->out_param.height = update_region->height;

	if ((proc_data->rotate == 90) || (proc_data->rotate == 270))
		pxp_conf->out_param.stride = update_region->height;
	else
		pxp_conf->out_param.stride = update_region->width;

	/* For EPDC v2.0, we need output to be 64-bit
	 * aligned since EPDC stride does not work. */
	if (fb_data->rev <= EPDC_VERSION_2_0_0)
		pxp_conf->out_param.stride = ALIGN(pxp_conf->out_param.stride, 8);


	desc = to_tx_desc(txd);
	length = desc->len;
	for (i = 0; i < length; i++) {
		if (i == 0) {/* S0 */
			memcpy(&desc->proc_data, proc_data, sizeof(struct pxp_proc_data));
			pxp_conf->s0_param.paddr = sg_dma_address(&sg[0]);
			memcpy(&desc->layer_param.s0_param, &pxp_conf->s0_param,
				sizeof(struct pxp_layer_param));
		} else if (i == 1) {
			pxp_conf->out_param.paddr = sg_dma_address(&sg[1]);
			memcpy(&desc->layer_param.out_param, &pxp_conf->out_param,
				sizeof(struct pxp_layer_param));
		}
		/* TODO: OverLay */

		desc = desc->next;
	}

	/* Submitting our TX starts the PxP processing task */
	cookie = txd->tx_submit(txd);
	if (cookie < 0) {
		dev_err(fb_data->info.device, "Error sending FB through PxP\n");
		return -EIO;
	}

	fb_data->txd = txd;

	/* trigger ePxP */
	dma_async_issue_pending(dma_chan);

	return 0;
}

#if defined(FIX_CURRENT_RESAMPLE)
struct local_pxp_config_data {
	struct pxp_layer_param s0_param;
	struct pxp_layer_param out_param;
	struct pxp_proc_data proc_data;
};
static int pxp_legacy_process_resample(struct mxcfb_update_data *upd_data, struct mxc_epdc_fb_data *fb_data,
			      u32 scaled_width, u32 scaled_height, u32 down_up)
{
	int i, ret;
	int length;
	struct dma_async_tx_descriptor *txd;
	struct scatterlist *sg = fb_data->sg;
	struct dma_chan *dma_chan;
	struct local_pxp_config_data local_pxp_conf;
	struct local_pxp_config_data *pxp_conf=&local_pxp_conf;
	struct pxp_proc_data *proc_data = &local_pxp_conf.proc_data;
	struct pxp_tx_desc *desc;
	dma_cookie_t cookie;

	if (mxc_epdc_debugging)
		printk(KERN_INFO"mxc_epdc_fb:Starting PxP resample process.\n");

	/* First, check to see that we have acquired a PxP Channel object */
	if (fb_data->pxp_chan == NULL) {
		/*
		 * PxP Channel has not yet been created and initialized,
		 * so let's go ahead and try
		 */
		ret = pxp_chan_init(fb_data);
		if (ret) {
			/*
			 * PxP channel init failed, and we can't use the
			 * PxP until the PxP DMA driver has loaded, so we abort
			 */
			dev_err(fb_data->dev, "PxP chan init failed\n");
			return -ENODEV;
		}
	}

	/*
	 * Init completion, so that we
	 * can be properly informed of the completion
	 * of the PxP task when it is done.
	 */
	init_completion(&fb_data->pxp_tx_cmpl);

	dma_chan = &fb_data->pxp_chan->dma_chan;

	txd = dma_chan->device->device_prep_slave_sg(dma_chan, sg, 2,
							 DMA_TO_DEVICE,
							 DMA_PREP_INTERRUPT);
	if (!txd) {
		dev_err(fb_data->info.device,
			"Error preparing a DMA transaction descriptor.\n");
		return -EIO;
	}

	txd->callback_param = txd;
	txd->callback = pxp_dma_done;

	memset(pxp_conf,0,sizeof(*pxp_conf));
	proc_data->lut_map = fb_data->pxp_conf.proc_data.lut_map;

	/* doing scaling for a larger update region coverring the whole line */
	if (down_up) { /* down sample from frame 0 to frame 1 */
		proc_data->srect.width = fb_data->info.var.xres_virtual;
		proc_data->srect.height = upd_data->update_region.height;
		proc_data->drect.width =  scaled_width;
		proc_data->drect.height = scaled_height;

		pxp_conf->s0_param.pixel_fmt =PXP_PIX_FMT_GREY;
		pxp_conf->s0_param.width = fb_data->info.var.xres_virtual;
		pxp_conf->s0_param.height = upd_data->update_region.height;
		pxp_conf->s0_param.stride = fb_data->info.var.xres_virtual;
		pxp_conf->s0_param.paddr = fb_data->phys_start +
			upd_data->update_region.top*fb_data->info.var.xres_virtual;

		pxp_conf->out_param.pixel_fmt = PXP_PIX_FMT_GREY;
		pxp_conf->out_param.width = scaled_width;
		pxp_conf->out_param.height =scaled_height;
		pxp_conf->out_param.stride = scaled_width;
		pxp_conf->out_param.paddr = fb_data->phys_addr_updbuf[0];
	} else { /* up sample from frame 1 to frame 0 */
		proc_data->srect.width =  scaled_width;
		proc_data->srect.height = scaled_height;
		proc_data->drect.width = fb_data->info.var.xres_virtual;
		proc_data->drect.height = upd_data->update_region.height;

		pxp_conf->s0_param.pixel_fmt =PXP_PIX_FMT_GREY;
		pxp_conf->s0_param.width = scaled_width;
		pxp_conf->s0_param.height = scaled_height;
		pxp_conf->s0_param.stride = scaled_width;
		pxp_conf->s0_param.paddr = fb_data->phys_addr_updbuf[0];

		pxp_conf->out_param.pixel_fmt = PXP_PIX_FMT_GREY;
		pxp_conf->out_param.width = fb_data->info.var.xres_virtual;
		pxp_conf->out_param.height = upd_data->update_region.height;
		pxp_conf->out_param.stride = fb_data->info.var.xres_virtual;
		pxp_conf->out_param.paddr = fb_data->phys_start +
			upd_data->update_region.top*fb_data->info.var.xres_virtual;
	}

	/* we should pass the rotated values to PXP */
	if ((proc_data->rotate == 90) || (proc_data->rotate == 270)) {
	} else {
	}

	desc = to_tx_desc(txd);
	length = desc->len;
	for (i = 0; i < length; i++) {
		if (i == 0) {	/* S0 */
			memcpy(&desc->proc_data, &pxp_conf->proc_data, sizeof(struct pxp_proc_data));
			memcpy(&desc->layer_param.s0_param, &pxp_conf->s0_param, sizeof(struct pxp_layer_param));
			desc = desc->next;
		} else if (i == 1) {	/* Output */
			memcpy(&desc->layer_param.out_param, &pxp_conf->out_param, sizeof(struct pxp_layer_param));
			desc = desc->next;
		} else if (i == 2) {
			/* OverLay */
			//memcpy(&desc->layer_param.ol_param, &pxp_conf->ol_param, sizeof(struct pxp_layer_param));
			//desc = desc->next;
		}
	}

	/* Submitting our TX starts the PxP processing task */
	cookie = txd->tx_submit(txd);
	if (cookie < 0) {
		dev_err(fb_data->info.device, "Error sending FB through PxP\n");
		return -EIO;
	}

	fb_data->txd = txd;

	/* trigger ePxP */
	dma_async_issue_pending(dma_chan);

	return 0;
}
#endif

static int pxp_complete_update(struct mxc_epdc_fb_data *fb_data, u32 *hist_stat)
{
	int ret;
	/*
	 * Wait for completion event, which will be set
	 * through our TX callback function.
	 */
	ret = wait_for_completion_timeout(&fb_data->pxp_tx_cmpl, HZ / 10);
	if (ret <= 0) {
		dev_info(fb_data->info.device,
			 "PxP operation failed due to %s\n",
			 ret < 0 ? "user interrupt" : "timeout");
		dma_release_channel(&fb_data->pxp_chan->dma_chan);
		fb_data->pxp_chan = NULL;
		return ret ? : -ETIMEDOUT;
	}

	if (((fb_data->pxp_conf.proc_data.lut_transform & EPDC_FLAG_USE_CMAP) || use_cmap) &&
		fb_data->pxp_conf.proc_data.lut_map_updated)
		fb_data->pxp_conf.proc_data.lut_map_updated = false;

	*hist_stat = to_tx_desc(fb_data->txd)->hist_status;
	dma_release_channel(&fb_data->pxp_chan->dma_chan);
	fb_data->pxp_chan = NULL;

	dev_dbg(fb_data->dev, "TX completed\n");

	return 0;
}

/*
 * Different dithering algorithm can be used. We chose
 * to implement Bill Atkinson's algorithm as an example
 * Thanks Bill Atkinson for his dithering algorithm.
 */

#if defined(CONFIG_LAB126)
/*
 * Sierra Lite implementation
 *
 * Error distribution:
 *   X 2
 *   1 1
 *
 * (1/4)
 */
static void do_dithering_processing_Y1_v1_0(
		unsigned char *update_region_ptr,
		struct mxcfb_rect *update_region,
		unsigned long update_region_stride,
		int *err_dist, int reaglWfm)
{
	int newPix;
	int x, y;
	int *err_dist_l0, *err_dist_l1, distrib_error;
	int *tmp_dist;
	int width_4 = update_region->width + 4;
	unsigned char *y8buf;

	err_dist_l0 = err_dist + 1;
	err_dist_l1 = err_dist_l0 + width_4;
	y8buf = update_region_ptr;

	memset(err_dist_l0 - 1, 0, sizeof(int) * width_4);
	for (y = 0; y < update_region->height; y++)
	{
		memset(err_dist_l1 - 1, 0, sizeof(int) * width_4);
		for (x = 0; x < update_region->width; x++)
		{
			int currentPixel = y8buf[x] + err_dist_l0[x];

			newPix = currentPixel < 128 ? 0 : 255;
			distrib_error = (currentPixel - newPix) / 4;
			y8buf[x] = (unsigned char)newPix;

			if (reaglWfm)
				y8buf[x] &= 0xF0;

			/* modify the error distribution buffer */
			err_dist_l0[x+1] += (distrib_error * 2);
			err_dist_l1[x-1] += (distrib_error);
			err_dist_l1[x  ] += (distrib_error);
		}
		y8buf += update_region_stride;

		tmp_dist = err_dist_l0;
		err_dist_l0 = err_dist_l1;
		err_dist_l1 = tmp_dist;
	}
	flush_cache_all();
	outer_flush_all();
}

#else

/*
 * Bill Atkinson implementation
 * Dithering algorithm implementation - Y8->Y1 version 1.0 for i.MX
 *
 * Error distribution:
 *   X 1 1
 * 1 1 1
 *   1
 *
 *  (1/8)
 */
static void do_dithering_processing_Y1_v1_0(
		unsigned char *update_region_ptr,
		struct mxcfb_rect *update_region,
		unsigned long update_region_stride,
		int *err_dist, int reaglWfm)
{
	int newPix;
	int x, y;
	int *err_dist_l0, *err_dist_l1, *err_dist_l2, distrib_error;
	int *tmp_dist;
	int width_4 = update_region->width + 4;
	unsigned char *y8buf;

	err_dist_l0 = err_dist + 1;
	err_dist_l1 = err_dist_l0 + width_4;
	err_dist_l2 = err_dist_l1 + width_4;
	y8buf = update_region_ptr;

	memset(err_dist, 0, sizeof(int) * width_4 * 2);
	for (y = 0; y < update_region->height; y++) {
		memset(err_dist_l2 - 1, 0, sizeof(int) * width_4);
		for (x = 0; x < update_region->width; x++) {
			int currentPixel = err_dist_l0[x] + y8buf[x];

			newPix = (currentPixel < 128) ? 0 : 0xFF;
			distrib_error = (currentPixel - newPix) / 8;
			y8buf[x] = (unsigned char)newPix;

			if (reaglWfm)
				y8buf[x] &= 0xf0;

			/* modify the error distribution buffer */
			*(err_dist_l0 + x + 1) += distrib_error;
			*(err_dist_l0 + x + 2) += distrib_error;
			*(err_dist_l1 + x - 1) += distrib_error;
			*(err_dist_l1 + x    ) += distrib_error;
			*(err_dist_l1 + x + 1) += distrib_error;
			*(err_dist_l2 + x    ) += distrib_error;
		}
		y8buf += update_region_stride;

		tmp_dist = err_dist_l0;
		err_dist_l0 = err_dist_l1;
		err_dist_l1 = err_dist_l2;
		err_dist_l2 = tmp_dist;
	}
	flush_cache_all();
	outer_flush_all();
}

#endif

/*
 * Bill Atkinson implementation
 * Dithering algorithm implementation - Y8->Y2 version 1.0 for i.MX
 */
static void do_dithering_processing_Y2_v1_0(
		unsigned char *update_region_ptr,
		struct mxcfb_rect *update_region,
		unsigned long update_region_stride,
		int *err_dist, int reaglWfm)
{
	int newPix;
	int x, y;
	int *err_dist_l0, *err_dist_l1, *err_dist_l2, distrib_error;
	int *tmp_dist;
	int width_4 = update_region->width + 4;
	unsigned char *y8buf;

	err_dist_l0 = err_dist + 1;
	err_dist_l1 = err_dist_l0 + width_4;
	err_dist_l2 = err_dist_l1 + width_4;
	y8buf = update_region_ptr;

	memset(err_dist, 0, sizeof(int) * width_4 * 2);
	for (y = 0; y < update_region->height; y++) {
		memset(err_dist_l2 - 1, 0, sizeof(int) * width_4);
		for (x = 0; x < update_region->width; x++) {
			int currentPixel = err_dist_l0[x] + y8buf[x];

			if (currentPixel > 0xAA) newPix = 0xFF;
			else if (currentPixel > 0x55) newPix = 0xAA;
			else if (currentPixel > 0x00) newPix = 0x55;
			else newPix = 0x00;

			distrib_error = (currentPixel - newPix) / 8;
			y8buf[x] = (unsigned char)newPix;

			if (reaglWfm)
				y8buf[x] &= 0xf0;

			/* modify the error distribution buffer */
			*(err_dist_l0 + x + 1) += distrib_error;
			*(err_dist_l0 + x + 2) += distrib_error;
			*(err_dist_l1 + x - 1) += distrib_error;
			*(err_dist_l1 + x    ) += distrib_error;
			*(err_dist_l1 + x + 1) += distrib_error;
			*(err_dist_l2 + x    ) += distrib_error;
		}
		y8buf += update_region_stride;

		tmp_dist = err_dist_l0;
		err_dist_l0 = err_dist_l1;
		err_dist_l1 = err_dist_l2;
		err_dist_l2 = tmp_dist;
	}
	flush_cache_all();
	outer_flush_all();
}

/*
 * Bill Atkinson Implementation
 * Dithering algorithm implementation - Y8->Y4 version 1.0 for i.MX
 */
static void do_dithering_processing_Y4_v1_0(
		unsigned char *update_region_ptr,
		struct mxcfb_rect *update_region,
		unsigned long update_region_stride,
		int *err_dist, int reaglWfm)
{
	int newPix;
	int x, y;
	int *err_dist_l0, *err_dist_l1, *err_dist_l2, distrib_error;
	int *tmp_dist;
	int width_4 = update_region->width + 4;
	unsigned char *y8buf;

	err_dist_l0 = err_dist + 1;
	err_dist_l1 = err_dist_l0 + width_4;
	err_dist_l2 = err_dist_l1 + width_4;
	y8buf = update_region_ptr;

	/* prime a few elements the error distribution array */
	memset(err_dist, 0, sizeof(int) * width_4 * 2);
	for (y = 0; y < update_region->height; y++) {
		memset(err_dist_l2 - 1, 0, sizeof(int) * width_4);
		for (x = 0; x < update_region->width; x++) {
			int currentPixel = err_dist_l0[x] + y8buf[x];

			if (currentPixel > 255)
				newPix = 255;
			else if (currentPixel < 0)
				newPix = 0;
			else
				newPix = currentPixel;

			distrib_error = (currentPixel - (newPix & 0xF0)) / 8;

			y8buf[x] = (unsigned char)newPix;
			if (reaglWfm)
				y8buf[x] &= 0xF0;

			/* modify the error distribution buffer */
			*(err_dist_l0 + x + 1) += distrib_error;
			*(err_dist_l0 + x + 2) += distrib_error;
			*(err_dist_l1 + x - 1) += distrib_error;
			*(err_dist_l1 + x    ) += distrib_error;
			*(err_dist_l1 + x + 1) += distrib_error;
			*(err_dist_l2 + x    ) += distrib_error;
		}
		y8buf += update_region_stride;

		tmp_dist = err_dist_l0;
		err_dist_l0 = err_dist_l1;
		err_dist_l1 = err_dist_l2;
		err_dist_l2 = tmp_dist;
	}
	flush_cache_all();
	outer_flush_all();
}

static int __init mxc_epdc_fb_init(void)
{
	return platform_driver_probe(&mxc_epdc_fb_driver, &mxc_epdc_fb_probe);
}
late_initcall(mxc_epdc_fb_init);


static void __exit mxc_epdc_fb_exit(void)
{
	platform_driver_unregister(&mxc_epdc_fb_driver);
}
module_exit(mxc_epdc_fb_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXC EPDC framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("fb");
