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

#ifndef _LAB126_KEYPAD_H
#define _LAB126_KEYPAD_H

#define KEYPAD_MISC_DEV_NAME    "keypad"
#define PATH_DEV_KEYPAD         "/dev/"KEYPAD_MISC_DEV_NAME

#define KEYPAD_MAGIC_NUMBER	    'B'

#define KEYPAD_IOCTL_SET_LOCK     _IOW(KEYPAD_MAGIC_NUMBER, 0x01, uint32_t)
#define KEYPAD_IOCTL_GET_LOCK     _IOR(KEYPAD_MAGIC_NUMBER, 0x02, uint32_t)
#define KEYPAD_IOCTL_EVENT_ENABLE _IOW(KEYPAD_MAGIC_NUMBER, 0x03, uint32_t)
#define KEYPAD_IOCTL_TOUCH_ACTIVE _IOW(KEYPAD_MAGIC_NUMBER, 0x04, uint32_t)

#endif //_LAB126_KEYPAD_H

