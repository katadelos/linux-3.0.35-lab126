/*
 * cyttsp4_core.c
 * Cypress TrueTouch(TM) Standard Product V4 Core driver module.
 * For use with Cypress Txx4xx parts.
 * Supported parts include:
 * TMA4XX
 * TMA1036
 *
 * Copyright (c) 2012-2014 Amazon.com, Inc. or its affiliates.
 * Copyright (C) 2012 Cypress Semiconductor
 * Copyright (C) 2011 Sony Ericsson Mobile Communications AB.
 *
 * Author: Aleksej Makarov <aleksej.makarov@sonyericsson.com>
 * Modified by: Cypress Semiconductor to add device functions
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
#include <linux/cyttsp4_io.h>

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/cyttsp4_core.h>
#include "cyttsp4_regs.h"
#include <linux/miscdevice.h>
#include <mach/boardid.h>
#include <llog.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define CY_CORE_MODE_CHANGE_TIMEOUT		1000
#define CY_CORE_RESET_AND_WAIT_TIMEOUT		1000
#define CY_CORE_WAKEUP_TIMEOUT			500

#define CY_CORE_STARTUP_RETRY_COUNT		3

/* Delay after switching touch load switch on or off */
#define CY_LOAD_SWITCH_DELAY_MS 1

#define CY_LOAD_SWTICH_ADDED_DELAY 50
MODULE_FIRMWARE(CY_FW_FILE_NAME);

extern void gpio_init_touch_switch_power(void);
extern void gpio_touch_switch_power_1v8(int on_off);
extern void gpio_touch_switch_power_3v2(int on_off);
extern void gpio_touch_reset_irq_switch(int);

const char *cy_driver_core_name = CYTTSP4_CORE_NAME;
const char *cy_driver_core_version = CY_DRIVER_VERSION;
const char *cy_driver_core_date = CY_DRIVER_DATE;

enum cyttsp4_sleep_state {
	SS_SLEEP_OFF,
	SS_SLEEP_ON,
	SS_SLEEPING,
	SS_WAKING,
};

enum cyttsp4_startup_state {
	STARTUP_NONE,
	STARTUP_QUEUED,
	STARTUP_RUNNING,
	STARTUP_ILLEGAL,
};

struct cyttsp4_core_data {
	struct device *dev;
	struct cyttsp4_core *core;
	struct list_head atten_list[CY_ATTEN_NUM_ATTEN];
	struct mutex system_lock;
	struct mutex adap_lock;
	struct mutex startup_lock;
	enum cyttsp4_mode mode;
	enum cyttsp4_sleep_state sleep_state;
	enum cyttsp4_startup_state startup_state;
	int int_status;
	int cmd_toggle;
	spinlock_t spinlock;
	struct cyttsp4_core_platform_data *pdata;
	wait_queue_head_t wait_q;
	wait_queue_head_t sleep_q;
	int irq;
	struct work_struct startup_work;
	struct workqueue_struct *startup_wq;
	struct workqueue_struct *mode_change_work_q;
	struct work_struct mode_change_work;
	struct cyttsp4_sysinfo sysinfo;
	void *exclusive_dev;
	int exclusive_waits;
	atomic_t ignore_irq;
	bool irq_enabled;

#ifdef VERBOSE_DEBUG
	u8 pr_buf[CY_MAX_PRBUF_SIZE];
#endif
	struct work_struct watchdog_work;
	struct timer_list watchdog_timer;
	bool wd_timer_started;
	bool exlock;
	u8 grip_sup_en;
	struct regulator* vdda; // only if(CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696)
	struct regulator* vddd; // only if(CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696)
};

/* Initial grip suppression settings. Values are in % of screen dimension */
struct cyttsp4_grip_suppression_data init_gsd_s = {
	.xa = 4,
	.xb = 4,
	.xexa = 4,
	.xexb = 4,
	.ya = 0,
	.yb = 0,
	.yexa = 22,
	.yexb = 22,
	.interval = 30, //scan interval in grip suppression region
};

int (*cyttsp4_easy_calibrate)(int);
EXPORT_SYMBOL(cyttsp4_easy_calibrate);
static struct cyttsp4_core_data * g_core_data;

#ifdef DEVELOPMENT_MODE
unsigned long long g_cyttsp4_timeofdata_us;
EXPORT_SYMBOL(g_cyttsp4_timeofdata_us);
#endif

/*Icewine between 4 and 49 inclusive may have load switches. rev50 would not have load switches. */
#define TOUCH_HAS_LOAD_SWITCHES ( (CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696) || \
                            ( (lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_512_EVT4) || \
                              lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_512_EVT4)) && \
                              !lab126_board_rev_greater_eq(BOARD_ID_ICEWINE_PRQ_NO_LSW) && \
                              !lab126_board_rev_greater_eq(BOARD_ID_ICEWINE_WFO_PRQ_NO_LSW)) )

struct atten_node {
	struct list_head node;
	int (*func)(struct cyttsp4_device *);
	struct cyttsp4_device *ttsp;
	int mode;
};

static int cyttsp4_startup_(struct cyttsp4_core_data *cd);
static int cyttsp4_startup_2(struct cyttsp4_core_data *cd);
static int cyttsp4_startup(struct cyttsp4_core_data *cd);
static bool recovery_mode;

module_param_named(recovery_mode, recovery_mode, bool, S_IRUGO);
MODULE_PARM_DESC(recovery_mode, "true to allow recover corrupted firmware");

static inline size_t merge_bytes(u8 high, u8 low)
{
	return (high << 8) + low;
}

#ifdef DEVELOPMENT_MODE
unsigned long long timeofday_microsec(void)
{
	struct timeval tv;

	do_gettimeofday(&tv);
	return (unsigned long long)(tv.tv_sec*1000*1000 + tv.tv_usec);
}
EXPORT_SYMBOL(timeofday_microsec);
#endif

#ifdef VERBOSE_DEBUG
void cyttsp4_pr_buf(struct device *dev, u8 *pr_buf, u8 *dptr, int size,
		const char *data_name)
{
	int i, k;
	const char fmt[] = "%02X ";
	int max;

	if (!size)
		return;

	max = (CY_MAX_PRBUF_SIZE - 1) - sizeof(CY_PR_TRUNCATED);
	pr_buf[0] = 0;
	for (i = k = 0; i < size && k < max; i++, k += 3)
		scnprintf(pr_buf + k, CY_MAX_PRBUF_SIZE, fmt, dptr[i]);

	dev_vdbg(dev, "%s:  %s[0..%d]=%s%s\n", __func__, data_name, size - 1,
			pr_buf, size <= max ? "" : CY_PR_TRUNCATED);
}
EXPORT_SYMBOL(cyttsp4_pr_buf);
#endif

static void cyttsp4_vdda_vddd_enable(struct cyttsp4_core_data* cd, bool en)
{
	if(CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696)
	{
		if(!cd->vdda || !cd->vddd) {
			dev_err(cd->dev, "null regulator %s..", __func__);
			return; 
		}
		if(en) {
			if(!regulator_is_enabled(cd->vdda))
				regulator_enable(cd->vdda);
			if(!regulator_is_enabled(cd->vddd))
				regulator_enable(cd->vddd);
		} else {
			if(regulator_is_enabled(cd->vdda))
				regulator_disable(cd->vddd);
			if(regulator_is_enabled(cd->vdda))
				regulator_disable(cd->vdda);
		}
	}else{
		if(en) {
			gpio_touch_switch_power_3v2(en);
			gpio_touch_switch_power_1v8(en);
		} else {
			gpio_touch_switch_power_1v8(en);
			gpio_touch_switch_power_3v2(en);
		}
	}
}


static int cyttsp4_load_status_regs(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	struct device *dev = cd->dev;
	int rc;

	if (!si->xy_mode) {
		dev_err(cd->dev, "%s: NULL xy_mode pointer\n", __func__);
		return -EINVAL;
	}

	rc = cyttsp4_adap_read(cd->core->adap, CY_REG_BASE,
		si->xy_mode, si->si_ofs.mode_size);
	if (rc < 0)
		dev_err(dev, "%s: fail read mode regs r=%d\n",
			__func__, rc);
	else
		cyttsp4_pr_buf(dev, cd->pr_buf, si->xy_mode,
			si->si_ofs.mode_size, "xy_mode");

	return rc;
}

static int cyttsp4_handshake(struct cyttsp4_core_data *cd, u8 mode)
{
	u8 cmd = mode ^ CY_HST_TOGGLE;
	int rc;

	if (mode & CY_HST_MODE_CHANGE) {
		dev_err(cd->dev, "%s: Host mode change bit set, NO handshake\n",
				__func__);
		return 0;
	}

	rc = cyttsp4_adap_write(cd->core->adap, CY_REG_BASE, &cmd,
			sizeof(cmd));

	if (rc < 0)
		dev_err(cd->dev, "%s: bus write fail on handshake (ret=%d)\n",
				__func__, rc);

	return rc;
}

static int cyttsp4_toggle_low_power(struct cyttsp4_core_data *cd, u8 mode)
{
	u8 cmd = mode ^ CY_HST_LOWPOW;
	int rc = cyttsp4_adap_write(cd->core->adap, CY_REG_BASE, &cmd,
			sizeof(cmd));
	if (rc < 0)
		dev_err(cd->dev,
			"%s: bus write fail on toggle low power (ret=%d)\n",
			__func__, rc);
	return rc;
}

static int cyttsp4_hw_soft_reset(struct cyttsp4_core_data *cd)
{
	u8 cmd = CY_HST_RESET | CY_HST_MODE_CHANGE;
	int rc = cyttsp4_adap_write(cd->core->adap, CY_REG_BASE, &cmd,
			sizeof(cmd));
	if (rc < 0) {
		dev_err(cd->dev, "%s: FAILED to execute SOFT reset\n",
				__func__);
		return rc;
	}
	dev_dbg(cd->dev, "%s: execute SOFT reset\n", __func__);
	return 0;
}

static int cyttsp4_hw_hard_reset(struct cyttsp4_core_data *cd)
{
	if (cd->pdata->xres) {
		cd->pdata->xres(cd->pdata, cd->dev);
		dev_dbg(cd->dev, "%s: execute HARD reset\n", __func__);
		return 0;
	}
	dev_err(cd->dev, "%s: FAILED to execute HARD reset\n", __func__);
	return -ENOSYS;
}

static int cyttsp4_hw_reset(struct cyttsp4_core_data *cd)
{
	int rc = cyttsp4_hw_hard_reset(cd);
	if (rc == -ENOSYS)
		rc = cyttsp4_hw_soft_reset(cd);
	return rc;
}

static inline int cyttsp4_bits_2_bytes(int nbits, int *max)
{
	*max = 1 << nbits;
	return (nbits + 7) / 8;
}

static int cyttsp4_si_data_offsets(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc = cyttsp4_adap_read(cd->core->adap, CY_REG_BASE, &si->si_data,
				   sizeof(si->si_data));
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read sysinfo data offsets r=%d\n",
			__func__, rc);
		return rc;
	}

	/* Print sysinfo data offsets */
	cyttsp4_pr_buf(cd->dev, cd->pr_buf, (u8 *)&si->si_data,
		       sizeof(si->si_data), "sysinfo_data_offsets");

	/* convert sysinfo data offset bytes into integers */

	si->si_ofs.map_sz = merge_bytes(si->si_data.map_szh,
			si->si_data.map_szl);
	si->si_ofs.map_sz = merge_bytes(si->si_data.map_szh,
			si->si_data.map_szl);
	si->si_ofs.cydata_ofs = merge_bytes(si->si_data.cydata_ofsh,
			si->si_data.cydata_ofsl);
	si->si_ofs.test_ofs = merge_bytes(si->si_data.test_ofsh,
			si->si_data.test_ofsl);
	si->si_ofs.pcfg_ofs = merge_bytes(si->si_data.pcfg_ofsh,
			si->si_data.pcfg_ofsl);
	si->si_ofs.opcfg_ofs = merge_bytes(si->si_data.opcfg_ofsh,
			si->si_data.opcfg_ofsl);
	si->si_ofs.ddata_ofs = merge_bytes(si->si_data.ddata_ofsh,
			si->si_data.ddata_ofsl);
	si->si_ofs.mdata_ofs = merge_bytes(si->si_data.mdata_ofsh,
			si->si_data.mdata_ofsl);
	return rc;
}

static int cyttsp4_si_get_cydata(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc;
	int size;
	u8 *buf;
	u8 *p;
	struct cyttsp4_cydata *cydata;

	/* Allocate a temp buffer for reading CYDATA registers */
	si->si_ofs.cydata_size = si->si_ofs.test_ofs - si->si_ofs.cydata_ofs;
	dev_dbg(cd->dev, "%s: cydata size: %d\n", __func__,
			si->si_ofs.cydata_size);
	buf = kzalloc(si->si_ofs.cydata_size, GFP_KERNEL);
	if (buf == NULL) {
		dev_err(cd->dev, "%s: fail alloc buffer for reading cydata\n",
			__func__);
		return -ENOMEM;
	}

	/* Read the CYDA registers to the temp buf */
	rc = cyttsp4_adap_read(cd->core->adap, si->si_ofs.cydata_ofs,
				buf, si->si_ofs.cydata_size);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read cydata r=%d\n",
				__func__, rc);
		goto free_buf;
	}

	/* Allocate local cydata structure */
	if (si->si_ptrs.cydata == NULL)
		si->si_ptrs.cydata = kzalloc(sizeof(struct cyttsp4_cydata),
					GFP_KERNEL);
	if (si->si_ptrs.cydata == NULL) {
		dev_err(cd->dev, "%s: fail alloc cydata memory\n", __func__);
		rc = -ENOMEM;
		goto free_buf;
	}

	cydata = (struct cyttsp4_cydata *)buf;

	/* Allocate MFGID memory */
	if (si->si_ptrs.cydata->mfg_id == NULL)
		si->si_ptrs.cydata->mfg_id = kzalloc(cydata->mfgid_sz,
			GFP_KERNEL);
	if (si->si_ptrs.cydata->mfg_id == NULL) {
		kfree(si->si_ptrs.cydata);
		si->si_ptrs.cydata = NULL;
		dev_err(cd->dev, "%s: fail alloc mfgid memory\n", __func__);
		rc = -ENOMEM;
		goto free_buf;
	}

	/* Copy all fields up to MFGID to local cydata structure */
	p = buf;
	size = offsetof(struct cyttsp4_cydata, mfgid_sz) + 1;
	memcpy(si->si_ptrs.cydata, p, size);

	cyttsp4_pr_buf(cd->dev, cd->pr_buf, (u8 *)si->si_ptrs.cydata,
			size, "sysinfo_cydata");

	/* Copy MFGID */
	p += size;
	memcpy(si->si_ptrs.cydata->mfg_id, p, si->si_ptrs.cydata->mfgid_sz);

	cyttsp4_pr_buf(cd->dev, cd->pr_buf, (u8 *)si->si_ptrs.cydata->mfg_id,
			si->si_ptrs.cydata->mfgid_sz, "sysinfo_cydata mfgid");

	/* Copy remaining registers after MFGID */
	p += si->si_ptrs.cydata->mfgid_sz;
	size = sizeof(struct cyttsp4_cydata) -
			offsetof(struct cyttsp4_cydata, cyito_idh) - 1;
	memcpy((u8 *)&si->si_ptrs.cydata->cyito_idh, p, size);

	cyttsp4_pr_buf(cd->dev, cd->pr_buf, &si->si_ptrs.cydata->cyito_idh,
			size, "sysinfo_cydata");

free_buf:
	kfree(buf);
	return rc;
}

static int cyttsp4_si_get_test_data(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc;

	si->si_ofs.test_size = si->si_ofs.pcfg_ofs - si->si_ofs.test_ofs;
	if (si->si_ptrs.test == NULL)
		si->si_ptrs.test = kzalloc(si->si_ofs.test_size, GFP_KERNEL);
	if (si->si_ptrs.test == NULL) {
		dev_err(cd->dev, "%s: fail alloc test memory\n", __func__);
		return -ENOMEM;
	}

	rc = cyttsp4_adap_read(cd->core->adap, si->si_ofs.test_ofs,
		si->si_ptrs.test, si->si_ofs.test_size);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read test data r=%d\n",
			__func__, rc);
		return rc;
	}

	cyttsp4_pr_buf(cd->dev, cd->pr_buf,
		       (u8 *)si->si_ptrs.test, si->si_ofs.test_size,
		       "sysinfo_test_data");
	/* this is hardware watchdog for PSOC */
	if (si->si_ptrs.test->post_codel &
	    CY_POST_CODEL_WDG_RST)
		dev_info(cd->dev, "%s: %s codel=%02X\n",
			 __func__, "Reset was a WATCHDOG RESET",
			 si->si_ptrs.test->post_codel);

	if (!(si->si_ptrs.test->post_codel &
	      CY_POST_CODEL_CFG_DATA_CRC_FAIL))
		dev_info(cd->dev, "%s: %s codel=%02X\n", __func__,
			 "Config Data CRC FAIL",
			 si->si_ptrs.test->post_codel);

	if (!(si->si_ptrs.test->post_codel &
	      CY_POST_CODEL_PANEL_TEST_FAIL))
		dev_info(cd->dev, "%s: %s codel=%02X\n",
			 __func__, "PANEL TEST FAIL",
			 si->si_ptrs.test->post_codel);

	dev_info(cd->dev, "%s: SCANNING is %s codel=%02X\n",
		 __func__, si->si_ptrs.test->post_codel & 0x08 ?
		 "ENABLED" : "DISABLED",
		 si->si_ptrs.test->post_codel);
	return rc;
}

static int cyttsp4_si_get_pcfg_data(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc;

	dev_vdbg(cd->dev, "%s: get pcfg data\n", __func__);
	si->si_ofs.pcfg_size = si->si_ofs.opcfg_ofs - si->si_ofs.pcfg_ofs;
	if (si->si_ptrs.pcfg == NULL)
		si->si_ptrs.pcfg = kzalloc(si->si_ofs.pcfg_size, GFP_KERNEL);
	if (si->si_ptrs.pcfg == NULL) {
		rc = -ENOMEM;
		dev_err(cd->dev, "%s: fail alloc pcfg memory r=%d\n",
			__func__, rc);
		return rc;
	}
	rc = cyttsp4_adap_read(cd->core->adap, si->si_ofs.pcfg_ofs,
			       si->si_ptrs.pcfg, si->si_ofs.pcfg_size);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read pcfg data r=%d\n",
			__func__, rc);
		return rc;
	}

	si->si_ofs.max_x = merge_bytes((si->si_ptrs.pcfg->res_xh
			& CY_PCFG_RESOLUTION_X_MASK), si->si_ptrs.pcfg->res_xl);
	si->si_ofs.x_origin = !!(si->si_ptrs.pcfg->res_xh
			& CY_PCFG_ORIGIN_X_MASK);
	si->si_ofs.max_y = merge_bytes((si->si_ptrs.pcfg->res_yh
			& CY_PCFG_RESOLUTION_Y_MASK), si->si_ptrs.pcfg->res_yl);
	si->si_ofs.y_origin = !!(si->si_ptrs.pcfg->res_yh
			& CY_PCFG_ORIGIN_Y_MASK);
	si->si_ofs.max_p = merge_bytes(si->si_ptrs.pcfg->max_zh,
			si->si_ptrs.pcfg->max_zl);

	cyttsp4_pr_buf(cd->dev, cd->pr_buf,
		       (u8 *)si->si_ptrs.pcfg,
		       si->si_ofs.pcfg_size, "sysinfo_pcfg_data");
	return rc;
}

static int cyttsp4_si_get_opcfg_data(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int i;
	enum cyttsp4_tch_abs abs;
	int rc;

	dev_vdbg(cd->dev, "%s: get opcfg data\n", __func__);
	si->si_ofs.opcfg_size = si->si_ofs.ddata_ofs - si->si_ofs.opcfg_ofs;
	if (si->si_ptrs.opcfg == NULL)
		si->si_ptrs.opcfg = kzalloc(si->si_ofs.opcfg_size, GFP_KERNEL);
	if (si->si_ptrs.opcfg == NULL) {
		dev_err(cd->dev, "%s: fail alloc opcfg memory\n", __func__);
		rc = -ENOMEM;
		goto cyttsp4_si_get_opcfg_data_exit;
	}

	rc = cyttsp4_adap_read(cd->core->adap, si->si_ofs.opcfg_ofs,
		si->si_ptrs.opcfg, si->si_ofs.opcfg_size);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read opcfg data r=%d\n",
			__func__, rc);
		goto cyttsp4_si_get_opcfg_data_exit;
	}
	si->si_ofs.cmd_ofs = si->si_ptrs.opcfg->cmd_ofs;
	si->si_ofs.rep_ofs = si->si_ptrs.opcfg->rep_ofs;
	si->si_ofs.rep_sz = (si->si_ptrs.opcfg->rep_szh * 256) +
		si->si_ptrs.opcfg->rep_szl;
	si->si_ofs.num_btns = si->si_ptrs.opcfg->num_btns;
	si->si_ofs.num_btn_regs = (si->si_ofs.num_btns +
		CY_NUM_BTN_PER_REG - 1) / CY_NUM_BTN_PER_REG;
	si->si_ofs.tt_stat_ofs = si->si_ptrs.opcfg->tt_stat_ofs;
	si->si_ofs.obj_cfg0 = si->si_ptrs.opcfg->obj_cfg0;
	si->si_ofs.max_tchs = si->si_ptrs.opcfg->max_tchs &
		CY_BYTE_OFS_MASK;
	si->si_ofs.tch_rec_size = si->si_ptrs.opcfg->tch_rec_size &
		CY_BYTE_OFS_MASK;

	/* Get the old touch fields */
	for (abs = CY_TCH_X; abs < CY_NUM_TCH_FIELDS; abs++) {
		si->si_ofs.tch_abs[abs].ofs =
			si->si_ptrs.opcfg->tch_rec_old[abs].loc &
			CY_BYTE_OFS_MASK;
		si->si_ofs.tch_abs[abs].size =
			cyttsp4_bits_2_bytes
			(si->si_ptrs.opcfg->tch_rec_old[abs].size,
			&si->si_ofs.tch_abs[abs].max);
		si->si_ofs.tch_abs[abs].bofs =
			(si->si_ptrs.opcfg->tch_rec_old[abs].loc &
			CY_BOFS_MASK) >> CY_BOFS_SHIFT;
	}

	/* button fields */
	si->si_ofs.btn_rec_size = si->si_ptrs.opcfg->btn_rec_size;
	si->si_ofs.btn_diff_ofs = si->si_ptrs.opcfg->btn_diff_ofs;
	si->si_ofs.btn_diff_size = si->si_ptrs.opcfg->btn_diff_size;

	if (si->si_ofs.tch_rec_size > CY_TMA1036_TCH_REC_SIZE) {
		/* Get the extended touch fields */
		for (i = 0; i < CY_NUM_EXT_TCH_FIELDS; abs++, i++) {
			si->si_ofs.tch_abs[abs].ofs =
				si->si_ptrs.opcfg->tch_rec_new[i].loc &
				CY_BYTE_OFS_MASK;
			si->si_ofs.tch_abs[abs].size =
				cyttsp4_bits_2_bytes
				(si->si_ptrs.opcfg->tch_rec_new[i].size,
				&si->si_ofs.tch_abs[abs].max);
			si->si_ofs.tch_abs[abs].bofs =
				(si->si_ptrs.opcfg->tch_rec_new[i].loc
				& CY_BOFS_MASK) >> CY_BOFS_SHIFT;
		}
	}

	for (abs = 0; abs < CY_TCH_NUM_ABS; abs++) {
		dev_dbg(cd->dev, "%s: tch_rec_%s\n", __func__,
			cyttsp4_tch_abs_string[abs]);
		dev_dbg(cd->dev, "%s:     ofs =%2d\n", __func__,
			si->si_ofs.tch_abs[abs].ofs);
		dev_dbg(cd->dev, "%s:     siz =%2d\n", __func__,
			si->si_ofs.tch_abs[abs].size);
		dev_dbg(cd->dev, "%s:     max =%2d\n", __func__,
			si->si_ofs.tch_abs[abs].max);
		dev_dbg(cd->dev, "%s:     bofs=%2d\n", __func__,
			si->si_ofs.tch_abs[abs].bofs);
	}

	si->si_ofs.mode_size = si->si_ofs.tt_stat_ofs + 1;
	si->si_ofs.data_size = si->si_ofs.max_tchs *
		si->si_ptrs.opcfg->tch_rec_size;

	cyttsp4_pr_buf(cd->dev, cd->pr_buf, (u8 *)si->si_ptrs.opcfg,
		si->si_ofs.opcfg_size, "sysinfo_opcfg_data");

cyttsp4_si_get_opcfg_data_exit:
	return rc;
}

static int cyttsp4_si_get_ddata(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc;

	dev_vdbg(cd->dev, "%s: get ddata data\n", __func__);
	si->si_ofs.ddata_size = si->si_ofs.mdata_ofs - si->si_ofs.ddata_ofs;
	if (si->si_ptrs.ddata == NULL)
		si->si_ptrs.ddata = kzalloc(si->si_ofs.ddata_size, GFP_KERNEL);
	if (si->si_ptrs.ddata == NULL) {
		dev_err(cd->dev, "%s: fail alloc ddata memory\n", __func__);
		return -ENOMEM;
	}

	rc = cyttsp4_adap_read(cd->core->adap, si->si_ofs.ddata_ofs,
			       si->si_ptrs.ddata, si->si_ofs.ddata_size);
	if (rc < 0)
		dev_err(cd->dev, "%s: fail read ddata data r=%d\n",
			__func__, rc);
	else {
		cyttsp4_pr_buf(cd->dev, cd->pr_buf,
			       (u8 *)si->si_ptrs.ddata,
			       si->si_ofs.ddata_size, "sysinfo_ddata");

		printk(KERN_INFO "cyttsp4_core:fw:ver=%02X%02X:\n",
			si->si_ptrs.ddata->lab126_fw_ver0,
			si->si_ptrs.ddata->lab126_fw_ver1);
	}

	return rc;
}

static int cyttsp4_si_get_mdata(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc;

	dev_vdbg(cd->dev, "%s: get mdata data\n", __func__);
	si->si_ofs.mdata_size = si->si_ofs.map_sz - si->si_ofs.mdata_ofs;
	if (si->si_ptrs.mdata == NULL)
		si->si_ptrs.mdata = kzalloc(si->si_ofs.mdata_size, GFP_KERNEL);
	if (si->si_ptrs.mdata == NULL) {
		dev_err(cd->dev, "%s: fail alloc mdata memory\n", __func__);
		return -ENOMEM;
	}

	rc = cyttsp4_adap_read(cd->core->adap, si->si_ofs.mdata_ofs,
			       si->si_ptrs.mdata, si->si_ofs.mdata_size);
	if (rc < 0)
		dev_err(cd->dev, "%s: fail read mdata data r=%d\n",
			__func__, rc);
	else
		cyttsp4_pr_buf(cd->dev, cd->pr_buf,
			       (u8 *)si->si_ptrs.mdata,
			       si->si_ofs.mdata_size, "sysinfo_mdata");
	return rc;
}

static int cyttsp4_si_get_btn_data(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int btn;
	int num_defined_keys;
	u16 *key_table;
	int rc = 0;

	dev_vdbg(cd->dev, "%s: get btn data\n", __func__);
	if (si->si_ofs.num_btns) {
		si->si_ofs.btn_keys_size = si->si_ofs.num_btns *
			sizeof(struct cyttsp4_btn);
		if (si->btn == NULL)
			si->btn = kzalloc(si->si_ofs.btn_keys_size, GFP_KERNEL);
		if (si->btn == NULL) {
			dev_err(cd->dev, "%s: %s\n", __func__,
				"fail alloc btn_keys memory");
			return -ENOMEM;
		}
		if (cd->pdata->sett[CY_IC_GRPNUM_BTN_KEYS] == NULL)
			num_defined_keys = 0;
		else if (cd->pdata->sett[CY_IC_GRPNUM_BTN_KEYS]->data == NULL)
			num_defined_keys = 0;
		else
			num_defined_keys = cd->pdata->sett
				[CY_IC_GRPNUM_BTN_KEYS]->size;

		for (btn = 0; btn < si->si_ofs.num_btns &&
			btn < num_defined_keys; btn++) {
			key_table = (u16 *)cd->pdata->sett
				[CY_IC_GRPNUM_BTN_KEYS]->data;
			si->btn[btn].key_code = key_table[btn];
			si->btn[btn].enabled = true;
		}
		for (; btn < si->si_ofs.num_btns; btn++) {
			si->btn[btn].key_code = KEY_RESERVED;
			si->btn[btn].enabled = true;
		}

		return rc;
	}

	si->si_ofs.btn_keys_size = 0;
	kfree(si->btn);
	si->btn = NULL;
	return rc;
}

static int cyttsp4_si_get_op_data_ptrs(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	if (si->xy_mode == NULL) {
		si->xy_mode = kzalloc(si->si_ofs.mode_size, GFP_KERNEL);
		if (si->xy_mode == NULL)
			return -ENOMEM;
	}

	if (si->xy_data == NULL) {
		si->xy_data = kzalloc(si->si_ofs.data_size, GFP_KERNEL);
		if (si->xy_data == NULL)
			return -ENOMEM;
	}

	if (si->btn_rec_data == NULL) {
		si->btn_rec_data = kzalloc(si->si_ofs.btn_rec_size *
					   si->si_ofs.num_btns, GFP_KERNEL);
		if (si->btn_rec_data == NULL)
			return -ENOMEM;
	}
#ifdef SENSOR_DATA_MODE
	/* initialize */
	si->monitor.mntr_status = CY_MNTR_DISABLED;
	memset(si->monitor.sensor_data, 0, sizeof(si->monitor.sensor_data));
#endif
	return 0;
}

static void cyttsp4_si_put_log_data(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	dev_dbg(cd->dev, "%s: cydata_ofs =%4d siz=%4d\n", __func__,
		si->si_ofs.cydata_ofs, si->si_ofs.cydata_size);
	dev_dbg(cd->dev, "%s: test_ofs   =%4d siz=%4d\n", __func__,
		si->si_ofs.test_ofs, si->si_ofs.test_size);
	dev_dbg(cd->dev, "%s: pcfg_ofs   =%4d siz=%4d\n", __func__,
		si->si_ofs.pcfg_ofs, si->si_ofs.pcfg_size);
	dev_dbg(cd->dev, "%s: opcfg_ofs  =%4d siz=%4d\n", __func__,
		si->si_ofs.opcfg_ofs, si->si_ofs.opcfg_size);
	dev_dbg(cd->dev, "%s: ddata_ofs  =%4d siz=%4d\n", __func__,
		si->si_ofs.ddata_ofs, si->si_ofs.ddata_size);
	dev_dbg(cd->dev, "%s: mdata_ofs  =%4d siz=%4d\n", __func__,
		si->si_ofs.mdata_ofs, si->si_ofs.mdata_size);

	dev_dbg(cd->dev, "%s: cmd_ofs       =%4d\n", __func__,
		si->si_ofs.cmd_ofs);
	dev_dbg(cd->dev, "%s: rep_ofs       =%4d\n", __func__,
		si->si_ofs.rep_ofs);
	dev_dbg(cd->dev, "%s: rep_sz        =%4d\n", __func__,
		si->si_ofs.rep_sz);
	dev_dbg(cd->dev, "%s: num_btns      =%4d\n", __func__,
		si->si_ofs.num_btns);
	dev_dbg(cd->dev, "%s: num_btn_regs  =%4d\n", __func__,
		si->si_ofs.num_btn_regs);
	dev_dbg(cd->dev, "%s: tt_stat_ofs   =%4d\n", __func__,
		si->si_ofs.tt_stat_ofs);
	dev_dbg(cd->dev, "%s: tch_rec_size   =%4d\n", __func__,
		si->si_ofs.tch_rec_size);
	dev_dbg(cd->dev, "%s: max_tchs      =%4d\n", __func__,
		si->si_ofs.max_tchs);
	dev_dbg(cd->dev, "%s: mode_size     =%4d\n", __func__,
		si->si_ofs.mode_size);
	dev_dbg(cd->dev, "%s: data_size     =%4d\n", __func__,
		si->si_ofs.data_size);
	dev_dbg(cd->dev, "%s: map_sz        =%4d\n", __func__,
		si->si_ofs.map_sz);

	dev_dbg(cd->dev, "%s: btn_rec_size   =%2d\n", __func__,
		si->si_ofs.btn_rec_size);
	dev_dbg(cd->dev, "%s: btn_diff_ofs  =%2d\n", __func__,
		si->si_ofs.btn_diff_ofs);
	dev_dbg(cd->dev, "%s: btn_diff_size  =%2d\n", __func__,
		si->si_ofs.btn_diff_size);

	dev_dbg(cd->dev, "%s: max_x    = 0x%04X (%d)\n", __func__,
		si->si_ofs.max_x, si->si_ofs.max_x);
	dev_dbg(cd->dev, "%s: x_origin = %d (%s)\n", __func__,
		si->si_ofs.x_origin,
		si->si_ofs.x_origin == CY_NORMAL_ORIGIN ?
		"left corner" : "right corner");
	dev_dbg(cd->dev, "%s: max_y    = 0x%04X (%d)\n", __func__,
		si->si_ofs.max_y, si->si_ofs.max_y);
	dev_dbg(cd->dev, "%s: y_origin = %d (%s)\n", __func__,
		si->si_ofs.y_origin,
		si->si_ofs.y_origin == CY_NORMAL_ORIGIN ?
		"upper corner" : "lower corner");
	dev_dbg(cd->dev, "%s: max_p    = 0x%04X (%d)\n", __func__,
		si->si_ofs.max_p, si->si_ofs.max_p);

	dev_dbg(cd->dev, "%s: xy_mode=%p xy_data=%p\n", __func__,
		si->xy_mode, si->xy_data);
}

static int cyttsp4_get_sysinfo_regs(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc;

	rc = cyttsp4_si_data_offsets(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_cydata(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_test_data(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_pcfg_data(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_opcfg_data(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_ddata(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_mdata(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_btn_data(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_op_data_ptrs(cd);
	if (rc < 0) {
		dev_err(cd->dev, "%s: failed to get_op_data\n",
			__func__);
		return rc;
	}
	cyttsp4_si_put_log_data(cd);

	/* provide flow control handshake */
	rc = cyttsp4_handshake(cd, si->si_data.hst_mode);
	if (rc < 0)
		dev_err(cd->dev, "%s: handshake fail on sysinfo reg\n",
			__func__);

	si->ready = true;
	return rc;
}

static void cyttsp4_toggle_loadswitch(struct cyttsp4_core_data *cd)
{
	atomic_set(&cd->ignore_irq, 1);
	disable_irq(cd->irq);

	/* reset hardware and wait for heartbeat */
	gpio_touch_reset_irq_switch(0);
	cyttsp4_vdda_vddd_enable(cd, 0);
	msleep(CY_LOAD_SWITCH_DELAY_MS);
	cyttsp4_vdda_vddd_enable(cd, 1);
	gpio_touch_reset_irq_switch(1);
	msleep(CY_LOAD_SWITCH_DELAY_MS);

	/* add delay between RST & interrupt enable to solve the i2c timeout */
	/* WS-725 */
	if(lab126_board_is(BOARD_ID_WHISKY_WFO) ||
			lab126_board_is(BOARD_ID_WHISKY_WAN))
		msleep(CY_LOAD_SWTICH_ADDED_DELAY);

	enable_irq(cd->irq);
	atomic_set(&cd->ignore_irq, 0);
}

static void cyttsp4_queue_startup(struct cyttsp4_core_data *cd)
{
	mutex_lock(&cd->system_lock);
	dev_vdbg(cd->dev, "%s: enter\n", __func__);

	if (cd->startup_state == STARTUP_NONE) {
		if (!work_pending(&cd->startup_work))
		{
			cd->startup_state = STARTUP_QUEUED;
			queue_work(cd->startup_wq, &cd->startup_work);
		}
	} else {
		dev_dbg(cd->dev, "%s: startup_state = %d\n", __func__,
			cd->startup_state);
	}
	dev_vdbg(cd->dev, "%s: exit\n", __func__);
	mutex_unlock(&cd->system_lock);
}

static void cyttsp4_startup_work_function(struct work_struct *work)
{
	struct cyttsp4_core_data *cd =  container_of(work,
			struct cyttsp4_core_data, startup_work);
	int rc;

	dev_vdbg(cd->dev, "%s: enter\n", __func__);
	/*
	 * Force clear exclusive access
	 * startup queue is called for abnormal case,
	 * and when a this called access can be acquired in other context
	 */
	mutex_lock(&cd->system_lock);
	if (cd->exclusive_dev != cd->core)
		cd->exclusive_dev = NULL;

	cd->startup_state = STARTUP_RUNNING;

	if (cd->exclusive_dev != cd->core)
		cd->exclusive_dev = NULL;
	mutex_unlock(&cd->system_lock);

	if(TOUCH_HAS_LOAD_SWITCHES) {
		rc = cyttsp4_startup_2(cd);
		/*
		 * in case of failure, give a second chance for a proper start up sequence. 
		 * */
		if(rc) {
			rc = cyttsp4_startup_(cd);
		}
	}else
		rc = cyttsp4_startup_(cd);

	mutex_lock(&cd->system_lock);
	cd->startup_state = STARTUP_NONE;
	mutex_unlock(&cd->system_lock);
	if (rc < 0)
		dev_err(cd->dev, "%s: Fail queued startup r=%d\n",
				__func__, rc);
	dev_vdbg(cd->dev, "%s: exit\n", __func__);
}


static void call_atten_cb(struct cyttsp4_core_data *cd, int mode)
{
	struct atten_node *atten, *atten_n;

	dev_vdbg(cd->dev, "%s: check list mode=%d\n", __func__, mode);
	spin_lock(&cd->spinlock);
	list_for_each_entry_safe(atten, atten_n,
			&cd->atten_list[CY_ATTEN_IRQ], node) {
		if (atten->mode & mode) {
			spin_unlock(&cd->spinlock);
			dev_vdbg(cd->dev, "%s: attention for mode=%d",
					__func__, atten->mode);
			atten->func(atten->ttsp);
			spin_lock(&cd->spinlock);
		}
	}
	spin_unlock(&cd->spinlock);
}

static irqreturn_t cyttsp4_irq(int irq, void *handle)
{
	struct cyttsp4_core_data *cd = handle;
	struct device *dev = cd->dev;
	enum cyttsp4_mode cur_mode;
	u8 cmd_ofs = cd->sysinfo.si_ofs.cmd_ofs;
	u8 mode[3];
	int rc;
	u8 cat_masked_cmd;

#ifdef DEVELOPMENT_MODE
	//wario_debug_toggle(1);

	if(g_cyttsp4_timeofdata_us) {
		g_cyttsp4_timeofdata_us=timeofday_microsec();
	//	printk(KERN_INFO "cyttsp4:irq:time=%lld:", g_cyttsp4_timeofdata_us);
	}
#endif
	/*
	 * Check whether this IRQ should be ignored (external)
	 * This should be the very first thing to check since
	 * ignore_irq may be set for a very short period of time
	 */
	if (atomic_read(&cd->ignore_irq)) {
		dev_vdbg(dev, "%s: Ignoring IRQ\n", __func__);
		return IRQ_HANDLED;
	}

	dev_vdbg(dev, "%s int:0x%x\n", __func__, cd->int_status);

	mutex_lock(&cd->system_lock);

	/* why interrupt sleeping? */
	if (cd->sleep_state == SS_SLEEP_ON) {
		dev_err(dev, "%s: Received IRQ while in sleep\n",
			__func__);
	} else if (cd->sleep_state == SS_SLEEPING) {
		dev_err(dev, "%s: Received IRQ while sleeping\n",
			__func__);
	}

	rc = cyttsp4_adap_read(cd->core->adap, CY_REG_BASE, mode, sizeof(mode));
	if (rc) {
		dev_err(cd->dev, "%s: Fail read adapter r=%d\n", __func__, rc);
		goto cyttsp4_irq_exit;
	}
	dev_vdbg(dev, "%s mode[0-2]:0x%X 0x%X 0x%X\n", __func__,
			mode[0], mode[1], mode[2]);

	if (IS_BOOTLOADER(mode[0], mode[1])) {
		cur_mode = CY_MODE_BOOTLOADER;
		dev_vdbg(dev, "%s: bl running\n", __func__);
		call_atten_cb(cd, cur_mode);
		if (cd->mode == CY_MODE_BOOTLOADER) {
			/* Signal bootloader heartbeat heard */
			wake_up(&cd->wait_q);
			goto cyttsp4_irq_exit;
		}
		/* catch operation->bl glitch */
		if (cd->mode != CY_MODE_UNKNOWN ) {
			/* switch to bootloader */
			dev_dbg(dev, "%s: restart switch to bl m=%d -> m=%d\n",
				__func__, cd->mode, cur_mode);

			/* Incase startup_state do not let startup_() */
			cd->mode = CY_MODE_UNKNOWN;
			mutex_unlock(&cd->system_lock);
			cyttsp4_queue_startup(cd);
			mutex_lock(&cd->system_lock);
			goto cyttsp4_irq_exit;
		}

		/*
		 * do not wake thread on this switch since
		 * it is possible to get an early heartbeat
		 * prior to performing the reset
		 */
		cd->mode = cur_mode;

		goto cyttsp4_irq_exit;
	}

	switch (mode[0] & CY_HST_MODE) {
	case CY_HST_OPERATE:
		cur_mode = CY_MODE_OPERATIONAL;
		dev_vdbg(dev, "%s: operational\n", __func__);
		break;
	case CY_HST_CAT:
		cur_mode = CY_MODE_CAT;
		/* set the start sensor mode state. */
		cat_masked_cmd = mode[2] & CY_CMD_MASK;

#ifdef SHOK_SENSOR_DATA_MODE
		if (cat_masked_cmd == CY_CMD_CAT_START_SENSOR_DATA_MODE)
			cd->sysinfo.monitor.mntr_status = CY_MNTR_INITIATED;
		else
			cd->sysinfo.monitor.mntr_status = CY_MNTR_DISABLED;
#endif
		/* Get the Debug info for the interrupt. */
		if (cat_masked_cmd != CY_CMD_CAT_NULL &&
				cat_masked_cmd !=
					CY_CMD_CAT_RETRIEVE_PANEL_SCAN &&
				cat_masked_cmd != CY_CMD_CAT_EXEC_PANEL_SCAN)
			dev_info(cd->dev,
				"%s: cyttsp4_CaT_IRQ=%02X %02X %02X\n",
				__func__, mode[0], mode[1], mode[2]);
		dev_vdbg(dev, "%s: CaT\n", __func__);
		break;
	case CY_HST_SYSINFO:
		cur_mode = CY_MODE_SYSINFO;
		dev_vdbg(dev, "%s: sysinfo\n", __func__);
		break;
	default:
		cur_mode = CY_MODE_UNKNOWN;
		dev_err(dev, "%s: unknown HST mode 0x%02X\n", __func__,
			mode[0]);
		break;
	}

	/* Check whether this IRQ should be ignored (internal) */
	if (cd->int_status & CY_INT_IGNORE) {
		dev_dbg(dev, "%s: Ignoring IRQ\n", __func__);
		goto cyttsp4_irq_exit;
	}

	/* Check for wake up interrupt */
	if (cd->int_status & CY_INT_AWAKE) {
		cd->int_status &= ~CY_INT_AWAKE;
		wake_up(&cd->sleep_q);
		dev_vdbg(dev, "%s: Received wake up interrupt\n", __func__);
		goto cyttsp4_irq_handshake;
	}

	/* Expecting mode change interrupt */
	if ((cd->int_status & CY_INT_MODE_CHANGE)
			&& (mode[0] & CY_HST_MODE_CHANGE) == 0) {
		cd->int_status &= ~CY_INT_MODE_CHANGE;
		dev_dbg(dev, "%s: finish mode switch m=%d -> m=%d\n",
				__func__, cd->mode, cur_mode);
		cd->mode = cur_mode;
		wake_up(&cd->wait_q);
		goto cyttsp4_irq_handshake;
	}
	
	if(cur_mode != CY_MODE_UNKNOWN && cd->mode == CY_MODE_UNKNOWN) {
		cd->int_status &= ~CY_INT_MODE_CHANGE;
		dev_dbg(dev, "%s: finish mode switch m=%d -> m=%d\n",
				__func__, cd->mode, cur_mode);
		cd->mode = cur_mode;
		wake_up(&cd->wait_q);
		goto cyttsp4_irq_handshake;
	}

	/* compare current core mode to current device mode */
	dev_vdbg(dev, "%s: cd->mode=%d cur_mode=%d\n",
			__func__, cd->mode, cur_mode);
	if ((mode[0] & CY_HST_MODE_CHANGE) == 0 && cd->mode != cur_mode) {
		/* Unexpected mode change occurred */
		dev_err(dev, "%s %d->%d 0x%x\n", __func__, cd->mode,
				cur_mode, cd->int_status);
		dev_err(dev, "%s: Unexpected mode change, startup\n",
				__func__);
		mutex_unlock(&cd->system_lock);
		cyttsp4_queue_startup(cd);
		mutex_lock(&cd->system_lock);
		goto cyttsp4_irq_exit;
	}

	/* Expecting command complete interrupt */
	dev_vdbg(dev, "%s: command byte:0x%x, toggle:0x%x\n",
			__func__, mode[cmd_ofs], cd->cmd_toggle);
	if ((cd->int_status & CY_INT_EXEC_CMD)
			&& mode[cmd_ofs] & CY_CMD_COMPLETE) {
		cd->int_status &= ~CY_INT_EXEC_CMD;
		dev_vdbg(dev, "%s: Received command complete interrupt\n",
				__func__);
		wake_up(&cd->wait_q);
		goto cyttsp4_irq_handshake;
	}

	/* This should be status report, read status regs */
	if (cd->mode == CY_MODE_OPERATIONAL) {
		dev_vdbg(dev, "%s: Read status registers\n", __func__);
		rc = cyttsp4_load_status_regs(cd);
		if (rc < 0)
			dev_err(dev, "%s: fail read mode regs r=%d\n",
				__func__, rc);
	}

	/* attention IRQ */
	call_atten_cb(cd, cd->mode);

cyttsp4_irq_handshake:
	/* handshake the event */
	dev_vdbg(dev, "%s: Handshake mode=0x%02X r=%d\n",
			__func__, mode[0], rc);
	rc = cyttsp4_handshake(cd, mode[0]);
	if (rc < 0)
		dev_err(dev, "%s: Fail handshake mode=0x%02X r=%d\n",
				__func__, mode[0], rc);

	/*
	 * a non-zero udelay period is required for using
	 * IRQF_TRIGGER_LOW in order to delay until the
	 * device completes isr deassert
	 */
	udelay(cd->pdata->level_irq_udelay);

cyttsp4_irq_exit:
	mutex_unlock(&cd->system_lock);
	dev_vdbg(dev, "%s: irq done\n", __func__);
	return IRQ_HANDLED;
}

static void cyttsp4_start_wd_timer(struct cyttsp4_core_data *cd)
{
	if (!CY_WATCHDOG_TIMEOUT)
		return;

	mod_timer(&cd->watchdog_timer, jiffies + CY_WATCHDOG_TIMEOUT);
	return;
}

static void cyttsp4_stop_wd_timer(struct cyttsp4_core_data *cd)
{
	if (!CY_WATCHDOG_TIMEOUT)
		return;

	del_timer(&cd->watchdog_timer);
	cancel_work_sync(&cd->watchdog_work);
	del_timer(&cd->watchdog_timer);
	return;
}

static void cyttsp4_watchdog_work(struct work_struct *work)
{
	struct cyttsp4_core_data *cd =
		container_of(work, struct cyttsp4_core_data, watchdog_work);
	u8 mode[2];
	int retval;
	int trigger_startup = false;

	if (cd == NULL) {
		dev_err(cd->dev, "%s: NULL context pointer\n", __func__);
		return;
	}

	mutex_lock(&cd->system_lock);

	retval = cyttsp4_load_status_regs(cd);
	if (retval < 0) {
		dev_err(cd->dev,
			"%s: failed to access device in watchdog timer r=%d\n",
			__func__, retval);
		LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
				"kernel", CYTTSP4_CORE_NAME, "ESD-watchdog-no-i2c", 1, "");
		if (!work_pending(&cd->startup_work))
		{
			cd->startup_state = STARTUP_NONE;
			trigger_startup = true;
		}
		goto cyttsp4_timer_watchdog_exit;
	}
	mode[0] = cd->sysinfo.xy_mode[CY_REG_BASE];
	mode[1] = cd->sysinfo.xy_mode[CY_REG_BASE+1];
	if (IS_BOOTLOADER(mode[0], mode[1])) {
		dev_err(cd->dev,
			"%s: device found in bootloader mode when operational mode 0x%02X, 0x%02X\n",
			__func__, mode[0], mode[1]);
		LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
				"kernel", CYTTSP4_CORE_NAME, "ESD-watchdog-bl-reset", 1, "");
		//ignore the start up state to make sure queue start up would always schedule a restart.
		if (!work_pending(&cd->startup_work))
		{
			cd->startup_state = STARTUP_NONE;
			trigger_startup = true;
		}
		goto cyttsp4_timer_watchdog_exit;
	}

	cyttsp4_start_wd_timer(cd);

cyttsp4_timer_watchdog_exit:
	mutex_unlock(&cd->system_lock);
	if (trigger_startup == true)
		cyttsp4_queue_startup(cd);

	return;
}

static void cyttsp4_watchdog_timer(unsigned long handle)
{
	struct cyttsp4_core_data *cd = (struct cyttsp4_core_data *)handle;

	dev_vdbg(cd->dev, "%s: Timer triggered\n", __func__);

	if (!cd)
		return;

	if (!work_pending(&cd->watchdog_work))
		schedule_work(&cd->watchdog_work);

	return;
}

static int cyttsp4_write_(struct cyttsp4_device *ttsp, int mode, u8 addr,
	const void *buf, int size)
{
	struct device *dev = &ttsp->dev;
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	int rc = 0;

	mutex_lock(&cd->adap_lock);
	if (mode != cd->mode) {
		dev_dbg(dev, "%s: %s (having %x while %x requested)\n",
			__func__, "attempt to write in missing mode",
			cd->mode, mode);
		rc = -EACCES;
		goto exit;
	}
	rc = cyttsp4_adap_write(core->adap, addr, buf, size);
exit:
	mutex_unlock(&cd->adap_lock);
	return rc;
}

static int cyttsp4_read_(struct cyttsp4_device *ttsp, int mode, u8 addr,
	void *buf, int size)
{
	struct device *dev = &ttsp->dev;
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	int rc = 0;

	mutex_lock(&cd->adap_lock);
	if (mode != cd->mode) {
		dev_dbg(dev, "%s: %s (having %x while %x requested)\n",
			__func__, "attempt to read in missing mode",
			cd->mode, mode);
		rc = -EACCES;
		goto exit;
	}
	rc = cyttsp4_adap_read(core->adap, addr, buf, size);
exit:
	mutex_unlock(&cd->adap_lock);
	return rc;
}

static int cyttsp4_subscribe_attention_(struct cyttsp4_device *ttsp,
	enum cyttsp4_atten_type type,
	int (*func)(struct cyttsp4_device *), int mode)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	unsigned long flags;
	struct atten_node *atten, *atten_new;

	atten_new = kzalloc(sizeof(*atten_new), GFP_KERNEL);
	if (!atten_new) {
		dev_err(cd->dev, "%s: Fail alloc atten node\n", __func__);
		return -ENOMEM;
	}

	dev_dbg(cd->dev, "%s from '%s'\n", __func__, dev_name(cd->dev));

	spin_lock_irqsave(&cd->spinlock, flags);
	list_for_each_entry(atten, &cd->atten_list[type], node) {
		if (atten->ttsp == ttsp && atten->mode == mode) {
			spin_unlock_irqrestore(&cd->spinlock, flags);
			dev_vdbg(cd->dev, "%s: %s=%p %s=%d\n",
				 __func__,
				 "already subscribed attention",
				 ttsp, "mode", mode);

			return 0;
		}
	}

	atten_new->ttsp = ttsp;
	atten_new->mode = mode;
	atten_new->func = func;

	list_add(&atten_new->node, &cd->atten_list[type]);
	spin_unlock_irqrestore(&cd->spinlock, flags);

	return 0;
}

static int cyttsp4_unsubscribe_attention_(struct cyttsp4_device *ttsp,
	enum cyttsp4_atten_type type, int (*func)(struct cyttsp4_device *),
	int mode)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	struct atten_node *atten, *atten_n;
	unsigned long flags;

	spin_lock_irqsave(&cd->spinlock, flags);
	list_for_each_entry_safe(atten, atten_n, &cd->atten_list[type], node) {
		if (atten->ttsp == ttsp && atten->mode == mode) {
			list_del(&atten->node);
			spin_unlock_irqrestore(&cd->spinlock, flags);
			kfree(atten);
			dev_vdbg(cd->dev, "%s: %s=%p %s=%d\n",
				__func__,
				"unsub for atten->ttsp", atten->ttsp,
				"atten->mode", atten->mode);
			return 0;
		}
	}
	spin_unlock_irqrestore(&cd->spinlock, flags);

	return -ENODEV;
}

static int request_exclusive(struct cyttsp4_core_data *cd, void *ownptr, int t)
{
	bool with_timeout = (t != 0);

	mutex_lock(&cd->system_lock);
	if (!cd->exclusive_dev && cd->exclusive_waits == 0) {
		cd->exclusive_dev = ownptr;
		goto exit;
	}

	cd->exclusive_waits++;
wait:
	mutex_unlock(&cd->system_lock);
	if (with_timeout) {
		t = wait_event_timeout(cd->wait_q, !cd->exclusive_dev, t);
		if (IS_TMO(t)) {
			dev_err(cd->dev, "%s: tmo waiting exclusive access\n",
				__func__);
			return -ETIME;
		}
	} else {
		wait_event(cd->wait_q, !cd->exclusive_dev);
	}
	mutex_lock(&cd->system_lock);
	if (cd->exclusive_dev)
		goto wait;
	cd->exclusive_dev = ownptr;
	cd->exclusive_waits--;
exit:
	mutex_unlock(&cd->system_lock);
	dev_vdbg(cd->dev, "%s: request_exclusive ok=%p\n",
		__func__, ownptr);

	return 0;
}

static int cyttsp4_request_exclusive_(struct cyttsp4_device *ttsp, int t)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	return request_exclusive(cd, (void *)ttsp, t);
}

/*
 * returns error if was not owned
 */
static int release_exclusive(struct cyttsp4_core_data *cd, void *ownptr)
{
	mutex_lock(&cd->system_lock);
	if (cd->exclusive_dev != ownptr) {
		mutex_unlock(&cd->system_lock);
		return -EINVAL;
	}

	dev_vdbg(cd->dev, "%s: exclusive_dev %p freed\n",
		__func__, cd->exclusive_dev);
	cd->exclusive_dev = NULL;
	wake_up(&cd->wait_q);
	mutex_unlock(&cd->system_lock);
	return 0;
}

static int cyttsp4_release_exclusive_(struct cyttsp4_device *ttsp)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	return release_exclusive(cd, (void *)ttsp);
}

static int cyttsp4_wait_bl_heartbeat(struct cyttsp4_core_data *cd)
{
	long t;
	int rc = 0;

	/* wait heartbeat */
	dev_vdbg(cd->dev, "%s: wait heartbeat...\n", __func__);
	t = wait_event_timeout(cd->wait_q, cd->mode == CY_MODE_BOOTLOADER,
		msecs_to_jiffies(CY_CORE_RESET_AND_WAIT_TIMEOUT));
	if  (IS_TMO(t)) {
		LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
			"kernel", CYTTSP4_CORE_NAME, "bl_heartbeat", 1, "");
		rc = -ETIME;
	}

	return rc;
}

static int cyttsp4_wait_sysinfo_mode(struct cyttsp4_core_data *cd)
{
	long t;

	dev_vdbg(cd->dev, "%s: wait sysinfo...\n", __func__);

	t = wait_event_timeout(cd->wait_q, cd->mode == CY_MODE_SYSINFO,
		msecs_to_jiffies(CY_CORE_MODE_CHANGE_TIMEOUT));
	if (IS_TMO(t)) {
		dev_err(cd->dev, "%s: tmo waiting exit bl cd->mode=%d\n",
			__func__, cd->mode);
		mutex_lock(&cd->system_lock);
		cd->int_status &= ~CY_INT_MODE_CHANGE;
		mutex_unlock(&cd->system_lock);
		return -ETIME;
	}

	return 0;
}

static int cyttsp4_reset_and_wait(struct cyttsp4_core_data *cd)
{
	int rc = 0;
	/* reset hardware */
	mutex_lock(&cd->system_lock);
	dev_info(cd->dev, "%s: reset hw...\n", __func__);
	cd->mode = CY_MODE_UNKNOWN;

	cd->int_status |= CY_INT_IGNORE;

	rc = cyttsp4_hw_reset(cd);

	cd->int_status &= ~CY_INT_IGNORE;

	mutex_unlock(&cd->system_lock);
	if (rc < 0) {
		dev_err(cd->dev, "%s: %s adap='%s' r=%d\n", __func__,
			"Fail hw reset", cd->core->adap->id, rc);
		return rc;
	}

	return cyttsp4_wait_bl_heartbeat(cd);
}

/*
 * returns err if refused or timeout(core uses fixed timeout period) occurs;
 * blocks until ISR occurs
 */
static int cyttsp4_request_reset_(struct cyttsp4_device *ttsp)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	int rc;

	rc = cyttsp4_reset_and_wait(cd);
	if (rc < 0)
		dev_err(cd->dev, "%s: Error on h/w reset r=%d\n",
			__func__, rc);

	return rc;
}

/*
 * returns err if refused ; if no error then restart has completed
 * and system is in normal operating mode
 */
static int cyttsp4_request_restart_(struct cyttsp4_device *ttsp)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	
	if(TOUCH_HAS_LOAD_SWITCHES)
		cyttsp4_stop_wd_timer(cd);
	
	cyttsp4_startup(cd);
			
	if(TOUCH_HAS_LOAD_SWITCHES && !recovery_mode)
		cyttsp4_start_wd_timer(cd);

	return 0;
}

/*
 * returns err if refused or timeout; block until mode change complete
 * bit is set (mode change interrupt)
 */
static int set_mode(struct cyttsp4_core_data *cd, void *ownptr,
		u8 new_dev_mode, int new_op_mode)
{
	long t = 1;
	int rc;
	int retry = CY_CORE_STARTUP_RETRY_COUNT;

	/* change mode */
	dev_dbg(cd->dev, "%s: %s=%p new_dev_mode=%02X new_mode=%d\n",
			__func__, "have exclusive", cd->exclusive_dev,
			new_dev_mode, new_op_mode);

	do {
		new_dev_mode |= CY_HST_MODE_CHANGE;

		mutex_lock(&cd->system_lock);
		cd->int_status |= CY_INT_MODE_CHANGE;
		rc = cyttsp4_adap_write(cd->core->adap, CY_REG_BASE,
				&new_dev_mode, sizeof(new_dev_mode));
		if (rc < 0)
		{
			mutex_unlock(&cd->system_lock);
			dev_err(cd->dev, "%s: Fail write mode change r=%d\n",
					__func__, rc);
			goto retry;
		}
		mutex_unlock(&cd->system_lock);

		/* wait for mode change done interrupt */
		t = wait_event_timeout(cd->wait_q,
				(cd->int_status & CY_INT_MODE_CHANGE) == 0,
				msecs_to_jiffies(CY_CORE_MODE_CHANGE_TIMEOUT));

		dev_dbg(cd->dev, "%s: back from wait t=%ld cd->mode=%d\n",
				__func__, t, cd->mode);

		if (IS_TMO(t)) {
			dev_err(cd->dev, "%s: %s\n", __func__,
				"tmo waiting mode change");
		}
retry:
		retry--;
		dev_vdbg(cd->dev, "%s: retry:%d cd->mode:%d, t:%ld", __func__, retry, cd->mode, t);
	}while( retry && \
			( (cd->mode != new_op_mode) || IS_TMO(t) )
			);

	if (IS_TMO(t)) {
		dev_err(cd->dev, "%s: %s\n", __func__,
				"tmo waiting mode change");
		mutex_lock(&cd->system_lock);
		cd->int_status &= ~CY_INT_MODE_CHANGE;
		mutex_unlock(&cd->system_lock);
		rc = -EINVAL;
	}

	return rc;
}

static int cyttsp4_request_set_mode_(struct cyttsp4_device *ttsp, int mode)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	int rc;
	enum cyttsp4_hst_mode_bits mode_bits;
	switch (mode) {
	case CY_MODE_OPERATIONAL:
		mode_bits = CY_HST_OPERATE;
		break;
	case CY_MODE_SYSINFO:
		mode_bits = CY_HST_SYSINFO;
		break;
	case CY_MODE_CAT:
		mode_bits = CY_HST_CAT;
		break;
	default:
		dev_err(cd->dev, "%s: invalid mode: %02X(%d)\n",
			__func__, mode, mode);
		return -EINVAL;
	}

	rc = set_mode(cd, (void *)ttsp, mode_bits, mode);
	if (rc < 0)
		dev_err(cd->dev, "%s: fail set_mode=%02X(%d)\n",
			__func__, cd->mode, cd->mode);
#ifdef SHOK_SENSOR_DATA_MODE
	if (cd->sysinfo.monitor.mntr_status == CY_MNTR_INITIATED &&
			mode == CY_MODE_OPERATIONAL)
		cd->sysinfo.monitor.mntr_status = CY_MNTR_STARTED;
	else
		cd->sysinfo.monitor.mntr_status = CY_MNTR_DISABLED;
#endif

	return rc;
}

/*
 * returns NULL if sysinfo has not been acquired from the device yet
 */
static struct cyttsp4_sysinfo *cyttsp4_request_sysinfo_(
		struct cyttsp4_device *ttsp)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);

	if (cd->sysinfo.ready)
		return &cd->sysinfo;

	return NULL;
}

static struct cyttsp4_loader_platform_data *cyttsp4_request_loader_pdata_(
		struct cyttsp4_device *ttsp)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	return cd->pdata->loader_pdata;
}

static int cyttsp4_request_handshake_(struct cyttsp4_device *ttsp, u8 mode)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	int rc;

	rc = cyttsp4_handshake(cd, mode);
	if (rc < 0)
		dev_err(&core->dev, "%s: Fail handshake r=%d\n", __func__, rc);

	return rc;
}

static int cyttsp4_request_toggle_lowpower_(struct cyttsp4_device *ttsp,
		u8 mode)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	int rc = cyttsp4_toggle_low_power(cd, mode);
	if (rc < 0)
		dev_err(&core->dev, "%s: Fail toggle low power r=%d\n",
				__func__, rc);
	return rc;
}

static const u8 ldr_exit[] = {
	0xFF, 0x01, 0x3B, 0x00, 0x00, 0x4F, 0x6D, 0x17
};

/*
 * Send command to device for CAT and OP modes
 * return negative value on error, 0 on success
 */
static int cyttsp4_exec_cmd(struct cyttsp4_core_data *cd, u8 mode,
		u8 *cmd_buf, size_t cmd_size, u8 *return_buf,
		size_t return_buf_size, int timeout)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	struct device *dev = cd->dev;
	int cmd_ofs;
	u8 command;
	int rc;

	mutex_lock(&cd->system_lock);
	if (mode != cd->mode) {
		dev_err(dev, "%s: %s (having 0x%x while 0x%x requested)\n",
				__func__, "attempt to exec cmd in missing mode",
				cd->mode, mode);
		mutex_unlock(&cd->system_lock);
		return -EACCES;
	}

	switch (mode) {
	case CY_MODE_CAT:
		cmd_ofs = CY_REG_CAT_CMD;
		break;
	case CY_MODE_OPERATIONAL:
		cmd_ofs = si->si_ofs.cmd_ofs;
		break;
	default:
		dev_err(dev, "%s: Unsupported mode %x for exec cmd\n",
				__func__, mode);
		mutex_unlock(&cd->system_lock);
		return -EACCES;
	}

	/* Check if complete is set, so write new command */
	rc = cyttsp4_adap_read(cd->core->adap, cmd_ofs, &command, 1);
	if (rc < 0) {
		dev_err(dev, "%s: Error on read r=%d\n", __func__, rc);
		mutex_unlock(&cd->system_lock);
		return rc;
	}

	cd->cmd_toggle = GET_TOGGLE(command);
	cd->int_status |= CY_INT_EXEC_CMD;

	if ((command & CY_CMD_COMPLETE_MASK) == 0) {
		/* Let irq handler run */
		mutex_unlock(&cd->system_lock);
		rc = wait_event_timeout(cd->wait_q,
				(cd->int_status & CY_INT_EXEC_CMD) == 0,
				msecs_to_jiffies(timeout));
		if (IS_TMO(rc)) {
			dev_err(dev, "%s: Command execution timed out\n",
					__func__);
			cd->int_status &= ~CY_INT_EXEC_CMD;
			return -EINVAL;
		}

		/* For next command */
		mutex_lock(&cd->system_lock);
		rc = cyttsp4_adap_read(cd->core->adap, cmd_ofs, &command, 1);
		if (rc < 0) {
			dev_err(dev, "%s: Error on read r=%d\n", __func__, rc);
			mutex_unlock(&cd->system_lock);
			return rc;
		}
		cd->cmd_toggle = GET_TOGGLE(command);
		cd->int_status |= CY_INT_EXEC_CMD;
	}

	/*
	 * Write new command
	 * Only update command bits 0:5
	 * Clear command complete bit & toggle bit
	 */
	cmd_buf[0] = cmd_buf[0] & CY_CMD_MASK;
	rc = cyttsp4_adap_write(cd->core->adap, cmd_ofs, cmd_buf, cmd_size);
	if (rc < 0) {
		dev_err(dev, "%s: Error on write command r=%d\n",
				__func__, rc);
		mutex_unlock(&cd->system_lock);
		return rc;
	}

	/*
	 * Wait command to be completed
	 */
	mutex_unlock(&cd->system_lock);
	rc = wait_event_timeout(cd->wait_q,
			(cd->int_status & CY_INT_EXEC_CMD) == 0,
			msecs_to_jiffies(timeout));
	if (IS_TMO(rc)) {
		dev_err(dev, "%s: Command execution timed out\n", __func__);
		cd->int_status &= ~CY_INT_EXEC_CMD;
		return -EINVAL;
	}

	if (return_buf_size == 0 || return_buf == NULL)
		return 0;

	if(return_buf) {
		rc = cyttsp4_adap_read(cd->core->adap, cmd_ofs + 1, return_buf,
				return_buf_size);
		if (rc < 0) {
			dev_err(dev, "%s: Error on read 3 r=%d\n", __func__, rc);
			return rc;
		}
	}
	return 0;
}

static int cyttsp4_request_exec_cmd_(struct cyttsp4_device *ttsp, u8 mode,
		u8 *cmd_buf, size_t cmd_size, u8 *return_buf,
		size_t return_buf_size, int timeout)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	return cyttsp4_exec_cmd(cd, mode, cmd_buf, cmd_size,
			return_buf, return_buf_size, timeout);
}

#ifdef CYTTSP4_WATCHDOG_NULL_CMD
static void cyttsp4_watchdog_work_null(struct work_struct *work)
{
	struct cyttsp4_core_data *cd =
		container_of(work, struct cyttsp4_core_data, watchdog_work);
	u8 cmd_buf[CY_CMD_OP_NULL_CMD_SZ];
	int rc;

	rc = request_exclusive(cd, cd->core, 1);
	if (rc < 0) {
		dev_vdbg(cd->dev, "%s: fail get exclusive ex=%p own=%p\n",
				__func__, cd->exclusive_dev, cd->core);
		goto restart_wd;
	}

	cmd_buf[0] = CY_CMD_OP_NULL;
	rc = cyttsp4_exec_cmd(cd, cd->mode,
			cmd_buf, CY_CMD_OP_NULL_CMD_SZ,
			NULL, CY_CMD_OP_NULL_RET_SZ,
			CY_COMMAND_COMPLETE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: Watchdog NULL cmd failed.\n", __func__);
		goto error;
	}

	if (release_exclusive(cd, cd->core) < 0)
		dev_err(cd->dev, "%s: fail to release exclusive\n", __func__);
	else
		dev_vdbg(cd->dev, "%s: pass release exclusive\n", __func__);

restart_wd:
	cyttsp4_start_wd_timer(cd);
	return;

error:
	if (release_exclusive(cd, cd->core) < 0)
		dev_err(cd->dev, "%s: fail to release exclusive\n", __func__);
	else
		dev_vdbg(cd->dev, "%s: pass release exclusive\n", __func__);

	cyttsp4_queue_startup(cd);
	return;
}
#endif

static int cyttsp4_request_stop_wd_(struct cyttsp4_device *ttsp)
{
	struct cyttsp4_core *core = ttsp->core;
	struct cyttsp4_core_data *cd = dev_get_drvdata(&core->dev);
	cyttsp4_stop_wd_timer(cd);
	return 0;
}

static int cyttsp4_op_get_param(struct cyttsp4_core_data* cd, u8 cmd, u8* size, u16* data)
{
	u8 cmd_buf[CY_CMD_OP_GET_PARAM_CMD_SZ];
	u8 return_buf[CY_CMD_OP_GET_PARAM_RET_SZ];
	int rc;

	cmd_buf[0] = CY_CMD_OP_GET_PARAM;
	cmd_buf[1] = cmd;
	rc = cyttsp4_exec_cmd(cd, cd->mode,
			cmd_buf, CY_CMD_OP_GET_PARAM_CMD_SZ,
			return_buf, CY_CMD_OP_GET_PARAM_RET_SZ,
			CY_COMMAND_COMPLETE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: operational mode get parameter cmd failed. %d\n", __func__, cd->sleep_state);
		return rc;
	}

	*size = return_buf[1];
	*data = merge_bytes(return_buf[2], return_buf[3]);

/*	printk(KERN_DEBUG "%s return_buf 0x%x %x %x %x %x %x\n", __func__,
			return_buf[0], return_buf[1], return_buf[2],
			return_buf[3], return_buf[4], return_buf[5]);
	*/
	return 0;
}

static int cyttsp4_op_set_param(struct cyttsp4_core_data* cd, u8 cmd, u8 size, u16 data)
{
	u8 cmd_buf[CY_CMD_OP_SET_PARAM_CMD_SZ] = {0};
	u8 return_buf[CY_CMD_OP_SET_PARAM_RET_SZ] = {0};
	int rc;
	int max_retries = 2;

retry:
	cmd_buf[0] = CY_CMD_OP_SET_PARAM;
	cmd_buf[1] = cmd;
	cmd_buf[2] = size;

	/* Left adjust */
	switch (size) {
		case 1:
			cmd_buf[3] = 0x00FF & data;
			break;
		case 2:
			cmd_buf[3] = (0xFF00 & data) >> 8;
			cmd_buf[4] = (0x00FF) & data;
			break;
		default:
				break;
	}

	rc = cyttsp4_exec_cmd(cd, cd->mode,
			cmd_buf, CY_CMD_OP_SET_PARAM_CMD_SZ,
			return_buf, CY_CMD_OP_SET_PARAM_RET_SZ,
			CY_COMMAND_COMPLETE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: operational mode set parameter cmd (0x%02x) failed. %d\n", __func__, cmd, cd->sleep_state);
		return rc;
	}

	printk(KERN_DEBUG "%s return_buf 0x%x %x \n", __func__,
			return_buf[0], return_buf[1]);

	if (return_buf[1] != size && max_retries > 0)
	{
		msleep(10);
		max_retries--;
		goto retry;
	}

	return 0;
}

static int cyttsp4_grip_get_param(struct cyttsp4_core_data* cd, struct cyttsp4_grip_suppression_data* data)
{
	u8 size = 0;
	u16 interval;
	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_XA, &size, &data->xa);
	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_XB, &size, &data->xb);
	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_XEXA, &size, &data->xexa);
	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_XEXB, &size, &data->xexb);

	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_YA, &size, &data->ya);
	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_YB, &size, &data->yb);
	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_YEXA, &size, &data->yexa);
	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_YEXB, &size, &data->yexb);

	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_INV, &size, &interval);
	data->interval = interval;
	return 0;
}

static int cyttsp4_grip_set_param(struct cyttsp4_core_data* cd, struct cyttsp4_grip_suppression_data* data)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int ret = 0;
	int xdim;
	int ydim;

	if (!si)
		return 0;

	xdim = si->si_ofs.max_x;
	ydim = si->si_ofs.max_y;

	printk(KERN_DEBUG "cyttsp4:setting grip suppression in pixels: %d %d %d %d %d %d %d %d interval %dms",
		data->xa, data->xb, data->xexa, data->xexb, data->ya, data->yb, data->yexa, data->yexb, data->interval);

	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_XA, 2,data->xa);
	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_XB, 2, data->xb);
	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_XEXA, 2, data->xexa);
	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_XEXB, 2, data->xexb);

	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_YA, 2, data->ya);
	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_YB, 2, data->yb);
	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_YEXA, 2, data->yexa);
	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_YEXB, 2, data->yexb);

	ret += cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_INV, 1, data->interval);

	return ret;
}

static ssize_t cyttsp4_grip_suppression_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);

	u8 cmd = CY_OP_PARAM_ACT_DIS;
	u8 size = 0;
	u16 data = 0;
	char* pbuf = buf;

	pm_runtime_get_sync(dev);
	for ( ;cmd <= CY_OPT_PARAM_GRP_INV; cmd++) {
		cyttsp4_op_get_param(cd, cmd, &size, &data);
		pbuf += sprintf(pbuf, "0x%02x 0x%04x \n", cmd, data);
	}
	pm_runtime_put(dev);

	return pbuf - buf;
}

static ssize_t cyttsp4_grip_suppression_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	int ret;
	struct cyttsp4_grip_suppression_data gsd;

	if(9 != sscanf(buf, " %hd, %hd, %hd, %hd, %hd, %hd, %hd, %hd, %hhd",
		&gsd.xa, &gsd.xb, &gsd.xexa, &gsd.xexb,
		&gsd.ya, &gsd.yb, &gsd.yexa, &gsd.yexb,
		&gsd.interval))
	{
		printk(KERN_ERR "missing grip suppression parameters!");
		return -EFAULT;
	}

	pm_runtime_get_sync(dev);
	ret = cyttsp4_grip_set_param(cd, &gsd);
	pm_runtime_put(dev);

	return ret == 0 ? size : ret;
}

static ssize_t cyttsp4_enable_grip_suppression_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	u16 state;
	int ret;

	if (sscanf(buf, "%hd", &state) <= 0)
		return -EINVAL;

	state = !!state;

	pm_runtime_get_sync(dev);
	ret = cyttsp4_op_set_param(cd, CY_OPT_PARAM_GRP_SUP_EN, 1, state);
	if(0 == ret)
		cd->grip_sup_en = state;
	pm_runtime_put(dev);

	return size;
}

static ssize_t cyttsp4_enable_grip_suppression_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	u8 size = 0;
	u16 data = 0;
	pm_runtime_get_sync(dev);
	cyttsp4_op_get_param(cd, CY_OPT_PARAM_GRP_SUP_EN, &size, &data);
	//cd->grip_sup_en = data;
	pm_runtime_put(dev);

	return sprintf(buf, data ? "true\n" : "false\n");
}

static int cyttsp4_core_sleep_(struct cyttsp4_core_data *cd)
{
	enum cyttsp4_sleep_state ss = SS_SLEEP_ON;
	enum cyttsp4_int_state int_status = CY_INT_IGNORE;
	int rc = 0;
	u8 mode[2];

	dev_vdbg(cd->dev, "%s: enter...\n", __func__);

	cancel_work_sync(&cd->startup_work);
	
	if(TOUCH_HAS_LOAD_SWITCHES)
		cyttsp4_stop_wd_timer(cd);
	/* Already in sleep mode? */
	mutex_lock(&cd->startup_lock);
	mutex_lock(&cd->system_lock);
	if (cd->sleep_state == SS_SLEEP_ON || cd->sleep_state == SS_SLEEPING) {
		mutex_unlock(&cd->system_lock);
		mutex_unlock(&cd->startup_lock);
		return 0;
	}
	cd->sleep_state = SS_SLEEPING;

	if(TOUCH_HAS_LOAD_SWITCHES){
		atomic_set(&cd->ignore_irq, 1);
		disable_irq(cd->irq);
		cd->startup_state = STARTUP_NONE;
		cd->sleep_state = SS_SLEEP_ON;
		gpio_touch_reset_irq_switch(0);
		cyttsp4_vdda_vddd_enable(cd, 0);
		msleep(CY_LOAD_SWITCH_DELAY_MS);
		atomic_set(&cd->ignore_irq, 0);
		dev_info(cd->dev, "sleep tight~");
		mutex_unlock(&cd->system_lock);
		mutex_unlock(&cd->startup_lock);
		return 0;
	}

	/* Wait until currently running IRQ handler exits and disable IRQ */
	disable_irq(cd->irq);

	dev_vdbg(cd->dev, "%s: write DEEP SLEEP...\n", __func__);
	rc = cyttsp4_adap_read(cd->core->adap, CY_REG_BASE, &mode,
			sizeof(mode));
	if (rc) {
		dev_err(cd->dev, "%s: Fail read adapter r=%d\n", __func__, rc);
		cd->sleep_state = SS_SLEEP_ON;
		enable_irq(cd->irq);
		mutex_unlock(&cd->system_lock);
		if(cd->startup_state != STARTUP_RUNNING)
			rc = cyttsp4_startup_(cd);
		mutex_unlock(&cd->startup_lock);
		goto exit;
	}

	if (IS_BOOTLOADER(mode[0], mode[1])) {
		dev_err(cd->dev, "%s: Device in BOOTLADER mode.\n", __func__);
		cd->sleep_state = SS_SLEEP_ON;
		enable_irq(cd->irq);
		mutex_unlock(&cd->system_lock);
		if(cd->startup_state != STARTUP_RUNNING)
			rc = cyttsp4_startup_(cd);
		mutex_unlock(&cd->startup_lock);
		goto exit;
	}

	mode[0] |= CY_HST_SLEEP;
	rc = cyttsp4_adap_write(cd->core->adap, CY_REG_BASE, &mode[0],
			sizeof(mode[0]));
	if (rc) {
		dev_err(cd->dev, "%s: Fail write adapter r=%d\n", __func__, rc);
		cd->sleep_state = SS_SLEEP_ON;
		mutex_unlock(&cd->system_lock);
		enable_irq(cd->irq);
		if(cd->startup_state != STARTUP_RUNNING)
			rc = cyttsp4_startup_(cd);
		mutex_unlock(&cd->startup_lock);
		goto exit;
	}
	dev_vdbg(cd->dev, "%s: write DEEP SLEEP succeeded\n", __func__);

	/* Give time to FW to sleep */
	msleep(50);

	cd->sleep_state = ss;
	cd->int_status |= int_status;
	mutex_unlock(&cd->system_lock);
	mutex_unlock(&cd->startup_lock);
	enable_irq(cd->irq);
exit:
	return rc;
}

static int cyttsp4_core_sleep(struct cyttsp4_core_data *cd)
{
	int rc;

	rc = request_exclusive(cd, cd->core, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail get exclusive ex=%p own=%p\n",
				__func__, cd->exclusive_dev, cd->core);
		return 0;
	}

	rc = cyttsp4_core_sleep_(cd);

	if (release_exclusive(cd, cd->core) < 0)
		dev_err(cd->dev, "%s: fail to release exclusive\n", __func__);
	else
		dev_vdbg(cd->dev, "%s: pass release exclusive\n", __func__);

	return rc;
}

static int cyttsp4_core_wake_(struct cyttsp4_core_data *cd)
{
	struct device *dev = cd->dev;
	int rc;
	int t;

	dev_vdbg(cd->dev, "%s: enter...\n", __func__);

	if(TOUCH_HAS_LOAD_SWITCHES){
		mutex_lock(&cd->startup_lock);
		mutex_lock(&cd->system_lock);
		if (cd->sleep_state == SS_SLEEP_OFF || cd->sleep_state == SS_WAKING) {
			mutex_unlock(&cd->system_lock);
			mutex_unlock(&cd->startup_lock);
			goto exit;
		}
		//need to turn off irq and reset pin when going to sleep.
		//since we disabled irq in sleep, enable back here
		enable_irq(cd->irq);
		cd->startup_state = STARTUP_NONE;
		cd->int_status &= ~CY_INT_AWAKE;//clear INT_AWAKE flag because we are really resetting not waking up..
		cd->sleep_state = SS_SLEEP_OFF;
		mutex_unlock(&cd->system_lock);
		cyttsp4_queue_startup(cd);
		mutex_unlock(&cd->startup_lock);
		goto exit;
	}

	/* Already woken? */
	mutex_lock(&cd->startup_lock);
	mutex_lock(&cd->system_lock);
	if (cd->sleep_state == SS_SLEEP_OFF || cd->sleep_state == SS_WAKING) {
		mutex_unlock(&cd->system_lock);
		mutex_unlock(&cd->startup_lock);
		goto exit;
	}

	cd->int_status &= ~CY_INT_IGNORE;
	cd->int_status |= CY_INT_AWAKE;
	cd->sleep_state = SS_WAKING;

	//	cyttsp4_start_wd_timer(cd);
	//	we dont start wd timer because eventually startup_ will call start_wd_timer

	dev_dbg(dev, "%s: Power up HW\n", __func__);
	rc = cd->pdata->power(cd->pdata, 1, dev, &cd->ignore_irq);
	mutex_unlock(&cd->system_lock);

	t = wait_event_timeout(cd->sleep_q,
			(cd->int_status & CY_INT_AWAKE) == 0,
			msecs_to_jiffies(CY_CORE_WAKEUP_TIMEOUT));
	if (IS_TMO(t)) {
		dev_err(dev, "%s: TMO waiting for wakeup\n", __func__);
		mutex_lock(&cd->system_lock);
		cd->int_status &= ~CY_INT_AWAKE;
		/* Try starting up */
		mutex_unlock(&cd->system_lock);
		cyttsp4_queue_startup(cd);
	}
	mutex_lock(&cd->system_lock);
	cd->sleep_state = SS_SLEEP_OFF;
	mutex_unlock(&cd->system_lock);
	mutex_unlock(&cd->startup_lock);

exit:
	return 0;
}

static int cyttsp4_core_wake(struct cyttsp4_core_data *cd)
{
	int rc;

	rc = request_exclusive(cd, cd->core, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail get exclusive ex=%p own=%p\n",
				__func__, cd->exclusive_dev, cd->core);
		return 0;
	}

	rc = cyttsp4_core_wake_(cd);

	if (release_exclusive(cd, cd->core) < 0)
		dev_err(cd->dev, "%s: fail to release exclusive\n", __func__);
	else
		dev_vdbg(cd->dev, "%s: pass release exclusive\n", __func__);

	return rc;
}

static int cyttsp4_get_ic_crc(struct cyttsp4_core_data *cd, u8 ebid)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	u8 cmd_buf[CY_CMD_OP_GET_CRC_CMD_SZ];
	u8 return_buf[CY_CMD_OP_GET_CRC_RET_SZ];
	int status;
	int rc;

	cmd_buf[0] = CY_CMD_OP_GET_CRC;
	cmd_buf[1] = ebid;
	rc = cyttsp4_exec_cmd(cd, cd->mode,
			cmd_buf, CY_CMD_OP_GET_CRC_CMD_SZ,
			return_buf, CY_CMD_OP_GET_CRC_RET_SZ,
			CY_COMMAND_COMPLETE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: Unable to get CRC data.\n", __func__);
		return rc;
	}

	status = return_buf[0];
	if (status) {
		dev_err(cd->dev, "%s: Get CRC data failed. status=%d\n",
				__func__, status);
		return -1;
	}

	si->crc.ic_tt_cfg_crc[0] = return_buf[1];
	si->crc.ic_tt_cfg_crc[1] = return_buf[2];

	dev_dbg(cd->dev, "%s: CRC: ebid:%d, crc:0x%02X 0x%02X\n",
			__func__, ebid, si->crc.ic_tt_cfg_crc[0],
			si->crc.ic_tt_cfg_crc[1]);

	return rc;
}

static int cyttsp4_startup_2(struct cyttsp4_core_data *cd)
{
	int rc;

	dev_dbg(cd->dev, "%s: enter...\n", __func__);

	/* acquire the lock here to avoid someone pull the plug at the middle of start up */
	mutex_lock(&cd->startup_lock);
	mutex_lock(&cd->system_lock);

	if (cd->startup_state != STARTUP_RUNNING)
	{
		dev_err(cd->dev, "%s Startup preempted. Startup state: %d\n", __func__, STARTUP_RUNNING);
		mutex_unlock(&cd->system_lock);
		mutex_unlock(&cd->startup_lock);
		return 0;
	}

	mutex_unlock(&cd->system_lock);

	if (cd->wd_timer_started)
		cyttsp4_stop_wd_timer(cd);

	cyttsp4_toggle_loadswitch(cd);
	
	rc = cyttsp4_wait_bl_heartbeat(cd);
	if(rc)
		dev_err(cd->dev, "%s: Fail waiting for bl heartbeat!! r=%d\n",
			__func__, rc);
	/* exit bl into sysinfo mode */

	dev_vdbg(cd->dev, "%s: write exit ldr...\n", __func__);
	cd->int_status &= ~CY_INT_IGNORE;
	cd->int_status |= CY_INT_MODE_CHANGE;
	
	/*
	 * this extra delay is put here for the case such that the wait_for_bl_heartbeat function call was tricked by 
	 * a false interrupt, in some cases, by ESD. The delay would help the controller get out of the reset state, 
	 * and take the exit bootloader command.  
	 * */
	msleep(50);
	
	rc = cyttsp4_adap_write(cd->core->adap, CY_REG_BASE,
		(u8 *)ldr_exit, sizeof(ldr_exit));
	if (rc < 0) {
		dev_err(cd->dev, "%s: Fail write adap='%s' r=%d line=%d\n",
			__func__, cd->core->adap->id, rc, __LINE__);
		goto exit;
	}

	rc = cyttsp4_wait_sysinfo_mode(cd);
	if (rc < 0) {
		/*
		 * Unable to switch to SYSINFO mode,
		 * Corrupted FW may cause crash, exit here.
		 */

		dev_err(cd->dev, "%s: Fail wait sysinfo mode r=%d line=%d\n",
			__func__, rc, __LINE__);
		goto exit;
	}

	/* switch to operational mode */
	dev_vdbg(cd->dev, "%s: set mode cd->core=%p hst_mode=%02X mode=%d...\n",
		__func__, cd->core, CY_HST_OPERATE, CY_MODE_OPERATIONAL);
	rc = set_mode(cd, cd->core, CY_HST_OPERATE, CY_MODE_OPERATIONAL);
	if (rc < 0) {
		dev_err(cd->dev, "%s: failed to set mode to operational rc=%d\n",
				__func__, rc);
		goto exit;
	}

	/* Required for signal to the TTHE */
	dev_info(cd->dev, "%s: cyttsp4_exit startup r=%d...\n", __func__, rc);

exit:
	if(cd->wd_timer_started && !recovery_mode)
		cyttsp4_start_wd_timer(cd);
	mutex_unlock(&cd->startup_lock);
	return rc;
}

static int cyttsp4_startup_(struct cyttsp4_core_data *cd)
{
	int retry = CY_CORE_STARTUP_RETRY_COUNT;
	struct atten_node *atten, *atten_n;
	unsigned long flags;
	int rc;

	dev_dbg(cd->dev, "%s: enter...\n", __func__);

reset:
	if (retry != CY_CORE_STARTUP_RETRY_COUNT)
		dev_dbg(cd->dev, "%s: Retry %d\n", __func__,
				CY_CORE_STARTUP_RETRY_COUNT - retry);

	/* reset hardware and wait for heartbeat */
	rc = cyttsp4_reset_and_wait(cd);
	if (rc < 0) {
		dev_err(cd->dev, "%s: Error on h/w reset r=%d\n", __func__, rc);
		if (retry--)
			goto reset;
		goto exit;
	}

	/* exit bl into sysinfo mode */
	dev_vdbg(cd->dev, "%s: write exit ldr...\n", __func__);
	mutex_lock(&cd->system_lock);
	cd->int_status &= ~CY_INT_IGNORE;
	cd->int_status |= CY_INT_MODE_CHANGE;

	rc = cyttsp4_adap_write(cd->core->adap, CY_REG_BASE,
		(u8 *)ldr_exit, sizeof(ldr_exit));
	mutex_unlock(&cd->system_lock);
	if (rc < 0) {
		dev_err(cd->dev, "%s: Fail write adap='%s' r=%d\n",
			__func__, cd->core->adap->id, rc);
		if (retry--)
			goto reset;
		goto exit;
	}

	rc = cyttsp4_wait_sysinfo_mode(cd);
	if (rc < 0) {
		/*
		 * Unable to switch to SYSINFO mode,
		 * Corrupted FW may cause crash, exit here.
		 */
		if (retry--)
			goto reset;
		goto exit;
	}

	/* read sysinfo data */
	dev_vdbg(cd->dev, "%s: get sysinfo regs..\n", __func__);
	rc = cyttsp4_get_sysinfo_regs(cd);
	if (rc < 0) {
		dev_err(cd->dev, "%s: failed to get sysinfo regs rc=%d\n",
			__func__, rc);
		if (retry--)
			goto reset;
		goto exit;
	}

	/* switch to operational mode */
	dev_vdbg(cd->dev, "%s: set mode cd->core=%p hst_mode=%02X mode=%d...\n",
		__func__, cd->core, CY_HST_OPERATE, CY_MODE_OPERATIONAL);
	rc = set_mode(cd, cd->core, CY_HST_OPERATE, CY_MODE_OPERATIONAL);
	if (rc < 0) {
		dev_err(cd->dev, "%s: failed to set mode to operational rc=%d\n",
				__func__, rc);
		if (retry--)
			goto reset;
		goto exit;
	}

	rc = cyttsp4_get_ic_crc(cd, CY_TCH_PARM_EBID);
	if (rc < 0) {
		dev_err(cd->dev, "%s: failed to crc data rc=%d\n",
			__func__, rc);
		if (retry--)
			goto reset;
		goto exit;
	}
		/* attention startup */
	spin_lock_irqsave(&cd->spinlock, flags);
		list_for_each_entry_safe(atten, atten_n,
			&cd->atten_list[CY_ATTEN_STARTUP], node) {
			dev_dbg(cd->dev, "%s: attention for '%s'", __func__,
				dev_name(&atten->ttsp->dev));
			spin_unlock_irqrestore(&cd->spinlock, flags);
			atten->func(atten->ttsp);
			spin_lock_irqsave(&cd->spinlock, flags);
		}
	spin_unlock_irqrestore(&cd->spinlock, flags);

	/* restore to sleep if was suspended */
	mutex_lock(&cd->system_lock);

	if (cd->sleep_state == SS_SLEEP_ON) {
		cd->sleep_state = SS_SLEEP_OFF;
		mutex_unlock(&cd->system_lock);
		dev_info(cd->dev, "%s: startup sleeping...\n", __func__);

		if(TOUCH_HAS_LOAD_SWITCHES) {
			;//do nothing
		} else {
			cyttsp4_core_sleep_(cd);
		}
		goto exit;
	}
	mutex_unlock(&cd->system_lock);

	/* Required for signal to the TTHE */
	dev_info(cd->dev, "%s: cyttsp4_exit startup r=%d...\n", __func__, rc);

exit:
	return rc;
}

static int cyttsp4_startup(struct cyttsp4_core_data *cd)
{
	int rc;

	mutex_lock(&cd->system_lock);
	cd->startup_state = STARTUP_RUNNING;
	mutex_unlock(&cd->system_lock);

	rc = request_exclusive(cd, cd->core, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail get exclusive ex=%p own=%p\n",
				__func__, cd->exclusive_dev, cd->core);
		goto exit;
	}

	rc = cyttsp4_startup_(cd);

	if (release_exclusive(cd, cd->core) < 0)
		/* Don't return fail code, mode is already changed. */
		dev_err(cd->dev, "%s: fail to release exclusive\n", __func__);
	else
		dev_vdbg(cd->dev, "%s: pass release exclusive\n", __func__);

exit:
	mutex_lock(&cd->system_lock);
	cd->startup_state = STARTUP_NONE;
	mutex_unlock(&cd->system_lock);

	return rc;
}

static void cyttsp4_free_si_ptrs(struct cyttsp4_core_data *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;

	if (!si)
		return;

	if (si->si_ptrs.cydata)
		kfree(si->si_ptrs.cydata->mfg_id);

	kfree(si->si_ptrs.cydata);
	kfree(si->si_ptrs.test);
	kfree(si->si_ptrs.pcfg);
	kfree(si->si_ptrs.opcfg);
	kfree(si->si_ptrs.ddata);
	kfree(si->si_ptrs.mdata);
	kfree(si->btn);
	kfree(si->xy_mode);
	kfree(si->xy_data);
	kfree(si->btn_rec_data);
}

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM_RUNTIME)
static int cyttsp4_core_suspend(struct device *dev)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	int rc;

	dev_dbg(dev, "%s\n", __func__);

	rc = cyttsp4_core_sleep(cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error on sleep\n", __func__);
		return -EAGAIN;
	}

	/*
	 * when there is no load switch to power gate touch, such as EVT4 and before,
	 * irq is disabled in suspend to avoid accidental wake ups.
	 * */
	if(!TOUCH_HAS_LOAD_SWITCHES) {
		disable_irq(cd->irq);
	}
	return 0;
}

static int cyttsp4_core_resume(struct device *dev)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	int rc;

	dev_dbg(dev, "%s\n", __func__);

	/*
	 * when there is no load switch to power gate touch, such as EVT4 and before, irq is disabled in suspend.
	 * enable back here
	 * */
	if(!TOUCH_HAS_LOAD_SWITCHES) {
		enable_irq(cd->irq);
	}

	//it's locked, let sleep in..
	if(cd->exlock)
		return 0;

	rc = cyttsp4_core_wake(cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error on wake\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

/*
static int cyttsp4_core_runtime_suspend(struct device *dev)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	int rc;

	dev_dbg(dev, "%s\n", __func__);

	rc = cyttsp4_core_sleep(cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error on sleep\n", __func__);
		return -EAGAIN;
	}
	return 0;
}

static int cyttsp4_core_runtime_resume(struct device *dev)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	int rc;

	dev_dbg(dev, "%s\n", __func__);

	//it's locked, let sleep in..
	if(cd->exlock)
		return 0;

	rc = cyttsp4_core_wake(cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error on wake\n", __func__);
		return -EAGAIN;
	}

	return 0;
}*/
#endif

static const struct dev_pm_ops cyttsp4_core_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cyttsp4_core_suspend, cyttsp4_core_resume)
	//SET_RUNTIME_PM_OPS(cyttsp4_core_runtime_suspend, cyttsp4_core_runtime_resume, NULL)
};

/*
 * Show Firmware version via sysfs
 */
static ssize_t cyttsp4_ic_ver_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	struct cyttsp4_cydata *cydata = cd->sysinfo.si_ptrs.cydata;

	return sprintf(buf,
		"%s: %02X%02X\n"
		"%s: 0x%02X 0x%02X\n"
		"%s: 0x%02X\n"
		"%s: 0x%02X\n"
		"%s: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n"
		"%s: 0x%02X\n"
		"%s: 0x%02X\n",
		"Lab126 firmware version", cd->sysinfo.si_ptrs.ddata->lab126_fw_ver0, cd->sysinfo.si_ptrs.ddata->lab126_fw_ver1,
		"TrueTouch Product ID", cydata->ttpidh, cydata->ttpidl,
		"Firmware Major Version", cydata->fw_ver_major,
		"Firmware Minor Version", cydata->fw_ver_minor,
		"Revision Control Number", cydata->revctrl[0],
		cydata->revctrl[1], cydata->revctrl[2], cydata->revctrl[3],
		cydata->revctrl[4], cydata->revctrl[5], cydata->revctrl[6],
		cydata->revctrl[7],
		"Bootloader Major Version", cydata->blver_major,
		"Bootloader Minor Version", cydata->blver_minor);
}

/*
 * Show Driver version via sysfs
 */
static ssize_t cyttsp4_drv_ver_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Driver: %s\nVersion: %s\nDate: %s\n",
		cy_driver_core_name, cy_driver_core_version,
		cy_driver_core_date);
}

/*
 * HW reset via sysfs
 */
static ssize_t cyttsp4_hw_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	int rc = 0;

	switch(buf[0]){
		case 'a':
			if(CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696) {
				if(buf[1] == '1') {
					if(!regulator_is_enabled(cd->vddd))
						regulator_enable(cd->vddd);
				}else {
					if(regulator_is_enabled(cd->vddd))
						regulator_disable(cd->vddd);
				}
			} else {
				gpio_touch_switch_power_1v8(buf[1] == '1');
			}
			printk(KERN_ERR "1v8 switch!!%d", buf[1] == '1');
			return size;
		break;
		case 'b':
			if(CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696) {
				if(buf[1] == '1') {
					if(!regulator_is_enabled(cd->vdda))
						regulator_enable(cd->vdda);
				}else {
					if(regulator_is_enabled(cd->vdda))
						regulator_disable(cd->vdda);
				}
			} else {
				gpio_touch_switch_power_3v2(buf[1] == '1');
			}
			printk(KERN_ERR "3v2 switch!!%d", buf[1] == '1');
			return size;
		break;
		case 'c':
			gpio_touch_reset_irq_switch(buf[1] == '1');
			printk(KERN_ERR "irq-switch going to %d", buf[1] == '1');
			return size;
		default:
		break;
	}
	
	if(TOUCH_HAS_LOAD_SWITCHES)
		cyttsp4_stop_wd_timer(cd);
	
	rc = cyttsp4_startup(cd);
	if (rc < 0)
		dev_err(dev, "%s: HW reset failed r=%d\n",
			__func__, rc);
			
	if(TOUCH_HAS_LOAD_SWITCHES && !recovery_mode)
		cyttsp4_start_wd_timer(cd);

	return size;
}

/*
 * Show IRQ status via sysfs
 */
static ssize_t cyttsp4_hw_irq_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	int retval;

	if (cd->pdata->irq_stat) {
		retval = cd->pdata->irq_stat(cd->pdata, dev);
		switch (retval) {
		case 0:
			return snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Interrupt line is LOW.\n");
		case 1:
			return snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Interrupt line is HIGH.\n");
		default:
			return snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Function irq_stat() returned %d.\n", retval);
		}
	}

	return snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Function irq_stat() undefined.\n");
}

/*
 * Show IRQ enable/disable status via sysfs
 */
static ssize_t cyttsp4_drv_irq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&cd->system_lock);
	if (cd->irq_enabled)
		ret = snprintf(buf, CY_MAX_PRBUF_SIZE,
			"Driver interrupt is ENABLED\n");
	else
		ret = snprintf(buf, CY_MAX_PRBUF_SIZE,
			"Driver interrupt is DISABLED\n");
	mutex_unlock(&cd->system_lock);

	return ret;
}

/*
 * Enable/disable IRQ via sysfs
 */
static ssize_t cyttsp4_drv_irq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	unsigned long value;
	int retval = 0;

	retval = strict_strtoul(buf, 10, &value);
	if (retval < 0) {
		dev_err(dev, "%s: Invalid value\n", __func__);
		goto cyttsp4_drv_irq_store_error_exit;
	}

	mutex_lock(&cd->system_lock);
	switch (value) {
	case 0:
		if (cd->irq_enabled) {
			cd->irq_enabled = false;
			/* Disable IRQ */
			disable_irq_nosync(cd->irq);
			dev_info(dev, "%s: Driver IRQ now disabled\n",
				__func__);
		} else
			dev_info(dev, "%s: Driver IRQ already disabled\n",
				__func__);
		break;

	case 1:
		if (cd->irq_enabled == false) {
			cd->irq_enabled = true;
			/* Enable IRQ */
			enable_irq(cd->irq);
			dev_info(dev, "%s: Driver IRQ now enabled\n",
				__func__);
		} else
			dev_info(dev, "%s: Driver IRQ already enabled\n",
				__func__);
		break;

	default:
		dev_err(dev, "%s: Invalid value\n", __func__);
	}
	mutex_unlock(&(cd->system_lock));

cyttsp4_drv_irq_store_error_exit:

	return size;
}

/*
 * Debugging options via sysfs
 */
static ssize_t cyttsp4_drv_debug_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	unsigned long value = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &value);
	if (rc < 0) {
		dev_err(dev, "%s: Invalid value\n", __func__);
		goto cyttsp4_drv_debug_store_exit;
	}

	switch (value) {
	case 4:	//suspend
		dev_info(dev, "%s: SUSPEND (cd=%p)\n", __func__, cd);
		rc = cyttsp4_core_sleep(cd);
		if (rc)
			dev_err(dev, "%s: Suspend failed rc=%d\n",
				__func__, rc);
		else
			dev_info(dev, "%s: Suspend succeeded\n", __func__);
		break;

	case 5: //resume
		dev_info(dev, "%s: RESUME (cd=%p)\n", __func__, cd);
		rc = cyttsp4_core_wake(cd);
		if (rc)
			dev_err(dev, "%s: Resume failed rc=%d\n",
				__func__, rc);
		else
			dev_info(dev, "%s: Resume succeeded\n", __func__);
		break;
	case 97: //soft reset
		dev_info(dev, "%s: SOFT RESET (cd=%p)\n", __func__, cd);
		rc = cyttsp4_hw_soft_reset(cd);
		break;
	case 98: //hard reset
		dev_info(dev, "%s: HARD RESET (cd=%p)\n", __func__, cd);
		rc = cyttsp4_hw_hard_reset(cd);
		break;
#ifdef DEVELOPMENT_MODE
	case 6:	//time interrupt to input event ON
		dev_info(dev, "%s: performance debug on(cd=%p)\n", __func__, cd);
		g_cyttsp4_timeofdata_us=timeofday_microsec();
		break;
	case 7: //time interrupt to input event OFF
		dev_info(dev, "%s: performance debug off(cd=%p)\n", __func__, cd);
		g_cyttsp4_timeofdata_us=0;
		break;
#endif
	default:
		dev_err(dev, "%s: Invalid value\n", __func__);
	}

cyttsp4_drv_debug_store_exit:
	return size;
}

/*
 * Show system status on deep sleep status via sysfs
 */
static ssize_t cyttsp4_sleep_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&cd->system_lock);
	if (cd->sleep_state == SS_SLEEP_ON)
		ret = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Deep Sleep is ENABLED\n");
	else
		ret = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Deep Sleep is DISABLED\n");
	mutex_unlock(&cd->system_lock);

	return ret;
}

/**** PROC ENTRY ****/
static int cyttsp4_proc_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;

	if(NULL == g_core_data) {
		printk(KERN_ERR "cyttsp_proc_read:proc:not ready\n");
		return -EFAULT;
	}

	if (off > 0) {
		*eof = 1;
		return 0;
	}
	if(g_core_data->sleep_state != SS_SLEEP_OFF)
		len = sprintf(page, "touch is locked\n");
	else
		len = sprintf(page, "touch is unlocked\n");

	return len;
}

static ssize_t cyttsp4_proc_write( struct file *filp, const char __user *buff,
		unsigned long len, void *data )
{
	char command[16];
	int sts = 0;

	if(NULL == g_core_data) {
		printk(KERN_ERR "cyttsp_proc_write:proc:not ready\n");
		return -EFAULT;
	}
	if (len >= 16){
		printk(KERN_ERR "cyttsp_proc_write:proc:command is too long!\n");
		return -ENOSPC;
	}

	if (copy_from_user(command, buff, len)) {
		printk(KERN_ERR "cyttsp_proc_write:proc::cannot copy from user!\n");
		return -EFAULT;
	}

	if ( !strncmp(command, "unlock", 6) ) {
		printk("cyttsp: I cyttsp_proc_write::command=unlock:\n");
			sts = cyttsp4_core_wake(g_core_data);
			if (sts < 0) {
					printk(KERN_ERR "cyttsp_proc_write:proc:command=%s:not succeed please retry\n", command);
					return -EBUSY;
			}
			g_core_data->exlock = false;
	} else if ( !strncmp(command, "lock", 4) ) {
		printk("cyttsp: I cyttsp_proc_write::command=lock:\n");
			sts = cyttsp4_core_sleep(g_core_data);
			if (sts < 0){
					printk(KERN_ERR "cyttsp_proc_write:proc:command=%s:not succeed please retry\n", command);
					return -EBUSY;
			}
			g_core_data->exlock = true;
	} else if (!strncmp(command, "cali", 4)) {
		if (cyttsp4_easy_calibrate) {

			LLOG_DEVICE_METRIC(DEVICE_METRIC_LOW_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
				"kernel", CYTTSP4_CORE_NAME, "calibrate", 1, "");

			cyttsp4_easy_calibrate(1);
		}
		else {
			printk(KERN_ERR "cyttsp: I :please modprobe cyttsp4_device_access before using this command\n");
		}

	} else {
	        printk(KERN_ERR "cyttsp_proc_write:proc:command=%s:Unrecognized command\n", command);
	}
	return len;
}

/**** END PROC ENTRY ****/

static struct device_attribute attributes[] = {
	__ATTR(ic_ver, S_IRUGO, cyttsp4_ic_ver_show, NULL),
	__ATTR(drv_ver, S_IRUGO, cyttsp4_drv_ver_show, NULL),
	__ATTR(hw_reset, S_IWUSR, NULL, cyttsp4_hw_reset_store),
	__ATTR(hw_irq_stat, S_IRUSR, cyttsp4_hw_irq_stat_show, NULL),
	__ATTR(drv_irq, S_IRUSR | S_IWUSR, cyttsp4_drv_irq_show,
		cyttsp4_drv_irq_store),
	__ATTR(drv_debug, S_IWUSR, NULL, cyttsp4_drv_debug_store),
	__ATTR(sleep_status, S_IRUSR, cyttsp4_sleep_status_show, NULL),
	__ATTR(grip_suppression, S_IRUSR | S_IWUSR,
		cyttsp4_grip_suppression_show, cyttsp4_grip_suppression_store),
	__ATTR(enable_grip_suppression, S_IRUSR | S_IWUSR,
		cyttsp4_enable_grip_suppression_show, cyttsp4_enable_grip_suppression_store),
};

static struct proc_dir_entry *proc_entry = NULL;

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
	{
		if (device_create_file(dev, attributes + i))
			goto undo;
	}

	proc_entry = create_proc_entry("touch", 0644, NULL );
	if (proc_entry == NULL) {
		dev_err(dev, "create_proc: could not create proc entry\n");
		goto undo;
	}
	proc_entry->read_proc = cyttsp4_proc_read;
	proc_entry->write_proc = cyttsp4_proc_write;

	return 0;
undo:
	for (; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	if(proc_entry)
		remove_proc_entry("touch", NULL);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}

static void remove_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);
	if(proc_entry)
		remove_proc_entry("touch", NULL);
}

static long cyttsp4_core_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret = 0;
	int grip_en;
	struct cyttsp4_grip_suppression_data gsd;

	if(NULL == g_core_data)
		return -EFAULT;

	switch (cmd) {
		case TOUCH_IOCTL_GRIP_SUP_EN:
		{
			if(copy_from_user(&grip_en, argp, sizeof(int)))
				return -EFAULT;
			pm_runtime_get_sync(g_core_data->dev);
			grip_en = grip_en > 0 ? 1 : 0;
			printk(KERN_INFO "cyttsp4: Doing grip suppression enable : %d\n", grip_en);
			ret = cyttsp4_op_set_param(g_core_data, CY_OPT_PARAM_GRP_SUP_EN, 1, grip_en);
			if (ret == 0)
				g_core_data->grip_sup_en = grip_en;
			pm_runtime_put(g_core_data->dev);
		}
		break;
		case CY4_IOCTL_GRIP_SET_DATA:
		{
			if(copy_from_user(&gsd, argp, sizeof(struct cyttsp4_grip_suppression_data)))
				return -EFAULT;
			pm_runtime_get_sync(g_core_data->dev);
			ret = cyttsp4_grip_set_param(g_core_data, &gsd);
			pm_runtime_put(g_core_data->dev);
		}
		break;
		case CY4_IOCTL_GRIP_GET_DATA:
		{
			struct cyttsp4_grip_suppression_data gdata;
			pm_runtime_get_sync(g_core_data->dev);
			ret = cyttsp4_grip_get_param(g_core_data, &gdata);
			pm_runtime_put(g_core_data->dev);
			if (copy_to_user(argp, &gdata, sizeof(struct cyttsp4_grip_suppression_data)))
				return -EFAULT;
		}
		break;
		case TOUCH_IOCTL_GET_LOCK:
		{
			int locked = g_core_data->exlock;
			if (copy_to_user(argp, &locked, sizeof(locked)))
				return -EFAULT;
			break;
		}
		case TOUCH_IOCTL_SET_LOCK:
		{
			int locked;
			if (copy_from_user(&locked, argp, sizeof(locked)))
				return -EFAULT;

			if (g_core_data->exlock == !!locked)
				break;
			if (locked)
			{
				int sts = cyttsp4_core_sleep(g_core_data);
				if (sts < 0){
					printk(KERN_ERR "cyttsp_proc_write:proc:command=lock:not succeed please retry\n");
					return -EBUSY;
				}
				g_core_data->exlock = true;
			}
			else
			{
				int sts = cyttsp4_core_wake(g_core_data);
				if (sts < 0) {
					printk(KERN_ERR "cyttsp_proc_write:proc:command=unlock:not succeed please retry\n");
					return -EBUSY;
				}
				g_core_data->exlock = false;
			}
			break;
		}
		default:
			ret = -ENOTTY;
		break;
	}
	return ret;
}

static ssize_t cyttsp4_misc_write_not_implemented(struct file *file, const char __user *buf,
                                 size_t count, loff_t *pos)
{
  return 0;
}

static ssize_t cyttsp4_misc_read_not_implemented(struct file *file, char __user *buf,
                                size_t count, loff_t *pos)
{
  return 0;
}

static const struct file_operations cyttsp4_misc_fops =
{
  .owner = THIS_MODULE,
  .read  = cyttsp4_misc_read_not_implemented,
  .write = cyttsp4_misc_write_not_implemented,
  .unlocked_ioctl = cyttsp4_core_ioctl,
};

static struct miscdevice cyttsp4_misc_device =
{
  .minor = 0,
  .name  = TOUCH_MISC_DEV_NAME,
  .fops  = &cyttsp4_misc_fops,
};

static int cyttsp4_core_probe(struct cyttsp4_core *core)
{
	struct cyttsp4_core_data *cd;
	struct device *dev = &core->dev;
	struct cyttsp4_core_platform_data *pdata = dev_get_platdata(dev);
	enum cyttsp4_atten_type type;
	unsigned long irq_flags;
	int rc = 0;

	dev_info(dev, "%s: startup\n", __func__);
	dev_dbg(dev, "%s: debug on\n", __func__);
	dev_vdbg(dev, "%s: verbose debug on\n", __func__);

	/* get context and debug print buffers */
	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (cd == NULL) {
		dev_err(dev, "%s: Error, kzalloc\n", __func__);
		rc = -ENOMEM;
		goto error_alloc_data_failed;
	}

	/* point to core device and init lists */
	cd->core = core;
	mutex_init(&cd->system_lock);
	mutex_init(&cd->adap_lock);
	mutex_init(&cd->startup_lock);
	for (type = 0; type < CY_ATTEN_NUM_ATTEN; type++)
		INIT_LIST_HEAD(&cd->atten_list[type]);
	init_waitqueue_head(&cd->wait_q);
	init_waitqueue_head(&cd->sleep_q);

	if(CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696) {
		cd->vddd = regulator_get(NULL, "TOUCH_VDDD");
		if(!cd->vddd)
			dev_err(dev, "expected regulator TOUCH_VDDD but return null\n");
		cd->vdda = regulator_get(NULL, "TOUCH_VDDA");
		if(!cd->vdda)
			dev_err(dev, "expected regulator TOUCH_VDDA but return null\n");
		
		if(cd->vdda && cd->vddd) {
			regulator_enable(cd->vdda);
			regulator_enable(cd->vddd);
		}
	}
	else if(TOUCH_HAS_LOAD_SWITCHES ) {
		gpio_init_touch_switch_power();
	}
	dev_dbg(dev, "%s: initialize core data\n", __func__);

	spin_lock_init(&cd->spinlock);

	cd->dev = dev;
	cd->pdata = pdata;
	cd->irq = gpio_to_irq(pdata->irq_gpio);
	cd->irq_enabled = true;
	
	
	dev_set_drvdata(dev, cd);
	if (cd->irq < 0) {
		rc = -EINVAL;
		goto error_gpio_irq;
	}

	if (cd->pdata->init) {
		dev_info(cd->dev, "%s: Init HW\n", __func__);
		rc = cd->pdata->init(cd->pdata, 1, cd->dev);
	} else {
		dev_info(cd->dev, "%s: No HW INIT function\n", __func__);
		rc = 0;
	}
	if (rc < 0)
		dev_err(cd->dev, "%s: HW Init fail r=%d\n", __func__, rc);

	cd->startup_wq = alloc_workqueue("Cyttsp4 startup wq",
			WQ_MEM_RECLAIM |
			WQ_CPU_INTENSIVE | WQ_HIGHPRI, 1);

	INIT_WORK(&cd->startup_work, cyttsp4_startup_work_function);

	dev_dbg(dev, "%s: initialize threaded irq=%d\n", __func__, cd->irq);
	if (cd->pdata->level_irq_udelay > 0)
		/* use level triggered interrupts */
		irq_flags = IRQF_TRIGGER_LOW | IRQF_ONESHOT;
	else
		/* use edge triggered interrupts */
		irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;

	rc = request_threaded_irq(cd->irq, NULL, cyttsp4_irq, irq_flags,
		dev_name(dev), cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error, could not request irq\n", __func__);
		goto error_request_irq;
	}

	cd->grip_sup_en = 0;

	pm_runtime_enable(dev);

	/*
	 * call startup directly to ensure that the device
	 * is tested before leaving the probe
	 */
	dev_dbg(dev, "%s: call startup\n", __func__);

	dev_info(dev, "recovery_mode=%d", recovery_mode);

	if(TOUCH_HAS_LOAD_SWITCHES) {
		cyttsp4_toggle_loadswitch(cd);
		cd->startup_state = STARTUP_RUNNING;
		rc = cyttsp4_startup_2(cd);
		if (rc < 0) {
			dev_err(cd->dev, "%s: Fail initial load switch startup r=%d\n",
				__func__, rc);
			/*in case of touch firmware corruption, continue to create sys entries for reflashing touch firmware*/
			if(!recovery_mode)
				goto error_startup_2;
		}
	}
	pm_runtime_get_sync(dev);
	rc = cyttsp4_startup(cd);
	pm_runtime_put(dev);
	if (rc < 0) {
		dev_err(cd->dev, "%s: Fail initial startup r=%d\n",
			__func__, rc);
		/*in case of touch firmware corruption, continue to create sys entries for reflashing touch firmware*/
		if(!recovery_mode)
			goto error_startup;
	}

	g_core_data = cd;

	dev_dbg(dev, "%s: add sysfs interfaces\n", __func__);
	rc = add_sysfs_interfaces(dev);
	if (rc < 0) {
		dev_err(dev, "%s: Error, fail sysfs init\n", __func__);
		goto error_attr_create;
	}

#ifdef CYTTSP4_WATCHDOG_NULL_CMD
	INIT_WORK(&cd->watchdog_work, cyttsp4_watchdog_work_null);
#endif

	/*Icewine special case for watchdog*/
	if(TOUCH_HAS_LOAD_SWITCHES) {
		INIT_WORK(&cd->watchdog_work, cyttsp4_watchdog_work);
		setup_timer(&cd->watchdog_timer, cyttsp4_watchdog_timer,
			(unsigned long)cd);
		if(!recovery_mode)
			cyttsp4_start_wd_timer(cd);
		cd->wd_timer_started = true;
	}

	dev_info(dev, "%s: ok\n", __func__);
	rc = 0;

	if (misc_register(&cyttsp4_misc_device)) {
		dev_err(dev, "%s: Error, register misc device \n", __func__);
		goto error_attr_create;
	}

	goto no_error;

error_attr_create:
error_startup:
	pm_runtime_disable(dev);
	cyttsp4_free_si_ptrs(cd);
error_startup_2:
	free_irq(cd->irq, cd);
error_request_irq:
error_gpio_irq:
	if (pdata->init)
		pdata->init(pdata, 0, dev);
	dev_set_drvdata(dev, NULL);
	kfree(cd);
error_alloc_data_failed:
	LLOG_DEVICE_METRIC(DEVICE_METRIC_HIGH_PRIORITY, DEVICE_METRIC_TYPE_COUNTER,
			"kernel", CYTTSP4_CORE_NAME, "probe_failed", 1, "");
no_error:
	return rc;
}

static int cyttsp4_core_release(struct cyttsp4_core *core)
{
	struct device *dev = &core->dev;
	struct cyttsp4_core_data *cd = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	pm_runtime_suspend(dev);
	pm_runtime_disable(dev);

	if(TOUCH_HAS_LOAD_SWITCHES){
		cyttsp4_stop_wd_timer(cd);
	}
	cancel_work_sync(&cd->startup_work);
	flush_workqueue(cd->startup_wq);
	destroy_workqueue(cd->startup_wq);

	remove_sysfs_interfaces(dev);
	free_irq(cd->irq, cd);
	if (cd->pdata->init)
		cd->pdata->init(cd->pdata, 0, dev);
	dev_set_drvdata(dev, NULL);
	cyttsp4_free_si_ptrs(cd);
	misc_deregister(&cyttsp4_misc_device);
	
	if(CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696) {
		regulator_put(cd->vddd);
		regulator_put(cd->vdda);
	}
	
	kfree(cd);
	return 0;
}

struct cyttsp4_core_driver cyttsp4_core_driver = {
	.probe = cyttsp4_core_probe,
	.remove = cyttsp4_core_release,
	.subscribe_attention = cyttsp4_subscribe_attention_,
	.unsubscribe_attention = cyttsp4_unsubscribe_attention_,
	.request_exclusive = cyttsp4_request_exclusive_,
	.release_exclusive = cyttsp4_release_exclusive_,
	.request_reset = cyttsp4_request_reset_,
	.request_restart = cyttsp4_request_restart_,
	.request_set_mode = cyttsp4_request_set_mode_,
	.request_sysinfo = cyttsp4_request_sysinfo_,
	.request_loader_pdata = cyttsp4_request_loader_pdata_,
	.request_handshake = cyttsp4_request_handshake_,
	.request_exec_cmd = cyttsp4_request_exec_cmd_,
	.request_stop_wd = cyttsp4_request_stop_wd_,
	.request_toggle_lowpower = cyttsp4_request_toggle_lowpower_,
	.write = cyttsp4_write_,
	.read = cyttsp4_read_,
	.driver = {
		.name = CYTTSP4_CORE_NAME,
		.bus = &cyttsp4_bus_type,
		.owner = THIS_MODULE,
		.pm = &cyttsp4_core_pm_ops,
	},
};

static int __init cyttsp4_core_init(void)
{
	int rc = 0;

	rc = cyttsp4_register_core_driver(&cyttsp4_core_driver);
	pr_info("%s: Cypress TTSP v4 core driver (Built %s) rc=%d\n",
		 __func__, CY_DRIVER_DATE, rc);
	return rc;
}
module_init(cyttsp4_core_init);

static void __exit cyttsp4_core_exit(void)
{
	cyttsp4_unregister_core_driver(&cyttsp4_core_driver);
	pr_info("%s: module exit\n", __func__);
}
module_exit(cyttsp4_core_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard touchscreen core driver");
MODULE_AUTHOR("Aleksej Makarov <aleksej.makarov@sonyericsson.com>");
