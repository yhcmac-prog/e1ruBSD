#!/usr/bin/env python3
"""mkimage.py - assemble the e1ruBSD raw disk image.

Layout (sector = 512B):
  LBA 0          bootsect.bin (MBR, 0xAA55)
  LBA 1..62      stage2.bin, patched with kernel/ramdisk geometry
  LBA 64..       kernel.bin (ELF -> flat binary, loaded to 0x100000)
  LBA 2048..     ramdisk.tar (loaded to 0x400000)

stage2 patch offsets (see sys/arch/amd64/boot/stage2.S):
  +0x04 kernel_sectors   +0x08 rd_lba   +0x0C rd_sectors
"""

import struct
import sys
import os

SECTOR = 512
STAGE2_SECTORS = 62
KERNEL_LBA = 64
RAMDISK_LBA = 2048
TOTAL_SECTORS = 32768                # 16MB


def read(path):
    with open(path, "rb") as f:
        return f.read()


def main(output):
    bootsect = read("build/bootsect.bin")
    stage2 = bytearray(read("build/stage2.bin"))
    kernel = read("build/kernel.bin")
    ramdisk = read("build/ramdisk.tar")

    if len(bootsect) != SECTOR:
        sys.exit(f"bootsect is {len(bootsect)} bytes, must be 512")
    if len(stage2) > STAGE2_SECTORS * SECTOR:
        sys.exit("stage2 too large")

    kernel_sectors = (len(kernel) + SECTOR - 1) // SECTOR
    rd_sectors = (len(ramdisk) + SECTOR - 1) // SECTOR
    if KERNEL_LBA + kernel_sectors > RAMDISK_LBA:
        sys.exit("kernel overlaps ramdisk area")
    if RAMDISK_LBA + rd_sectors > TOTAL_SECTORS:
        sys.exit("ramdisk exceeds image")

    struct.pack_into("<I", stage2, 4, kernel_sectors)
    struct.pack_into("<I", stage2, 8, RAMDISK_LBA)
    struct.pack_into("<I", stage2, 12, rd_sectors)

    img = bytearray(TOTAL_SECTORS * SECTOR)
    img[0:SECTOR] = bootsect
    pos = SECTOR
    img[pos:pos + len(stage2)] = stage2
    pos = KERNEL_LBA * SECTOR
    img[pos:pos + len(kernel)] = kernel
    pos = RAMDISK_LBA * SECTOR
    img[pos:pos + len(ramdisk)] = ramdisk

    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    with open(output, "wb") as f:
        f.write(img)
    print(f"mkimage: {output} kernel={kernel_sectors} sect, "
          f"ramdisk={rd_sectors} sect ({len(img)} bytes)")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         "build/e1ruBSD-0.1-amd64.img")
