/*
 * lab126_haptic.h
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

#ifndef _LAB126_HAPTIC_H
#define _LAB126_HAPTIC_H

#define HAPTIC_MISC_DEV_NAME              "haptic"
#define PATH_DEV_HAPTIC 			"/dev/"HAPTIC_MISC_DEV_NAME

struct drv26xx_effect{
    uint8_t amplitude;
    uint8_t frequency;
    uint8_t duration;
    uint8_t envelope;
};

struct drv26xx_waveform{
    uint8_t repeat_count;
    uint8_t num_effects;
    struct drv26xx_effect *effects;
};

struct drv26xx_waveform_program_info{
    uint8_t waveform_id;
    struct drv26xx_waveform waveform;
};

struct drv26xx_waveform_r1_e1{
    uint8_t waveform_id;
    struct drv26xx_effect waveform;
};

typedef struct _HAPTIC_WAVEFORM_SYNTHESIS
{
	uint16_t amplitude         ; /* Peak = amplitude / 255 * Full Scale Peak Voltage */
	uint16_t frequency         ; /* Sinosoid Freq(Hz) = 7.8125 * frequency */
	uint16_t cycles            ; /* Duration (ms) = 1000 * cycles / (7.8125 * frequency */
	uint16_t envelope          ; /* Ramp up = envelope[7:4], Ramp down = envelope[3:0] */
	/*
	 * Nibble value from 0 - 15 to (ms):
	 * no envelope, 32, 64, 96, 192, 224, 256, 512, 768, 1024, 1280, 1536, 1792, 2048
	 */
	uint16_t gain              ; /* gain */
	uint16_t repeat_count      ;
} HAPTIC_WAVEFORM_SYNTHESIS;

#define HAPTIC_WAVEFORM_ID_DIAGS  2

#define HAPTIC_MAGIC_NUMBER		'C'
/* LAB126 HAPTIC - Ioctl */
#define HAPTIC_IOCTL_RESET           _IOW(HAPTIC_MAGIC_NUMBER, 0x01, int)
#define HAPTIC_IOCTL_SET_WAVEFORM    _IOW(HAPTIC_MAGIC_NUMBER, 0x02, struct drv26xx_waveform_r1_e1)
#define HAPTIC_IOCTL_GET_WAVEFORM    _IOR(HAPTIC_MAGIC_NUMBER, 0x03, struct drv26xx_waveform_r1_e1)
#define HAPTIC_IOCTL_SET_GAIN        _IOW(HAPTIC_MAGIC_NUMBER, 0x04, int)
#define HAPTIC_IOCTL_GET_GAIN        _IOR(HAPTIC_MAGIC_NUMBER, 0x05, int)
#define HAPTIC_IOCTL_SET_LOCK        _IOW(HAPTIC_MAGIC_NUMBER, 0x06, int)
#define HAPTIC_IOCTL_GET_LOCK        _IOR(HAPTIC_MAGIC_NUMBER, 0x07, int)
#define HAPTIC_IOCTL_PLAY_DIAGS_WF   _IOW(HAPTIC_MAGIC_NUMBER, 0x08, int)



#endif

