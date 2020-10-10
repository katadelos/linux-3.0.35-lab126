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
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/version.h>

#include <asm/bitops.h>

static wait_queue_head_t *storage_wait_queue = NULL;
static int *wake_up_cond = NULL;
void set_falcon_callback(void (*func)(void));

static void falcon_wakeup_task(void);
void falcon_delayed_wakeup(void);

static unsigned long need_wakeup = 0;

void init_storage_common(void)
{
}

static void falcon_wakeup_task(void)
{
#ifdef CONFIG_FALCON_BLK
	if (strcmp("falcon_blkd", current->comm) == 0)
		return;
#endif
#ifdef CONFIG_FALCON_MTD_NAND
#define FALCON_MTD_NAND_COMM "mtdblock"

	if (strncmp(FALCON_MTD_NAND_COMM, current->comm, strlen(FALCON_MTD_NAND_COMM)) == 0)
		return;
#endif

	if (!storage_wait_queue || !wake_up_cond)
		return;

	if(*wake_up_cond == 1)
		return;

	if(preempt_count()) {

		test_and_set_bit(0, &need_wakeup);
	}
	else {
		*wake_up_cond = 1;
		wake_up(storage_wait_queue);
	}
}

void falcon_delayed_wakeup(void)
{
	if(test_and_clear_bit(0, &need_wakeup) == 0)
		return;

	BUG_ON(!storage_wait_queue || !wake_up_cond);

	*wake_up_cond = 1;
	wake_up(storage_wait_queue);
}

void falcon_set_wait_queue(wait_queue_head_t *q, int *cond)
{
	storage_wait_queue = q;
	wake_up_cond = cond;

	if (q && cond) {
		set_falcon_callback(falcon_wakeup_task);
	} else {
		set_falcon_callback(NULL);
	}
}

int falcon_storage_suspend(void)
{
	return 0;
}

void falcon_storage_resume(void)
{
	if (storage_wait_queue != NULL) {
		wake_up(storage_wait_queue);
	}
	need_wakeup = 0;
}
