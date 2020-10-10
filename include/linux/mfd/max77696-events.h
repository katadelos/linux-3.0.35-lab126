/*
 * Copyright 2004-2011 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2012 Amazon Technologies. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __PMIC_EVENTS_H__
#define __PMIC_EVENTS_H__

#include <linux/pmic_external.h>
#include <linux/pmic_status.h>
#include <linux/mfd/max77696.h>

/*!
 * @file pmic.h
 * @brief This file contains prototypes of all the functions to be
 * defined for each PMIC chip. The implementation of these may differ
 * from PMIC chip to PMIC chip.
 *
 * @ingroup PMIC_CORE
 */

#define MAX_ACTIVE_EVENTS	14
#define MAX77696_MAX_EVENTS     14

#define ENCODE_EVENT(x,y) ((x << 8) | (y))
#define DECODE_EVENT(x, y, z) (y) = (((x) >> 8) & 0xFF); (z) = ((x) & 0xFF)

/*!
 * Events(except evt recorder evts) are numbered as a combination of IRQ# and Bit positon of 
 * the event in the interrupt register. MAX77696 has matched bit positions for same event 
 * in all of the int, mask and status regs, so we should be good here
 */
/* TOPSYS Events has IRQs for each bit */
#define EVENT_TOPS_EN0RISE 			ENCODE_EVENT(MAX77696_ROOTINT_TOPSYS, BIT(7))
#define EVENT_TOPS_EN0FALL 			ENCODE_EVENT(MAX77696_ROOTINT_TOPSYS, BIT(6)) 
#define EVENT_TOPS_EN01SEC 			ENCODE_EVENT(MAX77696_ROOTINT_TOPSYS, BIT(5)) 
#define EVENT_TOPS_MRWARN 			ENCODE_EVENT(MAX77696_ROOTINT_TOPSYS, BIT(4)) 
#define EVENT_TOPS_MLOBAT 			ENCODE_EVENT(MAX77696_ROOTINT_TOPSYS, BIT(3)) 
#define EVENT_TOPS_HITEMPALRM1 			ENCODE_EVENT(MAX77696_ROOTINT_TOPSYS, BIT(2)) 
#define EVENT_TOPS_HITEMPALRM2 			ENCODE_EVENT(MAX77696_ROOTINT_TOPSYS, BIT(1)) 

/* PMIC Software Events
 * Software triggered events, for internel or external use to the PMIC driver.
 */
#define EVENT_SW_PARTIALRESTART     ENCODE_EVENT(MAX77696_ROOTINT_SOFTEVT, BIT(7))
#define EVENT_SW_FULLSHUTDWN        ENCODE_EVENT(MAX77696_ROOTINT_SOFTEVT, BIT(6))
#define EVENT_SW_CHGRIN             ENCODE_EVENT(MAX77696_ROOTINT_SOFTEVT, BIT(5))
#define EVENT_SW_CHGROUT            ENCODE_EVENT(MAX77696_ROOTINT_SOFTEVT, BIT(4))
#define EVENT_SW_FACTORYSHIP        ENCODE_EVENT(MAX77696_ROOTINT_SOFTEVT, BIT(3))
#define EVENT_SW_ONKEY_PRESS        ENCODE_EVENT(MAX77696_ROOTINT_SOFTEVT, BIT(2))

/* CHGA Events */
#define EVENT_CHGA_INA 				ENCODE_EVENT(MAX77696_ROOTINT_CHGA, BIT(6))
#define EVENT_CHGA_CHG 				ENCODE_EVENT(MAX77696_ROOTINT_CHGA, BIT(4))
#define EVENT_CHGA_BAT 				ENCODE_EVENT(MAX77696_ROOTINT_CHGA, BIT(3))
#define EVENT_CHGA_THM 				ENCODE_EVENT(MAX77696_ROOTINT_CHGA, BIT(2))
#define EVENT_CHGA_SYS2 			ENCODE_EVENT(MAX77696_ROOTINT_CHGA, BIT(0))

/* EPD Events */
#define EVENT_EPD_POK				ENCODE_EVENT(MAX77696_ROOTINT_EPD, BIT(7))
#define EVENT_EPD_PN				ENCODE_EVENT(MAX77696_ROOTINT_EPD, BIT(6))

/* EPD Virtual Events */
/*
 * These events don't have a bit in the EPD module mask. The second half of
 * their event ID is just an arbitrary, unique value.
 */
#define EVENT_EPD_FAULT				ENCODE_EVENT(MAX77696_ROOTINT_EPD, 0x01)

/* FG Events */
#define EVENT_FG_SMX				ENCODE_EVENT(MAX77696_ROOTINT_FG, BIT(5))
#define EVENT_FG_SMN				ENCODE_EVENT(MAX77696_ROOTINT_FG, BIT(4))
#define EVENT_FG_VMX				ENCODE_EVENT(MAX77696_ROOTINT_FG, BIT(3))
#define EVENT_FG_VMN				ENCODE_EVENT(MAX77696_ROOTINT_FG, BIT(2))
#define EVENT_FG_TMX				ENCODE_EVENT(MAX77696_ROOTINT_FG, BIT(1))
#define EVENT_FG_TMN				ENCODE_EVENT(MAX77696_ROOTINT_FG, BIT(0))

/* UIC Events */
#define EVENT_UIC_DCDTMR			ENCODE_EVENT(MAX77696_ROOTINT_UIC, BIT(7))
#define EVENT_UIC_CHGDETRISE		ENCODE_EVENT(MAX77696_ROOTINT_UIC, BIT(6))
#define EVENT_UIC_CHGDETFALL		ENCODE_EVENT(MAX77696_ROOTINT_UIC, BIT(5))
#define EVENT_UIC_VBVOLT			ENCODE_EVENT(MAX77696_ROOTINT_UIC, BIT(1))
#define EVENT_UIC_CHGTYP			ENCODE_EVENT(MAX77696_ROOTINT_UIC, BIT(0))

struct max77696_event_handler {
	int (*mask_irq)(void *, u16, bool);
	u16 event_id;
};

void pmic_event_list_init(void);

int max77696_eventhandler_register(struct max77696_event_handler *hdl, void *obj);
void max77696_eventhandler_unregister(struct max77696_event_handler *hdl);

int pmic_event_add_to_list(struct max77696_event_handler *me, void *obj);
int pmic_event_del_from_list(struct max77696_event_handler *me);

void pmic_event_callback(u16 event);
PMIC_STATUS pmic_event_unsubscribe(u16 event,
		pmic_event_callback_t *callback);
PMIC_STATUS pmic_event_subscribe(u16 event,
		pmic_event_callback_t *callback);
#endif				/* __PMIC_EVENTS_H__ */
