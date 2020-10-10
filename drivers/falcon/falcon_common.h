/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _FALCON_COMMON_H
#define _FALCON_COMMON_H

enum storage_request_type {
	RT_READ,
	RT_WRITE,
	RT_READ_SG,
	RT_WRITE_SG,
	RT_ERASE,
	RT_NAND_READID,
	RT_NAND_STATUS,
	RT_NAND_READ,
	RT_NAND_WRITE,
	RT_NAND_READ_SPARE,
	RT_NAND_WRITE_SPARE,
	RT_NAND_IS_BAD,
	RT_SBIOS_CALL,
};

enum storage_wait_event {
	WAIT_POLLING = 1,
	WAIT_IRQ,
};

enum falcon_irqreturn {
	INT_NONE = 0,
	INT_DONE,
	INT_DONE_WAKEUP,
	INT_ERR,
};

#define RC_ERR -1
#define RC_BUSY -2
#define RC_OK 0
#define RC_DONE 1

void falcon_set_wait_queue(wait_queue_head_t *q, int *cond);
void falcon_init_notice_info(void);
int falcon_storage_suspend(void);
void falcon_storage_resume(void);
void falcon_blk_platform_suspend(void);
void falcon_blk_platform_resume(void);
void init_storage_common(void);

/* same definition as sbios */
struct erase_struct {
	u32 sector_start;
	u32 sector_end;
	int part_num;
};

#endif
