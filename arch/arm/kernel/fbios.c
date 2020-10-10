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
#include <linux/init.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/bootmem.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/circ_buf.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
#include <linux/sysdev.h>
#endif
#include <linux/falcon_sbios_buf.h>
#include <asm/cacheflush.h>
#include <asm/falcon_syscall.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/ptrace.h>
#ifdef CONFIG_SMP
#include <asm/cpu.h>
#ifdef CONFIG_GENERIC_SMP_IDLE_THREAD
#include <asm/smp.h>
#endif
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
#include <asm/idmap.h>
#endif
#include <asm/tlbflush.h>

#ifdef CONFIG_HAVE_MEMBLOCK
#include <linux/memblock.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,27)
#undef for_each_nodebank
#endif
#define for_each_nodebank(iter,mi,no)                   \
        for (iter = 0; iter < (mi)->nr_banks; iter++)   \
                if ((mi)->bank[iter].node == no)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
#define bank_pfn_start(bank)    __phys_to_pfn((bank)->start)
#define bank_pfn_end(bank)      __phys_to_pfn((bank)->start + (bank)->size)
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,6)
#define USABLE_AREA     0xe7fddef1
#else
#define USABLE_AREA     0x0
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,6)
#define WORK_OFFSET     0xc00
#else
#define WORK_OFFSET     0x200
#endif
#define MAX_USABLE_AREA	8
#define ENTRIES_PER_PAGE (PAGE_SIZE/KERNEL_MMC_PRINT_CHUNK_SIZE)

enum {
  SBIOS_CALL_ERASE,
  SBIOS_CALL_DUMMY1,
  SBIOS_CALL_DUMMY2,
  SBIOS_CALL_DUMMY3,
  SBIOS_CALL_MAX
};

const u32 vector_work_offset = WORK_OFFSET;
EXPORT_SYMBOL(vector_work_offset);

struct bios {
	u32 marker;
};

struct erase_struct {
	u32 sector_start;
	u32 sector_end;
	int part_num;
};

static int is_exist_bios = 0;
static int is_bios_initialized = 0;
#ifdef CONFIG_FALCON_PSEUDO_NMI
static int is_exist_pnmi_handler = 0;
#endif
static void (*falcon_callback_fn)(void) = NULL;

static int do_falcon_callback(unsigned long addr, unsigned int fsr,
			      struct pt_regs *regs);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
static int falcon_suspend(struct sys_device *dev, pm_message_t state);
static int falcon_resume(struct sys_device *dev);
#else
static int falcon_suspend(void);
static void falcon_resume(void);
#endif
int in_falcon(void);

#ifdef CONFIG_WAKELOCK
#include <linux/wakelock.h>
static struct wake_lock falcon_wakelock;

#define TIMEOUT_FALCON_WAKELOCK		HZ * 7
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
static struct sysdev_class falcon_sysclass = {
	.name = "falcon",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
	.suspend = falcon_suspend,
	.resume = falcon_resume,
#endif
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
#include <linux/syscore_ops.h>
static struct syscore_ops falcon_syscore_ops = {
	.suspend = falcon_suspend,
	.resume = falcon_resume,
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
static struct sys_device device_falcon = {
	.id	= 0,
	.cls	= &falcon_sysclass,
};
#endif
static char **pp_bios_debug_buf = (char **)(CONFIG_FALCON_STORAGE_BIOS_ADDR + CONFIG_FALCON_STORAGE_BIOS_SIZE - 4 * sizeof(u32));
static int *pp_bios_debug_buf_enable = (int *)(CONFIG_FALCON_STORAGE_BIOS_ADDR + CONFIG_FALCON_STORAGE_BIOS_SIZE - 5 * sizeof(u32));
static int *pp_bios_cir_buf_head = (int *)(CONFIG_FALCON_STORAGE_BIOS_ADDR + CONFIG_FALCON_STORAGE_BIOS_SIZE - 6 * sizeof(u32));
static int *pp_bios_cir_buf_tail = (int *)(CONFIG_FALCON_STORAGE_BIOS_ADDR + CONFIG_FALCON_STORAGE_BIOS_SIZE - 7 * sizeof(u32));

extern int falcon_blk_kernel_sbios_call(void *_arg, int _size, void *_data);
extern int falcon_part_num_parse(char *path);

static int mmc_erase_result = 0;

static ssize_t falcon_ctrl_erase(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t size) {
        
	size_t buf_len = strlen(buf);
	struct erase_struct c;
	char* path;
        u32 from;
        u32 to;
	int part_num;
	int ret = 0;

	mmc_erase_result = 0;
	
	path = kmalloc(buf_len + 1, GFP_KERNEL);
	if (sscanf(buf, "%s %d %d", path, &from, &to) != 3) {
		printk(KERN_ERR "Error reading device path first_sector and nr_sectors\n");
		return -1;
	}
	
	part_num = falcon_part_num_parse(path);

	if(part_num < 0) {
		printk(KERN_ERR "invalid part num \n");
		return size;
	}
		
	c.part_num = part_num;
        c.sector_start = from;
        c.sector_end = to;
        
	ret = falcon_blk_kernel_sbios_call(SBIOS_CALL_ERASE, sizeof(c), &c);
	
	if(!ret)
		mmc_erase_result = 1;
        
	return size;
}

static ssize_t falcon_ctrl_erase_result(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	char *curr = buf;
	curr += sprintf(curr, "%d\n", mmc_erase_result);
	return curr - buf;
}
static SYSDEV_ATTR(falcon_ctrl_erase, S_IRUSR | S_IWUSR, falcon_ctrl_erase_result, falcon_ctrl_erase);

static ssize_t sbios_debug_enable_store(struct sys_device *dev, struct sysdev_attribute *attr, const char *buf, size_t size)
{
	int value = 0;
		
	if (sscanf(buf, "%d", &value) <= 0) { 
		printk(KERN_ERR "%s: Could not enable printk debug buf\n", __func__);
		return -1;
	}

	*pp_bios_debug_buf_enable = value;	

	return size;
}

static ssize_t sbios_debug_enable_show(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", *pp_bios_debug_buf_enable);
}
static SYSDEV_ATTR(sbios_debug_enable, S_IRUSR | S_IWUSR, sbios_debug_enable_show, sbios_debug_enable_store);

static ssize_t sbios_buf_dump(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	int head, tail; 
	char *bufp;
	/* only up to one page can be printed out each time */
	int cnt  = 0;
	char *curr = buf;
	
	if(*pp_bios_debug_buf == NULL)
		return sprintf(buf, "S-BIOS Circular Buffer not initialized\n");
		
	/* disable log during the dump */
	*pp_bios_debug_buf_enable = 0;
	
	head = *pp_bios_cir_buf_head;
	tail = *pp_bios_cir_buf_tail;

	while (cnt++ < ENTRIES_PER_PAGE) {
	
		if (CIRC_CNT(head, tail, KERNEL_MMC_PRINT_FIFO_SIZE) < 1) {
			curr += sprintf(curr, "End of S-BIOS Circular Buffer\n");
			break;
		}

		bufp = *pp_bios_debug_buf + (tail * KERNEL_MMC_PRINT_CHUNK_SIZE);		

		tail = (tail + 1) & (KERNEL_MMC_PRINT_FIFO_SIZE - 1);
			
		curr += sprintf(curr, "%s\n", bufp);
	}
	
	/* update the tail pointer for next cat operation */
	*pp_bios_cir_buf_tail = tail;
	
	return curr - buf;
}
static SYSDEV_ATTR(sbios_debug_dump, S_IRUSR | S_IWUSR, sbios_buf_dump, NULL);

static struct platform_suspend_ops *suspend_ops_orig = NULL;

int can_use_falcon(void)
{
	return is_exist_bios && is_bios_initialized;
}
EXPORT_SYMBOL(can_use_falcon);

int falcon_storage_init(void)
{
	if (!is_exist_bios)
		return -1;

	if (bios_svc(0x12, 0, 0, 0, 0, 0)) {
		printk(KERN_ERR "storage_init error\n");
		is_exist_bios = 0;
	}

	return 0;
}
EXPORT_SYMBOL(falcon_storage_init);
#if !defined(CONFIG_FALCON_BLK) && !defined(CONFIG_FALCON_MTD_NAND) && !defined(CONFIG_FALCON_MTD_NOR)
device_initcall(falcon_storage_init);
#endif

static void __init replace_ioaddr(void)
{
	int ret;
	u32 paddr, size, *vaddr;

	while ((ret = bios_svc(0x9, &paddr, &size, &vaddr, 0, 0)) == 0) {
		void *addr;

		addr = ioremap(paddr, size);
		if (addr == NULL) {
			printk(KERN_ERR "ioremap error!! paddr=%x\n", paddr);
		}

		printk(KERN_DEBUG "IOTABLE phys:%08x size:%08x ptr:%p => %p\n",
			   paddr, size, vaddr, addr);

		*vaddr = (u32)addr;
	}
}

#ifndef CONFIG_ARM_LPAE

u32 saved_pte;

static void add_write_perm(u32 addr)
{
	u32 *l1_base = (u32 *) init_mm.pgd;
	u32 *l2_base  = (u32 *) phys_to_virt(l1_base[addr >> 20] & 0xfffffc00);
	u32 *ptep = &l2_base[(addr >> 12) & 0xff];

	saved_pte = *ptep;
	*ptep = *ptep & ~(PTE_EXT_APX);

	__cpuc_flush_kern_all();
	barrier();
	__cpu_flush_kern_tlb_range(addr & PAGE_MASK, PAGE_SIZE);

}
EXPORT_SYMBOL(add_write_perm);

static void restore_perm(u32 addr)
{
	u32 *l1_base = (u32 *) init_mm.pgd;
	u32 *l2_base  = (u32 *) phys_to_virt(l1_base[addr >> 20] & 0xfffffc00);
	u32 *ptep = &l2_base[(addr >> 12) & 0xff];

	*ptep = saved_pte;

	__cpuc_flush_kern_all();
	barrier();
	__cpu_flush_kern_tlb_range(addr & PAGE_MASK, PAGE_SIZE);

}
EXPORT_SYMBOL(restore_perm);

#else
static void modify_pte(u32 addr, u64 clr_bit, u64 set_bit)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(&init_mm, addr);
	if (!pgd_present(*pgd))
		return;

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		return;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		return;

	pte = pte_offset_map(pmd, addr);
	if (pte_present(*pte)) {
		pte_t pteval = pte_val(*pte);
		pteval &= ~clr_bit;
		pteval |= set_bit;
		*pte = pteval;
	}
	pte_unmap(pte);

	__cpuc_flush_kern_all();
	barrier();
	__cpu_flush_kern_tlb_range(addr & PAGE_MASK, PAGE_SIZE);
}

static void add_write_perm(u32 addr)
{
	modify_pte(addr, L_PTE_RDONLY, 0);
}
EXPORT_SYMBOL(add_write_perm);

static void restore_perm(u32 addr)
{
	modify_pte(addr, 0, L_PTE_RDONLY);
}
EXPORT_SYMBOL(restore_perm);
#endif

static int __init prepare_for_hook(void)
{
	u32 vector = 0xffff0000;
	volatile u32 *addr = (u32 *)(0xffff0000 + vector_work_offset);
	int count = 0;
	int i;

	if(!USABLE_AREA)
		return 0;

	add_write_perm(vector);

	for (i = 0; i < vector_work_offset / 4; i++) {
		addr--;
		if (*addr == USABLE_AREA) {
			*addr = 0;
			count++;
			if(count >= MAX_USABLE_AREA)
				break;
		}
	}

	restore_perm(vector);

	if(count < MAX_USABLE_AREA)
		return -EINVAL;

	return 0;
}

static int __init hook_vector(int offset)
{
	volatile u32 *vector = (u32 *)(0xffff0000 + offset);
	volatile u32 *addr = (u32 *)(0xffff0000 + vector_work_offset);
	u32 org_vector;
	int i;
	int ret;
	unsigned long flags;

	for (i = 0; i < vector_work_offset / 4; i++) {
		addr--;
		if (*addr == 0)
			break;
	}

	if (*addr) {
		printk("Fatal: FALCON workarea is not found.\n");
		return -EINVAL;
	}

	local_irq_save(flags);

	add_write_perm((u32)vector);

	*addr = CONFIG_FALCON_BIOS_ADDR + offset;

	org_vector = *vector;

	*vector = 0xe59ff000 | ((u32)addr - (u32)vector - 8);
	__cpuc_flush_kern_all();

	ret = bios_svc(0x1, org_vector, CONFIG_FALCON_STORAGE_BIOS_ADDR, 1, 0, 0);
	if (ret) {
		printk("Fatal: hooking failed.");
		*vector = org_vector;
		*addr = 0;
	}

	__cpuc_flush_kern_all();

	restore_perm((u32)vector);

	local_irq_restore(flags);

	return 0;
}

static int __init check_bios(void)
{
	const struct bios *bios =
		(const struct bios *)(CONFIG_FALCON_BIOS_ADDR + 0x30);
	const struct bios *sbios =
		(const struct bios *)(CONFIG_FALCON_STORAGE_BIOS_ADDR + 0x20);

	if (bios->marker != 0x51494255 || sbios->marker != 0x51494255)
		return -1;

	printk("Falcon BIOS found.\n");

	return 0;
}

#ifdef CONFIG_FALCON_PSEUDO_NMI
static int __init check_pnmi_handler(void)
{
	struct header {
		u32 marker;
		u32 revision;
	};
	const struct header *header =
		(const struct header *)(CONFIG_FALCON_PSEUDO_NMI_HANDLER_ADDR + 0x20);

	if (header->marker != 0x71696275)
		return -1;

	printk("Pseudo NMI handler found.\n");

	return 0;
}

#ifdef CONFIG_FALCON_PSEUDO_NMI_VECTOR_HOOK
int __init hook_irq_vector(int offset)
{
        volatile u32 *vector = (u32 *)(0xffff0000 + offset);
        volatile u32 *addr = (u32 *)(0xffff0000 + STUBS_OFFSET);

        int i;
        unsigned long flags;

        if(check_pnmi_handler()) {
                return -EINVAL;
        }

        for (i = 0; i < STUBS_OFFSET / 4; i++) {
                addr--;
                if (*addr == 0)
                        break;
        }

        if (*addr) {
                printk("Fatal: FALCON workarea is not found.\n");
                return -EINVAL;
        }

        local_irq_save(flags);

        add_write_perm((u32)vector);

        *addr = CONFIG_FALCON_PSEUDO_NMI_HANDLER_ADDR + 8;

        *vector = 0xe59ff000 | ((u32)addr - (u32)vector - 8);
        __cpuc_flush_kern_all();

        restore_perm((u32)vector);

        local_irq_restore(flags);

        return 0;
}
#endif
#endif

static void __init register_bank(void)
{
#ifdef CONFIG_HAVE_MEMBLOCK
	struct memblock_region *reg;

	for_each_memblock(memory, reg) {
		phys_addr_t addr = reg->base;
		phys_addr_t left = reg->size;

		while (left > 0) {
			u32 size = (left > 0xc0000000UL)
				? 0x10000000UL : (u32)left;
			bios_svc(0x26, (u32)addr, size, (u32)__va(addr), (u32)((u64)addr >> 32), 0);
			addr += size;
			left -= size;
		}
	}
#else
	extern struct meminfo meminfo;
	int i;

	for_each_nodebank (i, &meminfo, 0) {
		struct membank *bank = &meminfo.bank[i];
		u32 vaddr = (u32)__va(bank->start);
		bios_svc(0x26, bank->start, bank->size, vaddr, 0, 0);
	}
#endif
}

static int __init falcon_init(void)
{
	if (is_exist_bios == 0)
		return 0;

	if (prepare_for_hook()) {
		printk("Hook preparation is failed.\n");
		return -EINVAL;
	}

	if (hook_vector(4)) {
		printk("BIOS initialization is failed.\n");
		return -EINVAL;
	}

#ifdef CONFIG_FALCON_PSEUDO_NMI_VECTOR_HOOK

	if (hook_irq_vector(0x18)) {
		printk("Hook IRQ vector failed.\n");
	}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	hook_fault_code(0, do_falcon_callback, SIGSEGV, "vector exception");
#else
	hook_fault_code(0, do_falcon_callback, SIGSEGV, 0, "vector exception");
#endif

	replace_ioaddr();

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
	if (sysdev_class_register(&falcon_sysclass) ||
	    sysdev_register(&device_falcon))
		is_exist_bios = 0;
	else { 
		sysdev_create_file(&device_falcon, &attr_falcon_ctrl_erase);
		sysdev_create_file(&device_falcon, &attr_sbios_debug_enable);
		sysdev_create_file(&device_falcon, &attr_sbios_debug_dump);
	}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
	register_syscore_ops(&falcon_syscore_ops);
#endif
	register_bank();

	is_bios_initialized = 1;

#ifdef CONFIG_WAKELOCK
	wake_lock_init(&falcon_wakelock, WAKE_LOCK_SUSPEND, "falcon");
#endif

	return 0;
}
core_initcall(falcon_init);

int falcon_workmem_size = CONFIG_FALCON_BIOS_WORK_SIZE;
EXPORT_SYMBOL(falcon_workmem_size);
void *falcon_workmem_addr = NULL;
EXPORT_SYMBOL(falcon_workmem_addr);

#ifdef CONFIG_HAVE_MEMBLOCK
#define BIOS_AREA_ADDR (CONFIG_FALCON_STORAGE_BIOS_ADDR)
#define BIOS_AREA_SIZE (CONFIG_FALCON_STORAGE_BIOS_SIZE + CONFIG_FALCON_BIOS_SIZE)

static int is_reserved_memblock = 0;

void __init falcon_mem_reserve(void)
{
	if (CONFIG_FALCON_STORAGE_BIOS_ADDR !=
		CONFIG_FALCON_BIOS_ADDR - CONFIG_FALCON_STORAGE_BIOS_SIZE) {
		printk(KERN_ERR "#### Error!! BIOS area is not valid!!\n");
		return;
	}

	if(!memblock_is_region_memory(__virt_to_phys(BIOS_AREA_ADDR), BIOS_AREA_SIZE)) {
		pr_err("#### Error!! BIOS area is not in a memory region!!\n");
		return;
	}

	if(memblock_is_region_reserved(__virt_to_phys(BIOS_AREA_ADDR), BIOS_AREA_SIZE)) {
		pr_err("#### Error!! BIOS area overlaps in-use memory region!!\n");
		return;
	}

	memblock_reserve(__virt_to_phys(BIOS_AREA_ADDR), BIOS_AREA_SIZE);

	is_reserved_memblock = 1;

	return;
}

#ifdef CONFIG_FALCON_PSEUDO_NMI
static int is_reserved_pnmi_handler = 0;

void __init falcon_pnmi_handler_reserve(void)
{
	u32 handler_addr = CONFIG_FALCON_PSEUDO_NMI_HANDLER_ADDR;
	u32 handler_size = CONFIG_FALCON_PSEUDO_NMI_HANDLER_SIZE;

	if(!memblock_is_region_memory(__virt_to_phys(handler_addr), handler_size)) {
		pr_err("#### Error!! Pseudo NMI handler area is not in a memory region!!\n");
		return;
	}

	if(memblock_is_region_reserved(__virt_to_phys(handler_addr), handler_size)) {
		pr_err("#### Error!! Ext Pseudo NMI handler area overlaps in-use memory region!!\n");
		return;
	}

	memblock_reserve(__virt_to_phys(handler_addr), handler_size);

	is_reserved_pnmi_handler = 1;

	return;
}
#endif

#endif

void __init falcon_mm_init(unsigned long start_pfn, unsigned long end_pfn)
{
#ifndef CONFIG_HAVE_MEMBLOCK
	int ret;
	u32 paddr;
	u32 size;
#endif
	u32 end_addr = (u32)phys_to_virt(__pfn_to_phys(end_pfn));

	if (is_exist_bios)
		return;
#ifdef CONFIG_HAVE_MEMBLOCK
	if(!is_reserved_memblock)
		return;
#else

	if (CONFIG_FALCON_STORAGE_BIOS_ADDR !=
		CONFIG_FALCON_BIOS_ADDR - CONFIG_FALCON_STORAGE_BIOS_SIZE) {
		printk(KERN_ERR "#### Error!! BIOS area is not valid!!\n");
		return;
	}

	if (CONFIG_FALCON_STORAGE_BIOS_ADDR < (u32)_end) {
		printk(KERN_ERR "#### Error!! SBIOS area is too low!!\n");
		return;
	}
#endif

	if (CONFIG_FALCON_BIOS_ADDR + CONFIG_FALCON_BIOS_SIZE > end_addr) {
		printk(KERN_ERR "#### Error!! BIOS area exceeds lowmem region!!\n");
#ifdef CONFIG_HAVE_MEMBLOCK
		memblock_free(__virt_to_phys(BIOS_AREA_ADDR), BIOS_AREA_SIZE);
#endif
		return;
	}

#ifndef CONFIG_HAVE_MEMBLOCK
	paddr = __virt_to_phys(CONFIG_FALCON_STORAGE_BIOS_ADDR);
	size = CONFIG_FALCON_BIOS_SIZE + CONFIG_FALCON_STORAGE_BIOS_SIZE;
	ret = reserve_bootmem(paddr, size, BOOTMEM_EXCLUSIVE);
	if(ret) {
		printk(KERN_ERR "#### Error!! BIOS area has been already reserved!!\n");
		return;
	}
#endif

	if(falcon_workmem_size) {
#ifndef CONFIG_HAVE_MEMBLOCK
		falcon_workmem_addr = __alloc_bootmem(falcon_workmem_size,
							PAGE_SIZE, __pa((u32)_end));
#else
		falcon_workmem_addr = __va(memblock_alloc(falcon_workmem_size, PAGE_SIZE));
#endif
		if(!falcon_workmem_addr) {
			pr_err("#### Error!! Can't allocate BIOS work area\n");
		}
		else {
			memset(falcon_workmem_addr, 0, falcon_workmem_size);
		}
	}

	if (check_bios()) {
		printk("Falcon BIOS is not found.\n");
		return;
	}
	is_exist_bios = 1;

#ifdef CONFIG_FALCON_PSEUDO_NMI
	if(!is_reserved_pnmi_handler)
		return;

	if(check_pnmi_handler()) {
		printk("Falcon Pseudo NMI handler is not found.\n");
		return;
	}
	is_exist_pnmi_handler = 1;
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
static int falcon_suspend(struct sys_device *dev, pm_message_t state)
#else
static int falcon_suspend(void)
#endif
{
	if (!is_exist_bios)
		return 0;

	return bios_svc(0x21, 0, 0, 0, 0, 0) ? -EINVAL : 0;
}

#ifdef CONFIG_WAKELOCK
static void falcon_resume_timeout(unsigned long data)
{
	wake_lock(&falcon_wakelock);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
#define RES_RET_TRUE	(0)
#define RES_RET_ERROR	(-EINVAL)
static int falcon_resume(struct sys_device *dev)
#else
#define RES_RET_TRUE
#define RES_RET_ERROR
static void falcon_resume(void)
#endif
{
	if (!is_exist_bios)
		return RES_RET_TRUE;

	if (in_falcon()) {
#ifdef CONFIG_WAKELOCK
		static struct timer_list timer;
		init_timer_on_stack(&timer);
		timer.expires = jiffies + 1;
		timer.function = falcon_resume_timeout;
		add_timer(&timer);
#endif
		return RES_RET_TRUE;
	}

	if (bios_svc(0x12, 0, 0, 0, 0, 0)) {
		is_exist_bios = 0;
		printk("falcon_resume error\n");
		return RES_RET_ERROR;
	}

	return RES_RET_TRUE;
}

int suspend_falcon_enter(struct platform_suspend_ops *ops)
{
	extern struct platform_suspend_ops *suspend_ops;
#ifdef CONFIG_EARLYSUSPEND
	extern void request_suspend_state(suspend_state_t state);
#endif

	suspend_ops_orig = suspend_ops;
	suspend_set_ops(ops);

#ifdef CONFIG_EARLYSUSPEND
	request_suspend_state(PM_SUSPEND_MEM);
	return 0; 
#else
	return pm_suspend(PM_SUSPEND_MEM);
#endif
}
EXPORT_SYMBOL(suspend_falcon_enter);

void suspend_falcon_exit(void)
{
	if (!suspend_ops_orig)
		return;

	suspend_set_ops(suspend_ops_orig);
	suspend_ops_orig = NULL;

#ifdef CONFIG_WAKELOCK

	wake_lock_timeout(&falcon_wakelock, TIMEOUT_FALCON_WAKELOCK);
#endif
}
EXPORT_SYMBOL(suspend_falcon_exit);

int in_falcon(void)
{
	return !!suspend_ops_orig;
}
EXPORT_SYMBOL(in_falcon);

void set_falcon_callback(void (*func)(void))
{
	falcon_callback_fn = func;
}

void call_falcon_callback(void)
{
	if (falcon_callback_fn)
		falcon_callback_fn();
}
EXPORT_SYMBOL(call_falcon_callback);

static int do_falcon_callback(unsigned long addr, unsigned int fsr,
			      struct pt_regs *regs)
{
	call_falcon_callback();
	asm volatile ("mcr p15, 0, %0, c5, c0, 0\n\t" :: "r"(0x40f));
	return 0;
}

#ifndef CONFIG_ARM_LPAE
static void falcon_revert_ownerless_mm(struct task_struct *p)
{
	if (p == NULL || p->active_mm == NULL)
		return;

	if (atomic_read(&p->active_mm->mm_users) < 1) {
		bios_svc(0x16, p->pid, p->active_mm->pgd, 0, 0, 0);
	}
}

void falcon_revert_process(void)
{
	struct task_struct *p;
#ifdef CONFIG_SMP
	int cpu;
#endif

	read_lock(&tasklist_lock);

	bios_svc(0x16, 0, init_mm.pgd, 0, 0, 0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
	bios_svc(0x16, 0, idmap_pgd, 0, 0, 0);
#endif

#ifndef CONFIG_SMP
	falcon_revert_ownerless_mm(current);
#else
	for_each_possible_cpu(cpu) {
#ifndef CONFIG_GENERIC_SMP_IDLE_THREAD
		p = (&per_cpu(cpu_data, cpu))->idle;
#else
		extern struct task_struct * get_cpu_idle_thread(unsigned int cpu);

		p = get_cpu_idle_thread(cpu);
		if (IS_ERR(p))
                	continue;
#endif
		if (p != NULL) {
			falcon_revert_ownerless_mm(p);
		}
	}
#endif

	for_each_process (p) {
		if (p->mm) {
			bios_svc(0x16, p->pid, p->mm->pgd, 0, 0, 0);
		}
	}

	read_unlock(&tasklist_lock);

}
#else
void falcon_revert_process(void)
{
	bios_svc(0x16, 0, init_mm.pgd, 0, 0, 0);
}
#endif
EXPORT_SYMBOL(falcon_revert_process);

int falcon_register_page(void)
{
#ifdef CONFIG_HAVE_MEMBLOCK
	struct memblock_region *reg;
	for_each_memblock(memory, reg) {
		u32 start_pfn, end_pfn;
		start_pfn = memblock_region_memory_base_pfn(reg);
		end_pfn = memblock_region_memory_end_pfn(reg);

		bios_svc(0x5, pfn_to_page(start_pfn), (u32)reg->base, end_pfn - start_pfn, (u32)((u64)reg->base >> 32), 0);
	}
#else
	extern struct meminfo meminfo;
	int bank;
	for (bank = 0; bank < meminfo.nr_banks; bank++) {
		u32 start_pfn = bank_pfn_start(&meminfo.bank[bank]);
		u32 end_pfn = bank_pfn_end(&meminfo.bank[bank]);
		bios_svc(0x5, pfn_to_page(start_pfn), __pfn_to_phys(start_pfn), end_pfn - start_pfn, 0, 0);
	}
#endif
	return 0;
}
EXPORT_SYMBOL(falcon_register_page);

#ifdef CONFIG_FALCON_PSEUDO_NMI

void handle_pseudo_nmi(u32 irqno, struct pt_regs *regs)
{
	if(is_exist_pnmi_handler) {
		void (*handler)(u32, struct pt_regs *) = (void *)CONFIG_FALCON_PSEUDO_NMI_HANDLER_ADDR;
 		handler(irqno, regs);
	}
	else {
		printk("WARNING: Pseudo NMI handler is missing!!\n");
	}
}

static int is_running_on_falcon(u32 ret_addr)
{
	u32 bios_area_start = CONFIG_FALCON_STORAGE_BIOS_ADDR;
        u32 bios_area_end = bios_area_start + CONFIG_FALCON_STORAGE_BIOS_SIZE + CONFIG_FALCON_BIOS_SIZE;

	if(ret_addr >= bios_area_start && ret_addr < bios_area_end) {

		return 1;
	}

	return 0;
}
#endif

#ifdef CONFIG_FALCON_DRV
static int is_running_falcon_callback(void)
{
	u32 dfsr;
	asm volatile ("mrc p15, 0, %0, c5, c0, 0\n\t" : "=r"(dfsr));

	if ((dfsr & 0x40f) == 0) {
		return 1;
	}
	return 0;
}
#endif

bool falcon_needs_skipping_preempt(struct pt_regs *p_regs)
{
#ifdef CONFIG_FALCON_PSEUDO_NMI
	if (is_running_on_falcon(p_regs->ARM_lr)) {
		return 1;
	}
#endif
#ifdef CONFIG_FALCON_DRV
	if (is_running_falcon_callback()) {
		return 1;
	}
#endif
	return 0;
}
