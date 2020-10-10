/*
 * Copyright 2012 Amazon Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/pmic_external.h>
#include <linux/pmic_status.h>
#include <linux/mfd/max77696-events.h>

int max77696_eventhandler_register(struct max77696_event_handler *hdl, void *obj)
{
	struct max77696_event_handler *me;

    	me = kzalloc(sizeof(*hdl), GFP_KERNEL);
   	if (unlikely(!me)) {
        	pr_debug("%s: out of memory (%uB requested)\n", __func__, sizeof(*me));
        	return -ENOMEM;
    	}

	me->mask_irq = hdl->mask_irq;
	me->event_id = hdl->event_id;

	return pmic_event_add_to_list(me, obj);
}
EXPORT_SYMBOL(max77696_eventhandler_register);

void max77696_eventhandler_unregister(struct max77696_event_handler *hdl)
{
	if (unlikely(!hdl)) {
        	pr_debug("%s: Unexpected driver unregister!\n", __func__);
        	return;
    	}
    	pmic_event_del_from_list(hdl);
}
EXPORT_SYMBOL(max77696_eventhandler_unregister);
