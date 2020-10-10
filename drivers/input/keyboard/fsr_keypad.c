/*
 * Copyright (c) 2013-2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * License Terms: GNU General Public License, version 2
 *
 * FSR keypad driver
 *
 * bootloader of KL05 is a big endian device, i2c protocol is not register based
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
#include <linux/lab126_fsr.h> //ioctl
#include <linux/miscdevice.h>
#include <linux/firmware.h>
#include <linux/proc_fs.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/cpufreq.h>

#include "fsr_fw.h"
#include "fsr_keypad.h"
#include "bootloader.h"
#include "bootloader_inception.h"

#define FSR_KEYMAP_SIZE 4
#define FSR_NO_REPEAT 1
#define FSR_IRQ_TYPE IRQ_TYPE_EDGE_FALLING

#define FSR_DRIVER_NAME "fsr_keypad"
#define FSR_I2C_ADDRESS 0x58

#define MCU_APP_START_ADDR 0x800

/*FIXME this should be determine by bootloader */
static u32 g_mcu_end_addr = 0x8000;
static int g_mcu_erase_block_size = 0x400; /*1KB*/
#define MCU_ERASE_END_ADDR (g_mcu_end_addr - g_mcu_erase_block_size)
#define MCU_CALIB_DATA_ADDR MCU_ERASE_END_ADDR /*in this case 0x8000 - 0x400 = 0x7C00*/

#define FLASH_WR_BLK_SZ   64

#define FSR_EXIT_BL_TIMEOUT 500

#define FSR_POWER_STATE_OFF 0x00
#define FSR_POWER_STATE_ON 0x01

#define FSR_KEY_RELEASED 0
#define FSR_KEY_PRESSED  1

#define FSR_PRESS_NEXT_WAVEFORM       1
#define FSR_RELEASE_NEXT_WAVEFORM     1
#define FSR_PRESS_PREVIOUS_WAVEFORM   1
#define FSR_RELEASE_PREVIOUS_WAVEFORM 1

enum fsr_button{
	FSR_BUTTON_LIFT_UP,           // 0
	FSR_BUTTON_LEFT_TOP,          // 1
	FSR_BUTTON_LEFT_BOTTOM,       // 2
	FSR_BUTTON_RIGHT_TOP,         // 3
	FSR_BUTTON_RIGHT_BOTTOM,      // 4
};

enum power_state{
	FSR_POWERSTATE_OFF,
	FSR_POWERSTATE_ON,
	FSR_POWERSTATE_ON_PENDING,
	FSR_POWERSTATE_OFF_PENDING,
};


#define G_RETRY_CT 0

#define CHECK( func ) if(unlikely((func)))	{ printk(KERN_ERR "%s: %d failed", __func__, __LINE__); return; }
#define CHECK_RET( ret_asn_func ) (ret_asn_func); if(unlikely(ret))	{ printk(KERN_ERR "%s: %d failed with %d", __func__, __LINE__, ret); return ret; }
#define CHECK_NO_RET( func ) if(unlikely((func)))	{ printk(KERN_ERR "%s: %d failed", __func__, __LINE__); }

#define FSR_DEF_REPEAT_DELAY 1000
#define FSR_DEF_REPEAT_PERIOD 600

#define FSR_DEF_PRESSURE     150

#define FSR_I2C_MAX_RETRY 5
#define FSR_I2C_RETRY_DELAY_MS 5

extern int drv26xx_execute_wform_sequence(u8 waveform);

struct fsr_key
{
	unsigned long keyval;
	int state;
};

struct fsr_fw_dynamic_config
{
	u16 pressure_down_fw;
	u16 pressure_up_fw;
	u16 pressure_down_back;
	u16 pressure_up_back;

	u8 scan_rate;
	u8 baseline_rate;
	u8 crosstalk_margin;
	u8 trigger_method;
};

struct fsr_fw_static_config
{
	u8 n_buttons;
	u8 n_adc;
	u8 adc_depth;
	u8 app_ver[2];
};

struct fsr_keypad
{
	struct input_dev *input;
	int irq;
	int wdog_irq;
	struct fsr_key kmap[FSR_KEYMAP_SIZE];
	struct work_struct fsr_work_repeat;
	struct work_struct restart_work;
	struct i2c_client *i2c_fsr;
	struct device* dev;
	struct fsr_fw_dynamic_config fw_state;
	struct fsr_fw_static_config  static_config;
	int device_mode;
	int debug ;
	int trigger_on_up;
	struct proc_dir_entry *proc_entry;
	bool exlock;
	int power_state;
	struct completion power_on_completion;
	volatile bool waiting_power_on;
	bool irq_enabled;
	struct fsr_repeat repeat;
	bool enable_next;  // Send next page turn event toggle
	bool enable_prev;  // Send prev page turn event toggle
	bool event_enable; // Event propagation toggle
	bool touch_active; // Touch is active on screen
	struct mutex system_lock;
};

struct fsr_keypad g_keypad;

extern void gpio_fsr_reset(void);
extern int gpio_fsr_button_irq(void);
extern int gpio_fsr_bootloader_irq(void);
extern void fsr_set_pin(int which, int enable);
extern void wario_fsr_init_pins(void);
extern void fsr_wake_pin_pta5(int enable);
extern void fsr_pm_pins(int suspend_resume);

static int fsr_keypad_get_adc(struct fsr_keypad *keypad, uint16_t *adc, int num_adc);
static int fsr_i2c_poweron(struct fsr_keypad* keypad);
static int fsr_i2c_poweroff(struct fsr_keypad* keypad);
static int fsr_switch_mode(struct fsr_keypad *keypad, u8 mode);
static int fsr_switch_mode_diags(struct fsr_keypad *keypad);
static void clear_keys(struct fsr_keypad *keypad);
static int fsr_wdog_irq_en(struct fsr_keypad *keypad, int en);

int i2c_probe_success = 0;

#define CMD_DELAY 10
static u8 g_sent_cmd[128];
static u8 g_retcmd[128] = {0};

static inline void fsr_wake_pta5(int enable)
{
	i2c_lock_adapter(g_keypad.i2c_fsr->adapter);
	fsr_wake_pin_pta5(enable);
	i2c_unlock_adapter(g_keypad.i2c_fsr->adapter);
}

static inline u8 checksum(u8* buf, int len)
{
	u8 ret = 0;

	for (len--; len >= 0; len--)
		ret += buf[len];
	return ret;
}

static u32 checksum_u32(u8* buf, int len)
{
	u32 ret = 0;

	for (len--; len >= 0; len--)
		ret += buf[len];
	return ret;
}

//call this function with 'z' as last arguement that always printbuf
static void printbuf(u8* buf, int len, char wr)
{
	char pb[512];
	char* ptr = pb;
	int i;
	if(unlikely(g_keypad.debug || wr == 'z')) {
		ptr += sprintf(ptr, "%c: ", wr);
		for (i = 0; i < len; i++) {
			ptr += sprintf(ptr, "%02X ", buf[i]);
			if((i+1) % 8 == 0)
				ptr += sprintf(ptr, "\n   ");
		}
		printk(KERN_ERR "%s", pb);
	}
}

static int wait_for_poweron(struct fsr_keypad *keypad)
{
	int ret;

	ret = wait_for_completion_timeout(&keypad->power_on_completion, msecs_to_jiffies(FSR_EXIT_BL_TIMEOUT));
	if (ret)
		return 0;
	printk(KERN_ERR "Timeout waiting for power on\n");
	keypad->waiting_power_on = false;
	return -1;
}

static inline void fsr_enable_irq(struct fsr_keypad *keypad)
{
	if (!keypad->irq || keypad->irq_enabled)
		return;
	keypad->irq_enabled = true;
	enable_irq(keypad->irq);
}

static inline void fsr_disable_irq(struct fsr_keypad *keypad)
{
	if (!keypad->irq || !keypad->irq_enabled)
		return;
	keypad->irq_enabled = false;
	disable_irq(keypad->irq);
}

static inline void fsr_disable_irq_nosync(struct fsr_keypad *keypad)
{
	if (!keypad->irq || !keypad->irq_enabled)
		return;
	keypad->irq_enabled = false;
	disable_irq_nosync(keypad->irq);
}

static int fsr_i2c_read_block_data(struct i2c_client *client, u8 addr, size_t length, u8 *values)
{
	int rc;

	rc = i2c_master_send(client, &addr, 1);

	if(rc < 0) {
		printk(KERN_ERR "%s failed with %d %d\n", __func__, rc, __LINE__);
		return rc;
	}

	rc = i2c_master_recv(client, values, length);

	printbuf(values, length, 'r');

	return (rc < 0) ? rc : rc == length ? 0 : -EIO;

}

static int fsr_i2c_read_block_data_no_addr(struct i2c_client *client, size_t length, u8 *values)
{
	int rc;
	struct i2c_msg xfer[1];

	/*
	 *
	 * Note that i2c protocol in KL05 is not a register based i2c. Ignore the address..
	 *
	 * */

	xfer[0].addr = client->addr;
	xfer[0].flags = I2C_M_RD;
	xfer[0].len = length;
	xfer[0].buf = values;

	rc = i2c_transfer(client->adapter, xfer, 1);

	printbuf(values, length, 'r');
	return (rc < 0) ? rc : rc == 1 ? 0 : -EIO;

}

static int fsr_i2c_write_block_data_no_addr(struct i2c_client *client, size_t length, u8 *values)
{
	int rc;
	struct i2c_msg xfer[1];
	/*
	 *
	 * Note that i2c protocol in KL05 is not a register based i2c. Ignore the address..
	 *
	 * */
	printbuf(values, length, 'w');

	if(length > 128) {
		return -EIO;
	}

	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = length;
	xfer[0].buf = values;

	/* write data */
	rc = i2c_transfer(client->adapter, xfer, 1);

	return (rc < 0) ? rc : rc != 1 ? -EIO : 0;
}

static ssize_t fsr_keypad_reg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	struct i2c_client* i2c = keypad->i2c_fsr;
	u8 regbuf[3];
	int ret;

	if ((ret = sscanf(buf, "%2hhx %2hhx %2hhx", &regbuf[0], &regbuf[1], &regbuf[2])) <= 0)
		return -EINVAL;
	mutex_lock(&keypad->system_lock);
	fsr_wake_pta5(1);
	ret = fsr_i2c_write_block_data_no_addr(i2c, ret, regbuf);
	fsr_wake_pta5(0);
	mutex_unlock(&keypad->system_lock);

	return ret < 0 ? ret : size;
}

static ssize_t fsr_keypad_reg_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	struct i2c_client* i2c = keypad->i2c_fsr;
	unsigned char val = 0;
	int ret;

	mutex_lock(&keypad->system_lock);
	fsr_wake_pta5(1);

	ret = i2c_master_recv(i2c, &val, 1);

	fsr_wake_pta5(0);
	mutex_unlock(&keypad->system_lock);

	if (ret < 0)
		return -EINVAL;

	return sprintf(buf, "0x%x\n", val);
}

static void fsr_get_fw_config(struct fsr_keypad* keypad)
{
	int ret;
	CHECK(fsr_switch_mode_diags(keypad));

	fsr_wake_pta5(1);
	ret = fsr_i2c_read_block_data(keypad->i2c_fsr, REG_D_NUM_ADC_VALS, 1, &(keypad->static_config.n_adc));
	if(ret) {
		keypad->static_config.n_adc = 6;
		printk(KERN_ERR "%s cannot get n_adc, fall back to 6\n", __func__);
	}
	ret = fsr_i2c_read_block_data(keypad->i2c_fsr, REG_D_ADC_DEPTH, 1, &(keypad->static_config.adc_depth));
	if(ret) {
		keypad->static_config.adc_depth = 16;
		printk(KERN_ERR "%s cannot get adc_depth, fall back to 16\n", __func__);
	}
	CHECK_NO_RET(fsr_switch_mode(keypad, ACTIVE_MODE_NORMAL));
	CHECK_NO_RET(fsr_i2c_read_block_data(keypad->i2c_fsr, REG_N_VERSION_MAJOR, 2, keypad->static_config.app_ver));
	CHECK_NO_RET(fsr_i2c_read_block_data(keypad->i2c_fsr, REG_N_NUM_BUTTONS, 1, &(keypad->static_config.n_buttons)));
	CHECK_NO_RET(fsr_i2c_read_block_data(keypad->i2c_fsr, REG_N_FLASH_END, 4, (u8*)&(g_mcu_end_addr)));
	fsr_wake_pta5(0);

	printk(KERN_ERR "fsr_config: n_adc=%d, adc_depth=%d, app_ver=%x, n_buttons=%d, flash=%x\n",
		keypad->static_config.n_adc,
		keypad->static_config.adc_depth,
		keypad->static_config.app_ver[1] | keypad->static_config.app_ver[0] << 8,
		keypad->static_config.n_buttons,
		g_mcu_end_addr);

}

static int kl05_simple_fc_cmd(struct i2c_client* i2c, u8 whichcmd, u8* retbuf, int retbuf_len)
{
	//[4 bytes cmd len big endian(6) ] [ 1 byte hook cmd (0x02) ] [1 byte CS]
	u8 buf[6] = {0x06, 0x00, 0x00, 0x00, 0x00, 0x00};
	int ret;
	int ready = 0;
	buf[4] = whichcmd;
	buf[5] = checksum(buf, 5);

	ret = fsr_i2c_write_block_data_no_addr(i2c, sizeof(buf), buf);
	if(unlikely(ret < 0)) {
		printk(KERN_ERR "%s: i2c write failed !\n", __func__);
		return ret;
	}

	if(retbuf == NULL || retbuf_len == 0)
		return ret;

	if (whichcmd == KL05_BL_CMD_EXIT_BL) {
		// After exiting bootloader, i2c does not work anymore,
		// Do not read back the result.
		return 0;
	}

	ret = fsr_i2c_read_block_data_no_addr(i2c, retbuf_len, retbuf);
	if(unlikely(ret < 0)) {
		printk(KERN_ERR "%s: i2c read failed !\n", __func__);
		return ret;
	}

	while(retbuf[0] != (buf[4] | 0x80) && ready++ < 5)
	{
		msleep(CMD_DELAY);
		ret = fsr_i2c_read_block_data_no_addr(i2c, retbuf_len, retbuf);
		if(unlikely(ret < 0)) {
			printk(KERN_ERR "%s: i2c read failed !\n", __func__);
			return ret;
		}
	}

	return 0;
}

/*
static int kl05_bl_cmd_hook(struct i2c_client* i2c, u8* retbuf)
{
	if(g_keypad.device_mode != KL05_BL_MODE) {
		printk(KERN_ERR "fsr_keypad:calling hook command while not in bootloader mode!");
		return -EINVAL;
	}
	return kl05_simple_fc_cmd(i2c, KL05_BL_CMD_HOOK, retbuf, 2);
}
*/

static int kl05_bl_cmd_exit_bl(struct i2c_client* i2c)
{
	if(g_keypad.device_mode != KL05_BL_MODE) {
		printk(KERN_ERR "fsr_keypad:calling exit bootloader command while not in bootloader mode!");
		return -EINVAL;
	}
	return kl05_simple_fc_cmd(i2c, KL05_BL_CMD_EXIT_BL, NULL, 0);
	//not in app mode yet, let's wait for the app mode interrupt
}

static void kl05_hw_reset(struct fsr_keypad *keypad)
{
	fsr_wdog_irq_en(keypad, 0);
	keypad->power_state = FSR_POWERSTATE_OFF;
	keypad->device_mode = KL05_BL_MODE;
	gpio_fsr_reset();
	keypad->power_state = FSR_POWERSTATE_ON;
}

static int kl05_bl_cmd_erase(struct i2c_client* i2c, u8* addr_4_le, u8* retbuf)
{
	//[4 bytes cmd len big endian(10) ] [ 1 byte erase cmd (0x45) ] [4 bytes address in little endian] [1 byte CS]
	u8 buf[10] = {0x0A, 0x00, 0x00, 0x00, 0x45};
	int ret;
	int ready = 0;

	buf[5] = addr_4_le[3];
	buf[6] = addr_4_le[2];
	buf[7] = addr_4_le[1];
	buf[8] = addr_4_le[0];

	buf[9] = checksum(buf, 9);

	ret = fsr_i2c_write_block_data_no_addr(i2c, sizeof(buf), buf);
	if(unlikely(ret < 0)) {
		printk(KERN_ERR "%s: i2c write failed !\n", __func__);
		return ret;
	}
	ret = fsr_i2c_read_block_data_no_addr(i2c, 2, retbuf);
	if(unlikely(ret < 0)) {
		printk(KERN_ERR "%s: i2c read failed !\n", __func__);
		return ret;
	}
	while(retbuf[0] != (buf[4] | 0x80) && ready++ < 5)
	{
		msleep(CMD_DELAY);
		ret = fsr_i2c_read_block_data_no_addr(i2c, 2, retbuf);
		if(unlikely(ret < 0)) {
			printk(KERN_ERR "%s: i2c read failed !\n", __func__);
			return ret;
		}
	}

	if(retbuf[1] != 0xFC) {
		printk(KERN_ERR "%s:command failed\n", __func__);
		return -EIO;
	}
	return 0;
}

static int kl05_erase_blocks(struct i2c_client* i2c, u32 from_addr_le, u32 to_addr_le)
{

	u32 i;
	u8 retbuf[8];
	int ret = 0;
	u32 addr;

	if(g_keypad.device_mode != KL05_BL_MODE) {
		return -EFAULT;
	}

	for (i = from_addr_le; i < to_addr_le; i+= 0x100) {
		addr = i;
		ret = kl05_bl_cmd_erase(i2c, (u8*)&addr, retbuf);
		if(unlikely(ret))
			break;
	}
	printk(KERN_ERR "%s stops at address %0x\n", __func__, i);
	return ret;
}

static int kl05_bl_cmd_read(struct i2c_client* i2c,  u8* addr_4_le, u8 read_len, u8* retbuf)
{
	//[4 bytes cmd len big endian(11) ] [ 1 byte read cmd (0x52) ] [4 bytes address in little endian] [1 byte read len] [1 byte CS]
	u8 buf[11] = {0x0B, 0x00, 0x00, 0x00, 0x52};
	int ret;
	int ready = 0;

	if( g_keypad.device_mode != KL05_BL_MODE) {
		printk(KERN_ERR "fsr_keypad:trying to access flash in app mode!!");
		return -EINVAL;
	}

	buf[5] = addr_4_le[3];
	buf[6] = addr_4_le[2];
	buf[7] = addr_4_le[1];
	buf[8] = addr_4_le[0];
	buf[9] = read_len;

	buf[10] = checksum(buf, 10);

	ret = fsr_i2c_write_block_data_no_addr(i2c, sizeof(buf), buf);
	if(unlikely(ret < 0)) {
		printk(KERN_ERR "%s: i2c write failed !\n", __func__);
		return ret;
	}
	ret = fsr_i2c_read_block_data_no_addr(i2c, read_len + 1, retbuf);
	if(unlikely(ret < 0)) {
		printk(KERN_ERR "%s: i2c read failed !\n", __func__);
		return ret;
	}
	while(retbuf[0] != (buf[4] | 0x80) && ready++ < 5)
	{
		msleep(CMD_DELAY);
		ret = fsr_i2c_read_block_data_no_addr(i2c, read_len + 1, retbuf);
		if(unlikely(ret < 0)) {
			printk(KERN_ERR "%s: i2c read failed !\n", __func__);
			return ret;
		}
	}

	return 0;
}

static int kl05_bl_cmd_write(struct i2c_client* i2c, u8* addr_4_le, u8 write_len, u8* data, u8* retbuf)
{
	//[cmd len 4 bytes in big endian] [1 byte write cmd (0x57)] [address 4 bytes little endian] [write len 1 byte] [data n bytes...] [checksum  1 byte]
	u8 buf[255] = {0x00, 0x00, 0x00, 0x00, 0x57};
	int ret;
	int ready = 0;
	int cmdlen = write_len + 11;

	if( g_keypad.device_mode != KL05_BL_MODE) {
		printk(KERN_ERR "fsr_keypad:trying to access flash in app mode!!");
		return -EINVAL;
	}

	//*** assumeing write_len will not exceed 255-11 bytes

	buf[0] = cmdlen;
	buf[5] = addr_4_le[3];
	buf[6] = addr_4_le[2];
	buf[7] = addr_4_le[1];
	buf[8] = addr_4_le[0];
	buf[9] = write_len;

	memcpy(buf+10, data, write_len);

	buf[cmdlen-1] = checksum(buf, cmdlen-1);

	ret = fsr_i2c_write_block_data_no_addr(i2c, cmdlen, buf);
	if(unlikely(ret < 0)) {
		printk(KERN_ERR "%s: i2c write failed !\n", __func__);
		return ret;
	}
	ret = fsr_i2c_read_block_data_no_addr(i2c,  2, retbuf);
	if(unlikely(ret < 0)) {
		printk(KERN_ERR "%s: i2c read failed !\n", __func__);
		return ret;
	}
	while(retbuf[0] != (buf[4] | 0x80)  && ready++ < 5)
	{
		msleep(CMD_DELAY);
		ret = fsr_i2c_read_block_data_no_addr(i2c,  2, retbuf);
		if(unlikely(ret < 0)) {
			printk(KERN_ERR "%s: i2c read failed !\n", __func__);
			return ret;
		}
	}
	if(ready >= 5 && retbuf[1] != 0xFC) {
		printk(KERN_ERR "%s:command failed\n", __func__);
		return -EIO;
	}
	return 0;
}

static int kl05_bl_write_block(struct i2c_client* i2c, u32 addr, u8* write_content, int len)
{
	u8 retbuf[2];
	int ret;

	int loop = len / FLASH_WR_BLK_SZ + 1;
	int i;
	for(i = 0;i < loop;i++) {
		ret = kl05_bl_cmd_write(i2c, (u8*)&addr, len > FLASH_WR_BLK_SZ ? FLASH_WR_BLK_SZ : len, write_content, retbuf);
		if(ret)
			return ret;
		len -= FLASH_WR_BLK_SZ;
		write_content += FLASH_WR_BLK_SZ;
		addr += FLASH_WR_BLK_SZ;
	}
	return ret;
}

static void kl05_verify_calib_data_special(struct i2c_client* i2c, u8* cali_header)
{
	u8 buf[128];
	u32 cali_addr = MCU_CALIB_DATA_ADDR;

	kl05_hw_reset(&g_keypad);

	memset(buf, sizeof(buf), 0);
	CHECK(kl05_bl_cmd_read(i2c, (u8*)&cali_addr,  83, buf));
	memcpy(cali_header, buf+1, 83);

	g_keypad.power_state = FSR_POWERSTATE_OFF;
	CHECK(fsr_i2c_poweron(&g_keypad));
	return;
}

static int kl05_verify_bootloaded_firmware(struct i2c_client* i2c, u8* data, int num_rec)
{
	u8 len;
	int i;
	int ret;
	int retry;
	long long sum_file = 0;
	long long sum_mcu = 0;
	u8 addr[4] = {0};
	u8 retbuf[128];
	u8* ptr = data;

	printk(KERN_INFO "%s enter", __func__);

	for (i = 0; i < num_rec; i++)
	{
		len = ptr[0];
		sum_file += checksum_u32(ptr+3, len-3);
		ptr += len;
	}

	printk(KERN_INFO "file checksum=%lld", sum_file);

	ptr = data;

	for (i = 0; i < num_rec; i++)
	{
		len = ptr[0];
		addr[0] = ptr[2];
		addr[1] = ptr[1];
		addr[2] = 0;
		addr[3] = 0;
		ret = kl05_bl_cmd_read(i2c, addr, len-3, retbuf);
		retry = 0;
		while(ret && retry++ < G_RETRY_CT) {
			printk("last cmd failed, ret=%d, retry=%d\n", ret, retry);
			msleep(10);
			ret = kl05_bl_cmd_read(i2c, addr, len-3, retbuf);
		}
		if(retry > G_RETRY_CT) {
			printk(KERN_ERR "%s retry too many times ...\n", __func__);
			return -EIO;

		}
		sum_mcu += checksum_u32(retbuf+1, len-3);
		ptr += len;
	}

	printk(KERN_INFO "mcu checksum=%lld", sum_mcu);

	return sum_mcu - sum_file;
}

static int kl05_bl_update_firmware(struct i2c_client* i2c, u8* data, int num_rec)
{
	u8 len;
	u8 addr[4] = {0};
	u8 retbuf[128];
	int i;
	u8* ptr = data;
	int ret = 0;
	int retry = 0;

	mutex_lock(&g_keypad.system_lock);
	fsr_disable_irq(&g_keypad);

	kl05_hw_reset(&g_keypad);
	printk(KERN_INFO "%s: erasing blocks..\n", __func__);
	CHECK_RET(ret = kl05_erase_blocks(i2c, MCU_APP_START_ADDR, MCU_ERASE_END_ADDR));
	printk(KERN_INFO "%s: sending blocks..\n", __func__);

	for (i = 0; i < num_rec; i++)
	{
		len = ptr[0];
		addr[0] = ptr[2];
		addr[1] = ptr[1];
		addr[2] = 0;
		addr[3] = 0;
		ret = kl05_bl_cmd_write(i2c, addr, len-3, ptr+3, retbuf);
		retry = 0;
		while((ret || retbuf[1] != 0xFC) && retry++ < G_RETRY_CT) {
			printk(KERN_ERR "last cmd failed, ret=%d, retry=%d\n", ret, retry);
			msleep(10);
			ret = kl05_bl_cmd_write(i2c, addr, len-3, ptr+3, retbuf);
		}
		if(retry > G_RETRY_CT) {
			printk(KERN_ERR "%s retry too many times ...\n", __func__);
			ret = -EIO;
			goto out;
		}
		printbuf(retbuf, len+2, 'b');
		ptr += len;
	}

	if(kl05_verify_bootloaded_firmware(i2c, data, num_rec) != 0) {
		printk(KERN_ERR "\n\n%s:bootloaded firmware checksum failed!!\n\n", __func__);
		ret = -EFAULT;
		//something bad happened, try to power on anyway
	}else {
		printk(KERN_INFO "%s: checksum passed\n", __func__);
	}

out:
	mutex_unlock(&g_keypad.system_lock);
	g_keypad.power_state = FSR_POWERSTATE_OFF;
	fsr_i2c_poweron(&g_keypad);
	msleep(100);
	clear_keys(&g_keypad);
	//get a new firmware version and everything else
	fsr_get_fw_config(&g_keypad);

	return ret;
}

static ssize_t fsr_keypad_debug_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	if (sscanf(buf, "%d", &keypad->debug) <= 0)
		return -EINVAL;
	if (keypad->debug < 0)
		keypad->debug = 0;
	if (keypad->debug > 1)
		keypad->debug = 1;
	return size;
}

static ssize_t fsr_keypad_debug_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	fsr_get_fw_config(keypad);

	return sprintf(buf, "%d\n", keypad->debug);
}

static ssize_t fsr_keypad_haponup_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	if (sscanf(buf, "%d", &keypad->trigger_on_up) <= 0)
		return -EINVAL;
	if (keypad->trigger_on_up < 0)
		keypad->trigger_on_up = 0;
	if (keypad->trigger_on_up > 1)
		keypad->trigger_on_up = 1;
	return size;
}

static ssize_t fsr_keypad_haponup_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%d\n", keypad->trigger_on_up);
}

static ssize_t fsr_keypad_adc_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	uint16_t adc[6];
	int ret;

	CHECK_RET(ret = fsr_switch_mode_diags(keypad));
	fsr_disable_irq_nosync(&g_keypad);

	if (fsr_keypad_get_adc(keypad, adc, keypad->static_config.n_adc) < 0) {
		fsr_enable_irq(&g_keypad);
		return -EIO;
	}
	fsr_enable_irq(&g_keypad);

	CHECK_RET(ret = fsr_switch_mode(keypad, ACTIVE_MODE_NORMAL));

	return sprintf(buf, "%4d %4d %4d %4d, %4d %4d\n", adc[0], adc[1], adc[2], adc[3], adc[4], adc[5] );
}

static irqreturn_t fsr_bootloader_irq(int irq, void *dev)
{
	struct fsr_keypad *keypad = dev;
	if(keypad->device_mode == KL05_APP_MODE) {
		//watchdog just reset the MCU, let's reinitialize it.
		printk(KERN_ERR "fsr:kl05 watchdog reset!!");
		clear_keys(keypad);
		keypad->power_state = FSR_POWERSTATE_OFF;
		keypad->device_mode = KL05_BL_MODE;
		schedule_work(&keypad->restart_work);
	}

	return IRQ_HANDLED;
}

static int fsr_wdog_irq_en(struct fsr_keypad *keypad, int en)
{
	if(keypad->wdog_irq == 0 && en ) {
		keypad->wdog_irq = gpio_fsr_bootloader_irq();
		return request_threaded_irq(keypad->wdog_irq, NULL, fsr_bootloader_irq, FSR_IRQ_TYPE, "fsr-bootloader", keypad);

	} else if(keypad->wdog_irq != 0 && !en){
		free_irq(keypad->wdog_irq, keypad);
		keypad->wdog_irq = 0;
	}
	return 0;
}

static ssize_t fsr_write_not_implemented(struct file *file, const char __user *buf,
                                 size_t count, loff_t *pos)
{
  return 0;
}

static ssize_t fsr_read_not_implemented(struct file *file, char __user *buf,
                                size_t count, loff_t *pos)
{
  return 0;
}

/*
 *
 * get FSR_IOCTL_DATA_SIZE (8) sets of ADC value filled into data
 * */
static int fsr_ioctl_get_data(struct fsr_data_st* st)
{
	int i;
	int ret;
	st->num_valid_data=0;

	CHECK_RET(ret = fsr_switch_mode_diags(&g_keypad));
	//throw away stale data from last read
	fsr_keypad_get_adc(&g_keypad, (u16*)&(st->data[0]), g_keypad.static_config.n_adc);

	for (i =0; i< FSR_IOCTL_DATA_SIZE; i++) {
		ret = fsr_keypad_get_adc(&g_keypad, (u16*)&(st->data[i]), g_keypad.static_config.n_adc);
		if(0 == ret)
			st->num_valid_data++;
		else
			return ret;
	}

	return 0;
}

static int kl05_store_calib_data(struct fsr_calib_data* data_st)
{
	u8 cal_header_adc[CALIB_DATA_SIZE];
	int ret = -EINVAL;
	u32 addr = MCU_CALIB_DATA_ADDR;
	int n_valid_calib_data = 0;
	int i;
	u8 readback_retbuf[CALIB_DATA_SIZE + 1];

	for (i = 0; i < 8; i++) {
		if( data_st->left_top[i]     != 0x0 && data_st->left_top[i]     != 0xBEEF &&
			data_st->left_bottom[i]  != 0x0 && data_st->left_bottom[i]  != 0xBEEF &&
			data_st->right_top[i]    != 0x0 && data_st->right_top[i]    != 0xBEEF &&
			data_st->right_bottom[i] != 0x0 && data_st->right_bottom[i] != 0xBEEF )
			n_valid_calib_data++;
	}
	printk(KERN_INFO "fsr_keypad:valid calibration data:%d\n", n_valid_calib_data);
	if(n_valid_calib_data < 2) {
		printk(KERN_ERR "fsr_keypad: calibration data is not valid!!");
		return -EFAULT;
	}
	kl05_hw_reset(&g_keypad);

	cal_header_adc[0] = N_BUTTONS; //4 buttons
	cal_header_adc[1] = n_valid_calib_data;
	cal_header_adc[2] = 1; //1 ADC values per weight
	switch(n_valid_calib_data) {
	case 3:
		*((u16*)(cal_header_adc+3)) = 0;
		*((u16*)(cal_header_adc+5)) = 250;
		*((u16*)(cal_header_adc+7)) = 500;
		*((u16*)(cal_header_adc+9)) = 0;
		*((u16*)(cal_header_adc+11)) = 0;
		*((u16*)(cal_header_adc+13)) = 0;
		*((u16*)(cal_header_adc+15)) = 0;
		*((u16*)(cal_header_adc+17)) = 0;
		break;
	case 8:
	default:
		*((u16*)(cal_header_adc+3)) = 0;
		*((u16*)(cal_header_adc+5)) = 50;
		*((u16*)(cal_header_adc+7)) = 150;
		*((u16*)(cal_header_adc+9)) = 250;
		*((u16*)(cal_header_adc+11)) = 350;
		*((u16*)(cal_header_adc+13)) = 500;
		*((u16*)(cal_header_adc+15)) = 750;
		*((u16*)(cal_header_adc+17)) = 1000;
		break;
	}
	memcpy(cal_header_adc + CALIB_HEADER_SIZE, (u8*)data_st, sizeof(struct fsr_calib_data));

	CHECK_RET( ret = kl05_bl_write_block(g_keypad.i2c_fsr, addr, cal_header_adc, CALIB_DATA_SIZE));

	CHECK_RET( ret = kl05_bl_cmd_read(g_keypad.i2c_fsr, (u8*)&addr, CALIB_DATA_SIZE, readback_retbuf));

	if(memcmp(cal_header_adc, readback_retbuf+1, CALIB_DATA_SIZE)) {
		printk(KERN_ERR "%s:read back does not match write buffer", __func__);

		printbuf(cal_header_adc, CALIB_DATA_SIZE, 'W');

		printbuf(readback_retbuf+1, CALIB_DATA_SIZE, 'R');

		return -EFAULT;
	}

	return ret;
}

static int fsr_keypad_get_pressure(struct fsr_keypad *keypad, u16 *pressure_down_fw, u16 * pressure_down_back)
{
	struct i2c_client *i2c = keypad->i2c_fsr;
	u8 reg[2];
	int ret;

	fsr_wake_pta5(1);

	CHECK_RET(ret = fsr_switch_mode(keypad, ACTIVE_MODE_CONFIG));

	/* Read pressure down value */
	CHECK_RET(ret = fsr_i2c_read_block_data(i2c, REG_C_OPT1, 2, reg));
	*pressure_down_fw = (reg[1] << 8) | reg[0];

	/* Read pressure up value */
	CHECK_RET(ret = fsr_i2c_read_block_data(i2c, REG_C_OPT3, 2, reg));
	*pressure_down_back = (reg[1] << 8) | reg[0];

	CHECK_RET(ret = fsr_switch_mode(keypad, ACTIVE_MODE_NORMAL));

	fsr_wake_pta5(0);

	printk(KERN_INFO "Pressure down: [%d, %d]\n", *pressure_down_fw, *pressure_down_back);
	return 0;
}

// caller of this function is responsible for calling fsr_wake_pta5(1) before calling this function
static int fsr_set_option(struct fsr_keypad *keypad, u8 option, u16 value)
{
	struct i2c_client *i2c = keypad->i2c_fsr;
	u8 reg[3];
	int ret;
	reg[0] = option;
	reg[1] = (((u8*)&value)[0]);
	reg[2] = (((u8*)&value)[1]);

	ret = fsr_i2c_write_block_data_no_addr(i2c, 3, reg);
	return ret;
}

static int fsr_keypad_set_pressure(struct fsr_keypad *keypad, int pressure_down_fw, int pressure_up_fw, int pressure_down_back, int pressure_up_back)
{
	int ret = 0;

	fsr_wake_pta5(1);
	CHECK_NO_RET( ret += fsr_switch_mode(keypad, ACTIVE_MODE_CONFIG));
	CHECK_NO_RET( ret += fsr_set_option(keypad, REG_C_OPT1, pressure_down_fw));
	CHECK_NO_RET( ret += fsr_set_option(keypad, REG_C_OPT2, pressure_up_fw));
	CHECK_NO_RET( ret += fsr_switch_mode(keypad, ACTIVE_MODE_NORMAL));
	keypad->fw_state.pressure_down_fw = pressure_down_fw;
	keypad->fw_state.pressure_up_fw = pressure_up_fw;

	keypad->fw_state.pressure_down_back = pressure_down_back;
	keypad->fw_state.pressure_up_back = pressure_up_back;
	fsr_wake_pta5(0);

	return ret;
}

static void fsr_restore_fw_state(struct fsr_keypad *keypad)
{
	/* If pressure has been set, restores pressure settings */
	int pressure_down_fw   = keypad->fw_state.pressure_down_fw;
	int pressure_up_fw     = keypad->fw_state.pressure_up_fw;
	int pressure_down_back = keypad->fw_state.pressure_down_back;
	int pressure_up_back   = keypad->fw_state.pressure_up_back;

	fsr_wake_pta5(1);

	if(pressure_down_fw && pressure_up_fw && pressure_down_back && pressure_up_back) {
		fsr_keypad_set_pressure(keypad, pressure_down_fw, pressure_up_fw, pressure_down_back, pressure_up_back);
	}

	fsr_wake_pta5(0);
}

static long fsr_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret = 0;
	struct fsr_data_st data_st;
	struct fsr_calib_data calib_data;

	switch (cmd) {
		case FSR_IOCTL_SET_PREV_ENABLE:
		{
			int enable = 0;
			if (g_keypad.exlock)
				break;

			if (copy_from_user(&enable, argp, sizeof(int)))
				return -EFAULT;

			g_keypad.enable_prev = !!enable;
			break;
		}
		case FSR_IOCTL_GET_PREV_ENABLE:
		{
			int enable = g_keypad.enable_prev;

			if (copy_to_user(argp, &enable, sizeof(int)))
				ret = -EFAULT;
			break;
		}
		case FSR_IOCTL_SET_NEXT_ENABLE:
		{
			int enable = 0;
			if (g_keypad.exlock)
				break;

			if (copy_from_user(&enable, argp, sizeof(int)))
				return -EFAULT;

			g_keypad.enable_next = !!enable;
			break;
		}
		case FSR_IOCTL_GET_NEXT_ENABLE:
		{
			int enable = g_keypad.enable_next;

			if (copy_to_user(argp, &enable, sizeof(int)))
				ret = -EFAULT;
			break;
		}
		case FSR_IOCTL_GET_DATA:
		{
			if(g_keypad.device_mode == KL05_APP_MODE) {
				fsr_disable_irq_nosync(&g_keypad);
				if(fsr_ioctl_get_data(&data_st) == 0) {
					if (copy_to_user(argp, &data_st, sizeof(struct fsr_data_st)))
						ret = -EFAULT;
				} else
					ret = -EIO;
				fsr_enable_irq(&g_keypad);
			} else
				ret = -EIO;
			break;
		}
		case FSR_IOCTL_STORE_CALIB:
		{
			fsr_disable_irq_nosync(&g_keypad);
			if(copy_from_user(&calib_data, argp, sizeof(struct fsr_calib_data))) {
				fsr_enable_irq(&g_keypad);
				return -EFAULT;
			}
			ret = kl05_store_calib_data(&calib_data);
			fsr_enable_irq(&g_keypad);
			// Make sure the irq is enabled back when the store calib failed.
			// Ex: if the calibration data is not valid, it returns error
			// but it still needs to enable the irq back.
			// Not sure if it can enable the irq before storing calib data.
			 if(unlikely(ret)) {
				 printk(KERN_ERR "%s: %d failed with %d", __func__, __LINE__, ret);
				 return ret;
			}
			g_keypad.power_state = FSR_POWERSTATE_OFF;
			CHECK_RET(ret = fsr_i2c_poweron(&g_keypad));
			break;
		}
		case FSR_IOCTL_GET_HAPONUP:
		{
			if (copy_to_user(argp, &g_keypad.trigger_on_up, sizeof(g_keypad.trigger_on_up)))
				return -EFAULT;
			break;
		}
		case FSR_IOCTL_SET_HAPONUP:
		{
			u32 trigger_on_up;

			if (copy_from_user(&trigger_on_up, argp, sizeof(trigger_on_up)))
				return -EFAULT;
			g_keypad.trigger_on_up = trigger_on_up < 1 ? 0 : 1;
			break;
		}
		case FSR_IOCTL_GET_REPEAT:
		{
			if (copy_to_user(argp, &g_keypad.repeat, sizeof(g_keypad.repeat)))
				return -EFAULT;
			break;
		}
		case FSR_IOCTL_SET_REPEAT:
		{
			if (copy_from_user(&g_keypad.repeat, argp, sizeof(g_keypad.repeat)))
				return -EFAULT;
			g_keypad.input->rep[REP_DELAY]  = g_keypad.repeat.delay;
			g_keypad.input->rep[REP_PERIOD] = g_keypad.repeat.period;
			if (g_keypad.repeat.enabled)
				__set_bit(EV_REP, g_keypad.input->evbit);
			else
				__clear_bit(EV_REP, g_keypad.input->evbit);
			cancel_work_sync(&g_keypad.fsr_work_repeat);
			break;
		}
		case KEYPAD_IOCTL_GET_LOCK:
		{
			int locked = g_keypad.exlock;
			if (copy_to_user(argp, &locked, sizeof(locked)))
				return -EFAULT;
			break;
		}
		case KEYPAD_IOCTL_SET_LOCK:
		{
			int sts;
			int locked;
			if (copy_from_user(&locked, argp, sizeof(locked)))
				return -EFAULT;
			if (g_keypad.exlock == (bool)locked)
				break;
			if (locked > 0)
			{
				sts = fsr_i2c_poweroff(&g_keypad);
				if (sts < 0){
					printk(KERN_ERR "fsr:proc:command=lock:not succeed please retry\n");
					return -EBUSY;
				}
				g_keypad.exlock = true;
			}
			else
			{
				sts = fsr_i2c_poweron(&g_keypad);
				if (sts < 0) {
					printk(KERN_ERR "fsr:proc:command=unlock:not succeed please retry\n");
					return -EBUSY;
				}
				g_keypad.exlock = false;
			}
			break;
		}
		case KEYPAD_IOCTL_EVENT_ENABLE:
		{
			int enable = 1;

			if (copy_from_user(&enable, argp, sizeof(int)))
				return -EFAULT;

			g_keypad.event_enable = !!enable;
			break;
		}
		case KEYPAD_IOCTL_TOUCH_ACTIVE:
		{
			int touch_active;

			if (copy_from_user(&touch_active, argp, sizeof(int)))
				return -EFAULT;
			g_keypad.touch_active = !!touch_active;
			break;
		}
		case FSR_IOCTL_GET_PRESSURE:
		{

			u16 pressure_down = 0;
			u16 pressure_up   = 0;
			u32 prdown_retval = FSR_DEF_PRESSURE;

			if (g_keypad.exlock)
				break;
			ret = fsr_keypad_get_pressure(&g_keypad, &pressure_down, &pressure_up);

			prdown_retval = pressure_down;
			if (copy_to_user(argp, &prdown_retval, sizeof(prdown_retval)))
				return -EFAULT;
			break;
		}
		case FSR_IOCTL_SET_PRESSURE:
		{
			u32 pressure;
			u16 prdown;

			if (g_keypad.exlock)
				break;

			if (copy_from_user(&pressure, argp, sizeof(pressure)))
				return -EFAULT;
			prdown = (u16)pressure;

			mutex_lock(&g_keypad.system_lock);
			ret = fsr_keypad_set_pressure(&g_keypad, prdown, (prdown/2), prdown, (prdown/2) );
			mutex_unlock(&g_keypad.system_lock);
			break;
		}
		default:
			printk(KERN_ERR "ioctl no case !!\n");
			ret = -ENOTTY;
			break;
	}
	return ret;
}

static const struct file_operations fsr_fops =
{
  .owner = THIS_MODULE,
  .read  = fsr_read_not_implemented,
  .write = fsr_write_not_implemented,
  .unlocked_ioctl = fsr_ioctl,
};

static struct miscdevice fsr_misc_device =
{
  .minor = 167,
  .name  = KEYPAD_MISC_DEV_NAME,
  .fops  = &fsr_fops,
};

//caller of this function is responsible to call fsr_wake_pta5(1); before calling this function
static int fsr_switch_mode(struct fsr_keypad *keypad, u8 mode)
{
	int ret = 0;
	int retry = FSR_I2C_MAX_RETRY;
	struct i2c_client *i2c = keypad->i2c_fsr;
	u8 reg[2];

	if (mode > ACTIVE_MODE_DIAGS) {
		printk(KERN_ERR "cannot set to mode %d !!\n", mode);
		return -EINVAL;
	}
	do {
		reg[0] = REG_ACTIVE_MODE;
		reg[1] = mode;
		ret = fsr_i2c_write_block_data_no_addr(i2c, 2, reg);
		if (ret < 0)
			msleep(FSR_I2C_RETRY_DELAY_MS);
	} while(ret < 0 && retry--);

	return ret;

}

static int fsr_switch_mode_diags(struct fsr_keypad * keypad)
{
	int ret;
	fsr_wake_pta5(1);
	ret = fsr_switch_mode(keypad, ACTIVE_MODE_DIAGS);
	fsr_wake_pta5(0);
	return ret;
}

static ssize_t mcu_test_simple_cmd_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	char* ptr = buf;
	ptr += sprintf(ptr, "echo 51 for exit bootloader");
	ptr += sprintf(ptr, "echo 53 for soft reset");
	ptr += sprintf(ptr, "echo 02 for hook command");

	return ptr-buf;
}

static ssize_t mcu_test_simple_cmd_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	struct i2c_client* i2c = keypad->i2c_fsr;
	int ret;
	u8 retbuf[20];
	int cmd;

	sscanf(buf, "%x", &cmd);

	printk("cmd=0x%x", cmd);

	ret = kl05_simple_fc_cmd(i2c, cmd, retbuf, 20);

	printbuf(retbuf, 2, 's');

	printk("end %s, ret=%d\n", __func__, ret);

	return size;
}

static ssize_t mcu_cmd_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	char* ptr = (char*)buf;
	int loop = size / 3; // assuming 2 bytes of each hex follow by a space
	u8 checksum;
	int i;

	memset(g_sent_cmd, 0x0, sizeof(g_sent_cmd));

	g_sent_cmd[0] = simple_strtol(buf, &ptr, 16);
	checksum = g_sent_cmd[0];
	for (i = 1; i < loop; i++) {
		g_sent_cmd[i] = simple_strtol(ptr+1, &ptr, 16);
		checksum += g_sent_cmd[i];
	}
	g_sent_cmd[i] = checksum;

	printk(KERN_ERR "%x %x %x %x %x %x %x, checksum=%x",
		g_sent_cmd[0], g_sent_cmd[1], g_sent_cmd[2], g_sent_cmd[3], g_sent_cmd[4], g_sent_cmd[5], g_sent_cmd[6], g_sent_cmd[i]);

	return size;
}

static ssize_t mcu_cmd_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	int i;
	char* ptr = buf;

	int ret1, ret2;
	int cmd_size = g_sent_cmd[3] << 24 | g_sent_cmd[2] << 16 | g_sent_cmd[1] << 8 | g_sent_cmd[0];

	printk(KERN_ERR "cmd_size=%d", cmd_size);
	ret1 = fsr_i2c_write_block_data_no_addr(keypad->i2c_fsr, cmd_size, g_sent_cmd);
	msleep(CMD_DELAY);

	ret2 = fsr_i2c_read_block_data_no_addr(keypad->i2c_fsr, sizeof(g_retcmd), g_retcmd);

	printk(KERN_ERR "ret1=%d ret2=%d\n", ret1, ret2);
	ptr = buf;
	for (i = 0; i < sizeof(g_retcmd); i++ ) {
		ptr += sprintf(ptr, "%02x, ", g_retcmd[i]);
		if((i+1) % 4 == 0)
			ptr += sprintf(ptr, "\n");
	}

	memset(g_retcmd, 0x0, sizeof(g_retcmd));
	return ptr - buf;
}

static void kl05_firmware_cont(const struct firmware* fw, void* context)
{
	struct fsr_keypad *keypad = (struct fsr_keypad*)context;
	int lines;
	lines = *(int*)((u8*)fw->data + fw->size - sizeof(int));

	kl05_bl_update_firmware(keypad->i2c_fsr, (u8*)fw->data, lines);

	return;
}

static int kl05_load_firmware(struct fsr_keypad* keypad)
{
	int ret;

	ret = request_firmware_nowait(THIS_MODULE,
		FW_ACTION_NOHOTPLUG, "fsr-kl05", keypad->dev,
		GFP_KERNEL, keypad, kl05_firmware_cont);

	return ret;
}

static int kl05_update_bootloader(void)
{
	u8 len;
	u8 addr[4] = {0};
	u8 retbuf[128];
	int i;
	u8* ptr = bootloader_inception_img;
	int num_rec = bootloader_inception_lines;
	int ret = 0;
	int retry = 0;
	struct i2c_client* i2c = g_keypad.i2c_fsr;

	printk(KERN_INFO "%s enter..", __func__);

	fsr_disable_irq(&g_keypad);

	printk(KERN_INFO "%s: reset to original bootloader \n", __func__);
	kl05_hw_reset(&g_keypad);
	printk(KERN_INFO "%s: erasing blocks in app region..\n", __func__);
	CHECK_RET(ret = kl05_erase_blocks(i2c, MCU_APP_START_ADDR, MCU_ERASE_END_ADDR));
	printk(KERN_INFO "%s: sending blocks..\n", __func__);

	for (i = 0; i < num_rec; i++)
	{
		len = ptr[0];
		addr[0] = ptr[2];
		addr[1] = ptr[1];
		addr[2] = 0;
		addr[3] = 0;
		ret = kl05_bl_cmd_write(i2c, addr, len-3, ptr+3, retbuf);
		retry = 0;
		while((ret || retbuf[1] != 0xFC) && retry++ < G_RETRY_CT) {
			printk(KERN_ERR "last cmd failed, ret=%d, retry=%d\n", ret, retry);
			msleep(10);
			ret = kl05_bl_cmd_write(i2c, addr, len-3, ptr+3, retbuf);
		}
		if(retry > G_RETRY_CT) {
			printk(KERN_ERR "%s retry too many times ...\n", __func__);
			return -EIO;

		}
		printbuf(retbuf, len+2, 'b');
		ptr += len;
	}

	CHECK_RET(ret = kl05_simple_fc_cmd(i2c, KL05_BL_CMD_EXIT_BL, NULL, 0));

	printk(KERN_INFO "%s: now we are in bootloader inception\n", __func__);

	printk(KERN_INFO "%s: erasing blocks in bootloader region..\n", __func__);
	CHECK_RET(ret = kl05_erase_blocks(i2c, 0x0, 0x7ff));
	printk(KERN_INFO "%s: sending blocks..\n", __func__);

	num_rec = bootloader_lines;
	ptr = bootloader_img;

	for (i = 0; i < num_rec; i++)
	{
		len = ptr[0];
		addr[0] = ptr[2];
		addr[1] = ptr[1];
		addr[2] = 0;
		addr[3] = 0;
		ret = kl05_bl_cmd_write(i2c, addr, len-3, ptr+3, retbuf);
		retry = 0;
		while((ret || retbuf[1] != 0xFC) && retry++ < G_RETRY_CT) {
			printk(KERN_ERR "last cmd failed, ret=%d, retry=%d\n", ret, retry);
			msleep(10);
			ret = kl05_bl_cmd_write(i2c, addr, len-3, ptr+3, retbuf);
		}
		if(retry > G_RETRY_CT) {
			printk(KERN_ERR "%s retry too many times ...\n", __func__);
			return -EIO;

		}
		printbuf(retbuf, len+2, 'b');
		ptr += len;
	}

	printk(KERN_INFO "%s done..", __func__);
	g_keypad.power_state = FSR_POWERSTATE_OFF;
	return fsr_i2c_poweron(&g_keypad);
}

static ssize_t mcu_firmware_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	int ret;

	printk(KERN_INFO "enter %s\n", __func__);

	ret = kl05_load_firmware(keypad);

	printk(KERN_INFO "end %s, ret=%d\n", __func__, ret);
	return size;
}

static ssize_t mcu_hardware_reset_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	if(buf[0] == '1')
		kl05_hw_reset(keypad);
	else
		gpio_fsr_reset();
	return size;
}

static ssize_t reprogram_bootloader_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	if(0 == strncmp(buf, "inception", 9))
		kl05_update_bootloader();

	return size;
}

static ssize_t mcu_device_mode_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	int ret = 0;
	switch (keypad->device_mode) {
		case KL05_BL_MODE:
			ret += sprintf(buf, "BOOTLOADER MODE," );
		break;
		case KL05_APP_MODE:
			ret += sprintf(buf, "APP MODE," );
		break;
		default:
			ret += sprintf(buf, "UNKNOWN MODE," );
		break;
	}
	switch (keypad->power_state) {
		case FSR_POWERSTATE_OFF:
			ret += sprintf(buf+ret, "OFF" );
		break;
		case FSR_POWERSTATE_ON:
			ret += sprintf(buf+ret, "ON" );
		break;
		case FSR_POWERSTATE_OFF_PENDING:
			ret += sprintf(buf+ret, "OFF_PENDING" );
		break;
		case FSR_POWERSTATE_ON_PENDING:
			ret += sprintf(buf+ret, "ON_PENDING" );
		break;
		default:
			ret += sprintf(buf+ret, "unknown" );
		break;
	}
	return ret;
}

static ssize_t mcu_set_pin_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	int pin;
	int enable;

	if (sscanf(buf, "%d %d", &pin, &enable) <= 2)
		return -EINVAL;

	printk(KERN_DEBUG "pin %d, enable %d\n", pin, enable);

	fsr_set_pin(pin, enable);

	return size;
}

static ssize_t mcu_set_pin_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{

	 return sprintf(buf, " echo ""pin,enable"" to this entry , such as ""9,0"" available entries are:\n"
					"8: MX6SL_ARM2_EPDC_SDDO_8 \n "
					"9: MX6SL_ARM2_EPDC_SDDO_9 \n "
					"10: MX6SL_ARM2_EPDC_SDDO_10 \n "
					"11: MX6SL_ARM2_EPDC_SDDO_11 \n "
					"12: MX6SL_ARM2_EPDC_SDDO_12 \n "
					"13: MX6SL_PIN_KEY_COL1 \n ");
}

static ssize_t save_cali_data_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	struct i2c_client* i2c = keypad->i2c_fsr;
	int i;
	u8 cal_header[83];

	char* ptr = buf;

	kl05_verify_calib_data_special(i2c, cal_header);

	if(keypad->debug) {
		ptr += sprintf(ptr, "\n===================\n");
		for(i = 0; i < sizeof(cal_header); i++)
		{
			ptr += sprintf(ptr , "%02x ", cal_header[i]);
			if((i+1) % 4 == 0)
				ptr += sprintf(ptr , "\n");
		}
	}
	ptr += sprintf(ptr, "\n%d %d %d\n", cal_header[0], cal_header[1], cal_header[2]);
	ptr += sprintf(ptr, "%hu %hu %hu %hu %hu %hu %hu %hu",	*(u16*)(cal_header+3),
																*(u16*)(cal_header+5),
																*(u16*)(cal_header+7),
																*(u16*)(cal_header+9),
																*(u16*)(cal_header+11),
																*(u16*)(cal_header+13),
																*(u16*)(cal_header+15),
																*(u16*)(cal_header+17));


	for(i = 19; i < sizeof(cal_header); i+=2)
	{
		if(((i-19)/2) % 4 == 0)
			ptr += sprintf(ptr , "\n");
		ptr += sprintf(ptr , "%5hu ", *(u16*)(cal_header+i));
	}
	return ptr - buf;
}

static ssize_t cal_data_valid_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	struct i2c_client* i2c = keypad->i2c_fsr;
	int ret = 0;
	u8 valid;

	mutex_lock(&keypad->system_lock);
	fsr_wake_pta5(1);
	ret += fsr_switch_mode(keypad, ACTIVE_MODE_NORMAL);
	ret += fsr_i2c_read_block_data(i2c, REG_N_CAL_VALID, 1, &valid);
	fsr_wake_pta5(0);
	mutex_unlock(&keypad->system_lock);
	if(ret == 0)
		ret = sprintf(buf, "calibration data is %svalid", valid ? "" : "NOT ");
	
	return ret;
}

static ssize_t fsr_irq_sysentry_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	if(buf[0] == '0') {
		fsr_disable_irq(&g_keypad);
	} else {
		fsr_enable_irq(&g_keypad);
	}
	return size;
}

static ssize_t kl05_app_ver_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	return sprintf(buf, "0x%02x%02x", keypad->static_config.app_ver[0], keypad->static_config.app_ver[1]);
}

static ssize_t fsr_key_mapping_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	return sprintf(buf, "%lud %lud %lud %lud", keypad->kmap[0].keyval,keypad->kmap[1].keyval,keypad->kmap[2].keyval,keypad->kmap[3].keyval);
}

static void init_keymap(struct fsr_keypad *keypad)
{
	keypad->kmap[0].keyval = KEY_PAGEUP;
	keypad->kmap[0].state  = FSR_KEY_RELEASED;
	keypad->kmap[1].keyval = KEY_PAGEDOWN;
	keypad->kmap[1].state  = FSR_KEY_RELEASED;
	keypad->kmap[2].keyval = KEY_PAGEUP;
	keypad->kmap[2].state  = FSR_KEY_RELEASED;
	keypad->kmap[3].keyval = KEY_PAGEDOWN;
	keypad->kmap[3].state  = FSR_KEY_RELEASED;
}

static void init_keymap_lf_rb(struct fsr_keypad *keypad)
{
	keypad->kmap[0].keyval = KEY_PAGEDOWN;
	keypad->kmap[0].state  = FSR_KEY_RELEASED;
	keypad->kmap[1].keyval = KEY_PAGEDOWN;
	keypad->kmap[1].state  = FSR_KEY_RELEASED;
	keypad->kmap[2].keyval = KEY_PAGEUP;
	keypad->kmap[2].state  = FSR_KEY_RELEASED;
	keypad->kmap[3].keyval = KEY_PAGEUP;
	keypad->kmap[3].state  = FSR_KEY_RELEASED;
}

static void init_keymap_lb_rf(struct fsr_keypad *keypad)
{
	keypad->kmap[0].keyval = KEY_PAGEUP;
	keypad->kmap[0].state  = FSR_KEY_RELEASED;
	keypad->kmap[1].keyval = KEY_PAGEUP;
	keypad->kmap[1].state  = FSR_KEY_RELEASED;
	keypad->kmap[2].keyval = KEY_PAGEDOWN;
	keypad->kmap[2].state  = FSR_KEY_RELEASED;
	keypad->kmap[3].keyval = KEY_PAGEDOWN;
	keypad->kmap[3].state  = FSR_KEY_RELEASED;
}

static void init_keymap_all_fw(struct fsr_keypad *keypad)
{
	keypad->kmap[0].keyval = KEY_PAGEDOWN;
	keypad->kmap[0].state  = FSR_KEY_RELEASED;
	keypad->kmap[1].keyval = KEY_PAGEDOWN;
	keypad->kmap[1].state  = FSR_KEY_RELEASED;
	keypad->kmap[2].keyval = KEY_PAGEDOWN;
	keypad->kmap[2].state  = FSR_KEY_RELEASED;
	keypad->kmap[3].keyval = KEY_PAGEDOWN;
	keypad->kmap[3].state  = FSR_KEY_RELEASED;
}

static ssize_t fsr_key_mapping_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	switch(buf[0])
	{
		case '1':
		init_keymap(keypad);
		break;
		case '2':
		init_keymap_lb_rf(keypad);
		break;
		case '3':
		init_keymap_lf_rb(keypad);
		break;
		case '4':
		init_keymap_all_fw(keypad);
		break;
		default:
		printk(KERN_ERR "unknown keymapping..");
		break;
	}
	return size;
}

static ssize_t fsr_repeat_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	int enabled = keypad->repeat.enabled;
	int delay = keypad->repeat.delay;
	int period = keypad->repeat.period;

	if (sscanf(buf, "%d %d %d", &enabled, &delay, &period) < 1)
		return -EINVAL;

	if (!enabled)
	{
		__clear_bit(EV_REP, keypad->input->evbit);
		cancel_work_sync(&keypad->fsr_work_repeat);
		keypad->repeat.enabled = false;
	}
	else
	{
		__set_bit(EV_REP, keypad->input->evbit);
		keypad->repeat.enabled = true;
		keypad->input->rep[REP_DELAY]  = delay;
		keypad->input->rep[REP_PERIOD] = period;
	}
	return size;
}

static ssize_t fsr_repeat_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	return sprintf(buf, "[enable] [delay] [period]\n%d %d %d\n",
			keypad->repeat.enabled, keypad->repeat.delay, keypad->repeat.period);
}

static ssize_t fsr_keypad_pressure_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	int fw_down;
	int fw_up;
	int ret;
	int num;

	if ((num=sscanf(buf, " %d %d", &fw_down, &fw_up)) <= 0)
		return -EINVAL;

	if (num < 2)
		return -EINVAL;

	printk(KERN_INFO "Got pressure : %d %d\n", fw_down, fw_up);
	mutex_lock(&keypad->system_lock);
	ret = fsr_keypad_set_pressure(keypad, fw_down, fw_up, fw_down, fw_up);
	mutex_unlock(&keypad->system_lock);
	if (ret < 0)
	{
		return -EIO;
	}
	return size;
}

static ssize_t fsr_keypad_pressure_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);
	u16 pressure_down_fw = 0;
	u16 pressure_down_back = 0;

	if (fsr_keypad_get_pressure(keypad, &pressure_down_fw, &pressure_down_back) < 0)
		return -EIO;

	return sprintf(buf, "%d\n", pressure_down_fw);
}

extern void config_i2c3_gpio_input(int enable);
extern int gpio_i2c3_fault(void);
extern void gpio_i2c3_scl_toggle(void);

static ssize_t i2c_toggle_recovery_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t size)
{
	config_i2c3_gpio_input(1);
	if(gpio_i2c3_fault())
	{	
		printk(KERN_ERR "fault detected!!");
		gpio_i2c3_scl_toggle();
	}
	config_i2c3_gpio_input(0);
	
	return size;
}

static struct device_attribute attributes[] = {
	__ATTR(reprogram_bootloader, S_IWUSR,
		NULL, reprogram_bootloader_store),
	__ATTR(key_mapping, S_IWUSR| S_IRUSR,
		fsr_key_mapping_show, fsr_key_mapping_store),
	__ATTR(repeat, S_IWUSR| S_IRUSR,
		fsr_repeat_show, fsr_repeat_store),
	__ATTR(app_ver, S_IRUSR,
		kl05_app_ver_show, NULL),
	__ATTR(cal_data_valid, S_IRUSR,
		cal_data_valid_show, NULL),
	__ATTR(irq, S_IWUSR,
		NULL, fsr_irq_sysentry_store),
	__ATTR(calib_data, S_IRUSR,
		save_cali_data_show, NULL),
	__ATTR(mcu_set_pin, S_IWUSR| S_IRUSR,
		mcu_set_pin_show, mcu_set_pin_store),
	__ATTR(device_mode, S_IRUSR,
		mcu_device_mode_show, NULL),
	__ATTR(hw_reset, S_IWUSR,
		NULL, mcu_hardware_reset_store),
	__ATTR(mcu_firmware, S_IWUSR,
		NULL, mcu_firmware_store),
	__ATTR(mcu_test_simple_cmd, S_IWUSR | S_IRUSR,
		mcu_test_simple_cmd_show, mcu_test_simple_cmd_store),
	__ATTR(mcu_cmd, S_IRUSR | S_IWUSR,
		mcu_cmd_show, mcu_cmd_store),
	__ATTR(haponup, S_IRUSR | S_IWUSR,
		fsr_keypad_haponup_show, fsr_keypad_haponup_store),
	__ATTR(debug, S_IRUSR | S_IWUSR,
		fsr_keypad_debug_show, fsr_keypad_debug_store),
	__ATTR(reg, S_IRUSR | S_IWUSR,
			fsr_keypad_reg_show, fsr_keypad_reg_store),
	__ATTR(adc, S_IRUSR ,
			fsr_keypad_adc_show, NULL),
	__ATTR(pressure, S_IRUSR | S_IWUSR,
		fsr_keypad_pressure_show, fsr_keypad_pressure_store),
	__ATTR(i2c_toggle_recovery, S_IWUSR,
		NULL, i2c_toggle_recovery_store),
};

static int fsr_keypad_proc_read(char *page, char **start, off_t off,
		int count, int *eof, void *data);
static ssize_t fsr_keypad_proc_write( struct file *filp, const char __user *buff,
		unsigned long len, void *data );

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
	{
		if (device_create_file(dev, attributes + i))
			goto undo;
	}

	g_keypad.proc_entry = create_proc_entry("keypad", 0644, NULL );
	if (g_keypad.proc_entry == NULL) {
		dev_err(dev, "create_proc: could not create proc entry\n");
		goto undo;
	}
	g_keypad.proc_entry->read_proc = fsr_keypad_proc_read;
	g_keypad.proc_entry->write_proc = fsr_keypad_proc_write;

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
	if(g_keypad.proc_entry)	{
		remove_proc_entry("keypad", NULL);
		g_keypad.proc_entry = NULL;
	}
}

static void clear_keys(struct fsr_keypad *keypad)
{
	int i;

	for (i = 0; i < FSR_KEYMAP_SIZE; i++)
	{
		if (keypad->kmap[i].state == FSR_KEY_PRESSED)
		{
			input_report_key(keypad->input, keypad->kmap[i].keyval, 0);
			keypad->kmap[i].state = FSR_KEY_RELEASED;
		}
	}
	input_sync(keypad->input);
//	printk(KERN_INFO "fsr: %s", __func__);
}

static void send_user_event(struct input_dev *input, u8 prev, u8 cur)
{
  struct device *dev = &input->dev;

#ifdef CONFIG_CPU_FREQ_OVERRIDE_LAB126
  if (cur > 0)
  {
    cpufreq_override(1);
  }
#endif

  // No more touches. Tell user-space.
  if (cur == 0 && prev > 0)
  {
    char *envp[] = {"Touch=notouch", NULL};
    kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
  }
  else if (cur > 0 && prev == 0)
  {
    char *envp[] = {"Touch=touch", NULL};
    kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
  }
}

// caller of this function is responsible for calling fsr_wake_pta5(1) before calling this function
int fsr_keypad_get_adc(struct fsr_keypad *keypad, uint16_t *adc, int num_adc)
{
	struct i2c_client *i2c = keypad->i2c_fsr;
	int ret;
	u8 ready[2] = {0};
	int retry = 100;

	do {
		fsr_wake_pta5(1);
		ret = fsr_i2c_read_block_data(i2c, REG_D_READY, 1, ready);
		fsr_wake_pta5(0);
		msleep(20);
	}while((ready[0] != FLAG_ADC_READY_FOR_READ || ret < 0) && retry--);

	if(ready[0] != FLAG_ADC_READY_FOR_READ) {
		fsr_wake_pta5(1);
		fsr_switch_mode(keypad, ACTIVE_MODE_NORMAL);
		fsr_wake_pta5(0);
		return -EINVAL;
	}

	fsr_wake_pta5(1);

	if( (ret = fsr_i2c_read_block_data(i2c, REG_D_ADC_VALS, num_adc*sizeof(u16), (u8*)adc) ) < 0)
	{
		fsr_wake_pta5(0);
		return ret;
	}

	ready[0] = REG_D_READY;
	ready[1] = 0x55;

	ret = fsr_i2c_write_block_data_no_addr(i2c, 2, ready);
	fsr_wake_pta5(0);

	return ret;
}

static void fsr_work_repeat_func(struct work_struct *work)
{
	struct fsr_keypad *keypad = container_of(work, struct fsr_keypad, fsr_work_repeat);

	input_event(keypad->input, EV_KEY, keypad->input->repeat_key, 2);
	input_sync(keypad->input);
	drv26xx_execute_wform_sequence(FSR_PRESS_NEXT_WAVEFORM);
}

static void fsr_restart_func(struct work_struct *work)
{
	struct fsr_keypad *keypad = container_of(work, struct fsr_keypad, restart_work);
	int ret = 0;
	int old_state = keypad->power_state;

	//forcefully change the power state and eventually set the power state in poweron
	keypad->power_state = FSR_POWERSTATE_ON;
	fsr_i2c_poweroff(keypad);
	keypad->power_state = FSR_POWERSTATE_OFF;
	ret = fsr_i2c_poweron(keypad);
	if(old_state == FSR_POWERSTATE_OFF_PENDING) {
		ret = fsr_i2c_poweroff(keypad);
	}
}

static void do_fsr_triggered(struct fsr_keypad *keypad)
{
	struct i2c_client *i2c = keypad->i2c_fsr;
	char reg;
	static char last_reg;
	int ret;

	cancel_work_sync(&keypad->fsr_work_repeat);
	fsr_wake_pta5(1);
	ret = fsr_i2c_read_block_data(i2c, REG_N_BUTTON_ID, 1, &reg);
	fsr_wake_pta5(0);
	if(ret < 0) {
		printk(KERN_ERR "%s: failed with %d, %d", __func__, ret, __LINE__);
		return;
	}

	if (!reg) {
		/*
		 * Either the button was released or there is an active touch
		 * on the screen
		 */
		clear_keys(keypad);
		send_user_event(keypad->input, 1, 0);
		if (keypad->trigger_on_up) {
			switch(last_reg){
				case FSR_BUTTON_LEFT_TOP:
				case FSR_BUTTON_RIGHT_TOP:
				if(keypad->enable_prev)
					drv26xx_execute_wform_sequence(FSR_RELEASE_PREVIOUS_WAVEFORM);
				break;
				case FSR_BUTTON_LEFT_BOTTOM:
				case FSR_BUTTON_RIGHT_BOTTOM:
				if(keypad->enable_next)
					drv26xx_execute_wform_sequence(FSR_RELEASE_NEXT_WAVEFORM);
				break;
			default:
				break;
			}
		}
	}
	else if (reg <= FSR_KEYMAP_SIZE) {
		switch(reg){
				case FSR_BUTTON_LEFT_TOP:
				case FSR_BUTTON_RIGHT_TOP:
				if(!keypad->touch_active && keypad->event_enable && keypad->enable_prev) {
					drv26xx_execute_wform_sequence(FSR_PRESS_PREVIOUS_WAVEFORM);
					input_report_key(keypad->input, keypad->kmap[reg-1].keyval, 1);
					keypad->kmap[reg-1].state = FSR_KEY_PRESSED;
					input_sync(keypad->input);
					send_user_event(keypad->input, 0, 1);
				}
			break;

				case FSR_BUTTON_LEFT_BOTTOM:
				case FSR_BUTTON_RIGHT_BOTTOM:
				if(!keypad->touch_active && keypad->event_enable && keypad->enable_next) {
					drv26xx_execute_wform_sequence(FSR_PRESS_NEXT_WAVEFORM);
					input_report_key(keypad->input, keypad->kmap[reg-1].keyval, 1);
					keypad->kmap[reg-1].state = FSR_KEY_PRESSED;
					input_sync(keypad->input);
					send_user_event(keypad->input, 0, 1);
				}
			break;

			default:
				break;
		}
	}
	else {
		//interrupt is going crazy, lets reset the controller.
		printk(KERN_ERR "Reported button out of range\n");
		//TODO may reset the controller here too?
	}
	last_reg = reg;
}

static void input_repeat_key(unsigned long data)
{
	struct fsr_keypad *keypad = (struct fsr_keypad *)data;

	schedule_work(&keypad->fsr_work_repeat);
	if (keypad->input->rep[REP_PERIOD])
		mod_timer(&keypad->input->timer, jiffies + msecs_to_jiffies(keypad->input->rep[REP_PERIOD]));
}

static irqreturn_t fsr_threaded_irq(int irq, void *dev)
{
	struct fsr_keypad *keypad = dev;

	mutex_lock(&g_keypad.system_lock);
	if (keypad->power_state != FSR_POWERSTATE_ON)
	{
		mutex_unlock(&g_keypad.system_lock);
		return IRQ_HANDLED;
	}
	do_fsr_triggered(keypad);
	mutex_unlock(&g_keypad.system_lock);
	return IRQ_HANDLED;
}

static irqreturn_t fsr_quick_check_threaded_irq(int irq, void *dev)
{
	struct fsr_keypad *keypad = dev;

	if (keypad->waiting_power_on)
	{
		keypad->waiting_power_on = false;
		complete(&keypad->power_on_completion);
		return IRQ_HANDLED;
	}

	if (keypad->power_state == FSR_POWERSTATE_OFF)
		return IRQ_HANDLED;

	return IRQ_WAKE_THREAD;
}

static int fsr_keypad_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	int err, irq, i;

	input = input_allocate_device();
	if (!input) {
		dev_err(&pdev->dev, "failed to allocate fsr_keypad memory\n");
		err = -ENOMEM;
		goto out_exit;
	}

	init_keymap(&g_keypad);

	g_keypad.input = input;

	input->id.bustype = BUS_I2C;
	input->name = pdev->name;
	input->dev.parent = &pdev->dev;

	input_set_drvdata(input, &g_keypad);

	__set_bit(EV_KEY, input->evbit);
	input->timer.data = (long) &g_keypad;
	input->timer.function = input_repeat_key;
	input->rep[REP_DELAY]  = FSR_DEF_REPEAT_DELAY;
	input->rep[REP_PERIOD] = FSR_DEF_REPEAT_PERIOD;
	g_keypad.repeat.delay  = FSR_DEF_REPEAT_DELAY;
	g_keypad.repeat.period = FSR_DEF_REPEAT_PERIOD;
	g_keypad.enable_next = true;
	g_keypad.enable_prev = true;
	g_keypad.event_enable = false;
	g_keypad.touch_active = false;

	if (!FSR_NO_REPEAT)
	{
		__set_bit(EV_REP, input->evbit);
		g_keypad.repeat.enabled = true;
	}
	else
	{
		g_keypad.repeat.enabled = false;
	}

	for (i = 0; i < FSR_KEYMAP_SIZE; i++)
	{
		g_keypad.kmap[i].state = FSR_KEY_RELEASED;
		__set_bit(g_keypad.kmap[i].keyval, input->keybit);
	}

	mutex_init(&g_keypad.system_lock);
	INIT_WORK(&g_keypad.fsr_work_repeat, fsr_work_repeat_func);
	INIT_WORK(&g_keypad.restart_work, fsr_restart_func);

	err = input_register_device(input);
	if (err) {
		dev_err(&pdev->dev, "Could not register input device\n");
		goto out_free_mem;
	}

	platform_set_drvdata(pdev, &g_keypad);

	if (misc_register(&fsr_misc_device))
	{
		printk(KERN_ERR "could not register fsr misc device 10, 161.\n");
		goto out_free_logging_irq;
	}

	/* Reset the FSR controller */
	wario_fsr_init_pins();

	irq = gpio_fsr_button_irq();
	irq_set_irq_type(irq, FSR_IRQ_TYPE);

	g_keypad.irq = irq;
	g_keypad.dev = &(pdev->dev);
	g_keypad.power_state = FSR_POWERSTATE_OFF;

	err = request_threaded_irq(irq, fsr_quick_check_threaded_irq, fsr_threaded_irq, FSR_IRQ_TYPE,
		"fsr-keypad", &g_keypad);
	if (err < 0) {
		dev_err(&pdev->dev, "Could not allocate irq %d. Error : %d\n", irq, err);
		goto out_unregister_misc;
	}
	g_keypad.irq_enabled = true;
	fsr_disable_irq(&g_keypad);

	g_keypad.device_mode = KL05_BL_MODE;
	g_keypad.wdog_irq = gpio_fsr_bootloader_irq();
	err = request_threaded_irq(g_keypad.wdog_irq, NULL, fsr_bootloader_irq, FSR_IRQ_TYPE,
		"fsr-bootloader", &g_keypad);
	if (err < 0) {
		dev_err(&pdev->dev, "Could not allocate irq %d. Error : %d\n", g_keypad.wdog_irq, err);
		goto out_free_irq1;
	}

	// We are ready to power on.
	err = fsr_i2c_poweron(&g_keypad);
	if(err) {
		printk(KERN_ERR "Failed to exit bootloader 1!!\n");
		//allow using the sys entries recover the device
	}else {
		msleep(100);
		fsr_get_fw_config(&g_keypad);
	}
	clear_keys(&g_keypad);

	g_keypad.trigger_on_up = 0;

	err = add_sysfs_interfaces(&pdev->dev);
	if (err < 0) {
		dev_err(&pdev->dev, "Could not register pressure sys entry\n");
		goto out_free_irq2;
	}

	printk(KERN_INFO "Done Registering FSR platform device\n");

	return 0;

out_free_irq2:
	fsr_wdog_irq_en(&g_keypad, 0);
out_free_irq1:
	free_irq(irq, &g_keypad);
	g_keypad.irq = 0;
out_unregister_misc:
	misc_deregister(&fsr_misc_device);
out_free_logging_irq:
	input_unregister_device(input);
out_free_mem:
	input_free_device(input);
	mutex_destroy(&g_keypad.system_lock);
out_exit:
	return err;
}

static int __devexit fsr_keypad_remove(struct platform_device *pdev)
{
	struct fsr_keypad *keypad = platform_get_drvdata(pdev);

	if(keypad->irq)
		free_irq(keypad->irq, keypad);

	if(keypad->wdog_irq)
		free_irq(keypad->wdog_irq, keypad);

	cancel_work_sync(&keypad->restart_work);

	if (keypad->input)
		input_unregister_device(keypad->input);

	misc_deregister(&fsr_misc_device);

	remove_sysfs_interfaces(&pdev->dev);
	return 0;
}

static int fsr_keypad_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	client->addr = FSR_I2C_ADDRESS;
	i2c_set_clientdata(client, &g_keypad);
	g_keypad.i2c_fsr = client;
	i2c_probe_success = 1;
	return 0;
}

static int fsr_keypad_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static int kl05_cmd_deep_sleep(struct i2c_client* i2c)
{
	int ret;
	u8 reg[2];
	int retry = FSR_I2C_MAX_RETRY;
	fsr_wake_pta5(1);
	do {
		ret = fsr_switch_mode(&g_keypad, ACTIVE_MODE_NORMAL);
		if(ret < 0)	{
			printk(KERN_ERR "%s %d: ret=%d, retry=%d", __func__, __LINE__, ret, retry);
			msleep(FSR_I2C_RETRY_DELAY_MS);
		}
	}while(ret < 0 && retry--);

	if(ret < 0)
		goto out;

	reg[0] = REG_N_POWER;
	reg[1] = 1;

	retry = FSR_I2C_MAX_RETRY;
	do {
		ret = fsr_i2c_write_block_data_no_addr(i2c, 2, reg);
		if(ret < 0)	{
			printk(KERN_ERR "%s %d: ret=%d, retry=%d", __func__, __LINE__, ret, retry);
			msleep(FSR_I2C_RETRY_DELAY_MS);
		}
	}while(ret < 0 && retry--);

	g_keypad.device_mode = KL05_BL_MODE;
out:
	fsr_wake_pta5(0);

	return ret;
}

static int fsr_i2c_poweroff(struct fsr_keypad* keypad)
{
	int ret = 0;
	mutex_lock(&keypad->system_lock);
	if(keypad->power_state == FSR_POWERSTATE_ON) {
		keypad->power_state = FSR_POWERSTATE_OFF_PENDING;
		fsr_disable_irq(keypad);
		ret = kl05_cmd_deep_sleep(keypad->i2c_fsr);
		if(ret < 0) {
			printk(KERN_ERR "fsr:failed to send deep sleep command!\n");
			keypad->power_state = FSR_POWERSTATE_ON;
			goto out;
		}
		keypad->power_state = FSR_POWERSTATE_OFF;
		clear_keys(keypad);
		printk(KERN_INFO "fsr:power off completed\n");
	}
out:
	mutex_unlock(&keypad->system_lock);
	return ret;
}

static int fsr_i2c_poweron(struct fsr_keypad* keypad)
{
	int ret = 0;
	mutex_lock(&keypad->system_lock);
	if(keypad->power_state == FSR_POWERSTATE_OFF) {
		keypad->power_state = FSR_POWERSTATE_ON_PENDING;
		fsr_pm_pins(1);
		init_completion(&keypad->power_on_completion);
		keypad->waiting_power_on = true;

		kl05_hw_reset(keypad);
		ret = kl05_bl_cmd_exit_bl(keypad->i2c_fsr);
		if (ret < 0)
		{
			printk(KERN_ERR "Error exiting bootloader\n");
			keypad->waiting_power_on = false;
			keypad->power_state = FSR_POWERSTATE_OFF;
			goto out;
		}
		fsr_enable_irq(keypad);

		ret = wait_for_poweron(keypad);
		if (ret)
		{
			printk(KERN_ERR "fsr:Waiting for completion timeout\n");
			keypad->power_state = FSR_POWERSTATE_OFF;
			goto out;
		}
		printk(KERN_INFO "fsr:power on completed\n");
		keypad->device_mode = KL05_APP_MODE;
		fsr_wdog_irq_en(keypad, 1);
		keypad->power_state = FSR_POWERSTATE_ON;
		fsr_restore_fw_state(keypad);
	}
out:
	mutex_unlock(&keypad->system_lock);
	return ret;
}

static int fsr_i2c_suspend(struct device *dev)
{
	fsr_i2c_poweroff(&g_keypad);
	cancel_work_sync(&g_keypad.restart_work);
	printk(KERN_INFO "fsr: fsr_i2c_suspend completed\n");
	return 0;
}

static int fsr_i2c_resume(struct device *dev)
{
	//it's locked, let it sleep in..
	if(g_keypad.exlock)
		return 0;
	fsr_i2c_poweron(&g_keypad);
	printk(KERN_INFO "fsr: fsr_i2c_resume completed\n");
	return 0;
}

/**** PROC ENTRY ****/
static int fsr_keypad_proc_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;

	if (off > 0) {
		*eof = 1;
		return 0;
	}
	if(g_keypad.exlock)
		len = sprintf(page, "keypad is locked\n");
	else
		len = sprintf(page, "keypad is unlocked\n");

	return len;
}

static ssize_t fsr_keypad_proc_write( struct file *filp, const char __user *buff,
		unsigned long len, void *data )
{
	char command[16];
	int sts = 0;

	if (len >= 16){
		printk(KERN_ERR "%s:proc:command is too long!\n", __func__);
		return -ENOSPC;
	}

	if (copy_from_user(command, buff, len)) {
		printk(KERN_ERR "%s:proc::cannot copy from user!\n", __func__);
		return -EFAULT;
	}

	if ( !strncmp(command, "unlock", 6) ) {
		if(g_keypad.exlock == false)
			goto out;
		printk(KERN_INFO "fsr_keypad: I %s::command=unlock:\n", __func__);
		sts = fsr_i2c_poweron(&g_keypad);

		if (sts < 0) {
			printk(KERN_ERR "%s:proc:command=%s:not succeed please retry\n", __func__, command);
			return -EBUSY;
		}
		g_keypad.exlock = false;
		printk(KERN_INFO "fsr: unlock done\n");
	} else if ( !strncmp(command, "lock", 4) ) {
		if(g_keypad.exlock == true)
			goto out;
		printk(KERN_INFO "fsr_keypad: I %s::command=lock:\n", __func__);
		sts = fsr_i2c_poweroff(&g_keypad);
		if (sts < 0){
			printk(KERN_ERR "%s:proc:command=%s:not succeed please retry\n", __func__, command);
			return -EBUSY;
		}
		g_keypad.exlock = true;
		printk(KERN_INFO "fsr: lock done\n");
	} else if ( !strncmp(command, "reset", 5) ) {
		printk(KERN_INFO "fsr_keypad: I %s::command=reset:\n", __func__);

		//forcefully change the power state and eventually set the power state in poweron.
		g_keypad.power_state = FSR_POWERSTATE_ON;
		fsr_i2c_poweroff(&g_keypad);
		g_keypad.power_state = FSR_POWERSTATE_OFF;
		fsr_i2c_poweron(&g_keypad);

	} else {
		printk(KERN_ERR "%s:Unrecognized command=%s\n", __func__, command);
	}
out:
	return len;
}

static struct i2c_device_id fsr_i2c_id[] =
{
	{ FSR_DRIVER_NAME, 0 },
	{ },
};

static const struct dev_pm_ops fsr_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fsr_i2c_suspend, fsr_i2c_resume)
};

static struct i2c_driver fsr_i2c_driver =
{
	.probe = fsr_keypad_i2c_probe,
	.remove = fsr_keypad_i2c_remove,
	.id_table = fsr_i2c_id,
	.driver = {
		.name = FSR_DRIVER_NAME,
		.pm = &fsr_pm_ops,
	},
};

static void fsr_device_release(struct device* dev)
{
	/* Do Nothing */
}

static struct platform_device fsr_device = {
	.name = FSR_DRIVER_NAME,
	.id   = 0,
	.dev  = {
		.release = fsr_device_release,
	},
};

static struct platform_driver fsr_keypad_driver = {
	. driver = {
		.name = FSR_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = fsr_keypad_probe,
	.remove = __devexit_p(fsr_keypad_remove),
};

int do_fsr_keypad_init(void)
{
	int ret = 0;

	i2c_probe_success = 0;
	ret = i2c_add_driver(&fsr_i2c_driver);
	if (ret < 0)
	{
		printk(KERN_ERR "Error probing %s %d\n", FSR_DRIVER_NAME, __LINE__);
		return -ENODEV;
	}

	if (!i2c_probe_success)
	{
		printk(KERN_ERR "Error probing %s %d\n", FSR_DRIVER_NAME, __LINE__);
		i2c_del_driver(&fsr_i2c_driver);
		return -ENODEV;
	}
	platform_device_register(&fsr_device);
	if ((ret = platform_driver_register(&fsr_keypad_driver)))
	{
		printk(KERN_ERR "Error probing device %s\n", FSR_DRIVER_NAME);
		platform_device_unregister(&fsr_device);
		i2c_probe_success = 0;
	}
	return ret;
}

void do_fsr_keypad_exit(void)
{
	if (i2c_probe_success)
	{
		platform_driver_unregister(&fsr_keypad_driver);
		platform_device_unregister(&fsr_device);
	}
	i2c_del_driver(&fsr_i2c_driver);
}

static int __init fsr_keypad_init(void)
{
	int ret = 0;

	ret = do_fsr_keypad_init();
	if (ret < 0)
	{
		printk(KERN_ERR "Error probing %s %d\n", FSR_DRIVER_NAME, __LINE__);
		return -ENODEV;
	}
	return ret;
}
module_init(fsr_keypad_init);

static void __exit fsr_keypad_exit(void)
{
	do_fsr_keypad_exit();
}

module_exit(fsr_keypad_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nadim Awad");
MODULE_DESCRIPTION("FSR Keypad driver");
