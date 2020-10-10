/*
 * lab126_touch.h
 * Cypress TrueTouch(TM) Standard Product V4 Core driver module.
 * For use with Cypress Txx4xx parts.
 * Supported parts include:
 * TMA4XX
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
 * Contact Cypress Semiconductor at www.cypress.com <ttdrivers@cypress.com>
 *
 */

#ifndef _LINUX_LAB126_TOUCH_H
#define _LINUX_LAB126_TOUCH_H

#define TOUCH_MISC_DEV_NAME "touch"
#define PATH_DEV_TOUCH "/dev/"TOUCH_MISC_DEV_NAME

#define TOUCH_MAGIC_NUMBER                 'Y'
// Standard API
#define TOUCH_IOCTL_GET_LOCK               _IOR(TOUCH_MAGIC_NUMBER, 0x01, int)
#define TOUCH_IOCTL_SET_LOCK               _IOW(TOUCH_MAGIC_NUMBER, 0x02, int)
#define TOUCH_IOCTL_GRIP_SUP_EN            _IOW(TOUCH_MAGIC_NUMBER, 0x03, int)

#endif /* _LINUX_LAB126_TOUCH_H */
