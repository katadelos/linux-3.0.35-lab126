/*
 * cyttsp4_device_access.c
 * Cypress TrueTouch(TM) Standard Product V4 Device Access module.
 * Configuration and Test command/status user interface.
 * For use with Cypress Txx4xx parts.
 * Supported parts include:
 * TMA4XX
 * TMA1036
 * Copyright (c) 2012-2014 Amazon.com, Inc. or its affiliates. 
 * Copyright (C) 2012 Cypress Semiconductor
 * Copyright (C) 2011 Sony Ericsson Mobile Communications AB.
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

#include <linux/cyttsp4_bus.h>
#include <linux/cyttsp4_core.h>
#include <linux/cyttsp4_mt.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "cyttsp4_device_access.h"
#include "cyttsp4_regs.h"
#include <asm/unaligned.h>
#include <boardid.h>
#include <mach/boardid.h>

#define CY_MAX_CONFIG_BYTES    256
#define CY_CMD_INDEX             0
#define CY_NULL_CMD_INDEX        1
#define CY_NULL_CMD_MODE_INDEX   2
#define CY_NULL_CMD_SIZE_INDEX   3
#define CY_NULL_CMD_SIZEL_INDEX  2
#define CY_NULL_CMD_SIZEH_INDEX  3


#define DEVICE_TYPE_TMA4xx      0
#define DEVICE_TYPE_TMA445      1

#define OPENS_TMA4xx_TEST_TYPE_MUTUAL   0
#define OPENS_TMA4xx_TEST_TYPE_BUTTON   1

#define HI_BYTE(x)  (u8)(((x) >> 8) & 0xFF)
#define LOW_BYTE(x) (u8)((x) & 0xFF)

#define STATUS_SUCCESS  0
#define STATUS_FAIL     -1

#define READ_LENGTH_MAX 65535

struct heatmap_param {
	bool scan_start;
	enum scanDataTypeList dataType; /* raw, base, diff */
	int numElement;
};

struct cyttsp4_device_access_data {
	struct cyttsp4_device *ttsp;
	struct cyttsp4_device_access_platform_data *pdata;
	struct cyttsp4_sysinfo *si;
	struct cyttsp4_test_mode_params test;
	struct mutex sysfs_lock;
	uint32_t ic_grpnum;
	uint32_t ic_grpoffset;
	bool own_exclusive;
	uint32_t ebid_row_size;
	bool sysfs_nodes_created;
#ifdef VERBOSE_DEBUG
	u8 pr_buf[CY_MAX_PRBUF_SIZE];
#endif
	wait_queue_head_t wait_q;
	u8 ic_buf[CY_MAX_PRBUF_SIZE];
	u8 return_buf[CY_MAX_PRBUF_SIZE];
	struct heatmap_param heatmap;
	u8 get_idac_data_id;
	u8 opens_device_type;
        u8 opens_test_type;
};

struct cyttsp4_device_access_data *g_da_data;
extern int (*cyttsp4_easy_calibrate)(int);

/*

 * cyttsp4_is_awakening_grpnum
 * Returns true if a grpnum requires being awake
 */
static bool cyttsp4_is_awakening_grpnum(int grpnum)
{
	int i;

	/* Array that lists which grpnums require being awake */
	static const int awakening_grpnums[] = {
		CY_IC_GRPNUM_CMD_REGS,
		CY_IC_GRPNUM_TEST_REGS,
	};

	for (i = 0; i < ARRAY_SIZE(awakening_grpnums); i++)
		if (awakening_grpnums[i] == grpnum)
			return true;

	return false;
}

/*
 * Show function prototype.
 * Returns response length or Linux error code on error.
 */
typedef int (*cyttsp4_show_function) (struct device *dev, u8 *ic_buf,
		size_t length);

/*
 * Store function prototype.
 * Returns Linux error code on error.
 */
typedef int (*cyttsp4_store_function) (struct device *dev, u8 *ic_buf,
		size_t length);

/*
 * grpdata show function to be used by
 * reserved and not implemented ic group numbers.
 */
int cyttsp4_grpdata_show_void (struct device *dev, u8 *ic_buf, size_t length)
{
	return -ENOSYS;
}

/*
 * grpdata store function to be used by
 * reserved and not implemented ic group numbers.
 */
int cyttsp4_grpdata_store_void (struct device *dev, u8 *ic_buf, size_t length)
{
	return -ENOSYS;
}

/*
 * SysFs group number entry show function.
 */
static ssize_t cyttsp4_ic_grpnum_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int val = 0;

	mutex_lock(&dad->sysfs_lock);
	val = dad->ic_grpnum;
	mutex_unlock(&dad->sysfs_lock);

	return scnprintf(buf, CY_MAX_PRBUF_SIZE, "Current Group: %d\n", val);
}

/*
 * SysFs group number entry store function.
 */
static ssize_t cyttsp4_ic_grpnum_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	unsigned long value;
	int prev_grpnum;
	int rc;

	rc = strict_strtoul(buf, 10, &value);
	if (rc < 0) {
		dev_err(dev, "%s: Invalid value\n", __func__);
		return size;
	}

	if (value >= CY_IC_GRPNUM_NUM) {
		dev_err(dev, "%s: Group %lu does not exist.\n",
				__func__, value);
		return size;
	}

	if (value > 0xFF)
		value = 0xFF;

	mutex_lock(&dad->sysfs_lock);
	/*
	 * Block grpnum change when own_exclusive flag is set
	 * which means the current grpnum implementation requires
	 * running exclusively on some consecutive grpdata operations
	 */
	if (dad->own_exclusive) {
		mutex_unlock(&dad->sysfs_lock);
		dev_err(dev, "%s: own_exclusive\n", __func__);
		return -EBUSY;
	}
	prev_grpnum = dad->ic_grpnum;
	dad->ic_grpnum = (int) value;
	mutex_unlock(&dad->sysfs_lock);

	/* Check whether the new grpnum requires being awake */
	if (cyttsp4_is_awakening_grpnum(prev_grpnum) !=
		cyttsp4_is_awakening_grpnum(value)) {
		if (cyttsp4_is_awakening_grpnum(value))
			pm_runtime_get(dev);
		else
			pm_runtime_put(dev);
	}

	dev_vdbg(dev, "%s: ic_grpnum=%d, return size=%d\n",
			__func__, (int)value, (int)size);
	return size;
}

/*
 * SysFs group offset entry show function.
 */
static ssize_t cyttsp4_ic_grpoffset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int val = 0;

	mutex_lock(&dad->sysfs_lock);
	val = dad->ic_grpoffset;
	mutex_unlock(&dad->sysfs_lock);

	return scnprintf(buf, CY_MAX_PRBUF_SIZE, "Current Offset: %d\n", val);
}

/*
 * SysFs group offset entry store function.
 */
static ssize_t cyttsp4_ic_grpoffset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = strict_strtoul(buf, 10, &value);
	if (ret < 0) {
		dev_err(dev, "%s: Invalid value\n", __func__);
		return size;
	}

	if (value > 0xFFFF)
		value = 0xFFFF;

	mutex_lock(&dad->sysfs_lock);
	dad->ic_grpoffset = (int)value;
	mutex_unlock(&dad->sysfs_lock);

	dev_vdbg(dev, "%s: ic_grpoffset=%d, return size=%d\n", __func__,
			(int)value, (int)size);
	return size;
}

/*
 * Prints part of communication registers.
 */
static int cyttsp4_grpdata_show_registers(struct device *dev, u8 *ic_buf,
		size_t length, int num_read, int offset, int mode)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int rc;

	if (dad->ic_grpoffset >= num_read)
		return -EINVAL;

	num_read -= dad->ic_grpoffset;

	if (length < num_read) {
		dev_err(dev, "%s: not sufficient buffer req_bug_len=%d, length=%d\n",
				__func__, num_read, length);
		return -EINVAL;
	}

	rc = cyttsp4_read(dad->ttsp, mode, offset + dad->ic_grpoffset, ic_buf,
			num_read);
	if (rc < 0)
		return rc;

	return num_read;
}

/*
 * SysFs grpdata show function implementation of group 1.
 * Prints status register contents of Operational mode registers.
 */
static int cyttsp4_grpdata_show_operational_regs(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int num_read = dad->si->si_ofs.rep_ofs - dad->si->si_ofs.cmd_ofs;
	int i;

	if (dad->ic_grpoffset >= num_read) {
		dev_err(dev,
			"%s: ic_grpoffset bigger than command registers, cmd_registers=%d\n",
			__func__, num_read);
		return -EINVAL;
	}

	num_read -= dad->ic_grpoffset;

	if (length < num_read) {
		dev_err(dev,
			"%s: not sufficient buffer req_bug_len=%d, length=%d\n",
			__func__, num_read, length);
		return -EINVAL;
	}

	if (dad->ic_grpoffset + num_read > CY_MAX_PRBUF_SIZE) {
		dev_err(dev,
			"%s: not sufficient source buffer req_bug_len=%d, length=%d\n",
			__func__, dad->ic_grpoffset + num_read,
			CY_MAX_PRBUF_SIZE);
		return -EINVAL;
	}


	/* cmd result already put into dad->return_buf */
	for (i = 0; i < num_read; i++)
		ic_buf[i] = dad->return_buf[dad->ic_grpoffset + i];

	return num_read;
}

/*
 * SysFs grpdata show function implementation of group 2.
 * Prints current contents of the touch registers (full set).
 */
static int cyttsp4_grpdata_show_touch_regs(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int num_read = dad->si->si_ofs.rep_sz;
	int offset = dad->si->si_ofs.rep_ofs;

	return cyttsp4_grpdata_show_registers(dev, ic_buf, length, num_read,
			offset, CY_MODE_OPERATIONAL);
}

/*
 * Prints some content of the system information
 */
static int cyttsp4_grpdata_show_sysinfo(struct device *dev, u8 *ic_buf,
		size_t length, int num_read, int offset)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int rc = 0, rc2 = 0, rc3 = 0;

	if (dad->ic_grpoffset >= num_read)
		return -EINVAL;

	num_read -= dad->ic_grpoffset;

	if (length < num_read) {
		dev_err(dev, "%s: not sufficient buffer req_bug_len=%d, length=%d\n",
				__func__, num_read, length);
		return -EINVAL;
	}

	rc = cyttsp4_request_exclusive(dad->ttsp,
			CY_DA_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		return rc;
	}

	rc = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_SYSINFO);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request set mode r=%d\n",
				__func__, rc);
		goto cyttsp4_grpdata_show_sysinfo_err_release;
	}

	rc = cyttsp4_read(dad->ttsp, CY_MODE_SYSINFO,
			offset + dad->ic_grpoffset,
			ic_buf, num_read);
	if (rc < 0)
		dev_err(dev, "%s: Fail read cmd regs r=%d\n",
				__func__, rc);

	rc2 = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_OPERATIONAL);
	if (rc2 < 0)
		dev_err(dev, "%s: Error on request set mode 2 r=%d\n",
				__func__, rc2);

cyttsp4_grpdata_show_sysinfo_err_release:
	rc3 = cyttsp4_release_exclusive(dad->ttsp);
	if (rc3 < 0) {
		dev_err(dev, "%s: Error on release exclusive r=%d\n",
				__func__, rc3);
		return rc3;
	}

	if (rc < 0)
		return rc;
	if (rc2 < 0)
		return rc2;

	return num_read;
}

/*
 * SysFs grpdata show function implementation of group 3.
 * Prints content of the system information DATA record.
 */
static int cyttsp4_grpdata_show_sysinfo_data_rec(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int num_read = dad->si->si_ofs.cydata_size;
	int offset = dad->si->si_ofs.cydata_ofs;

	return cyttsp4_grpdata_show_sysinfo(dev, ic_buf, length, num_read,
			offset);
}

/*
 * SysFs grpdata show function implementation of group 4.
 * Prints content of the system information TEST record.
 */
static int cyttsp4_grpdata_show_sysinfo_test_rec(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int num_read = dad->si->si_ofs.test_size;
	int offset = dad->si->si_ofs.test_ofs;

	return cyttsp4_grpdata_show_sysinfo(dev, ic_buf, length, num_read,
			offset);
}

/*
 * SysFs grpdata show function implementation of group 5.
 * Prints content of the system information PANEL data.
 */
static int cyttsp4_grpdata_show_sysinfo_panel(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int num_read = dad->si->si_ofs.pcfg_size;
	int offset = dad->si->si_ofs.pcfg_ofs;

	return cyttsp4_grpdata_show_sysinfo(dev, ic_buf, length, num_read,
			offset);
}

/*
 * Get EBID Row Size is a Config mode command
 */
static int _cyttsp4_get_ebid_row_size(struct device *dev)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u8 cmd_buf[CY_CMD_CAT_GET_CFG_BLK_SZ_CMD_SZ];
	u8 return_buf[CY_CMD_CAT_GET_CFG_BLK_SZ_RET_SZ];
	int rc;

	cmd_buf[0] = CY_CMD_CAT_GET_CFG_ROW_SZ;
	rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_CAT,
			cmd_buf, CY_CMD_CAT_GET_CFG_BLK_SZ_CMD_SZ,
			return_buf, CY_CMD_CAT_GET_CFG_BLK_SZ_RET_SZ,
			CY_DA_COMMAND_COMPLETE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Unable to read EBID row size.\n", __func__);
		return rc;
	}

	dad->ebid_row_size = (return_buf[0] << 8) + return_buf[1];
	return rc;
}

/*
 * SysFs grpdata show function implementation of group 6.
 * Prints contents of the touch parameters a row at a time.
 */
static int cyttsp4_grpdata_show_touch_params(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u8 cmd_buf[CY_CMD_CAT_READ_CFG_BLK_CMD_SZ];
	int return_buf_size = CY_CMD_CAT_READ_CFG_BLK_RET_SZ;
	int row_offset;
	int offset_in_single_row = 0;
	int rc;
	int rc2 = 0;
	int rc3;
	int i, j;

	rc = cyttsp4_request_exclusive(dad->ttsp,
			CY_DA_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		return rc;
	}

	rc = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_CAT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request set mode r=%d\n",
				__func__, rc);
		goto cyttsp4_grpdata_show_touch_params_err_release;
	}

	if (dad->ebid_row_size == 0) {
		rc = _cyttsp4_get_ebid_row_size(dev);
		if (rc < 0)
			goto cyttsp4_grpdata_show_touch_params_err_change_mode;
	}

	/* Perform buffer size check since we have just acquired row size */
	return_buf_size += dad->ebid_row_size;

	if (length < return_buf_size) {
		dev_err(dev, "%s: not sufficient buffer "
				"req_buf_len=%d, length=%d\n",
				__func__, return_buf_size, length);
		rc = -EINVAL;
		goto cyttsp4_grpdata_show_touch_params_err_change_mode;
	}

	row_offset = dad->ic_grpoffset / dad->ebid_row_size;

	cmd_buf[0] = CY_CMD_CAT_READ_CFG_BLK;
	cmd_buf[1] = HI_BYTE(row_offset);
	cmd_buf[2] = LOW_BYTE(row_offset);
	cmd_buf[3] = HI_BYTE(dad->ebid_row_size);
	cmd_buf[4] = LOW_BYTE(dad->ebid_row_size);
	cmd_buf[5] = CY_TCH_PARM_EBID;
	rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_CAT,
			cmd_buf, CY_CMD_CAT_READ_CFG_BLK_CMD_SZ,
			ic_buf, return_buf_size,
			CY_DA_COMMAND_COMPLETE_TIMEOUT);

	offset_in_single_row = dad->ic_grpoffset % dad->ebid_row_size;

	/* Remove Header data from return buffer */
	for (i = 0, j = CY_CMD_CAT_READ_CFG_BLK_RET_HDR_SZ
				+ offset_in_single_row;
			i < (dad->ebid_row_size - offset_in_single_row);
			i++, j++)
		ic_buf[i] = ic_buf[j];

cyttsp4_grpdata_show_touch_params_err_change_mode:
	rc2 = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_OPERATIONAL);
	if (rc2 < 0)
		dev_err(dev, "%s: Error on request set mode r=%d\n",
				__func__, rc2);

cyttsp4_grpdata_show_touch_params_err_release:
	rc3 = cyttsp4_release_exclusive(dad->ttsp);
	if (rc3 < 0) {
		dev_err(dev, "%s: Error on release exclusive r=%d\n",
				__func__, rc3);
		return rc3;
	}

	if (rc < 0)
		return rc;
	if (rc2 < 0)
		return rc2;

	return dad->ebid_row_size - offset_in_single_row;
}

/*
 * SysFs grpdata show function implementation of group 7.
 * Prints contents of the touch parameters sizes.
 */
static int cyttsp4_grpdata_show_touch_params_sizes(struct device *dev,
		u8 *ic_buf, size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	struct cyttsp4_core_platform_data *pdata =
			dev_get_platdata(&dad->ttsp->core->dev);
	int max_size;
	int block_start;
	int block_end;
	int num_read;

	if (pdata->sett[CY_IC_GRPNUM_TCH_PARM_SIZE] == NULL) {
		dev_err(dev, "%s: Missing platform data Touch Parameters Sizes"
			       " table\n", __func__);
		return -EINVAL;
	}

	if (pdata->sett[CY_IC_GRPNUM_TCH_PARM_SIZE]->data == NULL) {
		dev_err(dev, "%s: Missing platform data Touch Parameters Sizes"
			       " table data\n", __func__);
		return -EINVAL;
	}

	max_size = pdata->sett[CY_IC_GRPNUM_TCH_PARM_SIZE]->size;
	max_size *= sizeof(uint16_t);
	if (dad->ic_grpoffset >= max_size)
		return -EINVAL;

	block_start = (dad->ic_grpoffset / CYTTSP4_TCH_PARAM_SIZE_BLK_SZ)
			* CYTTSP4_TCH_PARAM_SIZE_BLK_SZ;
	block_end = CYTTSP4_TCH_PARAM_SIZE_BLK_SZ + block_start;
	if (block_end > max_size)
		block_end = max_size;
	num_read = block_end - dad->ic_grpoffset;
	if (length < num_read) {
		dev_err(dev, "%s: not sufficient buffer %s=%d, %s=%d\n",
				__func__, "req_buf_len", num_read, "length",
				length);
		return -EINVAL;
	}

	memcpy(ic_buf, (u8 *)pdata->sett[CY_IC_GRPNUM_TCH_PARM_SIZE]->data
			+ dad->ic_grpoffset, num_read);

	return num_read;
}

/*
 * SysFs grpdata show function implementation of group 10.
 * Prints content of the system information Operational Configuration data.
 */
static int cyttsp4_grpdata_show_sysinfo_opcfg(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int num_read = dad->si->si_ofs.opcfg_size;
	int offset = dad->si->si_ofs.opcfg_ofs;

	return cyttsp4_grpdata_show_sysinfo(dev, ic_buf, length, num_read,
			offset);
}

/*
 * SysFs grpdata show function implementation of group 11.
 * Prints content of the system information Design data.
 */
static int cyttsp4_grpdata_show_sysinfo_design(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int num_read = dad->si->si_ofs.ddata_size;
	int offset = dad->si->si_ofs.ddata_ofs;

	return cyttsp4_grpdata_show_sysinfo(dev, ic_buf, length, num_read,
			offset);
}

/*
 * SysFs grpdata show function implementation of group 12.
 * Prints content of the system information Manufacturing data.
 */
static int cyttsp4_grpdata_show_sysinfo_manufacturing(struct device *dev,
		u8 *ic_buf, size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int num_read = dad->si->si_ofs.mdata_size;
	int offset = dad->si->si_ofs.mdata_ofs;

	return cyttsp4_grpdata_show_sysinfo(dev, ic_buf, length, num_read,
			offset);
}

/*
 * SysFs grpdata show function implementation of group 13.
 * Prints status register contents of Configuration and
 * Test registers.
 */
static int cyttsp4_grpdata_show_test_regs(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u8 mode;
	int rc = 0;
	int num_read = 0;
	int i;

	dev_vdbg(dev, "%s: test.cur_cmd=%d test.cur_mode=%d\n",
			__func__, dad->test.cur_cmd, dad->test.cur_mode);

	if (dad->test.cur_cmd == CY_CMD_CAT_NULL) {
		num_read = 1;
		if (length < num_read) {
			dev_err(dev, "%s: not sufficient buffer %s=%d, %s=%d\n",
					__func__, "req_buf_len", num_read,
					"length", length);
			return -EINVAL;
		}

		dev_vdbg(dev, "%s: GRP=TEST_REGS: NULL CMD: host_mode=%02X\n",
				__func__, ic_buf[0]);
		rc = cyttsp4_read(dad->ttsp,
				dad->test.cur_mode == CY_TEST_MODE_CAT ?
					CY_MODE_CAT : CY_MODE_OPERATIONAL,
				CY_REG_BASE, &mode, sizeof(mode));
		if (rc < 0) {
			ic_buf[0] = 0xFF;
			dev_err(dev, "%s: failed to read host mode r=%d\n",
					__func__, rc);
		} else {
			ic_buf[0] = mode;
		}
	} else if (dad->test.cur_mode == CY_TEST_MODE_CAT) {
		num_read = dad->test.cur_status_size;
		if (length < num_read) {
			dev_err(dev, "%s: not sufficient buffer %s=%d, %s=%d\n",
					__func__, "req_buf_len", num_read,
					"length", length);
			return -EINVAL;
		}
		if (dad->ic_grpoffset + num_read > CY_MAX_PRBUF_SIZE) {
			dev_err(dev,
				"%s: not sufficient source buffer req_bug_len=%d, length=%d\n",
				__func__, dad->ic_grpoffset + num_read,
				CY_MAX_PRBUF_SIZE);
			return -EINVAL;
		}

		dev_vdbg(dev, "%s: GRP=TEST_REGS: num_rd=%d at ofs=%d + "
				"grpofs=%d\n", __func__, num_read,
				dad->si->si_ofs.cmd_ofs, dad->ic_grpoffset);

		/* cmd result already put into dad->return_buf */
		for (i = 0; i < num_read; i++)
			ic_buf[i] = dad->return_buf[dad->ic_grpoffset + i];
	} else {
		dev_err(dev, "%s: Not in Config/Test mode\n", __func__);
	}

	return num_read;
}

/*
 * SysFs grpdata show function implementation of group 14.
 * Prints CapSense button keycodes.
 */
static int cyttsp4_grpdata_show_btn_keycodes(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	struct cyttsp4_btn *btn = dad->si->btn;
	int num_btns = dad->si->si_ofs.num_btns - dad->ic_grpoffset;
	int n;

	if (num_btns <= 0 || btn == NULL || length < num_btns)
		return -EINVAL;

	for (n = 0; n < num_btns; n++)
		ic_buf[n] = (u8) btn[dad->ic_grpoffset + n].key_code;

	return n;
}

/*
 * SysFs grpdata show function implementation of group 15.
 * Prints status register contents of Configuration and
 * Test registers.
 */
static int cyttsp4_grpdata_show_tthe_test_regs(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int rc = 0;
	int num_read = 0;

	dev_vdbg(dev, "%s: test.cur_cmd=%d test.cur_mode=%d\n",
			__func__, dad->test.cur_cmd, dad->test.cur_mode);

	if (dad->test.cur_cmd == CY_CMD_CAT_NULL) {
		num_read = dad->test.cur_status_size;
		if (length < num_read) {
			dev_err(dev, "%s: not sufficient buffer %s=%d, %s=%d\n",
					__func__, "req_buf_len", num_read,
					"length", length);
			return -EINVAL;
		}

		dev_vdbg(dev, "%s: GRP=TEST_REGS: NULL CMD: host_mode=%02X\n",
				__func__, ic_buf[0]);
		rc = cyttsp4_read(dad->ttsp,
				(dad->test.cur_mode == CY_TEST_MODE_CAT)
					? CY_MODE_CAT :
				(dad->test.cur_mode == CY_TEST_MODE_SYSINFO)
					? CY_MODE_SYSINFO : CY_MODE_OPERATIONAL,
				CY_REG_BASE, ic_buf, num_read);
		if (rc < 0) {
			ic_buf[0] = 0xFF;
			dev_err(dev, "%s: failed to read host mode r=%d\n",
					__func__, rc);
		}
	} else if (dad->test.cur_mode == CY_TEST_MODE_CAT
			|| dad->test.cur_mode == CY_TEST_MODE_SYSINFO) {
		num_read = dad->test.cur_status_size;
		if (length < num_read) {
			dev_err(dev, "%s: not sufficient buffer %s=%d, %s=%d\n",
					__func__, "req_buf_len", num_read,
					"length", length);
			return -EINVAL;
		}
		dev_vdbg(dev, "%s: GRP=TEST_REGS: num_rd=%d at ofs=%d + "
				"grpofs=%d\n", __func__, num_read,
				dad->si->si_ofs.cmd_ofs, dad->ic_grpoffset);
		rc = cyttsp4_read(dad->ttsp,
				(dad->test.cur_mode == CY_TEST_MODE_CAT)
					? CY_MODE_CAT : CY_MODE_SYSINFO,
				CY_REG_BASE, ic_buf, num_read);
		if (rc < 0)
			return rc;
	} else {
		dev_err(dev, "%s: In unsupported mode\n", __func__);
	}

	return num_read;
}

static cyttsp4_show_function
		cyttsp4_grpdata_show_functions[CY_IC_GRPNUM_NUM] = {
	[CY_IC_GRPNUM_RESERVED] = cyttsp4_grpdata_show_void,
	[CY_IC_GRPNUM_CMD_REGS] = cyttsp4_grpdata_show_operational_regs,
	[CY_IC_GRPNUM_TCH_REP] = cyttsp4_grpdata_show_touch_regs,
	[CY_IC_GRPNUM_DATA_REC] = cyttsp4_grpdata_show_sysinfo_data_rec,
	[CY_IC_GRPNUM_TEST_REC] = cyttsp4_grpdata_show_sysinfo_test_rec,
	[CY_IC_GRPNUM_PCFG_REC] = cyttsp4_grpdata_show_sysinfo_panel,
	[CY_IC_GRPNUM_TCH_PARM_VAL] = cyttsp4_grpdata_show_touch_params,
	[CY_IC_GRPNUM_TCH_PARM_SIZE] = cyttsp4_grpdata_show_touch_params_sizes,
	[CY_IC_GRPNUM_RESERVED1] = cyttsp4_grpdata_show_void,
	[CY_IC_GRPNUM_RESERVED2] = cyttsp4_grpdata_show_void,
	[CY_IC_GRPNUM_OPCFG_REC] = cyttsp4_grpdata_show_sysinfo_opcfg,
	[CY_IC_GRPNUM_DDATA_REC] = cyttsp4_grpdata_show_sysinfo_design,
	[CY_IC_GRPNUM_MDATA_REC] = cyttsp4_grpdata_show_sysinfo_manufacturing,
	[CY_IC_GRPNUM_TEST_REGS] = cyttsp4_grpdata_show_test_regs,
	[CY_IC_GRPNUM_BTN_KEYS] = cyttsp4_grpdata_show_btn_keycodes,
	[CY_IC_GRPNUM_TTHE_REGS] = cyttsp4_grpdata_show_tthe_test_regs,
};

static ssize_t cyttsp4_ic_grpdata_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int i;
	ssize_t num_read;
	int index;

	mutex_lock(&dad->sysfs_lock);
	dev_vdbg(dev, "%s: grpnum=%d grpoffset=%u\n",
			__func__, dad->ic_grpnum, dad->ic_grpoffset);

	index = scnprintf(buf, CY_MAX_PRBUF_SIZE,
			"Group %d, Offset %u:\n", dad->ic_grpnum,
			dad->ic_grpoffset);

	pm_runtime_get_sync(dev);
	num_read = cyttsp4_grpdata_show_functions[dad->ic_grpnum] (dev,
			dad->ic_buf, CY_MAX_PRBUF_SIZE);
	pm_runtime_put(dev);
	if (num_read < 0) {
		index = num_read;
		if (num_read == -ENOSYS) {
			dev_err(dev, "%s: Group %d is not implemented.\n",
				__func__, dad->ic_grpnum);
			goto cyttsp4_ic_grpdata_show_error;
		}
		dev_err(dev, "%s: Cannot read Group %d Data.\n",
				__func__, dad->ic_grpnum);
		goto cyttsp4_ic_grpdata_show_error;
	}

	for (i = 0; i < num_read; i++) {
		index += scnprintf(buf + index, CY_MAX_PRBUF_SIZE - index,
				"0x%02X\n", dad->ic_buf[i]);
	}

	index += scnprintf(buf + index, CY_MAX_PRBUF_SIZE - index,
			"(%d bytes)\n", num_read);

cyttsp4_ic_grpdata_show_error:
	mutex_unlock(&dad->sysfs_lock);
	return index;
}

static int _cyttsp4_cmd_handshake(struct cyttsp4_device_access_data *dad)
{
	struct device *dev = &dad->ttsp->dev;
	u8 mode;
	int rc;

	rc = cyttsp4_read(dad->ttsp, CY_MODE_CAT,
			CY_REG_BASE, &mode, sizeof(mode));
	if (rc < 0) {
		dev_err(dev, "%s: Fail read host mode r=%d\n", __func__, rc);
		return rc;
	}

	rc = cyttsp4_request_handshake(dad->ttsp, mode);
	if (rc < 0)
		dev_err(dev, "%s: Fail cmd handshake r=%d\n", __func__, rc);

	return rc;
}

static int _cyttsp4_cmd_toggle_lowpower(struct cyttsp4_device_access_data *dad)
{
	struct device *dev = &dad->ttsp->dev;
	u8 mode;
	int rc = cyttsp4_read(dad->ttsp,
			(dad->test.cur_mode == CY_TEST_MODE_CAT)
				? CY_MODE_CAT : CY_MODE_OPERATIONAL,
			CY_REG_BASE, &mode, sizeof(mode));
	if (rc < 0) {
		dev_err(dev, "%s: Fail read host mode r=%d\n",
				__func__, rc);
		return rc;
	}

	rc = cyttsp4_request_toggle_lowpower(dad->ttsp, mode);
	if (rc < 0)
		dev_err(dev, "%s: Fail cmd handshake r=%d\n",
				__func__, rc);
	return rc;
}

static int cyttsp4_test_cmd_mode(struct cyttsp4_device_access_data *dad,
		u8 *ic_buf, size_t length)
{
	struct device *dev = &dad->ttsp->dev;
	int rc = -ENOSYS;
	u8 mode;

	if (length < CY_NULL_CMD_MODE_INDEX + 1)  {
		dev_err(dev, "%s: %s length=%d\n", __func__,
				"Buffer length is not valid", length);
		return -EINVAL;
	}
	mode = ic_buf[CY_NULL_CMD_MODE_INDEX];

	if (mode == CY_HST_CAT) {
		rc = cyttsp4_request_exclusive(dad->ttsp,
				CY_REQUEST_EXCLUSIVE_TIMEOUT);
		if (rc < 0) {
			dev_err(dev, "%s: Fail rqst exclusive r=%d\n",
					__func__, rc);
			goto cyttsp4_test_cmd_mode_exit;
		}
		rc = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_CAT);
		if (rc < 0) {
			dev_err(dev, "%s: Fail rqst set mode=%02X r=%d\n",
					__func__, mode, rc);
			rc = cyttsp4_release_exclusive(dad->ttsp);
			if (rc < 0)
				dev_err(dev, "%s: %s r=%d\n", __func__,
						"Fail release exclusive", rc);
			goto cyttsp4_test_cmd_mode_exit;
		}
		dad->test.cur_mode = CY_TEST_MODE_CAT;
		dad->own_exclusive = true;
		dev_vdbg(dev, "%s: %s=%d %s=%02X %s=%d(CaT)\n", __func__,
				"own_exclusive", dad->own_exclusive == true,
				"mode", mode, "test.cur_mode",
				dad->test.cur_mode);
	} else if (mode == CY_HST_OPERATE) {
		if (dad->own_exclusive) {
			rc = cyttsp4_request_set_mode(dad->ttsp,
					CY_MODE_OPERATIONAL);
			if (rc < 0)
				dev_err(dev, "%s: %s=%02X r=%d\n", __func__,
						"Fail rqst set mode", mode, rc);
				/* continue anyway */

			rc = cyttsp4_release_exclusive(dad->ttsp);
			if (rc < 0) {
				dev_err(dev, "%s: %s r=%d\n", __func__,
						"Fail release exclusive", rc);
				/* continue anyway */
				rc = 0;
			}
			dad->test.cur_mode = CY_TEST_MODE_NORMAL_OP;
			dad->own_exclusive = false;
			dev_vdbg(dev, "%s: %s=%d %s=%02X %s=%d(Operate)\n",
					__func__, "own_exclusive",
					dad->own_exclusive == true,
					"mode", mode,
					"test.cur_mode", dad->test.cur_mode);
		} else
			dev_vdbg(dev, "%s: %s mode=%02X(Operate)\n", __func__,
					"do not own exclusive; cannot switch",
					mode);
	} else
		dev_vdbg(dev, "%s: unsupported mode switch=%02X\n",
				__func__, mode);

cyttsp4_test_cmd_mode_exit:
	return rc;
}

static int cyttsp4_test_tthe_cmd_mode(struct cyttsp4_device_access_data *dad,
		u8 *ic_buf, size_t length)
{
	struct device *dev = &dad->ttsp->dev;
	int rc = -ENOSYS;
	u8 mode;
	enum cyttsp4_test_mode test_mode;
	int new_mode;

	if (length < CY_NULL_CMD_MODE_INDEX + 1)  {
		dev_err(dev, "%s: %s length=%d\n", __func__,
				"Buffer length is not valid", length);
		return -EINVAL;
	}
	mode = ic_buf[CY_NULL_CMD_MODE_INDEX];

	switch (mode) {
	case CY_HST_CAT:
		new_mode = CY_MODE_CAT;
		test_mode = CY_TEST_MODE_CAT;
		break;
	case CY_HST_OPERATE:
		new_mode = CY_MODE_OPERATIONAL;
		test_mode = CY_TEST_MODE_NORMAL_OP;
		break;
	case CY_HST_SYSINFO:
		new_mode = CY_MODE_SYSINFO;
		test_mode = CY_TEST_MODE_SYSINFO;
		break;
	default:
		dev_vdbg(dev, "%s: unsupported mode switch=%02X\n",
				__func__, mode);
		goto cyttsp4_test_tthe_cmd_mode_exit;
	}

	rc = cyttsp4_request_exclusive(dad->ttsp, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Fail rqst exclusive r=%d\n", __func__, rc);
		goto cyttsp4_test_tthe_cmd_mode_exit;
	}
	rc = cyttsp4_request_set_mode(dad->ttsp, new_mode);
	if (rc < 0)
		dev_err(dev, "%s: Fail rqst set mode=%02X r=%d\n",
				__func__, mode, rc);
	rc = cyttsp4_release_exclusive(dad->ttsp);
	if (rc < 0) {
		dev_err(dev, "%s: %s r=%d\n", __func__,
				"Fail release exclusive", rc);
		if (mode == CY_HST_OPERATE)
			rc = 0;
		else
			goto cyttsp4_test_tthe_cmd_mode_exit;
	}
	dad->test.cur_mode = test_mode;
	dev_vdbg(dev, "%s: %s=%d %s=%02X %s=%d\n", __func__,
			"own_exclusive", dad->own_exclusive == true,
			"mode", mode,
			"test.cur_mode", dad->test.cur_mode);

cyttsp4_test_tthe_cmd_mode_exit:
	return rc;
}

/*
 * SysFs grpdata store function implementation of group 1.
 * Stores to command and parameter registers of Operational mode.
 */
static int cyttsp4_grpdata_store_operational_regs(struct device *dev,
		u8 *ic_buf, size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	size_t cmd_ofs = dad->si->si_ofs.cmd_ofs;
	int num_read = dad->si->si_ofs.rep_ofs - dad->si->si_ofs.cmd_ofs;
	u8 *return_buf = dad->return_buf;
	int rc;

	if ((cmd_ofs + length) > dad->si->si_ofs.rep_ofs) {
		dev_err(dev, "%s: %s length=%d\n", __func__,
				"Buffer length is not valid", length);
		return -EINVAL;
	}

	return_buf[0] = ic_buf[0];
	rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_OPERATIONAL,
			ic_buf, length,
			return_buf + 1, num_read,
			CY_DA_COMMAND_COMPLETE_TIMEOUT);
	if (rc < 0)
		dev_err(dev, "%s: Fail to execute cmd r=%d\n", __func__, rc);

	return rc;
}

/*
 * SysFs store function of Test Regs group.
 */
static int cyttsp4_grpdata_store_test_regs(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int rc;
	u8 *return_buf = dad->return_buf;

	/* Caller function guaranties, length is not bigger than ic_buf size */
	if (length < CY_CMD_INDEX + 1) {
		dev_err(dev, "%s: %s length=%d\n", __func__,
				"Buffer length is not valid", length);
		return -EINVAL;
	}

	dad->test.cur_cmd = ic_buf[CY_CMD_INDEX];
	if (dad->test.cur_cmd == CY_CMD_CAT_NULL) {
		if (length < CY_NULL_CMD_INDEX + 1) {
			dev_err(dev, "%s: %s length=%d\n", __func__,
					"Buffer length is not valid", length);
			return -EINVAL;
		}
		dev_vdbg(dev, "%s: test-cur_cmd=%d null-cmd=%d\n", __func__,
				dad->test.cur_cmd, ic_buf[CY_NULL_CMD_INDEX]);
		switch (ic_buf[CY_NULL_CMD_INDEX]) {
		case CY_NULL_CMD_NULL:
			dev_err(dev, "%s: empty NULL cmd\n", __func__);
			break;
		case CY_NULL_CMD_MODE:
			if (length < CY_NULL_CMD_MODE_INDEX + 1) {
				dev_err(dev, "%s: %s length=%d\n", __func__,
						"Buffer length is not valid",
						length);
				return -EINVAL;
			}
			dev_vdbg(dev, "%s: Set cmd mode=%02X\n", __func__,
					ic_buf[CY_NULL_CMD_MODE_INDEX]);
			cyttsp4_test_cmd_mode(dad, ic_buf, length);
			break;
		case CY_NULL_CMD_STATUS_SIZE:
			if (length < CY_NULL_CMD_SIZE_INDEX + 1) {
				dev_err(dev, "%s: %s length=%d\n", __func__,
						"Buffer length is not valid",
						length);
				return -EINVAL;
			}
			dad->test.cur_status_size =
				ic_buf[CY_NULL_CMD_SIZEL_INDEX]
				+ (ic_buf[CY_NULL_CMD_SIZEH_INDEX] << 8);
			dev_vdbg(dev, "%s: test-cur_status_size=%d\n",
					__func__, dad->test.cur_status_size);
			break;
		case CY_NULL_CMD_HANDSHAKE:
			dev_vdbg(dev, "%s: try null cmd handshake\n",
					__func__);
			rc = _cyttsp4_cmd_handshake(dad);
			if (rc < 0)
				dev_err(dev, "%s: %s r=%d\n", __func__,
						"Fail test cmd handshake", rc);
		default:
			break;
		}
	} else {
		dev_dbg(dev, "%s: TEST CMD=0x%02X length=%d %s%d\n",
				__func__, ic_buf[0], length, "cmd_ofs+grpofs=",
				dad->ic_grpoffset + dad->si->si_ofs.cmd_ofs);
		cyttsp4_pr_buf(dev, dad->pr_buf, ic_buf, length, "test_cmd");
		return_buf[0] = ic_buf[0]; /* Save cmd byte to return_buf */
		rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_CAT,
				ic_buf, length,
				return_buf + 1, dad->test.cur_status_size,
				CY_DA_COMMAND_COMPLETE_TIMEOUT);
		if (rc < 0)
			dev_err(dev, "%s: Fail to execute cmd r=%d\n",
					__func__, rc);
	}
	return 0;
}

/*
 * SysFs store function of Test Regs group.
 */
static int cyttsp4_grpdata_store_tthe_test_regs(struct device *dev, u8 *ic_buf,
		size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int rc;

	/* Caller function guaranties, length is not bigger than ic_buf size */
	if (length < CY_CMD_INDEX + 1) {
		dev_err(dev, "%s: %s length=%d\n", __func__,
				"Buffer length is not valid", length);
		return -EINVAL;
	}

	dad->test.cur_cmd = ic_buf[CY_CMD_INDEX];
	if (dad->test.cur_cmd == CY_CMD_CAT_NULL) {
		if (length < CY_NULL_CMD_INDEX + 1) {
			dev_err(dev, "%s: %s length=%d\n", __func__,
					"Buffer length is not valid", length);
			return -EINVAL;
		}
		dev_vdbg(dev, "%s: test-cur_cmd=%d null-cmd=%d\n", __func__,
				dad->test.cur_cmd, ic_buf[CY_NULL_CMD_INDEX]);
		switch (ic_buf[CY_NULL_CMD_INDEX]) {
		case CY_NULL_CMD_NULL:
			dev_err(dev, "%s: empty NULL cmd\n", __func__);
			break;
		case CY_NULL_CMD_MODE:
			if (length < CY_NULL_CMD_MODE_INDEX + 1) {
				dev_err(dev, "%s: %s length=%d\n", __func__,
						"Buffer length is not valid",
						length);
				return -EINVAL;
			}
			dev_vdbg(dev, "%s: Set cmd mode=%02X\n", __func__,
					ic_buf[CY_NULL_CMD_MODE_INDEX]);
			cyttsp4_test_tthe_cmd_mode(dad, ic_buf, length);
			break;
		case CY_NULL_CMD_STATUS_SIZE:
			if (length < CY_NULL_CMD_SIZE_INDEX + 1) {
				dev_err(dev, "%s: %s length=%d\n", __func__,
						"Buffer length is not valid",
						length);
				return -EINVAL;
			}
			dad->test.cur_status_size =
				ic_buf[CY_NULL_CMD_SIZEL_INDEX]
				+ (ic_buf[CY_NULL_CMD_SIZEH_INDEX] << 8);
			dev_vdbg(dev, "%s: test-cur_status_size=%d\n",
					__func__, dad->test.cur_status_size);
			break;
		case CY_NULL_CMD_HANDSHAKE:
			dev_vdbg(dev, "%s: try null cmd handshake\n",
					__func__);
			rc = _cyttsp4_cmd_handshake(dad);
			if (rc < 0)
				dev_err(dev, "%s: %s r=%d\n", __func__,
						"Fail test cmd handshake", rc);
		case CY_NULL_CMD_LOW_POWER:
			dev_vdbg(dev, "%s: try null cmd low power\n", __func__);
			rc = _cyttsp4_cmd_toggle_lowpower(dad);
			if (rc < 0)
				dev_err(dev, "%s: %s r=%d\n", __func__,
					"Fail test cmd toggle low power", rc);
		default:
			break;
		}
	} else {
		dev_dbg(dev, "%s: TEST CMD=0x%02X length=%d %s%d\n",
				__func__, ic_buf[0], length, "cmd_ofs+grpofs=",
				dad->ic_grpoffset + dad->si->si_ofs.cmd_ofs);
		cyttsp4_pr_buf(dev, dad->pr_buf, ic_buf, length, "test_cmd");
		/* Support Operating mode command. */
		rc = cyttsp4_write(dad->ttsp,
				(dad->test.cur_mode == CY_TEST_MODE_CAT)
					?  CY_MODE_CAT : CY_MODE_OPERATIONAL,
				dad->ic_grpoffset + dad->si->si_ofs.cmd_ofs,
				ic_buf, length);
		if (rc < 0)
			dev_err(dev, "%s: Fail write cmd regs r=%d\n",
					__func__, rc);
	}
	return 0;
}

/*
 * Gets user input from sysfs and parse it
 * return size of parsed output buffer
 */
static int cyttsp4_ic_parse_input(struct device *dev, const char *buf,
		size_t buf_size, u8 *ic_buf, size_t ic_buf_size)
{
	const char *pbuf = buf;
	unsigned long value;
	char scan_buf[CYTTSP4_INPUT_ELEM_SZ];
	int i = 0;
	int j;
	int last = 0;
	int ret;

	dev_dbg(dev, "%s: pbuf=%p buf=%p size=%d %s=%d buf=%s\n", __func__,
			pbuf, buf, (int) buf_size, "scan buf size",
			CYTTSP4_INPUT_ELEM_SZ, buf);

	while (pbuf <= (buf + buf_size)) {
		if (i >= CY_MAX_CONFIG_BYTES) {
			dev_err(dev, "%s: %s size=%d max=%d\n", __func__,
					"Max cmd size exceeded", i,
					CY_MAX_CONFIG_BYTES);
			return -EINVAL;
		}
		if (i >= ic_buf_size) {
			dev_err(dev, "%s: %s size=%d buf_size=%d\n", __func__,
					"Buffer size exceeded", i, ic_buf_size);
			return -EINVAL;
		}
		while (((*pbuf == ' ') || (*pbuf == ','))
				&& (pbuf < (buf + buf_size))) {
			last = *pbuf;
			pbuf++;
		}

		if (pbuf >= (buf + buf_size))
			break;

		memset(scan_buf, 0, CYTTSP4_INPUT_ELEM_SZ);
		if ((last == ',') && (*pbuf == ',')) {
			dev_err(dev, "%s: %s \",,\" not allowed.\n", __func__,
					"Invalid data format.");
			return -EINVAL;
		}
		for (j = 0; j < (CYTTSP4_INPUT_ELEM_SZ - 1)
				&& (pbuf < (buf + buf_size))
				&& (*pbuf != ' ')
				&& (*pbuf != ','); j++) {
			last = *pbuf;
			scan_buf[j] = *pbuf++;
		}

		ret = strict_strtoul(scan_buf, 16, &value);
		if (ret < 0) {
			dev_err(dev, "%s: %s '%s' %s%s i=%d r=%d\n", __func__,
					"Invalid data format. ", scan_buf,
					"Use \"0xHH,...,0xHH\"", " instead.",
					i, ret);
			return ret;
		}

		ic_buf[i] = value;
		i++;
	}
	
	return i;
}

static const u8 security_key[] = {
	0xA5, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD, 0x5A
};

#define RAW_FILTER_MASK_OFFSET  0x0204
#define RAW_FILTER_GRP_NUM      6

static u16 cyttsp4_calc_partial_app_crc(const u8 *data, int size, u16 crc)
{
	int i, j;

	for (i = 0; i < size; i++) {
		crc ^= ((u16)data[i] << 8);
		for (j = 8; j > 0; j--)
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x1021;
			else
				crc <<= 1;
	}

	return crc;
}

static inline u16 cyttsp4_calc_app_crc(const u8 *data, int size)
{
	return cyttsp4_calc_partial_app_crc(data, size, 0xFFFF);
}

static int cyttsp4_cat_exec_cmd(struct cyttsp4_device_access_data* dad, 
	u8* cmd, u8* cmd_param, int param_size, 
	u8* return_buf, int size_return);

static int cyttsp4_write_config_block(struct cyttsp4_device_access_data *dad, u8 ebid,
		u16 row, const u8 *data, u16 length)
{
	u8 return_buf[CY_CMD_CAT_WRITE_CFG_BLK_RET_SZ];
	u8 *command_buf;
	int command_buf_sz;
	u16 crc;
	int rc;

	/* Allocate buffer for write config block command
	 * Header(6) + Data(length) + Security Key(8) + CRC(2)
	 */
	command_buf_sz = CY_CMD_CAT_WRITE_CFG_BLK_CMD_SZ + length
		+ sizeof(security_key);
	command_buf = kmalloc(command_buf_sz, GFP_KERNEL);
	if (!command_buf) {
		printk(KERN_ERR "%s: Cannot allocate buffer\n",
			__func__);
		rc = -ENOMEM;
		goto exit;
	}

	crc = cyttsp4_calc_app_crc(data, length);

	command_buf[0] = CY_CMD_CAT_WRITE_CFG_BLK;
	command_buf[1] = HI_BYTE(row);
	command_buf[2] = LO_BYTE(row);
	command_buf[3] = HI_BYTE(length);
	command_buf[4] = LO_BYTE(length);
	command_buf[5] = ebid;

	command_buf[CY_CMD_CAT_WRITE_CFG_BLK_CMD_HDR_SZ + length
		+ sizeof(security_key)] = HI_BYTE(crc);
	command_buf[CY_CMD_CAT_WRITE_CFG_BLK_CMD_HDR_SZ + 1 + length
		+ sizeof(security_key)] = LO_BYTE(crc);

	memcpy(&command_buf[CY_CMD_CAT_WRITE_CFG_BLK_CMD_HDR_SZ], data,
		length);
	memcpy(&command_buf[CY_CMD_CAT_WRITE_CFG_BLK_CMD_HDR_SZ + length],
		security_key, sizeof(security_key));

	cyttsp4_pr_buf(&dad->ttsp->dev, dad->pr_buf, command_buf, command_buf_sz,
		"write_config_block");
	
	rc = cyttsp4_cat_exec_cmd(dad, command_buf, command_buf+1, 
		command_buf_sz - 1, return_buf, CY_CMD_CAT_WRITE_CFG_BLK_RET_SZ);
	if (rc) {
		printk(KERN_ERR "%s: Error executing command r=%d\n",
			__func__, rc);
		goto free_buffer;
	}

	/* Validate response */
	if (return_buf[0] != CY_CMD_STATUS_SUCCESS
			|| return_buf[1] != ebid
			|| return_buf[2] != HI_BYTE(length)
			|| return_buf[3] != LO_BYTE(length)) {
		printk(KERN_ERR "%s: Fail executing command\n",
				__func__);
		rc = -EINVAL;
		goto free_buffer;
	}
	
	
free_buffer:
	cyttsp4_pr_buf(&dad->ttsp->dev, dad->pr_buf, return_buf, CY_CMD_CAT_WRITE_CFG_BLK_RET_SZ, "write_config_return_buf");
	
	kfree(command_buf);
exit:
	return rc;
}

static int cyttsp4_read_config_block(struct cyttsp4_device_access_data *dad, u8 ebid,
		u16 row, u8 *data, u16 length)
{
	u8 command_buf[CY_CMD_CAT_READ_CFG_BLK_CMD_SZ];
	u8 *return_buf;
	int return_buf_sz;
	u16 crc;
	int rc;
	
	/* Allocate buffer for read config block command response
	 * Header(5) + Data(length) + CRC(2)
	 */
	return_buf_sz = CY_CMD_CAT_READ_CFG_BLK_RET_SZ + length;
	return_buf = kzalloc(return_buf_sz, GFP_KERNEL);
	if (!return_buf) {
		printk(KERN_ERR "%s: Cannot allocate buffer\n",
			__func__);
		rc = -ENOMEM;
		goto exit;
	}
	if (dad->ebid_row_size == 0) {
		rc = _cyttsp4_get_ebid_row_size(&(dad->ttsp->dev));
		if (rc < 0)
			goto free_buffer;
	}
	
	command_buf[0] = CY_CMD_CAT_READ_CFG_BLK;
	command_buf[1] = HI_BYTE(row);
	command_buf[2] = LO_BYTE(row);
	command_buf[3] = HI_BYTE(length);
	command_buf[4] = LO_BYTE(length);
	command_buf[5] = ebid;

	rc = cyttsp4_cat_exec_cmd(dad, command_buf, command_buf+1, 
		sizeof(command_buf)-1, return_buf, return_buf_sz);		
	if (rc) {
		printk(KERN_ERR "%s: fail to execute command r=%d\n",
			__func__, rc);
		goto free_buffer;
	}

	crc = cyttsp4_calc_app_crc(
		&return_buf[CY_CMD_CAT_READ_CFG_BLK_RET_HDR_SZ], length);

	/* Validate response */
	if (return_buf[0] != CY_CMD_STATUS_SUCCESS
			|| return_buf[1] != ebid
			|| return_buf[2] != HI_BYTE(length)
			|| return_buf[3] != LO_BYTE(length)
			|| return_buf[CY_CMD_CAT_READ_CFG_BLK_RET_HDR_SZ
				+ length] != HI_BYTE(crc)
			|| return_buf[CY_CMD_CAT_READ_CFG_BLK_RET_HDR_SZ
				+ length + 1] != LO_BYTE(crc)) {
		printk(KERN_ERR "%s: executed command return failure\n",
				__func__);
		rc = -EINVAL;
		goto free_buffer;
	}

	memcpy(data, &return_buf[CY_CMD_CAT_READ_CFG_BLK_RET_HDR_SZ], length);

free_buffer:
	cyttsp4_pr_buf(&(dad->ttsp->dev), dad->pr_buf, return_buf, return_buf_sz, "return_buf_read_config_block");
	cyttsp4_pr_buf(&(dad->ttsp->dev), dad->pr_buf, data,       length,        "data_read_config_block");

	kfree(return_buf);
exit:
	return rc;
}

static int cyttsp4_get_config_length(struct cyttsp4_device_access_data *dad, u8 ebid,
		u16 *length, u16 *max_length)
{ 
	struct cyttsp4_sysinfo *si = cyttsp4_request_sysinfo(dad->ttsp);
	u8 data[CY_CONFIG_LENGTH_INFO_SIZE];
	int rc;

	if (!si->ready) {
		rc  = -ENODEV;
		goto exit;
	}

	rc = cyttsp4_read_config_block(dad, ebid, CY_CONFIG_LENGTH_INFO_OFFSET,
			data, sizeof(data));
	if (rc) {
		printk(KERN_ERR "%s: Error on read config block\n",
			__func__);
		goto exit;
	}

	*length = get_unaligned_le16(&data[CY_CONFIG_LENGTH_OFFSET]);
	*max_length = get_unaligned_le16(&data[CY_CONFIG_MAXLENGTH_OFFSET]);

exit:
	return rc;
}

static int cyttsp4_verify_config_block_crc(struct cyttsp4_device_access_data *dad,
		u8 ebid, u16 *calc_crc, u16 *stored_crc, bool *match)
{
	u8 command_buf[CY_CMD_CAT_VERIFY_CFG_BLK_CRC_CMD_SZ];
	u8 return_buf[CY_CMD_CAT_VERIFY_CFG_BLK_CRC_RET_SZ];
	int rc;

	command_buf[0] = CY_CMD_CAT_VERIFY_CFG_BLK_CRC;
	command_buf[1] = ebid;


	rc = cyttsp4_cat_exec_cmd(dad, command_buf, command_buf+1, 
		sizeof(command_buf)-1, return_buf, sizeof(return_buf));
	if (rc) {
		printk(KERN_ERR "%s: Error executing command r=%d\n",
			__func__, rc);
		goto exit;
	}
	
	printk(KERN_ERR "%s return_buf: %x %x %x %x %x \n",
		__func__, return_buf[0], return_buf[1], return_buf[2], return_buf[3], return_buf[4]);

	*calc_crc   = get_unaligned_le16(&return_buf[1]);
	*stored_crc = get_unaligned_le16(&return_buf[3]);
	if (match)
		*match = !return_buf[0];
exit:
	return rc;
}

static int cyttsp4_get_config_row_size(struct cyttsp4_device_access_data *dad,
		u16 *config_row_size)
{
	u8 command_buf[CY_CMD_CAT_GET_CFG_ROW_SIZE_CMD_SZ];
	u8 return_buf[CY_CMD_CAT_GET_CFG_ROW_SIZE_RET_SZ];
	int rc;

	command_buf[0] = CY_CMD_CAT_GET_CFG_ROW_SZ;
	
	rc = cyttsp4_cat_exec_cmd(dad, command_buf, command_buf+1, 
		sizeof(command_buf)-1, return_buf, sizeof(return_buf));
	if (rc) {
		printk(KERN_ERR "%s: Error executing command r=%d\n",
			__func__, rc);
		goto exit;
	}

	*config_row_size = get_unaligned_be16(&return_buf[0]);

exit:
	return rc;
}

static int cyttsp4_write_config_common(struct cyttsp4_device_access_data *dad, u8 ebid,
		u16 offset, u8 *data, u16 length)
{
	u16 cur_block, cur_off, end_block, end_off;
	int copy_len;
	u16 config_row_size = 0;
	u8 *row_data = NULL;
	int rc;

	rc = cyttsp4_get_config_row_size(dad, &config_row_size);
	if (rc) {
		printk(KERN_ERR "%s: Cannot get config row size\n",
			__func__);
		goto exit;
	}

	cur_block = offset / config_row_size;
	cur_off = offset % config_row_size;
	
	end_block = (offset + length) / config_row_size;
	end_off = (offset + length) % config_row_size;

	/* Check whether we need to fetch the whole block first */
	if (cur_off == 0)
		goto no_offset;

	row_data = kmalloc(config_row_size, GFP_KERNEL);
	if (!row_data) {
		printk(KERN_ERR "%s: Cannot allocate buffer\n", __func__);
		rc = -ENOMEM;
		goto exit;
	}

	copy_len = (cur_block == end_block) ?
		length : config_row_size - cur_off;

	/* Read up to current offset, append the new data and write it back */
	rc = cyttsp4_read_config_block(dad, ebid, cur_block, row_data, cur_off);
	if (rc) {
		printk(KERN_ERR "%s: Error on read config block\n", __func__);
		goto free_row_data;
	}

	memcpy(&row_data[cur_off], data, copy_len);

	rc = cyttsp4_write_config_block(dad, ebid, cur_block, row_data,
			cur_off + copy_len);
	if (rc) {
		printk(KERN_ERR "%s: Error on initial write config block\n",
			__func__);
		goto free_row_data;
	}

	data += copy_len;
	cur_off = 0;
	cur_block++;

no_offset:
	while (cur_block < end_block) {
		rc = cyttsp4_write_config_block(dad, ebid, cur_block, data,
				config_row_size);
		if (rc) {
			printk(KERN_ERR "%s: Error on write config block\n",
				__func__);
			goto free_row_data;
		}

		data += config_row_size;
		cur_block++;
	}

	/* Last block */
	if (cur_block == end_block) {
		rc = cyttsp4_write_config_block(dad, ebid, end_block, data,
				end_off);
		if (rc) {
			printk(KERN_ERR "%s: Error on last write config block\n",
				__func__);
			goto free_row_data;
		}
	}

free_row_data:
	kfree(row_data);
exit:
	return rc;
}

static int cyttsp4_da_write_config(struct cyttsp4_device_access_data *dad, u8 ebid, u16 offset, u8 *data, u16 length) 
{
	struct cyttsp4_sysinfo *si = cyttsp4_request_sysinfo(dad->ttsp);
	u16 crc_new, crc_old;
	u16 crc_offset = 0;
	u16 conf_len;
	u8 crc_data[2];
	int rc;

	if (!si->ready) {
		rc  = -ENODEV;
		goto exit;
	}

	/* CRC is stored at config max length offset */
	rc = cyttsp4_get_config_length(dad, ebid, &conf_len, &crc_offset);
	if (rc) {
		printk(KERN_ERR "%s: Error on get config length\n",
			__func__);
		goto exit;
	}
	
	/* Allow CRC update also */
	if (offset + length > crc_offset + 2) {
		printk(KERN_ERR "%s: offset + length exceeds max length(%d)\n",
			__func__, crc_offset + 2);
		rc = -EINVAL;
		goto exit;
	}

	rc = cyttsp4_write_config_common(dad, ebid, offset, data, length);
	if (rc) {
		printk(KERN_ERR "%s: Error on write config\n",
			__func__);
		goto exit;
	}

	/* Verify config block CRC */
	rc = cyttsp4_verify_config_block_crc(dad, ebid,
			&crc_new, &crc_old, NULL);
	if (rc) {
		printk(KERN_ERR "%s: Error on verify config block crc\n",
			__func__);
		goto exit;
	}

	printk(KERN_ERR "%s: crc_new:%04X crc_old:%04X\n",
		__func__, crc_new, crc_old);

	if (crc_new == crc_old) {
		printk(KERN_INFO "%s: Calculated crc matches stored crc\n",
			__func__);
		goto exit;
	}

	put_unaligned_be16(crc_new, crc_data);
	
	rc = cyttsp4_write_config_common(dad, ebid, crc_offset , crc_data, 2);
	if (rc) {
		printk(KERN_ERR "%s: Error on write config crc\n",
			__func__);
		goto exit;
	}

exit:
	return rc;
}

/* 
 * SysFs grpdata store function implementation of group 6.
 * Stores the contents of the touch parameters.
 */
static int cyttsp4_grpdata_store_touch_params(struct device *dev,
		u8 *ic_buf, size_t length)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int rc, rc2 = 0, rc3 = 0;

	pm_runtime_get_sync(dev);

	rc = cyttsp4_request_exclusive(dad->ttsp,
			CY_DA_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto cyttsp4_grpdata_store_touch_params_err_put;
	}

	rc = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_CAT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request set mode r=%d\n",
				__func__, rc);
		goto cyttsp4_grpdata_store_touch_params_err_release;
	}

	rc = cyttsp4_da_write_config(dad, CY_TCH_PARM_EBID,
			dad->ic_grpoffset, ic_buf, length);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request write config r=%d\n",
				__func__, rc);
		goto cyttsp4_grpdata_store_touch_params_err_change_mode;
	}

cyttsp4_grpdata_store_touch_params_err_change_mode:
	rc2 = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_OPERATIONAL);
	if (rc2 < 0)
		dev_err(dev, "%s: Error on request set mode r=%d\n",
				__func__, rc2);

cyttsp4_grpdata_store_touch_params_err_release:
	rc3 = cyttsp4_release_exclusive(dad->ttsp);
	if (rc3 < 0)
		dev_err(dev, "%s: Error on release exclusive r=%d\n",
				__func__, rc3);

cyttsp4_grpdata_store_touch_params_err_put:
	pm_runtime_put(dev);

	if (rc == 0)
		cyttsp4_request_restart(dad->ttsp);
	else
		return rc;
	if (rc2 < 0)
		return rc2;
	if (rc3 < 0)
		return rc3;

	return rc;
}

/*
 * SysFs store functions of each group member.
 */
static cyttsp4_store_function
		cyttsp4_grpdata_store_functions[CY_IC_GRPNUM_NUM] = {
	[CY_IC_GRPNUM_RESERVED] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_CMD_REGS] = cyttsp4_grpdata_store_operational_regs,
	[CY_IC_GRPNUM_TCH_REP] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_DATA_REC] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_TEST_REC] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_PCFG_REC] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_TCH_PARM_VAL] = cyttsp4_grpdata_store_touch_params,
	[CY_IC_GRPNUM_TCH_PARM_SIZE] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_RESERVED1] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_RESERVED2] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_OPCFG_REC] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_DDATA_REC] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_MDATA_REC] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_TEST_REGS] = cyttsp4_grpdata_store_test_regs,
	[CY_IC_GRPNUM_BTN_KEYS] = cyttsp4_grpdata_store_void,
	[CY_IC_GRPNUM_TTHE_REGS] = cyttsp4_grpdata_store_tthe_test_regs,
};

static ssize_t cyttsp4_ic_grpdata_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	ssize_t length;
	int rc;

	mutex_lock(&dad->sysfs_lock);
	length = cyttsp4_ic_parse_input(dev, buf, size, dad->ic_buf,
			CY_MAX_PRBUF_SIZE);
	if (length <= 0) {
		dev_err(dev, "%s: %s Group Data store\n", __func__,
				"Malformed input for");
		goto cyttsp4_ic_grpdata_store_exit;
	}

	dev_vdbg(dev, "%s: grpnum=%d grpoffset=%u\n",
			__func__, dad->ic_grpnum, dad->ic_grpoffset);

	if (dad->ic_grpnum >= CY_IC_GRPNUM_NUM) {
		dev_err(dev, "%s: Group %d does not exist.\n",
				__func__, dad->ic_grpnum);
		goto cyttsp4_ic_grpdata_store_exit;
	}

	/* write ic_buf to log */
	cyttsp4_pr_buf(dev, dad->pr_buf, dad->ic_buf, length, "ic_buf");

	pm_runtime_get_sync(dev);
	/* Call relevant store handler. */
	rc = cyttsp4_grpdata_store_functions[dad->ic_grpnum] (dev, dad->ic_buf,
			length);
	pm_runtime_put(dev);
	if (rc < 0)
		dev_err(dev, "%s: Failed to store for grpmun=%d.\n",
				__func__, dad->ic_grpnum);

cyttsp4_ic_grpdata_store_exit:
	mutex_unlock(&dad->sysfs_lock);
	dev_vdbg(dev, "%s: return size=%d\n", __func__, size);
	return size;
}

/*
 * Execute scan command
 */
static int _cyttsp4_exec_scan_cmd(struct device *dev)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u8 cmd_buf[CY_CMD_CAT_GET_CFG_BLK_SZ_CMD_SZ];
	u8 return_buf[CY_CMD_CAT_GET_CFG_BLK_SZ_RET_SZ];
	int rc;

	rc = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_CAT);
	if (rc < 0) {
		printk(KERN_ERR "%s: Fail set config mode 1 r=%d\n", __func__, rc);
		return rc;
	}

	cmd_buf[0] = CY_CMD_CAT_EXEC_PANEL_SCAN;
	rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_CAT,
			cmd_buf, CY_CMD_CAT_EXEC_SCAN_CMD_SZ,
			return_buf, CY_CMD_CAT_EXEC_SCAN_RET_SZ,
			CY_DA_COMMAND_COMPLETE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Unable to send execute panel scan command.\n",
				__func__);
		return rc;
	}

	if (return_buf[0] != 0)
		return -EINVAL;
	return rc;
}

/*
 * Retrieve panel data command
 */
static int _cyttsp4_ret_scan_data_cmd(struct device *dev, int readOffset,
		int numElement, u8 dataType, u8 *return_buf)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u8 cmd_buf[CY_CMD_CAT_READ_CFG_BLK_CMD_SZ];
	int rc;

	cmd_buf[0] = CY_CMD_CAT_RETRIEVE_PANEL_SCAN;
	cmd_buf[1] = HI_BYTE(readOffset);
	cmd_buf[2] = LOW_BYTE(readOffset);
	cmd_buf[3] = HI_BYTE(numElement);
	cmd_buf[4] = LOW_BYTE(numElement);
	cmd_buf[5] = dataType;
	rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_CAT,
			cmd_buf, CY_CMD_CAT_RET_PANEL_DATA_CMD_SZ,
			return_buf, CY_CMD_CAT_RET_PANEL_DATA_RET_SZ,
			CY_DA_COMMAND_COMPLETE_TIMEOUT);
	if (rc < 0)
		return rc;

	if (return_buf[0] != 0)
		return -EINVAL;
	return rc;
}


/*
 * SysFs grpdata show function implementation of group 6.
 * Prints contents of the touch parameters a row at a time.
 */
static ssize_t cyttsp4_get_panel_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u8 return_buf[CY_CMD_CAT_RET_PANEL_DATA_RET_SZ];

	int rc = 0;
	int rc1 = 0;
	int dataIdx = -1;
	int i = 0;
	int printIdx = -1;
	u8 cmdParam_ofs = dad->si->si_ofs.cmd_ofs + 1;
	int readByte = CY_CMD_CAT_RET_PANEL_DATA_RET_SZ + cmdParam_ofs;
	int leftOverElement = 0;
	int returnedElement = 0;
	int readElementOffset = 0;
	int newline = 15;
	u8 elementStartOffset = cmdParam_ofs + CY_CMD_CAT_RET_PANEL_DATA_RET_SZ;
		
	if(lab126_board_is(BOARD_ID_MUSCAT_WAN) || lab126_board_is(BOARD_ID_MUSCAT_WFO) 
			|| lab126_board_is(BOARD_ID_MUSCAT_32G_WFO))
		newline = 40;
	
	if(lab126_board_is(BOARD_ID_WHISKY_WFO) || lab126_board_is(BOARD_ID_WHISKY_WAN))
		newline = 42;

	rc = cyttsp4_request_exclusive(dad->ttsp,
			CY_DA_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto cyttsp4_get_panel_data_show_err_release;
	}

	if (dad->heatmap.scan_start)	{
		/* Start scan */
		rc = _cyttsp4_exec_scan_cmd(dev);
		if (rc < 0)
			goto cyttsp4_get_panel_data_show_err_release;
	}
	/* retrieve scan data */
	rc = _cyttsp4_ret_scan_data_cmd(dev, CY_CMD_IN_DATA_OFFSET_VALUE,
			dad->heatmap.numElement, dad->heatmap.dataType,
			return_buf);
	if (rc < 0) {	
		printk(KERN_ERR "ret_scan_data_cmd return %d !!!!", rc);
		goto cyttsp4_get_panel_data_show_err_release;
	}
	if (return_buf[CY_CMD_OUT_STATUS_OFFSET] != CY_CMD_STATUS_SUCCESS) {
		printk(KERN_ERR "return buffer is not success, %d!!!!! ", return_buf[CY_CMD_OUT_STATUS_OFFSET]);
		goto cyttsp4_get_panel_data_show_err_release;
	}
	/* read data */
	readByte += (dad->heatmap.numElement *
			(return_buf[CY_CMD_RET_PNL_OUT_DATA_FORMAT_OFFS] &
				CY_CMD_RET_PANEL_ELMNT_SZ_MASK));

	if (readByte >= I2C_BUF_MAX_SIZE) {
		rc = cyttsp4_read(dad->ttsp, CY_MODE_CAT, 0, dad->ic_buf,
				I2C_BUF_MAX_SIZE);
		dataIdx = I2C_BUF_MAX_SIZE;
	} else {
		rc = cyttsp4_read(dad->ttsp, CY_MODE_CAT, 0, dad->ic_buf,
				readByte);
		dataIdx = readByte;
	}
	if (rc < 0) {
		dev_err(dev, "%s: Error on read r=%d\n", __func__, readByte);
		goto cyttsp4_get_panel_data_show_err_release;
	}

	if (readByte < I2C_BUF_MAX_SIZE)
		goto cyttsp4_get_panel_data_show_err_release;

	leftOverElement = dad->heatmap.numElement;
	readElementOffset = 0;
	returnedElement =
		return_buf[CY_CMD_RET_PNL_OUT_ELMNT_SZ_OFFS_H] * 256
		+ return_buf[CY_CMD_RET_PNL_OUT_ELMNT_SZ_OFFS_L];

	leftOverElement -= returnedElement;
	readElementOffset += returnedElement;
	do {
		/* get the data */
		rc = _cyttsp4_ret_scan_data_cmd(dev, readElementOffset,
				leftOverElement, dad->heatmap.dataType,
				return_buf);
		if (rc < 0)
			goto cyttsp4_get_panel_data_show_err_release;

		if (return_buf[CY_CMD_OUT_STATUS_OFFSET]
				!= CY_CMD_STATUS_SUCCESS)
			goto cyttsp4_get_panel_data_show_err_release;

		/* DO read */
		readByte = leftOverElement *
			(return_buf[CY_CMD_RET_PNL_OUT_DATA_FORMAT_OFFS]
				& CY_CMD_RET_PANEL_ELMNT_SZ_MASK);

		if (readByte >= (I2C_BUF_MAX_SIZE - elementStartOffset)) {
			rc = cyttsp4_read(dad->ttsp, CY_MODE_CAT,
					elementStartOffset,
					dad->ic_buf + dataIdx,
					I2C_BUF_MAX_SIZE - elementStartOffset);
			dataIdx += (I2C_BUF_MAX_SIZE - elementStartOffset);
		} else {
			rc = cyttsp4_read(dad->ttsp, CY_MODE_CAT,
					elementStartOffset,
					dad->ic_buf + dataIdx, readByte);
			dataIdx += readByte;
		}
		if (rc < 0) {
			dev_err(dev, "%s: Error on read r=%d\n", __func__, rc);
			goto cyttsp4_get_panel_data_show_err_release;
		}
		returnedElement =
			return_buf[CY_CMD_RET_PNL_OUT_ELMNT_SZ_OFFS_H] * 256
			+ return_buf[CY_CMD_RET_PNL_OUT_ELMNT_SZ_OFFS_L];
		/* Update element status */
		leftOverElement -= returnedElement;
		readElementOffset += returnedElement;

	} while (leftOverElement > 0);
	/* update on the buffer */
	dad->ic_buf[CY_CMD_RET_PNL_OUT_ELMNT_SZ_OFFS_H + cmdParam_ofs] =
		HI_BYTE(readElementOffset);
	dad->ic_buf[CY_CMD_RET_PNL_OUT_ELMNT_SZ_OFFS_L + cmdParam_ofs] =
		LOW_BYTE(readElementOffset);

cyttsp4_get_panel_data_show_err_release:
	rc1 = cyttsp4_release_exclusive(dad->ttsp);
	if (rc1 < 0) {
		dev_err(dev, "%s: Error on release exclusive r=%d\n",
				__func__, rc1);
		goto cyttsp4_get_panel_data_show_err_sysfs;
	}

	if (rc < 0)
		goto cyttsp4_get_panel_data_show_err_sysfs;

	printIdx = 0;
	printIdx += scnprintf(buf, CY_MAX_PRBUF_SIZE, "CY_DATA:");
	for (i = 0; i < 8; i++) {
		printIdx += scnprintf(buf + printIdx,
				CY_MAX_PRBUF_SIZE - printIdx,
				"%02X ", dad->ic_buf[i]);
	}
	printIdx += sprintf(buf + printIdx,	"\n");
	for (; i < dataIdx; i++) {
		printIdx += scnprintf(buf + printIdx,
				CY_MAX_PRBUF_SIZE - printIdx,
				"%02X ", dad->ic_buf[i]);
		if((i+ newline - 8 + 1) % newline == 0)
			printIdx += sprintf(buf + printIdx,	"\n");
	}
	printIdx += scnprintf(buf + printIdx, CY_MAX_PRBUF_SIZE - printIdx,
			":(%d bytes)\n\n\n--------------------", dataIdx);
	
	//print in readable decimal values
	for (i = 0; i < 8; i++) {
		printIdx += scnprintf(buf + printIdx,
				CY_MAX_PRBUF_SIZE - printIdx,
				"%02X ", dad->ic_buf[i]);
	}
	printIdx += sprintf(buf + printIdx,	"\n");
	
	
	for (; i < dataIdx; i+=2) {
		printIdx += scnprintf(buf + printIdx,
				CY_MAX_PRBUF_SIZE - printIdx,
				"%5d ", (short)(dad->ic_buf[i] + (dad->ic_buf[i+1] << 8)) );
		if((i+ newline - 8 + 1) % newline == (newline-1))
			printIdx += sprintf(buf + printIdx,	"\n");
	}

cyttsp4_get_panel_data_show_err_sysfs:
	return printIdx;
}

/*
 * SysFs grpdata show function implementation of group 6.
 * Prints contents of the touch parameters a row at a time.
 */
static int cyttsp4_get_panel_data_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	ssize_t length;

	mutex_lock(&dad->sysfs_lock);

	length = cyttsp4_ic_parse_input(dev, buf, size, dad->ic_buf,
			CY_MAX_PRBUF_SIZE);
	if (length <= 0) {
		dev_err(dev, "%s: %s Group Data store\n", __func__,
				"Malformed input for");
		goto cyttsp4_get_panel_data_store_exit;
	}

	dev_vdbg(dev, "%s: grpnum=%d grpoffset=%u\n",
			__func__, dad->ic_grpnum, dad->ic_grpoffset);

	if (dad->ic_grpnum >= CY_IC_GRPNUM_NUM) {
		dev_err(dev, "%s: Group %d does not exist.\n",
				__func__, dad->ic_grpnum);
		goto cyttsp4_get_panel_data_store_exit;
	}

	pm_runtime_get_sync(dev);
	/*update parameter value */
	dad->heatmap.numElement = dad->ic_buf[4] + (dad->ic_buf[3] * 256);
	dad->heatmap.dataType = dad->ic_buf[5];

	if (dad->ic_buf[6] > 0)
		dad->heatmap.scan_start = true;
	else
		dad->heatmap.scan_start = false;
	pm_runtime_put(dev);

cyttsp4_get_panel_data_store_exit:
	mutex_unlock(&dad->sysfs_lock);
	dev_vdbg(dev, "%s: return size=%d\n", __func__, size);
	return size;
}

/*
 * Retrieve Data Structure command
 */
static int _cyttsp4_retrieve_data_structure_cmd(struct device *dev,
                u16 offset, u16 length, u8 data_id, u8 *status,
                u8 *data_format, u16 *act_length, u8 *data)

{
        u8 cmd_buf[CY_CMD_CAT_RETRIEVE_DATA_STRUCT_CMD_SZ];
        u8 ret_buf[CY_CMD_CAT_RETRIEVE_DATA_STRUCT_RET_SZ];
        u16 total_read_length = 0;
        u16 read_length;
        u16 off_buf = 0;
        int rc;
	struct cyttsp4_device_access_data *dad
                = dev_get_drvdata(dev);
again:

        cmd_buf[0] = CY_CMD_CAT_RETRIEVE_DATA_STRUCTURE;
        cmd_buf[1] = HI_BYTE(offset);
        cmd_buf[2] = LO_BYTE(offset);
        cmd_buf[3] = HI_BYTE(length);
        cmd_buf[4] = LO_BYTE(length);
        cmd_buf[5] = data_id;

	rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_CAT,
                        cmd_buf, CY_CMD_CAT_RETRIEVE_DATA_STRUCT_CMD_SZ,
                        ret_buf, CY_CMD_CAT_RETRIEVE_DATA_STRUCT_RET_SZ,
                        CY_DA_COMMAND_COMPLETE_TIMEOUT);

        if (rc)
                goto exit;

        read_length = (ret_buf[2] << 8) + ret_buf[3];
	if (read_length && data) {
                /* Read data */
                rc = cyttsp4_read(dad->ttsp, CY_MODE_CAT, CY_REG_CAT_CMD + 1 +
                                CY_CMD_CAT_RETRIEVE_DATA_STRUCT_RET_SZ,
                                &data[off_buf], read_length);
                if (rc)
                        goto exit;

                total_read_length += read_length;

		if (read_length < length) {
                        offset += read_length;
                        off_buf += read_length;
                        length -= read_length;
                        goto again;
                
		}
	}

	if (status)
                *status = ret_buf[0];
        if (data_format)
                *data_format = ret_buf[4];
        if (act_length)
                *act_length = total_read_length;
exit:
        return rc;
}

static ssize_t cyttsp4_get_idac_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{

	struct cyttsp4_device_access_data *dad
                = dev_get_drvdata(dev);
	
	int status = STATUS_FAIL;
	u8 cmd_status = 0;
	u8 data_format = 0;
	u16 act_length = 0;
	int length;
	int size = 0;
	int rc;
	int i;
	mutex_lock(&dad->sysfs_lock);
	pm_runtime_get_sync(dev);
			
	rc = cyttsp4_request_exclusive(dad->ttsp,
			CY_DA_REQUEST_EXCLUSIVE_TIMEOUT);
 
	if (rc < 0) {
                dev_err(dev, "%s: Error on request exclusive r=%d\n",
                                __func__, rc);
                goto cyttsp4_get_idac_show_err_sysfs;
        }

	rc = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_CAT);
        if (rc < 0) {
                dev_err(dev, "%s: Error on request set mode to CAT r=%d\n",
                                __func__, rc);
                goto cyttsp4_get_idac_show_err_release;
        }
	
        /* If device type is TMA445, set read length to max */
	/* ic_buf[5] start of read data */ 
	rc = _cyttsp4_retrieve_data_structure_cmd(dev, 0, READ_LENGTH_MAX,
        	dad->get_idac_data_id, &cmd_status, &data_format,
                &act_length, &dad->ic_buf[5]);

	if (rc < 0) {
                dev_err(dev, "%s: Error on retrieve data structure r=%d\n",
                                __func__, rc);
                goto set_mode_to_operational;
        }	

	dad->ic_buf[0] = cmd_status;
        dad->ic_buf[1] = dad->get_idac_data_id;
        dad->ic_buf[2] = HI_BYTE(act_length);
        dad->ic_buf[3] = LO_BYTE(act_length);
        dad->ic_buf[4] = data_format;

        length = 5 + act_length;
        status = STATUS_SUCCESS;

set_mode_to_operational:
	cyttsp4_request_set_mode(dad->ttsp, CY_MODE_OPERATIONAL);

cyttsp4_get_idac_show_err_release:
	rc = cyttsp4_release_exclusive(dad->ttsp);
        if (rc < 0) {
                dev_err(dev, "%s: Error on release exclusive r=%d\n",
                                __func__, rc);
                goto cyttsp4_get_idac_show_err_sysfs;
        }

cyttsp4_get_idac_show_err_sysfs:
	pm_runtime_put(dev);
	
	size += scnprintf(buf, CY_MAX_PRBUF_SIZE, "IDAC_DATA:");
	
	if (status == STATUS_FAIL)
                length = 0;

	size += scnprintf(buf + size, CY_MAX_PRBUF_SIZE, "status %d\n", status);
	
	for (i = 0; i < length; i++) {
                size += scnprintf(buf + size,
                                CY_MAX_PRBUF_SIZE - size,
                                "%02X\n", dad->ic_buf[i]);
        }

        mutex_unlock(&dad->sysfs_lock);

	return size;
}

static int cyttsp4_get_idac_store(struct device *dev,
                struct device_attribute *attr, const char *buf, size_t size)
{
        struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
        ssize_t length;
	int rc;

        mutex_lock(&dad->sysfs_lock);

        length = cyttsp4_ic_parse_input(dev, buf, size, dad->ic_buf,
                        CY_MAX_PRBUF_SIZE);
	
        if (length <= 0) {
                dev_err(dev, "%s: %s Group Data store\n", __func__,
                                "Malformed input for");
		rc = -EINVAL;
                goto cyttsp4_get_idac_store_exit;
        }

        /*update parameter value */
	dad->get_idac_data_id = dad->ic_buf[0];
cyttsp4_get_idac_store_exit:
        mutex_unlock(&dad->sysfs_lock);
	
        dev_vdbg(dev, "%s: return size=%d\n", __func__, size);
        
	if (rc)
        	return rc;

	return size;
}

/* it's safer to write parameter first, then exec command */
static int cyttsp4_cat_exec_cmd(struct cyttsp4_device_access_data* dad, 
	u8* cmd, u8* cmd_param, int param_size, 
	u8* return_buf, int size_return)
{
	struct cyttsp4_device* ttsp = dad->ttsp;
	struct cyttsp4_core *core = ttsp->core;
	int retval;
	u8 empty[6] = {0};
	
	retval = cyttsp4_adap_write(core->adap, 0x3, empty, sizeof(empty));
	if (retval < 0) {
		printk(KERN_ERR " %s: fail to execute cmd=0x%x retval=%d when write empty param\n",
			__func__, cmd[0], retval);
		return retval;
	}
	if(param_size) {
		retval = cyttsp4_adap_write(core->adap, 0x3, cmd_param, param_size );
		if (retval < 0) {
			printk(KERN_ERR " %s: fail to execute cmd=0x%x retval=%d when write param\n",
				__func__, cmd[0], retval);
			return retval;
		}	
	}
	retval = cyttsp4_request_exec_cmd(ttsp, CY_MODE_CAT, cmd, 1, 
		return_buf, size_return, CY_DA_COMMAND_COMPLETE_TIMEOUT);		
	if (retval < 0) {
		printk(KERN_ERR " %s: fail to execute cmd=0x%x retval=%d\n",
			__func__, cmd[0], retval);
		return retval;
	} 
	retval = _cyttsp4_cmd_handshake(dad);
	if (retval < 0) {
		printk(KERN_ERR "%s: Command handshake error r=%d\n",
			__func__, retval);
		/* continue anyway; rely on handshake tmo */
		msleep(3000);
		retval = 0;
	}	
	return retval;
}

static int cyttsp4_baseline(struct cyttsp4_device_access_data *dad)
{
	int retval;
	u8 cmd[1];
	u8 cmd_param[3] = {0};
	
	struct cyttsp4_device* ttsp = dad->ttsp;

	printk(KERN_INFO "change mode config mode ");
	retval = cyttsp4_request_set_mode(ttsp, CY_MODE_CAT);
	if (retval < 0) {
		printk(KERN_ERR "%s: Fail set config mode 1 r=%d\n", __func__, retval);
		return retval;
	}
	
	retval = _cyttsp4_cmd_handshake(dad);
	if (retval < 0) {
		printk(KERN_ERR "%s: Command handshake error r=%d\n",
			__func__, retval);
		/* continue anyway; rely on handshake tmo */
		msleep(3000);
		retval = 0;
	}
	
	cmd[0] = CY_CMD_CAT_INIT_BASELINES;
	cmd_param[0] = 0x5;
	cmd_param[1] = 0x0;
	cmd_param[2] = 0x0;
	printk(KERN_INFO "baseline 1.. ");	
	retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), NULL, 0);
	if (retval < 0) {
		printk(KERN_ERR "%s: baseline 1.. failed r=%d\n",
		__func__, retval);
	}
	
	retval = cyttsp4_request_set_mode(ttsp, CY_MODE_OPERATIONAL);

	return retval;
}

static u8 idac_buf[309];

/*  status, self-test id, len[15:8], len[7:0], reserved, data*/
static u8 load_self_data[64];  

static int cyttsp4_calibrate(struct cyttsp4_device_access_data *dad, enum cy_cali_mode flag)
{
	int retval;
	u8 cmd[1];
	u8 cmd_param[3] = {0};
	u8 rds_cmd[6] = {0};
	
	struct cyttsp4_device* ttsp = dad->ttsp;
	struct cyttsp4_core *core = ttsp->core;
	
	printk(KERN_INFO "change mode config mode ");
	retval = cyttsp4_request_set_mode(ttsp, CY_MODE_CAT);
	if (retval < 0) {
		printk(KERN_ERR "%s: Fail set config mode 1 r=%d\n", __func__, retval);
		return retval;
	}
	
	////////////////////////1///////////////////	
	retval = _cyttsp4_cmd_handshake(dad);
	if (retval < 0) {
		printk(KERN_ERR "%s: Command handshake error r=%d\n",
			__func__, retval);
		/* continue anyway; rely on handshake tmo */
		msleep(3000);
		retval = 0;
	}	
	
	if(flag != 42) {  
		cmd[0] = CY_CMD_CAT_CALIBRATE_IDACS;
		cmd_param[0] = 0x0;
		cmd_param[1] = 0x0;
		cmd_param[2] = 0x0;
		printk(KERN_INFO "calibrating 1.. ");
		//ignore return_buf
		retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), NULL, 0);
		if (retval < 0) {
			printk(KERN_ERR "%s: calibrating 1.. failed r=%d\n",
			__func__, retval);
			return retval;
		}

		cmd[0] = CY_CMD_CAT_INIT_BASELINES;
		cmd_param[0] = 0x1;
		cmd_param[1] = 0x0;
		cmd_param[2] = 0x0;
		printk(KERN_INFO "baseline 1.. ");	
		retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), NULL, 0);
		if (retval < 0) {
			printk(KERN_ERR "%s: baseline 1.. failed r=%d\n",
			__func__, retval);
			return retval;
		}
	
		/////////////////////////2//////////////////////////
		
		cmd[0] = CY_CMD_CAT_CALIBRATE_IDACS;
		cmd_param[0] = 0x2;
		cmd_param[1] = 0x0;
		cmd_param[2] = 0x0;
		printk(KERN_INFO "calibrating 2.. ");
		retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), NULL, 0);
		if (retval < 0) {
			printk(KERN_ERR "%s: calibrating 2.. failed r=%d\n",
			__func__, retval);
			return retval;
		}

		
		cmd[0] = CY_CMD_CAT_INIT_BASELINES;
		cmd_param[0] = 0x4;
		cmd_param[1] = 0x0;
		cmd_param[2] = 0x0;
		printk(KERN_INFO "baseline 2.. ");
		retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), NULL, 0);
		if (retval < 0) {
			printk(KERN_ERR "%s: baseline 2.. failed r=%d\n",
			__func__, retval);
			return retval;
		}

		/////////////////////////3//////////////////////////
		cmd[0] = 0x09;
		cmd_param[0] = 0x3;
		cmd_param[1] = 0x0;
		cmd_param[2] = 0x0;
		printk(KERN_INFO "calibrating 3.. ");
		retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), NULL, 0);
		if (retval < 0) {
			printk(KERN_ERR "%s: calibrating 3.. failed r=%d\n",
			__func__, retval);
			return retval;
		}

		
		cmd[0] = CY_CMD_CAT_INIT_BASELINES;
		cmd_param[0] = 0x8;
		cmd_param[1] = 0x0;
		cmd_param[2] = 0x0;
		printk(KERN_INFO "baseline 3.. ");
		retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), NULL, 0);
		if (retval < 0) {
			printk(KERN_ERR "%s: baseline 3.. failed r=%d\n",
			__func__, retval);
			return retval;
		}

	}
	
	rds_cmd[0] = CY_CMD_CAT_RETRIEVE_DATA_STRUCTURE;
	rds_cmd[1] = 0x00;
	rds_cmd[2] = 0x00;
	rds_cmd[3] = 0x00;
	rds_cmd[4] = 0xF7;
	rds_cmd[5] = 0x00;
	printk(KERN_INFO "retrieve data structure.. ");
	retval = cyttsp4_cat_exec_cmd(dad, rds_cmd, rds_cmd+1, sizeof(rds_cmd) - 1, NULL, 0);
	
	if (retval < 0) {
		printk(KERN_ERR " %s: fail to rds_cmd retval=%d\n",
			__func__, retval);
	}
	
	switch(flag){
		case 42:
		case CY_CALI_SHOW_IDAC:	
			retval = cyttsp4_adap_read(core->adap, CY_REG_BASE,
				idac_buf, 0xff );
				
			if (retval < 0) {
					printk(KERN_ERR " %s: fail to read idac retval=%d\n",
					__func__, retval);
			} 
			
			rds_cmd[0] = CY_CMD_CAT_RETRIEVE_DATA_STRUCTURE;
			rds_cmd[1] = 0x00;
			rds_cmd[2] = 0xF7;
			rds_cmd[3] = 0x00;
			rds_cmd[4] = 0x36;
			rds_cmd[5] = 0x00;
			printk(KERN_INFO "retrieve data structure.. ");
			retval = cyttsp4_cat_exec_cmd(dad, rds_cmd, rds_cmd+1, sizeof(rds_cmd) - 1, NULL, 0);
	
			if (retval < 0) {
				printk(KERN_ERR " %s: fail to rds_cmd retval=%d\n",
					__func__, retval);
			}
			
			retval = cyttsp4_adap_read(core->adap, CY_REG_BASE+8,
				idac_buf+0xff, 0x36 );
				
			if (retval < 0) {
					printk(KERN_ERR " %s: fail to read idac retval=%d\n",
					__func__, retval);
			}
			break;
		case CY_CALI_NORMAL:
			printk(KERN_INFO "Finished. Operational now!");
			retval = cyttsp4_request_set_mode(ttsp, CY_MODE_OPERATIONAL);
			break;			
		default:
		break;
	}
	return retval;
}

static int cyttsp4_calibrate_(int flag)
{
	return cyttsp4_calibrate(g_da_data, flag);
}

static ssize_t cyttsp4_calibrate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	cyttsp4_calibrate(dad, CY_CALI_NORMAL);
	return 1;
}

static ssize_t cyttsp4_calibrate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	
	switch (buf[0]) {	
		case '1':
			cyttsp4_calibrate(dad, CY_CALI_NORMAL);
			break;
		case '9':
			cyttsp4_calibrate(dad, 42);
			break;
		case '8':
			cyttsp4_baseline(dad);
		default:
			printk(KERN_INFO "!!calibration does not return to operational mode!!\n");
			cyttsp4_calibrate(dad, CY_CALI_SHOW_IDAC);
			break;
	}
	return size;
}

static int cyttsp4_bist(struct cyttsp4_device_access_data *dad)
{
	int retval1 = -1, retval2 = -1;
	u8 cmd[1];
	u8 cmd_param[3] = {0};
	u8 retbuf[2] = {42, 42};
		
	struct cyttsp4_device* ttsp = dad->ttsp;
	
	printk(KERN_INFO "change mode config mode ");
	retval1 = cyttsp4_request_set_mode(ttsp, CY_MODE_CAT);
	if (retval1 < 0) {
		printk(KERN_ERR "%s: Fail set config mode 1 r=%d\n", __func__, retval1);
		goto done;
	}
	cmd[0] = CY_CMD_CAT_RUN_SELF_TEST;
	cmd_param[0] = CY_CAT_TEST_BIST; 
	cmd_param[1] = 0x01;

	retval2 = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), retbuf, 2);
	if (retval2 < 0) {
		printk(KERN_ERR " %s: fail to exe cmd retval=%d\n",
		__func__, retval2);
	}
done:
	return !retval1 && !retval2 && (retbuf[0] == 0) && (retbuf[1] == 0); 
}

static int cyttsp4_open_test(struct cyttsp4_device_access_data *dad)
{
	int retval;
	u8 cmd[1];
	u8 cmd_param[3] = {0};
	u8 rds_cmd[6] = {0};
		
	struct cyttsp4_device* ttsp = dad->ttsp;
	struct cyttsp4_core *core = ttsp->core;
	
	printk(KERN_INFO "change mode config mode ");
	retval = cyttsp4_request_set_mode(ttsp, CY_MODE_CAT);
	if (retval < 0) {
		printk(KERN_ERR "%s: Fail set config mode 1 r=%d\n", __func__, retval);
		return retval;
	}
			
	cmd[0] = CY_CMD_CAT_RUN_SELF_TEST;
	cmd_param[0] = CY_CAT_TEST_OPEN; 
	cmd_param[1] = 0x01;
	cmd_param[2] = 0x00;
	retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), NULL, 0);
	
	rds_cmd[0] = CY_CMD_CAT_RETRIEVE_DATA_STRUCTURE;
	rds_cmd[1] = 0x00; //offset high bits
	rds_cmd[2] = 0x00; //offset low bits
	rds_cmd[3] = 0x00; //len high bits
	rds_cmd[4] = 0xF7; //len low bits
	rds_cmd[5] = CY_RDS_MUTUAL_IDAC_CENTER; //data id	 
	printk(KERN_INFO "retrieve data structure.. ");
	
	retval = cyttsp4_cat_exec_cmd(dad, rds_cmd, rds_cmd+1, sizeof(rds_cmd) - 1, NULL, 0);
	
	if (retval < 0) {
		printk(KERN_ERR " %s: fail to rds_cmd retval=%d\n",
			__func__, retval);
	} 	
	
	retval = cyttsp4_adap_read(core->adap, CY_REG_BASE,
		idac_buf, 0xff );
	
	rds_cmd[0] = CY_CMD_CAT_RETRIEVE_DATA_STRUCTURE;
	rds_cmd[1] = 0x00; //offset high bits
	rds_cmd[2] = 0xF7; //offset low bits
	rds_cmd[3] = 0x00; //len high bits
	rds_cmd[4] = 0x36; //len low bits
	rds_cmd[5] = CY_RDS_MUTUAL_IDAC_CENTER; //data id	 
	printk(KERN_INFO "retrieve data structure.. ");
	
	retval = cyttsp4_cat_exec_cmd(dad, rds_cmd, rds_cmd+1, sizeof(rds_cmd) - 1, NULL, 0);
	
	if (retval < 0) {
		printk(KERN_ERR " %s: fail to rds_cmd retval=%d\n",
			__func__, retval);
	} 	
	
	retval = cyttsp4_adap_read(core->adap, CY_REG_BASE+8,
		idac_buf+0xff, 0x36 );
		
	if (retval < 0) {
			printk(KERN_ERR " %s: fail to read idac retval=%d\n",
			__func__, retval);
	} 
	
	return 0;
}

static int cyttsp4_short_test(struct cyttsp4_device_access_data *dad)
{
	int retval;
	u8 cmd[1];
	u8 cmd_param[6] = {0};
	int i;
	struct cyttsp4_device* ttsp = dad->ttsp;
	u8 return_buf[5];
	
	printk(KERN_INFO "change mode config mode ");
	retval = cyttsp4_request_set_mode(ttsp, CY_MODE_CAT);
	if (retval < 0) {
		printk(KERN_ERR "%s: Fail set config mode 1 r=%d\n", __func__, retval);
		return retval;
	}
	
	////////////////////////1///////////////////	
	retval = _cyttsp4_cmd_handshake(dad);
	if (retval < 0) {
		printk(KERN_ERR "%s: Command handshake error r=%d\n",
			__func__, retval);
		/* continue anyway; rely on handshake tmo */
		msleep(3000);
		retval = 0;
	}	
	cmd[0] = CY_CMD_CAT_RUN_SELF_TEST;
	cmd_param[0] = 0x04; //some how this reserved command is short test
	cmd_param[1] = 0x0;
	cmd_param[2] = 0x0;
	printk(KERN_INFO "short 1.. ");
	retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), return_buf, sizeof(return_buf));	
	printk(KERN_INFO "%s: %x %x %x %x %x\n", __func__, 
		return_buf[0], return_buf[1], return_buf[2], return_buf[3], return_buf[4]);
	
	cmd[0] = CY_CMD_CAT_GET_SELF_TEST_RESULT;
	cmd_param[0] = 0x0; //offset hi bits
	cmd_param[1] = 0x0; //offset lo bits
	cmd_param[2] = 0x0; //size hi bits
	cmd_param[3] = 27; //size lo bits
	cmd_param[4] = 0x04; //which command, somehow 0x04 is a reserved command..
	printk(KERN_INFO "short 2.. ");
	retval = cyttsp4_cat_exec_cmd(dad, cmd, cmd_param, sizeof(cmd_param), load_self_data, sizeof(load_self_data));	
	
	for (i=0; i < sizeof(load_self_data); i+=4)
		printk(KERN_INFO "%x %x %x %x\n", 
			load_self_data[i], load_self_data[i+1], load_self_data[i+2], load_self_data[i+3]);	
			
	return 0;
}

static ssize_t cyttsp4_short_test_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	cyttsp4_short_test(dad);
	return size;
}

static ssize_t cyttsp4_short_test_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char* ptr = buf;
	int i;
	int len; 
	u8* dataptr;
	
	len = (load_self_data[2] << 8) | load_self_data[3];
	dataptr = load_self_data+5; // offset of 5 bytes to data
	
	for (i=0; i < len ; i++) {
		if(i % 9 == 0)
			ptr += sprintf(ptr, "\n");
		ptr += sprintf(ptr, "0x%02x ", dataptr[i]);	
	}
	return ptr - buf;
}

static int prepare_print_buffer(int status, u8 *in_buf, int length,
                u8 *out_buf)
{
	int index = 0;
	int i;
	
	/* whisky only, 21 is the hardware number of sensors per line */
	int newline = 21;
        index += scnprintf(out_buf, CY_MAX_PRBUF_SIZE, "status %d\n", status);
	index += scnprintf(out_buf + index,  CY_MAX_PRBUF_SIZE - index, "pre-fix\n");
	
	for (i = 0; i < 5; i++)
		index += scnprintf(out_buf + index, CY_MAX_PRBUF_SIZE - index, "%02x ", in_buf[i]);
	
	index += sprintf(out_buf + index,     "\n");
        
	for (; i < length; i++) {
                index += scnprintf(out_buf + index, CY_MAX_PRBUF_SIZE - index, "%05d ", in_buf[i]);
		if((i + newline - 5 + 1) % newline == 0)
			index += sprintf(out_buf + index,     "\n");
        }
	index += scnprintf(out_buf + index, CY_MAX_PRBUF_SIZE - index,
			"length: %d \n\n\n--------------------", length - 5);
 
        return index;
}

/*
 * Get Opens Self Test Results command
 */
static int _cyttsp4_get_opens_self_test_results_cmd(struct device *dev,
                u16 offset, u16 length, u8 *status, u16 *act_length, u8 *data)
{
        u8 cmd_buf[CY_CMD_CAT_GET_OPENS_ST_RES_CMD_SZ];
        u8 ret_buf[CY_CMD_CAT_GET_OPENS_ST_RES_RET_SZ];
        u16 read_length;
        int rc;	
	struct cyttsp4_device_access_data *dad
                = dev_get_drvdata(dev);

        cmd_buf[0] = CY_CMD_CAT_GET_SELF_TEST_RESULT;
        cmd_buf[1] = HI_BYTE(offset);
        cmd_buf[2] = LO_BYTE(offset);
        cmd_buf[3] = HI_BYTE(length);
        cmd_buf[4] = LO_BYTE(length);
        cmd_buf[5] = CY_ST_ID_OPENS;

        rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_CAT,
                        cmd_buf, CY_CMD_CAT_GET_OPENS_ST_RES_CMD_SZ,
                        ret_buf, CY_CMD_CAT_GET_OPENS_ST_RES_RET_SZ,
                        CY_COMMAND_COMPLETE_TIMEOUT);
        if (rc)
                goto exit;

	read_length = (ret_buf[2] << 8) + ret_buf[3];
	if (read_length && data) {
	        /* Read data */
                rc = cyttsp4_read(dad->ttsp, CY_MODE_CAT, CY_REG_CAT_CMD + 1 +
                                CY_CMD_CAT_GET_OPENS_ST_RES_RET_SZ,
                                data, read_length);
                if (rc)
                        goto exit;
        }
	
        if (status)
                *status = ret_buf[0];
        if (act_length)
                *act_length = read_length;
	
exit:
        return rc;
}

/*
 * Run Opens Self Test command
 */
static int _cyttsp4_run_opens_self_test_cmd(struct device *dev,
                u8 write_idacs_to_flash, u8 *status, u8 *summary_result,
                u8 *results_available)
{
        u8 cmd_buf[CY_CMD_CAT_RUN_OPENS_ST_CMD_SZ];
        u8 ret_buf[CY_CMD_CAT_RUN_OPENS_ST_RET_SZ];
        int rc;

	struct cyttsp4_device_access_data *dad
                = dev_get_drvdata(dev);
        
	cmd_buf[0] = CY_CMD_CAT_RUN_SELF_TEST;
        cmd_buf[1] = CY_ST_ID_OPENS;
        cmd_buf[2] = write_idacs_to_flash;

        rc = cyttsp4_request_exec_cmd(dad->ttsp, CY_MODE_CAT,
                        cmd_buf, CY_CMD_CAT_RUN_OPENS_ST_CMD_SZ,
                        ret_buf, CY_CMD_CAT_RUN_OPENS_ST_RET_SZ,
                        CY_COMMAND_COMPLETE_TIMEOUT);
        if (rc)
                goto exit;

        if (status)
                *status = ret_buf[0];
        if (summary_result)
                *summary_result = ret_buf[1];
        if (results_available)
                *results_available = ret_buf[2];
exit:
        return rc;
}

static int _cyttsp4_run_opens_self_test_tma445(struct device *dev,
                u8 *status, u8 *summary_result)
{
        return _cyttsp4_run_opens_self_test_cmd(dev, 0, status,
                        summary_result, NULL);
}

static int _cyttsp4_get_opens_self_test_results_tma445(struct device *dev,
                u8 *status, u16 *act_length, u8 *data)
{
        /* Set length to 315 to read all */
        return _cyttsp4_get_opens_self_test_results_cmd(dev, 0, 315,
                        status, act_length, data);
}
	
static ssize_t cyttsp4_open_test_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u8 device_type;
	u8 test_type;
	u8 input_val;
	ssize_t length;

	mutex_lock(&dad->sysfs_lock);
	
	length = cyttsp4_ic_parse_input(dev, buf, size, dad->ic_buf,
                        CY_MAX_PRBUF_SIZE);

	/* Check device type */
        device_type = dad->ic_buf[0];
	test_type = dad->ic_buf[1];
	input_val = dad->ic_buf[2];
	dad->opens_device_type = device_type;
        dad->opens_test_type = test_type;
	
	if (device_type == DEVICE_TYPE_TMA4xx) {
		if(input_val == 1) {
			cyttsp4_open_test(dad);
			msleep(100);
			if(cyttsp4_calibrate(dad, CY_CALI_NORMAL) < 0)
			{
				memset(idac_buf, 0, sizeof(idac_buf));
			}
		} else 
			cyttsp4_calibrate(dad, 42);
	}
	/* return right after save device type for TMA445 */
	mutex_unlock(&dad->sysfs_lock);
	
	return size;
}

static ssize_t cyttsp4_open_test_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	int i;
	char* ptr = buf;
	int status = STATUS_FAIL;
	u8 cmd_status = 0;
	u8 summary_result = 0;
	u16 act_length = 0;
	int length;
	int size;
	int rc;
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);

	if (dad->opens_device_type == DEVICE_TYPE_TMA4xx) {
        	for (i = 0; i < sizeof(idac_buf); i++) {
                	if(i % 10 == 0)
                        	ptr += sprintf(ptr, "\n");
                	ptr += sprintf(ptr, "0x%02x ", idac_buf[i]);
        	}
       		return ptr - buf;
	}
	else if (dad->opens_device_type == DEVICE_TYPE_TMA445) {
		mutex_lock(&dad->sysfs_lock); 

		pm_runtime_get_sync(dev);

		rc = cyttsp4_request_exclusive(dad->ttsp,
				CY_DA_REQUEST_EXCLUSIVE_TIMEOUT);

		if (rc < 0) {
			dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
			goto put_pm_runtime;
		}

		rc = cyttsp4_request_set_mode(dad->ttsp, CY_MODE_CAT);

		if (rc < 0) {
			dev_err(dev, "%s: Error on request set mode to CAT r=%d\n",
				__func__, rc);
			goto release_exclusive;
		}
	
		/* Run Opens Self Test */
		/* For TMA445 */
		rc = _cyttsp4_run_opens_self_test_tma445(dev, &cmd_status,
			&summary_result);

		if (rc < 0) {
                	dev_err(dev, "%s: Error on run opens self test r=%d\n",
                                __func__, rc);
                	goto set_mode_to_operational;
        	}

		/* Form response buffer */
		dad->ic_buf[0] = cmd_status;
		dad->ic_buf[1] = summary_result;
	
		length = 2;

		rc = _cyttsp4_get_opens_self_test_results_tma445(dev,
			&cmd_status, &act_length, &dad->ic_buf[5]);

		if (rc < 0) {
			dev_err(dev, "%s: Error on get opens self test results r=%d\n",
				__func__, rc);
			goto set_mode_to_operational;
		}
		dad->ic_buf[2] = cmd_status;
		dad->ic_buf[3] = HI_BYTE(act_length);
		dad->ic_buf[4] = LO_BYTE(act_length);
		length = 5 + act_length;

		status = STATUS_SUCCESS;

set_mode_to_operational:
		cyttsp4_request_set_mode(dad->ttsp, CY_MODE_OPERATIONAL);

release_exclusive:
		rc = cyttsp4_release_exclusive(dad->ttsp);

put_pm_runtime:
		pm_runtime_put(dev);
	
		if (status == STATUS_FAIL)
			length = 0;
	
		size = prepare_print_buffer(status, dad->ic_buf, length, buf);
	
		mutex_unlock(&dad->sysfs_lock);
		return size;
	}
	
}

static ssize_t cyttsp4_bist_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char* ptr = buf;
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	
	ptr += sprintf(ptr, "%s", cyttsp4_bist(dad) ? "pass" : "fail" );
	
	return ptr - buf;
}

static ssize_t cyttsp4_cmf_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u16 reg;
	int rc;
	
	reg = simple_strtol(buf, NULL, 16);
	
	mutex_lock(&dad->sysfs_lock);
	/*
	 * Block grpnum change when own_exclusive flag is set
	 * which means the current grpnum implementation requires
	 * running exclusively on some consecutive grpdata operations
	 */
	if (dad->own_exclusive) {
		mutex_unlock(&dad->sysfs_lock);
		dev_err(dev, "%s: own_exclusive\n", __func__);
		return -EBUSY;
	}
	dad->ic_grpnum = RAW_FILTER_GRP_NUM;
	dad->ic_grpoffset = RAW_FILTER_MASK_OFFSET;
	memcpy(dad->ic_buf, &reg, 2);
	
	pm_runtime_get_sync(dev);
	
	rc = cyttsp4_grpdata_store_functions[dad->ic_grpnum] (dev, dad->ic_buf, 2);
	if (rc < 0)
		dev_err(dev, "%s: Failed to store for grpmun=%d.\n",
				__func__, dad->ic_grpnum);

	pm_runtime_put(dev);

	mutex_unlock(&dad->sysfs_lock);

	return size;
}

static ssize_t cyttsp4_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int rc;
	char* vendor;
	/*
	 * Block grpnum change when own_exclusive flag is set
	 * which means the current grpnum implementation requires
	 * running exclusively on some consecutive grpdata operations
	 */
	if (dad->own_exclusive) {
		mutex_unlock(&dad->sysfs_lock);
		dev_err(dev, "%s: own_exclusive\n", __func__);
		return -EBUSY;
	}
	dad->ic_grpnum = CY_IC_GRPNUM_PCFG_REC;
	dad->ic_grpoffset = 12;
	
	pm_runtime_get_sync(dev);
	
	rc = cyttsp4_grpdata_show_functions[dad->ic_grpnum] (dev, dad->ic_buf, 1);
	if (rc < 0)
		dev_err(dev, "%s: Failed to store for grpmun=%d.\n",
				__func__, dad->ic_grpnum);

	pm_runtime_put(dev);

	mutex_unlock(&dad->sysfs_lock);

	switch (dad->ic_buf[0]) {
		case 7:
			vendor = "Icewine AUO";
		break;
		case 6:
			vendor = "Icewine TPK";
		break;
		case 2:
			vendor = "Pinot Cando";
		break;
		case 3:
			vendor = "Pinot Wintek";
		break;
		default:
			vendor = "unknow";
		break;
	}
	
	return sprintf(buf, "0x%02x: %s", dad->ic_buf[0], vendor);
}

static struct device_attribute attributes[] = {
	__ATTR(vendor_id,		S_IRUSR ,			cyttsp4_vendor_show, NULL),
	__ATTR(bist,			S_IRUSR ,			cyttsp4_bist_show, NULL),
	__ATTR(calibrate,		S_IRUSR | S_IWUSR,	cyttsp4_calibrate_show, cyttsp4_calibrate_store),
	__ATTR(get_panel_data,	S_IRUSR | S_IWUSR,	cyttsp4_get_panel_data_show, cyttsp4_get_panel_data_store),
	__ATTR(ic_grpdata,		S_IRUSR | S_IWUSR,	cyttsp4_ic_grpdata_show, cyttsp4_ic_grpdata_store),
	__ATTR(ic_grpoffset,	S_IRUSR | S_IWUSR,	cyttsp4_ic_grpoffset_show, cyttsp4_ic_grpoffset_store),
	__ATTR(ic_grpnum,		S_IRUSR | S_IWUSR,	cyttsp4_ic_grpnum_show, cyttsp4_ic_grpnum_store),
	__ATTR(open_test,		S_IRUSR | S_IWUSR,	cyttsp4_open_test_show, cyttsp4_open_test_store),
	__ATTR(short_test,		S_IRUSR | S_IWUSR,	cyttsp4_short_test_show, cyttsp4_short_test_store),
	__ATTR(touch_filter,	S_IWUSR,			NULL, cyttsp4_cmf_store),
	__ATTR(get_idac, 	S_IRUSR | S_IWUSR,      cyttsp4_get_idac_show, cyttsp4_get_idac_store),	
};

#ifdef CONFIG_PM_SLEEP
static int cyttsp4_device_access_suspend(struct device *dev)
{
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	if (!mutex_trylock(&dad->sysfs_lock))
		return -EBUSY;

	mutex_unlock(&dad->sysfs_lock);
	return 0;
}

static int cyttsp4_device_access_resume(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);

	return 0;
}
#endif

static int cyttsp4_setup_sysfs(struct cyttsp4_device *ttsp)
{
	struct device *dev = &ttsp->dev;
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
	{	
		if (device_create_file(dev, attributes + i))
			goto undo;
	}	
	dad->sysfs_nodes_created = true;
	return 0;
undo:
	for (; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}

static void remove_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);
}

static const struct dev_pm_ops cyttsp4_device_access_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cyttsp4_device_access_suspend,
			cyttsp4_device_access_resume)
};

static int cyttsp4_setup_sysfs_attention(struct cyttsp4_device *ttsp)
{
	struct device *dev = &ttsp->dev;
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	int rc = 0;

	dev_vdbg(dev, "%s\n", __func__);

	dad->si = cyttsp4_request_sysinfo(ttsp);
	if (!dad->si)
		return -1;

	rc = cyttsp4_setup_sysfs(ttsp);

	cyttsp4_unsubscribe_attention(ttsp, CY_ATTEN_STARTUP,
		cyttsp4_setup_sysfs_attention, 0);

	return rc;
}

static int cyttsp4_device_access_probe(struct cyttsp4_device *ttsp)
{
	struct device *dev = &ttsp->dev;
	struct cyttsp4_device_access_data *dad;
	struct cyttsp4_device_access_platform_data *pdata =
			dev_get_platdata(dev);
	int rc = 0;

	dev_info(dev, "%s\n", __func__);
	dev_dbg(dev, "%s: debug on\n", __func__);
	dev_vdbg(dev, "%s: verbose debug on\n", __func__);

	dad = kzalloc(sizeof(*dad), GFP_KERNEL);
	if (dad == NULL) {
		dev_err(dev, "%s: Error, kzalloc\n", __func__);
		rc = -ENOMEM;
		goto cyttsp4_device_access_probe_data_failed;
	}

	mutex_init(&dad->sysfs_lock);
	init_waitqueue_head(&dad->wait_q);
	dad->ttsp = ttsp;
	dad->pdata = pdata;
	dad->ic_grpnum = CY_IC_GRPNUM_TCH_REP;
	dad->test.cur_cmd = -1;
	dad->heatmap.numElement = 200;
	dev_set_drvdata(dev, dad);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	/* get sysinfo */
	dad->si = cyttsp4_request_sysinfo(ttsp);
	pm_runtime_put(dev);
	if (dad->si) {
		rc = cyttsp4_setup_sysfs(ttsp);
		if (rc)
			goto cyttsp4_device_access_setup_sysfs_failed;
	} else {
		dev_err(dev, "%s: Fail get sysinfo pointer from core p=%p\n",
				__func__, dad->si);
		cyttsp4_subscribe_attention(ttsp, CY_ATTEN_STARTUP,
			cyttsp4_setup_sysfs_attention, 0);
	}

	/* Stay awake if the current grpnum requires */
	if (cyttsp4_is_awakening_grpnum(dad->ic_grpnum))
		pm_runtime_get(dev);
	
	g_da_data = dad;
	cyttsp4_easy_calibrate = cyttsp4_calibrate_;
	dev_dbg(dev, "%s: ok\n", __func__);
	return 0;

 cyttsp4_device_access_setup_sysfs_failed:
	pm_runtime_suspend(dev);
	pm_runtime_disable(dev);
	dev_set_drvdata(dev, NULL);
	kfree(dad);
 cyttsp4_device_access_probe_data_failed:
	dev_err(dev, "%s failed.\n", __func__);
	return rc;
}

static int cyttsp4_device_access_release(struct cyttsp4_device *ttsp)
{
	struct device *dev = &ttsp->dev;
	struct cyttsp4_device_access_data *dad = dev_get_drvdata(dev);
	u8 ic_buf[CY_NULL_CMD_MODE_INDEX + 1];
	dev_dbg(dev, "%s\n", __func__);
	
	cyttsp4_easy_calibrate = NULL;
	
	/* If the current grpnum required being awake, release it */
	mutex_lock(&dad->sysfs_lock);
	if (cyttsp4_is_awakening_grpnum(dad->ic_grpnum))
		pm_runtime_put(dev);
	mutex_unlock(&dad->sysfs_lock);

	if (dad->own_exclusive) {
		dev_err(dev, "%s: Can't unload in CAT mode. "
				"First switch back to Operational mode\n"
				, __func__);
		ic_buf[CY_NULL_CMD_MODE_INDEX] = CY_HST_OPERATE;
		cyttsp4_test_cmd_mode(dad, ic_buf, CY_NULL_CMD_MODE_INDEX + 1);
	}

	pm_runtime_suspend(dev);
	pm_runtime_disable(dev);

	if (dad->sysfs_nodes_created) {
		remove_sysfs_interfaces(dev);
	} else {
		cyttsp4_unsubscribe_attention(ttsp, CY_ATTEN_STARTUP,
			cyttsp4_setup_sysfs_attention, 0);
	}

	dev_set_drvdata(dev, NULL);
	kfree(dad);
	return 0;
}

struct cyttsp4_driver cyttsp4_device_access_driver = {
	.probe = cyttsp4_device_access_probe,
	.remove = cyttsp4_device_access_release,
	.driver = {
		.name = CYTTSP4_DEVICE_ACCESS_NAME,
		.bus = &cyttsp4_bus_type,
		.owner = THIS_MODULE,
		.pm = &cyttsp4_device_access_pm_ops,
	},
};

static struct cyttsp4_device_access_platform_data _cyttsp4_device_access_platform_data = {
	.device_access_dev_name = CYTTSP4_DEVICE_ACCESS_NAME,
};

static const char cyttsp4_device_access_name[] = CYTTSP4_DEVICE_ACCESS_NAME;
static struct cyttsp4_device_info cyttsp4_device_access_infos[CY_MAX_NUM_CORE_DEVS];

static const char *core_ids[CY_MAX_NUM_CORE_DEVS] = {
	CY_DEFAULT_CORE_ID,
	NULL,
	NULL,
	NULL,
	NULL
};

static int num_core_ids = 1;

module_param_array(core_ids, charp, &num_core_ids, 0);
MODULE_PARM_DESC(core_ids,
	"Core id list of cyttsp4 core devices for device access module");


static int __init cyttsp4_device_access_init(void)
{
	int rc = 0;
	int i, j;

	/* Check for invalid or duplicate core_ids */
	for (i = 0; i < num_core_ids; i++) {
		if (!strlen(core_ids[i])) {
			pr_err("%s: core_id %d is empty\n",
				__func__, i+1);
			return -EINVAL;
		}
		for (j = i+1; j < num_core_ids; j++)
			if (!strcmp(core_ids[i], core_ids[j])) {
				pr_err("%s: core_ids %d and %d are same\n",
					__func__, i+1, j+1);
				return -EINVAL;
			}
	}

	for (i = 0; i < num_core_ids; i++) {
		cyttsp4_device_access_infos[i].name =
			cyttsp4_device_access_name;
		cyttsp4_device_access_infos[i].core_id = core_ids[i];
		cyttsp4_device_access_infos[i].platform_data =
			&_cyttsp4_device_access_platform_data;
		pr_info("%s: Registering device access device for core_id: %s\n",
			__func__, cyttsp4_device_access_infos[i].core_id);
		rc = cyttsp4_register_device(&cyttsp4_device_access_infos[i]);
		if (rc < 0) {
			pr_err("%s: Error, failed registering device\n",
				__func__);
			goto fail_unregister_devices;
		}
	}
	rc = cyttsp4_register_driver(&cyttsp4_device_access_driver);
	if (rc) {
		pr_err("%s: Error, failed registering driver\n", __func__);
		goto fail_unregister_devices;
	}

	pr_info("%s: Cypress TTSP Device Access (Built %s) rc=%d\n",
		 __func__, CY_DRIVER_DATE, rc);
	return 0;

fail_unregister_devices:
	for (i--; i <= 0; i--) {
		cyttsp4_unregister_device(cyttsp4_device_access_infos[i].name,
			cyttsp4_device_access_infos[i].core_id);
		pr_info("%s: Unregistering device access device for core_id: %s\n",
			__func__, cyttsp4_device_access_infos[i].core_id);
	}
	return rc;
}
module_init(cyttsp4_device_access_init);

static void __exit cyttsp4_device_access_exit(void)
{
	int i;

	cyttsp4_unregister_driver(&cyttsp4_device_access_driver);
	for (i = 0; i < num_core_ids; i++) {
		cyttsp4_unregister_device(cyttsp4_device_access_infos[i].name,
			cyttsp4_device_access_infos[i].core_id);
		pr_info("%s: Unregistering device access device for core_id: %s\n",
			__func__, cyttsp4_device_access_infos[i].core_id);
	}
	pr_info("%s: module exit\n", __func__);
}
module_exit(cyttsp4_device_access_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard Product Device Access Driver");
MODULE_AUTHOR("Cypress Semiconductor");
