/*
 * bootdata.c
 *
 * Copyright (C) 2006-2010 Amazon Technologies
 *
 */

#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <mach/boot_globals.h>
#include <linux/jiffies.h>

// Templates for reading/writing numeric boot-global data.
//
typedef unsigned long (*get_boot_data_t)(void);
typedef void (*set_boot_data_t)(unsigned long boot_data);

#define PROC_GET_BOOT_DATA(f) return proc_get_boot_data(page, start, off, count, eof, data, (get_boot_data_t)f)
#define PROC_SET_BOOT_DATA(f) return proc_set_boot_data(file, buf, count, data, (set_boot_data_t)f)

static int
proc_get_boot_data(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data,
    get_boot_data_t get_boot_data)
{
    int len = sprintf(page, "%08X\n", (unsigned int)(*get_boot_data)());

    *eof = 1;

    return len;
}

static int
proc_set_boot_data(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data,
    set_boot_data_t set_boot_data)
{
    char lbuf[16];

    memset(lbuf, 0, 16);

    if (copy_from_user(lbuf, buf, 8)) {
        return -EFAULT;
    }

    (*set_boot_data)(simple_strtol(lbuf, NULL, 16)); 

    return count;
}

// read/write update_flag
//
static int
proc_update_flag_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_update_flag);
}

static int
proc_update_flag_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_update_flag);
}

// read/write update_data
//
static int
proc_update_data_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_update_data);
}

static int
proc_update_data_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_update_data);
}

// read/write drivemode_screen_ready
//
static int
proc_drivemode_screen_ready_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_drivemode_screen_ready);
}

static int
proc_drivemode_screen_ready_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_drivemode_screen_ready);
}

// read/write framework_started, framework_running, framework_stopped
//
static int
proc_framework_started_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_framework_started);
}

static int
proc_framework_started_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_framework_started);
}

static int
proc_framework_running_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_framework_running);
}

static int
proc_framework_running_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_framework_running);
}

static int
proc_framework_stopped_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_framework_stopped);
}

static int
proc_framework_stopped_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_framework_stopped);
}

// read/write env_script
//
static int
proc_env_script_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    int len = sprintf(page, "%s", get_env_script());

    *eof = 1;

    return len;
}

static int
proc_env_script_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    char lbuf[ENV_SCRIPT_SIZE];
    
    if (count > ENV_SCRIPT_SIZE) {
        return -EINVAL;
    }

    memset(lbuf, 0, count);

    if (copy_from_user(lbuf, buf, count)) {
        return -EFAULT;
    }

    lbuf[count] = '\0';
    set_env_script(lbuf);

    return count;
}

// read/write dirty_boot_flag
//
static int
proc_dirty_boot_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_dirty_boot_flag);
}

static int
proc_dirty_boot_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_dirty_boot_flag);
}

// read calibration_info
//
static int
proc_calibration_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    int len = sprintf(page, "%d, %ld, %ld\n", get_op_amp_offset(), get_gain_value(), get_rve_value());

    *eof = 1;

    return len;
}

// read/write panel_size
//
static int
proc_panel_size_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_panel_size);
}

static int
proc_panel_size_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_panel_size);
}

// read panel params
//
static char *
get_panel_params(
    void)
{
    char *result = NULL;
    
    switch ( get_panel_size() )
    {
        case PANEL_ID_6_0_INCH_SIZE:
        default:
            result = PANEL_ID_6_0_INCH_PARAMS;
        break;
        
        case PANEL_ID_9_7_INCH_SIZE:
            result = PANEL_ID_9_7_INCH_PARAMS;
        break;
    }
    
    return ( result );
}

static int
proc_panel_params_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    int len = sprintf(page, "%s\n", get_panel_params());

    *eof = 1;

    return len;
}

// read/write mw_flags
//
static int
proc_mw_flags_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    unsigned long raw_mw_flags =  *(unsigned long *)get_mw_flags();
    int len = sprintf(page, "%08lX\n", raw_mw_flags);

    *eof = 1;

    return len;
}

static int
proc_mw_flags_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    unsigned long raw_mw_flags;
    mw_flags_t mw_flags;
    char lbuf[16];

    memset(lbuf, 0, 16);

    if (copy_from_user(lbuf, buf, 8)) {
        return -EFAULT;
    }

    raw_mw_flags = (unsigned long)simple_strtol(lbuf, NULL, 0);
    memcpy(&mw_flags, &raw_mw_flags, sizeof(mw_flags_t));
    
    set_mw_flags(&mw_flags);

    return count;
}

// read/write rcs_flags
//
static int
proc_rcs_flags_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    unsigned long raw_rcs_flags =  *(unsigned long *)get_rcs_flags();
    int len = sprintf(page, "%08lX\n", raw_rcs_flags);

    *eof = 1;

    return len;
}

static int
proc_rcs_flags_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    unsigned long raw_rcs_flags;
    rcs_flags_t rcs_flags;
    char lbuf[16];

    memset(lbuf, 0, 16);

    if (copy_from_user(lbuf, buf, 8)) {
        return -EFAULT;
    }

    raw_rcs_flags = (unsigned long)simple_strtol(lbuf, NULL, 0);
    memcpy(&rcs_flags, &raw_rcs_flags, sizeof(rcs_flags_t));
    
    set_rcs_flags(&rcs_flags);

    return count;
}

// read/write progress_count
//
static int
proc_progress_count_read(
    char *page,
    char **start,
    off_t off,
    int count,
    int *eof,
    void *data)
{
    PROC_GET_BOOT_DATA(get_progress_bar_value);
}

static int
proc_progress_count_write(
    struct file *file,
    const char __user *buf,
    unsigned long count,
    void *data)
{
    PROC_SET_BOOT_DATA(set_progress_bar_value);
}

/* Current number of boot milestones. This starts initialized to 0 */
static unsigned int num_boot_milestones;

static rwlock_t boot_milestone_lock;

/** Read the boot milestones.
 *
 * @param buf		(out param) the buffer to put the boot milestones into.
 *			Must have at least PAGE_SIZE bytes.
 *
 * @return		Total number of bytes read, or a negative error code.
 */
static int boot_milestone_read(char *buf)
{
	char *b;
	unsigned int i;
	int total_read;

	/* We assume that we can fit all of our output into a single page.  If
	 * that turns out not to be true, this has to be rewritten in a more
	 * complex way.
	 */
	BUILD_BUG_ON(BOOT_MILESTONE_TO_STR_SZ * MAX_BOOT_MILESTONES > PAGE_SIZE);

	read_lock(&boot_milestone_lock);
	if (num_boot_milestones > MAX_BOOT_MILESTONES) {
		printk("error: too many boot milestones!\n");
		read_unlock(&boot_milestone_lock);
		return -EINVAL;
	}
	total_read = 0;
	for (i = 0, b = buf; i < num_boot_milestones; i++) {
		int res;
		res = boot_milestone_to_str(i, b);
		b += res;
		total_read += res;
	}
	read_unlock(&boot_milestone_lock);
	return total_read;
}

int boot_milestone_write(const char *name, unsigned long name_len)
{
	struct boot_milestone *bom;
	unsigned int ret, i;

	write_lock(&boot_milestone_lock);
	if (name_len != BOOT_MILESTONE_NAME_SZ) {
		printk(KERN_WARNING " W : milestone name must be exactly "
		       "%d alphanumeric bytes, not %ld\n",
		       BOOT_MILESTONE_NAME_SZ, name_len);
		ret = -EINVAL;
		goto done;
	}
	for (i = 0; i < BOOT_MILESTONE_NAME_SZ; i++) {
		if (! isalnum(name[i])) {
			printk(KERN_WARNING " W : won't accept "
				"non-alphanumeric milestone name "
				"0x%02x%02x%02x%02x\n",
				name[0], name[1], name[2], name[3]);
			ret = -EINVAL;
			goto done;
		}
	}
	if (! memcmp(name, "clrm", BOOT_MILESTONE_NAME_SZ)) {
		printk(KERN_NOTICE "clrm: cleared all boot milestones.\n");
		num_boot_milestones = 0;
		ret = 0;
		goto done;
	}
	if (num_boot_milestones >= MAX_BOOT_MILESTONES) {
		printk(KERN_WARNING " W : can't add another boot milestone. "
			"We already have %d boot milestones, and the maximum "
			"is %d\n", num_boot_milestones, MAX_BOOT_MILESTONES);
		ret = -EINVAL;
		goto done;
	}
	bom = get_boot_globals()->globals.boot_milestones + num_boot_milestones;
	num_boot_milestones++;
	memcpy(bom->name, name, BOOT_MILESTONE_NAME_SZ);
	bom->jiff = jiffies;
	ret = 0;

done:
	write_unlock(&boot_milestone_lock);
	return ret;
}

// read/write boot milestones
//
static int
proc_boot_milestone_read(char *page, char **start,
	off_t off, int count, int *eof, void *data)
{
	return boot_milestone_read(page);
}

static int
proc_boot_milestone_write(struct file *file, const char __user *buf,
	unsigned long count, void *data)
{
	int ret;
	ret = boot_milestone_write(buf, count);
	return ret ? ret : BOOT_MILESTONE_NAME_SZ;
}

// Entry/Exit
//
#define BD_PROC_MODE_PARENT    (S_IFDIR | S_IRUGO | S_IXUGO)
#define BD_PROC_MODE_CHILD_RW  (S_IWUGO | S_IRUGO)
#define BD_PROC_MODE_CHILD_R   (S_IRUGO)

#define BD_PROC_PARENT         "bd"
#define BD_PROC_UPDATE_FLAG    "update_flag"
#define BD_PROC_UPDATE_DATA    "update_data"
#define BD_PROC_DRIVEMODE      "drivemode_screen_ready"
#define BD_PROC_FRAMEWORK1     "framework_started"
#define BD_PROC_FRAMEWORK2     "framework_running"
#define BD_PROC_FRAMEWORK3     "framework_stopped"
#define BD_PROC_ENV_SCRIPT     "env_script"
#define BD_PROC_DIRTY_BOOT     "dirty_boot_flag"
#define BD_PROC_CALIBRATION    "calibration_info"
#define BD_PROC_PANEL_SIZE     "panel_size"
#define BD_PROC_PANEL_PARAMS   "panel_params"
#define BD_PROC_MW_FLAGS       "mw_flags"
#define BD_PROC_RCS_FLAGS      "rcs_flags"
#define BD_PROC_PROGRESS_COUNT "progress_count"
#define BD_PROC_BOOT_MILESTONE "boot_milestone"

static struct proc_dir_entry *proc_bd_parent      = NULL;
static struct proc_dir_entry *proc_update_flag    = NULL;
static struct proc_dir_entry *proc_update_data    = NULL;
static struct proc_dir_entry *proc_drivemode      = NULL;
static struct proc_dir_entry *proc_framework1     = NULL;
static struct proc_dir_entry *proc_framework2     = NULL;
static struct proc_dir_entry *proc_framework3     = NULL;
static struct proc_dir_entry *proc_env_script     = NULL;
static struct proc_dir_entry *proc_dirty_boot     = NULL;
static struct proc_dir_entry *proc_calibration    = NULL;
static struct proc_dir_entry *proc_panel_size     = NULL;
static struct proc_dir_entry *proc_panel_params   = NULL;
static struct proc_dir_entry *proc_mw_flags       = NULL;
static struct proc_dir_entry *proc_rcs_flags      = NULL;
static struct proc_dir_entry *proc_progress_count = NULL;
static struct proc_dir_entry *proc_boot_milestone = NULL;

static struct proc_dir_entry *create_bd_proc_entry(const char *name, mode_t mode, struct proc_dir_entry *parent,
    read_proc_t *read_proc, write_proc_t *write_proc)
{
    struct proc_dir_entry *bd_proc_entry = create_proc_entry(name, mode, parent);
    
    if (bd_proc_entry) {
        bd_proc_entry->data       = NULL;
        bd_proc_entry->read_proc  = read_proc;
        bd_proc_entry->write_proc = write_proc;
    }
    
    return bd_proc_entry;
}

#define remove_bd_proc_entry(name, entry, parent)   \
    do                                              \
    if ( entry )                                    \
    {                                               \
        remove_proc_entry(name, parent);            \
        entry = NULL;                               \
    }                                               \
    while ( 0 )

static void
bootdata_cleanup(
	void)
{
	if ( proc_bd_parent )
	{
	    remove_bd_proc_entry(BD_PROC_UPDATE_FLAG,    proc_update_flag,    proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_UPDATE_DATA,    proc_update_data,    proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_DRIVEMODE,      proc_drivemode,      proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_FRAMEWORK1,     proc_framework1,     proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_FRAMEWORK2,     proc_framework2,     proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_FRAMEWORK3,     proc_framework3,     proc_bd_parent);	    
	    remove_bd_proc_entry(BD_PROC_ENV_SCRIPT,     proc_env_script,     proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_DIRTY_BOOT,     proc_dirty_boot,     proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_CALIBRATION,    proc_calibration,    proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_PANEL_SIZE,     proc_panel_size,     proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_PANEL_PARAMS,   proc_panel_params,   proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_MW_FLAGS,       proc_mw_flags,       proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_RCS_FLAGS,      proc_rcs_flags,      proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_PROGRESS_COUNT, proc_progress_count, proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_BOOT_MILESTONE, proc_boot_milestone, proc_bd_parent);
	    remove_bd_proc_entry(BD_PROC_PARENT,         proc_bd_parent,      NULL);
	}
}

static int __init
bootdata_init(
	void)
{
    int result = -ENOMEM;

    // Parent:  /proc/bd.
    //
    proc_bd_parent = create_proc_entry(BD_PROC_PARENT, BD_PROC_MODE_PARENT, NULL);
    
    if (proc_bd_parent)
    {
        int null_check = -1;
        
        // Child:   /proc/bd/update_flag.
        //
        proc_update_flag = create_bd_proc_entry(BD_PROC_UPDATE_FLAG, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_update_flag_read, proc_update_flag_write);
        
        null_check &= (int)proc_update_flag;

        // Child:   /proc/bd/update_data.
        //
        proc_update_data = create_bd_proc_entry(BD_PROC_UPDATE_DATA, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_update_data_read, proc_update_data_write);
            
        null_check &= (int)proc_update_data;

        // Child:   /proc/bd/drivemode_screen_ready.
        //
        proc_drivemode = create_bd_proc_entry(BD_PROC_DRIVEMODE, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_drivemode_screen_ready_read, proc_drivemode_screen_ready_write);
            
        null_check &= (int)proc_drivemode;
        
        // Child:   /proc/bd/framework_started.
        //
        proc_framework1 = create_bd_proc_entry(BD_PROC_FRAMEWORK1, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_framework_started_read, proc_framework_started_write);
            
        null_check &= (int)proc_framework1;
        
        // Child:   /proc/bd/framework_running.
        //
        proc_framework2 = create_bd_proc_entry(BD_PROC_FRAMEWORK2, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_framework_running_read, proc_framework_running_write);
            
        null_check &= (int)proc_framework2;
        
        // Child:   /proc/bd/framework_stopped.
        //
        proc_framework3 = create_bd_proc_entry(BD_PROC_FRAMEWORK3, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_framework_stopped_read, proc_framework_stopped_write);
            
        null_check &= (int)proc_framework3;
        
        // Child:   /proc/bd/env_script.
        //
        proc_env_script = create_bd_proc_entry(BD_PROC_ENV_SCRIPT, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_env_script_read, proc_env_script_write);
            
        null_check &= (int)proc_env_script;

        // Child:   /proc/bd/dirty_boot_flag.
        //
        proc_dirty_boot = create_bd_proc_entry(BD_PROC_DIRTY_BOOT, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_dirty_boot_read, proc_dirty_boot_write);

        null_check &= (int)proc_dirty_boot;
        
        // Child:   /proc/bd/calibration_info.
        //
        proc_calibration = create_bd_proc_entry(BD_PROC_CALIBRATION, BD_PROC_MODE_CHILD_R, proc_bd_parent,
            proc_calibration_read, NULL);
        
        null_check &= (int)proc_calibration;
        
        // Child:   /proc/bd/panel_size.
        //
        proc_panel_size = create_bd_proc_entry(BD_PROC_PANEL_SIZE, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_panel_size_read, proc_panel_size_write);
        
        null_check &= (int)proc_panel_size;
        
        // Child:   /proc/bd/panel_params.
        //
        proc_panel_params = create_bd_proc_entry(BD_PROC_PANEL_PARAMS, BD_PROC_MODE_CHILD_R, proc_bd_parent,
            proc_panel_params_read, NULL);
        
        null_check &= (int)proc_panel_params;
        
        // Child:   /proc/bd/mw_flags.
        //
        proc_mw_flags = create_bd_proc_entry(BD_PROC_MW_FLAGS, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_mw_flags_read, proc_mw_flags_write);

        null_check &= (int)proc_mw_flags;

        // Child:   /proc/bd/rcs_flags.
        //
        proc_rcs_flags = create_bd_proc_entry(BD_PROC_RCS_FLAGS, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_rcs_flags_read, proc_rcs_flags_write);
            
        null_check &= (int)proc_rcs_flags;

        // Child:   /proc/bd/progress_count.
        //
        proc_progress_count = create_bd_proc_entry(BD_PROC_PROGRESS_COUNT, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_progress_count_read, proc_progress_count_write);

        null_check &= (int)proc_progress_count;

        // Child:   /proc/bd/boot_milestone.
        //
        proc_boot_milestone = create_bd_proc_entry(BD_PROC_BOOT_MILESTONE, BD_PROC_MODE_CHILD_RW, proc_bd_parent,
            proc_boot_milestone_read, proc_boot_milestone_write);

        null_check &= (int)proc_boot_milestone;
	num_boot_milestones = 0;
	rwlock_init(&boot_milestone_lock);

        // Success? 
        //
        if ( 0 == null_check )
            bootdata_cleanup();
        else
            result = 0;
    }

    // Done.
    //
	return ( result );
}

static void __exit
bootdata_exit(
	void)
{
    bootdata_cleanup();
}

module_init(bootdata_init);
module_exit(bootdata_exit);

EXPORT_SYMBOL(boot_milestone_write);

MODULE_DESCRIPTION("bootdata driver");
MODULE_AUTHOR("Lab126");
MODULE_LICENSE("Proprietary");
