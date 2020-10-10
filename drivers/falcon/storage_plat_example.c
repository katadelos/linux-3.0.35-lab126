/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/err.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/falcon_storage.h>

#define SECTOR_SIZE	512

/*
 *  Block device Host controller parameters
 */
static struct falcon_blk_host_param xxx_falcon_blk_param =
{
	.name = "xxx",
	.max_seg_size = 128 * 1024,
	.max_hw_segs = 1,		/* max_hw_segs (use Bounce Buffer) */
	//.max_hw_segs = 32,		/* max_hw_segs (use Multisector I/O) */
	.max_phys_segs = 32,
	.max_req_size = 128 * 1024,
	.max_blk_size = SECTOR_SIZE,
	.max_blk_count = 128 * 1024 / SECTOR_SIZE,
	//.irq = 9,			/* irq  */
	.irq = -1,			/* irq (doesn't use irq) */
};

/*
 *  NAND Host controller parameters
 */
static struct falcon_nand_host_param xxx_falcon_nand_param =
{
	//.irq = 33,			/* irq */
	.irq =-1,			/* irq (doesn't use irq) */
};

/**
 * Get block dev host controller parameter
 *
 * @return    addr of host param structure
 */
struct falcon_blk_host_param *falcon_blk_get_hostinfo(void)
{
	return &xxx_falcon_blk_param;
}

/**
 * Do platform depending operations
 * This is called before real HW access is done.
 */
void falcon_blk_platform_pre(void)
{
	return;
}

/**
 * Do platform depending operations
 * This is called after real HW access is done.
 */
void falcon_blk_platform_post(void)
{
	return;
}

/**
 * Initialization for platform depending operations
 * This is called once when falcon block wrapper driver is initalized.
 */
void __init falcon_blk_platform_init(void)
{
	return;
}

/**
 * 
 * 
 */
void falcon_blk_platform_suspend(void)
{
	return;
}

/**
 * 
 * 
 */
void falcon_blk_platform_resume(void)
{
	return;
}

/**
 * Get NAND controller parameter
 *
 * @return    addr of NAND controller param structure
 */
struct falcon_nand_host_param *falcon_nand_get_hostinfo(void)
{
	return &xxx_falcon_nand_param;
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "K&R"
 * tab-width: 8
 * indent-tabs-mode: t
 * c-basic-offset: 8
 * End:
 */
