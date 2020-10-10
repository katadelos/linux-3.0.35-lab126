/*
 * lab126_als.h
 * 
 * Copyright (c) 2012-2013 Amazon.com, Inc. or its affiliates.
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

#ifndef _LAB126_ALS_H
#define _LAB126_ALS_H

#define ALS_MISC_DEV_NAME           "als"
#define PATH_DEV_ALS                "/dev/"ALS_MISC_DEV_NAME


/**
 * This header file contains information used by both kernel, 
 * user space and diags
 *
 */

#define ALS_MAGIC_NUMBER		'A'
/* LAB126 ALS - Ioctl to read LUX */
#define ALS_IOCTL_GET_LUX	 _IOR(ALS_MAGIC_NUMBER, 0x01, int)
#define ALS_IOCTL_AUTOMODE_EN	 _IOW(ALS_MAGIC_NUMBER, 0x02, int)
/* 
 * IOCTL to get count to be converted to lux. 
 * In case using K = expected lux value when calibrating, this return the lux value
 */
#define ALS_IOCTL_GET_COUNT      _IOR(ALS_MAGIC_NUMBER, 0x03, int)
/*
 * Read 0x03 and 0x04 without STOP between the 2 i2c read to disable the 
 * chip from updating the count value while reading.
 */
#define ALS_IOCTL_READ_LUX_REGS  _IOWR(ALS_MAGIC_NUMBER, 0x04, ALS_REGS)

#define ALS_IOCTL_READ_REG	 _IOWR(ALS_MAGIC_NUMBER, 0x05, ALS_REGS)
#define ALS_IOCTL_WRITE_REG      _IOW(ALS_MAGIC_NUMBER, 0x06, ALS_REGS)

typedef struct _ALS_REGS
{
        int             addr;
        unsigned char   value[4];
} ALS_REGS;


#define MAX44009_CLOCK_REG_1            0x09
#define MAX44009_CLOCK_REG_2            0x0A
#define MAX44009_GAIN_REG_1             0x0B
#define MAX44009_GAIN_REG_2             0x0C
#define MAX44009_CONTROL_REG            0x0D

/*
 * When it is set, the chip is set to customer mode.
 * When it is not set, the chip is in default factory mode.
 */
#define OTPSEL_BIT                      0x01


/**
 * When the system is not calibrated, the default gains from manufacturer
 * is based on the reading without glass.
 * Need to initialize the chip with the default gains for T=14% visible transmission.
 * Default value when K = 622, T = 14%.
 *
 */
#define ALS_CAL_DEFAULT_K_622_G1_VAL    107
#define ALS_CAL_DEFAULT_K_622_G2_VAL    77

/**
 * Default value to read the lux directly from count when K = 200
 */
#define ALS_CAL_DEFAULT_K_200_G1_VAL    34
#define ALS_CAL_DEFAULT_K_200_G2_VAL    25

#define MAX44009_MAX_REG        	0x0F

/*
 * Enable this macro if we decice to use the count as lux value.
 */
#define USE_COUNT_AS_LUX_VALUE

#ifdef USE_COUNT_AS_LUX_VALUE

#define ALS_CAL_DEFAULT_G1_VAL	ALS_CAL_DEFAULT_K_200_G1_VAL
#define ALS_CAL_DEFAULT_G2_VAL	ALS_CAL_DEFAULT_K_200_G2_VAL

#define ALS_LUX_PER_COUNT_LOW_RES		16
#define ALS_LUX_PER_COUNT_HI_RES		1

#else // USE_COUNT_AS_LUX_VALUE

#define ALS_CAL_DEFAULT_G1_VAL  ALS_CAL_DEFAULT_K_622_G1_VAL
#define ALS_CAL_DEFAULT_G2_VAL  ALS_CAL_DEFAULT_K_622_G2_VAL
/*
 * To return the original lux value (before glass, divided by 0.14)
 * LOW RES: (18 / 25) / 0.14 = (18 / 25) * (100 / 14) = 36/7
 */
#define ALS_LUX_PER_COUNT_LOW_RES		35 / 7
/*
 * HI RES: (45 / 1000) / 0.14 = ( 45 / 1000) * ( 100 / 14) = 45 /140
 */
#define ALS_LUX_PER_COUNT_HI_RES		45 / 140

#endif // USE_COUNT_AS_LUX_VALUE

#endif

