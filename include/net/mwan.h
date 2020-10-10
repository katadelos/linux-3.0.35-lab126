/*
 * mwan.h  --  Mario WAN hardware control driver
 *
 * Copyright 2005-2008 Lab126, Inc.  All rights reserved.
 *
 */

#ifndef __MWAN_H__
#define __MWAN_H__

typedef enum {
	WAN_INVALID = -1,
	WAN_OFF,			// 0
	WAN_ON,				// 1
	WAN_OFF_KILL			// 2
} wan_status_t;

extern void wan_set_power_status(wan_status_t);
extern wan_status_t wan_get_power_status(void);

#define MODEM_TYPE_UNKNOWN		(-1)
#define MODEM_TYPE_AD_DTM               0
#define MODEM_TYPE_NVTL_EVDO            1
#define MODEM_TYPE_NVTL_HSPA            2
#define MODEM_TYPE_AD_DTP               3

extern int wan_get_modem_type(void);

typedef void (*wan_usb_wake_callback_t)(void *);

extern wan_usb_wake_callback_t usb_wake_callback;
extern void *usb_wake_callback_data;

#endif // __MWAN_H__

