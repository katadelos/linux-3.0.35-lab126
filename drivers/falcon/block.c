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
#include <linux/platform_device.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include <linux/vmalloc.h>
#include <linux/falcon_sbios_buf.h>

#include <asm/falcon_syscall.h>
#include <linux/falcon_storage.h>
#include "falcon_common.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Falcon Block device driver");
MODULE_AUTHOR("");

#if 0
#define Dprintk(...) {printk(KERN_ERR"%s:\t", __func__);\
		printk(__VA_ARGS__);}
#else
#define Dprintk(...)
#endif

#define FALCON_BLK_DRV_NAME	"falconblk"
#define FALCON_BLK_DEV_NAME	"mmcblk0"
#define FALCON_BLK_BOOTDEV0_NAME "mmcblk0boot0"
#define FALCON_BLK_BOOTDEV1_NAME "mmcblk0boot1"

#define FALCON_BLK_DEV_PATH "/dev/mmcblk0"
#define FALCON_BLK_BOOTDEV0_PATH "/dev/mmcblk0boot0"
#define FALCON_BLK_BOOTDEV1_PATH "/dev/mmcblk0boot1"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
#ifdef CONFIG_FALCON_BLK_BOOTPART
#warning BootPartition is unsupported less than Linux 3.0
#endif
#define FALCON_BLK_DEV_MAX	1
#else
#ifdef CONFIG_FALCON_BLK_BOOTPART
#define FALCON_BLK_DEV_MAX	3
#else
#define FALCON_BLK_DEV_MAX	1
#endif
#endif
#define SECTOR_SIZE 512
#define FALCON_MINORS 16

#define EXEC_TIMEOUT	(HZ / 2)

#ifdef CONFIG_ARM_LPAE
typedef u64 fpaddr_t;
#else
typedef u32 fpaddr_t;
#endif

static char **pp_bios_debug_buf;

struct falcon_sglist {
	fpaddr_t	paddr;
	u32	length;
	u32	offset;
};

struct falcon_sginfo {
	u32	size;
	struct falcon_sglist *list;
};

struct falcon_blkinfo {
	u32	part;
};

struct falcon_blk_host {

	char			*name;
	unsigned int		max_seg_size;
	unsigned short		max_hw_segs;
	unsigned short		max_phys_segs;
	unsigned int		max_req_size;
	unsigned int		max_blk_size;
	unsigned int		max_blk_count;

	int			irq;

	u64			dma_mask;
};

struct falcon_blk_dev {
	struct device		*dev;
	struct falcon_blk_host	*host;

	struct scatterlist	*sg;
	char			*bounce_buf;
	struct scatterlist	*bounce_sg;
	unsigned int		bounce_sg_len;
	struct falcon_sginfo	falcon_sg;

	int			use_irq;
	int			irq_handled;
	struct semaphore	falcon_sem;
	wait_queue_head_t	irq_waitq;
};

static struct falcon_blkinfo falcon_binfo[FALCON_BLK_DEV_MAX];
struct falcon_blk_part {
	struct task_struct	*thread;
	struct falcon_blk_dev	*dev;

	struct request_queue	*queue;
	struct request		*req;
	struct request		*prev_req;
	struct gendisk		*gd;
	spinlock_t		lock;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
	struct device_attribute	force_ro;
#endif
	struct falcon_blkinfo	*falcon_binfo;
};

static int do_request(struct falcon_blk_dev *dev)
{
	int ret;
	int wait_stat;

	if(dev->use_irq) {

		dev->irq_handled = 0;
		ret = bios_svc(0x14, WAIT_IRQ, 0, 0, 0, 0);

		while (ret == RC_OK) {

			wait_stat = wait_event_timeout(dev->irq_waitq, dev->irq_handled, EXEC_TIMEOUT);

			if(wait_stat < 0) {
				printk("%s: ERROR: wait event error\n", __func__);
			}

			dev->irq_handled = 0;
			ret = bios_svc(0x14, WAIT_IRQ, 0, 0, 0, 0);

		}
	}
	else {
		ret = bios_svc(0x14, WAIT_POLLING, 0, 0, 0, 0);
		while (ret == RC_OK) {
			set_user_nice(current, 10);
			schedule();
			ret = bios_svc(0x14, WAIT_POLLING, 0, 0, 0, 0);
		}
	}

	return ret;
}

#ifdef CONFIG_FALCON_BLK_MULTISEG

static unsigned int falcon_blk_queue_map_sg(struct falcon_blk_part *part, struct request *req)
{
	struct falcon_blk_dev *dev = part->dev;
	unsigned int sg_len;
	size_t buflen;
	struct scatterlist *sg;
	int i;

	if (!dev->bounce_buf)
		return blk_rq_map_sg(part->queue, req, dev->sg);

	BUG_ON(!dev->bounce_sg);

	sg_len = blk_rq_map_sg(part->queue, req, dev->bounce_sg);

	dev->bounce_sg_len = sg_len;

	buflen = 0;
	for_each_sg(dev->bounce_sg, sg, sg_len, i) {
		buflen += sg->length;
		Dprintk("segment %d: %d\n", i, sg->length);
	}

	sg_init_one(dev->sg, dev->bounce_buf, buflen);

	return 1;
}

static void falcon_blk_queue_bounce_pre(struct falcon_blk_dev *dev, struct request *req)
{
	unsigned long flags;

	if (!dev->bounce_buf)
		return;

	if (rq_data_dir(req) != WRITE)
		return;

	local_irq_save(flags);
	sg_copy_to_buffer(dev->bounce_sg, dev->bounce_sg_len,
			  dev->bounce_buf, dev->sg[0].length);
	local_irq_restore(flags);
}

static void falcon_blk_queue_bounce_post(struct falcon_blk_dev *dev, struct request *req)
{
	unsigned long flags;

	if (!dev->bounce_buf)
		return;

	if (rq_data_dir(req) != READ)
		return;

	local_irq_save(flags);
	sg_copy_from_buffer(dev->bounce_sg, dev->bounce_sg_len,
			    dev->bounce_buf, dev->sg[0].length);
	local_irq_restore(flags);
}

static void falcon_map_sg(struct falcon_blk_dev *dev, struct request *req, unsigned int count)
{
	int nents;

	nents = dma_map_sg(dev->dev, dev->sg, count,
				rq_data_dir(req) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

	BUG_ON(nents != count);

}

static void falcon_unmap_sg(struct falcon_blk_dev *dev, struct request *req, unsigned int count)
{

	dma_unmap_sg(dev->dev, dev->sg, count,
				rq_data_dir(req) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

}

static void prepare_falcon_sglist(struct falcon_blk_dev *dev, unsigned int count)
{
	struct falcon_sglist *falcon_sg = dev->falcon_sg.list;
	struct scatterlist *sg = NULL;
	int i;
	unsigned int size = 0;

	for_each_sg(dev->sg, sg, count, i) {

		BUG_ON(sg == NULL);

		falcon_sg->paddr = (fpaddr_t)sg_dma_address(sg);
		falcon_sg->offset = sg->offset;
		falcon_sg->length = sg_dma_len(sg);

		size += falcon_sg->length;

		falcon_sg++;
	}

	dev->falcon_sg.size = size;
}

static void process_request_sg(struct falcon_blk_part *part)
{
	struct falcon_blk_dev *dev = part->dev;
	struct request *rq = part->req;
	unsigned long flags;
	int is_left;

	Dprintk("enter\n");

	do {
		int ret;
		int count;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
		unsigned long pos = rq->sector;
#else
		unsigned long pos = blk_rq_pos(rq);
#endif

		count = falcon_blk_queue_map_sg(part, rq);

		BUG_ON(count > dev->host->max_phys_segs);

		falcon_blk_queue_bounce_pre(dev, rq);

		falcon_map_sg(dev, rq, count);

		prepare_falcon_sglist(dev, count);

		Dprintk("  %c: start sector:%lu, segment count:%u, size:0x%x\n",
			rq_data_dir(rq) ? 'W' : 'R',
			pos, count, dev->falcon_sg.size);

		falcon_blk_platform_pre();

		if (dev->bounce_buf) {
			ret = bios_svc(0x13, rq_data_dir(rq) ? RT_WRITE : RT_READ, pos, dev->bounce_buf, sg_dma_len(dev->sg) / 512, part->falcon_binfo);
		} else {
		ret = bios_svc(0x13, rq_data_dir(rq) ? RT_WRITE_SG : RT_READ_SG, pos, &dev->falcon_sg, count, part->falcon_binfo);
		}

		if (ret) {
			printk(KERN_ERR "falcon_blk: request error!! ret=%d\n", ret);
		} else {
			ret = do_request(dev);
		}
		Dprintk("ret = %d\n", ret);

		falcon_blk_platform_post();

		falcon_unmap_sg(dev, rq, count);

		falcon_blk_queue_bounce_post(dev, rq);

		spin_lock_irqsave(part->queue->queue_lock, flags);
		is_left = __blk_end_request(rq, ret != RC_DONE, dev->falcon_sg.size);
		spin_unlock_irqrestore(part->queue->queue_lock, flags);

	} while (is_left);

}

#else

static int get_first_rq_size(struct request *rq)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	unsigned long sectors = rq->hard_cur_sectors;
#else
	unsigned long sectors = blk_rq_cur_sectors(rq);
#endif

#if 1

	struct bio_vec *bv;
	int size = 0;
	struct req_iterator iter;
	unsigned int start_addr = (unsigned int)rq->buffer;

	rq_for_each_segment(bv, rq, iter) {

		if ((unsigned int)(page_address(bv->bv_page) + bv->bv_offset) ==
			start_addr + size) {
			size += bv->bv_len;
		} else {

			goto end;
		}
	}

end:
	if (size)
		return size / SECTOR_SIZE;
	else
		return sectors;
#else
	return rq->hard_cur_sectors;
#endif
}

static void process_request(struct falcon_blk_part *part)
{
	struct falcon_blk_dev *dev = part->dev;
	struct request *rq = part->req;
	unsigned long flags;
	int is_left;

	Dprintk("enter\n");

	do {
		int ret;
		int size = get_first_rq_size(rq);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
		unsigned long pos = rq->sector;
#else
		unsigned long pos = blk_rq_pos(rq);
#endif

		falcon_blk_platform_pre();

		Dprintk("  %c: sector:%lu, size:%u, buff:%p\n",
				rq_data_dir(rq) ? 'W' : 'R',
				pos, size, rq->buffer);

		ret = bios_svc(0x13, rq_data_dir(rq) ? RT_WRITE : RT_READ, pos, rq->buffer, size, dev->falcon_binfo);

		if (ret) {
			printk(KERN_ERR "falcon_blk: request error!!  ret=%d\n", ret);
		} else {
			ret = do_request(dev);

		}
		Dprintk("ret = %d\n", ret);

		falcon_blk_platform_post();

		spin_lock_irqsave(dev->queue->queue_lock, flags);
		is_left = __blk_end_request(rq, ret != RC_DONE, size * SECTOR_SIZE);
		spin_unlock_irqrestore(dev->queue->queue_lock, flags);

	} while (is_left);
}

#endif

static irqreturn_t falcon_blk_irq(int irq, void *devid)
{
	int stat;
	unsigned long flags;
	struct falcon_blk_dev *dev = devid;

	local_irq_save(flags);
	stat = bios_svc(0x17, NULL, 0, 0, 0, 0);
	local_irq_restore(flags);

	if(stat == INT_DONE_WAKEUP || stat == INT_ERR) {
		dev->irq_handled = 1;
		wake_up(&dev->irq_waitq);
	}

	return IRQ_HANDLED;
}

static int falcon_blk_request_sbios_call(struct falcon_blk_part *part,
					 struct falcon_blk_sbios_call_container __user *arg)
{
	struct falcon_blk_dev *dev = part->dev;
	struct falcon_blk_sbios_call_container container;
	void *data = NULL;
	int result = -EINVAL;
	int ret;

	if (down_timeout(&dev->falcon_sem, HZ)) {
		result = -EINTR;
		return result;
	}

	if (copy_from_user(&container, (void *)arg, sizeof(container)))
		goto end;

	if (container.size == 0)
		goto end;

	data = kmalloc(container.size, GFP_KERNEL);
	if (!data) {
		result = -ENOMEM;
		goto end;
	}

	if (copy_from_user(data, container.ptr, container.size))
		goto end;

	falcon_blk_platform_pre();

	ret = bios_svc(0x13, RT_SBIOS_CALL, 0, data, container.size, part->falcon_binfo);
	if (ret) {
		printk(KERN_ERR "falcon_blk: request error!! ret=%d\n", ret);
	} else {
		ret = do_request(dev);
	}

	falcon_blk_platform_post();

	if (ret != RC_DONE) {
		if(ret == RC_BUSY)
			result = -EBUSY;
		else
			result = -EIO;

		goto end;
	}

	result = -EINVAL;
	if (copy_to_user(container.ptr, data, container.size))
		goto end;

	result = 0;
end:
	if (data)
		kfree(data);

	up(&dev->falcon_sem);

	return result;
}

static int _falcon_blk_ioctl(struct falcon_blk_part *part, unsigned int cmd,
			     unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case F_SBIOS_CALL:
		ret = falcon_blk_request_sbios_call(part, (void *)arg);
		break;
	default:
		break;
	}
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
static int falcon_blk_ioctl(struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg)

{
	struct falcon_blk_part *part = filp->private_data;
	return _falcon_blk_ioctl(part, cmd, arg);
}
#else
static int falcon_blk_ioctl(struct block_device *bdev, fmode_t mode,
			    unsigned int cmd, unsigned long arg)
{
	struct falcon_blk_part *part = bdev->bd_disk->private_data;
	return _falcon_blk_ioctl(part, cmd, arg);
}
#endif

static int
falcon_blk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->cylinders = get_capacity(bdev->bd_disk) / (4 * 16);
	geo->heads = 4;
	geo->sectors = 16;
	return 0;
}

static void falcon_blk_issue(struct falcon_blk_part *part, struct request *req)
{
	if (!req) {
		return;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	if (!blk_fs_request(part->req)) {
#else
	if (part->req->cmd_type != REQ_TYPE_FS) {
#endif
		printk(KERN_NOTICE "Skip non-fs request\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
		end_request(part->req, 0);
#else
		__blk_end_request_cur(part->req, -EIO);
#endif
	}
	else {
#ifdef CONFIG_FALCON_BLK_MULTISEG
		process_request_sg(part);
#else
		process_request(part);
#endif
	}
}

static int falcon_blk_queue_thread(void *d)
{
	unsigned long flags;
	struct falcon_blk_part *part = d;
	struct request_queue *q = part->queue;
	struct falcon_blk_dev *dev = part->dev;

	down(&dev->falcon_sem);
	do {
		struct request *tmp_req = NULL;

		spin_lock_irqsave(q->queue_lock, flags);
		set_current_state(TASK_INTERRUPTIBLE);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
		if (!blk_queue_plugged(q))
#endif
		{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
			tmp_req = elv_next_request(q);
#else
			tmp_req = blk_fetch_request(q);
#endif
			part->req = tmp_req;
		}
		spin_unlock_irqrestore(q->queue_lock, flags);

		if (tmp_req || part->prev_req) {
			up(&dev->falcon_sem);
			down(&dev->falcon_sem);

			set_current_state(TASK_RUNNING);
			falcon_blk_issue(part, tmp_req);

			part->prev_req = part->req;
		}
		else {
			if (kthread_should_stop()) {
				set_current_state(TASK_RUNNING);
				break;
			}
			up(&dev->falcon_sem);
			schedule();
			down(&dev->falcon_sem);
		}
	} while (1);
	up(&dev->falcon_sem);

	return 0;
}

static void falcon_blk_request(struct request_queue *q)
{
	struct falcon_blk_part *part = q->queuedata;
	struct request *req;

	if (!part) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
		while ((req = elv_next_request(q)) != NULL) {
			int ret;
			do {
				ret = __blk_end_request(req, -EIO,
							blk_rq_cur_bytes(req));
			} while (ret);
		}
#else
		while ((req = blk_fetch_request(q)) != NULL) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
			req->cmd_flags |= REQ_QUIET;
#endif
			__blk_end_request_all(req, -EIO);
		}
#endif
		return;
	}

	if (!part->req && !part->prev_req) {
		wake_up_process(part->thread);
	}
}

static int falcon_blk_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct falcon_blk_dev *dev = platform_get_drvdata(pdev);

	down(&dev->falcon_sem);

	falcon_blk_platform_suspend();
	printk("%s: done\n", __func__);
	return 0;
}

static int falcon_blk_resume(struct platform_device *pdev)
{
	struct falcon_blk_dev *dev = platform_get_drvdata(pdev);

	if(dev->use_irq) {
		dev->irq_handled = 0;
		wait_event_timeout(dev->irq_waitq, dev->irq_handled, 1);
	}

	falcon_storage_resume();

	falcon_blk_platform_resume();
	up(&dev->falcon_sem);

	printk("%s: done\n", __func__);
	return 0;
}

static struct platform_device *falcon_blk_platform_device;

static struct platform_driver falcon_blk_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = FALCON_BLK_DRV_NAME,
		},
	.suspend = falcon_blk_suspend,
	.resume = falcon_blk_resume,
};

static struct block_device_operations falcon_blk_ops =
{
	.ioctl = falcon_blk_ioctl,
        .getgeo = falcon_blk_getgeo,
	.owner = THIS_MODULE,
};

int falcon_part_num_parse(char *path) {
	
	int part_num;
	
	if(!strcmp(path, FALCON_BLK_DEV_PATH))
		part_num = 0;
	else if(!strcmp(path, FALCON_BLK_BOOTDEV0_PATH))
		part_num = 1;
	else if(!strcmp(path, FALCON_BLK_BOOTDEV1_PATH))
		part_num = 2;
	else {
		return -1;
	}
	return part_num;
}

int falcon_blk_kernel_sbios_call(u32 arg, int size, void *data)
{
	struct falcon_blk_dev *dev = platform_get_drvdata(falcon_blk_platform_device);
	int result = -EINVAL;
	int ret;

	if (down_timeout(&dev->falcon_sem, HZ)) {
		result = -EINTR;
		return result;
	}
	falcon_blk_platform_pre();

	ret = bios_svc(0x13, RT_SBIOS_CALL, 0, arg, size, data);

	if (ret) {
		printk(KERN_ERR "falcon_blk: request error!! ret=%d\n", ret);
	} else {
		/*  to maintain S-BIOS sanity */
		ret = do_request(dev);
	}

	falcon_blk_platform_post();

	if (ret != RC_DONE) {
		if(ret == RC_BUSY)
			result = -EBUSY;
		else
			result = -EIO;

		goto end;
	}
	result = 0;

end:

	up(&dev->falcon_sem);

	return result;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
static ssize_t force_ro_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d\n",
		       get_disk_ro(dev_to_disk(dev)));
	return ret;
}

static ssize_t force_ro_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int ret;
	char *end;
	unsigned long set = simple_strtoul(buf, &end, 0);
	if (end == buf) {
		ret = -EINVAL;
		goto out;
	}

	set_disk_ro(dev_to_disk(dev), set);
	ret = count;
out:
	return ret;
}
#endif

static struct falcon_blk_host * __init get_host_info(void)
{
	struct falcon_blk_host *host;
	struct falcon_blk_host_param *p;

	host = kmalloc(sizeof(struct falcon_blk_host), GFP_KERNEL);
	if(!host) {
		printk(KERN_ERR "falcon_blk: kmalloc error\n");
		return NULL;
	}

	p = falcon_blk_get_hostinfo();

	if(p) {
		printk("%s: Found target host info\n", __func__);
		host->name = p->name;
		host->max_hw_segs = p->max_hw_segs;
		host->max_phys_segs = p->max_phys_segs;
		host->max_seg_size = p->max_seg_size;

		host->max_req_size = p->max_req_size;
		host->max_blk_size = p->max_blk_size;
		host->max_blk_count = p->max_blk_count;

		host->irq = p->irq;

		host->dma_mask = p->dma_mask;
	}
	else {

		host->name = "falcon_blk default";
		host->max_hw_segs = 1;
		host->max_phys_segs = 32;
		host->max_seg_size = 128 * 1024;

		host->max_req_size = 128 * 1024;
		host->max_blk_size = SECTOR_SIZE;
		host->max_blk_count = host->max_req_size / SECTOR_SIZE;

		host->irq = 0;

		host->dma_mask = 0xffffffffULL;
	}

	return host;
}

#define FALCON_BLK_QUEUE_BOUNCESZ	131072
static int __init create_queue(struct falcon_blk_dev *dev, struct falcon_blk_part *part)
{
	int ret;
	struct falcon_blk_host *host = dev->host;
	u64 limit = BLK_BOUNCE_HIGH;

	if (host->dma_mask)
		limit = host->dma_mask;

	part->queue = blk_init_queue(falcon_blk_request, &part->lock);
	if (!part->queue) {
		printk(KERN_ERR "falcon_blk: blk_init_queue is failed.\n");
		return -EINVAL;
	}

	part->queue->queuedata = part;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	blk_queue_hardsect_size(part->queue, SECTOR_SIZE);
#else
	blk_queue_logical_block_size(part->queue, SECTOR_SIZE);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
	blk_queue_ordered(part->queue, QUEUE_ORDERED_DRAIN, NULL);
#endif

	if(host->max_hw_segs == 1) {

		unsigned int bouncesz;

		bouncesz = FALCON_BLK_QUEUE_BOUNCESZ;

		if (bouncesz > host->max_req_size)
			bouncesz = host->max_req_size;
		if (bouncesz > host->max_seg_size)
			bouncesz = host->max_seg_size;
		if (bouncesz > (host->max_blk_count * 512))
			bouncesz = host->max_blk_count * 512;

		if (bouncesz > 512) {
			if (!dev->bounce_buf) {
				dev->bounce_buf = kmalloc(bouncesz, GFP_DMA);
				if (!dev->bounce_buf) {
					printk(KERN_WARNING "falcon_blk: unable to "
					       "allocate bounce buffer\n");
				}
				else
					printk(KERN_WARNING "falcon_blk: bounce buf virt=0x%p phys=0x%lx\n",
					       dev->bounce_buf,
					       (unsigned long)virt_to_phys(dev->bounce_buf));
			}
		}

		if (dev->bounce_buf) {
			blk_queue_bounce_limit(part->queue, BLK_BOUNCE_ANY);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
			blk_queue_max_sectors(part->queue, bouncesz / 512);
			blk_queue_max_phys_segments(part->queue, bouncesz / 512);
			blk_queue_max_hw_segments(part->queue, bouncesz / 512);
#else
			blk_queue_max_hw_sectors(part->queue, bouncesz / 512);
			blk_queue_max_segments(part->queue, bouncesz / 512);
#endif
			blk_queue_max_segment_size(part->queue, bouncesz);

			if (!dev->sg) {
				dev->sg = kmalloc(sizeof(struct scatterlist),
						  GFP_KERNEL);
				if (!dev->sg) {
					printk(KERN_WARNING "falcon_blk: unable to "
					       "allocate sg list\n");
					ret = -ENOMEM;
					goto cleanup_queue;
				}
				sg_init_table(dev->sg, 1);
			}
			if (!dev->bounce_sg) {
				dev->bounce_sg = kmalloc(sizeof(struct scatterlist) *
							 bouncesz / 512, GFP_KERNEL);
				if (!dev->bounce_sg) {
					printk(KERN_WARNING "falcon_blk: unable to "
					       "allocate bounce sg list\n");
					ret = -ENOMEM;
					goto cleanup_queue;
				}
				sg_init_table(dev->bounce_sg, bouncesz / 512);

				printk("Use BounceBuffer (size = %d)\n", bouncesz);
			}
		}
	}

	if (!dev->bounce_buf) {

		blk_queue_bounce_limit(part->queue, limit);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
		blk_queue_max_sectors(part->queue,
				      min(host->max_blk_count, host->max_req_size / 512));
		blk_queue_max_phys_segments(part->queue, host->max_phys_segs);
		blk_queue_max_hw_segments(part->queue, host->max_hw_segs);
#else
		blk_queue_max_hw_sectors(part->queue,
					 min(host->max_blk_count, host->max_req_size / 512));
		blk_queue_max_segments(part->queue, host->max_phys_segs);
#endif
		blk_queue_max_segment_size(part->queue, host->max_seg_size);

		if (!dev->sg) {
			dev->sg = kmalloc(sizeof(struct scatterlist) *
					  host->max_phys_segs, GFP_KERNEL);
			if (!dev->sg) {
				ret = -ENOMEM;
				goto cleanup_queue;
			}
			sg_init_table(dev->sg, host->max_phys_segs);
		}
	}

	if (!dev->falcon_sg.list) {
		dev->falcon_sg.list = kmalloc(sizeof(struct falcon_sglist) * host->max_phys_segs,
					      GFP_KERNEL);
		if (!dev->falcon_sg.list) {
			ret = -ENOMEM;
			goto cleanup_queue;
		}
	}

	return 0;

cleanup_queue:
	if (dev->sg)
		kfree(dev->sg);
	dev->sg = NULL;
	if (dev->bounce_sg)
		kfree(dev->bounce_sg);
	dev->bounce_sg = NULL;
	if (dev->bounce_buf)
		kfree(dev->bounce_buf);
	dev->bounce_buf = NULL;
	blk_cleanup_queue(part->queue);

	return ret;
}

static int __init part_setup(struct falcon_blk_dev *dev, int major, int part_num,
			       struct falcon_blk_part **p_part)
{
	struct falcon_blk_part *part;
	char const falcon_block_name[FALCON_BLK_DEV_MAX][32] = {
		FALCON_BLK_DEV_NAME,
#if FALCON_BLK_DEV_MAX == 3
		FALCON_BLK_BOOTDEV0_NAME,
		FALCON_BLK_BOOTDEV1_NAME,
#endif
	};
	int ret;

	part = *p_part = kmalloc(sizeof(*part), GFP_KERNEL);
	if (!part) {
		printk(KERN_ERR "falcon_blk: unable to get host controller infomation\n");
		return -ENOMEM;
	}
	memset(part, 0, sizeof(struct falcon_blk_part));

	spin_lock_init(&part->lock);

	if(create_queue(dev, part) != 0) {
		printk(KERN_ERR "falcon_blk: unable to init queue\n");
		ret = -ENOMEM;
		goto err1;
	}

	part->gd = alloc_disk(FALCON_MINORS);
	if (!part->gd) {
		printk(KERN_ERR "falcon_blk: alloc_disk is failed.\n");
		ret = -ENOMEM;
		goto err2;
	}

	part->gd->major = major;
	part->gd->first_minor = part_num * FALCON_MINORS;
	part->gd->minors = FALCON_MINORS;
	part->gd->fops = &falcon_blk_ops;
	part->gd->queue = part->queue;
	part->gd->private_data = part;
        if (part_num) {
                /*
                 * For Boot Partitions, do not scan for MBR.
                 */
                part->gd->minors = 1;
        }

	falcon_binfo[part_num].part = part_num;
	part->falcon_binfo = &falcon_binfo[part_num];

	part->dev = dev;

	part->thread = kthread_run(falcon_blk_queue_thread, part, "falcon blk");
	if (IS_ERR(part->thread)) {
		printk(KERN_ERR "falcon_blk: kthread_run is failed.\n");
		ret = PTR_ERR(part->thread);
		goto err3;
	}

	strncpy(part->gd->disk_name, falcon_block_name[part_num], 32);

	set_capacity(part->gd, bios_svc(0x15, part_num, 0, 0, 0, 0));
/* Our system is expecting the boot block devices to not be read only on boot.
 * Normally I'd just delete this line of code, but due to the way we are doing
 * code drops for this file, it would be best to leave this commented out so
 * this doesn't regress.
	set_disk_ro(part->gd, part_num != 0);
*/
	add_disk(part->gd);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
	if (part_num != 0) {

		part->force_ro.show = force_ro_show;
		part->force_ro.store = force_ro_store;
		sysfs_attr_init(&part->force_ro.attr);
		part->force_ro.attr.name = "force_ro";
		part->force_ro.attr.mode = S_IRUGO | S_IWUSR;
		ret = device_create_file(disk_to_dev(part->gd), &part->force_ro);
		if (ret) {
			printk(KERN_ERR "falcon_blk: device_create_file is failed.\n");
			goto err3;
		}
	}
#endif

	return 0;

err3:
	del_gendisk(part->gd);
	kthread_stop(part->thread);
	put_disk(part->gd);
err2:
	blk_cleanup_queue(part->queue);
err1:
	kfree(part);
	*p_part = NULL;

	return ret;
}

static int __init init_falcon_blk(void)
{
	int ret;
	int major = -1;
	struct falcon_blk_dev *dev = NULL;
	struct falcon_blk_part *part[FALCON_BLK_DEV_MAX] = {};
	extern int can_use_falcon(void);
	extern int falcon_storage_init(void);
	int i;
	char *kernel_buffer;

	falcon_blk_platform_init();
	if(falcon_storage_init()) {
		printk("falcon_storage_init error\n");
		return -EINVAL;
	}

	if (!can_use_falcon()) {
		printk( "unable to get Falcon storage driver information\n");
		return -EINVAL;
	}

	if(platform_driver_register(&falcon_blk_driver)) {
		printk(KERN_ERR "falcon_blk: unable to register driver\n");
		return -EBUSY;
	}

	falcon_blk_platform_device = platform_device_register_simple(FALCON_BLK_DRV_NAME, -1, NULL, 0);
	if(IS_ERR(falcon_blk_platform_device)) {
		printk(KERN_ERR "falcon_blk: unable to register platform device\n");
		ret = -EBUSY;
		goto err1;
	}

	major = register_blkdev(MMC_QB_WRAPPER_MAJOR, FALCON_BLK_DRV_NAME);
        printk(KERN_ERR "falcon_blk: major number %d\n", major);
	if (major < 0) {
		printk(KERN_ERR "falcon_blk: unable to register block device\n");
		ret = -EBUSY;
		goto err1;
	}
        major = MMC_QB_WRAPPER_MAJOR;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		printk(KERN_ERR "falcon_blk: unable to get host controller infomation\n");
		ret = -ENOMEM;
		goto err1;
	}
	memset(dev, 0, sizeof(struct falcon_blk_dev));

	platform_set_drvdata(falcon_blk_platform_device, dev);

	dev->dev = &falcon_blk_platform_device->dev;
	dev->host = get_host_info();
	if(!dev->host) {
		printk(KERN_ERR "falcon_blk: blk_init_queue is failed.\n");
		ret = -ENOMEM;
		goto err1;
	}
	dev->dev->dma_mask = &dev->host->dma_mask;

	if(dev->host->irq > -1) {
		init_waitqueue_head(&dev->irq_waitq);
		falcon_set_wait_queue(&dev->irq_waitq, &dev->irq_handled);
		dev->use_irq = 1;

		ret = request_irq(dev->host->irq, falcon_blk_irq, 0, "falcon_blk", dev);
		if(ret) {
			printk(KERN_ERR "falcon_blk: request_irq is failed.\n");
			ret = -EBUSY;
			goto err1;
		}

		printk("%s: Use interrupt : IRQ=%d\n", __func__, dev->host->irq);
	}
	else {
		dev->use_irq = 0;
		printk("%s: Not Use interrupt\n", __func__);
	}
	sema_init(&dev->falcon_sem, 1);

	for (i = 0; i < FALCON_BLK_DEV_MAX; i++) {
		if (!bios_svc(0x15, i, 0, 0, 0, 0)) {
			break;
		}

		ret = part_setup(dev, major, i, &part[i]);

		if (ret) {
			goto err2;
		}
	}
	init_storage_common();

	kernel_buffer = vmalloc(KERNEL_MMC_PRINT_BUFFER_SIZE);

	if(!kernel_buffer)
		printk(KERN_ERR "error allocating %luM memory.\n", KERNEL_MMC_PRINT_BUFFER_SIZE/(1024*1024UL));
	else {
		/* assign the 1M buffer */
		pp_bios_debug_buf = (char **)(CONFIG_FALCON_STORAGE_BIOS_ADDR + CONFIG_FALCON_STORAGE_BIOS_SIZE - 4 * sizeof(u32));
		*pp_bios_debug_buf = kernel_buffer;
	}
	
	return 0;

err2:
	for (i = 0; i < FALCON_BLK_DEV_MAX; i++) {
		if (part[i]) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
			if (i != 0) {
				device_remove_file(disk_to_dev(part[i]->gd),
						   &part[i]->force_ro);
			}
#endif
			del_gendisk(part[i]->gd);
			kthread_stop(part[i]->thread);
			blk_cleanup_queue(part[i]->queue);
			put_disk(part[i]->gd);
			kfree(part[i]);
		}
	}

	if (dev->host->irq > -1)
		free_irq(dev->host->irq, dev);

err1:
	if(dev) {
		if(dev->host)
			kfree(dev->host);
		if(dev->sg)
			kfree(dev->sg);
		if(dev->bounce_sg)
			kfree(dev->bounce_sg);
		if(dev->bounce_buf)
			kfree(dev->bounce_buf);
		if(dev->falcon_sg.list)
			kfree(dev->falcon_sg.list);
		kfree(dev);
		dev = NULL;
	}

	if(major > 0)
		unregister_blkdev(0, "falcon_blk");

	platform_driver_unregister(&falcon_blk_driver);

	printk("#### %s: error!!\n", __func__);

	return ret;
}

static void __exit exit_falcon_blk(void)
{
}

module_init(init_falcon_blk);
module_exit(exit_falcon_blk);
