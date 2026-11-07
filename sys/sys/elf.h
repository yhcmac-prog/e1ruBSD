/*	$e1ruBSD: elf.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_ELF_H_
#define _SYS_ELF_H_

#include <sys/types.h>

typedef uint64_t	Elf64_Addr;
typedef uint64_t	Elf64_Off;
typedef uint16_t	Elf64_Half;
typedef uint32_t	Elf64_Word;
typedef uint64_t	Elf64_Xword;

#define	ELFMAG0		0x7f
#define	ELFCLASS64	2
#define	ELFDATA2LSB	1
#define	PT_LOAD		1

struct elf64_ehdr {
	uint8_t		e_ident[16];
	Elf64_Half	e_type;
	Elf64_Half	e_machine;
	Elf64_Word	e_version;
	Elf64_Addr	e_entry;
	Elf64_Off	e_phoff;
	Elf64_Off	e_shoff;
	Elf64_Word	e_flags;
	Elf64_Half	e_ehsize;
	Elf64_Half	e_phentsize;
	Elf64_Half	e_phnum;
	Elf64_Half	e_shentsize;
	Elf64_Half	e_shnum;
	Elf64_Half	e_shstrndx;
};

struct elf64_phdr {
	Elf64_Word	p_type;
	Elf64_Word	p_flags;
	Elf64_Off	p_offset;
	Elf64_Addr	p_vaddr;
	Elf64_Addr	p_paddr;
	Elf64_Xword	p_filesz;
	Elf64_Xword	p_memsz;
	Elf64_Xword	p_align;
};

#endif	/* !_SYS_ELF_H_ */
