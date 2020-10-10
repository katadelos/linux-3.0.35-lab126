/*
 * mwan-core.c  --  Mario WAN hardware control driver
 *
 * Copyright 2005-2010 Lab126, Inc.  All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <net/mwan.h>


static wan_status_t wan_power_status = WAN_INVALID;


void
wan_set_power_status(
	wan_status_t status)
{
	wan_power_status = status;
}

EXPORT_SYMBOL(wan_set_power_status);


wan_status_t
wan_get_power_status(
	void)
{
	return wan_power_status;
}

EXPORT_SYMBOL(wan_get_power_status);

