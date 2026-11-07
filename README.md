# e1ruBSD 0.1 (RUNE) — amd64

从零实现的教学/实验用 BSD 风格操作系统：自写引导扇区与 stage2、四级分页
pmap、轮转调度器、vnode/ramfs VFS、autoconf 设备模型，以及通过 `int $0x80`
与内核通信的 ring-3 用户态。

## 当前状态 (2026-11-07)

内核到用户态链路已打通，并在本机 VirtualBox 上实测通过：

- 启动 banner、E820/pmap 初始化、设备探测、ramfs 解包、exec `/init`
- `/init` 进入用户态后 exec `/bin/rsh`，出现 `e1ru$` 提示符
- 外部命令与内建命令均可用（`ls`/`cat`/`echo`/`write`/`ps`/`uptime`/`uname`/
  `clear`/`reboot`/`e1rufetch`，内建 `help`/`clear`/`cd`/`pwd`/`exit`）
- IRQ0（100 Hz 定时器）与 IRQ1（键盘）正常
- 用户态 `int $0x80` 与 `int3` 都能正确进入 trap 入口

本轮（2026-11-07）新增并逐项构建 + 实测：

| 项 | 实现 | 实测结果 |
| -- | ---- | ----- |
| fork COW | 帧引用计数 `pm_ref[]`；`pmap_copy_user()` 只共享页并把双方标为 RO+COW；写故障由 `pmap_cow_fault()` 复制 | 验证期间临时埋点打印 `cow: va=... old -> new`，每次外部命令（fork）稳定出现两处栈写故障；`uname`/`ls`/`ps`/`e1rufetch` 均正常。埋点已删除，最终镜像无调试输出 |
| `malloc(size > 4096)` | `pm_alloc_pages()` 在空闲页链中找物理连续 run，按请求页数分配；`free()` 按 `size` 走 `pm_free_run()` | 启动期临时自检分配 3 页、跨页写读校验并释放，输出 `malloc-selftest: ok`（自检已删除） |
| trap/断点退出码 | 用户异常按 `128 + signal` 写入 `p_exitcode`；`int3`/单步 → SIGTRAP(5) | 临时 `trapme` 程序执行 `int3`：内核打印 `pid 3 (trapme): breakpoint at rip=... (signal 5)`，shell 打印 `rsh: trapme: exit status 133`（程序已删除） |
| `int3` DPL | IDT 3 号门 DPL=3（与 `int 0x80` 同为中断门） | 同上，用户态直接执行不被 `#GP` 拦截 |

验证方法（可复现）：`./build.sh` 全绿后
`E1_HEADLESS=1 E1_UART_TCP=1 ./run-vbox.sh`，用任意 TCP 客户端连
`127.0.0.1:4555` 即可看到串口输出并输入命令。

## 未做项与原因

- **ramfs 持久化：未做。** 当前没有任何可写块设备驱动：`sys/dev` 只有
  `autoconf/clock/cons/pckbd/md`，其中 `md` 是从 ramdisk 复制的只读内存盘。
  持久化需要 ATA/AHCI（或 virtio-blk）驱动、磁盘分区布局、ramfs 写回路径；
  这是一整条新驱动链，不适合在本轮最小改动中塞进来。ramfs 只读挂载、
  文件可创建但仅在内存中存活。
- **需求分页：未做。** 需求分页需要 vnode-backed page cache、page-in 路径、
  fault 分类（缺页/保护/栈增长）以及和 exec 的段映射协同。当前 `exec`
  一次性映射全部段与栈、`fork` 用 COW 共享，已足以支撑现有用户态；
  只做半套容易留下“页存在但内容未加载”的隐蔽状态。
- **信号机制：未做。** 仅用退出码 `128 + signal` 把异常结果传给父进程，
  没有 handler/屏蔽字/信号栈；用户态异常一律终止进程。

## 构建与运行

依赖：macOS 上的 `clang`（Xcode Command Line Tools）、`zig`（仅用其
`ld.lld` 与 `objcopy`，可用 `ZIG=` 覆盖路径）、`python3`、VirtualBox。

```sh
./build.sh                    # 产出 build/e1ruBSD-0.1-amd64.img（16 MB raw）
./run-vbox.sh                 # GUI 启动，串口写入 .vbox/serial.log
E1_HEADLESS=1 ./run-vbox.sh   # 无界面启动
E1_HEADLESS=1 E1_UART_TCP=1 ./run-vbox.sh   # 串口走 TCP 4555，可交互
./run-vbox.sh clean           # 注销并删除 e1ruBSD VM
```

说明：

- `build.sh` 用本机 clang（target `x86_64-unknown-none-elf`）编译到 `.o`，
  再用 zig 自带的 `ld.lld` 链接。这样可以绕开 zig 每次链接都构建
  compiler_rt 的开销；内核和用户态都是 freestanding，不需要 compiler_rt。
- 每次运行默认从 raw 镜像重建 `.vbox/e1ruBSD.vdi`（`E1_KEEP=1` 保留旧盘）。
- `ZIG` 默认指向 `/Users/macbook/Desktop/os/e1LibreOS/downloads/zig/zig`，
  换机器时请显式设置。

## 启动链路与内存布局

```
BIOS → bootsect (LBA 0, 0x7C00) → stage2 (0x7E00, real mode)
     → A20 + 4GB identity map + long mode
     → kernel 0x10000 → 0x100000, ramdisk 0x40000 → 0x400000
     → locore.S 建立 DMAP/高半部映射 → x86_boot() → C 世界
```

| 物理地址 | 用途 |
| --- | --- |
| `0x5000` | E820 表（24 B/项） |
| `0x6000` | bootinfo（magic `e1ru`） |
| `0x7C00` | bootsect / 实模式栈 |
| `0x7E00` | stage2 |
| `0x10000` | kernel 暂存区（最大 192 KB） |
| `0x40000` | ramdisk 暂存区（最大 368 KB，止于 `0x9C000`） |
| `0x8C000`–`0x91FFF` | stage2 引导页表：PML4 / PDPT / 4×PD（2 MB 大页） |
| `0x100000` | kernel 最终位置 |
| `0x400000` | ramdisk 最终位置 |

内核虚拟布局：`KERNEL_BASE = 0xFFFFFFFF80100000`，
`DMAP_BASE = 0xFFFFFFFF80000000`（直接映射前 1 GB 物理内存）。

用户地址空间：代码/数据 `0x00400000` 起，堆 `0x01000000`–`0x08000000`，
栈顶 `0x7FFF0000`、大小 1 MB；每个用户 pmap 共享内核 `PML4[511]`。

## 关键机制

**pmap 与 COW.** 物理页池按帧管理，`pm_ref[]` 记录每个帧被多少地址空间
共享。`fork` 时 `pmap_copy_user()` 不复制数据页，只把父子的 PTE 都清
`PTE_RW`、置 `PTE_COW` 并增加引用计数；任一方写入触发 `#PF`，`trap()`
识别“present + write + user”且带 COW 标记后调用 `pmap_cow_fault()`：
分配新帧、复制内容、改 PTE 为私有可写、释放旧引用。进程销毁时
`pmap_free_subtree()` 逐页降引用，最后一个持有者才真正归还页池。

**malloc.** 9 个 bucket（16–4096 B）按大小分派，空闲链指针存放在空闲块
内部；大于 4096 B 的请求按页粒度分配连续物理页（页首 16 字节存页数
和用户指针），释放时按 `size` 判断走 bucket 还是 `pm_free_run()`。

**trap 与系统调用.** IDT 32 个异常 + 16 个 IRQ 全部由 `locore.S` 的
ISR stub 收敛到 `alltraps` → `trap(tf, p)`。用户态异常按
`128 + signal` 记录退出码并终止进程（`int3` → 133），内核态异常
panic 并停 CPU。系统调用走 `int $0x80`（门类型 `0xEE`），
`rax`=编号、`rdi`/`rsi`/`rdx` 传参、`rax` 返回，共 19 个调用
（见 `sys/sys/syscall.h`）。

**VFS/ramfs.** vnode + 简单 VFS 层；ramfs 由 stage2 载入的 tar 解包填充，
`md0` 是只读内存盘副本；支持读、列目录、建文件、写内存文件。

## 已知限制

- **BIOS INT13 限制**：stage2 用 `AH=42h` 扩展读，目标缓冲区必须低于
  640 KB，因此 kernel 暂存区 ≤ 192 KB、ramdisk 暂存区 ≤ 368 KB；
  镜像更大需要分段加载或改用高地址 DMA 方案。
- **引导页表位置**：`0x8C000`–`0x91FFF` 必须避开 ramdisk 暂存区上界
  与 `0xA0000`（VGA/ROM 区）。
- 单 CPU，无 SMP；调度为 10 tick（100 ms）量子的轮转，无优先级、无抢占细化。
- 无需求分页：exec 一次性映射段与栈；fork 的 COW 扫描
  `0x00400000`–`0x08000000` 与栈区，未映射页直接跳过。
- 页池与 DMAP 仅覆盖前 1 GB 物理内存；更高内存不参与分配。
- `NPROC=16`，内核栈为 16 槽 × 16 KB 的连续池。
- `malloc(size > 4096)` 要求物理连续页；碎片化严重时可能失败（返回 NULL）。
- 无信号处理机制；用户异常只打印一行并终止进程（退出码 `128+signal`）。
- ramfs 无持久化，重启后回到镜像初始内容。
- 系统调用最多 3 个参数，无 syscall 重启/错误码约定（负数直接作为返回值）。

## 仓库结构

```
sys/arch/amd64/boot/     bootsect.S / stage2.S / 链接脚本
sys/arch/amd64/amd64/    locore.S, cpu.c, pmap.c, trap.c
sys/arch/amd64/include/  machine/cpu.h, machine/pmap.h
sys/kern/                init_main, sched, syscalls, exec_elf, vfs_subr, malloc
sys/dev/                 autoconf, cons, clock, pckbd, md
sys/fs/ramfs.c           内存文件系统
lib/libc/                用户态最小 libc 与 crt0
usr.bin/                 init, rsh, ls, cat, echo, write, ps, ...
tools/                   mkramdisk.py, mkimage.py
build.sh, run-vbox.sh
```

## 修复记录（历史）

1. **stage2 暂存区与引导页表重叠**：ramdisk 暂存于 `0x40000`，而引导
   页表建在 `0x70000`–`0x75FFF`，覆盖了 tar 尾部内容（`/bin/uname`）。
   修复：页表迁到 `0x8C000`。
2. **`free()` 误判释放路径**：按“指针是否页对齐”区分 bucket 块与整页块
   会双重释放活动页表。修复：按 `size` 分派。
3. **fork 子进程首次调度栈布局**：`p_ctx.rsp` 必须指向 `trapret` 槽，
   原先多减 `6*8` 导致 `ret` 弹出垃圾。
4. **`int3` 从 ring 3 不可用**：3 号门 DPL=0 触发 `#GP(0x1a)`；
   改为 DPL=3 中断门。
5. **`pm_alloc_pages()` 断链错误**：空闲链是降序链，按“升序找 run”永远
   找不到；且断链只摘掉 run 的最后一个节点，造成重复页。修复：同时支持
   升/降序扩展，并从 run 起点前驱摘链。
6. **`write` 创建文件触发内核 panic**：`vn_open()` 用 `O_TRUNC` 时调用
   `vn_write(vp, 0, NULL, 0, NULL)`，`ramfs_write()` 无条件写
   `*donep`，解引用 NULL 触发 `#PF(err=0x2)` 并 panic。修复：所有
   `donep` 写入判空；同时让该调用真正释放数据块并归零大小（此前
   O_TRUNC 并不截断）。实测 `write /hi` → `cat /hi` 正常，覆盖写更短
   内容后无残留。
7. **用户态 `printf` 不支持 `%-Ns`**：`ps` 表头输出成 `PID 9s STATE`。
   修复：解析 `-`/`0` 标志，实现左对齐填充；`ps` 现输出
   `PID STATE COMMAND` 三列对齐。
