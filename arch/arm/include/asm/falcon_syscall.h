/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _FALCON_SYSCALL_H
#define _FALCON_SYSCALL_H

#ifdef __KERNEL__

#define bios_svc(num, arg0, arg1, arg2, arg3, arg4)			\
	_illegal_inst(0xf7f9b0f0 | (((num) & 0xf0) << 4) | ((num) & 0xf), \
		      arg0, arg1, arg2, arg3, arg4)
#define sbios_svc(num, arg0, arg1, arg2, arg3, arg4)				\
	_illegal_inst(0xf7f9bff0 | (((num) & 0xf0) << 4) | ((num) & 0xf), \
				  arg0, arg1, arg2, arg3, arg4)
#define _illegal_inst(num, arg0, arg1, arg2, arg3, arg4)		\
	({								\
		int ret;						\
		__asm__ __volatile__(					\
			"mov r0, %1\n"					\
			"mov r1, %2\n"					\
			"mov r2, %3\n"					\
			"mov r3, %4\n"					\
			"mov r4, %5\n"					\
			".word "#num"\n"				\
			"mov %0, r0\n"					\
			:"=r"(ret)					\
			:"r"(arg0), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4)	\
			:"r0", "r1", "r2", "r3", "r4", "cc");		\
		ret;})

int can_use_falcon(void);

#endif

#endif
