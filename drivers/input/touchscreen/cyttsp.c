/* 
 * Copyright (C) 2009, 2010 Cypress Semiconductor, Inc.
 *
 * Copyright 2011 Amazon.com, Inc. All rights reserved.
 * Haili Wang (hailiw@lab126.com)
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

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/byteorder/generic.h>
#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/cpufreq.h>

#include <linux/cyttspio.h>
#include <linux/miscdevice.h>

#include "cyttsp.h"

static struct cyttsp_platform_data cyttsp_i2c_platform_data = {
	.mt_sync = 0,
	.maxx = 758,
	.maxy = 1024,
	.flags = 0,
	.gen = CY_GEN3,
	.use_st = 0,
	.use_mt = 1,
	.use_trk_id = 1,
	.use_hndshk = 1,
	.use_timer = 0,
	.use_sleep = 1,
	.use_gestures = 0,
	.use_load_file = 1,
	.use_force_fw_update = 1,
	.use_virtual_keys = 0,
	.use_lpm = 0,
	/* activate up to 4 groups
	 * and set active distance
	 */
	.gest_set = 0,
	/* change act_intrvl to customize the Active power state
	 * scanning/processing refresh interval for Operating mode
	 */
	.act_intrvl = CY_ACT_INTRVL_DFLT,
	/* change tch_tmout to customize the touch timeout for the
	 * Active power state for Operating mode
	 */
	.tch_tmout = CY_TCH_TMOUT_DFLT,
	/* change lp_intrvl to customize the Low Power power state
	 * scanning/processing refresh interval for Operating mode
	 */
	.lp_intrvl = CY_LP_INTRVL_DFLT,
	.name = CY_DRIVER_NAME,
};

static irqreturn_t cyttsp_bl_ready_irq(int irq, void *handle);
static int cyttsp_soft_reset(struct cyttsp *ts, bool *status);
static int cyttsp_set_operational_mode(struct cyttsp *ts);
static int cyttsp_exit_bl_mode(struct cyttsp *ts);

static void send_user_event(struct cyttsp *ts, u8 prev, u8 cur)
{
  // No more touches. Tell user-space. 
  if (cur == 0 && prev > 0)
  {
    char *envp[] = {"Touch=notouch", NULL};
    kobject_uevent_env(&ts->input->dev.kobj, KOBJ_CHANGE, envp);
  }
  else if (cur > 0 && prev == 0)
  {
    char *envp[] = {"Touch=touch", NULL};
    kobject_uevent_env(&ts->input->dev.kobj, KOBJ_CHANGE, envp); 
  }
#ifdef CONFIG_CPU_FREQ_OVERRIDE_LAB126
  if (cur > 0)
    cpufreq_override(1);
#endif
}

/* ************************************************************************
   *********************  CYTTSP - I2C CLIENT I/F *************************
  *************************************************************************/
struct cyttsp_i2c {
    struct cyttsp_bus_ops ops;
    struct i2c_client *client;
    void *ttsp_client;
};


static int i2c_probe_success = 0;
static atomic_t touch_locked;
static struct mutex proc_mutex;
extern void gpio_touchcntrl_request_irq(int enable);
extern int gpio_touchcntrl_irq(void);
extern void gpio_cyttsp_wake_signal(void);
extern int gpio_cyttsp_init_pins(void);
extern int gpio_cyttsp_hw_reset(void);
extern int gpio_touchcntrl_irq_get_value(void);
static struct cyttsp *cyttsp_tsc = NULL;
static struct cyttsp_i2c *cyttsp_i2c_clientst = NULL;
static struct proc_dir_entry *proc_entry = NULL;

static s32 ttsp_i2c_read_block_data(void *handle, u8 addr,
    u8 length, void *values)
{
	struct cyttsp_i2c *ts = container_of(handle, struct cyttsp_i2c, ops);
	struct i2c_msg xfer[2];
	int ret;
	
	xfer[0].addr = ts->client->addr;
	xfer[0].flags = I2C_M_TEN;
	xfer[0].len = 1;
	xfer[0].buf = &addr;

	xfer[1].addr = ts->client->addr;
	xfer[1].flags = I2C_M_TEN | I2C_M_RD;
	xfer[1].len = length;
	xfer[1].buf = values;

	ret = i2c_transfer(ts->client->adapter, xfer, 2);
	return (ret < 0) ?  ret : 0;	
}

static s32 ttsp_i2c_write_block_data(void *handle, u8 addr,
    u8 length, const void *values)
{
	u8 data[I2C_SMBUS_BLOCK_MAX+1];
	int num_bytes, count;
	int retval;
	struct cyttsp_i2c *ts = container_of(handle, struct cyttsp_i2c, ops);
	num_bytes = length;
	data[0] = addr;
	count = (num_bytes > I2C_SMBUS_BLOCK_MAX) ?
		I2C_SMBUS_BLOCK_MAX : num_bytes;
	memcpy(&data[1], values, count+1);
	num_bytes -= count;
	retval = i2c_master_send(ts->client, data, count+1);
	if (retval < 0)
		return retval;
	while (num_bytes > 0) {
		count = (num_bytes > I2C_SMBUS_BLOCK_MAX) ?
			I2C_SMBUS_BLOCK_MAX : num_bytes;
		memcpy(&data[0], values, count);
		num_bytes -= count;
		retval = i2c_master_send(ts->client, data, count);
		if (retval < 0)
			return retval;
	}
	
	return 0;
}

static s32 ttsp_i2c_tch_ext(void *handle, void *values)
{
	int retval = 0;
	struct cyttsp_i2c *ts = container_of(handle, struct cyttsp_i2c, ops);
	
	DBG(printk(KERN_INFO "%s: Enter\n", __func__);)
	
	/* Add custom touch extension handling code here */
	/* set: retval < 0 for any returned system errors,
	    retval = 0 if normal touch handling is required,
	    retval > 0 if normal touch handling is *not* required */
	if (!ts || !values)
		retval = -EIO;
	
	return retval;
}

static int __devinit cyttsp_i2c_probe(struct i2c_client *client,
    const struct i2c_device_id *id)
{
	struct cyttsp_i2c *ts;
	int retval;
	
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	
	/* allocate and clear memory */
	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		printk(KERN_ERR "%s: Error, kzalloc.\n", __func__);
		retval = -ENOMEM;
		goto error_alloc_data_failed;
	}
	
	/* register driver_data */
	ts->client = client;
	i2c_set_clientdata(client, ts);
	ts->ops.write = ttsp_i2c_write_block_data;
	ts->ops.read = ttsp_i2c_read_block_data;
	ts->ops.ext = ttsp_i2c_tch_ext;
	ts->client->addr = CYTTSP_I2C_ADDRESS;
	cyttsp_i2c_clientst = ts;		
	
	DBG(printk(KERN_INFO "%s: Registration complete %s\n",
	    __func__, CY_DRIVER_NAME);)
	i2c_probe_success = 1;		
	return 0;

error_alloc_data_failed:
	return retval;
}

static int __devexit cyttsp_i2c_remove(struct i2c_client *client)
{
	struct cyttsp_i2c *ts;
	
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	ts = i2c_get_clientdata(client);
	kfree(ts);
	return 0;
}

static const struct i2c_device_id cyttsp_i2c_id[] = {
	{ CY_DRIVER_NAME, 0 },  { }
};

static struct i2c_driver cyttsp_i2c_driver = {
	.driver = {
		.name = CY_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = cyttsp_i2c_probe,
	.remove = __devexit_p(cyttsp_i2c_remove),
	.id_table = cyttsp_i2c_id,
};


/* ************************************************************************
   ************************  CYTTSP - CORE I/F ****************************
  *************************************************************************/
static void cyttsp_init_tch(struct cyttsp *ts)
{
	/* init the touch structures */
	ts->num_prv_st_tch = CY_NTCH;
	memset(ts->act_trk, CY_NTCH, sizeof(ts->act_trk));
	memset(ts->prv_mt_pos, CY_NTCH, sizeof(ts->prv_mt_pos));
	memset(ts->prv_mt_tch, CY_IGNR_TCH, sizeof(ts->prv_mt_tch));
	memset(ts->prv_st_tch, CY_IGNR_TCH, sizeof(ts->prv_st_tch));
}

static int ttsp_read_block_data(struct cyttsp *ts, u8 command,
	u8 length, void *buf)
{
	int rc;
	int tries;

	if (!buf || !length) {
		printk(KERN_ERR "%s: Error, buf:%s len:%u\n",
				__func__, !buf ? "NULL" : "OK", length);
		return -EIO;
	}
	
	for (tries = 0, rc = -1; tries < CY_NUM_RETRY && (rc < 0); tries++) {
		rc = ts->bus_ops->read(ts->bus_ops, command, length, buf);
		if (rc) {
			msleep(CY_DELAY_DFLT);
			DBG(print_data_block(__func__, command, length, buf, tries);)
		}
	}

	if (rc < 0)
		printk(KERN_ERR "%s: error %d\n", __func__, rc);
	DBG(print_data_block(__func__, command, length, buf, tries);)
	return rc;
}

static int ttsp_write_block_data(struct cyttsp *ts, u8 command,
	u8 length, void *buf)
{
	int rc;
	int tries;

	if (!buf || !length) {
		printk(KERN_ERR "%s: Error, buf:%s len:%u\n",
				__func__, !buf ? "NULL" : "OK", length);
		return -EIO;
	}

	for (tries = 0, rc = -1; tries < CY_NUM_RETRY && (rc < 0); tries++) {
		rc = ts->bus_ops->write(ts->bus_ops, command, length, buf);
		if (rc) {
			msleep(CY_DELAY_DFLT);
			DBG(print_data_block(__func__, command, length, buf, tries);)
		}
	}

	if (rc < 0) {
		print_data_block(__func__, command, length, buf, tries);
		//printk(KERN_ERR "%s: error %d\n", __func__, rc);
	}
	DBG(print_data_block(__func__, command, length, buf, tries);)

	return rc;
}

static int ttsp_tch_ext(struct cyttsp *ts, void *buf)
{
	int rc;
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	if (!buf) {
		printk(KERN_ERR "%s: Error, buf:%s\n",
				__func__, !buf ? "NULL" : "OK");
		return -EIO;
	}
	rc = ts->bus_ops->ext(ts->bus_ops, buf);
	if (rc < 0)
		printk(KERN_ERR "%s: error %d\n", __func__, rc);
	return rc;
}

static int cyttsp_inlist(u16 prev_track[], u8 cur_trk_id, u8 *prev_loc,
	u8 num_touches)
{
	u8 id = 0;

	DBG(printk(KERN_INFO"%s: IN p[%d]=%d c=%d n=%d loc=%d\n",
		__func__, id, prev_track[id], cur_trk_id,
		num_touches, *prev_loc);)

	for (*prev_loc = CY_IGNR_TCH; id < num_touches; id++) {
		DBG(printk(KERN_INFO"%s: p[%d]=%d c=%d n=%d loc=%d\n",
			__func__, id, prev_track[id], cur_trk_id,
				num_touches, *prev_loc);)
		if (prev_track[id] == cur_trk_id) {
			*prev_loc = id;
			break;
		}
	}
	DBG(printk(KERN_INFO"%s: OUT p[%d]=%d c=%d n=%d loc=%d\n", __func__,
		id, prev_track[id], cur_trk_id, num_touches, *prev_loc);)

	return *prev_loc < CY_NUM_TRK_ID;
}

static int cyttsp_next_avail_inlist(u16 cur_trk[], u8 *new_loc,
	u8 num_touches)
{
	u8 id = 0;
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	for (*new_loc = CY_IGNR_TCH; id < num_touches; id++) {
		if (cur_trk[id] > CY_NUM_TRK_ID) {
			*new_loc = id;
			break;
		}
	}
	return *new_loc < CY_NUM_TRK_ID;
}

/* Timer function used as dummy interrupt driver */
static void cyttsp_timer(unsigned long handle)
{
	struct cyttsp *ts = (struct cyttsp *)handle;

	DBG(printk(KERN_INFO"%s: TTSP timer event!\n", __func__);)
	/* schedule motion signal handling */
	if (!work_pending(&ts->work))
		schedule_work(&ts->work);
	mod_timer(&ts->timer, jiffies + TOUCHSCREEN_TIMEOUT);
	return;
}

static int _cyttsp_hndshk(struct cyttsp *ts)
{
	int retval = -1;
	u8 hst_mode;
	u8 cmd;
	u8 tries = 0;
	
	printk(KERN_ERR "_cyttsp_hndshk\n");
	
	while (retval < 0 && tries++ < 20){
		msleep(5);
		retval = ttsp_read_block_data(ts, REG_HST_MODE,
			sizeof(u8), &hst_mode);
		if(retval < 0) {	
			pr_err("%s: bus read fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
			continue;
		}
		
		cmd = hst_mode & CY_HNDSHK_BIT ?
		hst_mode & ~CY_HNDSHK_BIT :
		hst_mode | CY_HNDSHK_BIT;

		retval = ttsp_write_block_data(ts, CY_REG_BASE,
			sizeof(cmd), (u8 *)&cmd);		
		if(retval < 0)
			pr_err("%s: bus write fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
	}
	
	return retval;
}

static int _cyttsp_hndshk_n_write(struct cyttsp *ts, u8 write_back)
{
	int retval = -1;
	u8 hst_mode;
	u8 cmd;
	u8 tries = 0;
	
	 while (retval < 0 && tries++ < 20){
		msleep(5);
		retval = ttsp_read_block_data(ts, REG_HST_MODE,
			sizeof(u8), &hst_mode);
		if(retval < 0) {	
			pr_err("%s: bus read fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
			continue;
		}
		
		cmd = hst_mode & CY_HNDSHK_BIT ?
		write_back & ~CY_HNDSHK_BIT :
		write_back | CY_HNDSHK_BIT;
		
		retval = ttsp_write_block_data(ts, CY_REG_BASE,
			sizeof(cmd), (u8 *)&cmd);		
		if(retval < 0)
			pr_err("%s: bus write fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
	}
	
	return retval;
}

/* ************************************************************************
 * ISR function. This function is general, initialized in drivers init
 * function and disables further IRQs until this IRQ is processed in worker.
 * *************************************************************************/
static irqreturn_t cyttsp_irq(int irq, void *handle)
{
	struct cyttsp *ts = (struct cyttsp *)handle;

	printk(KERN_INFO"%s: Got IRQ!\n", __func__);
	DBG(printk(KERN_INFO"%s: Got IRQ!\n", __func__);)
	if (!work_pending(&ts->work) && !atomic_read(&touch_locked))
		schedule_work(&ts->work);
	return IRQ_HANDLED;
}

int slots[CY_NUM_TRK_ID];

static int find_next_slot(void)
{
  int i;
  for (i = 0; i < CY_NUM_TRK_ID; i++)
    if (slots[i] == -1)
      return i;
  return -1;
}
 
static int find_slot_by_id(int id)
{
  int i;
  for (i = 0; i < CY_NUM_TRK_ID; i++)
    if (slots[i] == id)
      return i;
  return -1;
}

static void clear_slots(struct cyttsp *ts)
{
	int i;
	void (*mt_sync_func)(struct input_dev *) = ts->platform_data->mt_sync;

	if (!ts->input)
		return;

	for (i = 0; i < CY_NUM_TRK_ID; i++)
	{
		if (slots[i] != -1)
		{
			input_report_abs(ts->input, ABS_MT_SLOT, i);
			input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
			slots[i] = -1;
			if (mt_sync_func)
				mt_sync_func(ts->input);
		}
	}
	input_sync(ts->input);
	cyttsp_init_tch(ts);
}

static void handle_multi_touch(struct cyttsp_track_data *t, struct cyttsp *ts)
{

	u8 id;
	void (*mt_sync_func)(struct input_dev *) = ts->platform_data->mt_sync;
	u8 contacts_left = 0;
	int slot;

	/* terminate any previous touch where the track
	 * is missing from the current event */
	for (id = 0; id < CY_NUM_TRK_ID; id++) {
		if ((t->cur_trk[id] != CY_NTCH) || (ts->act_trk[id] == CY_NTCH) )
			continue;

		slot = find_slot_by_id(id);
		slots[slot] = -1;
		
		input_report_abs(ts->input, ABS_MT_SLOT, slot);						
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
		if (mt_sync_func)
			mt_sync_func(ts->input);
		
		ts->act_trk[id] = CY_NTCH;
		ts->prv_mt_pos[id][CY_XPOS] = 0;
		ts->prv_mt_pos[id][CY_YPOS] = 0;
	}
	/* set Multi-Touch current event signals */
	for (id = 0; id < CY_NUM_MT_TCH_ID; id++) {
		if (t->cur_mt_tch[id] >= CY_NUM_TRK_ID)
			continue;
		
		if ((slot = find_slot_by_id(t->cur_mt_tch[id])) < 0)
		{
			slot = find_next_slot();
			slots[slot] = t->cur_mt_tch[id];				
		}
		
		input_report_abs(ts->input, ABS_MT_SLOT, slot);
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, slot);

		input_report_abs(ts->input, ABS_MT_POSITION_X,
						t->cur_mt_pos[id][CY_XPOS]);
		input_report_abs(ts->input, ABS_MT_POSITION_Y,
						t->cur_mt_pos[id][CY_YPOS]);
		if (mt_sync_func)
			mt_sync_func(ts->input);

		ts->act_trk[id] = CY_TCH;
		ts->prv_mt_pos[id][CY_XPOS] = t->cur_mt_pos[id][CY_XPOS];
		ts->prv_mt_pos[id][CY_YPOS] = t->cur_mt_pos[id][CY_YPOS];
		contacts_left++;
	}

	send_user_event(ts, ts->num_prv_st_tch, contacts_left);
	ts->num_prv_st_tch = contacts_left;
	
	input_report_key(ts->input, BTN_TOUCH, contacts_left > 0);
	input_report_key(ts->input, BTN_TOOL_FINGER, contacts_left >= 1);
	input_report_key(ts->input, BTN_TOOL_DOUBLETAP, contacts_left >= 2);
	input_report_key(ts->input, BTN_TOOL_TRIPLETAP, contacts_left == 3);
	
	return;
}

static void cyttsp_xy_worker(struct work_struct *work)
{
	struct cyttsp *ts = container_of(work, struct cyttsp, work);
	struct cyttsp_xydata xy_data;
	u8 id;
	struct cyttsp_track_data trc;
	s32 retval;
	
	DBG(printk(KERN_INFO "%s: Enter\n", __func__);)
	/* get event data from CYTTSP device */
	retval = ttsp_read_block_data(ts, CY_REG_BASE,
				      sizeof(xy_data), &xy_data);

	if (retval < 0) {
		printk(KERN_ERR "%s: Error, fail to read device on host interface bus\n",
			__func__);
		goto exit_xy_worker;
	}

	/* provide flow control handshake */
	if (ts->irq > 0) {
		if (ts->platform_data->use_hndshk) {
			u8 cmd;
			cmd = xy_data.hst_mode & CY_HNDSHK_BIT ?
				xy_data.hst_mode & ~CY_HNDSHK_BIT :
				xy_data.hst_mode | CY_HNDSHK_BIT;
			retval = ttsp_write_block_data(ts, CY_REG_BASE,
						       sizeof(cmd), (u8 *)&cmd);
		}
	}
	trc.cur_tch = GET_NUM_TOUCHES(xy_data.tt_stat);

	if (ts->platform_data->power_state == CY_IDLE_STATE)
		goto exit_xy_worker;
	else if (GET_BOOTLOADERMODE(xy_data.tt_mode)) {
		/* TTSP device has reset back to bootloader mode */
		/* reset driver touch history */
		bool timeout;
		DBG3(printk(KERN_INFO
			"%s: Bootloader detected; reset driver\n",
			__func__);)
		cyttsp_init_tch(ts);
		if(ts->irq > 0) {
			free_irq(ts->irq, ts);
			ts->irq = -1;
		}
		cyttsp_soft_reset(ts, &timeout);
		cyttsp_exit_bl_mode(ts);
		cyttsp_set_operational_mode(ts);
		goto exit_xy_worker;
	} else if (IS_LARGE_AREA(xy_data.tt_stat) == 1) {
		/* terminate all active tracks */
		trc.cur_tch = CY_NTCH;
		DBG(printk(KERN_INFO "%s: Large area detected\n",
			__func__);)
	} else if (trc.cur_tch > CY_NUM_MT_TCH_ID) {
		/* terminate all active tracks */
		trc.cur_tch = CY_NTCH;
		DBG(printk(KERN_INFO "%s: Num touch error detected\n",
			__func__);)
	} else if (IS_BAD_PKT(xy_data.tt_mode)) {
		/* terminate all active tracks */
		trc.cur_tch = CY_NTCH;
		DBG(printk(KERN_INFO "%s: Invalid buffer detected\n",
			__func__);)
	}

	/* clear current active track ID array and count previous touches */
	for (id = 0, trc.prv_tch = CY_NTCH; id < CY_NUM_TRK_ID; id++) {
		trc.cur_trk[id] = CY_NTCH;
		trc.prv_tch += ts->act_trk[id];
	}

	/* send no events if there were no previous touches */
	/* and no new touches */
	if ((trc.prv_tch == CY_NTCH) && ((trc.cur_tch == CY_NTCH) ||
				(trc.cur_tch > CY_NUM_MT_TCH_ID)))
		goto exit_xy_worker;

	DBG(printk(KERN_INFO "%s: prev=%d  curr=%d\n", __func__,
		   trc.prv_tch, trc.cur_tch);)

	/* clear current single-touch array */
	memset(trc.cur_st_tch, CY_IGNR_TCH, sizeof(trc.cur_st_tch));

	/* clear single touch positions */
	trc.st_x1 = trc.st_y1 = trc.st_z1 =
			trc.st_x2 = trc.st_y2 = trc.st_z2 = CY_NTCH;

	/* clear current multi-touch arrays */
	memset(trc.cur_mt_tch, CY_IGNR_TCH, sizeof(trc.cur_mt_tch));
	memset(trc.cur_mt_pos, CY_NTCH, sizeof(trc.cur_mt_pos));
	memset(trc.cur_mt_z, CY_NTCH, sizeof(trc.cur_mt_z));

	DBG(
	if (trc.cur_tch) {
		unsigned i;
		u8 *pdata = (u8 *)&xy_data;

		printk(KERN_INFO "%s: TTSP data_pack: ", __func__);
		for (i = 0; i < sizeof(struct cyttsp_xydata); i++)
			printk(KERN_INFO "[%d]=0x%x ", i, pdata[i]);
		printk(KERN_INFO "\n");
	})

	/* process the touches */
	switch (trc.cur_tch) {
		/* do not break */
	case 2:
		xy_data.x2 = be16_to_cpu(xy_data.x2);
		xy_data.y2 = be16_to_cpu(xy_data.y2);
		id = GET_TOUCH2_ID(xy_data.touch12_id);
		if (ts->platform_data->use_trk_id) {
			trc.cur_mt_pos[CY_MT_TCH2_IDX][CY_XPOS] = xy_data.x2;
			trc.cur_mt_pos[CY_MT_TCH2_IDX][CY_YPOS] = xy_data.y2;
			trc.cur_mt_z[CY_MT_TCH2_IDX] = xy_data.z2;
		}
		trc.cur_mt_tch[CY_MT_TCH2_IDX] = id;
		trc.cur_trk[id] = CY_TCH;
		if (ts->prv_st_tch[CY_ST_FNGR1_IDX] <	CY_NUM_TRK_ID) {
			if (ts->prv_st_tch[CY_ST_FNGR1_IDX] == id) {
				trc.st_x1 = xy_data.x2;
				trc.st_y1 = xy_data.y2;
				trc.st_z1 = xy_data.z2;
				trc.cur_st_tch[CY_ST_FNGR1_IDX] = id;
			} else if (ts->prv_st_tch[CY_ST_FNGR2_IDX] == id) {
				trc.st_x2 = xy_data.x2;
				trc.st_y2 = xy_data.y2;
				trc.st_z2 = xy_data.z2;
				trc.cur_st_tch[CY_ST_FNGR2_IDX] = id;
			}
		}
		DBG(printk(KERN_INFO"%s: 2nd XYZ:% 3d,% 3d,% 3d  ID:% 2d\n",
				__func__, xy_data.x2, xy_data.y2, xy_data.z2,
				(xy_data.touch12_id & 0x0F));)

		/* do not break */
	case 1:
		xy_data.x1 = be16_to_cpu(xy_data.x1);
		xy_data.y1 = be16_to_cpu(xy_data.y1);
		id = GET_TOUCH1_ID(xy_data.touch12_id);
		if (ts->platform_data->use_trk_id) {
			trc.cur_mt_pos[CY_MT_TCH1_IDX][CY_XPOS] = xy_data.x1;
			trc.cur_mt_pos[CY_MT_TCH1_IDX][CY_YPOS] = xy_data.y1;
			trc.cur_mt_z[CY_MT_TCH1_IDX] = xy_data.z1;
		}
		trc.cur_mt_tch[CY_MT_TCH1_IDX] = id;
		trc.cur_trk[id] = CY_TCH;
		if (ts->prv_st_tch[CY_ST_FNGR1_IDX] <	CY_NUM_TRK_ID) {
			if (ts->prv_st_tch[CY_ST_FNGR1_IDX] == id) {
				trc.st_x1 = xy_data.x1;
				trc.st_y1 = xy_data.y1;
				trc.st_z1 = xy_data.z1;
				trc.cur_st_tch[CY_ST_FNGR1_IDX] = id;
			} else if (ts->prv_st_tch[CY_ST_FNGR2_IDX] == id) {
				trc.st_x2 = xy_data.x1;
				trc.st_y2 = xy_data.y1;
				trc.st_z2 = xy_data.z1;
				trc.cur_st_tch[CY_ST_FNGR2_IDX] = id;
			}
		}
		DBG(printk(KERN_INFO"%s: S1st XYZ:% 3d,% 3d,% 3d  ID:% 2d\n",
				__func__, xy_data.x1, xy_data.y1, xy_data.z1,
				((xy_data.touch12_id >> 4) & 0x0F));)

		break;
	case 0:
	default:
		break;
	}
	
	DBG(printk(KERN_ERR "x1 %d y1 %d x2 %d y2 %d ct %d", 
		xy_data.x1, xy_data.y1, xy_data.x2, xy_data.y2, trc.cur_tch);)
	
	handle_multi_touch(&trc, ts);

	/* signal the view motion event */
	input_sync(ts->input);

	/* update platform data for the current multi-touch information */
	for (id = 0; id < CY_NUM_TRK_ID; id++)
		ts->act_trk[id] = trc.cur_trk[id];
		
exit_xy_worker:
	DBG(printk(KERN_INFO"%s: finished.\n", __func__);)
	return;
}

/* ************************************************************************
 * Probe initialization functions
 * ************************************************************************ */

/* Timeout timer */
static void cyttsp_to_timer(unsigned long handle)
{
	struct cyttsp *ts = (struct cyttsp *)handle;

	DBG(printk(KERN_INFO"%s: TTSP timeout timer event!\n", __func__);)
	ts->to_timeout = true;
	return;
}

static void cyttsp_setup_to_timer(struct cyttsp *ts)
{
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	setup_timer(&ts->to_timer, cyttsp_to_timer, (unsigned long) ts);
}

static void cyttsp_kill_to_timer(struct cyttsp *ts)
{
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	del_timer(&ts->to_timer);
}

static void cyttsp_start_to_timer(struct cyttsp *ts, int ms)
{
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	ts->to_timeout = false;
	mod_timer(&ts->to_timer, jiffies + ms);
}

static bool cyttsp_timeout(struct cyttsp *ts)
{
	return ts->to_timeout;
}

static irqreturn_t cyttsp_bl_ready_irq(int irq, void *handle)
{
	struct cyttsp *ts = (struct cyttsp *)handle;

	DBG2(printk(KERN_INFO"%s: Got BL IRQ!\n", __func__);)
	ts->bl_ready = true;
	return IRQ_HANDLED;
}

static void cyttsp_set_bl_ready(struct cyttsp *ts, bool set)
{
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	ts->bl_ready = set;
	DBG(printk(KERN_INFO"%s: bl_ready=%d\n", __func__, (int)ts->bl_ready);)
}

static bool cyttsp_check_bl_ready(struct cyttsp *ts)
{
	return ts->bl_ready;
}

static int cyttsp_load_bl_regs(struct cyttsp *ts)
{
	int retval;

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	retval =  ttsp_read_block_data(ts, CY_REG_BASE,
				sizeof(ts->bl_data), &(ts->bl_data));

	if (retval < 0) {
		DBG2(printk(KERN_INFO "%s: Failed reading block data, err:%d\n",
			__func__, retval);)
		goto fail;
	}

	DBG({
	      int i;
	      u8 *bl_data = (u8 *)&(ts->bl_data);
	      for (i = 0; i < sizeof(struct cyttsp_bootloader_data); i++)
			printk(KERN_INFO "%s bl_data[%d]=0x%x\n",
				__func__, i, bl_data[i]);
	})
	DBG2(printk(KERN_INFO "%s: bl f=%02X s=%02X e=%02X\n",
		__func__,
		ts->bl_data.bl_file,
		ts->bl_data.bl_status,
		ts->bl_data.bl_error);)

	return 0;
fail:
	return retval;
}

static bool cyttsp_bl_app_valid(struct cyttsp *ts)
{
	int retval;

	ts->bl_data.bl_status = 0x00;

	retval = cyttsp_load_bl_regs(ts);

	if (retval < 0)
		return false;

	if (ts->bl_data.bl_status == 0x11) {
		printk(KERN_INFO "%s: App found; normal boot\n", __func__);
		return true;
	} else if (ts->bl_data.bl_status == 0x10) {
		printk(KERN_INFO "%s: NO APP; load firmware!!\n", __func__);
		return false;
	} else {
		if (ts->bl_data.bl_file == CY_OPERATE_MODE) {
			int invalid_op_mode_status;
			invalid_op_mode_status = ts->bl_data.bl_status & 0x3f;
			if (invalid_op_mode_status)
				return false;
			else {
				if (ts->platform_data->power_state ==
					CY_ACTIVE_STATE)
					printk(KERN_INFO "%s: Operational\n",
						__func__);
				else
					printk(KERN_INFO "%s: No bootloader\n",
						__func__);
			}
		}
		return true;
	}
}

static bool cyttsp_bl_status(struct cyttsp *ts)
{
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	return ((ts->bl_data.bl_status == 0x10) ||
		(ts->bl_data.bl_status == 0x11));
}

static bool cyttsp_bl_err_status(struct cyttsp *ts)
{
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	return (((ts->bl_data.bl_status == 0x10) &&
		(ts->bl_data.bl_error == 0x20)) ||
		((ts->bl_data.bl_status == 0x11) &&
		(ts->bl_data.bl_error == 0x20)));
}

static bool cyttsp_wait_bl_ready(struct cyttsp *ts,
	int pre_delay, int loop_delay, int max_try,
	bool (*done)(struct cyttsp *ts))
{
	int tries;
	bool rdy = false, tmo = false;

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	DBG(printk(KERN_INFO"%s: pre-dly=%d loop-dly=%d, max-try=%d\n",
		__func__, pre_delay, loop_delay, max_try);)

	tries = 0;
	ts->bl_data.bl_file = 0;
	ts->bl_data.bl_status = 0;
	ts->bl_data.bl_error = 0;
	
	cyttsp_start_to_timer(ts, abs(loop_delay) * max_try);
	while (!rdy && !tmo) {
		rdy = cyttsp_check_bl_ready(ts);
		tmo = cyttsp_timeout(ts);
		if (loop_delay < 0)
			udelay(abs(loop_delay));
		else
			msleep(abs(loop_delay));
		tries++;
	}
	DBG2(printk(KERN_INFO"%s: irq mode tries=%d rdy=%d tmo=%d\n",
		__func__, tries, (int)rdy, (int)tmo);)
	cyttsp_load_bl_regs(ts);
	

	if (tries >= max_try || tmo)
		return true;	/* timeout */
	else
		return false;
}

static int cyttsp_exit_bl_mode(struct cyttsp *ts)
{
	int retval;
	int tries = 0;

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	/* check if in bootloader mode;
	 * if in operational mode then just return without fail
	 */
	cyttsp_load_bl_regs(ts);
	if (!GET_BOOTLOADERMODE(ts->bl_data.bl_status))
		return 0;

	retval = ttsp_write_block_data(ts, CY_REG_BASE, sizeof(bl_cmd),
				       (void *)bl_cmd);
	if (retval < 0) {
		printk(KERN_ERR "%s: Failed writing block data, err:%d\n",
			__func__, retval);
		goto fail;
	}

	msleep(80);
	do {
		msleep(20);
		cyttsp_load_bl_regs(ts);
	} while (GET_BOOTLOADERMODE(ts->bl_data.bl_status) && tries++ < 10);

	DBG2(printk(KERN_INFO "%s: read tries=%d\n", __func__, tries);)

	DBG(printk(KERN_INFO"%s: Exit "
				"f=%02X s=%02X e=%02X\n",
				__func__,
				ts->bl_data.bl_file, ts->bl_data.bl_status,
				ts->bl_data.bl_error);)

	return 0;
fail:
	return retval;
}

/*
 * @cmd offset from mfg_cmd 0x2
 * assumed it's in sysinfo mode
 * */
static void cyttsp_mfg_cmd(struct cyttsp* ts, u8* mfgcmd, size_t len)
{
	u8 cmd;
	int ret, retry;
	u8 done = 0;
	
	_cyttsp_hndshk(ts);
	
	ret = ttsp_write_block_data(ts, REG_MFG_CMD, len, mfgcmd);
	if(ret < 0)
	{
		printk(KERN_ERR "cyttsp:recalibration:%s:cannot write to i2c %d", __func__, ret);
	}
	
	_cyttsp_hndshk(ts);
	
	/*
	 * Wait until MFG_STAT == 0x86 or 0x82, means calibration is completed successfully 
	 * */
	retry = 0;
	do {	
		ret = ttsp_read_block_data(ts, REG_MFG_STAT, sizeof(u8), &cmd);
		if(cmd == 0x86 || cmd == 0x82) {	
			printk(KERN_INFO "read STAT: 0x%x\n", cmd );
			done = 1;
			printk(KERN_INFO "DONE!\n");
			break;
		}
		msleep(100);
	} while(ret < 0 || retry++ < 100);
	
	_cyttsp_hndshk(ts);
}

static int cyttsp_set_sysinfo_mode(struct cyttsp *ts)
{
	int retval;
	int tries;
	u8 cmd = CY_SYSINFO_MODE;
	u8 sysinfo[1] = {0x3};

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	memset(&(ts->sysinfo_data), 0, sizeof(struct cyttsp_sysinfo_data));

	/* switch to sysinfo mode */
	retval = _cyttsp_hndshk_n_write(ts, cmd);
	if (retval < 0) {
		printk(KERN_ERR "%s: Failed writing block data, err:%d\n",
			__func__, retval);
		goto exit_sysinfo_mode;
	}

	_cyttsp_hndshk(ts);

	cyttsp_mfg_cmd(ts, sysinfo, sizeof(u8));
	
	/* read sysinfo registers */
	tries = 0;
	do {
		msleep(20);
		retval = ttsp_read_block_data(ts, CY_REG_BASE,
			sizeof(ts->sysinfo_data), &(ts->sysinfo_data));
	} while (!(retval < 0) &&
		(ts->sysinfo_data.tts_verh == 0) &&
		(ts->sysinfo_data.tts_verl == 0) &&
		(tries++ < (500/20)));
		
	
	
	printk(KERN_DEBUG "%s: hst_mode:0x%x, mfg_stat:0x%x, mfg_cmd:0x%x ", 
		__func__, ts->sysinfo_data.hst_mode, ts->sysinfo_data.mfg_stat, ts->sysinfo_data.mfg_cmd);

	if (retval < 0) {
		printk(KERN_ERR "%s: Failed reading block data, err:%d\n",
			__func__, retval);
		return retval;
	}
	
exit_sysinfo_mode:
	DBG(printk(KERN_INFO"%s:SI2: hst_mode=0x%02X mfg_cmd=0x%02X "
		"mfg_stat=0x%02X\n", __func__, ts->sysinfo_data.hst_mode,
		ts->sysinfo_data.mfg_cmd,
		ts->sysinfo_data.mfg_stat);)

	DBG(printk(KERN_INFO"%s:SI2: bl_ver=0x%02X%02X\n",
		__func__, ts->sysinfo_data.bl_verh, ts->sysinfo_data.bl_verl);)

	DBG(printk(KERN_INFO"%s:SI2: sysinfo act_intrvl=0x%02X "
		"tch_tmout=0x%02X lp_intrvl=0x%02X\n",
		__func__, ts->sysinfo_data.act_intrvl,
		ts->sysinfo_data.tch_tmout,
		ts->sysinfo_data.lp_intrvl);)

	printk(KERN_INFO"%s:SI2:tts_ver=0x%02X%02X app_id=0x%02X%02X "
		"app_ver=0x%02X%02X c_id=0x%02X%02X%02X\n", __func__,
		ts->sysinfo_data.tts_verh, ts->sysinfo_data.tts_verl,
		ts->sysinfo_data.app_idh, ts->sysinfo_data.app_idl,
		ts->sysinfo_data.app_verh, ts->sysinfo_data.app_verl,
		ts->sysinfo_data.cid[0], ts->sysinfo_data.cid[1],
		ts->sysinfo_data.cid[2]);

	ts->device_mode = CY_SI_MODE;
	
	return retval;
}

static int cyttsp_set_sysinfo_regs(struct cyttsp *ts)
{
	int retval = 0;

	ts->scn_typ = CY_SCN_TYP_SELF;
	retval = ttsp_write_block_data(ts,
				CY_REG_SCN_TYP,
				sizeof(u8), &(ts->scn_typ));
	if (retval < 0)
		return retval;

	if ((ts->platform_data->act_intrvl != CY_ACT_INTRVL_DFLT) ||
		(ts->platform_data->tch_tmout != CY_TCH_TMOUT_DFLT) ||
		(ts->platform_data->lp_intrvl != CY_LP_INTRVL_DFLT)) {

		u8 intrvl_ray[3];

		intrvl_ray[0] = ts->platform_data->act_intrvl;
		intrvl_ray[1] = ts->platform_data->tch_tmout;
		intrvl_ray[2] = ts->platform_data->lp_intrvl;

		/* set intrvl registers */
		retval = ttsp_write_block_data(ts,
				CY_REG_ACT_INTRVL,
				sizeof(intrvl_ray), intrvl_ray);

	}
	
	_cyttsp_hndshk(ts);
	msleep(CY_DELAY_SYSINFO);

	return retval;
}

static void cyttsp_disable_irq_cancel_work(struct cyttsp *ts)
{
	bool state = atomic_read(&touch_locked);
	atomic_set(&touch_locked, true);
	cancel_work_sync(&ts->work);
	if(ts->irq > 0)
	{
		free_irq(ts->irq, ts);
		ts->irq = -1;
	}
	atomic_set(&touch_locked, state);
}

static void cyttsp_disable_irq_cancel_work_lock(struct cyttsp *ts)
{
	atomic_set(&touch_locked, true);
	cyttsp_disable_irq_cancel_work(ts);
}

static int cyttsp_enable_irq(struct cyttsp *ts)
{
	int retval;
	if(ts->irq > 0)
	{
		free_irq(ts->irq, ts);
		ts->irq = -1;
	}
	
	_cyttsp_hndshk(ts);
	
	ts->irq = gpio_touchcntrl_irq();
	retval = request_irq(ts->irq, cyttsp_irq,
		IRQF_TRIGGER_FALLING | IRQF_DISABLED , ts->platform_data->name, ts);
	if(retval) {
		ts->irq = -1;
	} 
	return retval;
}

static int cyttsp_set_operational_mode(struct cyttsp *ts)
{
	int retval;
	u8 cmd = CY_OPERATE_MODE;
	u8 gest_default;
	struct cyttsp_xydata xy_data;

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	memset(&(xy_data), 0, sizeof(xy_data));
	
	if(ts->platform_data->use_lpm)
		cmd |= CY_LOW_POWER_MODE;
	
	retval = _cyttsp_hndshk_n_write(ts, cmd);
	if (retval < 0) {
		printk(KERN_ERR "%s: Failed writing block data, err:%d\n",
			__func__, retval);
		goto write_block_data_fail;
	}

	_cyttsp_hndshk(ts);
	
	/* wait for TTSP Device to complete switch to Operational mode */
	msleep(20);

	retval = ttsp_read_block_data(ts, CY_REG_BASE, sizeof(xy_data), &(xy_data));
	DBG2(printk(KERN_INFO "%s: read tries=%d\n", __func__, tries);)

	printk(KERN_DEBUG "%s: hstmode:0x%x tt_mode:0x%x tt_stat:0x%x ", __func__, xy_data.hst_mode, xy_data.tt_mode, xy_data.tt_stat);


	DBG2(printk(KERN_INFO
		"%s: Setting up Interrupt. Device name=%s\n",
		__func__, ts->platform_data->name);)
	
	retval = cyttsp_enable_irq(ts);
	if (retval < 0) {
		printk(KERN_ERR "%s: Error, could not request irq\n",
			__func__);
		goto write_block_data_fail;
	} else {
		DBG2(printk(KERN_INFO "%s: Interrupt=%d\n",
			__func__, ts->irq);)
	}
	
	ts->device_mode = CY_OP_MODE;
	atomic_set(&touch_locked, false);
	return 0;
write_block_data_fail:
	return retval;
}

static int cyttsp_set_test_mode(struct cyttsp *ts, u8 reg_hst_mode)
{
	int retval;
	struct cyttsp_test_mode_data tm_data;

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	
	retval = _cyttsp_hndshk_n_write(ts, reg_hst_mode);
	if (retval < 0) {
		printk(KERN_ERR "%s: Failed writing block data, err:%d\n",
			__func__, retval);
			return retval;
	}
	msleep(100);
	retval = _cyttsp_hndshk(ts);
	msleep(100);
	//get rid of stale data
	retval = ttsp_read_block_data(ts, CY_REG_BASE, 
			sizeof(struct cyttsp_test_mode_data), &tm_data);
	printk(KERN_INFO "%s hst_mode:0x%x, tt_mode:0x%x", 
			__func__, TO_U8(tm_data.reg_hst_mode), TO_U8(tm_data.reg_tt_mode));
	
	_cyttsp_hndshk(ts);
	ts->device_mode = CY_TE_MODE;
	
	return retval;
}

static int cyttsp_soft_reset(struct cyttsp *ts, bool *status)
{
	int retval;
	u8 cmd = CY_SOFT_RESET_MODE;

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	/* reset TTSP Device back to bootloader mode */
	DBG(printk(KERN_INFO
		"%s: Setting up BL Ready Interrupt. Device name=%s\n",
		__func__, ts->platform_data->name);)
	
	ts->irq = gpio_touchcntrl_irq();
	retval = request_irq(ts->irq, cyttsp_bl_ready_irq,
		IRQF_TRIGGER_FALLING, ts->platform_data->name, ts);
	if(retval < 0) {
		ts->irq = -1;
	}
	cyttsp_setup_to_timer(ts);
	
	retval = ttsp_write_block_data(ts, CY_REG_BASE, sizeof(cmd), &cmd);
	/* place this after write to reset;
	 * better to sacrifice one than fail
	 * on false early indication
	 */
	cyttsp_set_bl_ready(ts, false);
	/* wait for TTSP Device to complete reset back to bootloader */
	if (!retval)
		*status = cyttsp_wait_bl_ready(ts, 300, 10, 100,
			cyttsp_bl_status);

	cyttsp_kill_to_timer(ts);
	if(ts->irq > 0) {
		free_irq(ts->irq, ts);
		ts->irq = -1;
	}
	

	return retval;
}

static void clear_flags_command(void)
{
	u8 clear_flag_cmd[2] = { 0xE3, 0x2F};
	u8 stat = 0;
	int retry = 0;	
	
	printk(KERN_INFO "clear_flag_cmd enter ");
	
	_cyttsp_hndshk(cyttsp_tsc);
	
	if(cyttsp_tsc->device_mode != CY_SI_MODE)
		cyttsp_set_sysinfo_mode(cyttsp_tsc);
	
	ttsp_write_block_data(cyttsp_tsc, REG_MFG_STAT, sizeof(clear_flag_cmd), clear_flag_cmd);
	
	do{
		_cyttsp_hndshk(cyttsp_tsc);
		msleep(50);
		ttsp_read_block_data(cyttsp_tsc, REG_MFG_STAT,
			sizeof(u8), &stat);
			
		printk(KERN_INFO "clear_flag_cmd %d stat:0x%x\n", retry, stat);
	}while(((0xE3 & stat) != 0) && (retry++ < 100));
	
	_cyttsp_hndshk(cyttsp_tsc);

	printk(KERN_INFO "done! clear_flag_cmd %d stat:0x%x\n", retry, stat);
}

static void ttsp_recalibrate(struct i2c_client* client, int input)
{
	int ret = 0;
	u8 recal[3];
	u8 baseline[1] = {0x21};
	
	cyttsp_disable_irq_cancel_work_lock(cyttsp_tsc);
	
	if(-1 == input){
		memcpy(recal, OPEN_TEST_CALIBRATE, sizeof(recal));
	}else
	{
		memcpy(recal, NORMAL_CALIBRATE, sizeof(recal));
	}
	
	printk(KERN_INFO "\n\n.. start cyttsp recalibrate..\n\n");
	/*
	 * Change to SysInfo mode by writing Device Mode[2:0] of HST_MODE Reg = 001b 
	 * */
	if(cyttsp_tsc->device_mode != CY_SI_MODE) {
		ret = cyttsp_set_sysinfo_mode(cyttsp_tsc);
		if(ret < 0)
		{
			printk(KERN_ERR "could not set sysinfo mode");
		}		
	}
	
	clear_flags_command();
	
	cyttsp_mfg_cmd(cyttsp_tsc, recal, sizeof(recal));
	
	clear_flags_command();
	
	cyttsp_mfg_cmd(cyttsp_tsc, baseline, sizeof(u8));
	
	// change to operational mode: 
	if(input == 1) {
		ret = cyttsp_set_operational_mode(cyttsp_tsc);
		if (ret < 0) 
			printk(KERN_ERR "cyttsp: E calibrate::cannot set to operational mode");
		else	
			printk(KERN_INFO "cyttsp: I calibrate::touch is operational now");		
	}else {
		printk(KERN_INFO "cyttsp: I calibrate:mode=sysinfo:touch is NOT operational now");		
	}
	
	
	printk(KERN_INFO "\n\n.. end cyttsp recalibrate..\n\n");
}


/*
 * 	test mode, scan type, 
 * 	TT_MODE[7:6] new data counter (00, 10 or 01 11), TT_MODE[1:0] scan type
 * */
static int ttsp_test_mode_dump_reg(struct cyttsp_test_mode_pages *tm_pages)
{
	int retval = 0;	
	int i = 0;
	int retries;
	int inner_retry = 0;
	u8 baseline[1] = {0x21};
	u8 sysinfo[1] = {0x3};
	
	struct cyttsp_test_mode_data* tm_data = &(tm_pages->tm_data[0]);
	
	cyttsp_disable_irq_cancel_work_lock(cyttsp_tsc);
	
	clear_flags_command();
	cyttsp_mfg_cmd(cyttsp_tsc, sysinfo, sizeof(sysinfo));
	
	
	if(TO_U8(tm_data->reg_hst_mode) == CY_TEST_MODE_INT_RC_BASE) {
		printk(KERN_INFO "%s:scan_type=0x%x\n", __func__, cyttsp_tsc->scn_typ);
		ttsp_write_block_data(cyttsp_tsc,
					CY_REG_SCN_TYP,
					sizeof(u8), &(cyttsp_tsc->scn_typ));
		clear_flags_command();
		cyttsp_mfg_cmd(cyttsp_tsc, baseline, sizeof(baseline));
	}
	
	printk(KERN_INFO "which_test_mode 0x%x\n", TO_U8(tm_data->reg_hst_mode));
	retval = cyttsp_set_test_mode(cyttsp_tsc, TO_U8(tm_data->reg_hst_mode));
	if(retval < 0)
	{
		goto exit_test_mode_dump;
	}
	msleep(100);	

	/*
	 * new data counter TT_MODE[7:6] = 00, scan type TT_MODE[1:0] = 00 (mutual scan)
	 * */
	printk(KERN_ERR "tt_mode 0x%x", TO_U8(tm_data->reg_tt_mode));
	//page 0
	// tm_data->reg_tt_mode.new_data_ct;
	if(TO_U8(tm_data->reg_hst_mode) == CY_TEST_MODE_DIFF_COUNTS || 
		TO_U8(tm_data->reg_hst_mode) == CY_TEST_MODE_INT_RC_BASE)
		retries = 6;
	else
		retries = 1;
	
	for(i = 0; i < retries; i++){ /* read 6 times to get rid of stale data */
		
		retval = ttsp_read_block_data(cyttsp_tsc, CY_REG_BASE,
			sizeof(struct cyttsp_test_mode_data), &(tm_pages->tm_data[0]));
		if (retval < 0) {
			pr_err("%s: test mode access err (ret=%d) line %d \n",
				__func__, retval, __LINE__);
			continue;
		} else {
			
			/* provide flow control handshake */
			
			retval = _cyttsp_hndshk(cyttsp_tsc);
			msleep(100);	
		}
		do{	
			retval = ttsp_read_block_data(cyttsp_tsc, CY_REG_BASE,
			sizeof(struct cyttsp_test_mode_data), &(tm_pages->tm_data[1]));
			if (retval < 0) {
				pr_err("%s: test mode access err (ret=%d) line %d \n",
					__func__, retval, __LINE__);
				continue;
			}
			/* provide flow control handshake */
			retval = _cyttsp_hndshk(cyttsp_tsc);
		
			/* after handshake, delay 50ms*/
			msleep(100);	
		}while( ((0x40 & TO_U8(tm_pages->tm_data[1].reg_tt_mode)) == 
				(0x40 & TO_U8(tm_pages->tm_data[0].reg_tt_mode))) && inner_retry++ < 10 );
		
	}//end 6 times

	//retval = cyttsp_set_operational_mode(cyttsp_tsc);
	
exit_test_mode_dump:	
	return retval;
}

static int cyttsp_wakeup(void)
{
	gpio_cyttsp_wake_signal();
	return 0;
}

static int cyttsp_touch_ctrl(struct cyttsp *ts, bool enable)
{
	u8 sleep_mode = 0;
	int retval = 0;
	struct cyttsp_xydata xydata;
	u8 cmd;

	printk(KERN_DEBUG "%s %d", __func__, enable);
	if (enable) {
		if (ts->platform_data->use_sleep && 
					(ts->platform_data->power_state != CY_ACTIVE_STATE)) {
			if (ts->platform_data->wakeup) {
				retval = ts->platform_data->wakeup();
				if (retval < 0)
					printk(KERN_ERR "%s: Error, wakeup not hooked! \n", __func__);
			} else {
				printk(KERN_ERR "%s: Error, wakeup not implemented "
								"(check board file).\n", __func__);
				retval = -ENOSYS;
			}
			
			retval = ttsp_read_block_data(ts, CY_REG_BASE,
							sizeof(xydata), &xydata);
			if (!(retval < 0) && !GET_HSTMODE(xydata.hst_mode)) {
				ts->platform_data->power_state = CY_ACTIVE_STATE;
				if (ts->suspended) {	
					if(cyttsp_enable_irq(ts) == 0)
						ts->platform_data->power_state = CY_ACTIVE_STATE;
					ts->suspended = 0;
				}
			}
			
			if(ts->platform_data->use_lpm) {
				cmd = CY_OPERATE_MODE | CY_LOW_POWER; 
				retval = ttsp_write_block_data(ts, CY_REG_BASE,
							sizeof(cmd), &cmd);
			}
			
			enable_irq(ts->irq);
			printk(KERN_ERR "\nirq gpio value: %d*********\n", gpio_touchcntrl_irq_get_value());
			
			printk(KERN_ERR "haili: REG_BASE: 0x%x", *((u8*)&xydata));
		}
	} else {
		if (ts->platform_data->use_sleep && 
						(ts->platform_data->power_state == CY_ACTIVE_STATE)) {
			cyttsp_disable_irq_cancel_work(ts);  //use the no lock version since lock is set somewhere else
			sleep_mode = CY_DEEP_SLEEP_MODE;
			retval = ttsp_write_block_data(ts, 
								CY_REG_BASE, sizeof(sleep_mode), &sleep_mode);
			if (!(retval < 0)) {
				ts->platform_data->power_state = CY_SLEEP_STATE;
				if (!ts->suspended) {	
					ts->suspended = 1;					
				}
			}
			
			clear_slots(ts);
		}
	}
	

	
	DBG(printk(KERN_INFO"%s: CYTTSP Power state is %s\n", __func__,
		(ts->platform_data->power_state == CY_ACTIVE_STATE) ? "ACTIVE" :
		((ts->platform_data->power_state == CY_SLEEP_STATE) ? "SLEEP" : "LOW POWER"));)
	return retval;
}

/**** PROC ENTRY ****/
static int cyttsp_proc_read(char *page, char **start, off_t off,
    int count, int *eof, void *data)
{       
	int len;
	
	if (off > 0) {
		*eof = 1;
		return 0;
	}
	
	if (atomic_read(&touch_locked))
	        len = sprintf(page, "touch is locked\n");
	else
	        len = sprintf(page, "touch is unlocked\n");
	
	return len;
}

static ssize_t cyttsp_proc_write( struct file *filp, const char __user *buff,
    unsigned long len, void *data )
{
	char command[CY_PROC_CMD_LEN];
	int sts = 0;
	
	if (len > CY_PROC_CMD_LEN){
		DEBUG_ERR("cyttsp_proc_write:proc:command is too long!\n");
		return -ENOSPC;
	}
	
	if (copy_from_user(command, buff, len)) {
		DEBUG_ERR("cyttsp_proc_write:proc::cannot copy from user!\n");
		return -EFAULT;
	}
	//prevent lock/unlock command sending at the same time
	mutex_lock(&proc_mutex);
	if ( !strncmp(command, "unlock", 6) ) {
		printk("cyttsp: I cyttsp_proc_write::command=unlock:\n");
		if (atomic_read(&touch_locked)) {
			
			sts = cyttsp_touch_ctrl(cyttsp_tsc, true);
			if (sts < 0) {
				atomic_set(&touch_locked, true);
				DEBUG_ERR("cyttsp_proc_write:proc:command=%s:not succeed please retry\n", command);
				mutex_unlock(&proc_mutex);
				return -EBUSY;
			}
			
			atomic_set(&touch_locked, false);
		}
	} else if ( !strncmp(command, "lock", 4) ) {
		printk("cyttsp: I cyttsp_proc_write::command=lock:\n");
		if (!atomic_read(&touch_locked)) {
			atomic_set(&touch_locked, true);
			
			sts = cyttsp_touch_ctrl(cyttsp_tsc, false);
			if (sts < 0){
				atomic_set(&touch_locked, false);
				DEBUG_ERR("cyttsp_proc_write:proc:command=%s:not succeed please retry\n", command);
				mutex_unlock(&proc_mutex);
				return -EBUSY;
			}
		}
	} else {
	        DEBUG_ERR("cyttsp_proc_write:proc:command=%s:Unrecognized command\n", command);
	}
	mutex_unlock(&proc_mutex);
	return len;
}

/**** END PROC ENTRY ****/


#ifdef CY_INCLUDE_LOAD_RECS
#include "cyttsp_ldr.h"
#else
static int cyttsp_loader(struct cyttsp *ts)
{
	void *fptr = cyttsp_bl_err_status;	/* kill warning */

	if (ts) {
		DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
		DBG(printk(KERN_INFO"%s: Exit\n", __func__);)
	}

	if (!fptr)
		return -EIO;
	else
		return 0;
}
#endif

static int cyttsp_power_on(struct cyttsp *ts)
{
	int retval = 0;
	bool timeout;

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	ts->device_mode = CY_BL_MODE;
	ts->platform_data->power_state = CY_IDLE_STATE;
	
	/* check if the TTSP device has a bootloader installed */
	retval = cyttsp_soft_reset(ts, &timeout);
	DBG2(printk(KERN_INFO"%s: after softreset r=%d\n", __func__, retval);)
	if (retval < 0 || timeout)
		goto bypass;

	if (ts->platform_data->use_load_file)
		retval = cyttsp_loader(ts);

	if (!cyttsp_bl_app_valid(ts)) {
		retval = 1;
		goto bypass;
	}

	retval = cyttsp_exit_bl_mode(ts);
	DBG2(printk(KERN_INFO"%s: after exit bl r=%d\n", __func__, retval);)

	if (retval < 0)
		goto bypass;

	/* switch to System Information mode to read */
	/* versions and set interval registers */
	retval = cyttsp_set_sysinfo_mode(ts);
	if (retval < 0)
		goto bypass;

	retval = cyttsp_set_sysinfo_regs(ts);
	if (retval < 0)
		goto bypass;

	/* switch back to Operational mode */
	DBG2(printk(KERN_INFO"%s: switch back to operational mode\n",
		__func__);)
	retval = cyttsp_set_operational_mode(ts);
	if (retval < 0)
		goto bypass;  

bypass:
	if (!retval) {
		ts->platform_data->power_state = CY_ACTIVE_STATE;
		ts->suspended = 0;
	}

	printk(KERN_INFO"%s: Power state is %s\n",
		__func__, (ts->platform_data->power_state == CY_ACTIVE_STATE) ?
		"ACTIVE" : "IDLE");
	return retval;
}

static int cyttsp_resume(struct platform_device *pdev)
{
	struct cyttsp *ts = (struct cyttsp *) platform_get_drvdata(pdev);
	int retval = 0;
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	if (!atomic_read(&touch_locked)) {
		retval = cyttsp_touch_ctrl(ts, true);
		DBG(printk(KERN_INFO"%s: Touch Wake Up %s\n", __func__,
				(retval < 0) ? "FAIL" : "PASS");)
	} else {
		DBG(printk(KERN_INFO"%s: Touch not enabled \n", __func__);)
	}
	return retval; 
	
	/*should do nothing since the touch should only be enabled by proc entry unlock */
}

static int cyttsp_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct cyttsp *ts = (struct cyttsp *) platform_get_drvdata(pdev);
	int retval = 0;

	printk(KERN_DEBUG "%s: Enter\n", __func__);
	
	if (!atomic_read(&touch_locked)) {
		retval = cyttsp_touch_ctrl(ts, false);
		DBG(printk(KERN_INFO"%s: Touch Suspend %s\n", __func__,
				(retval < 0) ? "FAIL" : "PASS");)
	} 
	else {
		DBG(printk(KERN_INFO"%s: Touch not enabled \n", __func__);)
	}
	return retval;
}

#ifdef CONFIG_HAS_CYTTSP_EARLYSUSPEND
static void cyttsp_ts_early_suspend(struct early_suspend *h)
{
	struct cyttsp *ts = container_of(h, struct cyttsp, early_suspend);

	printk(KERN_DEBUG "%s: Enter\n", __func__);
	LOCK(ts->mutex);
	if (!ts->fw_loader_mode) {
		disable_irq_nosync(ts->irq);
		ts->suspended = 1;
		cancel_work_sync(&ts->work);
		cyttsp_suspend(ts);
	}
	UNLOCK(ts->mutex);
}

static void cyttsp_ts_late_resume(struct early_suspend *h)
{
	struct cyttsp *ts = container_of(h, struct cyttsp, early_suspend);

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	LOCK(ts->mutex);
	if (!ts->fw_loader_mode && ts->suspended) {
		ts->suspended = 0;
		if (cyttsp_resume(ts) < 0)
			printk(KERN_ERR "%s: Error, cyttsp_resume.\n",
				__func__);
		enable_irq(ts->irq);
	}
	UNLOCK(ts->mutex);
}
#endif

#ifdef CONFIG_HAS_CYTTSP_FWLOADER
static int cyttsp_wr_reg(struct cyttsp *ts, u8 reg_id, u8 reg_data)
{

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	return ttsp_write_block_data(ts,
		CY_REG_BASE + reg_id, sizeof(u8), &reg_data);
}

static int cyttsp_rd_reg(struct cyttsp *ts, u8 reg_id, u8 *reg_data)
{
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
	return ttsp_read_block_data(ts,
		CY_REG_BASE + reg_id, sizeof(u8), reg_data);
}

static ssize_t firmware_write(struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t pos, size_t size)
{
	unsigned short val;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct cyttsp *ts = dev_get_drvdata(dev );
	LOCK(ts->mutex);
	if (!ts->fw_loader_mode) {
		val = *(unsigned short *)buf;
		ts->reg_id = (val & 0xff00) >> 8;
		if (!(ts->reg_id & 0x80)) {
			/* write user specified operational register */
			cyttsp_wr_reg(ts, ts->reg_id, (u8)(val & 0xff));
			DBG2(printk(KERN_INFO "%s: write(r=0x%02X d=0x%02X)\n",
				__func__, ts->reg_id, (val & 0xff));)
		} else {
			/* save user specified operational read register */
			DBG2(printk(KERN_INFO "%s: read(r=0x%02X)\n",
				__func__, ts->reg_id);)
		}
	} else {
		int retval = 0;
		int tries = 0;
		DBG({
			char str[128];
			char *p = str;
			int i;
			for (i = 0; i < size; i++, p += 2)
				sprintf(p, "%02x", (unsigned char)buf[i]);
			printk(KERN_DEBUG "%s: size %d, pos %ld payload %s\n",
			       __func__, size, (long)pos, str);
		})
		do {
			retval = ttsp_write_block_data(ts,
				CY_REG_BASE, size, buf);
			if (retval < 0)
				msleep(500);
		} while ((retval < 0) && (tries++ < 10));
	}
	UNLOCK(ts->mutex);
	return size;
}

static ssize_t firmware_read(struct kobject *kobj,
	struct bin_attribute *ba,
	char *buf, loff_t pos, size_t size)
{
	int count = 0;
	u8 reg_data;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct cyttsp *ts = dev_get_drvdata(dev );

	DBG2(printk(KERN_INFO"%s: Enter (mode=%d)\n",
		__func__, ts->fw_loader_mode);)

	LOCK(ts->mutex);
	if (!ts->fw_loader_mode) {
		/* read user specified operational register */
		cyttsp_rd_reg(ts, ts->reg_id & ~0x80, &reg_data);
		*(unsigned short *)buf = reg_data << 8;
		count = sizeof(unsigned short);
		DBG2(printk(KERN_INFO "%s: read(d=0x%02X)\n",
			__func__, reg_data);)
	} else {
		int retval = 0;
		int tries = 0;

		do {
			retval = cyttsp_load_bl_regs(ts);
			if (retval < 0)
				msleep(500);
		} while ((retval < 0) && (tries++ < 10));

		if (retval < 0) {
			printk(KERN_ERR "%s: error reading status\n", __func__);
			count = 0;
		} else {
			*(unsigned short *)buf = ts->bl_data.bl_status << 8 |
				ts->bl_data.bl_error;
			count = sizeof(unsigned short);
		}

		DBG2(printk(KERN_INFO
			"%s:bl_f=0x%02X bl_s=0x%02X bl_e=0x%02X\n",
			__func__,
			ts->bl_data.bl_file,
			ts->bl_data.bl_status,
			ts->bl_data.bl_error);)
	}
	UNLOCK(ts->mutex);
	return count;
}

static struct bin_attribute cyttsp_firmware = {
	.attr = {
		.name = "firmware",
		.mode = 0644,
	},
	.size = 128,
	.read = firmware_read,
	.write = firmware_write,
};

static ssize_t attr_fwloader_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev );
	return sprintf(buf, "0x%02X%02X 0x%02X%02X 0x%02X%02X 0x%02X%02X%02X\n",
		ts->sysinfo_data.tts_verh, ts->sysinfo_data.tts_verl,
		ts->sysinfo_data.app_idh, ts->sysinfo_data.app_idl,
		ts->sysinfo_data.app_verh, ts->sysinfo_data.app_verl,
		ts->sysinfo_data.cid[0], ts->sysinfo_data.cid[1],
		ts->sysinfo_data.cid[2]);
}

static ssize_t attr_fwloader_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	char *p;
	int ret;
	bool timeout;
	struct cyttsp *ts =  dev_get_drvdata(dev );
	unsigned val = simple_strtoul(buf, &p, 10);

	ret = p - buf;
	if (*p && isspace(*p))
		ret++;
	printk(KERN_DEBUG "%s: %u\n", __func__, val);

	LOCK(ts->mutex)
	if (val == 3) {
		sysfs_remove_bin_file(&dev->kobj, &cyttsp_firmware);
		DBG2(printk(KERN_INFO "%s: FW loader closed for reg r/w\n",
			__func__);)
	} else if (val == 2) {
		if (sysfs_create_bin_file(&dev->kobj, &cyttsp_firmware))
			printk(KERN_ERR "%s: unable to create file\n",
				__func__);
		DBG2(printk(KERN_INFO "%s: FW loader opened for reg r/w\n",
			__func__);)
	} else if ((val == 1) && !ts->fw_loader_mode) {
		ts->fw_loader_mode = 1;
		if (ts->suspended) {
			cyttsp_touch_ctrl(cyttsp_tsc, true);
		} else {
			if (ts->platform_data->use_timer)
				del_timer(&ts->timer);
			else
				disable_irq_nosync(ts->irq);
			cancel_work_sync(&ts->work);
		}
		ts->suspended = 0;
		if (sysfs_create_bin_file(&dev->kobj, &cyttsp_firmware))
			printk(KERN_ERR "%s: unable to create file\n",
				__func__);
		if (ts->platform_data->power_state == CY_ACTIVE_STATE) {	
			if(ts->irq > 0) {
				free_irq(ts->irq, ts);
				ts->irq = -1;
			}
		}
		DBG2(printk(KERN_INFO
			"%s: FW loader opened for start load: ps=%d mode=%d\n",
			__func__,
			ts->platform_data->power_state, ts->fw_loader_mode);)
		cyttsp_soft_reset(ts, &timeout);
		printk(KERN_INFO "%s: FW loader started.\n", __func__);
	} else if (!val && ts->fw_loader_mode) {
		sysfs_remove_bin_file(&dev->kobj, &cyttsp_firmware);
		cyttsp_soft_reset(ts, &timeout);
		cyttsp_exit_bl_mode(ts);
		cyttsp_set_sysinfo_mode(ts);	/* update sysinfo rev data */
		cyttsp_set_operational_mode(ts);
		ts->platform_data->power_state = CY_ACTIVE_STATE;
		ts->fw_loader_mode = 0;
		printk(KERN_INFO "%s: FW loader finished.\n", __func__);
		cyttsp_bl_app_valid(ts);
	}
	UNLOCK(ts->mutex);
	return  ret == size ? ret : -EINVAL;
}

static struct device_attribute fwloader =
	__ATTR(fwloader, 0644, attr_fwloader_show, attr_fwloader_store);
	
#endif

static ssize_t attr_hardware_reset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int ret;
	struct cyttsp *ts = dev_get_drvdata(dev );
	
	cyttsp_touch_ctrl(ts, false);
	gpio_cyttsp_hw_reset();
	
	ret = cyttsp_power_on(ts);
	if(ret) {
		printk(KERN_ERR "cyttsp:hw_reset:power on failed");
		return ret;
	}
	return size;
}

static struct device_attribute attr_hw_reset =
	__ATTR(hw_reset, 0644, NULL, attr_hardware_reset_store);

static ssize_t attr_recali_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct cyttsp *ts =  dev_get_drvdata(dev);
	if(!cyttsp_i2c_clientst || ts->suspended) {
		printk(KERN_ERR "cyttsp:calibrate::cannot communicate when device is suspended");
		return -EINVAL;
	}	
	if( 0 == strncmp(buf, "open", 4) ) {
		printk(KERN_ERR "cyttsp:calibration:forcing open test mode");
		ttsp_recalibrate(cyttsp_i2c_clientst->client, -1);
		
	}else if(buf[0] == '0'){
		ttsp_recalibrate(cyttsp_i2c_clientst->client, 0);
	}else  {	
		ttsp_recalibrate(cyttsp_i2c_clientst->client, 1);
	}
	return size;
}

static ssize_t attr_recali_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	if(cyttsp_i2c_clientst && !ts->suspended) {	
		ttsp_recalibrate(cyttsp_i2c_clientst->client, 1);
	}else{
		printk(KERN_ERR "cyttsp:calibrate::cannot communicate when device is suspended");
	}
	return 1;
}

static struct device_attribute recalibrate =
	__ATTR(recalibrate, 0644, attr_recali_show, attr_recali_store);
	
static ssize_t attr_lpm_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct cyttsp *ts = dev_get_drvdata(dev); 
	int ai = 0, tout = 0, li = 0;
	int ret;
	u8 intrvl_ray[3];
	
	if(ts->suspended) {	
		printk(KERN_ERR "cyttsp:lpm::cannot communicate when device is suspended");
		return -EINVAL;
	}
		
	cyttsp_disable_irq_cancel_work_lock(ts);	
	sscanf(buf, "%d %d %d", &ai, &tout, &li);
	
	if(li && tout && li) {
		ts->platform_data->use_lpm = 1;
		
		intrvl_ray[0] = ai;
		intrvl_ray[1] = tout;
		intrvl_ray[2] = li;
		
		ret = cyttsp_set_sysinfo_mode(ts);
		if(ret) {
			printk(KERN_ERR "%s: cannot set sysinfo mode", __func__);
			goto out;
		}

		/* set intrvl registers */
		ret = ttsp_write_block_data(ts,
				CY_REG_ACT_INTRVL,
				sizeof(intrvl_ray), intrvl_ray);
		
	}else {
		ts->platform_data->use_lpm = 0;
	}
out:	
	/* write LPM bit in operational mode */
	ret = cyttsp_set_operational_mode(ts);
	if(ret) {
		printk(KERN_ERR "%s: cannot set operational mode", __func__);
	}
	return size;
}

static struct device_attribute lpm =
	__ATTR(lpm, 0222, NULL, attr_lpm_store);


static ssize_t attr_vendor_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cyttsp *ts = dev_get_drvdata(dev);
	u8 cyttsp_vendor_id = CYTTSP_UNKNOWN_PANEL;
	int retval;
	int i = 0;
	if(atomic_read(&touch_locked))
	{
		printk(KERN_ERR "cannot read vendor id when touch is disabled.");
		return -EBUSY;
	}
	do{
		retval |= ttsp_read_block_data(cyttsp_tsc, CY_REG_VENDOR_ID,
				      sizeof(u8), &cyttsp_vendor_id);
		i++;
	}while(CYTTSP_UNKNOWN_PANEL == cyttsp_vendor_id && i < 2);
	
	switch(cyttsp_vendor_id){
		case CYTTSP_CANDO_PANEL:
			return sprintf(buf, "CANDO");
		break;
		
		case CYTTSP_TPK_PANEL:
			return sprintf(buf, "TPK");
		break;
		
		default:
			return sprintf(buf, "UNKNOWN");
	}
	
	ts->platform_data->wakeup();
	
	return -EINVAL;
}

static struct device_attribute vendor_id =
	__ATTR(vendor_id, 0444, attr_vendor_id_show, NULL);
	
static ssize_t attr_sysinfo_scan_type_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	u8 scn_typ;
	int len = 0;
	struct cyttsp *ts = dev_get_drvdata(dev);
	
	cyttsp_disable_irq_cancel_work_lock(ts);
	if(!cyttsp_i2c_clientst || ts->suspended) {	
		printk(KERN_ERR "cyttsp:scan_type::cannot communicate when device is suspended");
		return -EINVAL;
	}
	if(ts->device_mode != CY_SI_MODE) {
		ret = cyttsp_set_sysinfo_mode(ts);
		if(ret) {
			printk(KERN_ERR "%s: cannot set sysinfo mode", __func__);
			goto out;
		}
	
	}
	
	ret = ttsp_read_block_data(ts,
				CY_REG_SCN_TYP,
				sizeof(scn_typ), &scn_typ);
	printk(KERN_INFO "scan type read 0x%x", scn_typ);
	switch(scn_typ & 0x03){
		case CY_SCN_TYP_MUT_SELF:
			len = sprintf(buf, "mutual-self");
		break;
		case CY_SCN_TYP_MUTUAL:
			len = sprintf(buf, "mutual");
		break;
		case CY_SCN_TYP_SELF:
			len = sprintf(buf, "self");
		break;
		default:
			len = sprintf(buf, "?scan type: 0x%x", scn_typ);
	}
	
	ret = cyttsp_set_operational_mode(ts);
	if(ret) {
		printk(KERN_ERR "%s: cannot set operational mode", __func__);
	}
	
out:
	return len;
}

static ssize_t attr_sysinfo_scan_type_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;
	u8 scn_typ;
	u8 st_read;
	int retry = 0;
	struct cyttsp *ts = dev_get_drvdata(dev);
	
	cyttsp_disable_irq_cancel_work_lock(ts);
	
	if(!cyttsp_i2c_clientst || ts->suspended) {	
		printk(KERN_ERR "cyttsp:scan_type::cannot communicate when device is suspended");
		return -EINVAL;
	}
	if(ts->device_mode != CY_SI_MODE) {
		ret = cyttsp_set_sysinfo_mode(ts);
		if(ret) {
			printk(KERN_ERR "%s: cannot set sysinfo mode", __func__);
			goto out;
		}
		
	}
	do{
		if(0 == strncmp(buf, "mutual", 6)) {
			scn_typ = CY_SCN_TYP_MUTUAL;
			ret = ttsp_write_block_data(ts,
					CY_REG_SCN_TYP,
					sizeof(scn_typ), &scn_typ);
		}else if(0 == strncmp(buf, "self", 4)) {
			scn_typ = CY_SCN_TYP_SELF;
			ret = ttsp_write_block_data(ts,
					CY_REG_SCN_TYP,
					sizeof(scn_typ), &scn_typ);
		}else if(0 == strncmp(buf, "mutual-self", 11)) {
			scn_typ = CY_SCN_TYP_MUT_SELF;
			ret = ttsp_write_block_data(ts,
					CY_REG_SCN_TYP,
					sizeof(scn_typ), &scn_typ);
		}else {
			printk(KERN_ERR "cannot recognize scan type %s", buf);
		}	
		msleep(100);
		_cyttsp_hndshk(ts);

		ret = ttsp_read_block_data(ts,
			CY_REG_SCN_TYP,
			sizeof(u8), &st_read);
		printk(KERN_INFO "\n\n%s scan_type read: 0x%x\n\n", __func__, st_read);
		
	}while(st_read != scn_typ && retry++ < 10);
out:
	clear_flags_command();

	printk(KERN_INFO "\n\n%s scan_type read: 0x%x retry %d\n\n", __func__, st_read, retry);	
	/*
	ret = cyttsp_set_operational_mode(ts);
	if(ret) {
		printk(KERN_ERR "%s: cannot set operational mode", __func__);
	}*/
	
	return strlen(buf);
}	

static struct device_attribute sysinfo_scan_type =
	__ATTR(sysinfo_scan_type, 0644, attr_sysinfo_scan_type_show, attr_sysinfo_scan_type_store);	

static long cyttsp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret = -EINVAL;
	
	if(cyttsp_tsc->suspended) {
		printk(KERN_ERR "cyttsp:ioctl::cannot communicate when device is suspended");
		return ret;
	}
	switch (cmd) {
		case CY_IOCTL_TEST_MODE_DUMP:
		{
			struct cyttsp_test_mode_pages tm_data;
			if(copy_from_user(&tm_data, argp, sizeof(struct cyttsp_test_mode_pages))) {
				return -EFAULT;
			}
			if(ttsp_test_mode_dump_reg(&tm_data) == 0) {
				if (copy_to_user(argp, &tm_data, sizeof(struct cyttsp_test_mode_pages))) {
					return -EFAULT;
				}else
				{
					ret = 0;
				}
			} else {
				printk(KERN_ERR "cannot read test mode registers, try again\n");
				return -EFAULT;
			}
		}
		break;
		
		default:
		break;
	}
	return ret;
}

static ssize_t cyttsp_misc_write(struct file *file, const char __user *buf,
                                 size_t count, loff_t *pos)
{
  return 0;
}

static ssize_t cyttsp_misc_read(struct file *file, char __user *buf,
                                size_t count, loff_t *pos)
{
  return 0;
}

static const struct file_operations cyttsp_misc_fops =
{
  .owner = THIS_MODULE,
  .read  = cyttsp_misc_read,
  .write = cyttsp_misc_write,
  .unlocked_ioctl = cyttsp_ioctl,
};

static struct miscdevice cyttsp_misc_device =
{
  .minor = CY_DEV_MINOR,
  .name  = CY_DRIVER_NAME,
  .fops  = &cyttsp_misc_fops,
};

static int cyttsp_probe(struct platform_device* pdev)
{
	struct input_dev *input_device;
	struct cyttsp *ts;
	int retval = 0;

	int i = 0;
	
	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		printk(KERN_ERR "%s: Error, kzalloc\n", __func__);
		retval = -ENOMEM;
		goto error_alloc_data_failed;
	}
	mutex_init(&ts->mutex);
	ts->pdev = &pdev->dev;

	ts->platform_data = &cyttsp_i2c_platform_data;
	ts->platform_data->wakeup = cyttsp_wakeup;
	if (cyttsp_i2c_clientst == NULL) {
		printk(KERN_ERR "%s: Error, CYTTSP I2C Client not initialized \n",__func__);
		goto error_init;
	}
	ts->bus_ops = &cyttsp_i2c_clientst->ops;       

	if (ts->platform_data->init)
		retval = ts->platform_data->init(1);
	if (retval) {
		printk(KERN_ERR "%s: platform init failed! \n", __func__);
		goto error_init;
	}
	
	if(gpio_cyttsp_init_pins() ){
		printk(KERN_ERR "cyttsp:gpio:request pins failed");
	}
	
	gpio_touchcntrl_request_irq(1);
	ts->irq = gpio_touchcntrl_irq();
	
	/* Create the input device and register it. */
	input_device = input_allocate_device();
	if (!input_device) {
		retval = -ENOMEM;
		printk(KERN_ERR "%s: Error, failed to allocate input device\n",
			__func__);
		goto error_input_allocate_device;
	}

	ts->input = input_device;
	input_device->name = ts->platform_data->name;
	input_device->phys = ts->phys;
	input_device->dev.parent = ts->pdev;

	/* Prepare our worker structure prior to setting up the timer/ISR */
	INIT_WORK(&ts->work, cyttsp_xy_worker);

	retval = cyttsp_power_on(ts);
	if (retval < 0) {
		printk(KERN_ERR "%s: Error, power on failed! \n", __func__);
		goto error_power_on;
	}

	set_bit(EV_SYN, input_device->evbit);
	set_bit(EV_KEY, input_device->evbit);
	set_bit(EV_ABS, input_device->evbit);
	set_bit(BTN_TOUCH, input_device->keybit);
	set_bit(BTN_TOOL_FINGER, input_device->keybit);
	set_bit(BTN_TOOL_DOUBLETAP, input_device->keybit);
	set_bit(BTN_TOOL_TRIPLETAP, input_device->keybit);

	set_bit(BTN_2, input_device->keybit);
	if (ts->platform_data->use_gestures)
		set_bit(BTN_3, input_device->keybit);

	if (ts->platform_data->use_mt)
	{
	//	input_mt_create_slots(input_device, CY_NUM_TRK_ID);
		input_set_abs_params(input_device, ABS_MT_SLOT, 0, CY_NUM_TRK_ID - 1,  0, 0); 
	}
	input_set_abs_params(input_device, ABS_X, 0, ts->platform_data->maxx,
			     0, 0);
	input_set_abs_params(input_device, ABS_Y, 0, ts->platform_data->maxy,
			     0, 0);
	/*input_set_abs_params(input_device, ABS_TOOL_WIDTH, 0,
			     CY_LARGE_TOOL_WIDTH, 0, 0);
	input_set_abs_params(input_device, ABS_PRESSURE, 0, CY_MAXZ, 0, 0);
	input_set_abs_params(input_device, ABS_HAT0X, 0,
			     ts->platform_data->maxx, 0, 0);
	input_set_abs_params(input_device, ABS_HAT0Y, 0,
			     ts->platform_data->maxy, 0, 0);*/
	if (ts->platform_data->use_gestures) {
		input_set_abs_params(input_device, ABS_HAT1X, 0, CY_MAXZ,
				     0, 0);
		input_set_abs_params(input_device, ABS_HAT1Y, 0, CY_MAXZ,
				     0, 0);
	}
	if (ts->platform_data->use_mt) {
		input_set_abs_params(input_device, ABS_MT_POSITION_X, 0,
				     ts->platform_data->maxx, 0, 0);
		input_set_abs_params(input_device, ABS_MT_POSITION_Y, 0,
				     ts->platform_data->maxy, 0, 0);
		/*input_set_abs_params(input_device, ABS_MT_TOUCH_MAJOR, 0,
				     CY_MAXZ, 0, 0);
		input_set_abs_params(input_device, ABS_MT_WIDTH_MAJOR, 0,
				     CY_LARGE_TOOL_WIDTH, 0, 0);*/
		if (ts->platform_data->use_trk_id)
			input_set_abs_params(input_device, ABS_MT_TRACKING_ID,
					0, CY_NUM_TRK_ID, 0, 0);
	}

	if (ts->platform_data->use_virtual_keys)
		input_set_capability(input_device, EV_KEY, KEY_PROG1);

	retval = input_register_device(input_device);
	if (retval) {
		printk(KERN_ERR "%s: Error, failed to register input device\n",
			__func__);
		goto error_input_register_device;
	}

	// Make sure we clear the MT_SLOT state
	clear_slots(ts);
	
	DBG(printk(KERN_INFO "%s: Registered input device %s\n",
		   __func__, input_device->name);)

#ifdef CONFIG_HAS_CYTTSP_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = cyttsp_ts_early_suspend;
	ts->early_suspend.resume = cyttsp_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

#ifdef CONFIG_HAS_CYTTSP_FWLOADER
	retval = device_create_file(&pdev->dev, &fwloader);	
	if (retval) {
		printk(KERN_ERR "%s: Error, could not create attribute\n",
			__func__);
		goto device_create_error;
	}
#endif
    
	retval = device_create_file(&pdev->dev, &recalibrate);	
	if (retval) {
		printk(KERN_ERR "%s: Error, could not create attribute\n",
			__func__);
		goto device_create_error_fwloader;
	}
        
	proc_entry = create_proc_entry(CY_PROC_NAME, 0644, NULL );
	if (proc_entry == NULL) {
		DEBUG_ERR("create_proc: could not create proc entry\n");
		goto device_create_error_recalibrate;
	}
	proc_entry->read_proc = cyttsp_proc_read;
	proc_entry->write_proc = cyttsp_proc_write;
	if (misc_register(&cyttsp_misc_device))
	{
		DEBUG_ERR("cyttsp: Couldn't register device 10, %d.\n", CY_DEV_MINOR);
		goto device_create_error_proc;
	}
	
	retval = device_create_file(&pdev->dev, &vendor_id);	
	if (retval) {
		printk(KERN_ERR "%s: Error, could not create attribute\n",
			__func__);
		goto device_create_error_vendor_id;
	}

	retval = device_create_file(&pdev->dev, &sysinfo_scan_type);	
	if (retval) {
		printk(KERN_ERR "%s: Error, could not create attribute\n",
			__func__);
		goto device_create_error_scan_type;
	}
	retval = device_create_file(&pdev->dev, &lpm);	
	if (retval) {
		printk(KERN_ERR "%s: Error, could not create attribute\n",
			__func__);
		goto device_create_error_lpm;
	}
	retval = device_create_file(&pdev->dev, &attr_hw_reset);	
	if (retval) {
		printk(KERN_ERR "%s: Error, could not create attribute\n",
			__func__);
		goto device_create_error_hw_reset;
	}
	mutex_init(&proc_mutex);
	platform_set_drvdata(pdev, ts);
	cyttsp_tsc = ts;
	return 0;
device_create_error_hw_reset:
	device_remove_file(&pdev->dev, &attr_hw_reset);
device_create_error_lpm:
	device_remove_file(&pdev->dev, &lpm);
device_create_error_scan_type:
	device_remove_file(&pdev->dev, &sysinfo_scan_type);
device_create_error_vendor_id:
	device_remove_file(&pdev->dev, &recalibrate);

device_create_error_proc:
		remove_proc_entry(CY_PROC_NAME, NULL);

device_create_error_recalibrate:
        device_remove_file(&pdev->dev, &recalibrate);

device_create_error_fwloader:
        device_remove_file(&pdev->dev, &fwloader);	

device_create_error:

#ifdef CONFIG_HAS_CYTTSP_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	input_unregister_device(input_device);
error_input_register_device:
	if (ts->irq > 0) {
		free_irq(ts->irq, ts);
		ts->irq = -1;
	}	
error_power_on:
	cyttsp_kill_to_timer(ts);
	input_free_device(input_device);
error_input_allocate_device:
	gpio_touchcntrl_request_irq(0);
	if (ts->platform_data->init)
		ts->platform_data->init(0);
error_init:
	mutex_destroy(&ts->mutex);
	kfree(ts);
error_alloc_data_failed:
	return retval;
}

static int cyttsp_remove(struct platform_device *pdev)
{
	struct cyttsp *ts;
	
	ts = (struct cyttsp *) platform_get_drvdata(pdev);
	if (ts == NULL)
		return -ENODEV;

	DBG(printk(KERN_INFO"%s: Enter\n", __func__);)
#ifdef CONFIG_HAS_CYTTSP_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	
	cyttsp_disable_irq_cancel_work_lock(ts);
	
	gpio_touchcntrl_request_irq(0);
	mutex_destroy(&ts->mutex);
	
	//if (ts->platform_data->use_mt)
//		input_mt_destroy_slots(ts->input);
        
	if(proc_entry)
		remove_proc_entry(CY_PROC_NAME, NULL);
	misc_deregister(&cyttsp_misc_device);
	device_remove_file(&pdev->dev, &recalibrate);
#ifdef CONFIG_HAS_CYTTSP_FWLOADER
	device_remove_file(&pdev->dev, &fwloader);		
#endif
	device_remove_file(&pdev->dev, &attr_hw_reset);
	device_remove_file(&pdev->dev, &vendor_id);
	device_remove_file(&pdev->dev, &sysinfo_scan_type);
	device_remove_file(&pdev->dev, &lpm);
	input_unregister_device(ts->input);
	input_free_device(ts->input);
	if (ts->platform_data->init)
		ts->platform_data->init(0);
	cyttsp_tsc = NULL;
	kfree(ts);

	return 0;
}

static void cyttsp_shutdown(struct platform_device *pdev)
{
	//return cyttsp_remove(pdev);
}

static struct platform_driver cyttsp_driver =
{
	.driver = {
	  .name = CY_DRIVER_NAME,
	},
	.probe = cyttsp_probe,
	.shutdown = cyttsp_shutdown,
	.remove = cyttsp_remove,
#ifdef CONFIG_PM
	.suspend = cyttsp_suspend, 
	.resume = cyttsp_resume, 
#endif
};

static void cyttsp_nop_release(struct device* dev)
{
	/* Do Nothing */
}

static struct platform_device cyttsp_device =
{
	.name = CY_DRIVER_NAME,
	.id   = 0,
	.dev  = {
		.release = cyttsp_nop_release,
	},
};

static int  __init cyttsp_init(void)
{
	int ret = 0;
	
	i2c_probe_success = 0;
	ret = i2c_add_driver(&cyttsp_i2c_driver);
	if (ret < 0)
	{
		DEBUG_ERR(CY_ERR_I2C_ADD);		
		return -ENODEV;
	}
	if (!i2c_probe_success)
	{
		i2c_del_driver(&cyttsp_i2c_driver);
		return -ENODEV;
	}
	
	DEBUG_INFO("Registering platform device\n");
	platform_device_register(&cyttsp_device);
	platform_driver_register(&cyttsp_driver);
	return ret;
}

static void __exit cyttsp_exit(void)
{
	DEBUG_INFO("Calling exit");
	if (i2c_probe_success)
	{
		i2c_probe_success = 0;
		platform_driver_unregister(&cyttsp_driver);
		platform_device_unregister(&cyttsp_device);
	}
	i2c_del_driver(&cyttsp_i2c_driver);
}

module_init(cyttsp_init);
module_exit(cyttsp_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard touchscreen driver core");
MODULE_AUTHOR("Haili Wang <hailiw@lab126.com>");

