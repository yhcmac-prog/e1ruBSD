/*	$e1ruBSD: bootinfo.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Information passed from stage2 to the kernel at physical 0x6000.
 * Layout must match sys/arch/amd64/boot/stage2.S.
 */

#ifndef _SYS_BOOTINFO_H_
#define _SYS_BOOTINFO_H_

#include <sys/types.h>

#define	BI_MAGIC	0x75723165UL		/* 'e1ru' */
#define	BI_E820_PA	0x5000UL		/* E820 entries, 24B each */

#define	E820_MAX	32

struct bios_e820 {
	uint64_t	base;
	uint64_t	len;
	uint32_t	type;
	uint32_t	pad;
} __packed;

#define	E820_RAM	1

struct bootinfo {
	uint32_t	bi_magic;
	uint32_t	bi_kernel_size;		/* bytes */
	uint32_t	bi_e820_count;
	uint32_t	bi_boot_drive;
	uint32_t	bi_ramdisk_pa;
	uint32_t	bi_ramdisk_size;
	uint32_t	bi_pad[2];
};

#endif	/* !_SYS_BOOTINFO_H_ */
