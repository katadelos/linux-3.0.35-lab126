/*
 * Copyright (c) 2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * License Terms: GNU General Public License, version 2
 *
 * Driver for Texas Instruments DRV26xx Haptic Controllers
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/sysdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/lab126_haptic.h>
#include "drv26xx.h"
#include <mach/boardid.h>

#define DRV26XX_MAX_EFFECTS 10
#define DRV26XX_I2C_READ_REG_NUM_MSG 2
#define DRV26XX_I2C_READ_NUM_MSG 3

// Wrapper for the drv26xx_waveform to include meta data
struct drv26xx_waveform_data{
    u8 ram_index; //Used as a flag to indicate that this holds a valid waveform
    struct drv26xx_waveform waveform;
};


/*
  These values are used when writing waveforms to ram
*/
struct drv26xx_ram_counters
{
	u8  curr_head;
	u16 curr_data;
	u8  curr_header_size;
};

struct drv26xx_hap
{
	struct i2c_client *drv26xx_client;
	struct drv26xx_waveform_data waveform_data_list[DRV26XX_MAX_NUM_WAVEFORMS]; // waveforms are indexed at 1
	int num_waveforms;
	int curr_page;
	int curr_gain;
	struct drv26xx_ram_counters ram_counters;
	struct delayed_work drive_pin_work;
	int lock;
};

struct drv26xx_hap haptics = {
	.num_waveforms = 0,
	.curr_page = -1,
	.curr_gain = 0,
};

static int drv26xx_i2c_write_reg(u8 reg, u8 val);
static int drv26xx_i2c_read_reg(u8 reg, u8 *buf);
static int drv26xx_i2c_read(u8 page, u8 reg_start, u8 *buf, int len);
static long haptic_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

extern int haptic_request_pins(void);
extern int gpio_haptic_ident(void);
extern void haptic_drive_pin(u8 enable);

module_param(drv26xx_debug, int, 0664);

static inline void reset_write_config(void)
{
	haptics.ram_counters.curr_head = 0x01;
	haptics.ram_counters.curr_data = 0x0100;
	haptics.ram_counters.curr_header_size = 0;
}

/*********************************************************
 * Convenience functions for basic drv26xx manipulation
 *********************************************************/
static int drv26xx_select_page(u8 page)
{
	int ret;
	if(page == haptics.curr_page) {
		return 0;
	} else {
		ret = drv26xx_i2c_write_reg(DRV26XX_PAGE_ADDRESS, page);
		if(unlikely(ret))
			printk_drv26xx_err("Unable to select page\n");
		haptics.curr_page = page;
		return ret;
	}
}

static int drv26xx_set_gain(u8 gain)
{
	int ret;
	u8 original;
	drv26xx_select_page(DRV26XX_CONTROL_PAGE);

	ret = drv26xx_i2c_read_reg(DRV26XX_GAIN_ADDRESS, &original);
	if(unlikely(ret)) {
		return ret;
	}
	gain = (original & 0xFC) | gain;
	ret = drv26xx_i2c_write_reg(DRV26XX_GAIN_ADDRESS, gain);

	return ret;
}

//This indexing is strange... starts at 1 because pages for effects do as well
static int drv26xx_set_sequencer_start_id(u8 waveform_id)
{
	int ret;
	drv26xx_select_page(DRV26XX_CONTROL_PAGE);
	ret = drv26xx_i2c_write_reg(DRV26XX_SEQUENCER_ADDRESS, waveform_id);
	if(unlikely(ret))
		printk_drv26xx_err("Unable to set sequencer address\n");
	return ret;
}

static int drv26xx_stop_wform_sequence(void)
{
	int ret;
	drv26xx_select_page(DRV26XX_CONTROL_PAGE);
	ret = drv26xx_i2c_write_reg(DRV26XX_GO_ADDRESS, 0);
	if(unlikely(ret))
		printk_drv26xx_err("Unable to stop waveform\n");


	/* older than or equal to EVT3 would need to drive these pins*/
	if(! (lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_EVT3) )) {
		cancel_delayed_work(&(haptics.drive_pin_work));
		haptic_drive_pin(false);
	}
	return ret;
}

static int _drv26xx_execute_wform_sequence(u8 waveform_ram_index)
{
	int ret;

	drv26xx_select_page(DRV26XX_CONTROL_PAGE); //make sure I'm on control page
	drv26xx_stop_wform_sequence();
	drv26xx_set_sequencer_start_id(waveform_ram_index);

	/* older than or equal to EVT3 would need to drive these pins*/
	if(!(lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_EVT3)))
		haptic_drive_pin(true);

	ret = drv26xx_i2c_write_reg(DRV26XX_GO_ADDRESS, DRV26XX_GO_CMD);
	if(unlikely(ret))
		printk_drv26xx_err("Unable to execute waveform\n");
	//schedule a work to float the drive pin some time later

	/* older than or equal to EVT3 would need to drive these pins*/
	if(!(lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_EVT3)))
		schedule_delayed_work(&(haptics.drive_pin_work),  msecs_to_jiffies(10));

	return ret;
}

int drv26xx_execute_wform_sequence(u8 waveform_id)
{
	if(haptics.lock)
		return 0;
	return _drv26xx_execute_wform_sequence(haptics.waveform_data_list[waveform_id].ram_index);
}
EXPORT_SYMBOL(drv26xx_execute_wform_sequence);

static int drv26xx_enter_standby(void)
{
	int ret;
	drv26xx_stop_wform_sequence();
	drv26xx_select_page(DRV26XX_CONTROL_PAGE);
	ret = drv26xx_i2c_write_reg(DRV26XX_STANDBY_ADDRESS, DRV26XX_STANDBY_CMD);
	if(unlikely(ret))
		printk_drv26xx_err("Unable to put device into standby\n");
	return ret;
}

static int drv26xx_exit_standby(void)
{
	int ret;
	drv26xx_select_page(DRV26XX_CONTROL_PAGE);
	ret = drv26xx_i2c_write_reg(DRV26XX_STANDBY_ADDRESS, DRV26XX_EXIT_STANDBY_CMD);
	if(unlikely(ret))
		printk_drv26xx_err("Unable to exit standby\n");
	return ret;
}

static inline void drv26xx_pause(void)
{
	msleep(5);
}

static int drv26xx_reset(void)
{
	int ret = 0, retries = 3;

	while (retries--)
	{
		drv26xx_select_page(DRV26XX_CONTROL_PAGE);
		ret = drv26xx_i2c_write_reg(DRV26XX_RESET_ADDRESS, DRV26XX_RESET_CMD);
		drv26xx_pause();

		if (ret)
			printk_drv26xx_err("Couldn't reach device for reset, retrying\n");
		else
			break;
	}
	if(unlikely(ret))
		printk_drv26xx_err("Unable to reset device\n");
	return ret;
}

#if 0
static int drv26xx_is_fifo_full(void)
{
	int ret;
	u8 fifo_status;

	drv26xx_select_page(DRV26XX_CONTROL_PAGE);
	ret = drv26xx_i2c_read_reg(DRV26XX_FIFO_FULL_ADDRESS, &fifo_status);
	if(unlikely(ret)) {
		printk_drv26xx_err("Unable to read fifo status!\n");
		return 1; // return true to be safe
	}
	return (fifo_status & 1); //check bit 0
}
#endif //0

/*********************************************************
 * Fucntions to write waveforms to ram
 *********************************************************/

/*
	 All headers are written on page 1, data starts on page 2
	 */
static u8 drv26xx_i2c_write_header(struct drv26xx_waveform_data *wfd)
{
	u8 start_addr_upper_byte;
	u8 start_addr_lower_byte;
	u8 end_addr_upper_byte;
	u8 end_addr_lower_byte;

	drv26xx_select_page(0x01);

	haptics.ram_counters.curr_header_size += 5;

	start_addr_upper_byte = (haptics.ram_counters.curr_data >> 8) | 0x80; // 0x80 indicates waveform synth mode
	start_addr_lower_byte = (haptics.ram_counters.curr_data & 0xFF);

	haptics.ram_counters.curr_data = haptics.ram_counters.curr_data + (wfd->waveform.num_effects * sizeof(struct drv26xx_effect));

	end_addr_upper_byte = ((haptics.ram_counters.curr_data-1) >> 8);
	end_addr_lower_byte = ((haptics.ram_counters.curr_data-1) & 0xFF);

	drv26xx_i2c_write_reg(0, haptics.ram_counters.curr_header_size); // header size - 1
	drv26xx_i2c_write_reg(haptics.ram_counters.curr_head++, start_addr_upper_byte);
	drv26xx_i2c_write_reg(haptics.ram_counters.curr_head++, start_addr_lower_byte);

	drv26xx_i2c_write_reg(haptics.ram_counters.curr_head++, end_addr_upper_byte);
	drv26xx_i2c_write_reg(haptics.ram_counters.curr_head++, end_addr_lower_byte);
	drv26xx_i2c_write_reg(haptics.ram_counters.curr_head++, wfd->waveform.repeat_count);

        return start_addr_lower_byte;
}

/*
	 All headers are written on page 1, data starts on page 2
	 */
static void drv26xx_i2c_write_data(struct drv26xx_waveform_data *wfd, u8 start_addr_lower_byte)
{
	int i;
	int data_pointer = start_addr_lower_byte;

	drv26xx_select_page(0x02);
	for(i = 0; i < wfd->waveform.num_effects; i++)
	{
		drv26xx_i2c_write_reg(data_pointer++, wfd->waveform.effects[i].amplitude);
		drv26xx_i2c_write_reg(data_pointer++, wfd->waveform.effects[i].frequency);
		drv26xx_i2c_write_reg(data_pointer++, wfd->waveform.effects[i].duration);
		drv26xx_i2c_write_reg(data_pointer++, wfd->waveform.effects[i].envelope);
	}
}

static void drv26xx_write_waveform(struct drv26xx_waveform_data *wfd)
{
        u8 start_addr_lower_byte;

	start_addr_lower_byte = drv26xx_i2c_write_header(wfd);
	drv26xx_i2c_write_data(wfd, start_addr_lower_byte);
}


static void drv26xx_clear_waveforms(void)
{
	int i;
        struct drv26xx_waveform_data *wfd;

	drv26xx_exit_standby();

	for(i = 0; i < DRV26XX_MAX_NUM_WAVEFORMS; i++) {
		wfd = &(haptics.waveform_data_list[i]);
		//Check to see if we have a real waveform
		if(wfd->ram_index) {
			if(wfd->waveform.num_effects) {
					printk_drv26xx_err("Freeing effects array for waveform %d\n", i);
					kfree(wfd->waveform.effects);
					wfd->waveform.num_effects = 0; //This will cause us to reallocate if we reassign this id
			}
			wfd->ram_index = 0;
		}
	}
	haptics.num_waveforms=0; // reset, but account for fadeform
	reset_write_config(); //reset for writing new waveforms into ram
}

static void drv26xx_write_all_wavforms_to_ram(void)
{
	int i;
	int curr_ram_index = 1;
	haptics.num_waveforms = 0;

	drv26xx_exit_standby();

	reset_write_config();

	for(i = 0; i < DRV26XX_MAX_NUM_WAVEFORMS; i++) {
			if(haptics.waveform_data_list[i].ram_index) //check for non-zero value
			{
					drv26xx_write_waveform(&haptics.waveform_data_list[i]);
					haptics.waveform_data_list[i].ram_index = curr_ram_index++;
					haptics.num_waveforms++;
			}
	}
	drv26xx_select_page(DRV26XX_CONTROL_PAGE); // get back to control space
}

static int prepare_effects_array(struct drv26xx_waveform_data *wfd, int old_num_effects)
{
	printk_drv26xx_info("Changing array size to %d\n", wfd->waveform.num_effects);

	//We have to be careful with this
	if(old_num_effects != 0) {
		printk_drv26xx_info("old_num_effects = %d, freeing array\n", old_num_effects);
		kfree(wfd->waveform.effects);
	}

	wfd->waveform.effects = kmalloc(wfd->waveform.num_effects * sizeof(struct drv26xx_effect), GFP_KERNEL);

	if(unlikely(!wfd->waveform.effects))
		return -ENOMEM;

	return 0;
}

static int check_duration_envelope_condition(u8 frequency, u8 duration, u8 envelope)
{
	int real_frequency = (frequency * 78125) / (10000); //Min frequency 7.8125Hz
	int waveform_length = ((1000000 / real_frequency) * duration) / 1000;
	int attack_val = (envelope >> 4) & 15; //4 bit attack value
	int envelope_length = envelope_time_lookup[attack_val];

	if(envelope_length > waveform_length) // if attack time is longer than waveform
		return 0;
	else
		return 1;
}


static int drv26xx_program_fade(struct drv26xx_waveform_program_info *wpi)
{
	struct drv26xx_waveform_data *wfd;
	int old_num_effects, i, valid_waveform = 1;

	wfd = &(haptics.waveform_data_list[wpi->waveform_id]);

	old_num_effects = wfd->waveform.num_effects;

	/* Start copying values */
	wfd->waveform.repeat_count = wpi->waveform.repeat_count;
	wfd->waveform.num_effects = wpi->waveform.num_effects;

	/* Prepare effects array */
	if(wfd->waveform.num_effects != old_num_effects)
	{
			if(prepare_effects_array(wfd, old_num_effects) == -ENOMEM)
				return -ENOMEM;
	}

	/* Copy over effect data */
	memcpy(wfd->waveform.effects, wpi->waveform.effects,
		   wfd->waveform.num_effects * sizeof(struct drv26xx_effect));

	/* If any effect has an attack defined, check for IC bug */
	for(i = 0; i < wfd->waveform.num_effects; i++)
	{
			if((wfd->waveform.effects[i].envelope >> 4) > 0)
			{
					valid_waveform &= check_duration_envelope_condition(
						wfd->waveform.effects[i].frequency,
						wfd->waveform.effects[i].duration,
						wfd->waveform.effects[i].envelope);
			}
	}

	if(valid_waveform)
	{
			wfd->ram_index = 1; //Mark wfd, so we write it to ram
			drv26xx_write_all_wavforms_to_ram();
			return 0;
	}
	else /* Remove waveform */
	{
			wfd->ram_index = 0;
			kfree(wfd->waveform.effects);
			printk_drv26xx_err("Invalid waveform! Attack is longer than duration!\n");
			return -EINVAL;
	}
}

/*************************************************************
 * DEBUGGING FUNCTIONS
 *************************************************************/
static int drv26xx_dump_page(u8 page_num)
{
	int i;
	u8 pagebuf[DRV26XX_PAGE_SIZE];
	char *buf;

	if(page_num >= 8) {
		printk_drv26xx_err("page number %d is out of range!", page_num);
		return 0;
	}

	buf = kmalloc(1024, GFP_KERNEL);

	printk_drv26xx_err(" **** PAGE %i **** ", page_num);

	drv26xx_i2c_read(page_num, 0, pagebuf, DRV26XX_PAGE_SIZE);

	for(i = 0; i < DRV26XX_PAGE_SIZE; i++)
	{
		if(i % 16 == 0)
			sprintf(buf, "%2X:%2X ", i, pagebuf[i]);
		else
			sprintf(buf + strlen(buf), "%2X:%2X ", i, pagebuf[i]);

		if(i % 16 == 15)
			printk_drv26xx_err("%s", buf);
	}

	kfree(buf);

	return 0;
}

static void drv26xx_print_fade(struct drv26xx_effect *effect)
{
	printk_drv26xx_info("**********\n");
	printk_drv26xx_info("F:%x", effect->frequency);
	printk_drv26xx_info("A:%x", effect->amplitude);
	printk_drv26xx_info("D:%x", effect->duration);
	printk_drv26xx_info("E:%x", effect->envelope);
	printk_drv26xx_info("**********\n");
}

/*************************************************************
 * SYSDEV functions (waveform registry + waveform playback)
 *************************************************************/
/*****************************************************
 * Waveform registry functions
 ****************************************************/
static ssize_t drv26xx_waveform_registry_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	int retval = 0;
	int i, j;
	if(haptics.num_waveforms == 0)
	{
		retval += sprintf(buf, "No waveforms are stored yet!\n");
	}
	else
	{
		for(i = 0; i < DRV26XX_MAX_NUM_WAVEFORMS; i++)
		{
			if(haptics.waveform_data_list[i].ram_index)
			{
				retval += sprintf(buf+strlen(buf),
								  "Wvfm  ID:%2i => DRV26XX RAM Index:%2i ",
								  i, haptics.waveform_data_list[i].ram_index);

				for(j = 0; j < haptics.waveform_data_list[i].waveform.num_effects; j++)
				{
					retval += sprintf(buf+strlen(buf), "{0x%02x,0x%02x,0x%02x,0x%02x} ",
									  haptics.waveform_data_list[i].waveform.effects[j].frequency,
									  haptics.waveform_data_list[i].waveform.effects[j].amplitude,
									  haptics.waveform_data_list[i].waveform.effects[j].duration,
									  haptics.waveform_data_list[i].waveform.effects[j].envelope);
				}
				retval += sprintf(buf+strlen(buf), "\n");
			}
		}
	}

	return retval;
}


/*****************************************************
 * Waveform playback functions
 ****************************************************/
static ssize_t drv26xx_play_waveform_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "To play a waveform, write a single byte (waveform id) into this file\n");
}

static ssize_t drv26xx_play_waveform_store(struct sys_device *dev,
		struct sysdev_attribute *attr,
		const char *buf, size_t count)
{
	u8 waveform_id;
	u8 drv26xx_ram_index;

	waveform_id = (u8)simple_strtol(buf, NULL, 10);
	drv26xx_ram_index = haptics.waveform_data_list[waveform_id].ram_index;
	if(drv26xx_ram_index)
	{
		printk_drv26xx_err("Playing app waveform %i, with drv26xx id %i\n",
		waveform_id, drv26xx_ram_index);
		_drv26xx_execute_wform_sequence(drv26xx_ram_index);
	}
	else
	{
		printk_drv26xx_err("Invalid waveform id!!!\n");
	}

	return count;
}


/*****************************************************
 * Page dumping access
 ****************************************************/
static ssize_t drv26xx_dump_page_show(struct sys_device *dev,
     struct sysdev_attribute *attr, char *buf)
{
    return sprintf(buf, "To dump a page, echo page number into this file\n");
}

static ssize_t drv26xx_dump_page_store(struct sys_device *dev,
		struct sysdev_attribute *attr,
		const char *buf, size_t count)
{
	int page_num;
	if ((sscanf(buf, "%d", &page_num) > 0)) {
			drv26xx_dump_page(page_num);
			return count;
	} else {
			return -EINVAL;
	}
}

/*****************************************************
 * Reset
 ****************************************************/
static ssize_t drv26xx_reset_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "To reset, echo a 1 into this file\n");
}

static ssize_t drv26xx_reset_store(struct sys_device *dev,
		struct sysdev_attribute *attr,
		const char *buf, size_t count)
{
	if(buf[0] == '1')
		drv26xx_reset();
	return count;
}

/*****************************************************
 * Clear Waveforms
 ****************************************************/
static ssize_t drv26xx_clear_waveforms_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "To clear waveforms, echo a 1 into this file\n");
}

static ssize_t drv26xx_clear_waveforms_store(struct sys_device *dev,
		struct sysdev_attribute *attr,
		const char *buf, size_t count)
{
	if(buf[0] == '1')
		drv26xx_clear_waveforms();
	return count;
}


/*****************************************************
 * Version
 ****************************************************/
static ssize_t drv26xx_version_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "%d.%d", DRV26XX_VERSION_MAJOR, DRV26XX_VERSION_MINOR);
}

static ssize_t drv26xx_id_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{

	int ret;
	u8 ctrl;
	ret = drv26xx_i2c_read_reg(DRV26XX_GAIN_ADDRESS, &ctrl);
	if(unlikely(ret)) {
		return ret;
	}
	return sprintf(buf, "ctrl reg=0x%02x, id=0x%02x\n", ctrl, (ctrl & DRV26XX_ID_MASK) >> DRV26XX_ID_SHIFT);
}

static ssize_t drv26xx_id_pin_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	int id;

	/* later than EVT3 would have one vendor only*/
	if(	lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_EVT3))
		id = 0;
	else
		id = gpio_haptic_ident();

	return sprintf(buf, "%d", id);
}

/*****************************************************
 * Global gain setting
 ****************************************************/
static ssize_t drv26xx_gain_setting_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "Current gain: %i\nTo set gain, echo gain value into this file (0-3)\n",
			haptics.curr_gain);
}

static ssize_t drv26xx_gain_setting_store(struct sys_device *dev,
		struct sysdev_attribute *attr,
		const char *buf, size_t count)
{
	u8 gain;

	gain = (u8)simple_strtol(buf, NULL, 10);

	if(gain < 4)
	{
		printk_drv26xx_info("Setting global gain to %i\n", gain);
		drv26xx_set_gain(gain);
		haptics.curr_gain = gain;
	}
	else
	{
		printk_drv26xx_err("Invalid gain value! Must be 0-3\n");
	}

	return count;
}

/*****************************************************
 * FADE!!!
 ****************************************************/
static ssize_t drv26xx_program_fade_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "To program FADE, echo values into this file.\n"
			"ID|REPEAT_COUNT|NUM_EFFECTS|FF|AA|DD|EE FF|AA|DD|EE ...\n");
}

static ssize_t drv26xx_program_fade_store(struct sys_device *dev,
		struct sysdev_attribute *attr,
		const char *buf, size_t count)
{
	int i, ret;
	char* pbuf;
	int id, repeat_count, num_effects, offset;
	int freq, amp, dur, env;
	struct drv26xx_waveform_program_info wpi;
	struct drv26xx_effect eff_buf[DRV26XX_MAX_EFFECTS];
	pbuf = (char*)buf;

	ret = sscanf(pbuf, "%x|%x|%x|%n", &id, &repeat_count, &num_effects, &offset);
	pbuf += offset;

	if(ret != 3) {
		printk_drv26xx_err("Unable to parse FADE string\n");
		return -EINVAL;
	}

	wpi.waveform_id = (u8)id;
	wpi.waveform.repeat_count = (u8)repeat_count;
	wpi.waveform.num_effects = (u8)num_effects;

	printk_drv26xx_info("Found ID: %d\n",     wpi.waveform_id);
	printk_drv26xx_info("Repeat Count: %d\n", wpi.waveform.repeat_count);
	printk_drv26xx_info("Num effects: %d\n",  wpi.waveform.num_effects);

	/* Temporarily allocate space for effects array */
	wpi.waveform.effects = eff_buf;

	for(i = 0; i < wpi.waveform.num_effects && i < DRV26XX_MAX_EFFECTS ; i++) {
		ret = sscanf(pbuf, "%x|%x|%x|%x|%n", &freq, &amp, &dur, &env, &offset);
		pbuf += offset;

		if(ret != 4)
		{
			printk_drv26xx_err("Unable to parse FADE string\n");
			return -EINVAL;
		}

		wpi.waveform.effects[i].frequency = (u8)freq;
		wpi.waveform.effects[i].amplitude = (u8)amp;
		wpi.waveform.effects[i].duration = (u8)dur;
		wpi.waveform.effects[i].envelope = (u8)env;

		drv26xx_print_fade(&wpi.waveform.effects[i]);
	}

	ret = drv26xx_program_fade(&wpi);

	if(ret)
		return ret;
	else
		return count;
}


/*****************************************************
 * Programming interface
 ****************************************************/
static ssize_t drv26xx_programming_interface_show(struct sys_device *dev,
		struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "To program drv26xx, cat config file into this file\n");
}

static ssize_t drv26xx_programming_interface_store(struct sys_device *dev,
		struct sysdev_attribute *attr,
		const char *buf, size_t count)
{
	int val1, val2;
	char *line;
	char wf[9];
	int i, ret;
	int waveform_id;
	int amp, freq, dur, env;
	char* pbuf;
	struct drv26xx_waveform_program_info wpi;
	pbuf = (char*)buf;

	while((line = strsep(&pbuf, "\n")) != NULL && (strcmp(line, "") != 0))
	{
		sscanf(line, "%s %i", wf, &waveform_id); //Grab the "WAVEFORM" and the id
		if(strcmp(wf, "WAVEFORM") == 0  && (waveform_id < DRV26XX_MAX_NUM_WAVEFORMS))
		{
			printk_drv26xx_info("Creating waveform with id: %i\n", waveform_id);
                        wpi.waveform_id = waveform_id;

			line = strsep(&pbuf, "\n"); //Grab the repeat count and the num effects
			sscanf(line, "R=%x N=%x", &val1, &val2);
					wpi.waveform.repeat_count = (u8)val1;
					wpi.waveform.num_effects = (u8)val2;

			printk_drv26xx_info("Found repeat: (%i), count: (%i)\n",
					wpi.waveform.repeat_count, wpi.waveform.num_effects);

			/* Temporarily allocate space for effects array */
			wpi.waveform.effects = kmalloc( wpi.waveform.num_effects *
										sizeof(struct drv26xx_effect), GFP_KERNEL);

			for(i = 0; i < wpi.waveform.num_effects; i++)
			{
				line = strsep(&pbuf, "\n");
				sscanf(line, "A=%x F=%x D=%x E=%x", &amp, &freq, &dur, &env);

				wpi.waveform.effects[i].amplitude = (u8) amp;
				wpi.waveform.effects[i].frequency = (u8) freq;
				wpi.waveform.effects[i].duration =  (u8) dur;
				wpi.waveform.effects[i].envelope =  (u8) env;

				drv26xx_print_fade(&wpi.waveform.effects[i]);
			}

			ret = drv26xx_program_fade(&wpi);

			/* Free temporarily allocated buffer */
			kfree(wpi.waveform.effects);

			if(ret)
				return ret;
		}
	}
	return count;
}

#if 0

/*****************************************************
 * Arbitrary waveform playback
 ****************************************************/

static void drv26xx_print_wav_header(struct drv26xx_waveform_header *header)
{
	printk_drv26xx_info("=================================");
	printk_drv26xx_info("chunk id:        0x%x", header->chunk_id);
	printk_drv26xx_info("chunk size:      %d",   header->chunk_size);
	printk_drv26xx_info("format:          0x%x", header->format);
	printk_drv26xx_info("format id:       0x%x", header->format_id);
	printk_drv26xx_info("format size:     %d",   header->format_size);
	printk_drv26xx_info("audio format:    %d",   header->audio_format);
	printk_drv26xx_info("num channels:    %d",   header->num_channels);
	printk_drv26xx_info("sample rate:     %d",   header->sample_rate);
	printk_drv26xx_info("byte rate:       %d",   header->byte_rate);
	printk_drv26xx_info("block align:     %d",   header->block_align);
	printk_drv26xx_info("bits per sample: %d",   header->bits_per_sample);
	printk_drv26xx_info("data id:         0x%x", header->data_id);
	printk_drv26xx_info("data size:       %d",   header->data_size);
	printk_drv26xx_info("=================================");
}

static inline int drv26xx_verify_val(char *name, u32 val, u32 expected)
{
	if(val != expected)
	{
		printk_drv26xx_err("Incorrect %s, found (%d), expected (%d)",
				name, val, expected);
		return 0;
	}
	return 1;
}

static int drv26xx_fill_wav_header(struct drv26xx_waveform_header *header, const char *buf)
{
	memcpy(header, buf, sizeof(struct drv26xx_waveform_header));

	if(drv26xx_verify_val("chunk id",      header->chunk_id,        DRV26XX_WAV_CHUNK_ID) &&
		drv26xx_verify_val("format",        header->format,          DRV26XX_WAV_FORMAT) &&
		drv26xx_verify_val("format id",     header->format_id,       DRV26XX_WAV_FORMAT_ID) &&
		drv26xx_verify_val("format size",   header->format_size,     DRV26XX_WAV_FORMAT_SIZE) &&
		drv26xx_verify_val("audio format",  header->audio_format,    DRV26XX_WAV_AUDIO_FMT) &&
		drv26xx_verify_val("num channels",  header->num_channels,    DRV26XX_WAV_NUM_CHAN) &&
		drv26xx_verify_val("sample rate",   header->sample_rate,     DRV26XX_WAV_SAMP_RATE) &&
		drv26xx_verify_val("byte rate",     header->byte_rate,       DRV26XX_WAV_BYTE_RATE) &&
		drv26xx_verify_val("block align",   header->block_align,     DRV26XX_WAV_BLK_ALIGN) &&
		drv26xx_verify_val("bits/sample",   header->bits_per_sample, DRV26XX_WAV_BITS_SAMP) &&
		drv26xx_verify_val("data id",       header->data_id,         DRV26XX_WAV_DATA_ID))
		return 1;
	else
		return 0;
}

#endif //#if 0

/*****************************************************
 * SYSDEV boilerplate (registration + deregistration)
 ****************************************************/
static struct sysdev_class haptics_sysclass = {
	.name = "drv26xx_haptics",
};

static struct sys_device haptics_sysdevice = {
	.id = 0,
	.cls = &haptics_sysclass,
};

static struct sysdev_attribute attributes[] = {
	_SYSDEV_ATTR(id_pin, S_IRUGO,
		drv26xx_id_pin_show, NULL),
	_SYSDEV_ATTR(id, S_IRUGO,
		drv26xx_id_show, NULL),
	_SYSDEV_ATTR(programming_interface, S_IRUGO|S_IWUSR,
		drv26xx_programming_interface_show, drv26xx_programming_interface_store),
	_SYSDEV_ATTR(waveform_registry, S_IRUGO|S_IWUSR,
		drv26xx_waveform_registry_show, NULL),
	_SYSDEV_ATTR(play_waveform, S_IRUGO|S_IWUSR,
		drv26xx_play_waveform_show, drv26xx_play_waveform_store),
	_SYSDEV_ATTR(program_fade, S_IRUGO|S_IWUSR,
		drv26xx_program_fade_show, drv26xx_program_fade_store),
	_SYSDEV_ATTR(gain_setting, S_IRUGO|S_IWUSR,
		drv26xx_gain_setting_show, drv26xx_gain_setting_store),
	_SYSDEV_ATTR(version, S_IRUGO,
		drv26xx_version_show, NULL),
	_SYSDEV_ATTR(reset, S_IRUGO|S_IWUSR,
		drv26xx_reset_show, drv26xx_reset_store),
	_SYSDEV_ATTR(clear_waveforms, S_IRUGO|S_IWUSR,
		drv26xx_clear_waveforms_show, drv26xx_clear_waveforms_store),
	_SYSDEV_ATTR(dump_page, S_IRUGO|S_IWUSR,
                drv26xx_dump_page_show, drv26xx_dump_page_store),

};

static int add_sysfs_interfaces(void )
{
	int i;
	int err = 0;
	err = sysdev_class_register(&haptics_sysclass);
	if (!err)
		err = sysdev_register(&haptics_sysdevice);
	if (!err) {
		for (i = 0; i < ARRAY_SIZE(attributes); i++)
		{
			if (sysdev_create_file(&haptics_sysdevice, attributes + i))
				goto undo;
		}
		return 0;
undo:
		for (; i >= 0 ; i--)
			sysdev_remove_file(&haptics_sysdevice, attributes + i);
		sysdev_unregister(&haptics_sysdevice);
		sysdev_class_unregister(&haptics_sysclass);
		printk_drv26xx_err("%s: failed to create sysfs interface\n", __func__);
	}
	return -ENODEV;
}

static void remove_sysfs_interfaces(void )
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		sysdev_remove_file(&haptics_sysdevice, attributes + i);
	sysdev_unregister(&haptics_sysdevice);
	sysdev_class_unregister(&haptics_sysclass);
}

/*************************************************************
 * CHRDEV functions (read/write i2c passthroughs)
 *************************************************************/
static ssize_t drv26xx_read_not_implemented(struct file *filp, char *buf, size_t len, loff_t *off)
{
	return 0;
}

static ssize_t drv26xx_write_not_implemented(struct file *filp, const char *buf, size_t len, loff_t *off)
{
	return 0;
}

/*****************************************************
 * CHRDEV boilerplate (registration + deregistration)
 ****************************************************/
static int drv26xx_major;
static struct class *drv26xx_class;
static struct device *drv26xx_dev;
static struct file_operations drv26xx_fops =
{
	.read = drv26xx_read_not_implemented,
	.write = drv26xx_write_not_implemented,
	.unlocked_ioctl = haptic_ioctl,
};

static int drv26xx_chrdev_init(void)
{
	void *ptr_err;
	if ((drv26xx_major = register_chrdev(0, HAPTIC_MISC_DEV_NAME, &drv26xx_fops)) < 0)
		return drv26xx_major;

	drv26xx_class = class_create(THIS_MODULE, HAPTIC_MISC_DEV_NAME);
	if (IS_ERR(ptr_err = drv26xx_class))
		goto err2;

	drv26xx_dev = device_create(drv26xx_class, NULL, MKDEV(drv26xx_major, 0), NULL, HAPTIC_MISC_DEV_NAME);
	if (IS_ERR(ptr_err = drv26xx_dev))
		goto err;

	return 0;
err:
	class_destroy(drv26xx_class);
err2:
	unregister_chrdev(drv26xx_major, HAPTIC_MISC_DEV_NAME);
	return PTR_ERR(ptr_err);
}


static int drv26xx_chrdev_exit(void)
{
	device_destroy(drv26xx_class, MKDEV(drv26xx_major, 0));
	class_destroy(drv26xx_class);
	unregister_chrdev(drv26xx_major, HAPTIC_MISC_DEV_NAME);

	drv26xx_clear_waveforms();

	return 0;
}

/*
 * some haptic controllers could not talk to address 59 because of the VDD ramp time was too slow. 
 * When the device is in this state, talk to i2c address 00 to reset the chip, and it should recover. 
 * 
 * */
static void drv26xx_i2c_write_recovery(void)
{
	u8 buf[DRV26XX_ALLOWED_W_BYTES];

	struct i2c_msg msgs[] = {
		{
			.addr = 0x0,
			.flags = haptics.drv26xx_client->flags,
			.len = 2,
		},
	};
 
	buf[0] = DRV26XX_RESET_ADDRESS;
	buf[1] = DRV26XX_RESET_CMD;

	msgs->buf = buf;
	
	//successfully wrote to address 00, indicating the controller was in a bad state. 
	if(i2c_transfer(haptics.drv26xx_client->adapter, msgs, 1) == 1) {
		dev_err(&haptics.drv26xx_client->dev, "haptic recovered\n");
		msleep(1);
	} 
	return;
}

/*****************************************************
 * Basic i2c communication functions
 ****************************************************/
static int drv26xx_i2c_write_reg(u8 reg, u8 val)
{
	int err;
	int tries = 0;
	u8 buf[DRV26XX_ALLOWED_W_BYTES];

	struct i2c_msg msgs[] = {
		{
			.addr = haptics.drv26xx_client->addr,
			.flags = haptics.drv26xx_client->flags,
			.len = 2,
		},
	};

	buf[0] = reg;
	buf[1] = val;

	msgs->buf = buf;

	do {
		err = i2c_transfer(haptics.drv26xx_client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(DRV26XX_I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < DRV26XX_MAX_RW_RETRIES));

	if (err != 1) {
		dev_err(&haptics.drv26xx_client->dev, "write transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

/*
 * work function to float the drive pin after the waveform complete.
 * If the waveform has not yet complete, check again 10ms later.
 *
 * old for EVT3 or older
 *
 * */
static void drv26xx_drive_pin_float_work_func(struct work_struct *work)
{

	int ret;
	u8 go_reg;

	ret = drv26xx_i2c_read_reg(DRV26XX_GO_ADDRESS, &go_reg);
	if(unlikely(ret)) {
		printk_drv26xx_err("%s:Unable to communicate with haptic controller\n", __func__);
		return;
	}
	if (go_reg == 0) {
		haptic_drive_pin(false);
	}else {
		schedule_delayed_work(&haptics.drive_pin_work,  msecs_to_jiffies(10));
	}

}

static int drv26xx_i2c_read_reg(u8 reg, u8 *buf)
{
	int err;
	int tries = 0;
	u8 reg_buf[DRV26XX_ALLOWED_R_BYTES];

	struct i2c_msg msgs[DRV26XX_I2C_READ_REG_NUM_MSG] = {
		{
			.addr = haptics.drv26xx_client->addr,
			.flags = haptics.drv26xx_client->flags,
			.len = 1,
		},
		{
			.addr = haptics.drv26xx_client->addr,
			.flags = (haptics.drv26xx_client->flags | I2C_M_RD),
			.len = 1,
			.buf = buf,
		},
	};
	reg_buf[0] = reg;
	msgs->buf = reg_buf;

	do {
		err = i2c_transfer(haptics.drv26xx_client->adapter, msgs, DRV26XX_I2C_READ_REG_NUM_MSG);
		if (err != DRV26XX_I2C_READ_REG_NUM_MSG)
			msleep_interruptible(DRV26XX_I2C_RETRY_DELAY);
	} while ((err != DRV26XX_I2C_READ_REG_NUM_MSG) && (++tries < DRV26XX_MAX_RW_RETRIES));

	if (err != DRV26XX_I2C_READ_REG_NUM_MSG) {
		dev_err(&haptics.drv26xx_client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int drv26xx_i2c_read(u8 page, u8 reg_start, u8 *buf, int len)
{
	int err;
	int tries = 0;
	char page_buf[2];

	struct i2c_msg msgs[DRV26XX_I2C_READ_NUM_MSG] = {
		{
			.addr = haptics.drv26xx_client->addr,
			.flags = haptics.drv26xx_client->flags,
			.len = 2,
			.buf = page_buf,
		},
		{
			.addr = haptics.drv26xx_client->addr,
			.flags = haptics.drv26xx_client->flags,
			.len = 1,
			.buf = &reg_start,
		},
		{
			.addr = haptics.drv26xx_client->addr,
			.flags = (haptics.drv26xx_client->flags | I2C_M_RD),
			.len = len,
			.buf = buf,
		},
	};

	page_buf[0] = DRV26XX_PAGE_ADDRESS;
	page_buf[1] = page;

	do {
		err = i2c_transfer(haptics.drv26xx_client->adapter, msgs, DRV26XX_I2C_READ_NUM_MSG);
		if (err != DRV26XX_I2C_READ_NUM_MSG)
			msleep_interruptible(DRV26XX_I2C_RETRY_DELAY);
	} while ((err != DRV26XX_I2C_READ_NUM_MSG) && (++tries < DRV26XX_MAX_RW_RETRIES));

	if (err != DRV26XX_I2C_READ_NUM_MSG) {
		dev_err(&haptics.drv26xx_client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int drv26xx_driver_suspend(struct device *dev)
{
	drv26xx_enter_standby();
	return 0;
}

static int drv26xx_driver_resume(struct device *dev)
{
	drv26xx_exit_standby();
	return 0;
}


static long haptic_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret = 0;
	struct drv26xx_waveform_r1_e1 wf;
	struct drv26xx_waveform_program_info wpi;
	int gain;
	int play;
	switch (cmd) {
		case HAPTIC_IOCTL_RESET:
		{
			ret = drv26xx_reset();
			break;
		}
		case HAPTIC_IOCTL_SET_WAVEFORM:
		{
			if (copy_from_user(&wf, argp, sizeof(struct drv26xx_waveform_r1_e1)))
				return -EFAULT;

			wpi.waveform_id = wf.waveform_id;
			wpi.waveform.num_effects = 1;
			wpi.waveform.repeat_count = 1;
			wpi.waveform.effects = &(wf.waveform);
			ret = drv26xx_program_fade(&wpi);

			break;
		}
		case HAPTIC_IOCTL_GET_WAVEFORM:
		{
			wf.waveform.frequency = haptics.waveform_data_list[1].waveform.effects[0].frequency;
			wf.waveform.amplitude = haptics.waveform_data_list[1].waveform.effects[0].amplitude;
			wf.waveform.duration = haptics.waveform_data_list[1].waveform.effects[0].duration;
			wf.waveform.envelope = haptics.waveform_data_list[1].waveform.effects[0].envelope;
			wf.waveform_id = 1;

			if (copy_to_user(argp, &wf, sizeof(struct drv26xx_waveform_r1_e1)))
				return -EFAULT;
			break;
		}
		case HAPTIC_IOCTL_SET_GAIN:
		{
			if (copy_from_user(&gain, argp, sizeof(int)))
				return -EFAULT;
			ret = drv26xx_set_gain(gain);
			break;
		}
		case HAPTIC_IOCTL_GET_GAIN:
		{
			if (copy_to_user(argp, &(haptics.curr_gain), sizeof(int)))
				return -EFAULT;
			break;
		}
		case HAPTIC_IOCTL_GET_LOCK:
		{
			if (copy_to_user(argp, &(haptics.lock), sizeof(int)))
				return -EFAULT;
			break;
		}
		case HAPTIC_IOCTL_SET_LOCK:
		{
			if (copy_from_user(&(haptics.lock), argp, sizeof(int)))
				return -EFAULT;
			break;
		}
		case HAPTIC_IOCTL_PLAY_DIAGS_WF:
		{
			if (copy_from_user(&play, argp, sizeof(int)))
				return -EFAULT;
			if(play)
				ret = drv26xx_execute_wform_sequence(HAPTIC_WAVEFORM_ID_DIAGS);
			else
				ret = drv26xx_stop_wform_sequence();
			break;
		}
		default:
			printk(KERN_ERR "ioctl no case !!\n");
			ret = -ENOTTY;
			break;
	}
	return ret;
}

/*************************************************************
 * I2C functions (probe + remove)
 *************************************************************/
static int drv26xx_driver_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
	int ret;
	u8 drv26xx_id = 0;

	haptics.lock = 0;
	client->addr = DRV26XX_I2C_ADDRESS;
	haptics.drv26xx_client = client;

	
	if(drv26xx_reset()) {
		drv26xx_i2c_write_recovery();
		if(drv26xx_reset()) 
			goto error;
	}

	if(drv26xx_exit_standby())
		goto error;

	ret = drv26xx_i2c_read_reg(DRV26XX_ID_ADDRESS, &drv26xx_id);
	if(unlikely(ret)) {
		printk_drv26xx_err("Unable to read id from device\n");
		return ret;
	}

	drv26xx_id = (drv26xx_id & DRV26XX_ID_MASK) >> DRV26XX_ID_SHIFT;

	if( ( (client->addr == 0x59) && (drv26xx_id == DRV26XX_ID) ) ||
			( (client->addr == 0x00) && (drv26xx_id == 0) ) )
		printk_drv26xx_err("Found matching id!\n");
	else
	{
		printk_drv26xx_err("Wrong DRV26XX id (%i)\n", drv26xx_id);
		return -1;
	}

	/* older than or equal to EVT3 would need to drive these pins*/
	if(! (lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_512_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WARIO_EVT3) ||
		lab126_board_rev_greater(BOARD_ID_ICEWINE_WFO_WARIO_EVT3) )) {
		haptic_request_pins();
		INIT_DELAYED_WORK(&(haptics.drive_pin_work), drv26xx_drive_pin_float_work_func);
	}

	add_sysfs_interfaces();
	drv26xx_chrdev_init();
	reset_write_config(); //this sets us up for writing to ram
	printk_drv26xx_err("Probe succeeded!\n");
	return 0;

error:
	printk_drv26xx_err("Device initialization failed\n");
	return -1;
}

static int __devexit drv26xx_driver_remove(struct i2c_client *client)
{
	remove_sysfs_interfaces();
	drv26xx_chrdev_exit();
	return 0;
}

static const struct dev_pm_ops haptic_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(drv26xx_driver_suspend, drv26xx_driver_resume)
};

static const struct i2c_device_id drv26xx_idtable[] = {
	{DRV26XX_NAME, 0},
	{ },
};

MODULE_DEVICE_TABLE(i2c, drv26xx_idtable);

static struct i2c_driver drv26xx_driver = {
	.probe    = drv26xx_driver_probe,
	.remove   = drv26xx_driver_remove,
	.id_table = drv26xx_idtable,
	.driver = {
		.name = DRV26XX_NAME,
		.pm = &haptic_pm_ops,
	},
};

static int __init drv_26xx_haptics_init(void)
{
	int ret = 0;

	ret = i2c_add_driver(&drv26xx_driver);
	if (ret < 0)
	{
                printk_drv26xx_err("Error probing %s\n", DRV26XX_NAME);
		return -ENODEV;
	}
	return ret;
}
module_init(drv_26xx_haptics_init);

static void __exit drv26xx_haptics_exit(void)
{
	i2c_del_driver(&drv26xx_driver);
}

module_exit(drv26xx_haptics_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nadim Awad");
MODULE_DESCRIPTION("FSR Keypad driver");
