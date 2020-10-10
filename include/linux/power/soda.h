/*
 * Copyright 2014-2015 Amazon Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __SODA_H__
#define __SODA_H__

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/time.h>

#define SODA_DRIVER_VERSION                 "1.1"
#define SODA_DRIVER_DESC                    "soda driver"
#define SODA_DRIVER_NAME                    "soda"
#define SODA_I2C                            "soda_i2c"
#define SODA_CHG_PSY_NAME                   "soda_chg"
#define SODA_FG_PSY_NAME                    "soda_fg"
#define SODA_BOOST_PSY_NAME                 "soda_boost"
#define SODA_EXT_CHG                        "soda_ext_chg"
#define SODA_DOCK_EVENT                     "soda_dock_event"

#define SODA_DOCK_DEB_MS                    100
#define SODA_MON_DOCK_MS                    100
#define SODA_ERROR_MON_RUN_MS               3000
#define SODA_MON_RUN_MS                     15000
#define SODA_MON_VBUS_MS                    200
#define SODA_LED_INIT_MS                    100
#define SODA_LED_MON_MS                     30000
#define SODA_ONKEY_MON_MS                   0
#define SODA_LED_BLINK_MS                   2000
#define SODA_OTG_VBUS_DELAY_MS              100
#define SODA_OTG_VBUS_READY_MS              1000
#define SODA_IF_ERR_MAX_CNT                 4
#define SODA_I2C_RESET_MAX_CNT              2
#define SODA_UVLO_ERR_MAX_CNT               2

#define WALL_CHARGER                        1
#define USB_CHARGER                         2

#define SODA_CHG_I2C_ADDR                   (0x36>>1)
#define SODA_FG_I2C_ADDR                    (0xAA>>1)

#define SODA_I2C_STS_FAIL                   0
#define SODA_I2C_STS_PASS                   1

#define SODA_I2C_IF_BAD                     0
#define SODA_I2C_IF_GOOD                    1

#define SODA_BOOST_STS_DISABLED             false
#define SODA_BOOST_STS_ENABLED              true

#define SODA_I2C_SDA_DISABLE                0
#define SODA_I2C_SDA_ENABLE                 1

#define SODA_BOOST_DISABLE                  0
#define SODA_BOOST_ENABLE                   1

#define SODA_VBUS_OFF                       0
#define SODA_VBUS_ON                        1

#define SODA_I2C_SDA_DOCK_INTR              0
#define SODA_I2C_SDA_DATA_LINE              1

#define SODA_BATTERY_ID_VALID               0
#define SODA_BATTERY_ID_INVALID             1

#define SODA_STATE_UNDOCKED                 0
#define SODA_STATE_DOCKED                   1

#define SODA_SDA_OPS_DATA                   0
#define SODA_SDA_OPS_INTR                   1

#define BATTERY_SODA                        0
#define BATTERY_SKIST                       1

#define LOG_BUFF_LENGTH                     32

struct soda_platform_data {
	unsigned scl_gpio;
	unsigned sda_gpio;
	unsigned i2c_bb_delay;
	unsigned soda_sda_dock_irq;
	unsigned i2c_sda_pu_gpio;
	unsigned boost_ctrl_gpio;
	unsigned ext_chg_gpio;
	unsigned ext_chg_irq;
	unsigned vbus_en_gpio;
	unsigned otg_sw_gpio;
	unsigned update_interval_ms;
};

struct soda_i2c {
	struct i2c_client *client;
	int (*read) (struct soda_i2c *me, u8 addr, u8 *val);
	int (*write) (struct soda_i2c *me, u8 addr, u8 *val);
	int (*bulk_read) (struct soda_i2c *me, u8 addr, u8 *dst, u16 len);
	int (*bulk_write) (struct soda_i2c *me, u8 addr, const u8 *src, u16 len);
};

struct soda_drvdata {
	struct soda_platform_data* pdata;
	struct device           *dev;
	struct kobject          *kobj;

	/* i2c */
	struct soda_i2c          soda_chg_i2c;
	struct soda_i2c          soda_fg_i2c;

	struct i2c_adapter soda_i2c_adap;
	struct i2c_algo_bit_data soda_bit_data;

	int status;
	struct power_supply soda_chg_psy;
	struct power_supply soda_fg_psy;
	struct power_supply soda_boost_psy;

	struct mutex soda_lock;
	struct mutex soda_chg_lock;
	struct mutex soda_fg_lock;
	struct mutex soda_boost_lock;

	struct delayed_work soda_monitor;
	struct delayed_work soda_vbus_ctrl_monitor;
	struct delayed_work soda_led_monitor;
	struct work_struct soda_onkey_monitor;
	struct delayed_work soda_led_blinking;

	unsigned scl_gpio;
	unsigned sda_gpio;
	int soda_sda_dock_irq;
	unsigned i2c_sda_pu_gpio;
	unsigned ext_chg_gpio;
	int ext_chg_irq;
	unsigned boost_ctrl_gpio;
	unsigned otg_sw_gpio;
	unsigned vbus_en_gpio;
	bool soda_soft_boost_status;

	int soda_fg_volt;
	int soda_fg_current;
	int soda_fg_temp_f;
	int soda_fg_temp_c;
	int soda_fg_soc;
	int soda_fg_lmd;
	int soda_fg_nac;
	int soda_fg_cyc_cnt;
	unsigned long soda_fg_volt_timestamp;
	unsigned long soda_fg_current_timestamp;
	unsigned long soda_fg_temp_timestamp;
	unsigned long soda_fg_soc_timestamp;
	unsigned long soda_fg_lmd_timestamp;
	unsigned long soda_fg_nac_timestamp;
	unsigned long soda_fg_cyc_cnt_timestamp;

	unsigned long update_interval;
};

static __always_inline int soda_i2c_read (struct soda_i2c *i2c,
		u8 addr, u8 *val)
{
	return i2c->read(i2c, addr, val);
}

static __always_inline int soda_i2c_write (struct soda_i2c *i2c,
		u8 addr, u8 *val)
{
	return i2c->write(i2c, addr, val);
} 

static __always_inline int soda_i2c_bulk_read (struct soda_i2c *i2c,
		u8 addr, u8 *dst, u16 len)
{
	return i2c->bulk_read(i2c, addr, dst, len);
}

static __always_inline int soda_i2c_bulk_write (struct soda_i2c *i2c,
		u8 addr, const u8 *src, u16 len)
{
	return i2c->bulk_write(i2c, addr, src, len);
}

#ifdef VERBOSE
#define dev_noise(args...) dev_dbg(args)
#else /* VERBOSE */
#define dev_noise(args...) do { } while (0)
#endif /* VERBOSE */

/* macros for conversion from 2-byte register value to integer */
#define __u16_to_intval(val) \
	((int)(val))
#define __s16_to_intval(val) \
	(((val) & 0x8000)? -((int)((0x7fff & ~(val)) + 1)) : ((int)(val)))

/* SODA CHARGER SMB1358 REGISTERS */
/* Mask/Bit helpers */
#define _SODA_CHG_MASK(BITS, POS) \
	((unsigned char)(((1 << (BITS)) - 1) << (POS)))
#define SODA_CHG_MASK(LEFT_BIT_POS, RIGHT_BIT_POS) \
	_SODA_CHG_MASK((LEFT_BIT_POS) - (RIGHT_BIT_POS) + 1, \
			(RIGHT_BIT_POS))

#define SODA_CHG_CFG_3_REG                  0x03
#define SODA_CHG_CFG_3_ITERM_M              SODA_CHG_MASK(5,3)
#define SODA_CHG_CFG_3_ITERM_S              3

#define SODA_CHG_CFG3_ITERM_50MA            0x08
#define SODA_CHG_CFG3_ITERM_100MA           0x10
#define SODA_CHG_CFG3_ITERM_150MA           0x18
#define SODA_CHG_CFG3_ITERM_200MA           0x20
#define SODA_CHG_CFG3_ITERM_250MA           0x28
#define SODA_CHG_CFG3_ITERM_500MA           0x30
#define SODA_CHG_CFG3_ITERM_600MA           0x38

#define SODA_CHG_CFG_4_REG                  0x04

#define SODA_CHG_CFG_5_REG                  0x05
#define SODA_CHG_CFG_5_RECHG_VLT_M          BIT (2)
#define SODA_CHG_CFG_5_RECHG_VLT_S          2

#define SODA_CHG_CFG_C_REG                  0x0C
#define SODA_CHG_CFG_C_USBIN_ICL_M          SODA_CHG_MASK(4,0)
#define SODA_CHG_CFG_C_USBIN_ICL_S          0
#define SODA_CHG_CFG_C_USBIN_ICL_300MA      0x0
#define SODA_CHG_CFG_C_USBIN_ICL_400MA      0x1
#define SODA_CHG_CFG_C_USBIN_ICL_450MA      0x2
#define SODA_CHG_CFG_C_USBIN_ICL_475MA      0x3
#define SODA_CHG_CFG_C_USBIN_ICL_500MA      0x4
#define SODA_CHG_CFG_C_USBIN_ICL_550MA      0x5
#define SODA_CHG_CFG_C_USBIN_ICL_600MA      0x6

#define SODA_CHG_CFG_14_REG                 0x14
#define SODA_CHG_CFG_14_CHG_EN_SRC_M        BIT (7)
#define SODA_CHG_CFG_14_CHG_EN_SRC_S        7
#define SODA_CHG_CFG_14_PRE_FAST_CMD_M      BIT (5)
#define SODA_CHG_CFG_14_PRE_FAST_CMD_S      5

#define SODA_CHG_CFG_17_REG                 0x17
#define SODA_CHG_CFG_17_STAT_PIN_CFG_M      BIT (4)
#define SODA_CHG_CFG_17_STAT_PIN_CFG_S      4
#define SODA_CHG_CFG_17_STAT_PIN_OP_M       BIT (0)
#define SODA_CHG_CFG_17_STAT_PIN_OP_S       0


#define SODA_CHG_CFG_1A_REG                 0x1A
#define SODA_CHG_CFG_1A_HOT_SL_FVC_M        BIT (3)
#define SODA_CHG_CFG_1A_HOT_SL_FVC_S        3
#define SODA_CHG_CFG_1A_COLD_SL_FVC_M       BIT (2)
#define SODA_CHG_CFG_1A_COLD_SL_FVC_S       2

#define SODA_CHG_CFG_1C_REG                 0x1C
#define SODA_CHG_CFG_1C_PRE_CC_M            SODA_CHG_MASK(7,5)
#define SODA_CHG_CFG_1C_PRE_CC_S            5
#define SODA_CHG_CFG_1C_PRE_CC_100MA        0x0
#define SODA_CHG_CFG_1C_PRE_CC_150MA        0x1
#define SODA_CHG_CFG_1C_PRE_CC_200MA        0x2
#define SODA_CHG_CFG_1C_PRE_CC_250MA        0x3
#define SODA_CHG_CFG_1C_FAST_CC_M           SODA_CHG_MASK(4,0)
#define SODA_CHG_CFG_1C_FAST_CC_S           0
#define SODA_CHG_CFG_1C_FAST_CC_300MA       0x00
#define SODA_CHG_CFG_1C_FAST_CC_400MA       0x01
#define SODA_CHG_CFG_1C_FAST_CC_450MA       0x02
#define SODA_CHG_CFG_1C_FAST_CC_475MA       0x03
#define SODA_CHG_CFG_1C_FAST_CC_500MA       0x04
#define SODA_CHG_CFG_1C_FAST_CC_550MA       0x05
#define SODA_CHG_CFG_1C_FAST_CC_600MA       0x06
#define SODA_CHG_CFG_1C_FAST_CC_650MA       0x07
#define SODA_CHG_CFG_1C_FAST_CC_700MA       0x08

#define SODA_CHG_CFG_1E_REG                 0x1E
#define SODA_CHG_CFG_1E_FLOAT_VOLT_M        SODA_CHG_MASK(5,0)
#define SODA_CHG_CFG_1E_FLOAT_VOLT_S        0
#define SODA_CHG_CFG_1E_FLOAT_VOLT_4P1V     0x1E
#define SODA_CHG_CFG_1E_FLOAT_VOLT_4P35V    0x2B

#define SODA_CHG_CMD_40_REG                 0x40		/* CMD_I2C */
#define SODA_CHG_CMD_40_VOLATILE_M          BIT (6)
#define SODA_CHG_CMD_40_VOLATILE_S          6

#define SODA_CHG_CMD_41_REG                 0x41		/* CMD_IL */
#define SODA_CHG_CMD_41_USBAC_M             SODA_CHG_MASK(2,0)
#define SODA_CHG_CMD_41_USBAC_S             0
#define SODA_CHG_CMD_41_USBAC_HP            0x05
#define SODA_CHG_CMD_41_USBAC_LP            0
#define SODA_CHG_CMD_41_CHIP_SUSPEND_M		BIT(6)
#define SODA_CHG_CMD_41_CHIP_SUSPEND_S		6

#define SODA_CHG_CMD_42_REG                 0x42
#define SODA_CHG_CMD_42_PRE_FAST_EN_M       BIT (2)
#define SODA_CHG_CMD_42_PRE_FAST_EN_S       2
#define SODA_CHG_CMD_42_CHARGE_ENABLE_M     BIT (1)
#define SODA_CHG_CMD_42_CHARGE_ENABLE_S     1

#define SODA_CHG_CMD_4A_REG					0x4A		/* battery Currnt status */
#define SODA_CHG_CMD_4A_REG_CHARGE_DONE		BIT(5)
#define	SODA_CHG_CMD_4A_REG_CHARGE_STATUS	0x06

#define SODA_CHG_CMD_4F_REG                 0x4F

#define SODA_CHG_HOT_SL_FV_COMP_OFF         0
#define SODA_CHG_HOT_SL_FV_COMP_ON          1

#define SODA_CHG_PRE_FAST_CTRL_AUTO         0
#define SODA_CHG_PRE_FAST_CTRL_CMD          1

#define SODA_CHG_CTRL_PRECHG_EN             0
#define SODA_CHG_CTRL_FASTCHG_EN            1

#define SODA_CHG_ENABLE_CTRL_CMD            0
#define SODA_CHG_ENABLE_CTRL_PIN            1

#define SODA_CHG_DISABLE                    0
#define SODA_CHG_ENABLE                     1

#define SODA_CHG_LOCK                       0
#define SODA_CHG_UNLOCK                     1

#define SODA_CHG_TEMP_C_T1                  0
#define SODA_CHG_TEMP_C_T2                  14
#define SODA_CHG_TEMP_C_T3                  45
#define SODA_CHG_TEMP_C_T4                  60

#define SODA_CHG_REG(reg)                   ((u8)(SODA_CHG_##reg##_REG))
#define SODA_CHG_REG_BITMASK(reg, bit)      ((u8)(SODA_CHG_##reg##_##bit##_M))
#define SODA_CHG_REG_BITSHIFT(reg, bit)          (SODA_CHG_##reg##_##bit##_S)

#define SODA_CHG_REG_BITGET(reg, bit, val) \
    ((u8)(((val) & SODA_CHG_REG_BITMASK(reg, bit))\
        >> SODA_CHG_REG_BITSHIFT(reg, bit)))
#define SODA_CHG_REG_BITSET(reg, bit, val) \
    ((u8)(((val) << SODA_CHG_REG_BITSHIFT(reg, bit))\
        & SODA_CHG_REG_BITMASK(reg, bit)))

/* SODA FG SN27741 REGISTERS */
#define SODA_FG_CNTL_REG                    0x00
#define SODA_FG_TEMP_REG                    0x06
#define SODA_FG_VOLT_REG                    0x08
#define SODA_FG_FLAGS_REG                   0x0A
#define SODA_FG_NAC_REG                     0x0C /* Nominal available capacity */
#define SODA_FG_FCC_REG                     0x12 /* Last measured discharge */
#define SODA_FG_AVGCURRENT_REG              0x14
#define SODA_FG_TTE_REG                     0x16
#define SODA_FG_CYCCNT_REG                  0x2A /* Cycle count total */
#define SODA_FG_SOC_REG                     0x2C /* State of Charge */

#define SODA_FG_CNTL_STAT                   0x0000
#define SODA_FG_CNTL_DEV_TYPE               0x0001
#define SODA_FG_CNTL_FW_VER                 0x0002
#define SODA_FG_CNTL_HW_VER                 0x0003
#define SODA_FG_CNTL_BATT_ID                0x0008

#define SODA_BATT_FULL_CAP                  95 	/* soc in % */
#define SODA_BATT_VOLTAGE_4P1V              4100	/* mV */
#define SODA_BATT_VOLTAGE_4P35V             4350    /* mV */
#define SODA_BATT_VOLTAGE_HYS               100     /* mV */ 
#define SODA_BATT_TEMP_HYS                  2       /* degC */

#define SODA_FG_REG(reg)                    ((u8)(SODA_FG_##reg##_REG))

#define SODA_FG_BAT_DATA_FLASH_BLOCK        0x3F
#define SODA_FG_BAT_DATA_BLOCK_A            0x01
#define SODA_FG_BAT_DATA_BLOCK_B            0x02
#define SODA_FG_BAT_BLOCK_DATA_BASE         0x40
#define SODA_FG_BAT_BLOCK_DATA_CHECKSUM     0x60
#define SODA_FG_BAT_ID_BASE                 0x44
#define SODA_FG_BAT_UNIQUEID_LEN            10
#define SODA_FG_BAT_AUTH_LEN                6
#define SODA_FG_BAT_SKIST                   0x41

#define SODA_FG_BAT_AUTH_2                  0x41
#define SODA_FG_BAT_AUTH_3                  0x42
#define SODA_FG_BAT_AUTH_4                  0x4D
#define SODA_FG_BAT_AUTH_5                  0x4E
#define SODA_FG_BAT_AUTH_6                  0x4F

#define SODA_FG_BAT_PARAM_UNKNOWN           -1
#define SODA_FG_LOBAT_VOLTAGE               3200     /* mV */
#define SODA_FG_VOLT_LTE_3P2V               1
#define SODA_FG_VOLT_GT_3P2V                0

#define __psy_to_soda_charger(psy_ptr) \
	container_of(psy_ptr, struct soda_drvdata, soda_chg_psy)

#define __psy_to_soda_battery(psy_ptr) \
	container_of(psy_ptr, struct soda_drvdata, soda_fg_psy)

#define __psy_to_soda_boost(psy_ptr) \
	container_of(psy_ptr, struct soda_drvdata, soda_boost_psy)

/* SODA CHG Register Read/Write */
#define soda_charger_reg_read(me, reg, val_ptr) \
	soda_i2c_read(&((me)->soda_chg_i2c), SODA_CHG_REG(reg), val_ptr)
#define soda_charger_reg_write(me, reg, val_ptr) \
	soda_i2c_write(&((me)->soda_chg_i2c), SODA_CHG_REG(reg), val_ptr)

static __always_inline int soda_charger_read_masked (struct soda_drvdata *me, 
        u8 addr, u8 mask, u8 *val)
{
	int rc = soda_i2c_read(&((me)->soda_chg_i2c), addr, val);
	*val &= mask;
	return rc;
} /* soda_charger_read_masked */

static __always_inline int soda_charger_write_masked (struct soda_drvdata *me,
        u8 addr, u8 mask, u8 *val)
{
	int rc;
	u8 buf;
	
	rc = soda_i2c_read(&((me)->soda_chg_i2c), addr, &buf);
	if (unlikely(rc)) {
		return rc;
	}
	buf = ((buf & (~mask)) | (*val & mask));
	return soda_i2c_write(&((me)->soda_chg_i2c), addr, &buf);
} /* soda_charger_write_masked */

#define soda_charger_reg_read_masked(me, reg, mask, val_ptr) \
	soda_charger_read_masked(me, SODA_CHG_REG(reg), mask, val_ptr)
#define soda_charger_reg_write_masked(me, reg, mask, val_ptr) \
	soda_charger_write_masked(me, SODA_CHG_REG(reg), mask, val_ptr)

#define soda_charger_reg_get_bit(me, reg, bit, val_ptr) \
	({\
		int __rc = soda_charger_reg_read_masked(me, reg, SODA_CHG_REG_BITMASK(reg, bit), val_ptr);\
		*(val_ptr) = SODA_CHG_REG_BITGET(reg, bit, *(val_ptr));\
		__rc;\
	})
#define soda_charger_reg_set_bit(me, reg, bit, val_ptr) \
	({\
		int __rc = 0;\
		*(val_ptr) = SODA_CHG_REG_BITSET(reg, bit, *(val_ptr));\
		__rc = soda_charger_reg_write_masked(me, reg, SODA_CHG_REG_BITMASK(reg, bit), val_ptr);\
		__rc;\
	})

/* SODA GAUGE Register Word I/O */
#define soda_fg_reg_read(me, reg, val_ptr) \
	soda_i2c_read(&(me->soda_fg_i2c), SODA_FG_REG(reg), val_ptr)
#define soda_fg_reg_write(me, reg, val_ptr) \
	soda_i2c_write(&(me->soda_fg_i2c), SODA_FG_REG(reg), val_ptr)
#define soda_fg_reg_bulk_read(me, reg, dst, len) \
	soda_i2c_bulk_read(&(me->soda_fg_i2c), SODA_FG_REG(reg), dst, len)
#define soda_fg_reg_bulk_write(me, reg, src, len) \
	soda_i2c_bulk_write(&(me->soda_fg_i2c(, SODA_FG_REG(reg), src, len)

#define soda_gauge_reg_read_word(me, reg, val_ptr) \
	({\
	 int __rc = soda_fg_reg_bulk_read(me,\
		 reg, (u8*)(val_ptr), 2);\
	 if (unlikely(__rc)) {\
	 dev_err((me)->dev, ""#reg" read error [%d]\n", __rc);\
	 }\
	 *(val_ptr) = __le16_to_cpu(*val_ptr);\
	 __rc;\
	 })

#define soda_gauge_reg_write_word(me, reg, val) \
	({\
	 u16 __buf = __cpu_to_le16(val);\
	 int __rc = soda_fg_reg_bulk_write(me,\
		 reg, (u8*)(&__buf), 2);\
	 if (unlikely(__rc)) {\
	 dev_err((me)->dev, ""#reg" write error [%d]\n", __rc);\
	 }\
	 __rc;\
	 })

#define __is_timestamp_expired(me, timestamp) \
	time_before(jiffies, (me)->timestamp + (me)->update_interval)
#define __reset_timestamp(me, timestamp) \
	((me)->timestamp = jiffies)

#define __lock_soda(me)          mutex_lock(&((me)->soda_lock))
#define __unlock_soda(me)        mutex_unlock(&((me)->soda_lock))
#define __lock_soda_chg(me)      mutex_lock(&((me)->soda_chg_lock))
#define __unlock_soda_chg(me)    mutex_unlock(&((me)->soda_chg_lock))
#define __lock_soda_fg(me)       mutex_lock(&((me)->soda_fg_lock))
#define __unlock_soda_fg(me)     mutex_unlock(&((me)->soda_fg_lock))
#define __lock_soda_boost(me)    mutex_lock(&((me)->soda_boost_lock))
#define __unlock_soda_boost(me)  mutex_unlock(&((me)->soda_boost_lock))

#define BATT1_GREEN_LED_THRESHOLD   90      /* Battery capacity (%) */
#define BATT2_GREEN_LED_THRESHOLD   95      /* Battery capacity (%) */
#define BATT2_CHARGE_FULL_THRESHOLD 100		/* Battery capacity (%) */
#define BATT2_RECHARGE_THRESHOLD    90		/* Battery capacity (%) */

enum {
	LED_AUTO = 0,
	LED_MAN,
	LED_GREEN_OFF,
	LED_GREEN_ON,
	LED_GREEN_BLINK,
	LED_AMBER_OFF,
	LED_AMBER_ON,
	LED_AMBER_BLINK,
};

enum {
	SODA_LED_OFF = 0,
	SODA_LED_ON = 1,
	SODA_LED_BLINK = 2,
};

struct battery_status {
	struct timespec time;
	int capacity;
	int temp;
	int voltage;
	char uniqueid[SODA_FG_BAT_UNIQUEID_LEN];
};

struct soda_metric_data {
	struct battery_status  usbin;
	struct battery_status  usbout;
	struct timespec  usb_charging_time;
	
	struct battery_status  soda_dock;
	struct battery_status  soda_undock;
	struct timespec soda_charging_time;
	
	struct battery_status  skist_dock;
	struct battery_status  skist_undock;
	struct timespec  skist_charging_time;
};

#endif /* __SODA_H__ */


