#ifndef _FALCONMEM_H_
#define _FALCONMEM_H_

typedef struct falcon_meminfo_t {
	struct mm_struct *mm; //optional, only used for usermem ranges
	u32 paddr;
	u32 size;
	struct list_head mem_list;
} falcon_meminfo;

struct falcon_pidinfo {
	pid_t pid;
	struct mm_struct *mm;
	int flags;
	unsigned int num_regions;
	uintptr_t *regions;

	struct list_head info_list;
};

extern int falcon_meminfo_pages;

extern struct list_head falcon_kernel_ranges_head;
void falcon_add_preload_kernel_range(u32 paddr, u32 size);
void falcon_del_preload_kernel_range(u32 paddr, u32 range);

extern struct list_head falcon_physical_ranges_head;
void falcon_add_preload_physical_range(u32 paddr, u32 size);
void falcon_del_preload_physical_range(u32 paddr, u32 range);

extern struct list_head falcon_usermem_ranges_head;
void falcon_add_preload_user_range(struct mm_struct *mm, u32 paddr, u32 size);
void falcon_del_preload_user_range(struct mm_struct *mm, u32 paddr, u32 range);
void falcon_delete_all_preload_usermem(void);

extern struct list_head falcon_process_options;
void fixup_falcon_process_options(void);

#define PROCESS_SAVE_CHAR_ALL_ANON   'A'
#define PROCESS_SAVE_CHAR_ACT_FILE   'f'
#define PROCESS_SAVE_CHAR_ALL_FILE   'F'
#define PROCESS_SAVE_CHAR_REGION_START '<'
#define PROCESS_SAVE_CHAR_REGION_END   '>'

#define PROCESS_SAVE_FLAG_ALL_ANON   1
#define PROCESS_SAVE_FLAG_ACT_FILE   2
#define PROCESS_SAVE_FLAG_INACT_FILE 4
#define PROCESS_SAVE_FLAG_ALL_FILE   (PROCESS_SAVE_FLAG_INACT_FILE | PROCESS_SAVE_FLAG_ACT_FILE)

#endif /* _FALCONMEM_H_ */
