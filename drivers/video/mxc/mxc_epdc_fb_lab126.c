/*
 * Copyright 2012-2017 Amazon Technologies, Inc. All Rights Reserved.
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

/* Lab126 functions for mxc_epdc
 */

#include <linux/spi/spi.h>
#include <linux/proc_fs.h>
#include <linux/zlib.h>
#include <linux/vmalloc.h>
#include <linux/einkwf.h>
#include <mach/boardid.h>

static int override_panel_settings = 0;
#define DISPLAY_UP_RIGHT   ( lab126_board_is(BOARD_ID_WOODY) || \
                              lab126_board_is(BOARD_ID_WHISKY_WAN) || \
                              lab126_board_is(BOARD_ID_WHISKY_WFO) )

#ifdef DEVELOPMENT_MODE
/*
 * The override_panel_settings flag allows the user to ignore the eink panel
 * settings and instead use the ones defined in panel_get_default_info(). This
 * is needed to load the EPDC module when connected to a blank panel (though
 * it would also work with no panel at all). Make sure the default settings
 * are correct before enabling this flag or you will risk burning up the panel.
 */
module_param(override_panel_settings, int, 0);
MODULE_PARM_DESC(override_panel_settings, "Do not read settings from the panel.");
#endif // DEVELOPMENT_MODE

/************
 * Utility  *
 ************/

#define GUNZIP_HEAD_CRC       2
#define GUNZIP_EXTRA_FIELD    4
#define GUNZIP_ORIG_NAME      8
#define GUNZIP_COMMENT        0x10
#define GUNZIP_RESERVED       0xe0
#define GUNZIP_DEFLATED       8

unsigned char sum8(unsigned char *buf, int len);
unsigned sum32(unsigned char *buf, int len);
static unsigned long crc32(unsigned char *buf, int len);
static unsigned long update_crc(unsigned long crc, unsigned char *buf, int len);
static void make_crc_table(void);
static int panel_data_write(unsigned long base_address, struct file *file, const char __user *buf, unsigned long count, void *data);
static int gunzip(unsigned char *dst, size_t dst_len, size_t *write_len, const unsigned char *src, size_t src_len);


/************
 *   eInk   *
 ************/

#define EINK_ADDR_CHECKSUM1         0x001F  // 1 byte  (checksum of bytes 0x00-0x1E)
#define EINK_ADDR_CHECKSUM2         0x002F  // 1 byte  (checksum of bytes 0x20-0x2E)
#define EINK_WAVEFORM_FILESIZE      262144  // 256K..
#define EINK_WAVEFORM_TYPE_WJ       0x15
#define EINK_WAVEFORM_TYPE_WR       0x2B
#define EINK_WAVEFORM_TYPE_AA       0x3C
#define EINK_WAVEFORM_TYPE_AC       0x4B
#define EINK_WAVEFORM_TYPE_BD       0x4C
#define EINK_WAVEFORM_TYPE_AE       0x50
#define EINK_CHECKSUM(c1, c2)       (((c2) << 16) | (c1))


/************
 * Waveform *
 ************/

#define WF_UPD_MODES_00             0       // Test waveform
#define WF_UPD_MODES_07             7       // V220 210 dpi85Hz modes
#define WF_UPD_MODES_18             18
#define WF_UPD_MODES_19             19
#define WF_UPD_MODES_24             24
#define WF_UPD_MODES_25             25

#define WF_PROC_PARENT              "eink"
#define WF_PROC_PANEL_PARENT        "eink/panel"
#define WF_PROC_PANEL_WFM_PARENT    "eink/panel/waveform"
#define WF_PROC_WFM_PARENT          "eink/waveform"

#define WAVEFORM_VERSION_STRING_MAX 64
#define CHECKSUM_STRING_MAX         64

#define WAVEFORM_AA_VCOM_SHIFT      250000

/* Steps for voltage control in uV */
#define EPDC_VC_VPOS_STEP 12500
#define EPDC_VC_VNEG_STEP -12500
#define EPDC_VC_VDDH_STEP 12500
#define EPDC_VC_VEE_STEP  -12500
#define EPDC_VC_MAX 0x7FFF

#define EPDC_VC_VCOM_OFFSET_POS_STEP 3125
#define EPDC_VC_VCOM_OFFSET_NEG_STEP -3125
#define EPDC_VC_VCOM_OFFSET_POS_MIN 0x0000
#define EPDC_VC_VCOM_OFFSET_POS_MAX 0x0FFF
#define EPDC_VC_VCOM_OFFSET_NEG_MIN 0x8000
#define EPDC_VC_VCOM_OFFSET_NEG_MAX 0x8FFF

struct update_mode {
	unsigned char mode;
	char *name;
};

struct update_modes {
	struct update_mode init;
	struct update_mode du;
	struct update_mode gc16;
	struct update_mode gcf;
	struct update_mode gl16;
	struct update_mode glf;
	struct update_mode a2;
	struct update_mode du4;
	struct update_mode gl4;
	struct update_mode glr;
	struct update_mode glrd;
	struct update_mode gldk;
	struct update_mode glr4;
};

struct panel_addrs {
	off_t cmd_sec_addr;
	off_t waveform_addr;
	off_t pnl_info_addr;
	off_t test_sec_addr;
	size_t cmd_sec_len;
	size_t waveform_len;
	size_t pnl_info_len;
	size_t test_sec_len;
	size_t flash_end;
};

extern struct mxc_epdc_fb_data *g_fb_data;

bool wfm_using_builtin = false;

char * wfm_name_for_mode(struct mxc_epdc_fb_data *fb_data, int mode);

static int proc_wfm_data_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_wfm_version_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_wfm_human_version_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_wfm_embedded_checksum_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_wfm_computed_checksum_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_wfm_info_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_wfm_source_read(char *page, char **start, off_t off, int count, int *eof, void *data);


/***********
 *  Panel  *
 ***********/

/*
 * Panel flash
 */

/* All panels must locate the waveform at the default offset. Otherwise, we
 * have no way of knowing what kind of panel is attached. Additionally, all
 * panels must be greater than or equal in size to the default flash size.
 */
#define DEFAULT_WFM_ADDR      0x00886
#define DEFAULT_FLASH_SIZE    0x40000

#define WFM_HDR_SIZE          (0x30)

#define PNL_BASE_PART_NUMBER  0x00
#define PNL_SIZE_PART_NUMBER  16

#define PNL_BASE_VCOM         0x10
#define PNL_SIZE_VCOM         5
#define PNL_SIZE_VCOM_STR     (PNL_SIZE_VCOM + 1)

#define PNL_BASE_WAVEFORM     0x20
#define PNL_SIZE_WAVEFORM     23

#define PNL_BASE_FPL          0x40
#define PNL_SIZE_FPL          3

#define PNL_BASE_BCD          0x50
#define PNL_SIZE_BCD          33
#define PNL_SIZE_BCD_STR      (PNL_SIZE_BCD + 1)
#define PNL_BCD_PREFIX_LEN    3

#define PNL_BASE_RESOLUTION   0x80
#define PNL_SIZE_RESOLUTION   16
#define PNL_SIZE_RESOLUTION_STR (PNL_SIZE_RESOLUTION + 1)

#define PNL_BASE_DIMENSIONS   0x90
#define PNL_SIZE_DIMENSIONS   32
#define PNL_SIZE_DIMENSIONS_STR (PNL_SIZE_DIMENSIONS + 1)

#define PNL_BASE_VDD          0xB0
#define PNL_SIZE_VDD          16
#define PNL_SIZE_VDD_STR      (PNL_SIZE_VDD + 1)

#define PNL_BASE_VERSION      0x300
#define PNL_SIZE_VERSION      16
#define PNL_SIZE_VERSION_STR  (PNL_SIZE_VERSION + 1)

/*
 * TODO: Once Eink finalizes the flash spec to include the panel resolution, this code can be re-enabled
#define PNL_BASE_XRES          0x00
#define PNL_BASE_YRES          0x00
 */

#define PNL_CHAR_UNKNOWN      '!'

/*
 * SPI Flash API
 */

#define SFM_WRSR              0x01
#define SFM_PP                0x02
#define SFM_READ              0x03
#define SFM_WRDI              0x04
#define SFM_RDSR              0x05
#define SFM_WREN              0x06
#define SFM_FAST_READ         0x0B
#define SFM_SE                0x20
#define SFM_BE                0xD8
#define SFM_RES               0xAB
#define SFM_ID                0x9F
#define SFM_WIP_MASK          BIT(0)
#define SFM_BP0_MASK          BIT(2)
#define SFM_BP1_MASK          BIT(3)

#define PNL_PAGE_SIZE         (256)
#define PNL_SECTOR_SIZE       (1024 * 4)
#define PNL_BLOCK_SIZE        (1024 * 64)
#define PNL_SIZE              (1024 * 128)

#define PANEL_ID_UNKNOWN      "????_???_??_???"
#define PNL_SIZE_ID_STR       32

#define MXC_SPI_MAX_CHARS     28
#define SFM_READ_CMD_LEN      4
#define SFM_WRITE_CMD_LEN     4

struct panel_info {
	struct panel_addrs *addrs;
	int  vcom_uV;
	long computed_checksum;
	long embedded_checksum;
	int  version_major;
	int  version_minor;
	struct eink_waveform_info_t *waveform_info;
	char human_version[WAVEFORM_VERSION_STRING_MAX];
	char version[WAVEFORM_VERSION_STRING_MAX];
	char bcd[PNL_SIZE_BCD_STR];
	char id[PNL_SIZE_ID_STR];
	char panel_info_version[PNL_SIZE_VERSION_STR];
	char resolution[PNL_SIZE_RESOLUTION_STR];
	char dimensions[PNL_SIZE_DIMENSIONS_STR];
	char vdd[PNL_SIZE_VDD_STR];
};

static struct panel_info *panel_info_cache = NULL;

static struct spi_device *panel_flash_spi = NULL;
static bool   spi_registered = false;
static int    panel_readonly = 1;

static int    panel_flash_probe(struct spi_device *spi);
static int    panel_flash_remove(struct spi_device *spi);
static int    panel_get_info(struct panel_info **panel, bool header_only);
static int    panel_read_from_flash(unsigned long addr, unsigned char *data, unsigned long size);
int           panel_program_flash(unsigned long addr, unsigned char *buffer, unsigned long blen);
static void   panel_data_translate(u8 *buffer, int to_read, bool strip);
bool          panel_flash_present(void);
static struct update_modes *panel_get_upd_modes(struct mxc_epdc_fb_data *fb_data);
static struct imx_epdc_fb_mode * panel_choose_fbmode(struct mxc_epdc_fb_data *fb_data);

extern int max77696_epd_set_vddh(int vddh_mV);
extern void epdc_iomux_config_lve(void);

/***********
 *   MXC   *
 ***********/

#define mV_to_uV(mV)        ((mV) * 1000)
#define uV_to_mV(uV)        ((uV) / 1000)
#define V_to_uV(V)          (mV_to_uV((V) * 1000))
#define uV_to_V(uV)         (uV_to_mV(uV) / 1000)

static int proc_panel_readonly_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_panel_readonly_write(struct file *file, const char __user *buf, unsigned long count, void *data);
static int proc_panel_data_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_panel_data_write(struct file *file, const char __user *buf, unsigned long count, void *data);
static int proc_panel_bcd_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_panel_id_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_panel_wfm_version_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_panel_wfm_human_version_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_panel_wfm_embedded_checksum_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_panel_wfm_computed_checksum_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_panel_wfm_info_read(char *page, char **start, off_t off, int count, int *eof, void *data);





/******************************************************************************
 ******************************************************************************
 **                                                                          **
 **                            Utility Functions                             **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/

/*
 * CRC-32 algorithm from:
 *  <http://glacier.lbl.gov/cgi-bin/viewcvs.cgi/dor-test/crc32.c?rev=HEAD>
 */

/* Table of CRCs of all 8-bit messages. */
static unsigned long crc_table[256];

/* Flag: has the table been computed? Initially false. */
static int crc_table_computed = 0;

/* Make the table for a fast CRC. */
static void make_crc_table(void)
{
	unsigned long c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (unsigned long) n;
		for (k = 0; k < 8; k++) {
			if (c & 1)
				c = 0xedb88320L ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc_table[n] = c;
	}
	crc_table_computed = 1;
}

/*
 * Update a running crc with the bytes buf[0..len-1] and return
 * the updated crc. The crc should be initialized to zero. Pre- and
 * post-conditioning (one's complement) is performed within this
 * function so it shouldn't be done by the caller. Usage example:
 *
 *   unsigned long crc = 0L;
 *
 *   while (read_buffer(buffer, length) != EOF) {
 *     crc = update_crc(crc, buffer, length);
 *   }
 *   if (crc != original_crc) error();
 */
static unsigned long update_crc(unsigned long crc, unsigned char *buf, int len)
{
	unsigned long c = crc ^ 0xffffffffL;
	int n;

	if (!crc_table_computed)
		make_crc_table();
	for (n = 0; n < len; n++)
		c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);

	return c ^ 0xffffffffL;
}

/* Return the CRC of the bytes buf[0..len-1]. */
static unsigned long crc32(unsigned char *buf, int len)
{
	return update_crc(0L, buf, len);
}

/* Return the sum of the bytes buf[0..len-1]. */
unsigned sum32(unsigned char *buf, int len)
{
	unsigned c = 0;
	int n;

	for (n = 0; n < len; n++)
		c += buf[n];

	return c;
}

/* Return the sum of the bytes buf[0..len-1]. */
unsigned char sum8(unsigned char *buf, int len)
{
	unsigned char c = 0;
	int n;

	for (n = 0; n < len; n++)
		c += buf[n];

	return c;
}


/*
** This is a hack: because procfs doesn't support large write operations,
** this function gets called multiple times (in 4KB chunks). Each time a
** large write is requested, fcount is reset to some "random" value. By
** setting it to the MAGIC_TOKEN, we are able to figure out which
** invocations belong together and keep track of the offset.
*/

#define MAGIC_TOKEN 0x42975623

static int panel_data_write(unsigned long base_address, struct file *file, const char __user *buf, unsigned long count, void *data)
{
#ifdef DEVELOPMENT_MODE
	int result = count;
	unsigned char *buffer;
	unsigned long *offset = data;
	long fcount = atomic_long_read(&file->f_count);

	if (panel_readonly) {
		printk(KERN_ERR "Panel flash read-only\n");
		return -EFAULT;
	}

	buffer = kmalloc(sizeof(unsigned char) * count, GFP_KERNEL);

	if (!buffer)
		return -ENOMEM;

	if (copy_from_user((void *)buffer, buf, count)) {
		result = -EFAULT;
		goto cleanup;
	}

	if (fcount != MAGIC_TOKEN) {
		atomic_set(&file->f_count, MAGIC_TOKEN);
		*offset = base_address;
	}

	pr_debug("fcount: %ld  data: %p (%ld)  count: %ld",
	         fcount, data, *offset, count);

	if (panel_program_flash(*offset, buffer, count))
		result = -EREMOTEIO;

	*offset += count;

cleanup:
	if (buffer)
		kfree(buffer);

	return result;
#else
	printk(KERN_ERR "Panel flash read-only\n");
	return -EFAULT;
#endif
}

static int gunzip(unsigned char *dst, size_t dst_len, size_t *write_len, const unsigned char *src, size_t src_len)
{
	z_stream stream;
	int i;
	int flags;
	int ret = 0;
	static void *z_inflate_workspace = NULL;

	z_inflate_workspace = kzalloc(zlib_inflate_workspacesize(), GFP_ATOMIC);
	if (z_inflate_workspace == NULL) {
		printk(KERN_ERR "%s: error: gunzip failed to allocate workspace\n", __func__);
		return -ENOMEM;
	}

	/* skip header */
	i = 10;
	flags = src[3];
	if (src[2] != GUNZIP_DEFLATED || (flags & GUNZIP_RESERVED) != 0) {
		printk(KERN_ERR "%s: error: Bad gzipped data\n", __func__);
		ret = -EINVAL;
		goto cleanup;
	}

	if ((flags & GUNZIP_EXTRA_FIELD) != 0)
		i = 12 + src[10] + (src[11] << 8);

	if ((flags & GUNZIP_ORIG_NAME) != 0)
		while (src[i++] != 0);

	if ((flags & GUNZIP_COMMENT) != 0)
		while (src[i++] != 0);

	if ((flags & GUNZIP_HEAD_CRC) != 0)
		i += 2;

	if (i >= src_len) {
		printk(KERN_ERR "%s: error: gunzip out of data in header\n", __func__);
		ret = -EINVAL;
		goto cleanup;
	}

	stream.workspace = z_inflate_workspace;
	ret = zlib_inflateInit2(&stream, -MAX_WBITS);
	if (ret != Z_OK) {
		printk(KERN_ERR "%s: error: zlib_inflateInit2() failed (%d)\n", __func__, ret);
		goto cleanup;
	}
	stream.next_in = src + i;
	stream.avail_in = src_len - i;
	stream.next_out = dst;
	stream.avail_out = dst_len;
	ret = zlib_inflate(&stream, Z_FINISH);
	if (ret != Z_OK && ret != Z_STREAM_END) {
		printk(KERN_ERR "%s: error: zlib_inflate() failed (%d)\n", __func__, ret);
		goto cleanup;
	}
	*write_len = dst_len - stream.avail_out;
	zlib_inflateEnd(&stream);

cleanup:
	kfree(z_inflate_workspace);
	return ret;
}




/******************************************************************************
 ******************************************************************************
 **                                                                          **
 **                              eInk Functions                              **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/

unsigned long eink_get_computed_waveform_checksum(u8 *wf_buffer)
{
	unsigned long checksum = 0;

	if (wf_buffer) {
		struct waveform_data_header *header = (struct waveform_data_header *)wf_buffer;
		unsigned long filesize = header->file_length;

		if (filesize) {
			unsigned long saved_embedded_checksum;

			// Save the buffer's embedded checksum and then set it zero.
			//
			saved_embedded_checksum = header->checksum;
			header->checksum = 0;

			// Compute the checkum over the entire buffer, including
			// the zeroed-out embedded checksum area, and then restore
			// the embedded checksum.
			//
			checksum = crc32((unsigned char *)wf_buffer, filesize);
			header->checksum = saved_embedded_checksum;
		} else {
			unsigned char checksum1, checksum2;
			int start, length;

			// Checksum bytes 0..(EINK_ADDR_CHECKSUM1 - 1).
			//
			start     = 0;
			length    = EINK_ADDR_CHECKSUM1;
			checksum1 = sum8((unsigned char *)wf_buffer + start, length);

			// Checksum bytes (EINK_ADDR_CHECKSUM1 + 1)..(EINK_ADDR_CHECKSUM2 - 1).
			//
			start     = EINK_ADDR_CHECKSUM1 + 1;
			length    = EINK_ADDR_CHECKSUM2 - start;
			checksum2 = sum8((unsigned char *)wf_buffer + start, length);

			checksum  = EINK_CHECKSUM(checksum1, checksum2);
		}
	}

	return checksum;
}

void eink_get_waveform_info(u8 *wf_buffer, struct eink_waveform_info_t *info)
{
	struct waveform_data_header *header = (struct waveform_data_header *)wf_buffer;

	if (info) {
		info->waveform.version         = header->wf_version;
		info->waveform.subversion      = header->wf_subversion;
		info->waveform.type            = header->wf_type;
		info->waveform.run_type        = header->run_type;
		info->fpl.platform             = header->fpl_platform;
		info->fpl.size                 = header->panel_size;
		info->fpl.adhesive_run_number  = header->fpl_lot;
		info->waveform.mode_version    = header->mode_version;
		info->waveform.mfg_code        = header->amepd_part_number;
		info->waveform.bit_depth       = ((header->luts & 0xC) == 0x4) ? 5 : 4;
		info->waveform.vcom_shift      = header->vcom_shifted;

		if (info->waveform.type == EINK_WAVEFORM_TYPE_WJ) {
			info->waveform.tuning_bias = header->wf_revision;
		} else if (info->waveform.type == EINK_WAVEFORM_TYPE_WR) {
			info->waveform.revision    = header->wf_revision;
		} else {
			info->waveform.revision    = header->wf_revision;
			info->waveform.awv         = header->advanced_wfm_flags;
		}

		info->waveform.fpl_rate        = header->frame_rate;

		info->fpl.lot                  = header->fpl_lot;

		info->checksum                 = header->checksum;
		info->filesize                 = header->file_length;
		info->waveform.serial_number   = header->serial_number;

		/* XWIA is only 3 bytes */
		info->waveform.xwia            = header->xwia;

		if (0 == info->filesize) {
			info->checksum = EINK_CHECKSUM(header->cs1, header->cs2);
			info->waveform.parse_wf_hex  = false;
		} else {
			info->waveform.parse_wf_hex  = false;
		}

		pr_debug(   "\n"
		            " Waveform version:  0x%02X\n"
		            "       subversion:  0x%02X\n"
		            "             type:  0x%02X (v%02d)\n"
		            "         run type:  0x%02X\n"
		            "     mode version:  0x%02X\n"
		            "      tuning bias:  0x%02X\n"
		            "       frame rate:  0x%02X\n"
		            "       vcom shift:  0x%02X\n"
		            "        bit depth:  0x%02X\n"
		            "\n"
		            "     FPL platform:  0x%02X\n"
		            "              lot:  0x%04X\n"
		            "             size:  0x%02X\n"
		            " adhesive run no.:  0x%02X\n"
		            "\n"
		            "        File size:  0x%08lX\n"
		            "         Mfg code:  0x%02X\n"
		            "       Serial no.:  0x%08lX\n"
		            "         Checksum:  0x%08lX\n",

		            info->waveform.version,
		            info->waveform.subversion,
		            info->waveform.type,
		            info->waveform.revision,
		            info->waveform.run_type,
		            info->waveform.mode_version,
		            info->waveform.tuning_bias,
		            info->waveform.fpl_rate,
		            info->waveform.vcom_shift,
		            info->waveform.bit_depth,

		            info->fpl.platform,
		            info->fpl.lot,
		            info->fpl.size,
		            info->fpl.adhesive_run_number,

		            info->filesize,
		            info->waveform.mfg_code,
		            info->waveform.serial_number,
		            info->checksum);
    }
}

bool eink_waveform_valid(u8 *wf_buffer)
{
	struct eink_waveform_info_t info;
	struct panel_info *panel_info;
	int result;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return false;
	}

	eink_get_waveform_info(wf_buffer, &info);

	if (info.filesize <= WFM_HDR_SIZE || info.filesize > panel_info->addrs->waveform_len) {
		printk(KERN_ERR "eink_fb_waveform: E invalid:Invalid filesize in waveform header:\n");
		return false;
	}

	return true;
}

int mxc_epdc_fb_vc_convert_to_volts(struct epd_vc_data *vcd, struct epd_vc_data_volts *vcdv)
{
	vcdv->vpos_v = (vcd->vpos <= EPDC_VC_MAX) ? (vcd->vpos * EPDC_VC_VPOS_STEP) : 0;
	vcdv->vneg_v = (vcd->vneg <= EPDC_VC_MAX) ? (vcd->vneg * EPDC_VC_VNEG_STEP) : 0;
	vcdv->vddh_v = (vcd->vddh <= EPDC_VC_MAX) ? (vcd->vddh * EPDC_VC_VDDH_STEP) : 0;
	vcdv->vee_v  = (vcd->vee <= EPDC_VC_MAX) ?(vcd->vee  * EPDC_VC_VEE_STEP) : 0;
	vcdv->vcom_v = 0;

	if (vcd->vcom_offset >= EPDC_VC_VCOM_OFFSET_POS_MIN && vcd->vcom_offset <= EPDC_VC_VCOM_OFFSET_POS_MAX)
		vcdv->vcom_v = vcd->vcom_offset * EPDC_VC_VCOM_OFFSET_POS_STEP;
	else if (vcd->vcom_offset >= EPDC_VC_VCOM_OFFSET_NEG_MIN && vcd->vcom_offset <= EPDC_VC_VCOM_OFFSET_NEG_MAX)
		vcdv->vcom_v = (vcd->vcom_offset - EPDC_VC_VCOM_OFFSET_NEG_MIN) * EPDC_VC_VCOM_OFFSET_NEG_STEP;

	return 0;
}

/* function for copying the voltage control data to the allocated buffer */
int mxc_epdc_fb_fetch_vc_data( unsigned char *waveform_vcd_buffer,
		unsigned w_mode,
		unsigned w_temp_range,
		unsigned waveform_mc,
		unsigned waveform_trc,
		unsigned char *voltage_ctrl_data)
{
	if ((w_mode >= waveform_mc) || (w_temp_range >= waveform_trc))
		return -1;
	/* copy the waveform voltage control data */
	memcpy(voltage_ctrl_data, waveform_vcd_buffer + sizeof(struct epd_vc_data) * (w_mode * waveform_trc + w_temp_range), sizeof(struct epd_vc_data));
	{
		/* Verify checksum */
		unsigned i;
		u8 cs = 0;
		for (i=0; i< (sizeof(struct epd_vc_data) - 1); i++)
			cs += voltage_ctrl_data[i];
		if (cs != voltage_ctrl_data[sizeof(struct epd_vc_data) - 1])  return -1;
	}
	return 0;
}

int fetch_epdc_pmic_voltages( struct epd_vc_data_volts *vcdv, struct mxc_epdc_fb_data *fb_data,
		u32 w_mode,
		u32 w_temp_range)
{
	struct epd_vc_data vcd;

	/* fetch and display the voltage control data  */
	if (!fb_data->waveform_vcd_buffer)
		return -1;

	/* fetch the voltage control data */
	if (mxc_epdc_fb_fetch_vc_data( fb_data->waveform_vcd_buffer, w_mode, w_temp_range, fb_data->waveform_mc, fb_data->waveform_trc, (unsigned char *)&vcd) < 0)
		dev_err(fb_data->dev, " *** Extra Waveform Data checksum error ***\n");
	else
		dev_dbg(fb_data->dev, " -- VC Data version 0x%04x : vpos = 0x%04x, vneg = 0x%04x, vddh = 0x%04x, vee = 0x%04x, vcom_offset = 0x%04x, unused_0 = 0x%04x, unused_1 = 0x%02x --\n",
				vcd.version, vcd.vpos, vcd.vneg, vcd.vddh, vcd.vee, vcd.vcom_offset, vcd.unused_0, vcd.unused_1 );

	mxc_epdc_fb_vc_convert_to_volts(&vcd, vcdv);
	return 0;
}

int eink_get_wfm_voltage_control(struct waveform_data_header *wv_header, u8 *wf_buffer)
{
	int wfm_mode_count;

	u64 longOffset; // Address of the first waveform
	u64 xwiOffset;
	u64 avcOffset;
	u64 tempOffset;

	wfm_mode_count = wv_header->mc + 1;

	longOffset = __le64_to_cpu(((uint64_t *)wf_buffer)[0]);
	if (longOffset <= (sizeof(u64) * wfm_mode_count))
		goto error;

	if (wv_header->advanced_wfm_flags > 3)
		goto error;

	if (wv_header->xwia > 0)
		xwiOffset = __le64_to_cpu(((uint64_t *)wf_buffer)[wfm_mode_count + wv_header->advanced_wfm_flags]);

	switch(wv_header->advanced_wfm_flags) {
		case 0:
			/* No voltage control information */
			break;
		case 1:
			/* Voltage control format V2 */
			avcOffset  = __le64_to_cpu(((uint64_t *)wf_buffer)[wfm_mode_count]);
			tempOffset = __le64_to_cpu(((uint64_t *)wf_buffer)[wfm_mode_count + 1]);
			if (!g_fb_data->waveform_vcd_buffer)
				g_fb_data->waveform_vcd_buffer = kmalloc((tempOffset - avcOffset), GFP_KERNEL);
			memcpy((void *)g_fb_data->waveform_vcd_buffer, &wf_buffer[avcOffset], (tempOffset - avcOffset));
			break;
		case 2:
			/* Algorithm Control information only */
			break;
		case 3:
			/* Voltage Control and Algorithm Control */
			avcOffset  = __le64_to_cpu(((uint64_t *)wf_buffer)[wfm_mode_count]);
			tempOffset = __le64_to_cpu(((uint64_t *)wf_buffer)[wfm_mode_count + 1]);
			if (!g_fb_data->waveform_vcd_buffer)
				g_fb_data->waveform_vcd_buffer = kmalloc((tempOffset - avcOffset), GFP_KERNEL);
			memcpy((void *)g_fb_data->waveform_vcd_buffer, &wf_buffer[avcOffset], (tempOffset - avcOffset));
			break;
		default:
			printk(KERN_ERR "Illegal advanced_wfm_flag value %d in waveform\n", wv_header->advanced_wfm_flags);
			goto error;
	}

	g_fb_data->waveform_mc = wfm_mode_count;
	g_fb_data->waveform_trc = wv_header->trc + 1;

	if (g_fb_data->waveform_vcd_buffer) {
		printk(KERN_INFO "Retrieved voltage control information\n");
	}
	return 0;
error:
	return -1;
}

char *eink_get_wfm_human_version(struct waveform_data_header *wv_header, u8 *wf_buffer, size_t wf_buffer_len, char *str, size_t str_len)
{
	int wfm_mode_count;
	int len;

	u64 longOffset;  // Address of the first waveform
	u64 xwiOffset;
	u8 *waveform_xwi_buffer = NULL;
	u8 waveform_xwi_len = 0;
	wfm_mode_count = wv_header->mc + 1;

	longOffset = __le64_to_cpu(((uint64_t *)wf_buffer)[0]);
	if (longOffset <= (sizeof(u64) * wfm_mode_count))
		goto error;

	if (wv_header->advanced_wfm_flags > 3)
		goto error;

	xwiOffset = __le64_to_cpu(((uint64_t *)wf_buffer)[wfm_mode_count + wv_header->advanced_wfm_flags]);
	waveform_xwi_len = wf_buffer[xwiOffset];
	waveform_xwi_buffer = wf_buffer + xwiOffset + 1;

	len = ((str_len - 1) < waveform_xwi_len) ? str_len - 1 : waveform_xwi_len;
	memmove(str, waveform_xwi_buffer, len);
	str[len] = '\0';
	return str;

error:
	snprintf(str, str_len, "?????");
	return str;
}

char *eink_get_pnl_wfm_human_version(u8 *wf_buffer, size_t wf_buffer_len, char *str, size_t str_len)
{
	struct eink_waveform_info_t info;
	struct panel_info *panel_info;
	int ret;
	u8 buf[str_len];
	u8 len = str_len - 1;

	eink_get_waveform_info(wf_buffer, &info);

	pr_debug("%s: reading embedded filename\n", __func__);

	/* Make sure there is a pointer to XWIA area */
	if (!info.waveform.xwia) {
		str[0] = '\0';
		return str;
	}

	// Check to see if the XWI is contained within the buffer
	if ((info.waveform.xwia >= wf_buffer_len) ||
			((wf_buffer[info.waveform.xwia] + info.waveform.xwia) >= wf_buffer_len)) {
		if (!panel_flash_present())
			goto error;

		if ((ret = panel_get_info(&panel_info, true))) {
			printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, ret);
			goto error;
		}

		if (panel_read_from_flash((panel_info->addrs->waveform_addr + info.waveform.xwia), buf, str_len)) {
			printk(KERN_ERR "Error reading from panel flash!\n");
			goto error;
		}

		// XWI[0] is the XWI length
		if (buf[0] < len)
			len = buf[0];

		memmove(str, buf + 1, len);
		str[len] = '\0';
	} else {
		if (wf_buffer[info.waveform.xwia] < len)
			len = wf_buffer[info.waveform.xwia];

		memmove(str, wf_buffer + info.waveform.xwia + 1, len);
		str[len] = '\0';
	}

	return str;

error:
	snprintf(str, str_len, "?????");
	return str;
}

char *eink_get_wfm_version(u8 *wf_buffer, char *version_string, size_t version_string_len)
{
	struct eink_waveform_info_t info;

	eink_get_waveform_info(wf_buffer, &info);

	// Build up a waveform version string in the following way:
	//
	//      <FPL PLATFORM>_<RUN TYPE>_<FPL LOT NUMBER>_<FPL SIZE>_
	//      <WF TYPE><WF VERSION><WF SUBVERSION>_
	//      (<WAVEFORM REV>|<TUNING BIAS>)_<MFG CODE>_<S/N>_<FRAME RATE>_MODEVERSION
	snprintf(version_string,
	        version_string_len,
	        "%02x_%02x_%04x_%02x_%02x%02x%02x_%02x_%02x_%08x_%02x_%02x",
	        info.fpl.platform,
	        info.waveform.run_type,
	        info.fpl.lot,
	        info.fpl.size,
	        info.waveform.type,
	        info.waveform.version,
	        info.waveform.subversion,
	        (info.waveform.type == EINK_WAVEFORM_TYPE_WR) ? info.waveform.revision : info.waveform.tuning_bias,
	        info.waveform.mfg_code,
	        (unsigned int) info.waveform.serial_number,
	        info.waveform.fpl_rate,
	        info.waveform.mode_version);

	return version_string;
}




/******************************************************************************
 ******************************************************************************
 **                                                                          **
 **                            Waveform Functions                            **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/

// Test waveform only
struct update_modes panel_mode_00 = {
		.init = { .mode = 0, .name = "init" },
		.du   = { .mode = 1, .name = "du" },
		.gc16 = { .mode = 2, .name = "gc16_fast" },
		.gcf  = { .mode = 2, .name = "gc16_fast" },
		.gl16 = { .mode = 3, .name = "gl16_fast" },
		.glf  = { .mode = 3, .name = "gl16_fast" },
		.a2   = { .mode = 6, .name = "a2" },
		.du4  = { .mode = 7, .name = "gl4" },
		.gl4  = { .mode = 7, .name = "gl4" },
		.glr  = { .mode = 4, .name = "reagl" },
		.glrd = { .mode = 5, .name = "reagld" },
		.gldk = { .mode = 3, .name = "gldk" },
		.glr4 = { .mode = 8, .name = "reagl4" },

};

struct update_modes panel_mode_07 = {
		.init = { .mode = 0, .name = "init" },
		.du   = { .mode = 1, .name = "du" },
		.gc16 = { .mode = 2, .name = "gc16" },
		.gcf  = { .mode = 3, .name = "gc16_fast" },
		.gl16 = { .mode = 5, .name = "gl16" },
		.glf  = { .mode = 6, .name = "gl16_fast" },
		.a2   = { .mode = 4, .name = "a2" },
		.du4  = { .mode = 2, .name = "gc16" },
		.gl4  = { .mode = 6, .name = "gl16_fast" },
		.glr  = { .mode = 6, .name = "gl16_fast" },
		.glrd = { .mode = 3, .name = "gc16_fast" },
		.gldk = { .mode = 3, .name = "gc16_fast" },
		.glr4 = { .mode = 6, .name = "gl16_fast" },
};

struct update_modes panel_mode_18 = {
		.init = { .mode = 0, .name = "init" },
		.du   = { .mode = 1, .name = "du" },
		.gc16 = { .mode = 2, .name = "gc16" },
		.gcf  = { .mode = 3, .name = "gc16_fast" },
		.gl16 = { .mode = 5, .name = "gl16" },
		.glf  = { .mode = 6, .name = "gl16_fast" },
		.a2   = { .mode = 4, .name = "a2" },
		.du4  = { .mode = 7, .name = "du4" },
		.gl4  = { .mode = 7, .name = "du4" },
		.glr  = { .mode = 6, .name = "gl16_fast" },
		.glrd = { .mode = 3, .name = "gc16_fast" },
		.gldk = { .mode = 3, .name = "gc16_fast" },
		.glr4 = { .mode = 6, .name = "gl16_fast" },
};

struct update_modes panel_mode_19 = {
		.init = { .mode = 0, .name = "init" },
		.du   = { .mode = 1, .name = "du" },
		.gc16 = { .mode = 2, .name = "gc16_fast" },
		.gcf  = { .mode = 2, .name = "gc16_fast" },
		.gl16 = { .mode = 3, .name = "gl16_fast" },
		.glf  = { .mode = 3, .name = "gl16_fast" },
		.a2   = { .mode = 4, .name = "a2" },
		.du4  = { .mode = 2, .name = "gc16_fast" },
		.gl4  = { .mode = 3, .name = "gl16_fast" },
		.glr  = { .mode = 3, .name = "gl16_fast" },
		.glrd = { .mode = 2, .name = "gc16_fast" },
		.gldk = { .mode = 2, .name = "gc16_fast" },
		.glr4 = { .mode = 3, .name = "gl16_fast" },
};

struct update_modes panel_mode_24 = {
		.init = { .mode = 0, .name = "init" },
		.du   = { .mode = 1, .name = "du" },
		.gc16 = { .mode = 2, .name = "gc16_fast" },
		.gcf  = { .mode = 2, .name = "gc16_fast" },
		.gl16 = { .mode = 3, .name = "gl16_fast" },
		.glf  = { .mode = 3, .name = "gl16_fast" },
		.a2   = { .mode = 6, .name = "a2" },
		.du4  = { .mode = 2, .name = "gc16_fast" },
		.gl4  = { .mode = 3, .name = "gl16_fast" },
		.glr  = { .mode = 4, .name = "reagl" },
		.glrd = { .mode = 5, .name = "reagld" },
		.gldk = { .mode = 2, .name = "gc16_fast" },
		.glr4 = { .mode = 4, .name = "reagl" },
};

struct update_modes panel_mode_25 = {
		.init = { .mode = 0, .name = "init" },
		.du   = { .mode = 1, .name = "du" },
		.gc16 = { .mode = 2, .name = "gc16_fast" },
		.gcf  = { .mode = 2, .name = "gc16_fast" },
		.gl16 = { .mode = 3, .name = "gl16_fast" },
		.glf  = { .mode = 3, .name = "gl16_fast" },
		.a2   = { .mode = 6, .name = "a2" },
		.du4  = { .mode = 7, .name = "du4" },
		.gl4  = { .mode = 7, .name = "du4" },
		.glr  = { .mode = 4, .name = "reagl" },
		.glrd = { .mode = 5, .name = "reagld" },
		.gldk = { .mode = 2, .name = "gc16_fast" },
		.glr4 = { .mode = 4, .name = "reagl" },
};

struct panel_addrs waveform_addrs_WJ = {
	.cmd_sec_addr  = 0x00000,
	.waveform_addr = 0x00886,
	.pnl_info_addr = 0x30000,
	.test_sec_addr = 0x3E000,
	.cmd_sec_len   = 0x00886 - 0x00000,
	.waveform_len  = 0x30000 - 0x00886,
	.pnl_info_len  = 0x3E000 - 0x30000,
	.test_sec_len  = 0x40000 - 0x3E000,
	.flash_end     = 0x40000,
};


struct panel_addrs waveform_addrs_WR = {
	.cmd_sec_addr  = 0x00000,
	.waveform_addr = 0x00886,
	.pnl_info_addr = 0x30000,
	.test_sec_addr = 0x3E000,
	.cmd_sec_len   = 0x00886 - 0x00000,
	.waveform_len  = 0x30000 - 0x00886,
	.pnl_info_len  = 0x3E000 - 0x30000,
	.test_sec_len  = 0x40000 - 0x3E000,
	.flash_end     = 0x40000,
};

struct panel_addrs waveform_addrs_AA_AC_AE_BD = {
	.cmd_sec_addr  = 0x00000,
	.waveform_addr = 0x00886,
	.pnl_info_addr = 0x70000,
	.test_sec_addr = 0x7E000,
	.cmd_sec_len   = 0x00886 - 0x00000,
	.waveform_len  = 0x70000 - 0x00886,
	.pnl_info_len  = 0x7E000 - 0x70000,
	.test_sec_len  = 0x80000 - 0x7E000,
	.flash_end     = 0x80000,
};

struct wf_proc_dir_entry {
	const char *name;
	mode_t mode;
	read_proc_t *read_proc;
	write_proc_t *write_proc;
	struct proc_dir_entry *proc_entry;
};

static struct wf_proc_dir_entry wfm_proc_entries[] = {
	// TODO ALEX Write
	{ "data", S_IRUGO, proc_wfm_data_read, NULL, NULL },
	{ "version", S_IRUGO, proc_wfm_version_read, NULL, NULL },
	{ "human_version", S_IRUGO, proc_wfm_human_version_read, NULL, NULL },
	{ "embedded_checksum", S_IRUGO, proc_wfm_embedded_checksum_read, NULL, NULL },
	// TODO ALEX computed checksum is not correct
	{ "computed_checksum", S_IRUGO, proc_wfm_computed_checksum_read, NULL, NULL },
	{ "info", S_IRUGO, proc_wfm_info_read, NULL, NULL },
	{ "source", S_IRUGO, proc_wfm_source_read, NULL, NULL },
};

static struct proc_dir_entry *proc_wf_parent            = NULL;
static struct proc_dir_entry *proc_wf_panel_parent      = NULL;
static struct proc_dir_entry *proc_wf_panel_wfm_parent  = NULL;
static struct proc_dir_entry *proc_wf_waveform_parent   = NULL;

static char *wfm_get_embedded_checksum(u8 *wf_buffer, char *cksum_str, size_t cksum_str_len)
{
	unsigned long embedded_checksum = 0;
	struct waveform_data_header *header = (struct waveform_data_header *)wf_buffer;

	if (header) {
		if (header->file_length)
			embedded_checksum = header->checksum;
		else
			embedded_checksum = EINK_CHECKSUM(header->cs1, header->cs2);
	}

	snprintf(cksum_str, cksum_str_len, "0x%08lX", embedded_checksum);

	return cksum_str;
}

static char *wfm_get_computed_checksum(u8 *wf_buffer, char *cksum_str, size_t cksum_str_len)
{
	unsigned long computed_checksum = 0;

	computed_checksum = eink_get_computed_waveform_checksum(wf_buffer);
	snprintf(cksum_str, cksum_str_len, "0x%08lX", computed_checksum);

	return cksum_str;
}



static struct proc_dir_entry *create_wf_proc_entry(const char *name,
                                                   mode_t mode,
                                                   struct proc_dir_entry *parent,
                                                   read_proc_t *read_proc,
                                                   write_proc_t *write_proc)
{
	struct proc_dir_entry *wf_proc_entry = create_proc_entry(name, mode, parent);

	if (wf_proc_entry) {
		wf_proc_entry->data       = NULL;
		wf_proc_entry->read_proc  = read_proc;
		wf_proc_entry->write_proc = write_proc;
	}

	return wf_proc_entry;
}

static inline void remove_wf_proc_entry(const char *name,
                                        struct proc_dir_entry *entry,
                                        struct proc_dir_entry *parent)
{
	if (entry) {
		remove_proc_entry(name, parent);
		entry = NULL;
	}
}

static void set_waveform_modes(struct mxc_epdc_fb_data *fb_data)
{
	struct mxcfb_waveform_modes waveform_modes;
	struct update_modes *wf_upd_modes = panel_get_upd_modes(fb_data);

	// Tell the EPDC what the waveform update modes are, based on
	// what we've read back from the waveform itself.  These are
	// what is used when we tell the EPDC to use WF_UPD_MODE_AUTO.
	//
	waveform_modes.mode_init      = wf_upd_modes->init.mode;
	waveform_modes.mode_du        = wf_upd_modes->du.mode;
	waveform_modes.mode_gc4       = wf_upd_modes->gc16.mode;
	waveform_modes.mode_gc8       = wf_upd_modes->gc16.mode;
	waveform_modes.mode_gc16      = wf_upd_modes->gc16.mode;
	waveform_modes.mode_gc16_fast = wf_upd_modes->gcf.mode;
	waveform_modes.mode_gc32      = wf_upd_modes->gc16.mode;
	waveform_modes.mode_gl16      = wf_upd_modes->gl16.mode;
	waveform_modes.mode_gl16_fast = wf_upd_modes->glf.mode;
	waveform_modes.mode_a2        = wf_upd_modes->a2.mode;
	waveform_modes.mode_du4       = wf_upd_modes->du4.mode;
	waveform_modes.mode_reagl     = wf_upd_modes->glr.mode;
	waveform_modes.mode_reagld    = wf_upd_modes->glrd.mode;
	waveform_modes.mode_gl4       = wf_upd_modes->gl4.mode;
	waveform_modes.mode_gl16_inv  = wf_upd_modes->gldk.mode;

	mxc_epdc_fb_set_waveform_modes(&waveform_modes, (struct fb_info *) fb_data);
}

char * wfm_name_for_mode(struct mxc_epdc_fb_data *fb_data, int mode)
{
	struct update_modes *wf_upd_modes = panel_get_upd_modes(fb_data);
	if (mode == wf_upd_modes->init.mode) return wf_upd_modes->init.name;
	if (mode == wf_upd_modes->du.mode)   return wf_upd_modes->du.name;
	if (mode == wf_upd_modes->gc16.mode) return wf_upd_modes->gc16.name;
	if (mode == wf_upd_modes->gcf.mode)  return wf_upd_modes->gcf.name;
	if (mode == wf_upd_modes->gl16.mode) return wf_upd_modes->gl16.name;
	if (mode == wf_upd_modes->glf.mode)  return wf_upd_modes->glf.name;
	if (mode == wf_upd_modes->a2.mode)   return wf_upd_modes->a2.name;
	if (mode == wf_upd_modes->du4.mode)  return wf_upd_modes->du4.name;
	if (mode == wf_upd_modes->glr.mode)  return wf_upd_modes->glr.name;
	if (mode == wf_upd_modes->glrd.mode) return wf_upd_modes->glrd.name;
	if (mode == wf_upd_modes->gldk.mode) return wf_upd_modes->gldk.name;
	if (mode == wf_upd_modes->gl4.mode)  return wf_upd_modes->gl4.name;
	if (mode == wf_upd_modes->glr4.mode) return wf_upd_modes->glr4.name;
	if (mode == WAVEFORM_MODE_AUTO)      return "auto";
	return NULL;
}

static int proc_wfm_data_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	off_t length = 0;
	size_t waveform_size = g_fb_data->waveform_buffer_size + g_fb_data->wv_header_size;

	*start = page;

	if ((off < waveform_size) && count) {
		if (off < g_fb_data->wv_header_size) {
			length = min((off_t)g_fb_data->wv_header_size - off, (off_t)count);
			memcpy(page, (u8 *)g_fb_data->wv_header + off, length);
		} else {
			length = min((off_t)waveform_size - off, (off_t)count);
			memcpy(page, (u8 *)g_fb_data->waveform_buffer_virt + (off - g_fb_data->wv_header_size), length);
		}
		*eof = 0;
	} else {
		*eof = 1;
	}

	pr_debug("%s: off=%ld flsz=%ld count=%d eof=%d\n", __func__, off, length, count, *eof);

	return length;
}

static int proc_wfm_version_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;

	// We're done after one shot.
	if (0 == off) {
		char version_str[WAVEFORM_VERSION_STRING_MAX] = "";
		result = snprintf(page, count, "%s\n",
			eink_get_wfm_version((u8 *)g_fb_data->wv_header,
				version_str, WAVEFORM_VERSION_STRING_MAX));
		*eof = 1;
	}

	return result;
}

static int proc_wfm_human_version_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;

	// We're done after one shot.
	if ( 0 == off ) {
		char version_str[WAVEFORM_VERSION_STRING_MAX] = "";
		result = snprintf(page, count, "%s\n",
			eink_get_wfm_human_version(g_fb_data->wv_header, (u8 *)g_fb_data->waveform_buffer_virt, sizeof(struct waveform_data_header),
				version_str, WAVEFORM_VERSION_STRING_MAX));
		*eof = 1;
	}

	return result;
}

static int proc_wfm_embedded_checksum_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;

	// We're done after one shot.
	if (0 == off) {
		char checksum_str[CHECKSUM_STRING_MAX] = "";
		result = snprintf(page, count, "%s\n",
		                  wfm_get_embedded_checksum((u8 *)g_fb_data->wv_header,
		                             checksum_str, CHECKSUM_STRING_MAX));
		*eof = 1;
	}

	return result;
}

static int proc_wfm_computed_checksum_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;

	// TODO ALEX
	printk(KERN_ALERT "Note: The computed checksum is not accurate (Todo for Alex)\n");

	// We're done after one shot.
	//
	if (0 == off) {
		char checksum_str[CHECKSUM_STRING_MAX] = "";
		result = snprintf(page, count, "%s\n",
		                  wfm_get_computed_checksum((u8 *)g_fb_data->waveform_buffer_virt,
		                  checksum_str, CHECKSUM_STRING_MAX));
		*eof = 1;
	}

	return result;
}

static int proc_wfm_info_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;
	struct eink_waveform_info_t info;
	eink_get_waveform_info((u8 *)g_fb_data->wv_header, &info);

	if (0 == off) {
		result = sprintf(page,
		                 " Waveform version:  0x%02X\n"
		                 "       subversion:  0x%02X\n"
		                 "             type:  0x%02X (v%02d)\n"
		                 "         run type:  0x%02X\n"
		                 "     mode version:  0x%02X\n"
		                 "      tuning bias:  0x%02X\n"
		                 "       frame rate:  0x%02X\n"
		                 "       vcom shift:  0x%02X\n"
		                 "        bit depth:  0x%02X\n"
		                 "\n"
		                 "     FPL platform:  0x%02X\n"
		                 "              lot:  0x%04X\n"
		                 "             size:  0x%02X\n"
		                 " adhesive run no.:  0x%02X\n"
		                 "\n"
		                 "        File size:  0x%08lX\n"
		                 "         Mfg code:  0x%02X\n"
		                 "       Serial no.:  0x%08lX\n"
		                 "         Checksum:  0x%08lX\n",

		                 info.waveform.version,
		                 info.waveform.subversion,
		                 info.waveform.type,
		                 info.waveform.revision,
		                 info.waveform.run_type,
		                 info.waveform.mode_version,
		                 info.waveform.tuning_bias,
		                 info.waveform.fpl_rate,
		                 info.waveform.vcom_shift,
		                 info.waveform.bit_depth,

		                 info.fpl.platform,
		                 info.fpl.lot,
		                 info.fpl.size,
		                 info.fpl.adhesive_run_number,

		                 info.filesize,
		                 info.waveform.mfg_code,
		                 info.waveform.serial_number,
		                 info.checksum);
		*eof = 1;
	}

	return result;
}

static int proc_wfm_source_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;

	if (off == 0) {
		result = sprintf(page, "%s\n", (wfm_using_builtin ? "built-in" : "stored"));
		*eof = 1;
	}

	return result;
}

static int proc_working_buffer_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	size_t buffer_size = g_fb_data->working_buffer_size;
	size_t ret_len = min((off_t)buffer_size - off, (off_t)count);

	*start = page;

	if (off < buffer_size) {
		*eof = 0;
		memcpy(page, ((char *)(g_fb_data->working_buffer_A_virt))+off, ret_len);

		return ret_len;
	} else {
		*eof = 1;
		return 0;
	}
}


/******************************************************************************
 ******************************************************************************
 **                                                                          **
 **                             Panel Functions                              **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/

/*
 * Decode panel data
 */

enum panel_data_characters {
	zero = 0x0, one, two, three, four, five, six, seven, eight, nine,
	underline = 0x0a, dot = 0x0b, negative = 0x0c,
	_a = 0xcb, _b, _c, _d, _e, _f, _g, _h, _i, _j, _k, _l, _m, _n,
	           _o, _p, _q, _r, _s, _t, _u, _v, _w, _x, _y, _z,

	_A = 0xe5, _B, _C, _D, _E, _F, _G, _H, _I, _J, _K, _L, _M, _N,
	           _O, _P, _Q, _R, _S, _T, _U, _V, _W, _X, _Y, _Z
};

struct fbmode_override {
	char *barcode_prefix;
	int vmode_index;
	int vddh;
	int lve;
};

struct fbmode_override fbmode_overrides[] = {
	{
		.barcode_prefix = "EC3",
		.vmode_index = PANEL_MODE_EN060TC1_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{ /* 1448x1072 85Hz G&T */
		.barcode_prefix = "EDG",
		.vmode_index = PANEL_MODE_EN060TC1_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{ /* 1448x1072 85Hz Icewine */
		.barcode_prefix = "ED4",
		.vmode_index = PANEL_MODE_ED060TC1_3CE,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "ED5",
		.vmode_index = PANEL_MODE_ED060TC1_3CE,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EB3",
		.vmode_index = PANEL_MODE_ED060TC1_3CE,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EB4",
		.vmode_index = PANEL_MODE_ED060TC1_3CE,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EB6",
		.vmode_index = PANEL_MODE_ED060TC1_3CE,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EBA",
		.vmode_index = PANEL_MODE_ED060TC1_3CE,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EBF",
		.vmode_index = PANEL_MODE_ED060TC1_3CE,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EDH",
		.vmode_index = PANEL_MODE_ED060TC1_3CE,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EE1",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EE2",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EE3",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EEB",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EGS",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJJ",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJK",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJL",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJM",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EQ3",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EQ4",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJN",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJQ",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJR",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJS",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "EJT",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "ENY",
		.vmode_index = PANEL_MODE_ED060TC1_3CE_CARTA_1_2,
		.vddh = 25000,
		.lve = 1
	},
	{
		.barcode_prefix = "S1V",
		.vmode_index = PANEL_MODE_EN060OC1_3CE_225,
		.vddh = 22000,
		.lve = 0
	}, /* 600x800 85Hz Sauza */
	{
		.barcode_prefix = "E6T",
		.vmode_index = PANEL_MODE_ED060SCN,
		.vddh = 22000,
		.lve = 0
	},
	{
		.barcode_prefix = "E6U",
		.vmode_index = PANEL_MODE_ED060SCN,
		.vddh = 22000,
		.lve = 0
	},
	{
		.barcode_prefix = "E6V",
		.vmode_index = PANEL_MODE_ED060SCN,
		.vddh = 22000,
		.lve = 0
	},
	{
		.barcode_prefix = "E6S",
		.vmode_index = PANEL_MODE_ED060SCN,
		.vddh = 22000,
		.lve = 0
	},
	{
		.barcode_prefix = "E6R",
		.vmode_index = PANEL_MODE_ED060SCN,
		.vddh = 22000,
		.lve = 0
	},
	{ /* 600x800 85Hz Bourbon */
		.barcode_prefix = "EBR",
		.vmode_index = PANEL_MODE_ED060SCP,
		.vddh = 22000,
		.lve = 0
	},
	{
		.barcode_prefix = "EBV",
		.vmode_index = PANEL_MODE_ED060SCP,
		.vddh = 22000,
		.lve = 0
	},
	{
		.barcode_prefix = "EBU",
		.vmode_index = PANEL_MODE_ED060SCP,
		.vddh = 22000,
		.lve = 0
	},
	{
		.barcode_prefix = "EBS",
		.vmode_index = PANEL_MODE_ED060SCP,
		.vddh = 22000,
		.lve = 0
	},
	{
		.barcode_prefix = "EBT",
		.vmode_index = PANEL_MODE_ED060SCP,
		.vddh = 22000,
		.lve = 0
	},
};

static struct spi_driver panel_flash_driver = {
	.driver = {
		.name = "panel_flash_spi",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
	.probe = panel_flash_probe,
	.remove = panel_flash_remove,
};

char *panel_get_bcd(struct panel_info *panel)
{
	u8 bcd[PNL_SIZE_BCD] = { 0 };

	if (!panel_flash_present()) {
		panel->bcd[0] = '\0';
		return panel->bcd;
	}

	panel_read_from_flash(panel->addrs->pnl_info_addr + PNL_BASE_BCD, bcd, PNL_SIZE_BCD);
	panel_data_translate(bcd, PNL_SIZE_BCD, true);
	strncpy(panel->bcd, bcd, PNL_SIZE_BCD);
	panel->bcd[PNL_SIZE_BCD] = '\0';

	pr_debug("%s: panel bcd=%s\n", __FUNCTION__, panel->bcd);

	return panel->bcd;
}

char *panel_get_panel_info_version(struct panel_info *panel)
{
	u8 version[PNL_SIZE_VERSION] = { 0 };

	if (!panel_flash_present()) {
		panel->panel_info_version[0] = '\0';
		return panel->panel_info_version;
	}

	panel_read_from_flash(panel->addrs->pnl_info_addr + PNL_BASE_VERSION, version, PNL_SIZE_VERSION);
	panel_data_translate(version, PNL_SIZE_VERSION, true);
	strncpy(panel->panel_info_version, version, PNL_SIZE_VERSION);
	panel->panel_info_version[PNL_SIZE_VERSION] = '\0';

	pr_debug("%s: panel info version=%s\n", __FUNCTION__, panel->panel_info_version);
	return panel->panel_info_version;
}

char *panel_get_resolution(struct panel_info *panel)
{
	u8 resolution[PNL_SIZE_RESOLUTION] = { 0 };

	if (!panel_flash_present()) {
		panel->resolution[0] = '\0';
		return panel->resolution;
	}

	panel_read_from_flash(panel->addrs->pnl_info_addr + PNL_BASE_RESOLUTION, resolution, PNL_SIZE_RESOLUTION);
	panel_data_translate(resolution, PNL_SIZE_RESOLUTION, true);
	strncpy(panel->resolution, resolution, PNL_SIZE_RESOLUTION);
	panel->resolution[PNL_SIZE_RESOLUTION] = '\0';

	pr_debug("%s: panel resolution=%s\n", __FUNCTION__, panel->resolution);
	return panel->resolution;
}

char *panel_get_dimensions(struct panel_info *panel)
{
	u8 dimensions[PNL_SIZE_DIMENSIONS] = { 0 };

	if (!panel_flash_present()) {
		panel->dimensions[0] = '\0';
		return panel->dimensions;
	}

	panel_read_from_flash(panel->addrs->pnl_info_addr + PNL_BASE_DIMENSIONS, dimensions, PNL_SIZE_DIMENSIONS);
	panel_data_translate(dimensions, PNL_SIZE_DIMENSIONS, true);
	strncpy(panel->dimensions, dimensions, PNL_SIZE_DIMENSIONS);
	panel->dimensions[PNL_SIZE_DIMENSIONS] = '\0';

	pr_debug("%s: panel dimensions=%s\n", __FUNCTION__, panel->dimensions);
	return panel->dimensions;
}

char *panel_get_vdd(struct panel_info *panel)
{
	u8 vdd[PNL_SIZE_VDD] = { 0 };

	if (!panel_flash_present()) {
		panel->vdd[0] = '\0';
		return panel->vdd;
	}

	panel_read_from_flash(panel->addrs->pnl_info_addr + PNL_BASE_VDD, vdd, PNL_SIZE_VDD);
	panel_data_translate(vdd, PNL_SIZE_VDD, true);
	strncpy(panel->vdd, vdd, PNL_SIZE_VDD);
	panel->dimensions[PNL_SIZE_VDD] = '\0';

	pr_debug("%s: panel vdd=%s\n", __FUNCTION__, panel->vdd);
	return panel->vdd;
}


#define panel_spi_sync(spi, message, result, fmt, args...) \
({ \
	int _ret = 0; \
	if (spi_sync((spi), (message)) != 0 || (message)->status != 0) { \
		printk(KERN_ERR "%s: SPI Error (%d): "fmt, __func__, (message)->status, ##args); \
		*(result) = (message)->status; \
		_ret = 1; \
	} \
	_ret; \
})

static int panel_read_from_flash(unsigned long addr, unsigned char *data, unsigned long size)
{
	struct spi_device *spi = panel_flash_spi;
	unsigned long start = addr;
	struct spi_transfer t;
	struct spi_message m;
	u32 len, xfer_len, xmit_len, extra;
	u8 *tx_buf, *rx_buf, *xmit_buf;
	int ret, i;
	u32 *rcv_buf;
	size_t flash_size = DEFAULT_FLASH_SIZE;

	if (spi == NULL) {
		pr_debug("uninitialized!\n");
		return -1;
	}

	pr_debug("%s: start = 0x%lx, size = %ld dest=0x%x\n", __func__, start, size, (u32) data);

	/* We can't use panel_get_info() here because it depends on this function.
	 * If we have no cached info, use the default; otherwise, use the read value.
	 */
	if (panel_info_cache)
		flash_size = panel_info_cache->addrs->flash_end;

	if ((start + size) > flash_size) {
		pr_debug("Attempting to read off the end of flash, start = %ld, length %ld\n",
			start, size);
		return -1;
	}


	// BEN TODO - split into separate spi transfers
	tx_buf = kzalloc(MXC_SPI_MAX_CHARS, GFP_KERNEL);
	if (!tx_buf) {
		pr_debug("Can't alloc spi tx buffer, length %d\n", MXC_SPI_MAX_CHARS);
		return -1;
	}

	// BEN TODO - optimize this
	rx_buf = kzalloc(MXC_SPI_MAX_CHARS, GFP_KERNEL);
	if (!rx_buf) {
		pr_debug("Can't alloc spi rx buffer, length %d\n", MXC_SPI_MAX_CHARS);
		return -1;
	}

	len = size;
	xmit_buf = data;

	while (len > 0) {

		/* BEN TODO: fix hardcoded hack */
		xfer_len = ((len + SFM_READ_CMD_LEN) > MXC_SPI_MAX_CHARS) ?
			MXC_SPI_MAX_CHARS : (len + SFM_READ_CMD_LEN);

		/* handle small reads */
		if (xfer_len % 4) {
			extra = (4 - (xfer_len % 4));
			xfer_len += extra;
		} else {
			extra = 0;
		}

		spi->mode = SPI_MODE_0;
		spi->bits_per_word = (xfer_len * 8);
		spi_setup(spi);

		spi_message_init(&m);

		/* command is 1 byte, addr is 3 bytes, MSB first */
		tx_buf[3] = SFM_READ;
		tx_buf[2] = (start >> 16) & 0xFF;
		tx_buf[1] = (start >> 8) & 0xFF;
		tx_buf[0] = start & 0xFF;

		memset(&t, 0, sizeof(t));
		t.tx_buf = (const void *) tx_buf;
		t.rx_buf = (void *) rx_buf;
		t.len = xfer_len;


		spi_message_add_tail(&t, &m);
		if (panel_spi_sync(spi, &m, &ret, "reading"))
			break;

		if ((xfer_len - m.actual_length) != 0) {
			printk(KERN_ERR "only %d bytes sent\n", m.actual_length);
			ret = -1;
			break;
		}

		xmit_len = (xfer_len - SFM_READ_CMD_LEN - extra);
		rcv_buf = (u32 *)(rx_buf + SFM_READ_CMD_LEN);

		/* need to byteswap */
		for (i = 0; i < (xmit_len / 4); i++) {
			*((__u32 *) xmit_buf) = __swab32p(rcv_buf);
			xmit_buf += 4;
			rcv_buf++;
		}

		/* handle requests smaller than 4 bytes */
		if (extra) {
			for (i = 0; i < (4 - extra); i++) {
				((u8 *) xmit_buf)[i] = (rcv_buf[0] >> ((3 - i) * 8)) & 0xFF;
			}
		}

		start += xmit_len;
		len -= xmit_len;

		ret = 0;
	}

	kfree(tx_buf);
	kfree(rx_buf);

	return ret;
}

int panel_program_flash(unsigned long addr, unsigned char *buffer, unsigned long blen)
{
	struct panel_info *panel_info;
	struct spi_transfer transfer;
	struct spi_message message;
	u8 *sector_buf = NULL;
	u32 *tx_buf = NULL;
	u32 *rx_buf = NULL;
	u8 protected_flags = 0;
	u8 status = 0;
	int result = 0;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	if (panel_info->addrs->flash_end < (addr + blen)) {
		pr_debug("%s: Attempting to write off the end of flash, "
		         "addr = %ld, length %ld\n", __func__, addr, blen);
		result = -1;
		goto cleanup;
	}

	sector_buf = kmalloc(PNL_SECTOR_SIZE, GFP_KERNEL);
	if (!sector_buf) {
		pr_debug("%s: Can't alloc sector buffer, length %d\n",
		         __func__, PNL_SECTOR_SIZE);
		result = -1;
		goto cleanup;
	}

	tx_buf = kmalloc(MXC_SPI_MAX_CHARS, GFP_KERNEL);
	if (!tx_buf) {
		pr_debug("%s: Can't alloc spi tx buffer, length %d\n",
		         __func__, MXC_SPI_MAX_CHARS);
		result = -1;
		goto cleanup;
	}

	rx_buf = kmalloc(MXC_SPI_MAX_CHARS, GFP_KERNEL);
	if (!rx_buf) {
		pr_debug("%s: Can't alloc spi rx buffer, length %d\n",
		         __func__, MXC_SPI_MAX_CHARS);
		result = -1;
		goto cleanup;
	}

	memset(&transfer, 0, sizeof(transfer));
	transfer.tx_buf = (const void *)tx_buf;
	transfer.rx_buf = (void *)rx_buf;

	spi_message_init(&message);
	spi_message_add_tail(&transfer, &message);

	// Setup spi
	panel_flash_spi->mode = SPI_MODE_0;

	// Check the block protection bits
	tx_buf[0] = SFM_RDSR << 8;
	transfer.len = 1;
	panel_flash_spi->bits_per_word = 16;
	spi_setup(panel_flash_spi);

	if (panel_spi_sync(panel_flash_spi, &message, &result,
	                   "reading status register"))
		goto cleanup;
	protected_flags = rx_buf[0];

	// Clear the block protection bits if they are set
	if (protected_flags & (SFM_BP0_MASK | SFM_BP1_MASK)) {
		pr_debug("%s: Block protection bits have been set (0x%02X). "
		         "Clearing them to allow flashing.", __func__, protected_flags);

		// Enable writing
		tx_buf[0] = SFM_WREN;
		transfer.len = 1;
		panel_flash_spi->bits_per_word = 8;
		spi_setup(panel_flash_spi);

		if (panel_spi_sync(panel_flash_spi, &message, &result,
		                   "enabling writing"))
			goto cleanup;

		// Clear the block protection flags
		tx_buf[0] = SFM_WRSR << 8;
		transfer.len = 1;
		panel_flash_spi->bits_per_word = 16;
		spi_setup(panel_flash_spi);

		if (panel_spi_sync(panel_flash_spi, &message, &result,
		                   "writing status (0x%02X)", 0))
			goto cleanup;

		// Wait for the flash to finish its erase/write operation
		tx_buf[0] = SFM_RDSR << 8;
		transfer.len = 1;
		panel_flash_spi->bits_per_word = 16;
		spi_setup(panel_flash_spi);
		do {
			if (panel_spi_sync(panel_flash_spi, &message, &result,
			                   "reading status register"))
				goto cleanup;
			status = rx_buf[0];
		} while (status & SFM_WIP_MASK);
	}


	while (blen > 0) {
		// Calculate the sector boundaries
		unsigned long sector_start = (addr / PNL_SECTOR_SIZE) *
		                             PNL_SECTOR_SIZE;
		unsigned long sector_end = sector_start + PNL_SECTOR_SIZE;
		unsigned long wlen = (addr + blen < sector_end) ?
		                     blen : (sector_end - addr);
		unsigned long buf_offset = addr - sector_start;
		unsigned int tx_len = PNL_SECTOR_SIZE;
		unsigned int tx_offset = 0;
		unsigned int page_offset;
		int i;

		// Read sector into buffer
		pr_debug("%s: Starting on sector at 0x%08lX", __func__, sector_start);
		if (panel_read_from_flash(sector_start,
		                          (unsigned char *)sector_buf,
		                          PNL_SECTOR_SIZE)) {
			printk(KERN_ERR "%s: panel_read_from_flash() failed", __func__);
			result = -1;
			goto cleanup;
		}

		// Change the buffer
		pr_debug("%s: buf_offset: 0x%08lX  wlen: 0x%08lX"
		         "  addr: 0x%08lX  sector_start: 0x%08lX",
		         __func__, buf_offset, wlen, addr, sector_start);
		memcpy(sector_buf + buf_offset, buffer, wlen);

		// Byte swap the entire buffer
		for (i = 0; i < PNL_SECTOR_SIZE/4; i++)
			((u32 *)sector_buf)[i] = __swab32p((u32 *)sector_buf + i);

		// Enable writing
		tx_buf[0] = SFM_WREN;
		transfer.len = 1;
		panel_flash_spi->bits_per_word = 8;
		spi_setup(panel_flash_spi);

		if (panel_spi_sync(panel_flash_spi, &message, &result,
		                   "enabling writing"))
			goto cleanup;

		// Erase the sector
		tx_buf[0] = (SFM_SE << 24) | (sector_start & 0x00FFFFFF);
		transfer.len = 1;
		panel_flash_spi->bits_per_word = 32;
		spi_setup(panel_flash_spi);

		pr_debug("Erasing sector at 0x%08lX", sector_start);
		if (panel_spi_sync(panel_flash_spi, &message, &result,
		                   "erasing sector at 0x%08lX", sector_start))
			goto cleanup;

		// Write the sector
		page_offset = 0;
		while (tx_len > 0) {
			u8 extra;
			u8 xmit_len;
			u8 xfer_len = ((tx_len + SFM_WRITE_CMD_LEN) > MXC_SPI_MAX_CHARS) ?
			              MXC_SPI_MAX_CHARS : (tx_len + SFM_WRITE_CMD_LEN);

			// Make sure we don't write across page boundaries
			if (xfer_len - SFM_WRITE_CMD_LEN + tx_offset > PNL_PAGE_SIZE)
				xfer_len = PNL_PAGE_SIZE - tx_offset + SFM_WRITE_CMD_LEN;

			// Handle small reads
			if (xfer_len % 4) {
				extra = (4 - (xfer_len % 4));
				xfer_len += extra;
			} else {
				extra = 0;
			}

			// Wait for the flash to finish its erase/write operation
			tx_buf[0] = SFM_RDSR << 8;
			transfer.len = 1;
			panel_flash_spi->bits_per_word = 16;
			spi_setup(panel_flash_spi);
			do {
				if (panel_spi_sync(panel_flash_spi, &message, &result,
				                   "reading status register"))
					goto cleanup;

				status = rx_buf[0];
			} while (status & SFM_WIP_MASK);

			// Enable writing
			tx_buf[0] = SFM_WREN;
			transfer.len = 1;
			panel_flash_spi->bits_per_word = 8;
			spi_setup(panel_flash_spi);

			if (panel_spi_sync(panel_flash_spi, &message, &result,
			                   "enabling writing"))
				goto cleanup;

			// Transmit the chunk
			panel_flash_spi->bits_per_word = (xfer_len * 8);
			spi_setup(panel_flash_spi);

			tx_buf[0] = (SFM_PP << 24) |
			            ((sector_start + page_offset + tx_offset) & 0x00FFFFFF);
			memcpy(tx_buf + 1,
			       sector_buf + page_offset + tx_offset,
			       xfer_len - extra - SFM_WRITE_CMD_LEN);
			memset((u8 *)tx_buf + xfer_len - extra,
			       0xFF,
			       extra);

			transfer.len = xfer_len / 4;

			if (panel_spi_sync(panel_flash_spi, &message, &result,
			                   "writing page at 0x%08lX",
			                   sector_start + page_offset + tx_offset))
				goto cleanup;

			if ((xfer_len - (message.actual_length * 4)) != 0) {
				printk(KERN_ERR "%s: only %d bytes sent\n",
				       __func__, message.actual_length * 4);
				result = -1;
				goto cleanup;
			}

			xmit_len = (xfer_len - SFM_READ_CMD_LEN - extra);

			if (xmit_len + tx_offset == PNL_PAGE_SIZE) {
				page_offset += PNL_PAGE_SIZE;
				tx_offset = 0;
			} else {
				tx_offset += xmit_len;
			}
			tx_len -= xmit_len;
		}

		blen -= wlen;
		buffer += wlen;
		addr += wlen;
	}

cleanup:
	// Revert the block protection bits if they were set
	if (protected_flags & (SFM_BP0_MASK | SFM_BP1_MASK)) {
		pr_debug("%s: Reverting the block protection bits.", __func__);

		// Enable writing
		tx_buf[0] = SFM_WREN;
		transfer.len = 1;
		panel_flash_spi->bits_per_word = 8;
		spi_setup(panel_flash_spi);

		if (panel_spi_sync(panel_flash_spi, &message, &result,
		                   "enabling writing"))
			goto free;

		// Restore the block protection flags
		tx_buf[0] = SFM_WRSR << 8 | protected_flags;
		transfer.len = 1;
		panel_flash_spi->bits_per_word = 16;
		spi_setup(panel_flash_spi);

		if (panel_spi_sync(panel_flash_spi, &message, &result,
		                   "writing status (0x%02X)", protected_flags))
			goto free;
	}

free:
	if (sector_buf)
		kfree(sector_buf);

	if (tx_buf)
		kfree(tx_buf);

	if (rx_buf)
		kfree(rx_buf);

	return result;
}

static void panel_data_translate(u8 *buffer, int to_read, bool strip)
{
	int i = 0;

	for (i = 0; i < to_read; i++) {
		if (buffer[i] >= _a && buffer[i] <= _z) {
			buffer[i] = 'a' + (buffer[i] - _a);
		} else if (buffer[i] >= _A && buffer[i] <= _Z) {
			buffer[i] = 'A' + (buffer[i] - _A);
		} else if (buffer[i] >= zero && buffer[i] <= nine) {
			buffer[i] = '0' + (buffer[i] - zero);
		} else if (buffer[i] == underline) {
			buffer[i] = '_';
		} else if (buffer[i] == dot) {
			buffer[i] = '.';
		} else if (buffer[i] == negative) {
			buffer[i] = '-';
		} else {
			if (strip)
			{
				buffer[i] = 0;
				break;
			}
			buffer[i] = PNL_CHAR_UNKNOWN;
		}
	}
}

static bool panel_data_valid(char *panel_data)
{
	bool result = false;

	if (panel_data) {
		if (strchr(panel_data, PNL_CHAR_UNKNOWN)) {
			printk(KERN_ERR "Unrecognized values in panel data\n");
			pr_debug("panel data = %s\n", panel_data);
		} else {
			result = true;
		}
	}

	return result;
}

bool panel_flash_present(void)
{
	return ((NULL != panel_flash_spi) && spi_registered);
}

static char *panel_get_vcom_str(char *vcom_str, size_t vcom_str_len)
{
	struct panel_info *panel_info;
	int ret;
	u8 vcom[PNL_SIZE_VCOM] = { 0 };
	pr_debug("%s begin\n", __FUNCTION__);

	if (!panel_flash_present()) {
		vcom_str = NULL;
		return vcom_str;
	}

	if ((ret = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, ret);
		vcom_str = NULL;
		return vcom_str;
	}

	if (panel_read_from_flash((panel_info->addrs->pnl_info_addr + PNL_BASE_VCOM), vcom, PNL_SIZE_VCOM)) {
		printk(KERN_ERR "Error reading from panel flash!\n");
		vcom_str = NULL;
		return vcom_str;
	}

	/* Decode panel data */
	panel_data_translate(vcom, sizeof(vcom), true);

	strncpy(vcom_str, vcom, min((size_t)PNL_SIZE_VCOM, vcom_str_len - 1));
	vcom_str[min((size_t)PNL_SIZE_VCOM, vcom_str_len - 1)] = '\0';


	// If the VCOM string returned from the panel data is invalid, then
	// use the default one instead.
	//

	if (!panel_data_valid(vcom_str)) {
		printk(KERN_ERR "Panel flash data is not valid!\n");
		vcom_str = NULL;
		return vcom_str;
	}

	pr_debug("%s vcom=%s\n", __FUNCTION__, vcom_str);

	return vcom_str;
}

static void panel_get_format(struct panel_info *panel, struct waveform_data_header *header)
{
	switch (header->wf_type) {
	case EINK_WAVEFORM_TYPE_WJ:
		panel->addrs = &waveform_addrs_WJ;
		break;
	case EINK_WAVEFORM_TYPE_WR:
		panel->addrs = &waveform_addrs_WR;
		break;
	case EINK_WAVEFORM_TYPE_AA:
	case EINK_WAVEFORM_TYPE_AC:
	case EINK_WAVEFORM_TYPE_BD:
	case EINK_WAVEFORM_TYPE_AE:
		panel->addrs = &waveform_addrs_AA_AC_AE_BD;
		break;
		
	default:
		printk(KERN_ALERT "%s: Unknown panel flash format (%d). Add a definition for the waveform you are trying to use.\n",
		       __func__, header->mode_version);
		BUG();
	}
}

static int parse_vcom_str(char *vcom_str, int vcom_str_len)
{
	int vcom = 0;
	int i = 0;
	int fct_ord = 0;
	bool dec_pnt_reached = false;

	for(i = 0; i < vcom_str_len; i++) {
		if ('.' == vcom_str[i]) {
			dec_pnt_reached = true;
			continue;
		}

		if ((vcom_str[i] >= '0') && (vcom_str[i] <= '9')) {
			vcom *= 10;
			vcom += (vcom_str[i] - '0');

			if (dec_pnt_reached)
				fct_ord++;
		}
	}

	// Normalize value to uV
	i = 6 - fct_ord; // log(1,000,000)/log(frac(vcom))
	for (; i > 0; vcom *= 10, i--);

	return vcom;
}

static int panel_get_vcom(struct panel_info *panel)
{
	char vcom_str[PNL_SIZE_VCOM_STR] = { 0 };

	if (panel_get_vcom_str(vcom_str, PNL_SIZE_VCOM_STR) == NULL) {
		printk(KERN_ERR "eink_fb_waveform: E vcom:Setting VCOM to default value -2.05V!!!\n");
		panel->vcom_uV = -2050000;
		return panel->vcom_uV;
	}

	// Skip the negative sign (i.e., i = 1, instead of i = 0).
	if ('-' == (char)vcom_str[0])
		panel->vcom_uV = -parse_vcom_str(vcom_str + 1, PNL_SIZE_VCOM - 1);
	else
		panel->vcom_uV = parse_vcom_str(vcom_str, PNL_SIZE_VCOM);

	pr_debug("%s vcom=%dmV\n", __FUNCTION__, uV_to_mV(panel->vcom_uV));

	return panel->vcom_uV;
}

char *panel_get_id(struct panel_info *panel)
{
	struct panel_info *panel_info;
	u8 panel_buffer[PNL_BASE_FPL + 4] = { 0 };
	char *part_number;
	int cur;
	int ret;

	if (!panel_flash_present()) {
		panel->id[0] = '\0';
		return panel->id;
	}

	if ((ret = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, ret);
		panel->id[0] = '\0';
		return panel->id;
	}

	// Waveform file names are of the form PPPP_XLLL_DD_TTVVSS_B, and
	// panel IDs are of the form PPPP_LLL_DD_MMM.
	//
	panel_read_from_flash(panel_info->addrs->pnl_info_addr, panel_buffer, sizeof(panel_buffer));
	panel_data_translate(panel_buffer, sizeof(panel_buffer), false);

	// The platform is (usually) the PPPP substring.  And, in those cases, we copy
	// the platform data from the EEPROM's waveform name.  However, we must special-case
	// the V220E waveforms since EINK isn't using the same convention as they did in
	// the V110A case (i.e., they named V110A waveforms 110A but they are just
	// calling the V220E waveforms V220 with a run-type of E; run-type is the X
	// field in the PPPP_XLLL_DD_TTVVSS_B part of waveform file names).
	//
	switch (panel_buffer[PNL_BASE_WAVEFORM + 5]) {
	case 'e':
		panel->id[0] = '2';
		panel->id[1] = '2';
		panel->id[2] = '0';
		panel->id[3] = 'e';
		break;

	default:
		panel->id[0] = panel_buffer[PNL_BASE_WAVEFORM + 0];
		panel->id[1] = panel_buffer[PNL_BASE_WAVEFORM + 1];
		panel->id[2] = panel_buffer[PNL_BASE_WAVEFORM + 2];
		panel->id[3] = panel_buffer[PNL_BASE_WAVEFORM + 3];
		break;
	}

	panel->id[ 4] = '_';

	// the lot number (aka fpl) is the the lll substring:  just
	// copy the number itself, skipping the batch (x) designation.
	//
	panel->id[ 5] = panel_buffer[PNL_BASE_FPL + 1];
	panel->id[ 6] = panel_buffer[PNL_BASE_FPL + 2];
	panel->id[ 7] = panel_buffer[PNL_BASE_FPL + 3];

	panel->id[ 8] = '_';

	// the display size is the the dd substring.
	//
	panel->id[ 9] = panel_buffer[PNL_BASE_WAVEFORM + 10];
	panel->id[10] = panel_buffer[PNL_BASE_WAVEFORM + 11];
	panel->id[11] = '_';

	/* copy in the full part number */
	part_number = &panel_buffer[PNL_BASE_PART_NUMBER];
	for (cur = 0; cur < PNL_SIZE_PART_NUMBER && part_number[cur] != PNL_CHAR_UNKNOWN; cur++)
		panel->id[12 + cur] = part_number[cur];

	panel->id[12 + cur] = 0;

	if (!panel_data_valid(panel->id))
		strcpy(panel->id, PANEL_ID_UNKNOWN);

	pr_debug("%s: panel id=%s\n", __FUNCTION__, panel->id);

	return panel->id;
}

static u8 *panel_get_waveform_from_flash(int offset, u8 *buffer, int buffer_size)
{
	/* We can't use panel_get_info() here because it depends on this function.
	 * If we have no cached info, use the default; otherwise, use the read value.
	 */
	off_t wfm_base = DEFAULT_WFM_ADDR;

	if (panel_info_cache)
		wfm_base = panel_info_cache->addrs->waveform_addr;

	pr_debug("Reading waveform.. (%d bytes)\n", buffer_size);
	panel_read_from_flash(wfm_base + offset, buffer, buffer_size);
	return buffer;
}

static u8 *panel_get_waveform(u8 *wf_buffer, size_t wf_buffer_len, bool header_only)
{
	if (!panel_flash_present()) {
		wf_buffer[0] = 0;
		return NULL;
	}

	pr_debug("%s: begin\n", __FUNCTION__);

	pr_debug("%s: reading waveform header\n", __FUNCTION__);

	if (wf_buffer_len < WFM_HDR_SIZE) {
		printk(KERN_ERR "eink_fb_waveform: E internal:buffer not large enough (header):\n");
		return NULL;
	}

	if (panel_get_waveform_from_flash(0, wf_buffer, WFM_HDR_SIZE) == NULL) {
		printk(KERN_ERR "%s: Could not read header from flash\n", __func__);
		return NULL;
	}

	// We may end up having to re-read it if the initial read and/or subsequent
	// reads are invalid.
	//
	if (!header_only) {
		if (eink_waveform_valid(wf_buffer)) {
			eink_waveform_info_t waveform_info;
			eink_get_waveform_info(wf_buffer, &waveform_info);

			pr_debug("%s: reading waveform from flash\n", __FUNCTION__);

			if (wf_buffer_len < waveform_info.filesize) {
				printk(KERN_ERR "eink_fb_waveform: E internal:buffer not large enough (waveform):\n");
				return NULL;
			}

			if (panel_get_waveform_from_flash(WFM_HDR_SIZE,
			          (wf_buffer + WFM_HDR_SIZE),
			          (waveform_info.filesize - WFM_HDR_SIZE)) == NULL) {
				printk(KERN_ERR "%s: Could not read waveform from flash\n", __func__);
				return NULL;
			}

			pr_debug("%s: verifying waveform checksum\n", __FUNCTION__);

			// Verify waveform checksum
			if (eink_get_computed_waveform_checksum(wf_buffer) != waveform_info.checksum) {
				printk(KERN_ERR "eink_fb_waveform: E invalid:Invalid waveform checksum:\n");
				return NULL;
			}

			pr_debug("%s: read waveform size %ld\n", __FUNCTION__, waveform_info.filesize);
		} else {
			printk(KERN_ERR "eink_fb_waveform: E invalid:Invalid waveform header:\n");
			return NULL;
		}
	}

	return wf_buffer;
}

static struct update_modes *panel_get_upd_modes(struct mxc_epdc_fb_data *fb_data)
{
	unsigned char wf_upd_mode_version;
	struct update_modes *wf_upd_modes;

	struct eink_waveform_info_t info;
	eink_get_waveform_info((u8 *)fb_data->wv_header, &info);

	switch (info.waveform.mode_version) {
	case WF_UPD_MODES_00:
		wf_upd_mode_version = WF_UPD_MODES_00;
		wf_upd_modes = &panel_mode_00;
		break;
	case WF_UPD_MODES_07:
		wf_upd_mode_version = WF_UPD_MODES_07;
		wf_upd_modes = &panel_mode_07;
		break;
	case WF_UPD_MODES_18:
		wf_upd_mode_version = WF_UPD_MODES_18;
		wf_upd_modes = &panel_mode_18;
		break;
	case WF_UPD_MODES_19:
		wf_upd_mode_version = WF_UPD_MODES_19;
		wf_upd_modes = &panel_mode_19;
	case WF_UPD_MODES_24:
		wf_upd_mode_version = WF_UPD_MODES_24;
		wf_upd_modes = &panel_mode_24;
		break;
	case WF_UPD_MODES_25:
		wf_upd_mode_version = WF_UPD_MODES_25;
		wf_upd_modes = &panel_mode_25;
		break;
	default:
		wf_upd_mode_version = WF_UPD_MODES_07;
		wf_upd_modes = &panel_mode_07;
		printk(KERN_ERR "%s: Unknown waveform mode. Using MODE_07!", __func__);
	}

	return wf_upd_modes;
}

static struct imx_epdc_fb_mode * panel_choose_fbmode(struct mxc_epdc_fb_data *fb_data)
{
	struct imx_epdc_fb_mode *epdc_modes = fb_data->pdata->epdc_mode;
	struct panel_info *panel_info;
/*
 * TODO: Needed below
	uint32_t buf;
	u32 xres;
	u32 yres;
	u32 vddh;
	u32 lve;
*/
	int i;

	if (fb_data->pdata->num_modes < 1)
		return NULL;

	// Read the panel resolution
	if (!panel_flash_present())
		return &fb_data->pdata->epdc_mode[PANEL_MODE_E60_PINOT];

	if (panel_get_info(&panel_info, true))
		return NULL;

/*
 * TODO: Once Eink finalizes the flash spec to include the panel resolution, this code can be re-enabled

	panel_read_from_flash(panel_info->addrs->pnl_info_addr + PNL_BASE_VDDH, (unsigned char *)&buf, sizeof(buf));
	vddh = __le32_to_cpu(buf);
	if(vddh)
		max77696_epd_set_vddh(vddh);

	panel_read_from_flash(panel_info->addrs->pnl_info_addr + PNL_BASE_LVE, (unsigned char *)&buf, sizeof(buf));
	lve = __le32_to_cpu(buf);
	if(lve)
		epdc_iomux_config_lve();

	panel_read_from_flash(panel_info->addrs->pnl_info_addr + PNL_BASE_XRES, (unsigned char *)&buf, sizeof(buf));
	xres = __le32_to_cpu(buf);

	panel_read_from_flash(panel_info->addrs->pnl_info_addr + PNL_BASE_YRES, (unsigned char *)&buf, sizeof(buf));
	yres = __le32_to_cpu(buf);

	// Lookup the resolution
	for (i = 0; i < fb_data->pdata->num_modes; i++) {
		if ((epdc_modes[i].vmode->xres == xres) && (epdc_modes[i].vmode->yres == yres)) {
			dev_dbg(fb_data->dev, "Found a video mode override (%d x %d)\n", xres, yres);
			return &epdc_modes[i];
		}
	}
*/

	// Check the barcode override table
	for (i = 0; i < ARRAY_SIZE(fbmode_overrides); i++) {
		if (strncmp(fbmode_overrides[i].barcode_prefix, panel_info->bcd, PNL_BCD_PREFIX_LEN) == 0) {

			dev_info(fb_data->dev, "Found a video mode override (%s)\n", fbmode_overrides[i].barcode_prefix);

			if(fbmode_overrides[i].lve)
				epdc_iomux_config_lve();

			if(fbmode_overrides[i].vddh != 22000)
				max77696_epd_set_vddh(fbmode_overrides[i].vddh);
			
			return &epdc_modes[fbmode_overrides[i].vmode_index];
		}
	}

	// Default to the pinot mode
	return &fb_data->pdata->epdc_mode[PANEL_MODE_E60_PINOT];
}



/*!
 * This function is called whenever the SPI slave device is detected.
 *
 * @param	spi	the SPI slave device
 *
 * @return 	Returns 0 on SUCCESS and error on FAILURE.
 */
static int panel_flash_probe(struct spi_device *spi)
{
	u32 cmd, res;
	struct spi_transfer t;
	struct spi_message m;
	u8 mfg;
	int ret;

	/* Setup the SPI slave */
	panel_flash_spi = spi;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 32;
	spi_setup(spi);

	/* command is 1 byte, addr is 3 bytes */
	cmd = (SFM_ID << 24);
	res = 0;

	memset(&t, 0, sizeof(t));
	t.tx_buf = (const void *) &cmd;
	t.rx_buf = (void *) &res;
	t.len = 4;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	if (panel_spi_sync(spi, &m, &ret, "reading flash ID"))
		return ret;

	// see if we received a valid response
	mfg = (res >> 16) & 0xFF;
	switch (mfg) {
	case 0x20:
		pr_debug("M25P20 flash detected\n");
		break;
	case 0xc2:
		pr_debug("MX25L2005 flash detected\n");
		break;
	case 0xef:
		pr_debug("MX25U4035 flash detected\n");
		break;

	case 0x00:
		/* BEN TODO - fix this */
		printk(KERN_ERR "Bad flash signature: 0x%x\n", res);
		panel_flash_spi = NULL;
		return -1;

	default:
		pr_debug("Unrecognized flash: 0x%x\n", mfg);
		break;
	}

	return 0;
}

/*!
 * This function is called whenever the SPI slave device is removed.
 *
 * @param   spi - the SPI slave device
 *
 * @return  Returns 0 on SUCCESS and error on FAILURE.
 */
static int panel_flash_remove(struct spi_device *spi)
{
	printk(KERN_INFO "Device %s removed\n", dev_name(&spi->dev));
	panel_flash_spi = NULL;

	return 0;
}

bool panel_flash_init(void)
{
	int ret = spi_register_driver(&panel_flash_driver);

	if (ret) {
		/* BEN TODO - fix logging */
		printk(KERN_ERR "spi driver registration failed: %d\n", ret);
		spi_registered = false;
	} else {
		spi_registered = true;
	}

	return spi_registered;
}

void panel_flash_exit(void)
{
	pr_debug("%s: begin\n", __func__);

	if (spi_registered) {
		spi_unregister_driver(&panel_flash_driver);
		spi_registered = false;
	}
}

#ifdef DEVELOPMENT_MODE
static void panel_get_default_info(struct panel_info *panel)
{
	panel->addrs             = &waveform_addrs_WJ;
	panel->vcom_uV           = -2050000;
	panel->computed_checksum = -1;
	panel->embedded_checksum = -1;

	strncpy(panel->human_version, "NO PANEL HUMAN VERSION", WAVEFORM_VERSION_STRING_MAX);
	strncpy(panel->version,       "NO PANEL VERSION",       WAVEFORM_VERSION_STRING_MAX);
	strncpy(panel->bcd,           "NO PANEL BCD",           PNL_SIZE_BCD_STR);
	strncpy(panel->id,            "NO PANEL ID",            PNL_SIZE_ID_STR);

	panel->waveform_info->filesize = 0;
	panel->waveform_info->checksum = -1;

	panel->waveform_info->waveform.version       = -1;
	panel->waveform_info->waveform.subversion    = -1;
	panel->waveform_info->waveform.type          = EINK_WAVEFORM_TYPE_WJ;
	panel->waveform_info->waveform.run_type      = -1;
	panel->waveform_info->waveform.mode_version  = WF_UPD_MODES_07;
	panel->waveform_info->waveform.mfg_code      = -1;
	panel->waveform_info->waveform.tuning_bias   = -1;
	panel->waveform_info->waveform.revision      = -1;
	panel->waveform_info->waveform.fpl_rate      = -1;
	panel->waveform_info->waveform.vcom_shift    = -1;
	panel->waveform_info->waveform.bit_depth     = -1;
	panel->waveform_info->waveform.serial_number = -1;
	panel->waveform_info->waveform.xwia          = -1;
	panel->waveform_info->waveform.parse_wf_hex  = -1;

	panel->waveform_info->fpl.platform            = -1;
	panel->waveform_info->fpl.size                = -1;
	panel->waveform_info->fpl.adhesive_run_number = -1;
	panel->waveform_info->fpl.lot                 = -1;
}
#endif // DEVELOPMENT_MODE

static int panel_get_info(struct panel_info **panel, bool header_only)
{
	u8 *panel_waveform = NULL;

	if (panel_info_cache == NULL) {
		if (override_panel_settings) {
#ifdef DEVELOPMENT_MODE
			panel_info_cache = kmalloc(sizeof(struct panel_info), GFP_KERNEL);
			if (panel_info_cache == NULL) {
				printk(KERN_ERR "%s: could not allocate space for panel_info_cache (%d bytes)",
						__func__, sizeof(struct panel_info));
				return -ENOMEM;
			}

			panel_info_cache->waveform_info = kmalloc(sizeof(struct eink_waveform_info_t), GFP_KERNEL);
			if (panel_info_cache->waveform_info == NULL) {
				printk(KERN_ERR "%s: could not allocate space for waveform_info (%d bytes)",
						__func__, sizeof(struct eink_waveform_info_t));
				kfree(panel_info_cache);
				panel_info_cache = NULL;
				return -ENOMEM;
			}

			panel_get_default_info(panel_info_cache);
#endif // DEVELOPMENT_MODE
		} else {
			panel_waveform = kmalloc(WFM_HDR_SIZE, GFP_KERNEL);

			if (panel_waveform == NULL) {
				printk(KERN_ERR "%s: could not allocate space for header (%d bytes)", __func__, WFM_HDR_SIZE);
				return -ENOMEM;
			}

			if (panel_get_waveform(panel_waveform, WFM_HDR_SIZE, header_only) == NULL) {
				printk(KERN_ERR "%s: could not read waveform", __func__);
				kfree(panel_waveform);
				return -ENXIO;
			}

			panel_info_cache = kmalloc(sizeof(struct panel_info), GFP_KERNEL);
			if (panel_info_cache == NULL) {
				printk(KERN_ERR "%s: could not allocate space for panel_info_cache (%d bytes)",
						__func__, sizeof(struct panel_info));
				kfree(panel_waveform);
				return -ENOMEM;
			}

			panel_info_cache->waveform_info = kmalloc(sizeof(struct eink_waveform_info_t), GFP_KERNEL);
			if (panel_info_cache->waveform_info == NULL) {
				printk(KERN_ERR "%s: could not allocate space for waveform_info (%d bytes)",
						__func__, sizeof(struct eink_waveform_info_t));
				kfree(panel_waveform);
				kfree(panel_info_cache);
				panel_info_cache = NULL;
				return -ENOMEM;
			}

			panel_get_format(panel_info_cache, (struct waveform_data_header *)panel_waveform);

			panel_get_bcd(panel_info_cache);
			panel_get_id(panel_info_cache);

			panel_get_panel_info_version(panel_info_cache);
			panel_get_resolution(panel_info_cache);
			panel_get_dimensions(panel_info_cache);
			panel_get_vdd(panel_info_cache);

			panel_info_cache->embedded_checksum = ((struct waveform_data_header *)panel_waveform)->checksum;
			panel_info_cache->computed_checksum = -1;

			eink_get_waveform_info(panel_waveform, panel_info_cache->waveform_info);
			eink_get_wfm_version(panel_waveform, panel_info_cache->version, WAVEFORM_VERSION_STRING_MAX);
			eink_get_pnl_wfm_human_version(panel_waveform, WFM_HDR_SIZE, panel_info_cache->human_version, WAVEFORM_VERSION_STRING_MAX);

			panel_get_vcom(panel_info_cache);

			kfree(panel_waveform);
			panel_waveform = NULL;
		}
	}

	if (panel_info_cache->computed_checksum == -1 && !header_only) {
		panel_waveform = kmalloc(panel_info_cache->addrs->waveform_len, GFP_KERNEL);

		if (panel_waveform == NULL) {
			printk(KERN_ERR "%s: could not allocate space for waveform (%d bytes)", __func__, panel_info_cache->addrs->waveform_len);
			return -ENOMEM;
		}

		if (panel_get_waveform(panel_waveform, panel_info_cache->addrs->waveform_len, false) == NULL) {
			printk(KERN_ERR "%s: could not read waveform", __func__);
			kfree(panel_waveform);
			return -ENXIO;
		}

		panel_info_cache->computed_checksum = eink_get_computed_waveform_checksum(panel_waveform);
		kfree(panel_waveform);
		panel_waveform = NULL;
	}

	*panel = panel_info_cache;

	return 0;
}

/******
 * proc entries
 */


static int proc_panel_readonly_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;

	if (off == 0) {
		result = sprintf(page, "%d\n", (int) (panel_readonly ? 1 : 0));
		*eof = 1;
	}

	return result;
}

static int proc_panel_readonly_write(struct file *file, const char __user *buf, unsigned long count, void *data)
{
	int ret;

	ret = kstrtoint(buf, 10, &panel_readonly);
	if (ret)
		return ret;

	return count;
}

static int proc_panel_data_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct panel_info *panel_info;
	u8 buffer[PNL_PAGE_SIZE];
	int result = 0;
	int i = 0, j = 0;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	if (off == 0) {
		if (panel_read_from_flash(panel_info->addrs->pnl_info_addr, buffer, sizeof(buffer))) {
			printk(KERN_ERR "Error reading from panel flash!\n");
			return -EINVAL;
		}

		/* Decode panel data */
		panel_data_translate(buffer, sizeof(buffer), false);

		for (i = 0; i < 16; i++) {
			for (j = 0; j < 16; j++) {
				result += sprintf((page + result), "%c", (char)buffer[(i*16) + j]);
			}
			result += sprintf((page + result), "\n");
		}

		*eof = 1;
	}

	return result;
}

static int proc_panel_data_write(struct file *file, const char __user *buf, unsigned long count, void *data)
{
	int result;
	struct panel_info *panel_info;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	if (panel_readonly) {
		printk(KERN_ERR "Panel flash read-only\n");
		return -EFAULT;
	}

	return panel_data_write(panel_info->addrs->pnl_info_addr, file, buf, count, data);
}

static int proc_panel_bcd_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result;
	struct panel_info *panel_info;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	// We're done after one shot.
	if (0 == off) {
		result = snprintf(page, count, "%s\n", panel_info->bcd);
		*eof = 1;
	}

	return result;
}

static int proc_panel_id_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result;
	struct panel_info *panel_info;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	if (0 == off) {
		result = sprintf(page, "%s\n", panel_info->id);
		*eof = 1;
	}

	return result;
}

static int proc_panel_wfm_data_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result;
	struct panel_info *panel_info;
	int j = 0;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	pr_debug("%s: off=%d flsz=%d count=%d\n", __func__, (int) off, panel_info->addrs->waveform_len, count);

	*eof = 0;

	if ((off < panel_info->addrs->waveform_len) && count) {
		int i = off;
		int total = min((unsigned long)(panel_info->addrs->waveform_len - off), (unsigned long) count);
		int length = (1 << PAGE_SHIFT);
		int num_loops = total / length;
		int remainder = total % length;
		bool done = false;

		*start = page;

		do {
			if (0 >= num_loops)
				length = remainder;

			panel_read_from_flash(i + panel_info->addrs->waveform_addr, (page + j), length);
			i += length; j += length;

			if (i < total)
				num_loops--;
			else
				done = true;
		} while (!done);
	} else {
		*eof = 1;
	}

	return j;
}

static int proc_panel_wfm_data_write(struct file *file, const char __user *buf, unsigned long count, void *data)
{
	int result;
	struct panel_info *panel_info;

	if (panel_readonly) {
		printk(KERN_ERR "Panel flash read-only\n");
		return -EFAULT;
	}

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	return panel_data_write(panel_info->addrs->waveform_addr, file, buf, count, data);
}

static int proc_panel_wfm_version_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;
	struct panel_info *panel_info;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	// We're done after one shot.
	if (0 == off) {
		result = snprintf(page, count, "%s\n", panel_info->version);
		*eof = 1;
	}

	return result;
}

static int proc_panel_wfm_human_version_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;
	struct panel_info *panel_info;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	// We're done after one shot.
	if ( 0 == off ) {
		result = snprintf(page, count, "%s\n", panel_info->human_version);
		*eof = 1;
	}

	return result;
}

static int proc_panel_wfm_embedded_checksum_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;
	struct panel_info *panel_info;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	// We're done after one shot.
	if (0 == off) {
		result = snprintf(page, count, "0x%08lX\n", panel_info->embedded_checksum);
		*eof = 1;
	}

	return result;
}

static int proc_panel_wfm_computed_checksum_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;
	struct panel_info *panel_info;

	if ((result = panel_get_info(&panel_info, false))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	// We're done after one shot.
	//
	if (0 == off) {
		result = snprintf(page, count, "0x%08lX\n", panel_info->computed_checksum);
		*eof = 1;
	}

	return result;
}

static int proc_panel_wfm_info_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int result = 0;
	struct eink_waveform_info_t *info;
	struct panel_info *panel_info;

	if ((result = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, result);
		return result;
	}

	info = panel_info->waveform_info;

	if (0 == off) {
		result = sprintf(page,
		                 " Waveform version:  0x%02X\n"
		                 "       subversion:  0x%02X\n"
		                 "             type:  0x%02X (v%02d)\n"
		                 "         run type:  0x%02X\n"
		                 "     mode version:  0x%02X\n"
		                 "      tuning bias:  0x%02X\n"
		                 "       frame rate:  0x%02X\n"
		                 "       vcom shift:  0x%02X\n"
		                 "        bit depth:  0x%02X\n"
		                 "\n"
		                 "     FPL platform:  0x%02X\n"
		                 "              lot:  0x%04X\n"
		                 "             size:  0x%02X\n"
		                 " adhesive run no.:  0x%02X\n"
		                 "\n"
		                 "        File size:  0x%08lX\n"
		                 "         Mfg code:  0x%02X\n"
		                 "       Serial no.:  0x%08lX\n"
		                 "         Checksum:  0x%08lX\n",

		                 info->waveform.version,
		                 info->waveform.subversion,
		                 info->waveform.type,
		                 info->waveform.revision,
		                 info->waveform.run_type,
		                 info->waveform.mode_version,
		                 info->waveform.tuning_bias,
		                 info->waveform.fpl_rate,
		                 info->waveform.vcom_shift,
		                 info->waveform.bit_depth,

		                 info->fpl.platform,
		                 info->fpl.lot,
		                 info->fpl.size,
		                 info->fpl.adhesive_run_number,

		                 info->filesize,
		                 info->waveform.mfg_code,
		                 info->waveform.serial_number,
		                 info->checksum);
		*eof = 1;
	}

	return result;
}




/******************************************************************************
 ******************************************************************************
 **                                                                          **
 **                              MXC Functions                               **
 **                                                                          **
 ******************************************************************************
 ******************************************************************************/

static struct wf_proc_dir_entry panel_proc_entries[] = {
	{ "readonly", S_IWUGO | S_IRUGO, proc_panel_readonly_read, proc_panel_readonly_write, NULL },
	{ "data", S_IWUGO | S_IRUGO, proc_panel_data_read, proc_panel_data_write, NULL },
	{ "bcd", S_IRUGO, proc_panel_bcd_read, NULL, NULL },
	{ "id", S_IRUGO, proc_panel_id_read, NULL, NULL },
	{ "working_buffer", S_IRUGO, proc_working_buffer_read, NULL, NULL},
};

static struct wf_proc_dir_entry panel_wfm_proc_entries[] = {
	{ "readonly", S_IWUGO | S_IRUGO, proc_panel_readonly_read, proc_panel_readonly_write, NULL },
	{ "data", S_IWUGO | S_IRUGO, proc_panel_wfm_data_read, proc_panel_wfm_data_write, NULL },
	{ "info", S_IRUGO, proc_panel_wfm_info_read, NULL, NULL },
	{ "version", S_IRUGO, proc_panel_wfm_version_read, NULL, NULL },
	{ "human_version", S_IRUGO, proc_panel_wfm_human_version_read, NULL, NULL },
	{ "embedded_checksum", S_IRUGO, proc_panel_wfm_embedded_checksum_read, NULL, NULL },
	{ "computed_checksum", S_IRUGO, proc_panel_wfm_computed_checksum_read, NULL, NULL },
};

static int has_vcom_shift(struct eink_waveform_info_t *info)
{
	if (info->waveform.type == EINK_WAVEFORM_TYPE_WR ||
			info->waveform.type == EINK_WAVEFORM_TYPE_WJ)
		return 0;
	return 1;
}

static int mxc_set_vcom(struct mxc_epdc_fb_data *fb_data, int vcom_uV)
{
	int ret = 0;
	struct eink_waveform_info_t info;

	mutex_lock(&fb_data->power_mutex);

	eink_get_waveform_info((u8 *)g_fb_data->wv_header, &info);

	if (has_vcom_shift(&info) && info.waveform.vcom_shift)
	{
		dev_dbg(fb_data->dev, "Vcom shift enabled. Shifting by: +%d uV\n", WAVEFORM_AA_VCOM_SHIFT);
		vcom_uV += WAVEFORM_AA_VCOM_SHIFT;
	}
	// TODO: -vcom_uV is a workaround. Mitch, remove this when you commit your PMIC changes.
	ret = regulator_set_voltage(fb_data->vcom_regulator, -vcom_uV, -vcom_uV);
	if (ret)
		dev_err(fb_data->dev, "unable to set VCOM = %dmV (err = %d)\n", uV_to_mV(vcom_uV), ret);
	else
		dev_dbg(fb_data->dev, "VCOM = %dmV\n", uV_to_mV(regulator_get_voltage(fb_data->vcom_regulator)));

	fb_data->vcom_uV = vcom_uV;
	mutex_unlock(&fb_data->power_mutex);

	return ret;
}

int mxc_epdc_panel_init(struct mxc_epdc_fb_data *fb_data)
{
	if (!panel_flash_present()) {
		if (!panel_flash_init()) {
			printk(KERN_ERR "%s: Panel flash not found!!\n", __func__);
			return -ENODEV;
		}
	}

	return 0;
}

int mxc_epdc_do_panel_init(struct mxc_epdc_fb_data *fb_data)
{
	struct fb_var_screeninfo tmpvar;

	// Say that we want to switch into Y8 mode:  This is where the INIT waveform
	// update occurs.
	//
	tmpvar = fb_data->info.var;
	tmpvar.bits_per_pixel = 8;
	tmpvar.grayscale = GRAYSCALE_8BIT;
	tmpvar.activate = FB_ACTIVATE_FORCE | FB_ACTIVATE_NOW | FB_ACTIVATE_ALL;

	// Say that we want to switch to portrait mode.
	//
	if(DISPLAY_UP_RIGHT) {
		tmpvar.rotate = FB_ROTATE_UR;
	} else {
		tmpvar.rotate = FB_ROTATE_CCW;
	}
	tmpvar.activate = FB_ACTIVATE_FORCE | FB_ACTIVATE_NOW | FB_ACTIVATE_ALL;
	fb_set_var(&(fb_data->info), &tmpvar);
	return 0;
}

int mxc_epdc_waveform_init(struct mxc_epdc_fb_data *fb_data)
{
	int i;
	int sz;
	int ret;
	struct panel_info *panel_info;

	pr_debug("%s: begin\n", __func__);

	if (!panel_flash_present())
		return 0;

	if ((ret = panel_get_info(&panel_info, true))) {
		printk(KERN_ERR "%s: Could not get panel info (%d)\n", __func__, ret);
		panel_flash_exit();
		return ret;
	}

	// Set vcom value
	//
	ret = mxc_set_vcom(fb_data, panel_info->vcom_uV);
	if (ret) {
		printk(KERN_ERR "%s: Unable to set VCOM = %dmV\n", __func__, uV_to_mV(panel_info->vcom_uV));
		panel_flash_exit();
		return ret;
	}

	// Set the waveform modes from the waveform.
	//
	set_waveform_modes(fb_data);

	proc_wf_parent = create_proc_entry(WF_PROC_PARENT,
	                                   (S_IFDIR | S_IRUGO | S_IXUGO),
	                                   NULL);
	if (proc_wf_parent) {
		/* Create panel proc entries */
		proc_wf_panel_parent = create_proc_entry(WF_PROC_PANEL_PARENT,
		                                         (S_IFDIR | S_IRUGO | S_IXUGO),
		                                         NULL);
		if (proc_wf_panel_parent) {
			sz = ARRAY_SIZE(panel_proc_entries);

			for (i = 0; i < sz; i++) {
				panel_proc_entries[i].proc_entry = create_wf_proc_entry(
					panel_proc_entries[i].name,
					panel_proc_entries[i].mode,
					proc_wf_panel_parent,
					panel_proc_entries[i].read_proc,
					panel_proc_entries[i].write_proc);
			}

			/* Create panel waveform proc entries */
			proc_wf_panel_wfm_parent = create_proc_entry(WF_PROC_PANEL_WFM_PARENT,
			                                             (S_IFDIR | S_IRUGO | S_IXUGO),
			                                             NULL);
			if (proc_wf_panel_wfm_parent) {
				sz = ARRAY_SIZE(panel_wfm_proc_entries);

				for (i = 0; i < sz; i++) {
					panel_wfm_proc_entries[i].proc_entry = create_wf_proc_entry(
						panel_wfm_proc_entries[i].name,
						panel_wfm_proc_entries[i].mode,
						proc_wf_panel_wfm_parent,
						panel_wfm_proc_entries[i].read_proc,
						panel_wfm_proc_entries[i].write_proc);
				}
			}
		}

		/* Create waveform proc entries */
		proc_wf_waveform_parent = create_proc_entry(WF_PROC_WFM_PARENT,
		                                            (S_IFDIR | S_IRUGO | S_IXUGO),
		                                            NULL);
		if (proc_wf_waveform_parent) {
			sz = ARRAY_SIZE(wfm_proc_entries);

			for (i = 0; i < sz; i++) {
				wfm_proc_entries[i].proc_entry = create_wf_proc_entry(
					wfm_proc_entries[i].name,
					wfm_proc_entries[i].mode,
					proc_wf_waveform_parent,
					wfm_proc_entries[i].read_proc,
					wfm_proc_entries[i].write_proc);
			}
		}
	}

	return 0;
}

void mxc_epdc_waveform_done(struct mxc_epdc_fb_data *fb_data)
{
	int i, sz;

	panel_flash_exit();

	if (panel_info_cache) {
		kfree(panel_info_cache->waveform_info);
		kfree(panel_info_cache);
		panel_info_cache = NULL;
	}

	if (proc_wf_parent) {
		if (proc_wf_waveform_parent) {
			sz = ARRAY_SIZE(wfm_proc_entries);

			for (i = 0; i < sz; i++) {
				remove_wf_proc_entry(wfm_proc_entries[i].name,
				                     wfm_proc_entries[i].proc_entry,
				                     proc_wf_waveform_parent);
			}

			remove_wf_proc_entry(WF_PROC_WFM_PARENT, proc_wf_waveform_parent, NULL);
		}

		if (proc_wf_panel_parent) {
			if (proc_wf_panel_wfm_parent) {
				sz = ARRAY_SIZE(panel_wfm_proc_entries);

				for (i = 0; i < sz; i++) {
					remove_wf_proc_entry(panel_wfm_proc_entries[i].name,
					                     panel_wfm_proc_entries[i].proc_entry,
					                     proc_wf_panel_wfm_parent);
				}

				remove_wf_proc_entry(WF_PROC_PANEL_WFM_PARENT, proc_wf_panel_wfm_parent, NULL);
			}

			sz = ARRAY_SIZE(panel_proc_entries);

			for (i = 0; i < sz; i++) {
				remove_wf_proc_entry(panel_proc_entries[i].name,
				                     panel_proc_entries[i].proc_entry,
				                     proc_wf_panel_parent);
			}

			remove_wf_proc_entry(WF_PROC_PANEL_PARENT, proc_wf_panel_parent, NULL);
		}

		remove_wf_proc_entry(WF_PROC_PARENT, proc_wf_parent, NULL);
	}
}

