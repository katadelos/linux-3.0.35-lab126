#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>

#define RESET_DELAY			3000

struct proc_dir_entry *usb_otg_proc_entry;

extern int audio_enumerated;
extern int max77696_charger_set_otg(int enable);
extern bool asession_enable(bool connected);
extern int max77696_uic_is_otg_connected(void);

void setup_usb_otg(void) {
    audio_enumerated = 1;
    asession_enable(false);
	max77696_charger_set_otg(0);
	msleep(RESET_DELAY);
	max77696_charger_set_otg(1);
	asession_enable(true);
}

void disable_usb_otg(void) {
    audio_enumerated = 0;
	max77696_charger_set_otg(0);
	asession_enable(false);
}

int usb_otg_write(struct file *file, const char __user *buffer, unsigned long count, void *data) {
	
	int detect = 99, param = 0;
	sscanf(buffer, "%d %d", &detect, &param);
	switch(detect)
	{
	case 0:
		printk("Disabling USB OTG...\n");
		disable_usb_otg();
		break;
	case 1:
		printk("Enabling USB OTG...\n");
		setup_usb_otg();
		break;
	case 2:
		printk("Setting audio_enumerated to 1...\n");
		audio_enumerated = 1;
		break;
	case 3:
		printk("Setting audio_enumerated to 0...\n");
		audio_enumerated = 0;
		break;
	default:
		break;
	}
	return count;
}

static int usb_otg_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data) {
	int len;
	len = sprintf(page, "max77696_uic_is_otg_connected() %d\n"
						"audio_enumerated %d\n"
						 ,
				max77696_uic_is_otg_connected() ,
				audio_enumerated);
	return len;
}

void setup_usb_otg_proc(void) {
	usb_otg_proc_entry = create_proc_entry("usb_otg", S_IRUGO, NULL);
	if (usb_otg_proc_entry) {
		usb_otg_proc_entry->read_proc = usb_otg_proc_read;
		usb_otg_proc_entry->write_proc = usb_otg_write;
		printk("procfs entry available at /proc/usb_otg \n");
	} else {
		printk(KERN_ERR "Failed to initialize usb_otg proc entry\n");
	}
}

int __init otg_init(void) {
	printk("Kindle USB OTG Control for iMX6 (v0.1)\n");
	setup_usb_otg_proc();
	return 0;
}
 
void __exit otg_exit(void) {
	if (usb_otg_proc_entry)
		remove_proc_entry("usb_otg", NULL);
	printk("Removing Kindle USB OTG Control...\n");
}
 
module_init(otg_init);
module_exit(otg_exit);

MODULE_AUTHOR("katadelos");
MODULE_DESCRIPTION("Kindle USB OTG Control for iMX6");
MODULE_LICENSE("GPL");
