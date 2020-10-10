/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef _FALCON_LOCK_H
#define _FALCON_LOCK_H

#include <asm/falcon_syscall.h>

#ifdef __KERNEL__

#define FALCON_LOCK_GIC 0

#define FALCON_SLOCK_OCACHE 0

#if defined(CONFIG_SMP) && defined(CONFIG_FALCON)
#define falcon_spin_lock(id, lock) ({					\
	if (can_use_falcon()) {						\
		bios_svc(0x28, 0, id, 0, 0, 0);						\
	} else {							\
		spin_lock(lock);					\
	}})

#define falcon_spin_unlock(id, lock) ({					\
	if (can_use_falcon()) {						\
		bios_svc(0x28, 1, id, 0, 0, 0);					\
	} else {							\
		spin_unlock(lock);					\
	}})

#define falcon_spin_lock_irqsave(id, lock, flags) ({			\
	if (can_use_falcon()) {						\
		preempt_disable(); local_irq_save(flags); bios_svc(0x28, 0, id, 0, 0, 0); \
	} else {							\
		spin_lock_irqsave(lock, flags);				\
	}})

#define falcon_spin_unlock_irqrestore(id, lock, flags) ({		\
	if (can_use_falcon()) {						\
		bios_svc(0x28, 1, id, 0, 0, 0); local_irq_restore(flags); preempt_enable(); \
	} else {							\
		spin_unlock_irqrestore(lock, flags);			\
	}})

#define falcon_raw_spin_lock(id, lock) ({				\
	if (can_use_falcon()) {						\
		bios_svc(0x28, 0, id, 0, 0, 0);						\
	} else {							\
		raw_spin_lock(lock);					\
	}})

#define falcon_raw_spin_unlock(id, lock) ({				\
	if (can_use_falcon()) {						\
		bios_svc(0x28, 1, id, 0, 0, 0);					\
	} else {							\
		raw_spin_unlock(lock);					\
	}})

#define falcon_raw_spin_lock_irqsave(id, lock, flags) ({		\
	if (can_use_falcon()) {						\
		preempt_disable(); local_irq_save(flags); bios_svc(0x28, 0, id, 0, 0, 0); \
	} else {							\
		raw_spin_lock_irqsave(lock, flags);			\
	}})

#define falcon_raw_spin_unlock_irqrestore(id, lock, flags) ({		\
	if (can_use_falcon()) {						\
		bios_svc(0x28, 1, id, 0, 0, 0); local_irq_restore(flags); preempt_enable(); \
	} else {							\
		raw_spin_unlock_irqrestore(lock, flags);		\
	}})

#define falcon_s_spin_lock(id, lock, flags) ({				\
	if (can_use_falcon()) {						\
		preempt_disable(); local_irq_save(flags); sbios_svc(0x8, 0, id, 0, 0, 0); \
	} else {							\
		spin_lock(lock);					\
	}})

#define falcon_s_spin_unlock(id, lock, flags) ({			\
	if (can_use_falcon()) {						\
		sbios_svc(0x8, 1, id, 0, 0, 0); local_irq_restore(flags); preempt_enable(); \
	} else {							\
		spin_unlock(lock);					\
	}})

#define falcon_s_spin_lock_irqsave(id, lock, flags) ({			\
	if (can_use_falcon()) {						\
		preempt_disable(); local_irq_save(flags); sbios_svc(0x8, 0, id, 0, 0, 0); \
	} else {							\
		spin_lock_irqsave(lock, flags);				\
	}})

#define falcon_s_spin_unlock_irqrestore(id, lock, flags) ({		\
	if (can_use_falcon()) {						\
		sbios_svc(0x8, 1, id, 0, 0, 0); local_irq_restore(flags); preempt_enable(); \
	} else {							\
		spin_unlock_irqrestore(lock, flags);			\
	}})

#define falcon_s_raw_spin_lock(id, lock, flags) ({			\
	if (can_use_falcon()) {						\
		preempt_disable(); local_irq_save(flags); sbios_svc(0x8, 0, id, 0, 0, 0); \
	} else {							\
		raw_spin_lock(lock);					\
	}})

#define falcon_s_raw_spin_unlock(id, lock, flags) ({			\
	if (can_use_falcon()) {						\
		sbios_svc(0x8, 1, id, 0, 0, 0); local_irq_restore(flags); preempt_enable(); \
	} else {							\
		raw_spin_unlock(lock);					\
	}})

#define falcon_s_raw_spin_lock_irqsave(id, lock, flags) ({		\
	if (can_use_falcon()) {						\
		preempt_disable(); local_irq_save(flags); sbios_svc(0x8, 0, id, 0, 0, 0); \
	} else {							\
		raw_spin_lock_irqsave(lock, flags);			\
	}})

#define falcon_s_raw_spin_unlock_irqrestore(id, lock, flags) ({		\
	if (can_use_falcon()) {						\
		sbios_svc(0x8, 1, id, 0, 0, 0); local_irq_restore(flags); preempt_enable(); \
	} else {							\
		raw_spin_unlock_irqrestore(lock, flags);		\
	}})
#else
#define falcon_spin_lock(id, lock) \
	({spin_lock(lock);})

#define falcon_spin_unlock(id, lock) \
	({spin_unlock(lock);})

#define falcon_spin_lock_irqsave(id, lock, flags) \
	({spin_lock_irqsave(lock, flags);})

#define falcon_spin_unlock_irqrestore(id, lock, flags) \
	({spin_unlock_irqrestore(lock, flags);})

#define falcon_raw_spin_lock(id, lock) \
	({raw_spin_lock(lock);})

#define falcon_raw_spin_unlock(id, lock) \
	({raw_spin_unlock(lock);})

#define falcon_raw_spin_lock_irqsave(id, lock, flags) \
	({raw_spin_lock_irqsave(lock, flags);})

#define falcon_raw_spin_unlock_irqrestore(id, lock, flags) \
	({raw_spin_unlock_irqrestore(lock, flags);})

#define falcon_s_spin_lock(id, lock, flags) \
	({spin_lock(lock);})

#define falcon_s_spin_unlock(id, lock, flags) \
	({spin_unlock(lock);})

#define falcon_s_spin_lock_irqsave(id, lock, flags) \
	({spin_lock_irqsave(lock, flags);})

#define falcon_s_spin_unlock_irqrestore(id, lock, flags) \
	({spin_unlock_irqrestore(lock, flags);})

#define falcon_s_raw_spin_lock(id, lock, flags) \
	({raw_spin_lock(lock);})

#define falcon_s_raw_spin_unlock(id, lock, flags) \
	({raw_spin_unlock(lock);})

#define falcon_s_raw_spin_lock_irqsave(id, lock, flags) \
	({raw_spin_lock_irqsave(lock, flags);})

#define falcon_s_raw_spin_unlock_irqrestore(id, lock, flags) \
	({raw_spin_unlock_irqrestore(lock, flags);})
#endif

#endif

#endif
