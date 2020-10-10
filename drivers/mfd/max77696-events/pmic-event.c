/*
 * Copyright 2004-2011 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2012 Amazon.com, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file pmic_event.c
 * @brief This file manage all event of PMIC component.
 *
 * It contains event subscription, unsubscription and callback
 * launch methods implemeted.
 *
 * Modified for Max77696 IRQ framework
 */

/*
 * Includes
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/mfd/max77696-events.h>

/*!
 * This structure is used to keep a list of subscribed
 * callbacks for an event.
 */
typedef struct {
	/*!
	 * Keeps a list of subscribed clients to an event.
	 */
	struct list_head list;

	/*!
	 * Callback function with parameter, called when event occurs
	 */
	pmic_event_callback_t *callback;
} pmic_event_callback_list_t;

/* Create a mutex to be used to prevent concurrent access to the event list */
static DEFINE_MUTEX(event_mutex);

/* This is a pointer to the event handler array. It defines the currently
 * active set of events and user-defined callback functions.
 */
typedef struct {
	struct list_head evt_list;
	pmic_event_callback_list_t cb;
	struct max77696_event_handler *hdl;
	void *objref;
} pmic_events_list_t;

static pmic_events_list_t pmic_events_list;

/*!
 * This function initializes event list for PMIC event handling.
 *
 */
void pmic_event_list_init(void)
{
	INIT_LIST_HEAD(&pmic_events_list.evt_list);
	mutex_init(&event_mutex);
	return;
}

int pmic_event_add_to_list(struct max77696_event_handler *me, void *obj)
{
	struct list_head *evt;
	pmic_events_list_t *new_evt = NULL, *tmp;

       if (mutex_lock_interruptible(&event_mutex)) {
	       return -EINTR;
       }

    	/*search event list */
    	list_for_each(evt, &pmic_events_list.evt_list) {
    	tmp = list_entry(evt, pmic_events_list_t, evt_list);
		if(tmp->hdl->event_id == me->event_id) {
			mutex_unlock(&event_mutex);
			return -EEXIST;
		} 
    	}
   	/* If we are here, then add new event to list */
    	new_evt = kmalloc(sizeof(pmic_events_list_t), GFP_KERNEL);
    	if (NULL == new_evt) {
		mutex_unlock(&event_mutex);
      		return -ENOMEM;
    	}
   	new_evt->hdl = me;
	new_evt->objref = obj;
    	INIT_LIST_HEAD(&new_evt->cb.list);
    	list_add_tail(&new_evt->evt_list, &pmic_events_list.evt_list);
	mutex_unlock(&event_mutex);
	return 0;
}

int pmic_event_del_from_list(struct max77696_event_handler *me)
{
   	struct list_head *evt;
   	pmic_events_list_t *tmp;

        if (mutex_lock_interruptible(&event_mutex)) {
	        return -EINTR;
	}

    	/*search event list */
    	list_for_each(evt, &pmic_events_list.evt_list) {
      		tmp = list_entry(evt, pmic_events_list_t, evt_list);
   		if(tmp->hdl->event_id == me->event_id) {
			if(!list_empty(&tmp->cb.list)) {
				mutex_unlock(&event_mutex);
				pr_debug("\n%s called and callbacks exist!\n", __func__);
				return -EEXIST;
			}		
			list_del(evt);
			kfree(tmp->hdl);
	    		kfree(tmp);
			mutex_unlock(&event_mutex);
			return 0;
      		}
    	}
	mutex_unlock(&event_mutex);
	pr_debug("\n%s called with unknown event[%d]!!\n", __func__, me->event_id);
	return -EINVAL;
}

/*!
 * This function is used to subscribe on an event.
 *
 * @param	event   the event number to be subscribed
 * @param	callback the callback funtion to be subscribed
 *
 * @return       This function returns 0 on SUCCESS, error on FAILURE.
 */
PMIC_STATUS pmic_event_subscribe(u16 event,
				 pmic_event_callback_t *callback)
{
	struct list_head *evt;
	pmic_event_callback_list_t *new_cb = NULL;
	pmic_events_list_t *tmp;

	pr_debug("Event:%d Subscribe\n", event);

	if (NULL == callback->func) {
		pr_debug("Null or Invalid Callback\n");
		return PMIC_PARAMETER_ERROR;
	}
    	if (NULL == callback->param) {
        	pr_debug("Null handler param\n");
        	return PMIC_PARAMETER_ERROR;
    	}
   	/* Lock the mutex */
        if (mutex_lock_interruptible(&event_mutex)) {
		return PMIC_SYSTEM_ERROR_EINTR;
	}

	/*Go through the event list */
	list_for_each(evt, &pmic_events_list.evt_list) {
		tmp = list_entry(evt, pmic_events_list_t, evt_list);
		if(tmp->hdl->event_id == event) {
   			/* Create a new cb list entry */
    			new_cb = kmalloc(sizeof(pmic_event_callback_list_t), GFP_KERNEL);
    			if (NULL == new_cb) {
				mutex_unlock(&event_mutex);
       				return PMIC_MALLOC_ERROR;
    			}
    			/* Initialize the list node fields */
    			new_cb->callback = callback;
    			INIT_LIST_HEAD(&new_cb->list);

            		if(list_empty(&tmp->cb.list)) { 
              		/* unmask the requested event */
			if(tmp->hdl->mask_irq)
              			tmp->hdl->mask_irq(tmp->objref, event, false);
            		}
			list_add_tail(&new_cb->list, &tmp->cb.list);
			goto out;
		}
	}
	mutex_unlock(&event_mutex);	
	pr_debug("\nSubscribe called on unknown event! %d\n", event);
	return PMIC_PARAMETER_ERROR;	
out:
	/* Release the lock */
	mutex_unlock(&event_mutex);
	return PMIC_SUCCESS;
}

/*!
 * This function is used to unsubscribe on an event.
 *
 * @param	event   the event number to be unsubscribed
 * @param	callback the callback funtion to be unsubscribed
 *
 * @return       This function returns 0 on SUCCESS, error on FAILURE.
 */
PMIC_STATUS pmic_event_unsubscribe(u16 event,
				   pmic_event_callback_t *callback)
{
	struct list_head *p, *n;
	struct list_head *q, *m;
    	pmic_event_callback_list_t *temp = NULL;
    	pmic_events_list_t *tmp = NULL;
	int ret = PMIC_EVENT_NOT_SUBSCRIBED;

	pr_debug("Event:%d Unsubscribe\n", event);

	if (NULL == callback->func) {
		pr_debug("Null or Invalid Callback\n");
		return PMIC_PARAMETER_ERROR;
	}

    	if (NULL == callback->param) {
        	pr_debug("Null handler param\n");
        	return PMIC_PARAMETER_ERROR;
    	}

	/* Obtain the lock to access the list */
	if (mutex_lock_interruptible(&event_mutex)) {
		return PMIC_SYSTEM_ERROR_EINTR;
	}

    	if (list_empty(&pmic_events_list.evt_list)) {
       		mutex_unlock(&event_mutex);
       		pr_debug("%s: PMIC event list is empty\n",
            	__func__);
       		return PMIC_ERROR;
    	}

	/* Find the entry in the list */
	list_for_each_safe(p, n, &pmic_events_list.evt_list) {
		tmp = list_entry(p, pmic_events_list_t, evt_list);
      		if(tmp->hdl->event_id == event) {
            		list_for_each_safe(q, m, &tmp->cb.list) {
                		temp = list_entry(q, pmic_event_callback_list_t, list);
				if(temp->callback->func == callback->func
            			&& temp->callback->param == callback->param) {
					list_del_init(q);
					kfree(temp);
					ret = PMIC_SUCCESS;
					break;
				}
			}
			if(list_empty(&tmp->cb.list)) { /* no cb's for this event now */
				/* mask the requested event */
				if(tmp->hdl->mask_irq)
					tmp->hdl->mask_irq(tmp->objref, event, true);

				ret = PMIC_SUCCESS;
				break;
			}
		}
	}
	/* Release the lock */
	mutex_unlock(&event_mutex);
	return ret;
}

/*!
 * This function calls all callback of a specific event.
 *
 * @param	event   the active event number
 *
 * @return 	None
 */
void pmic_event_callback(u16 event)
{
	struct list_head *p, *q;
	pmic_event_callback_list_t *temp = NULL;
	pmic_events_list_t *tmp = NULL;

	/* Obtain the lock to access the list */
	if (mutex_lock_interruptible(&event_mutex)) {
		return;
	}

	if (list_empty(&pmic_events_list.evt_list)) {
		mutex_unlock(&event_mutex);
		pr_debug("pmic event:%d detected. No callback subscribed\n", event);
		return;
	}

   	list_for_each(p, &pmic_events_list.evt_list) {
    		tmp = list_entry(p, pmic_events_list_t, evt_list);
        	if(tmp->hdl->event_id == event) {
			/* invoke all cb's for this event entry */
			list_for_each(q, &tmp->cb.list) {
				temp = list_entry(q, pmic_event_callback_list_t, list);
				if(temp->callback->func)
					temp->callback->func(tmp->objref, temp->callback->param);
			}
			break;	
        	}
    	}
	/* Release the lock */
	mutex_unlock(&event_mutex);
	return;
}

EXPORT_SYMBOL(pmic_event_list_init);
EXPORT_SYMBOL(pmic_event_subscribe);
EXPORT_SYMBOL(pmic_event_unsubscribe);
EXPORT_SYMBOL(pmic_event_callback);
