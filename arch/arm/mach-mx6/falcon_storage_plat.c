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
#include <linux/workqueue.h>

#include <linux/clk.h>

#define SECTOR_SIZE	512

struct clk *qb_clk = NULL;
static struct delayed_work qb_clk_disable_work;
static bool have_enabled_qb_clk;

/*
 *  Block device Host controller parameters
 */
static struct falcon_blk_host_param mx6sl_falcon_blk_param =
{
	.name = "mx6-falcon-mmc",
	.max_seg_size = (64 * 1024)-1,
	//.max_hw_segs = 1,		/* max_hw_segs (use Bounce Buffer) */
	.max_hw_segs = 128,		/* max_hw_segs (use Multisector I/O) */
	.max_phys_segs = 128,
	.max_req_size = 512 * 1024,
	.max_blk_size = SECTOR_SIZE,
	.max_blk_count = 512 * 1024 / SECTOR_SIZE,
	.irq = 55,			/* irq usdhc2 */
	//.irq = -1,			/* irq (doesn't use irq) */

	.dma_mask	= 0xffffffff,
};

/*
 *  NAND Host controller parameters
 */
static struct falcon_nand_host_param mx6sl_falcon_nand_param =
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
	return &mx6sl_falcon_blk_param;
}

/**
 * Do platform depending operations
 * This is called before real HW access is done.
 */
void falcon_blk_platform_pre(void)
{
	int rc;
	cancel_delayed_work_sync(&qb_clk_disable_work);

	if (!have_enabled_qb_clk) {
		rc = clk_enable(qb_clk);
		if (rc == 0) {
			 have_enabled_qb_clk = true;
		} else {
			/* Until we change the signature of this callback, I'd rather panic
			 * with this in the console than cause the system to watchdog
			 * quietly because we couldn't report the failure to the block
			 * driver.
			 */
			panic("Could not enable Falcon storage clock. (%d)\n", rc);
		}
	}

	return;
}

/**
 * Do platform depending operations
 * This is called after real HW access is done.
 */
void falcon_blk_platform_post(void)
{
	/*
	 * The delay can't be too short, or the clock will be turned off between
	 * every mmc access. It can't be too long, or we waste a lot of power.
	 * I chose 2 jiffies, or 20 ms on our system. I wanted to stay above 1
	 * jiffy, to avoid problems where the timer is scheduled at the end of
	 * a jiffy, and the roll over happens immediately, causing unnecessary
	 * flapping of the clock. There is no real reason to make it longer than 2,
	 * because that would waste power. All we are trying to do is make sure the
	 * clock stays up between immediate iterations of the falcon block device
	 * thread.
	 */
	schedule_delayed_work(&qb_clk_disable_work, 2);

	return;
}

void qb_clk_disable_work_fn(struct work_struct *work)
{
	if (have_enabled_qb_clk) {
		clk_disable(qb_clk);
		have_enabled_qb_clk = false;
	}
}

/**
 * Initialization for platform depending operations
 * This is called once when falcon block wrapper driver is initalized.
 */
void __init falcon_blk_platform_init(void)
{
	qb_clk = clk_get_sys("sdhci-esdhc-imx.1", NULL);
	if(IS_ERR(qb_clk)) {
		printk(KERN_ERR "qbblk: Can't get functional clock\n");
		qb_clk = NULL;
		return;
	}
	clk_enable(qb_clk);
	have_enabled_qb_clk = true;

	INIT_DELAYED_WORK(&qb_clk_disable_work, qb_clk_disable_work_fn);
	return;
}

/**
 * 
 * 
 */
void falcon_blk_platform_suspend(void)
{
	if (cancel_delayed_work_sync(&qb_clk_disable_work)) {
		qb_clk_disable_work_fn(NULL);
	}

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
	return &mx6sl_falcon_nand_param;
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

