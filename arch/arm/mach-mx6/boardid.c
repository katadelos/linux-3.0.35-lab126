/*
 * boardid.c
 *
 * Copyright (C) 2012-2015 Amazon Technologies, Inc. All rights reserved.
 *
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sysdev.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <asm/mach-types.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <mach/boardid.h>

#define DRIVER_VER "2.0"
#define DRIVER_INFO "Board ID and Serial Number driver for Lab126 boards version " DRIVER_VER

#define BOARDID_USID_PROCNAME		"usid"
#define BOARDID_PROCNAME_BOARDID	"board_id"
#define BOARDID_PROCNAME_PANELID	"panel_id"
#define BOARDID_PROCNAME_PCBSN		"pcbsn"
#define BOARDID_PROCNAME_MACADDR	"mac_addr"
#define BOARDID_PROCNAME_MACSEC		"mac_sec"
#define BOARDID_PROCNAME_BOOTMODE	"bootmode"
#define BOARDID_PROCNAME_POSTMODE	"postmode"
#define BOARDID_PROCNAME_BTMACADDR	"btmac_addr"
#ifdef CONFIG_FALCON
#define BOARDID_PROCNAME_OLDBOOT	"oldboot"
#define BOARDID_PROCNAME_QBCOUNT	"qbcount"
#endif

#define SERIAL_NUM_SIZE         16
#define BOARD_ID_SIZE           16
#define PANEL_ID_SIZE           32

#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

char lab126_serial_number[SERIAL_NUM_SIZE + 1];
EXPORT_SYMBOL(lab126_serial_number);

char lab126_board_id[BOARD_ID_SIZE + 1];
EXPORT_SYMBOL(lab126_board_id);

char lab126_panel_id[PANEL_ID_SIZE + 1];
EXPORT_SYMBOL(lab126_panel_id);

char lab126_mac_address[MAC_ADDR_SIZE + 1];
EXPORT_SYMBOL(lab126_mac_address);

char lab126_btmac_address[MAC_ADDR_SIZE + 1];
EXPORT_SYMBOL(lab126_btmac_address);

char lab126_mac_secret[MAC_SEC_SIZE + 1];
EXPORT_SYMBOL(lab126_mac_secret);

char lab126_bootmode[BOOTMODE_SIZE + 1];
char lab126_postmode[BOOTMODE_SIZE + 1];
#ifdef CONFIG_FALCON
char lab126_oldboot[BOOTMODE_SIZE + 1];
char lab126_qbcount[QBCOUNT_SIZE + 1];
#endif

int lab126_board_is(char *id) {
    return (BOARD_IS_(lab126_board_id, id, strlen(id)));
}
EXPORT_SYMBOL(lab126_board_is);

int lab126_board_rev_greater(char *id)
{
  return (BOARD_REV_GREATER(lab126_board_id, id));
}
EXPORT_SYMBOL(lab126_board_rev_greater);

int lab126_board_rev_greater_eq(char *id)
{
  return (BOARD_REV_GREATER_EQ(lab126_board_id, id));
}
EXPORT_SYMBOL(lab126_board_rev_greater_eq);

int lab126_board_rev_eq(char *id)
{
  return (BOARD_REV_EQ(lab126_board_id, id));
}
EXPORT_SYMBOL(lab126_board_rev_eq);

#define PCBSN_X_INDEX 5
char lab126_pcbsn_x(void)
{
  return lab126_board_id[PCBSN_X_INDEX];
}
EXPORT_SYMBOL(lab126_pcbsn_x);
	
static int proc_id_read(char *page, char **start, off_t off, int count,
				int *eof, void *data, char *id)
{
	strcpy(page, id);
	*eof = 1;

	return strlen(page);
}

#define PROC_ID_READ(id) proc_id_read(page, start, off, count, eof, data, id)

static int proc_usid_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_serial_number);
}

static int proc_board_id_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_board_id);
}

static int proc_panel_id_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_panel_id);
}

static int proc_mac_address_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_mac_address);
}

static int proc_btmac_address_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_btmac_address);
}

static int proc_mac_secret_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_mac_secret);
}

static int proc_bootmode_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_bootmode);
}

static int proc_postmode_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_postmode);
}

#ifdef CONFIG_FALCON
static int proc_oldboot_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_oldboot);
}

static int proc_qbcount_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(lab126_qbcount);
}
#endif

int bootmode_is_diags(void)
{
	return (strncmp(system_bootmode, "diags", 5) == 0);
}
EXPORT_SYMBOL(bootmode_is_diags);

int __init lab126_idme_vars_init(void)
{
	/* initialize the proc accessors */
	struct proc_dir_entry *proc_usid = create_proc_entry(BOARDID_USID_PROCNAME, S_IRUGO, NULL);
	struct proc_dir_entry *proc_board_id = create_proc_entry(BOARDID_PROCNAME_BOARDID, S_IRUGO, NULL);
	struct proc_dir_entry *proc_panel_id = create_proc_entry(BOARDID_PROCNAME_PANELID, S_IRUGO, NULL);
	struct proc_dir_entry *proc_mac_address = create_proc_entry(BOARDID_PROCNAME_MACADDR, S_IRUGO, NULL);
	struct proc_dir_entry *proc_mac_secret = create_proc_entry(BOARDID_PROCNAME_MACSEC, S_IRUGO, NULL);
	struct proc_dir_entry *proc_btmac_address = create_proc_entry(BOARDID_PROCNAME_BTMACADDR, S_IRUGO, NULL);
	struct proc_dir_entry *proc_bootmode = create_proc_entry(BOARDID_PROCNAME_BOOTMODE, S_IRUGO, NULL);
	struct proc_dir_entry *proc_postmode = create_proc_entry(BOARDID_PROCNAME_POSTMODE, S_IRUGO, NULL);
#ifdef CONFIG_FALCON
	struct proc_dir_entry *proc_oldboot = create_proc_entry(BOARDID_PROCNAME_OLDBOOT, S_IRUGO, NULL);
	struct proc_dir_entry *proc_qbcount = create_proc_entry(BOARDID_PROCNAME_QBCOUNT, S_IRUGO, NULL);
#endif
	if (proc_usid != NULL) {
		proc_usid->data = NULL;
		proc_usid->read_proc = proc_usid_read;
		proc_usid->write_proc = NULL;
	}

	if (proc_board_id != NULL) {
		proc_board_id->data = NULL;
		proc_board_id->read_proc = proc_board_id_read;
		proc_board_id->write_proc = NULL;
	}

	if (proc_panel_id != NULL) {
		proc_panel_id->data = NULL;
		proc_panel_id->read_proc = proc_panel_id_read;
		proc_panel_id->write_proc = NULL;
	}

	if (proc_mac_address != NULL) {
		proc_mac_address->data = NULL;
		proc_mac_address->read_proc = proc_mac_address_read;
		proc_mac_address->write_proc = NULL;
	}

	if (proc_mac_secret != NULL) {
		proc_mac_secret->data = NULL;
		proc_mac_secret->read_proc = proc_mac_secret_read;
		proc_mac_secret->write_proc = NULL;
	}
	
	if (proc_btmac_address != NULL) {
		proc_btmac_address->data = NULL;
		proc_btmac_address->read_proc = proc_btmac_address_read;
		proc_btmac_address->write_proc = NULL;
	}

	if (proc_bootmode != NULL) {
		proc_bootmode->data = NULL;
		proc_bootmode->read_proc = proc_bootmode_read;
		proc_bootmode->write_proc = NULL;
	}

	if (proc_postmode != NULL) {
		proc_postmode->data = NULL;
		proc_postmode->read_proc = proc_postmode_read;
		proc_postmode->write_proc = NULL;
	}

#ifdef CONFIG_FALCON
	if (proc_oldboot != NULL) {
		proc_oldboot->data = NULL;
		proc_oldboot->read_proc = proc_oldboot_read;
		proc_oldboot->write_proc = NULL;
	}

	if (proc_qbcount != NULL) {
		proc_qbcount->data = NULL;
		proc_qbcount->read_proc = proc_qbcount_read;
		proc_qbcount->write_proc = NULL;
	}
#endif

	/* Initialize the idme values */
	memcpy(lab126_serial_number, system_serial16, MIN(SERIAL_NUM_SIZE, sizeof(system_serial16)));
	lab126_serial_number[SERIAL_NUM_SIZE] = '\0';

	memcpy(lab126_board_id, system_rev16, MIN(BOARD_ID_SIZE, sizeof(system_rev16)));
	lab126_board_id[BOARD_ID_SIZE] = '\0';

	strcpy(lab126_panel_id, ""); /* start these as empty and populate later. */

	memcpy(lab126_mac_address, system_mac_addr, MIN(sizeof(lab126_mac_address)-1, sizeof(system_mac_addr))); 
	lab126_mac_address[sizeof(lab126_mac_address)-1] = '\0';

	memcpy(lab126_mac_secret, system_mac_sec, MIN(sizeof(lab126_mac_secret)-1, sizeof(system_mac_sec))); 
	lab126_mac_secret[sizeof(lab126_mac_secret)-1] = '\0';

	memcpy(lab126_btmac_address, system_btmac_addr, MIN(sizeof(lab126_btmac_address)-1, sizeof(system_btmac_addr))); 
	lab126_btmac_address[sizeof(lab126_btmac_address)-1] = '\0';

	memcpy(lab126_bootmode, system_bootmode, MIN(sizeof(lab126_bootmode)-1, sizeof(system_bootmode))); 
	lab126_bootmode[sizeof(lab126_bootmode)-1] = '\0';

	memcpy(lab126_postmode, system_postmode, MIN(sizeof(lab126_postmode)-1, sizeof(system_postmode))); 
	lab126_postmode[sizeof(lab126_postmode)-1] = '\0';

#ifdef CONFIG_FALCON
	memcpy(lab126_oldboot, system_oldboot, MIN(sizeof(lab126_oldboot)-1, sizeof(system_oldboot))); 
	lab126_oldboot[sizeof(lab126_oldboot)-1] = '\0';

	memcpy(lab126_qbcount, system_qbcount, MIN(sizeof(lab126_qbcount)-1, sizeof(system_qbcount))); 
	lab126_qbcount[sizeof(lab126_qbcount)-1] = '\0';
#endif

	printk ("LAB126 Board id - %s\n", lab126_board_id);

	return 0;
}

/* Inits boardid if in case some kernel initialization code needs it
 * before idme is initialised;
 * right now mx6_cpu_op_init needs it for BOURBON-237 */
void __init early_init_lab126_board_id(void)
{
	memcpy(lab126_board_id, system_rev16, MIN(BOARD_ID_SIZE, sizeof(system_rev16)));
	lab126_board_id[BOARD_ID_SIZE] = '\0';
}
EXPORT_SYMBOL(early_init_lab126_board_id);

