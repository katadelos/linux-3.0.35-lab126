/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/falcon_storage.h>
#include <linux/version.h>
#include <linux/mutex.h>

#include <asm/mach/flash.h>

#include <asm/falcon_syscall.h>
#include "falcon_common.h"

#define MTD_DEBUG_LEVEL0 (0)
#define MTD_DEBUG_LEVEL1 (1)
#define MTD_DEBUG_LEVEL2 (2)
#define MTD_DEBUG_LEVEL3 (3)

#ifdef CONFIG_MTD_DEBUG
#define DEBUG(n, args...) \
	do { \
		printk(KERN_INFO args); \
	} while(0)
#else
#define DEBUG(n, args...) \
	do { \
		if (0) \
		printk(KERN_INFO args); \
	} while(0)

/*
 * OOB placement block for use with hardware ecc generation
 */
/* CAUTION!! You must modify this for your NAND device! */
static struct nand_ecclayout nand_oob_layout = {
	.eccbytes = 20,
	.eccpos = {
		40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
		50, 51, 52, 53, 54, 55, 56, 57, 58, 59},
	.oobfree = {{2, 38}}
};

#define SECTOR_SIZE 512

#define NAND_BUFF_ALIGN 64
#define NAND_BUFF_ALIGNMENT(x) (((u32)(x) + NAND_BUFF_ALIGN - 1) & ~(NAND_BUFF_ALIGN - 1))

#define EXEC_TIMEOUT	HZ

struct falcon_mtd_s {
	struct mtd_info mtd;
	struct nand_chip nand;
	struct mtd_partition *parts;
	struct device *dev;
	void *p_data_buf;
	char *data_buf;
	int result;
	u16 colAddr;
	u32 writeAddr;
	bool write_oobonly;
	int irq_num;
	int irq_handled;
	wait_queue_head_t irq_waitq;
	struct mutex transaction_lock;
};

static struct falcon_mtd_s *falcon_nand_data = NULL;

int do_exec(enum storage_request_type type, u32 arg1, void *arg2, void *arg3)
{
	int ret;
	int count = 0;
	int wait_stat;

	mutex_lock(&falcon_nand_data->transaction_lock);

	ret = bios_svc(0x13, type, arg1, arg2, arg3, NULL);
	if(ret != RC_OK) {
		mutex_unlock(&falcon_nand_data->transaction_lock);
		return (ret == RC_DONE ? 0 : -1);
	}

	if(falcon_nand_data->irq_num != -1) {

		falcon_nand_data->irq_handled = 0;
		ret = bios_svc(0x14, WAIT_IRQ, 0, 0, 0, 0);

		while (ret == RC_OK) {
			wait_stat = wait_event_timeout(falcon_nand_data->irq_waitq, falcon_nand_data->irq_handled, EXEC_TIMEOUT);

			if(wait_stat < 0) {
				printk("%s: ERROR: wait completion error\n", __func__);
			}

			falcon_nand_data->irq_handled = 0;
			ret = bios_svc(0x14, WAIT_IRQ, 0, 0, 0, 0);
		}
	}
	else {
		ret = bios_svc(0x14, WAIT_POLLING, 0, 0, 0, 0);
		while (ret == RC_OK) {
			ret = bios_svc(0x14, WAIT_POLLING, 0, 0, 0, 0);
			set_user_nice(current, 10);
			schedule();
			count++;
		}
	}

	mutex_unlock(&falcon_nand_data->transaction_lock);

	return (ret == RC_DONE ? 0 : -1);
}

static int read_id(u8 *buf)
{
	return do_exec(RT_NAND_READID, 0, buf, 0);
}

static int read_status(void)
{
	u32 status;
	if (do_exec(RT_NAND_STATUS, 0, &status, 0))
		return -1;
	return status;
}

static int read_single_page(u32 addr, void *buff, void *spare)
{
	return do_exec(RT_NAND_READ, addr / SECTOR_SIZE, buff, spare);
}

static int write_single_page(u32 addr, void *buff, void *spare)
{
	return do_exec(RT_NAND_WRITE, addr / SECTOR_SIZE, buff, spare);
}

static int read_single_spare(u32 addr, void *spare)
{
	return do_exec(RT_NAND_READ_SPARE, addr / SECTOR_SIZE, NULL, spare);
}

static int write_single_spare(u32 addr, void *spare)
{
	return do_exec(RT_NAND_WRITE_SPARE, addr / SECTOR_SIZE, NULL, spare);
}

static int erase_block(u32 addr)
{
	return do_exec(RT_ERASE, addr / SECTOR_SIZE, NULL, NULL);
}

#if defined(CONFIG_MTD_PARTITIONS) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
#if defined(CONFIG_MACH_ARMADILLO500)
static const char *part_probes[] = { "cmdlinepart", NULL };
#else
static const char *part_probes[] = { "RedBoot", "cmdlinepart", NULL };
#endif
#endif

static void wait_op_done(void)
{

}

static int falcon_nand_dev_ready(struct mtd_info *mtd)
{
	return 1;
}

static void falcon_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{

}

static int falcon_nand_correct_data(struct mtd_info *mtd, u_char * dat,
				 u_char * read_ecc, u_char * calc_ecc)
{

	return 0;
}

static int falcon_nand_calculate_ecc(struct mtd_info *mtd, const u_char * dat,
				  u_char * ecc_code)
{

	return 0;
}

static u_char falcon_nand_read_byte(struct mtd_info *mtd)
{
	u_char retVal = 0;
	retVal = falcon_nand_data->data_buf[falcon_nand_data->colAddr++];

	DEBUG(MTD_DEBUG_LEVEL3,
		  "%s: ret=0x%x\n", __func__, retVal);

	return retVal;
}

static u16 falcon_nand_read_word(struct mtd_info *mtd)
{
	u16 retVal;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "falcon_nand_read_word(col = %d)\n", falcon_nand_data->colAddr);

	retVal = falcon_nand_data->data_buf[falcon_nand_data->colAddr++];
	retVal |= (u16)falcon_nand_data->data_buf[falcon_nand_data->colAddr++] << 8;

	return retVal;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
static int falcon_nand_read_page(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf, int pages)
#else
static int falcon_nand_read_page(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf)
#endif
{
	memcpy(buf, falcon_nand_data->data_buf, mtd->writesize);
	memcpy(chip->oob_poi, falcon_nand_data->data_buf + mtd->writesize,
		   mtd->oobsize);

	if (falcon_nand_data->result)
		mtd->ecc_stats.failed++;

	return 0;
}

static void falcon_nand_write_buf(struct mtd_info *mtd,
			       const u_char * buf, int len)
{
	int n;
	int col;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "falcon_nand_write_buf(col = %d, len = %d)\n",
	      falcon_nand_data->colAddr, len);

	col = falcon_nand_data->colAddr;

	n = mtd->writesize + mtd->oobsize - col;
	n = min(len, n);

	DEBUG(MTD_DEBUG_LEVEL3,
	      "%s:%d: col = %d, n = %d\n", __FUNCTION__, __LINE__, col, n);

	memcpy(falcon_nand_data->data_buf + falcon_nand_data->colAddr, buf, n);

	falcon_nand_data->colAddr += n;
}

static void falcon_nand_write_page(struct mtd_info *mtd, struct nand_chip *chip,
				  const uint8_t *buf)
{

	memcpy(falcon_nand_data->data_buf, buf, mtd->writesize);
	memcpy(falcon_nand_data->data_buf + mtd->writesize, chip->oob_poi,
		   mtd->oobsize);

	return;
}

static void falcon_nand_read_buf(struct mtd_info *mtd, u_char * buf, int len)
{

	int n;
	int col;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "falcon_nand_read_buf(col = %d, len = %d)\n",
	      falcon_nand_data->colAddr, len);

	col = falcon_nand_data->colAddr;

	n = mtd->writesize + mtd->oobsize - col;
	n = min(len, n);

	memcpy(buf, falcon_nand_data->data_buf + col, n);

	falcon_nand_data->colAddr = col + n;
}

static int
falcon_nand_verify_buf(struct mtd_info *mtd, const u_char * buf, int len)
{
	printk(KERN_INFO "%s not implemented\n", __func__);
	return -EFAULT;
}

static void falcon_nand_select_chip(struct mtd_info *mtd, int chip)
{
}

static void falcon_nand_command(struct mtd_info *mtd, unsigned command,
			     int column, int page_addr)
{
	if (command != NAND_CMD_READOOB)
		DEBUG(MTD_DEBUG_LEVEL3,
			  "falcon_nand_command (cmd = 0x%x, col = 0x%x, page = 0x%x)\n",
			  command, column, page_addr);

	switch (command) {

	case NAND_CMD_STATUS:
		falcon_nand_data->data_buf[0] = read_status();

		falcon_nand_data->data_buf[1] = 0;
		falcon_nand_data->colAddr = 0;
		return;

	case NAND_CMD_READ0:
		falcon_nand_data->result =
			read_single_page(page_addr << 11,
					 falcon_nand_data->data_buf,
					 falcon_nand_data->data_buf + mtd->writesize);
		falcon_nand_data->colAddr = column;
		return;

	case NAND_CMD_READID:
		read_id(falcon_nand_data->data_buf);
		falcon_nand_data->colAddr = 0;
		return;

	case NAND_CMD_READOOB:
		read_single_spare(page_addr << 11,
				  falcon_nand_data->data_buf + mtd->writesize);
		falcon_nand_data->colAddr = mtd->writesize;
		return;

	case NAND_CMD_SEQIN:
		if (column >= mtd->writesize) {

			if (column != mtd->writesize) {

				read_single_spare(page_addr << 11,
						  falcon_nand_data->data_buf + mtd->writesize);
			}
			falcon_nand_data->write_oobonly = 1;
		} else {
			if (column != 0) {

				read_single_page(page_addr << 11,
						 falcon_nand_data->data_buf,
						 falcon_nand_data->data_buf + mtd->writesize);
			}
			falcon_nand_data->write_oobonly = 0;
		}
		falcon_nand_data->colAddr = column;
		falcon_nand_data->writeAddr = page_addr << 11;
		return;

	case NAND_CMD_PAGEPROG:
		if (!falcon_nand_data->write_oobonly) {
			falcon_nand_data->result =
				write_single_page(falcon_nand_data->writeAddr,
						  falcon_nand_data->data_buf,
						  falcon_nand_data->data_buf + mtd->writesize);
		} else {
			falcon_nand_data->result =
				write_single_spare(falcon_nand_data->writeAddr,
						   falcon_nand_data->data_buf + mtd->writesize);
		}
		return;

	case NAND_CMD_ERASE1:
		falcon_nand_data->result = erase_block(page_addr << 11);

		wait_op_done();
		return;

	case NAND_CMD_ERASE2:
		return;

	case NAND_CMD_RESET:
		return;

	default:
		printk("unknown command %x\n", command);
		return;
	}

}

static int falcon_nand_wait(struct mtd_info *mtd, struct nand_chip *chip)
{
	return falcon_nand_data->result ? NAND_STATUS_FAIL : 0;
}

static int falcon_nand_write_oob(struct mtd_info *mtd, struct nand_chip *chip,
				 int page)
{
	int ret = 0;
	unsigned long flags;

	memcpy(falcon_nand_data->data_buf + mtd->writesize,
	       chip->oob_poi, mtd->oobsize);
	local_irq_save(flags);
	ret = write_single_spare(page << 11, falcon_nand_data->data_buf + mtd->writesize);
	local_irq_restore(flags);

	return ret ? -EIO : 0;
}

static irqreturn_t falcon_nand_irq(int irq, void *devid)
{
	int stat;
	unsigned long flags;
	struct falcon_mtd_s *dev = devid;

	local_irq_save(flags);
	stat = bios_svc(0x17, NULL, 0, 0, 0, 0);
	local_irq_restore(flags);

	if(stat == INT_DONE_WAKEUP || stat == INT_ERR) {
		dev->irq_handled = 1;
		wake_up(&dev->irq_waitq);
	}

	return IRQ_HANDLED;
}

static int __init falcon_nand_probe(struct platform_device *pdev)
{
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct flash_platform_data *flash = pdev->dev.platform_data;
	int nr_parts = 0;
	struct falcon_nand_host_param *p;
	int irq_num;

	int err = 0;

	DEBUG(MTD_DEBUG_LEVEL3, "FALCON_NAND: %s\n", __func__);

	falcon_nand_data = kmalloc(sizeof(struct falcon_mtd_s), GFP_KERNEL);
	if (!falcon_nand_data) {
		printk(KERN_ERR "%s: failed to allocate mtd_info\n",
		       __FUNCTION__);
		err = -ENOMEM;
		goto out;
	}
	memset(falcon_nand_data, 0, sizeof(struct falcon_mtd_s));

	falcon_nand_data->p_data_buf = kmalloc(NAND_MAX_PAGESIZE + NAND_MAX_OOBSIZE + NAND_BUFF_ALIGN, GFP_KERNEL);
	if (!falcon_nand_data->p_data_buf) {
		printk(KERN_ERR "%s: failed to allocate nand_buff\n",
		       __FUNCTION__);
		err = -ENOMEM;
		goto out_1;
	}
	falcon_nand_data->data_buf = (char *)NAND_BUFF_ALIGNMENT(falcon_nand_data->p_data_buf);

	falcon_nand_data->dev = &pdev->dev;

	this = &falcon_nand_data->nand;
	mtd = &falcon_nand_data->mtd;
	mtd->priv = this;
	mtd->owner = THIS_MODULE;

	this->chip_delay = 5;

	this->priv = falcon_nand_data;
	this->dev_ready = falcon_nand_dev_ready;
	this->cmdfunc = falcon_nand_command;
	this->select_chip = falcon_nand_select_chip;
	this->read_byte = falcon_nand_read_byte;
	this->read_word = falcon_nand_read_word;
	this->write_buf = falcon_nand_write_buf;
	this->read_buf = falcon_nand_read_buf;
	this->verify_buf = falcon_nand_verify_buf;
	this->waitfunc = falcon_nand_wait;

	this->ecc.calculate = falcon_nand_calculate_ecc;
	this->ecc.hwctl = falcon_nand_enable_hwecc;
	this->ecc.correct = falcon_nand_correct_data;
	this->ecc.mode = NAND_ECC_HW;
	this->ecc.size = 512;
	this->ecc.bytes = 3;
	this->ecc.layout = &nand_oob_layout;
	this->ecc.read_page = falcon_nand_read_page;
	this->ecc.write_page = falcon_nand_write_page;
	this->ecc.write_oob = falcon_nand_write_oob;

	p = falcon_nand_get_hostinfo();
	if(p)
		irq_num = p->irq;
	else
		irq_num = -1;

	if(irq_num > -1) {
		init_waitqueue_head(&falcon_nand_data->irq_waitq);
		falcon_set_wait_queue(&falcon_nand_data->irq_waitq,
				      &falcon_nand_data->irq_handled);

		err = request_irq(irq_num , falcon_nand_irq, 0, "falconnand",
				  falcon_nand_data);
		if(err) {
			printk(KERN_ERR "falcon nand: request_irq is failed.\n");
			falcon_nand_data->irq_num = -1;
			err = -EBUSY;
			goto out_2;
		}
		falcon_nand_data->irq_num = irq_num;

		printk("%s: Use interrupt : IRQ=%d\n",
		       __func__, falcon_nand_data->irq_num);
	}
	else {
		falcon_nand_data->irq_num = -1;
		printk("%s: Not Use interrupt\n", __func__);
	}

	mutex_init(&falcon_nand_data->transaction_lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
	if (nand_scan(mtd,1))
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
	if (nand_scan_ident(mtd,1))
#else
	if (nand_scan_ident(mtd,1,NULL))
#endif
	{
		DEBUG(MTD_DEBUG_LEVEL0,
		      "FALCON_NAND: Unable to find any NAND device.\n");
		err = -ENXIO;
		goto out_2;
	}

#if 0
	if (mtd->writesize != NAND_PAGESIZE_2KB) {
		this->ecc.layout = .....;
	}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
	if (nand_scan_tail(mtd)) {
		DEBUG(MTD_DEBUG_LEVEL0,
		      "FALCON_NAND: Failed to finish NAND scan.\n");
		err = -ENXIO;
		goto out_2;
	}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
	nr_parts =
	    parse_mtd_partitions(mtd, part_probes, &falcon_nand_data->parts, 0);
	if (nr_parts > 0)
		mtd_device_register(mtd, falcon_nand_data->parts, nr_parts);
	else if (flash->parts)
		mtd_device_register(mtd, flash->parts, flash->nr_parts);
	else
	{
		pr_info("Registering %s as whole device\n", mtd->name);
		mtd_device_register(mtd, NULL, 0);
	}
#else
#ifdef CONFIG_MTD_PARTITIONS
	nr_parts =
	    parse_mtd_partitions(mtd, part_probes, &falcon_nand_data->parts, 0);
	if (nr_parts > 0)
		add_mtd_partitions(mtd, falcon_nand_data->parts, nr_parts);
	else if (flash->parts)
		add_mtd_partitions(mtd, flash->parts, flash->nr_parts);
	else
#endif
	{
		pr_info("Registering %s as whole device\n", mtd->name);
		add_mtd_device(mtd);
	}
#endif

	platform_set_drvdata(pdev, mtd);
	return 0;

out_2:
	if(falcon_nand_data->irq_num > -1)
		free_irq(falcon_nand_data->irq_num, falcon_nand_data);

	kfree(falcon_nand_data->p_data_buf);
out_1:
	kfree(falcon_nand_data);
out:
	return err;

}

static int __exit falcon_nand_remove(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (falcon_nand_data) {
		nand_release(mtd);
		kfree(falcon_nand_data->p_data_buf);
		kfree(falcon_nand_data);
	}

	return 0;
}

#ifdef CONFIG_PM

static int falcon_nand_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mtd_info *info = platform_get_drvdata(pdev);
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "FALCON_NAND : NAND suspend\n");

	mutex_lock(&falcon_nand_data->transaction_lock);

	if (info)
		ret = info->suspend(info);

	if (!ret)
		ret = falcon_storage_suspend();

	return ret;
}

static int falcon_nand_resume(struct platform_device *pdev)
{
	struct mtd_info *info = platform_get_drvdata(pdev);
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL0, "FALCON_NAND : NAND resume\n");

	if (falcon_nand_data->irq_num != -1) {
		falcon_nand_data->irq_handled = 0;
		wait_event_timeout(falcon_nand_data->irq_waitq, falcon_nand_data->irq_handled, 1);
	}

	if (info) {
		info->resume(info);
	}

	falcon_storage_resume();

	mutex_unlock(&falcon_nand_data->transaction_lock);

	return ret;
}

#else
#define falcon_nand_suspend   NULL
#define falcon_nand_resume    NULL
#endif

static struct platform_driver falcon_nand_driver = {
	.driver = {
		.name = "mxc_nand_flash",
	},
	.probe = falcon_nand_probe,
	.remove = __exit_p(falcon_nand_remove),
	.suspend = falcon_nand_suspend,
	.resume = falcon_nand_resume,
};

static int __init falcon_nand_init(void)
{
	extern int can_use_falcon(void);
	extern void falcon_storage_init(void);

	falcon_storage_init();

	if (!can_use_falcon()) {
		printk(KERN_ERR "Falcon BIOS is not found.\n");
		return -EINVAL;
	}

	init_storage_common();

	return platform_driver_register(&falcon_nand_driver);
}

static void __exit falcon_nand_cleanup(void)
{

	platform_driver_unregister(&falcon_nand_driver);
}

module_init(falcon_nand_init);
module_exit(falcon_nand_cleanup);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("Falcon NAND MTD driver");
MODULE_LICENSE("GPL");
