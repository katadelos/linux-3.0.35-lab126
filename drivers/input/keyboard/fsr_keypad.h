/*
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LINUX_KL05_FSR_I2C_H
#define _LINUX_KL05_FSR_I2C_H

#define KL05_BL_CMD_SOFT_RESET            0x53
#define KL05_BL_CMD_EXIT_BL               0x51
#define KL05_BL_CMD_HOOK                  0x02
#define KL05_BL_CMD_IDENT                 0x49

#define KL05_FC_CMD_ADC                   0x60
#define KL05_FC_CMD_DEEP_SLEEP1           0x61
#define KL05_FC_CMD_DEEP_SLEEP2           0x62


enum kl05_device_mode{
KL05_BL_MODE, //00
KL05_APP_MODE, //01	
};

#define ACTIVE_MODE_NORMAL 0x0
#define ACTIVE_MODE_CONFIG 0x1
#define ACTIVE_MODE_DIAGS  0x2

// List of registers
#define REG_ACTIVE_MODE	0x0

// Registers in normal mode
#define REG_N_POWER         0x01
#define REG_N_VERSION_MAJOR 0x02
#define REG_N_VERSION_MINOR 0x03
#define REG_N_NUM_BUTTONS   0x04
#define REG_N_BUTTON_STATE  0x05
#define REG_N_BUTTON_ID     0x06
#define REG_N_CAL_VALID     0x07
#define REG_N_LOG_ADC       0x08
#define REG_N_CROSS_MARGIN  0x09
#define REG_N_ADC           0x0A
#define REG_N_FORCE         0x2E
#define REG_N_CHAN_10K_PD   0x48
#define REG_N_TRIG_METHOD   0x4E
#define REG_N_FLASH_END     0x16


// Registers in config mode
#define REG_C_SCAN_FREQ     0x01
#define REG_C_INT_TIMEOUT   0x02
#define REG_C_ALGO          0x03
#define REG_C_BASELINE_RATE 0x04
#define REG_C_NUM_OPTS      0x05
#define REG_C_OPT1          0x06
#define REG_C_OPT2          0x08
#define REG_C_OPT3          0x0A
#define REG_C_OPT4          0x0C
#define REG_C_OPT5          0x0E
#define REG_C_OPT6          0x10
#define REG_C_OPT7          0x12
#define REG_C_OPT8          0x14
#define REG_C_OPT9          0x16
#define REG_C_OPT10         0x18
#define REG_C_OPT11         0x1A
#define REG_C_OPT12         0x1C
#define REG_C_OPT13         0x1E
#define REG_C_OPT14         0x20
#define REG_C_OPT15         0x22
#define REG_C_OPT16         0x24

//Registers in diags mode
/**
	The commands should match with the FSRModeDiags offset in 
	FSR firmware: whisper_touch/Sources/fsr/fsr.h
	Make sure everytime the FSR firmware structure is changed:
	linux: drivers/input/keyboard/fsr_keypad.h 
	and 
	uboot: opensource/uboot/dist/include/fsr_keypad.h 
	should also changed accordingly
typedef struct __attribute__((packed))
{
       uint8_t reserved1;                               // 0x00
       uint8_t adc_values;                              // 0x01
       uint8_t adc_depth;                               // 0x02
       volatile uint8_t dataready_flag;                 // 0x03
       volatile uint16_t adc[FSR_NUM_CHANNELS];         // 0x04
       uint8_t reserved[24];                            // 0x10
       uint8_t num_cal_weights;                         // 0x28
       uint8_t num_adc_per_weight;                      // 0x29
       uint16_t cal_weights[MAX_NUM_CAL_WEIGHTS];       // 0x2A
       uint16_t cal_data[NUM_BUTTONS][MAX_NUM_CAL_WEIGHTS][NUM_ADC_PER_WEIGHT]; // 0x42
       uint8_t padding[2];                     // 0xA2
} FSRModeDiags;
 */
#define REG_D_NUM_ADC_VALS     0x01
#define REG_D_ADC_DEPTH        0x02
#define REG_D_READY            0x03
#define REG_D_ADC_VALS         0x04
#define REG_D_NUM_CAL_WEIGHTS  0x28
#define REG_D_ADC_PER_WEIGHT   0x29
#define REG_D_CAL_WEIGHTS      0x2A
#define REG_D_CAL_DATA         0x42

#define N_CHANNELS             6
#define N_BUTTONS              4
#define N_WEIGHTS              8
#define CALIB_HEADER_SIZE      (3 + N_WEIGHTS*sizeof(u16))
#define CALIB_ADC_DATA_SIZE    (N_WEIGHTS * N_BUTTONS * sizeof(u16) + 2 * sizeof(u16) ) /*+2 ref sensor*/
#define CALIB_DATA_SIZE        (CALIB_HEADER_SIZE + CALIB_ADC_DATA_SIZE)  /* 87 in our case*/

#define FLAG_ADC_READY_FOR_READ  0xAA

#endif
