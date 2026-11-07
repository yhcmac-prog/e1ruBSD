# e1ruBSD stage2 loader.
# Loaded at 0x7E00 by bootsect. Runs in real mode, then transitions to long mode.
# VGA stays in 80x25 text mode: the kernel console writes 0xB8000 directly,
# mirroring output to COM1 for headless testing.
#
# Memory map used by this loader:
#   0x5000  E820 entries (24B each), count stored in bootinfo
#   0x6000  bootinfo struct (32B, passed to kernel in RDI)
#   0x7C00  bootsect / initial stack
#   0x7E00  this stage2 (up to 0xFAFF)
#   0x10000 kernel staging area (up to 0x40000, 192KB max)
#   0x40000 ramdisk staging area (up to 0x9C000, 368KB max)
#   0x8C000 PML4 / 0x8D000 PDPT / 0x8E000-0x91FFF PDx4 (identity map 4GB, 2MB pages)
#   0x100000   kernel final location
#   0x400000   ramdisk final location
#
# bootinfo layout (0x6000):
#   +0x00 u32 magic 'e1ru' = 0x75723165
#   +0x04 u32 kernel_size (bytes)
#   +0x08 u32 e820_count
#   +0x0C u32 boot_drive
#   +0x10 u32 ramdisk_pa   (physical)
#   +0x14 u32 ramdisk_size (bytes)
#   +0x18 u32 reserved[2]
#
# File layout patched by mkimage.py:
#   +0x04 u32 kernel_sectors
#   +0x08 u32 rd_lba
#   +0x0C u32 rd_sectors

.intel_syntax noprefix
.code16
.section .text
.global stage2_start

stage2_start:
    jmp stage2_main
    .byte 0, 0
.global kernel_sectors
kernel_sectors: .long 0          # offset 4, patched by mkimage.py
.global rd_lba
rd_lba:         .long 0          # offset 8, patched by mkimage.py
.global rd_sectors
rd_sectors:     .long 0          # offset 12, patched by mkimage.py

stage2_main:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    mov [boot_drive_save], dl

    # ---------------- E820 memory map ----------------
    mov di, 0x5000
    xor ebx, ebx
    xor bp, bp                    # entry count
e820_loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150           # 'SMAP'
    int 0x15
    jc e820_done
    cmp eax, 0x534D4150
    jne e820_done
    inc bp
    add di, 24
    test ebx, ebx
    jnz e820_loop
e820_done:
    mov [e820_count], bp

    # ---------------- load kernel to 0x10000 ----------------
    mov ax, 0x1000
    mov es, ax                    # dest segment
    mov ebx, 64                   # start LBA
    mov ebp, [kernel_sectors]     # remaining sectors
    call load_chunks
    jc load_error
    xor ax, ax
    mov es, ax

    # ---------------- load ramdisk to 0x40000 ----------------
    mov ax, 0x4000
    mov es, ax                    # dest segment
    mov ebx, [rd_lba]             # start LBA
    mov ebp, [rd_sectors]         # remaining sectors
    call load_chunks
    jc load_error
    xor ax, ax
    mov es, ax

    # ---------------- enable A20 ----------------
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    # ---------------- build page tables ----------------
    # Bootstrap tables live at 0x8C000-0x91FFF, above the ramdisk staging
    # area (0x40000-0x80800) and still below the 640KB BIOS limit.
    # zero 0x8C000-0x91FFF (6 pages)
    mov ax, 0x8C00
    mov es, ax
    xor di, di
    xor eax, eax
    mov cx, 6*1024                # 6 pages * 4KB / 4 = 6144 dwords
    rep stosd

    # PML4[0] -> PDPT
    mov di, 0x0000
    mov dword ptr es:[di],   0x8D003
    mov dword ptr es:[di+4], 0
    # PDPT[i] -> PD_i (i=0..3)
    mov di, 0x1000
    mov eax, 0x8E003
    mov cx, 4
pdpt_fill:
    mov es:[di], eax
    mov dword ptr es:[di+4], 0
    add eax, 0x1000
    add di, 8
    loop pdpt_fill
    # PDs: 4 * 512 entries of 2MB pages
    mov di, 0x2000
    mov eax, 0x83                 # present | rw | PS
    mov cx, 4*512
pd_fill:
    mov es:[di], eax
    mov dword ptr es:[di+4], 0
    add eax, 0x200000
    add di, 8
    loop pd_fill

    # ---------------- enter long mode ----------------
    cli
    lgdt [gdt_ptr]

    mov eax, cr4
    or eax, 1 << 5                # CR4.PAE
    mov cr4, eax

    mov eax, 0x8C000
    mov cr3, eax

    mov ecx, 0xC0000080           # EFER
    rdmsr
    or eax, 1 << 8                # LME
    wrmsr

    mov eax, cr0
    or eax, 0x80000001            # PG | PE
    mov cr0, eax

    .att_syntax prefix
    ljmp $0x08, $long_mode_start
    .intel_syntax noprefix

load_error:
    xor ax, ax
    mov es, ax
    lea si, [err_load]
    call print16s2
    cli
    hlt

err_load:       .asciz "e1ruBSD: kernel/ramdisk load error\r\n"
boot_drive_save: .byte 0
e820_count:     .word 0

# load_chunks: ES:0 = dest segment base, EBX = start LBA, EBP = sector count.
# Reads in 64-sector chunks via INT13 AH=42. Clobbers EAX ECX EBP EBX ESI.
# CF set on error.
load_chunks:
    test ebp, ebp
    jz load_done
    mov ecx, 64                   # chunk = 64 sectors (32KB)
    cmp ebp, 64
    jge chunk_ok
    mov ecx, ebp
chunk_ok:
    mov [dap2+2], cx              # count
    mov ax, es
    mov [dap2+6], ax              # segment
    mov [dap2+8], ebx             # lba low
    push es
    xor ax, ax
    mov es, ax
    mov ah, 0x42
    mov dl, [boot_drive_save]
    lea si, [dap2]
    int 0x13
    pop es
    jc load_fail
    movzx eax, word ptr [dap2+2]
    add ebx, eax                  # lba += count
    sub ebp, eax                  # remaining -= count
    shl eax, 5                    # count*512/16 = paragraphs
    mov cx, es
    add cx, ax
    mov es, cx
    jmp load_chunks
load_fail:
    stc
load_done:
    ret

print16s2:
    lodsb
    or al, al
    jz 1f
    mov ah, 0x0E
    int 0x10
    jmp print16s2
1:  ret

.align 4
dap2:
    .byte 0x10
    .byte 0
    .word 0           # +2 count
    .word 0           # +4 offset
    .word 0           # +6 segment
    .quad 0           # +8 lba

# GDT: null / code64 / data64
.align 8
gdt:
    .quad 0
    .quad 0x00AF9A000000FFFF      # code64: L=1
    .quad 0x00CF92000000FFFF      # data64
gdt_ptr:
    .word 23
    .long gdt

.code64
long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    # ---- write bootinfo at 0x6000 ----
    mov dword ptr [0x6000], 0x75723165    # magic 'e1ru'
    mov eax, [kernel_sectors]
    shl eax, 9
    mov [0x6004], eax                     # kernel_size
    movzx eax, word ptr [e820_count]
    mov [0x6008], eax                     # e820_count
    movzx eax, byte ptr [boot_drive_save]
    mov [0x600C], eax                     # boot_drive
    mov dword ptr [0x6010], 0x400000      # ramdisk_pa
    mov eax, [rd_sectors]
    shl eax, 9
    mov [0x6014], eax                     # ramdisk_size
    mov dword ptr [0x6018], 0
    mov dword ptr [0x601C], 0

    # ---- copy kernel 0x10000 -> 0x100000 ----
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, [kernel_sectors]
    shl ecx, 9
    rep movsb

    # ---- copy ramdisk 0x40000 -> 0x400000 ----
    mov esi, 0x40000
    mov edi, 0x400000
    mov ecx, [rd_sectors]
    shl ecx, 9
    rep movsb

    # ---- jump to kernel, RDI = bootinfo ----
    mov edi, 0x6000
    mov eax, 0x100000
    jmp rax
