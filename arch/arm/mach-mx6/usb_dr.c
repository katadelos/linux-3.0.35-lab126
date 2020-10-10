/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define CONFIG_USB_EHCI_ARC_OTG

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <mach/arc_otg.h>
#include <mach/hardware.h>
#include "devices-imx6q.h"
#include "regs-anadig.h"
#include "usb.h"
#include <mach/boardid.h>
#include <linux/proc_fs.h>
#include <boardid.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

static struct delayed_work otg_enum_check;
static struct delayed_work otg_recovery_work;

DEFINE_MUTEX(otg_wakeup_enable_mutex);
static int usbotg_init_ext(struct platform_device *pdev);
static void boot_otg_detect(struct platform_device *pdev);
static void usbotg_uninit_ext(struct platform_device *pdev);
static void usbotg_clock_gate(bool on);
static void _dr_discharge_line(bool enable);
extern bool usb_icbug_swfix_need(void);
static void enter_phy_lowpower_suspend(struct fsl_usb2_platform_data *pdata, \
								bool enable);

void usb_otg_esd_recovery(void);

extern int max77696_led_set_manual_mode(unsigned int led_id, bool manual_mode);
extern int max77696_led_ctrl(unsigned int led_id, int led_state);

#ifdef CONFIG_POWER_SODA
extern bool soda_charger_docked(void);
extern void soda_otg_vbus_output(int en, int ping);
#endif

/* The usb_phy1_clk do not have enable/disable function at clock.c
 * and PLL output for usb1's phy should be always enabled.
 * usb_phy1_clk only stands for usb uses pll3 as its parent.
 */
static struct clk *usb_phy1_clk;
static struct clk *usb_oh3_clk;
static u8 otg_used;
DEFINE_MUTEX(usb_clk_mutex);

static void usbotg_wakeup_event_clear(void);
extern int clk_get_usecount(struct clk *clk);
extern int max77696_uic_is_otg_connected(void);

/* VBus, ID Reset delay in ms
 * this delay is needed for user-space daemons
 * to recover completely JSEVENONE-4221
 */
#define RESET_DELAY			6000
#define ENUMCHK_RECOVERY_TRIGGER_DELAY	8000
#define ESD_RECOVERY_DEBOUNCE  3000

/* Beginning of Common operation for DR port */

/*
 * platform data structs
 * 	- Which one to use is determined by CONFIG options in usb.h
 * 	- operating_mode plugged at run time
 */
static struct fsl_usb2_platform_data dr_utmi_config = {
	.name              = "DR",
	.init              = usbotg_init_ext,
	.late_init         = boot_otg_detect,
	.exit              = usbotg_uninit_ext,
	.phy_mode          = FSL_USB2_PHY_UTMI_WIDE,
	.power_budget      = 500,		/* 500 mA max power */
	.usb_clock_for_pm  = usbotg_clock_gate,
	.transceiver       = "utmi",
	.phy_regs = USB_PHY0_BASE_ADDR,
	.dr_discharge_line = _dr_discharge_line,
	.lowpower	       = true, /* Default driver low power is true */
};

#ifdef CONFIG_MFD_MAX77696

#include <linux/mfd/max77696-events.h>
#include <linux/pmic_external.h>
#include <linux/regulator/driver.h>

#define BOARDID_NEEDS_USB_OVERRIDE ( \
lab126_board_is(BOARD_ID_MUSCAT_WAN) || \
lab126_board_is(BOARD_ID_MUSCAT_WFO) || \
lab126_board_is(BOARD_ID_MUSCAT_32G_WFO) || \
lab126_board_is(BOARD_ID_ICEWINE_WARIO_512) || \
lab126_board_is(BOARD_ID_ICEWINE_WFO_WARIO_512) || \
lab126_board_rev_greater_eq(BOARD_ID_ICEWINE_WARIO_P5) || \
lab126_board_rev_greater_eq(BOARD_ID_ICEWINE_WFO_WARIO_P5) || \
lab126_board_is(BOARD_ID_PINOT) || \
lab126_board_rev_greater_eq(BOARD_ID_PINOT_WFO_EVT1) || \
lab126_board_is(BOARD_ID_PINOT_2GB) || \
lab126_board_rev_greater_eq(BOARD_ID_PINOT_WFO_2GB_EVT1) || \
lab126_board_rev_greater_eq(BOARD_ID_WARIO_2) || \
lab126_board_is(BOARD_ID_BOURBON_WFO) || \
lab126_board_is(BOARD_ID_BOURBON_WFO_PREEVT2) || \
lab126_board_is(BOARD_ID_WHISKY_WAN) || \
lab126_board_is(BOARD_ID_WHISKY_WFO) || \
lab126_board_is(BOARD_ID_WOODY) )

int (* otg_sethost)(bool, void *) = NULL;
EXPORT_SYMBOL(otg_sethost);
void *otg_sethost_data = NULL;
EXPORT_SYMBOL(otg_sethost_data);
extern int max77696_charger_get_connected(void);
extern bool max77696_uic_is_usbhost(void);
DEFINE_MUTEX(udc_phy_force_mutex);
static bool detect_forced = false;

static bool usbmode_is_host = 0; 

extern int max77696_charger_set_otg(int enable);
extern int max77696_uic_is_otg_connected(void);
extern int max77696_uic_is_otg_connected_irq(void);

#ifdef CONFIG_POWER_SODA
extern atomic_t soda_usb_charger_connected;
#endif

static void usbotg_force_bsession(bool connected)
{
	u32 contents;
	contents = __raw_readl(MX6_IO_ADDRESS(ANATOP_BASE_ADDR) + HW_ANADIG_USB1_VBUS_DETECT);

	contents |= BM_ANADIG_USB1_VBUS_DETECT_VBUS_OVERRIDE_EN;

	if (connected) {
		contents |= BM_ANADIG_USB1_VBUS_DETECT_BVALID_OVERRIDE;
		contents |= BM_ANADIG_USB1_VBUS_DETECT_VBUSVALID_OVERRIDE;
		contents &= ~BM_ANADIG_USB1_VBUS_DETECT_SESSEND_OVERRIDE;
	} else {
		contents &= ~BM_ANADIG_USB1_VBUS_DETECT_BVALID_OVERRIDE;
		contents &= ~BM_ANADIG_USB1_VBUS_DETECT_VBUSVALID_OVERRIDE;
		contents |= BM_ANADIG_USB1_VBUS_DETECT_SESSEND_OVERRIDE;
	}
	__raw_writel(contents, MX6_IO_ADDRESS(ANATOP_BASE_ADDR) + HW_ANADIG_USB1_VBUS_DETECT);
}


int audio_enumerated;
EXPORT_SYMBOL(audio_enumerated);
extern int max77696_charger_set_icl(void);

void otg_check_enumeration(int ms)
{
	schedule_delayed_work(&otg_enum_check, msecs_to_jiffies(ms));
}
EXPORT_SYMBOL(otg_check_enumeration);

/*
 * internal function being call by recovery or enumeration works in this driver (usb_dr.c)
 * the actual function exported is a version that could cancel all pending works and run this internal function
 * */
void asession_enable__(bool connected) 
{
	u32 contents;
	mutex_lock(&udc_phy_force_mutex);

	if(usbmode_is_host != connected ) {

		contents = __raw_readl(MX6_IO_ADDRESS(ANATOP_BASE_ADDR) + HW_ANADIG_USB1_VBUS_DETECT);
			
		contents |= BM_ANADIG_USB1_VBUS_DETECT_VBUS_OVERRIDE_EN;

		if (connected) {
			contents |= BM_ANADIG_USB1_VBUS_DETECT_AVALID_OVERRIDE;
			contents |= BM_ANADIG_USB1_VBUS_DETECT_VBUSVALID_OVERRIDE;
			contents &= ~BM_ANADIG_USB1_VBUS_DETECT_SESSEND_OVERRIDE;
		} else {
			contents &= ~BM_ANADIG_USB1_VBUS_DETECT_AVALID_OVERRIDE;
			contents &= ~BM_ANADIG_USB1_VBUS_DETECT_VBUSVALID_OVERRIDE;
			contents |= BM_ANADIG_USB1_VBUS_DETECT_SESSEND_OVERRIDE;
		}
		__raw_writel(contents, MX6_IO_ADDRESS(ANATOP_BASE_ADDR) + HW_ANADIG_USB1_VBUS_DETECT);
		if (otg_sethost) {
			otg_sethost(connected, otg_sethost_data);
		}
		usbmode_is_host = connected;
	}
	if(connected) {
		printk(KERN_DEBUG "\nSchedule OTG enumeration triggered recovery\n");
		otg_check_enumeration(ENUMCHK_RECOVERY_TRIGGER_DELAY);
	}
	mutex_unlock(&udc_phy_force_mutex);
}

/*
 * this is the entry point of OTG connect or disconnect
 * it should take precedence over whatever pending about OTG
 * make sure 
 * */
void asession_enable(bool connected) 
{
	cancel_delayed_work_sync(&otg_enum_check);
	cancel_delayed_work_sync(&otg_recovery_work);
	asession_enable__(connected);
}
EXPORT_SYMBOL(asession_enable);

static void otg_enum_fn(struct work_struct *work)
{
	int reset_delay = (usbmode_is_host == 0 ? 0 : RESET_DELAY);
#ifdef CONFIG_POWER_SODA
	if(!audio_enumerated && soda_charger_docked() && max77696_uic_is_otg_connected() ) {
		printk(KERN_ERR "with soda: %s not enumerated, catcha!", __func__);

		asession_enable__(false);
		soda_otg_vbus_output(0, 0);	
		msleep(reset_delay);
		soda_otg_vbus_output(1, 0);	
		asession_enable__(true);
		max77696_charger_set_icl();
	}else {
		//printk(KERN_ERR "okay, either otg is not connected, or connected and enumerated successfully. ");
		printk(KERN_ERR "is_otg_connected() %d audio_enumerated %d, soda_charger_docked %d", max77696_uic_is_otg_connected(), audio_enumerated, soda_charger_docked());
	}
#else
	if(!audio_enumerated && max77696_uic_is_otg_connected() ) {
		printk(KERN_ERR " %s not enumerated, catcha!", __func__);
		asession_enable__(false);
		max77696_charger_set_otg(0);
		msleep(reset_delay);
		max77696_charger_set_otg(1);
		asession_enable__(true);
		/* Turn OFF amber led - manual mode */
		max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_OFF);
		/* Turn OFF green led - manual mode */
		max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
	}else {
		//printk(KERN_ERR "okay, either otg is not connected, or connected and enumerated successfully. ");
		printk(KERN_ERR "is_otg_connected() %d audio_enumerated %d", max77696_uic_is_otg_connected(), audio_enumerated);
	}
	
#endif
}

static void otg_recovery_work_fn(struct work_struct *work)
{
	usb_otg_esd_recovery();
}

void otg_esd_recovery_kick(int ms)
{
	int ret;
	printk(KERN_DEBUG "%s() recovery will happen in %d ms", __func__, ms);
	ret = cancel_delayed_work_sync(&otg_recovery_work);
	printk(KERN_DEBUG "%s() cancel sync previous %d", __func__, ret);
	schedule_delayed_work(&otg_recovery_work, msecs_to_jiffies(ms));
}
EXPORT_SYMBOL(otg_esd_recovery_kick);

static void arcotg_usb_force_detect(bool detect) {

	mutex_lock(&udc_phy_force_mutex);

	if (detect != detect_forced) {
		struct regulator *phyreg = regulator_get(NULL, "USB_GADGET_PHY");
		if (!IS_ERR(phyreg)) {
			if (detect) {
				regulator_enable(phyreg);
				usbotg_force_bsession(true);
			} else {
				usbotg_force_bsession(false);
				regulator_disable(phyreg);
			}
		} else {
			printk(KERN_ERR "Could not get USB PHY Regulator\n");
		}
		regulator_put(phyreg);
		detect_forced = detect;
	}
	mutex_unlock(&udc_phy_force_mutex);
}

int asession_write(struct file *file, const char __user *buffer, unsigned long count, void *data) {
	
	int detect = 99, param = 0;
	struct regulator *wan_reg;

	sscanf(buffer, "%d %d", &detect, &param);
	switch(detect)
	{
		case 0:
		case 1:
			asession_enable__(!!detect);
			break;
		
		case 3:
			usbotg_clock_gate(0);
			break;
		case 4:
			usbotg_clock_gate(1);
			break;
		case 41:
			clk_enable(usb_phy1_clk);
			printk(KERN_DEBUG "clk_enable(usb_phy1_clk); clk_get_usecount(usb_phy1_clk) %d", clk_get_usecount(usb_phy1_clk));
			break;
		case 42:
			clk_enable(usb_oh3_clk);
			printk(KERN_DEBUG "clk_enable(usb_oh3_clk); clk_get_usecount(usb_oh3_clk) %d", clk_get_usecount(usb_oh3_clk));
			break;
		case 43:
			clk_disable(usb_phy1_clk);
			printk(KERN_DEBUG "clk_disable(usb_phy1_clk); clk_get_usecount(usb_phy1_clk) %d", clk_get_usecount(usb_phy1_clk));
			break;
		case 44:
			clk_disable(usb_oh3_clk);
			printk(KERN_DEBUG "clk_disable(usb_oh3_clk); clk_get_usecount(usb_oh3_clk) %d", clk_get_usecount(usb_oh3_clk));
			break;
		case 5:
			arcotg_usb_force_detect(!!param);
			break;
		case 6:
			usbotg_force_bsession(!!param);
			break;
		case 7:
			wan_reg = regulator_get(NULL, "WAN_USB_HOST_PHY");
			if (!IS_ERR(wan_reg)) 
				regulator_enable(wan_reg);
			regulator_put(wan_reg);
			break;
		case 8:
			wan_reg = regulator_get(NULL, "WAN_USB_HOST_PHY");
			if (!IS_ERR(wan_reg))
				regulator_disable(wan_reg);
			regulator_put(wan_reg);
			break;
		case 9:
			wan_reg = regulator_get(NULL, "USB_GADGET_PHY");
			if (!IS_ERR(wan_reg)) 
				regulator_enable(wan_reg);
			regulator_put(wan_reg);
			break;
		case 10:
			wan_reg = regulator_get(NULL, "USB_GADGET_PHY");
			if (!IS_ERR(wan_reg))
				regulator_disable(wan_reg);
			regulator_put(wan_reg);
			break;	
		case 11:
			printk(KERN_DEBUG "User-space triggered recovery /proc/asession entry 11\n");
			otg_esd_recovery_kick(ESD_RECOVERY_DEBOUNCE);
			break;
		default:
			break;
	}
	return count;
}

/**** PROC ENTRY ****/
static int asession_proc_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;

	
	len = sprintf(page, "max77696_charger_get_connected() %d \n"
						"max77696_uic_is_usbhost() %d \n "
						"max77696_uic_is_otg_connected() %d\n "
						 "clk_get_usecount(usb_oh3_clk), %d \n"
						 "clk_get_usecount(usb_phy1_clk) %d\n"
						 ,
				max77696_charger_get_connected(),
				max77696_uic_is_usbhost(),
				max77696_uic_is_otg_connected() ,
				clk_get_usecount(usb_oh3_clk), clk_get_usecount(usb_phy1_clk) );
	
	return len;
}

void setup_asession_proc(struct platform_device* pdev) {
	struct proc_dir_entry *asession_proc_entry;

	asession_proc_entry = create_proc_entry("asession", S_IRUGO, NULL);

	if (asession_proc_entry) {
		asession_proc_entry->read_proc = asession_proc_read;
		asession_proc_entry->write_proc = asession_write;
		asession_proc_entry->data = pdev;
	} else {
		printk(KERN_ERR "Failed to initialize asession proc entry\n");
	}
}

static void pmic_chgin_cb (void *obj, void *param) {
	
#if defined(CONFIG_POWER_SODA)
	return; //driven by pmic_soda_connects_notify_usb_in
#else
	arcotg_usb_force_detect(true);
#endif
};

static void pmic_chgout_cb (void *obj, void *param) {
	
#if defined(CONFIG_POWER_SODA)
	return; //driven by pmic_soda_connects_notify_usb_out
#else
	arcotg_usb_force_detect(false);
#endif
};

#if defined(CONFIG_POWER_SODA)
void pmic_soda_connects_notify_usb_in(void)
{
	arcotg_usb_force_detect(true);
}
EXPORT_SYMBOL(pmic_soda_connects_notify_usb_in);

void pmic_soda_connects_notify_usb_out(void){
	arcotg_usb_force_detect(false);
}
EXPORT_SYMBOL(pmic_soda_connects_notify_usb_out);
#endif

static pmic_event_callback_t pmic_chgin = {
	.func = pmic_chgin_cb,
};
static pmic_event_callback_t pmic_chgout = {
	.func = pmic_chgout_cb,
};

#endif

/* Platform data for wakeup operation */
static struct fsl_usb2_wakeup_platform_data dr_wakeup_config = {
	.name = "DR wakeup",
	.usb_clock_for_pm  = usbotg_clock_gate,
	.usb_wakeup_exhandle = usbotg_wakeup_event_clear,
};

static void usbotg_internal_phy_clock_gate(bool on)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);
	if (on) {
		__raw_writel(BM_USBPHY_CTRL_CLKGATE, phy_reg + HW_USBPHY_CTRL_CLR);
	} else {
		__raw_writel(BM_USBPHY_CTRL_CLKGATE, phy_reg + HW_USBPHY_CTRL_SET);
	}
}

int usb_dr_get_clock_ref(void)
{
	int ret;
	mutex_lock(&usb_clk_mutex);
	ret = min(clk_get_usecount(usb_oh3_clk), clk_get_usecount(usb_phy1_clk));
	mutex_unlock(&usb_clk_mutex);
	return ret;
}
EXPORT_SYMBOL(usb_dr_get_clock_ref);

static int usb_phy_enable(struct fsl_usb2_platform_data *pdata)
{
	u32 tmp;
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);
	void __iomem *phy_ctrl;

	/* Stop then Reset */
	UOG_USBCMD &= ~UCMD_RUN_STOP;
	while (UOG_USBCMD & UCMD_RUN_STOP)
		;

	UOG_USBCMD |= UCMD_RESET;
	while ((UOG_USBCMD) & (UCMD_RESET))
		;

	/*
	 * If the controller reset does not put the PHY be out of
	 * low power mode, do it manually.
	 */
	if (UOG_PORTSC1 & PORTSC_PHCD) {
		UOG_PORTSC1 &= ~PORTSC_PHCD;	
	}
	/* Wait PHY clock stable */
	mdelay(1);
	/* Reset USBPHY module */
	phy_ctrl = phy_reg + HW_USBPHY_CTRL;
	tmp = __raw_readl(phy_ctrl);
	tmp |= BM_USBPHY_CTRL_SFTRST;
	__raw_writel(tmp, phy_ctrl);
	udelay(10);

	/* Remove CLKGATE and SFTRST */
	tmp = __raw_readl(phy_ctrl);
	tmp &= ~(BM_USBPHY_CTRL_CLKGATE | BM_USBPHY_CTRL_SFTRST);
	__raw_writel(tmp, phy_ctrl);
	udelay(10);

	/* Power up the PHY */
	__raw_writel(0, phy_reg + HW_USBPHY_PWD);
	if ((pdata->operating_mode == FSL_USB2_DR_HOST) ||
			(pdata->operating_mode == FSL_USB2_DR_OTG)) {
		/* enable FS/LS device */
		__raw_writel(BM_USBPHY_CTRL_ENUTMILEVEL2 | BM_USBPHY_CTRL_ENUTMILEVEL3
				, phy_reg + HW_USBPHY_CTRL_SET);
	}

	if (!usb_icbug_swfix_need())
		__raw_writel((1 << 17), phy_reg + HW_USBPHY_IP_SET);
	if (cpu_is_mx6sl())
		__raw_writel((1 << 18), phy_reg + HW_USBPHY_IP_SET);

#ifdef CONFIG_LAB126
	/* Set Termination Impedance for Wario platform and leave
	 * USBPHY_TX_EDGECTRL as default */
	__raw_writel(
		(__raw_readl(phy_reg + HW_USBPHY_TX) &
			BM_USBPHY_TX_USBPHY_TX_EDGECTRL) |

		BF_USBPHY_TX_TXCAL45DP(0x9) |
		BF_USBPHY_TX_TXCAL45DN(0x9) |
		BF_USBPHY_TX_D_CAL(0x1),
		phy_reg + HW_USBPHY_TX);

	/* Set Receiver sensitivity for Wario Platform ENVADJ=100mV */
	__raw_writel(__raw_readl(phy_reg + HW_USBPHY_RX) |
		BF_USBPHY_RX_ENVADJ(0x1),
		phy_reg + HW_USBPHY_RX);
#endif

	return 0;
}

void usb_otg_esd_recovery(void)
{
#ifdef CONFIG_POWER_SODA
	if(max77696_uic_is_otg_connected() && soda_charger_docked()) {
		asession_enable__(false);
		soda_otg_vbus_output(0, 0);
		msleep(RESET_DELAY);
		soda_otg_vbus_output(1, 0);
		asession_enable__(true);
		
	}else {
		printk(KERN_ERR "not a usb host or no soda connected, doing nothing");
	}
#else
	if(max77696_uic_is_otg_connected()) {
		asession_enable__(false);
		max77696_charger_set_otg(0);
		
		msleep(RESET_DELAY);
		
		max77696_charger_set_otg(1);
		asession_enable__(true);
		/* Turn OFF amber led - manual mode */
		max77696_led_ctrl(MAX77696_LED_AMBER, MAX77696_LED_OFF);
		/* Turn OFF green led - manual mode */
		max77696_led_ctrl(MAX77696_LED_GREEN, MAX77696_LED_OFF);
	}else {
		printk(KERN_ERR "not a usb host, doing nothing");
	}
#endif

}
EXPORT_SYMBOL(usb_otg_esd_recovery);

static void boot_otg_detect(struct platform_device *pdev)
{
	if ( max77696_charger_get_connected() && max77696_uic_is_usbhost()) {
		printk(KERN_INFO "boot:USB host cable is already plugged in. Simulating event\n");
		arcotg_usb_force_detect(false);
		arcotg_usb_force_detect(true);
	}else if (max77696_uic_is_otg_connected()) {
		printk(KERN_INFO "boot:USB OTG cable might be already plugged in. Simulating event otg_check_enumeration;\n");
		usbmode_is_host = 0; //so that from otg_enum_fn -> asession_enable__(0) does nothing, but asession_enable__(1) will kick in  
		otg_check_enumeration(1000);
	}
}

/* Notes: configure USB clock and setup PMIC events */
static int usbotg_init_ext(struct platform_device *pdev)
{
	struct clk *usb_clk;
	u32 ret;

	/* at mx6q: this clock is AHB clock for usb core */
	usb_clk = clk_get(NULL, "usboh3_clk");
	clk_enable(usb_clk);
	usb_oh3_clk = usb_clk;

	usb_clk = clk_get(NULL, "usb_phy1_clk");
	clk_enable(usb_clk);
	usb_phy1_clk = usb_clk;

	ret = usbotg_init(pdev);
	if (ret) {
		printk(KERN_ERR "otg init fails......\n");
		return ret;
	}
	if (!otg_used) {
		usb_phy_enable(pdev->dev.platform_data);
		enter_phy_lowpower_suspend(pdev->dev.platform_data, false);
		/*after the phy reset,can not read the readingvalue for id/vbus at
		* the register of otgsc ,cannot  read at once ,need delay 3 ms
		*/
		mdelay(3);
	}
	otg_used++;

#ifdef CONFIG_MFD_MAX77696
	/* set session end override as default */
	usbotg_force_bsession(false);

	if(BOARDID_NEEDS_USB_OVERRIDE) {
		pmic_chgin.param = (void *) pdev;
		ret = pmic_event_subscribe(EVENT_SW_CHGRIN, &pmic_chgin);
		if (unlikely(ret)) {
			printk(KERN_ERR "Could not register charger in callback.\n");
		}

		pmic_chgout.param = (void *) pdev;
		ret = pmic_event_subscribe(EVENT_SW_CHGROUT, &pmic_chgout);
		if (unlikely(ret)) {
			printk(KERN_ERR "Could not register charger out callback.\n");
		}

#ifndef CONFIG_USB_OTG
		if ( max77696_charger_get_connected() && max77696_uic_is_usbhost()) {
			printk("USB Cable is already plugged in. Simulating event\n");
			arcotg_usb_force_detect(true);
		} 
#endif //not defined CONFIG_USB_OTG

	}
#endif

	return ret;
}

static void usbotg_uninit_ext(struct platform_device *pdev)
{
	otg_used--;
	if (!otg_used) {
		clk_put(usb_phy1_clk);
		clk_put(usb_oh3_clk);
	}

#ifdef CONFIG_MFD_MAX77696

	if(BOARDID_NEEDS_USB_OVERRIDE) {
		pmic_event_unsubscribe(EVENT_SW_CHGROUT, &pmic_chgout);
		pmic_event_unsubscribe(EVENT_SW_CHGRIN, &pmic_chgin);
	}
#endif
}

void usbotg_clock_gate(bool on)
{
	pr_debug("%s: on is %d\n", __func__, on);
	mutex_lock(&usb_clk_mutex);
	if( (on == false) && ((clk_get_usecount(usb_oh3_clk) == 0) || (clk_get_usecount(usb_phy1_clk) == 0)) ) {
		goto out;
	}
	if (on) {
		clk_enable(usb_oh3_clk);
		clk_enable(usb_phy1_clk);
	} else {
		clk_disable(usb_phy1_clk);
		clk_disable(usb_oh3_clk);
	}
out:
	mutex_unlock(&usb_clk_mutex);
	pr_debug("usb_oh3_clk:%d, usb_phy_clk1_ref_count:%d\n", clk_get_usecount(usb_oh3_clk), clk_get_usecount(usb_phy1_clk));
}

EXPORT_SYMBOL(usbotg_clock_gate);

static void dr_platform_phy_power_on(void)
{
	void __iomem *anatop_base_addr = MX6_IO_ADDRESS(ANATOP_BASE_ADDR);
	__raw_writel(BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG,
				anatop_base_addr + HW_ANADIG_ANA_MISC0_SET);
}


static void _dr_discharge_line(bool enable)
{
	void __iomem *anatop_base_addr = MX6_IO_ADDRESS(ANATOP_BASE_ADDR);
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);

	pr_debug("DR: %s  enable is %d\n", __func__, enable);
	if (enable) {
		__raw_writel(BM_USBPHY_DEBUG_CLKGATE, phy_reg + HW_USBPHY_DEBUG_CLR);
		__raw_writel(BM_ANADIG_USB1_LOOPBACK_UTMI_DIG_TST1
					|BM_ANADIG_USB1_LOOPBACK_TSTI_TX_EN,
			anatop_base_addr + HW_ANADIG_USB1_LOOPBACK);
	} else {
		__raw_writel(0x0,
			anatop_base_addr + HW_ANADIG_USB1_LOOPBACK);
		__raw_writel(BM_USBPHY_DEBUG_CLKGATE, phy_reg + HW_USBPHY_DEBUG_SET);
	}
}

/* Below two macros are used at otg mode to indicate usb mode*/
#define ENABLED_BY_HOST   (0x1 << 0)
#define ENABLED_BY_DEVICE (0x1 << 1)
static u32 low_power_enable_src; /* only useful at otg mode */
static void enter_phy_lowpower_suspend(struct fsl_usb2_platform_data *pdata, bool enable)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);
	u32 tmp;
	pr_debug("DR: %s begins, enable is %d\n", __func__, enable);

	if (enable) {
		UOG_PORTSC1 |= PORTSC_PHCD;
		tmp = (BM_USBPHY_PWD_TXPWDFS
			| BM_USBPHY_PWD_TXPWDIBIAS
			| BM_USBPHY_PWD_TXPWDV2I
			| BM_USBPHY_PWD_RXPWDENV
			| BM_USBPHY_PWD_RXPWD1PT1
			| BM_USBPHY_PWD_RXPWDDIFF
			| BM_USBPHY_PWD_RXPWDRX);
		__raw_writel(tmp, phy_reg + HW_USBPHY_PWD_SET);
		usbotg_internal_phy_clock_gate(false);

	} else {
		if (UOG_PORTSC1 & PORTSC_PHCD) {
			UOG_PORTSC1 &= ~PORTSC_PHCD;
		}
		
		/* Wait PHY clock stable */
		mdelay(1);
		
		usbotg_internal_phy_clock_gate(true);
                udelay(2);

		tmp = (BM_USBPHY_PWD_TXPWDFS
			| BM_USBPHY_PWD_TXPWDIBIAS
			| BM_USBPHY_PWD_TXPWDV2I
			| BM_USBPHY_PWD_RXPWDENV
			| BM_USBPHY_PWD_RXPWD1PT1
			| BM_USBPHY_PWD_RXPWDDIFF
			| BM_USBPHY_PWD_RXPWDRX);
		__raw_writel(tmp, phy_reg + HW_USBPHY_PWD_CLR);
		/*
		 * The PHY works at 32Khz clock when it is at low power mode,
		 * it needs 10 clocks from 32Khz to normal work state, so
		 * 500us is the safe value for PHY enters stable status
		 * according to IC engineer.
		 *
		 * Besides, the digital value needs 1ms debounce time to
		 * wait the value to be stable. We have expected the
		 * value from OTGSC is correct after calling this API.
		 *
		 * So delay 2ms is a safe value.
		 */
		mdelay(2);

	}
	pr_debug("DR: %s ends, enable is %d\n", __func__, enable);
}

static void __phy_lowpower_suspend(struct fsl_usb2_platform_data *pdata, bool enable, int source)
{
	if (enable) {
		low_power_enable_src |= source;
#ifdef CONFIG_USB_OTG
		if (low_power_enable_src == (ENABLED_BY_HOST | ENABLED_BY_DEVICE)) {
			pr_debug("phy lowpower enabled\n");
			enter_phy_lowpower_suspend(pdata, enable);
		}
#else
		enter_phy_lowpower_suspend(pdata, enable);
#endif
	} else {
		pr_debug("phy lowpower disable\n");
		enter_phy_lowpower_suspend(pdata, enable);
		low_power_enable_src &= ~source;
	}
}

static void otg_wake_up_enable(struct fsl_usb2_platform_data *pdata, bool enable)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);

	pr_debug("%s, enable is %d\n", __func__, enable);
	if (enable) {
		__raw_writel(BM_USBPHY_CTRL_ENDPDMCHG_WKUP
				| BM_USBPHY_CTRL_ENAUTOSET_USBCLKS
				| BM_USBPHY_CTRL_ENAUTOCLR_PHY_PWD
				| BM_USBPHY_CTRL_ENAUTOCLR_CLKGATE
				| BM_USBPHY_CTRL_ENAUTOCLR_USBCLKGATE
				| BM_USBPHY_CTRL_ENAUTO_PWRON_PLL , phy_reg + HW_USBPHY_CTRL_SET);
		USB_OTG_CTRL |= UCTRL_OWIE;
	} else {
		__raw_writel(BM_USBPHY_CTRL_ENDPDMCHG_WKUP
				| BM_USBPHY_CTRL_ENAUTOSET_USBCLKS
				| BM_USBPHY_CTRL_ENAUTOCLR_PHY_PWD
				| BM_USBPHY_CTRL_ENAUTOCLR_CLKGATE
				| BM_USBPHY_CTRL_ENAUTOCLR_USBCLKGATE
				| BM_USBPHY_CTRL_ENAUTO_PWRON_PLL , phy_reg + HW_USBPHY_CTRL_CLR);
		USB_OTG_CTRL &= ~UCTRL_OWIE;
		/* The interrupt must be disabled for at least 3 clock
		 * cycles of the standby clock(32k Hz) , that is 0.094 ms*/
		udelay(100);
	}
}

static u32 wakeup_irq_enable_src; /* only useful at otg mode */
static void __wakeup_irq_enable(struct fsl_usb2_platform_data *pdata, bool on, int source)
 {
	/* otg host and device share the OWIE bit, only when host and device
	 * all enable the wakeup irq, we can enable the OWIE bit
	 */
	mutex_lock(&otg_wakeup_enable_mutex);
	if (on) {
#ifdef CONFIG_USB_OTG
		wakeup_irq_enable_src |= source;
		if (wakeup_irq_enable_src == (ENABLED_BY_HOST | ENABLED_BY_DEVICE)) {
			otg_wake_up_enable(pdata, on);
		}
#else
		otg_wake_up_enable(pdata, on);
#endif
	} else {
		otg_wake_up_enable(pdata, on);
		wakeup_irq_enable_src &= ~source;
		/* The interrupt must be disabled for at least 3 clock
		 * cycles of the standby clock(32k Hz) , that is 0.094 ms*/
		udelay(100);
	}
	mutex_unlock(&otg_wakeup_enable_mutex);
}

/* The wakeup operation for DR port, it will clear the wakeup irq status
 * and re-enable the wakeup
 */
static void usbotg_wakeup_event_clear(void)
{
	int wakeup_req = USB_OTG_CTRL & UCTRL_OWIR;

	if (wakeup_req != 0) {
		printk(KERN_INFO "Unknown wakeup.(OTGSC 0x%x)\n", UOG_OTGSC);
		/* Disable OWIE to clear OWIR, wait 3 clock
		 * cycles of standly clock(32KHz)
		 */
		USB_OTG_CTRL &= ~UCTRL_OWIE;
		udelay(100);
		USB_OTG_CTRL |= UCTRL_OWIE;
	}
}

/* End of Common operation for DR port */

#ifdef CONFIG_USB_EHCI_ARC_OTG
/* Beginning of host related operation for DR port */
static void fsl_platform_otg_set_usb_phy_dis(
		struct fsl_usb2_platform_data *pdata, bool enable)
{
	u32 usb_phy_ctrl_dcdt = 0;
	void __iomem *anatop_base_addr = MX6_IO_ADDRESS(ANATOP_BASE_ADDR);
	usb_phy_ctrl_dcdt = __raw_readl(
			MX6_IO_ADDRESS(pdata->phy_regs) + HW_USBPHY_CTRL) &
			BM_USBPHY_CTRL_ENHOSTDISCONDETECT;
	if (enable) {
		if (usb_phy_ctrl_dcdt == 0) {
			__raw_writel(BM_ANADIG_USB1_PLL_480_CTRL_EN_USB_CLKS,
					anatop_base_addr + HW_ANADIG_USB1_PLL_480_CTRL_CLR);

			__raw_writel(BM_USBPHY_PWD_RXPWDENV,
					MX6_IO_ADDRESS(pdata->phy_regs) + HW_USBPHY_PWD_SET);

			udelay(300);

			__raw_writel(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
				MX6_IO_ADDRESS(pdata->phy_regs)
				+ HW_USBPHY_CTRL_SET);

			UOG_USBSTS |= (1 << 7);

			while ((UOG_USBSTS & (1 << 7)) == 0)
				;

			udelay(2);

			__raw_writel(BM_USBPHY_PWD_RXPWDENV,
					MX6_IO_ADDRESS(pdata->phy_regs) + HW_USBPHY_PWD_CLR);

			__raw_writel(BM_ANADIG_USB1_PLL_480_CTRL_EN_USB_CLKS,
					anatop_base_addr + HW_ANADIG_USB1_PLL_480_CTRL_SET);

		}
	} else {
		if (usb_phy_ctrl_dcdt
				== BM_USBPHY_CTRL_ENHOSTDISCONDETECT)
			__raw_writel(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
				MX6_IO_ADDRESS(pdata->phy_regs)
				+ HW_USBPHY_CTRL_CLR);
	}
}

static void _host_platform_rh_suspend_swfix(struct fsl_usb2_platform_data *pdata)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);
	u32 tmp;
	u32 index = 0;

	/* before we set and then clear PWD bit,
	 * we must wait LS to be suspend */
	if ((UOG_PORTSC1 & (3 << 26)) != (1 << 26)) {
		while (((UOG_PORTSC1 & PORTSC_LS_MASK) != PORTSC_LS_J_STATE) &&
				(index < 1000)) {
			index++;
			udelay(4);
		}
	} else {
		while (((UOG_PORTSC1 & PORTSC_LS_MASK) != PORTSC_LS_K_STATE) &&
				(index < 1000)) {
			index++;
			udelay(4);
		}
	}

	if (index >= 1000)
		printk(KERN_INFO "%s big error\n", __func__);

	tmp = (BM_USBPHY_PWD_TXPWDFS
		| BM_USBPHY_PWD_TXPWDIBIAS
		| BM_USBPHY_PWD_TXPWDV2I
		| BM_USBPHY_PWD_RXPWDENV
		| BM_USBPHY_PWD_RXPWD1PT1
		| BM_USBPHY_PWD_RXPWDDIFF
		| BM_USBPHY_PWD_RXPWDRX);
	__raw_writel(tmp, phy_reg + HW_USBPHY_PWD_SET);

	__raw_writel(tmp, phy_reg + HW_USBPHY_PWD_CLR);

	fsl_platform_otg_set_usb_phy_dis(pdata, 0);
}

static void _host_platform_rh_resume_swfix(struct fsl_usb2_platform_data *pdata)
{
	u32 index = 0;

	if ((UOG_PORTSC1 & (PORTSC_PORT_SPEED_MASK)) != PORTSC_PORT_SPEED_HIGH)
		return ;
	while ((UOG_PORTSC1 & PORTSC_PORT_FORCE_RESUME)
			&& (index < 1000)) {
		udelay(500);
		index++;
	}
	if (index >= 1000)
		printk(KERN_ERR "failed to wait for the resume finished in %s() line:%d\n",
		__func__, __LINE__);
	/* We should add some delay to wait for the device switch to
	  * High-Speed 45ohm termination resistors mode. */
	udelay(500);
	fsl_platform_otg_set_usb_phy_dis(pdata, 1);
}
static void _host_platform_rh_suspend(struct fsl_usb2_platform_data *pdata)
{
	/*for mx6sl ,we do not need any sw fix*/
	if (cpu_is_mx6sl())
		return ;
	__raw_writel(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
		MX6_IO_ADDRESS(pdata->phy_regs)
		+ HW_USBPHY_CTRL_CLR);
}

static void _host_platform_rh_resume(struct fsl_usb2_platform_data *pdata)
{
	u32 index = 0;

	/*for mx6sl ,we do not need any sw fix*/
	if (cpu_is_mx6sl())
		return ;
	if ((UOG_PORTSC1 & (PORTSC_PORT_SPEED_MASK)) != PORTSC_PORT_SPEED_HIGH)
		return ;
	while ((UOG_PORTSC1 & PORTSC_PORT_FORCE_RESUME)
			&& (index < 1000)) {
		udelay(500);
		index++;
	}
	if (index >= 1000)
		printk(KERN_ERR "failed to wait for the resume finished in %s() line:%d\n",
		__func__, __LINE__);
	/* We should add some delay to wait for the device switch to
	  * High-Speed 45ohm termination resistors mode. */
	udelay(500);
	__raw_writel(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
		MX6_IO_ADDRESS(pdata->phy_regs)
		+ HW_USBPHY_CTRL_SET);
}

static void _host_phy_lowpower_suspend(struct fsl_usb2_platform_data *pdata, bool enable)
{
	__phy_lowpower_suspend(pdata, enable, ENABLED_BY_HOST);
}

static void _host_wakeup_enable(struct fsl_usb2_platform_data *pdata, bool enable)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);

	__wakeup_irq_enable(pdata, enable, ENABLED_BY_HOST);
	if (enable) {
		pr_debug("host wakeup enable\n");
		USB_OTG_CTRL |= UCTRL_WKUP_ID_EN;
		__raw_writel(BM_USBPHY_CTRL_ENIDCHG_WKUP, phy_reg + HW_USBPHY_CTRL_SET);
	} else {
		pr_debug("host wakeup disable\n");
		__raw_writel(BM_USBPHY_CTRL_ENIDCHG_WKUP, phy_reg + HW_USBPHY_CTRL_CLR);
		USB_OTG_CTRL &= ~UCTRL_WKUP_ID_EN;
		/* The interrupt must be disabled for at least 3 clock
		 * cycles of the standby clock(32k Hz) , that is 0.094 ms*/
		udelay(100);
	}
	pr_debug("the otgsc is 0x%x, usbsts is 0x%x, portsc is 0x%x, otgctrl: 0x%x\n", UOG_OTGSC, UOG_USBSTS, UOG_PORTSC1, USB_OTG_CTRL);
}

static enum usb_wakeup_event _is_host_wakeup(struct fsl_usb2_platform_data *pdata)
{
	u32 wakeup_req = USB_OTG_CTRL & UCTRL_OWIR;
	u32 otgsc = UOG_OTGSC;

	if (wakeup_req) {
		pr_debug("the otgsc is 0x%x, usbsts is 0x%x, portsc is 0x%x, wakeup_irq is 0x%x\n", UOG_OTGSC, UOG_USBSTS, UOG_PORTSC1, wakeup_req);
	}

	/* if ID change sts, it is a host wakeup event */
	if (otgsc & OTGSC_IS_USB_ID || max77696_uic_is_otg_connected_irq()) {
		pr_debug("otg host ID wakeup\n");
		/* if host ID wakeup, we must clear the ID change sts */
		otgsc |= OTGSC_IS_USB_ID;
		return WAKEUP_EVENT_ID;
	}
	if (wakeup_req  && (!(otgsc & OTGSC_STS_USB_ID))) {
		pr_debug("otg host Remote wakeup\n");
		return WAKEUP_EVENT_DPDM;
	}

	return WAKEUP_EVENT_INVALID;
}

static void host_wakeup_handler(struct fsl_usb2_platform_data *pdata)
{
	_host_phy_lowpower_suspend(pdata, false);
	_host_wakeup_enable(pdata, false);
}
/* End of host related operation for DR port */
#endif /* CONFIG_USB_EHCI_ARC_OTG */


#if defined(CONFIG_USB_GADGET_ARC)
/* Beginning of device related operation for DR port */

static int fsl_platform_config_phy_parameters(void)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);
	u32 phy_tx_parameter;

	switch (UOG_PORTSC1 & PORTSC_PORT_SPEED_MASK) {
	case PORTSC_PORT_SPEED_HIGH:
		phy_tx_parameter = ((__raw_readl(phy_reg + HW_USBPHY_TX) & BM_USBPHY_TX_USBPHY_TX_EDGECTRL) |
					        BF_USBPHY_TX_TXCAL45DP(0x7) |
							BF_USBPHY_TX_TXCAL45DN(0x7) |
							BF_USBPHY_TX_D_CAL(0x1));
		break;
	case PORTSC_PORT_SPEED_FULL:
	case PORTSC_PORT_SPEED_LOW:
		phy_tx_parameter = ((__raw_readl(phy_reg + HW_USBPHY_TX) & BM_USBPHY_TX_USBPHY_TX_EDGECTRL) |
					        BF_USBPHY_TX_TXCAL45DP(0xC) |
							BF_USBPHY_TX_TXCAL45DN(0xC) |
							BF_USBPHY_TX_D_CAL(0x1));
		break;
	default:
		printk("ERROR: the speed is undef after bus reset\n");
		return -EPROTO;
	}
	__raw_writel(phy_tx_parameter, phy_reg + HW_USBPHY_TX);
	return 0;
}


static void _device_phy_lowpower_suspend(struct fsl_usb2_platform_data *pdata, bool enable)
{
	__phy_lowpower_suspend(pdata, enable, ENABLED_BY_DEVICE);
}

static void _device_wakeup_enable(struct fsl_usb2_platform_data *pdata, bool enable)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY0_BASE_ADDR);
	__wakeup_irq_enable(pdata, enable, ENABLED_BY_DEVICE);
	/* if udc is not used by any gadget, we can not enable the vbus wakeup */
	if (!pdata->port_enables) {
		USB_OTG_CTRL &= ~UCTRL_WKUP_VBUS_EN;
		return;
	}
	if (enable) {
		pr_debug("device wakeup enable\n");
		USB_OTG_CTRL |= UCTRL_WKUP_VBUS_EN;
		__raw_writel(BM_USBPHY_CTRL_ENVBUSCHG_WKUP, phy_reg + HW_USBPHY_CTRL_SET);
	} else {
		pr_debug("device wakeup disable\n");
		__raw_writel(BM_USBPHY_CTRL_ENVBUSCHG_WKUP, phy_reg + HW_USBPHY_CTRL_CLR);
		USB_OTG_CTRL &= ~UCTRL_WKUP_VBUS_EN;
	}
}

extern u8 pmic_soda_conects_usb_in(void);

static enum usb_wakeup_event _is_device_wakeup(struct fsl_usb2_platform_data *pdata)
{
	int wakeup_req = USB_OTG_CTRL & UCTRL_OWIR;
	int id;
	pr_debug("%s\n", __func__);
	id = !max77696_uic_is_otg_connected();
	/* if ID=1, it is a device wakeup event */
	if (wakeup_req && id && (UOG_USBSTS & USBSTS_URI)) {
		printk(KERN_INFO "otg udc wakeup, host sends reset signal\n");
		return WAKEUP_EVENT_DPDM;
	}
	if (wakeup_req && id &&  \
		((UOG_USBSTS & USBSTS_PCI) || (UOG_PORTSC1 & PORTSC_PORT_FORCE_RESUME))) {
		/*
		 * When the line state from J to K, the Port Change Detect bit
		 * in the USBSTS register is also set to '1'.
		 */
		printk(KERN_INFO "otg udc wakeup, host sends resume signal\n");
		return WAKEUP_EVENT_DPDM;
	}
	if (wakeup_req && id && (UOG_OTGSC & OTGSC_STS_A_VBUS_VALID) \
		&& (UOG_OTGSC & OTGSC_IS_B_SESSION_VALID)) {
		printk(KERN_INFO "otg udc vbus rising wakeup\n");
		return WAKEUP_EVENT_VBUS;
	}
	if (wakeup_req && id && !(UOG_OTGSC & OTGSC_STS_A_VBUS_VALID)) {
		printk(KERN_INFO "otg udc vbus falling wakeup\n");
		return WAKEUP_EVENT_VBUS;
	}

	return WAKEUP_EVENT_INVALID;
}

static void device_wakeup_handler(struct fsl_usb2_platform_data *pdata)
{
	_device_phy_lowpower_suspend(pdata, false);
	_device_wakeup_enable(pdata, false);
}

/* end of device related operation for DR port */
#endif /* CONFIG_USB_GADGET_ARC */

static struct platform_device *pdev[3], *pdev_wakeup;
static driver_vbus_func  mx6_set_usb_otg_vbus;
static int devnum;
static int  __init mx6_usb_dr_init(void)
{
	int i = 0;
	void __iomem *anatop_base_addr = MX6_IO_ADDRESS(ANATOP_BASE_ADDR);
	struct imx_fsl_usb2_wakeup_data imx6q_fsl_otg_wakeup_data =
		imx_fsl_usb2_wakeup_data_entry_single(MX6Q, 0, OTG);
	struct imx_mxc_ehci_data __maybe_unused imx6q_mxc_ehci_otg_data =
		imx_mxc_ehci_data_entry_single(MX6Q, 0, OTG);
	struct imx_fsl_usb2_udc_data __maybe_unused imx6q_fsl_usb2_udc_data =
		imx_fsl_usb2_udc_data_entry_single(MX6Q);
	struct imx_fsl_usb2_otg_data __maybe_unused imx6q_fsl_usb2_otg_data  =
		imx_fsl_usb2_otg_data_entry_single(MX6Q);

	/* Some phy and power's special controls for otg
	 * 1. The external charger detector needs to be disabled
	 * or the signal at DP will be poor
	 * 2. The EN_USB_CLKS is always enabled.
	 * The PLL's power is controlled by usb and others who
	 * use pll3 too.
	 */
	__raw_writel(BM_ANADIG_USB1_CHRG_DETECT_EN_B  \
			| BM_ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B,  \
			anatop_base_addr + HW_ANADIG_USB1_CHRG_DETECT);
	__raw_writel(BM_ANADIG_USB1_PLL_480_CTRL_EN_USB_CLKS,
			anatop_base_addr + HW_ANADIG_USB1_PLL_480_CTRL_SET);
	mx6_get_otghost_vbus_func(&mx6_set_usb_otg_vbus);
	dr_utmi_config.platform_driver_vbus = mx6_set_usb_otg_vbus;

#ifdef CONFIG_USB_OTG
	/* wake_up_enable is useless, just for usb_register_remote_wakeup execution*/
	dr_utmi_config.wake_up_enable = _device_wakeup_enable;
	dr_utmi_config.operating_mode = FSL_USB2_DR_OTG;
	dr_utmi_config.wakeup_pdata = &dr_wakeup_config;
	pdev[i] = imx6q_add_fsl_usb2_otg(&dr_utmi_config);
	dr_wakeup_config.usb_pdata[i] = pdev[i]->dev.platform_data;
	i++;
#endif
#ifdef CONFIG_USB_EHCI_ARC_OTG
	dr_utmi_config.operating_mode = FSL_USB2_DR_OTG;
	dr_utmi_config.wake_up_enable = _host_wakeup_enable;
	if (usb_icbug_swfix_need()) {
		dr_utmi_config.platform_rh_suspend = _host_platform_rh_suspend_swfix;
		dr_utmi_config.platform_rh_resume  = _host_platform_rh_resume_swfix;
	} else {
		dr_utmi_config.platform_rh_suspend = _host_platform_rh_suspend;
		dr_utmi_config.platform_rh_resume  = _host_platform_rh_resume;
	}
	dr_utmi_config.platform_set_disconnect_det = fsl_platform_otg_set_usb_phy_dis;
	dr_utmi_config.platform_config_phy_parameters = NULL;
	dr_utmi_config.phy_lowpower_suspend = _host_phy_lowpower_suspend;
#ifndef CONFIG_LAB126
// no host wake up capability, dont even ask for it
	dr_utmi_config.is_wakeup_event = _is_host_wakeup;
#endif
	dr_utmi_config.wakeup_pdata = &dr_wakeup_config;
	dr_utmi_config.wakeup_handler = host_wakeup_handler;
	dr_utmi_config.platform_phy_power_on = dr_platform_phy_power_on;
	pdev[i] = imx6q_add_fsl_ehci_otg(&dr_utmi_config);
	dr_wakeup_config.usb_pdata[i] = pdev[i]->dev.platform_data;
	i++;
#endif
#ifdef CONFIG_USB_GADGET_ARC
	dr_utmi_config.operating_mode = FSL_USB2_MPH_HOST;
	dr_utmi_config.wake_up_enable = _device_wakeup_enable;
	dr_utmi_config.platform_rh_suspend = NULL;
	dr_utmi_config.platform_rh_resume  = NULL;
	dr_utmi_config.platform_set_disconnect_det = NULL;
#ifdef CONFIG_LAB126
	if (lab126_board_is(BOARD_ID_WHISKY_WAN) ||
		lab126_board_is(BOARD_ID_WHISKY_WFO) ||
		lab126_board_is(BOARD_ID_WOODY) ) 
		dr_utmi_config.platform_config_phy_parameters = fsl_platform_config_phy_parameters;
	else 
#endif
	dr_utmi_config.platform_config_phy_parameters = NULL;
	dr_utmi_config.phy_lowpower_suspend = _device_phy_lowpower_suspend;
	dr_utmi_config.is_wakeup_event = _is_device_wakeup;
	dr_utmi_config.wakeup_pdata = &dr_wakeup_config;
	dr_utmi_config.wakeup_handler = device_wakeup_handler;
	dr_utmi_config.charger_base_addr = anatop_base_addr;
	dr_utmi_config.platform_phy_power_on = dr_platform_phy_power_on;
	pdev[i] = imx6q_add_fsl_usb2_udc(&dr_utmi_config);
	dr_wakeup_config.usb_pdata[i] = pdev[i]->dev.platform_data;
	i++;
#endif
	devnum = i;
	/* register wakeup device */
	pdev_wakeup = imx6q_add_fsl_usb2_otg_wakeup(&dr_wakeup_config);
	for (i = 0; i < devnum; i++) {
		platform_device_add(pdev[i]);
		((struct fsl_usb2_platform_data *)(pdev[i]->dev.platform_data))->wakeup_pdata =
			(struct fsl_usb2_wakeup_platform_data *)(pdev_wakeup->dev.platform_data);
	}
	setup_asession_proc(pdev[0]);
	INIT_DELAYED_WORK(&otg_enum_check, otg_enum_fn);
	INIT_DELAYED_WORK(&otg_recovery_work, otg_recovery_work_fn);

	return 0;
}
module_init(mx6_usb_dr_init);

static void __exit mx6_usb_dr_exit(void)
{
	int i;
	void __iomem *anatop_base_addr = MX6_IO_ADDRESS(ANATOP_BASE_ADDR);
	
	cancel_delayed_work(&otg_enum_check);
	cancel_delayed_work(&otg_recovery_work);

	for (i = 0; i < devnum; i++)
		platform_device_del(pdev[devnum-i-1]);
	platform_device_unregister(pdev_wakeup);
	otg_used = 0;

	__raw_writel(BM_ANADIG_USB1_PLL_480_CTRL_EN_USB_CLKS,
			anatop_base_addr + HW_ANADIG_USB1_PLL_480_CTRL_CLR);
	return ;
}
module_exit(mx6_usb_dr_exit);

MODULE_AUTHOR("Freescale Semiconductor");
MODULE_LICENSE("GPL");
