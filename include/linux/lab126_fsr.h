/*
 * lab126_fsr.h
 *
 * Copyright (c) 2012-2014 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#ifndef _LAB126_FSR_H
#define _LAB126_FSR_H

#include "linux/lab126_keypad.h"

#define PATH_DEV_FSR_KEYPAD PATH_DEV_KEYPAD

/**
 * This header file contains information used by both kernel,
 * user space and diags
 *
 */

struct __attribute__((packed)) fsr_data {
	uint16_t top_left     ;
	uint16_t bottom_left  ;
	uint16_t top_right    ;
	uint16_t bottom_right ;
	uint16_t ref_left;
	uint16_t ref_right;
};

#define FSR_IOCTL_DATA_SIZE 8

struct __attribute__((packed)) fsr_data_st {
	size_t num_valid_data;
	struct fsr_data data[FSR_IOCTL_DATA_SIZE];
};

struct __attribute__((packed)) fsr_calib_data {
	uint16_t left_top     [8];
	uint16_t left_bottom  [8];
	uint16_t right_top    [8];
	uint16_t right_bottom [8];
	uint16_t ref_sensors  [2];
};

struct __attribute__((packed)) fsr_repeat {
	uint16_t enabled;
	uint16_t delay;
	uint16_t period;
};

/* LAB126 FSR - Ioctl to read 4 data points  */
#define FSR_IOCTL_GET_DATA            _IOR(KEYPAD_MAGIC_NUMBER, 0x11, struct fsr_data_st)
#define FSR_IOCTL_STORE_CALIB         _IOR(KEYPAD_MAGIC_NUMBER, 0x12, struct fsr_calib_data)
#define FSR_IOCTL_GET_PRESSURE        _IOR(KEYPAD_MAGIC_NUMBER, 0x13, uint32_t)
#define FSR_IOCTL_SET_PRESSURE        _IOW(KEYPAD_MAGIC_NUMBER, 0x14, uint32_t)
#define FSR_IOCTL_SET_HAPONUP         _IOW(KEYPAD_MAGIC_NUMBER, 0x15, uint32_t)
#define FSR_IOCTL_GET_HAPONUP         _IOR(KEYPAD_MAGIC_NUMBER, 0x16, uint32_t)
#define FSR_IOCTL_SET_REPEAT          _IOW(KEYPAD_MAGIC_NUMBER, 0x17, struct fsr_repeat)
#define FSR_IOCTL_GET_REPEAT          _IOR(KEYPAD_MAGIC_NUMBER, 0x18, struct fsr_repeat)
#define FSR_IOCTL_GET_PREV_ENABLE     _IOR(KEYPAD_MAGIC_NUMBER, 0x19, int)
#define FSR_IOCTL_SET_PREV_ENABLE     _IOW(KEYPAD_MAGIC_NUMBER, 0x1A, int)
#define FSR_IOCTL_GET_NEXT_ENABLE     _IOR(KEYPAD_MAGIC_NUMBER, 0x1B, int)
#define FSR_IOCTL_SET_NEXT_ENABLE     _IOW(KEYPAD_MAGIC_NUMBER, 0x1C, int)

#endif

