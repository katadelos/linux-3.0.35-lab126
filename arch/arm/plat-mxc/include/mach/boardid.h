/*
 * boardid.h
 *
 * (C) Copyright 2012 Amazon Technologies, Inc.  All rights reserved.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __MACH_BOARDID_H__
#define __MACH_BOARDID_H__

#include <boardid.h>
#include <linux/init.h>

/* boardid.c */
int lab126_board_is(char *id);
int lab126_board_rev_greater(char *id);
int lab126_board_rev_greater_eq(char *id);
int lab126_board_rev_eq(char *id);
char lab126_pcbsn_x(void);
int bootmode_is_diags(void);
int __init lab126_idme_vars_init(void);
void __init early_init_lab126_board_id(void);

extern char lab126_serial_number[];
extern char lab126_mac_address[];

#define LAB126_BOARDS_ENABLE_USB_OTG (lab126_board_is(BOARD_ID_ICEWINE_WARIO_512) ||\
                                          lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO_512) ||\
                                          lab126_board_is(BOARD_ID_MUSCAT_WAN) ||\
                                          lab126_board_is(BOARD_ID_MUSCAT_WFO) ||\
                                          lab126_board_is(BOARD_ID_MUSCAT_32G_WFO) ||\
                                          lab126_board_is(BOARD_ID_WHISKY_WFO) ||\
                                          lab126_board_is(BOARD_ID_WHISKY_WAN))

#define LAB126_BOARD_HAS_WAN  (lab126_board_is(BOARD_ID_PINOT) ||\
                                lab126_board_is(BOARD_ID_PINOT_2GB) ||\
                                lab126_board_is(BOARD_ID_ICEWINE_WARIO_512) ||\
                                lab126_board_is(BOARD_ID_MUSCAT_WAN) ||\
                                lab126_board_is(BOARD_ID_WHISKY_WAN) )

#define CONFIG_BOARD_DISP_TOUCH_LS_IN_MAX77696 \
			(lab126_board_rev_greater(BOARD_ID_WHISKY_WAN_HVT1) ||\
			lab126_board_rev_greater(BOARD_ID_WHISKY_WFO_HVT1) ||\
			lab126_board_rev_greater_eq(BOARD_ID_WOODY_2))

#endif
