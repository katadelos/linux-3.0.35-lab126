/*
 * lab126_hall.h
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

#ifndef _LAB126_HALL_H
#define _LAB126_HALL_H

struct hall_platform_data {
	int hall_gpio;
	int hall_irq;
	char *desc;
	int wakeup;		/* configure the button as a wake-up source */
	int (*enable)(struct device *dev);
	void (*disable)(struct device *dev);
};

#define HALL_MISC_DEV_NAME "hall"
#define PATH_DEV_HALL "/dev/"HALL_MISC_DEV_NAME

#define HALL_INPUT_CODE	KEY_STOP	
#define HALL_INPUT_TYPE	EV_KEY 

#define HALL_MAGIC_NUMBER		'H'
/* HALL_SENSOR STATES : OPEN=0 CLOSED=1 */
#define HALL_IOCTL_GET_STATE	 _IOR(HALL_MAGIC_NUMBER, 0x01, int)

#endif
