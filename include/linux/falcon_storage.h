/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _FALCON_ST_COMMON_H
#define _FALCON_ST_COMMON_H

#ifdef __KERNEL__

struct falcon_blk_host_param {
	char            *name;
	unsigned int    max_seg_size;
	unsigned short  max_hw_segs;
	unsigned short  max_phys_segs;
	unsigned int    max_req_size;
	unsigned int    max_blk_size;
	unsigned int    max_blk_count;
	int             irq;
	u64             dma_mask;
};

struct falcon_nand_host_param {
	int	irq;
};

void falcon_blk_platform_init(void);
struct falcon_blk_host_param *falcon_blk_get_hostinfo(void);
struct falcon_nand_host_param *falcon_nand_get_hostinfo(void);
void falcon_blk_platform_pre(void);
void falcon_blk_platform_post(void);

#endif

struct falcon_blk_sbios_call_container {
	void *ptr;
	int size;
};

#define F_SBIOS_CALL	_IOWR(0xee, 1, struct falcon_blk_sbios_call_container)

#endif
