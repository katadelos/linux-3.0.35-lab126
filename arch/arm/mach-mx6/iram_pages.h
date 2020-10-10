#ifndef __IRAM__PAGES__
#define __IRAM__PAGES__

#ifdef __ASSEMBLY__

/* Attributes of pages accessed in iram
 * Fixed control register virtual mappings are same as in mach/mx6.h
 * At initialization time these attributes are collected and printed
 * by bus_freq.c initialization code, they should be same as in this file
 */
#define IRAM_BASE_VA 
#define IRAM_BASE_PA 0x902043
#define IRAM_BASE_ATTR 0x2
#define IRAM_BASE_PA_PLUS_4K 0x902843

#define IOMUXC_VA 0xf40e0200
#define IOMUXC_PA  0x20110c3
#define IOMUXC_ATTR 0x149

#define MMDCP0_VA 0xf41b0200
#define MMDCP0_PA 0x21110c3
#define MMDCP0_ATTR 0x149

#define L2_VA 0xf2a02200
#define L2_PA 0xa02043
#define L2_ATTR 0x149

#define CCM_VA 0xf40c4200
#define CCM_PA 0x20110c3
#define CCM_ATTR 0x149


#define ANATOP_VA 0xf40c8200
#define ANATOP_PA 0x20110c3
#define ANATOP_ATTR 0x149

/*
 * Lock TLB entry at idx and set it with va,pa.attr properties
 * tmp is scratch register
 * Locking API is based on Cortex A-9 page  4-39
 */
.macro tlb_entry_in_idx tmp va pa attr  idx

	mov \tmp, #\idx
	mcr p15,5,\tmp,c15,c4,4 //TLB idx for write

	//va
	ldr \tmp, =\va
	mcr p15,0,\tmp,c8,c7,1 //invalidate the entry va unified tlb
	mcr p15,0,\tmp,c8,c6,1 //invalidate the entry va D tlb
	mcr p15,0,\tmp,c8,c5,1 //invalidate the entry va I tlb B3-138 Arm V7
	mcr p15,5,\tmp,c15,c5,2 // va in

	//attr
	ldr \tmp, =\attr
	mcr p15,5,\tmp,c15,c7,2 //attr

	//pa to write last to commit the entry
	ldr \tmp, =\pa
	mcr p15,5,\tmp,c15,c6,2 //pa

	dsb
	isb

  .endm

/*Unlock TLB entry at idx*/
  .macro tlb_entry_unlock_idx tmp idx

	mov \tmp, #\idx
	mcr p15,5,\tmp,c15,c4,4 //TLB idx for read
	mov \tmp, #0
	mcr p15,5,\tmp,c15,c6,2 //read pa

	dsb
	isb

  .endm
		
#endif //__ASSEMBLY__
#endif // __IRAM__PAGES__
