#!/bin/sh
# build.sh - build the e1ruBSD disk image.
# Output: build/e1ruBSD-0.1-amd64.img
set -eu
cd "$(dirname "$0")"

ZIG=${ZIG:-/Users/macbook/Desktop/os/e1LibreOS/downloads/zig/zig}
CC=${CC:-cc}
mkdir -p build/obj build/bin

# Compile with the host C compiler (clang), link with zig's bundled ld.lld.
# Going through "zig cc <src> -o <exe>" makes zig build compiler_rt for
# every link; this kernel and userland are freestanding and never use it.
BCFLAGS="-target x86_64-unknown-none-elf -nostdlib"

# ---------------- boot sectors ----------------
echo "== [1/5] boot sectors =="
$CC $BCFLAGS -c sys/arch/amd64/boot/bootsect.S -o build/bootsect.o
$ZIG ld.lld -m elf_x86_64 -T sys/arch/amd64/boot/bootsect.ld \
    build/bootsect.o -o build/bootsect.elf
$ZIG objcopy -O binary build/bootsect.elf build/bootsect.bin
$CC $BCFLAGS -c sys/arch/amd64/boot/stage2.S -o build/stage2.o
$ZIG ld.lld -m elf_x86_64 -T sys/arch/amd64/boot/stage2.ld \
    build/stage2.o -o build/stage2.elf
$ZIG objcopy -O binary build/stage2.elf build/stage2.bin

# ---------------- kernel ----------------
# -mno-red-zone: interrupts may use the kernel stack below rsp
# -mcmodel=kernel -fno-pic: higher-half static kernel
# -mno-sse -mno-mmx -mno-80387: no FPU use inside the kernel
echo "== [2/5] kernel =="
KCFLAGS="-target x86_64-unknown-none-elf -ffreestanding -nostdlib \
 -fno-pic -fno-pie -fno-stack-protector -fno-tree-vectorize \
 -mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx -mno-80387 -O1 -std=c99 -Wall -Wextra \
 -Wno-unused-parameter -I. -Isys -Isys/arch/amd64 -Isys/arch/amd64/include"

KOBJ=""
for src in \
    sys/arch/amd64/amd64/locore.S \
    sys/kern/init_main.c sys/kern/sched.c sys/kern/subr_prf.c \
    sys/kern/subr_malloc.c sys/kern/syscalls.c sys/kern/vfs_subr.c \
    sys/kern/exec_elf.c sys/kern/subr_tar.c \
    sys/dev/cons.c sys/dev/pckbd.c sys/dev/clock.c sys/dev/md.c \
    sys/dev/acpi.c sys/dev/rtc.c sys/dev/autoconf.c \
    sys/fs/ramfs.c \
    sys/libkern/libkern.c \
    sys/arch/amd64/amd64/cpu.c sys/arch/amd64/amd64/pmap.c \
    sys/arch/amd64/amd64/trap.c; do
    obj="build/obj/$(echo "$src" | tr / _ | sed 's/\.[cS]$/.o/')"
    $CC $KCFLAGS -c "$src" -o "$obj"
    KOBJ="$KOBJ $obj"
done

$ZIG ld.lld -m elf_x86_64 -T sys/kern.ld $KOBJ -o build/kernel.elf
$ZIG objcopy -O binary build/kernel.elf build/kernel.bin
KERNEL_SECTORS=$(( ($(stat -f%z build/kernel.bin) + 511) / 512 ))
echo "kernel: $(stat -f%z build/kernel.bin) bytes ($KERNEL_SECTORS sectors)"

# ---------------- userland ----------------
echo "== [3/5] userland =="
UCFLAGS="-target x86_64-unknown-none-elf -ffreestanding -nostdlib \
 -fno-pic -fno-stack-protector -O1 -std=c99 -Wall \
 -Wno-unused-parameter -I. -Isys -Ilib/libc"
ULIBC="lib/libc/start.c lib/libc/sys.c lib/libc/string.c lib/libc/stdio.c lib/libc/crt0.S"

ULIBOBJ=""
for src in $ULIBC; do
    obj="build/obj/$(echo "$src" | tr / _ | sed 's/\.[cS]$/.o/')"
    $CC $UCFLAGS -c "$src" -o "$obj"
    ULIBOBJ="$ULIBOBJ $obj"
done

for prog in init rsh ls cat echo write ps uptime uname clear reboot \
    e1rufetch mkdir rmdir rm touch chmod stat date halt poweroff shutdown \
    pkg hello; do
    pobj="build/obj/usr.bin_${prog}.o"
    $CC $UCFLAGS -c "usr.bin/$prog/$prog.c" -o "$pobj"
    $ZIG ld.lld -m elf_x86_64 -T lib/libc/user.ld $pobj $ULIBOBJ \
        -o "build/bin/$prog"
done

# hello is not part of the base image: ship it as packages/hello.pkg
python3 tools/mkramdisk.py build/hello.pkg \
    "manifest=pkg/hello/manifest" \
    "bin/hello=build/bin/hello" \
    "share/doc/hello/README=pkg/hello/README"

# ---------------- ramdisk ----------------
echo "== [4/5] ramdisk =="
python3 tools/mkramdisk.py build/ramdisk.tar \
    "bin/" "etc/" "packages/" \
    "init=build/bin/init" \
    "bin/rsh=build/bin/rsh" "bin/ls=build/bin/ls" \
    "bin/cat=build/bin/cat" "bin/echo=build/bin/echo" \
    "bin/write=build/bin/write" "bin/ps=build/bin/ps" \
    "bin/uptime=build/bin/uptime" "bin/uname=build/bin/uname" \
    "bin/clear=build/bin/clear" "bin/reboot=build/bin/reboot" \
    "bin/e1rufetch=build/bin/e1rufetch" \
    "bin/mkdir=build/bin/mkdir" "bin/rmdir=build/bin/rmdir" \
    "bin/rm=build/bin/rm" "bin/touch=build/bin/touch" \
    "bin/chmod=build/bin/chmod" "bin/stat=build/bin/stat" \
    "bin/date=build/bin/date" "bin/halt=build/bin/halt" \
    "bin/poweroff=build/bin/poweroff" "bin/shutdown=build/bin/shutdown" \
    "bin/pkg=build/bin/pkg" \
    "packages/hello.pkg=build/hello.pkg" \
    "etc/motd=etc/motd"

# ---------------- disk image ----------------
echo "== [5/5] disk image =="
OUT=${E1RUBSD_IMG:-build/e1ruBSD-0.1-amd64.img}
rm -f "$OUT"
python3 tools/mkimage.py "$OUT"
echo "build OK: $OUT"
