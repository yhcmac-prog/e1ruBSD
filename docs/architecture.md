# e1ruBSD 架构说明

本文描述 e1ruBSD 0.1 (RUNE) 的实际实现，而不是设计目标。代码路径以仓库
内文件为准。

## 1. 启动链路

```
BIOS
 └─ LBA 0 bootsect.S        16-bit 实模式，0x7C00，加载 stage2
     └─ LBA 1..62 stage2.S  实模式：E820、A20、引导页表、long mode
         └─ kernel (0x100000) + ramdisk (0x400000)
             └─ locore.S    建 DMAP / 高半部映射，跳 x86_boot()
                 └─ sys/kern/init_main.c  x86_boot()
```

**bootsect** (`sys/arch/amd64/boot/bootsect.S`)：只做一件事——用 INT13
`AH=42h` 把 stage2 读到 `0x7E00`，然后跳过去。stage2 的几何参数由
`tools/mkimage.py` 回填到 bootsect 固定偏移。

**stage2** (`sys/arch/amd64/boot/stage2.S`)：

1. 读 BIOS E820 到 `0x5000`，把 bootinfo 写到 `0x6000`；
2. 打开 A20，建立 4 GB 恒等映射的引导页表（`0x8C000`–`0x91FFF`）；
3. 进入 long mode，把 kernel 从 `0x10000` 搬到 `0x100000`，ramdisk 从
   `0x40000` 搬到 `0x400000`；
4. 跳入内核入口。

注意 INT13 `AH=42h` 的 DMA 缓冲区必须低于 640 KB，所以 kernel 与 ramdisk
的暂存区大小受限（192 KB / 368 KB）。

**locore.S**（`sys/arch/amd64/amd64/locore.S`）：在 `0x76000`–`0x7BFFF`
自建内核页表：前 1 GB 物理内存做 DMAP（`0xFFFFFFFF80000000` 起），
内核镜像映射到 `0xFFFFFFFF80100000`。随后清掉低地址恒等映射，
把栈切到高半部，调用 `x86_boot()`。

## 2. 内存管理

### 2.1 物理页池

`pmap_init()`（`sys/arch/amd64/amd64/pmap.c`）从 E820 收集可用 RAM，
跳过 kernel 镜像与 ramdisk 区域，把 4 KB 帧串成单链表空闲池；
链指针直接存放在空闲页的第一个 8 字节。`pm_total`/`pm_free` 记录总量。

内核栈单独从最高可用区域的顶部切出 16 槽 × 16 KB 的连续池
（`pm_kstack_alloc()`），不进入空闲链。

### 2.2 页表

- 4 级页表，4 KB 页；用户地址空间独立 PML4，`PML4[511]` 与内核共享。
- `pmap_map_user()` 在每一级都置 `PTE_USER`（x86 的 U/S 位是全链路 AND，
  少一级就会变成 supervisor-only）。
- `pt[i1]` 写入时带 `PTE_RW` 或只读，供 COW 使用。
- 软件位 `PTE_COW`（`0x200`）标记“present、只读、与其他地址空间共享”。

### 2.3 帧引用计数与 COW

```
pm_ref[MAX_PHYS_FRAMES]  // uint16，按页号索引
pm_alloc_page()          -> ref = 1
pm_free_page()           -> ref > 1 则递减；否则归零并回收到空闲链
```

`fork` 走 `pmap_copy_user(dst, src)`：

1. 扫描 `USER_TEXT..USER_BRK_MAX` 和用户栈区，未映射页跳过；
2. 对每个已映射页，在子 pmap 建立同物理页的只读映射（`pmap_map_user(..., 0)`）；
3. 把父子两侧 PTE 都清 `PTE_RW`、置 `PTE_COW`，`pm_ref_inc(pa)`。

任一方写入时触发 `#PF`（err = present|write|user）。`trap()` 在
`vec == 14 && (err & 3) == 3` 时调用 `pmap_cow_fault(pmap, cr2)`：
分配新帧 → 复制整页 → 把 PTE 改成私有可写 → 对旧帧 `pm_free_page()`
（降引用或回收）。失败（非 COW 页）才按异常终止进程。

这样 fork 不再逐页复制；代价是每个写过的页第一次写要复制一次。

### 2.4 malloc

`sys/kern/subr_malloc.c` 是页分配器之上的小型分配器：

| 请求大小 | 路径 |
| --- | --- |
| ≤ 4096 | 9 个 bucket（16/32/64/128/256/512/1024/2048/4096），每页切成等长块，空闲链指针存在块内部 |
| > 4096 | `pm_alloc_pages(npg)` 找物理连续帧；页首 16 字节存页数，返回 `页首+16` |

`free(ptr, size)` **按 `size` 分派**，绝不靠地址对齐猜测。页粒度块通过
`pm_free_run(页首, 页数)` 归还。请求物理连续意味着碎片化时可能失败。

## 3. 进程与调度

`sys/sys/proc.h` 中的 `struct proc` 包含 pid、名字、状态、退出码、
`p_pmap`、内核栈、保存的 `struct ctx`（callee-saved 寄存器 + rip/rsp）、
打开的 vnode 表等。进程表大小 `NPROC=16`。

- `sched_newproc()` 分配 proc 与内核栈（`pm_kstack_alloc()`）。
- `sched_enqueue()` 放入就绪队列；`sched_start()` 进入调度循环。
- 调度为轮转，量子 `SCHED_QUANTUM=10` tick（100 Hz 下 100 ms），
  `swtch()` 在内核栈之间切换。
- `sched_exit()` 标记 zombie、写退出码；`wait4` 取走退出码并把槽位
  还给 `SUNUSED`。`proc_free_resources()` 销毁用户 pmap（降 COW 引用）、
  释放内核栈并清 vnode 引用。

`fork`（`sys_fork`）复制 pmap（COW）、复制 fd 表，子进程 `ctx` 的
`rip/rsp` 指向父进程 `trapret` 槽，返回值 `rax=0`。

`exec`（`exec_load()`，`sys/kern/exec_elf.c`）新建 pmap，解析 ELF，
把 PT_LOAD 段映射到 `USER_TEXT` 起，构建用户栈（argv/env），
最后 `pmap_activate()` 并 `trapret` 进 ring 3。

## 4. trap、中断与系统调用

- IDT 共 48 项：0–31 异常、32–47 IRQ。`cpu_init()` 装载 GDT/TSS/IDT。
- `locore.S` 的 ISR stub 把向量号压栈后统一跳 `alltraps`，保存
  `struct trapframe`，调用 `trap(tf, p)`。
- 内核态异常 → `panic()` 打印向量/err/rip/rsp/cs 并停 CPU。
- 用户态异常 → 按向量映射为信号号（`int3`/单步 → 5，除零 → 8，
  非法指令 → 4，越界/保护 → 11），`p_exitcode = 128 + sig`，
  打印一行 `pid N (name): ... at rip=... (signal S)` 后 `sched_exit()`。
- IRQ0（PIT 100 Hz）维护 tick 并触发调度；IRQ1 读键盘扫描码。
- 系统调用：`int $0x80`，门类型 `0xEE`（中断门，进入时清 IF）。
  ABI：`rax`=编号，`rdi`/`rsi`/`rdx` 为参数，返回值在 `rax`。
  `NSYSCALL=25`，见 `sys/sys/syscall.h`。`SYS_reboot` 的 `rdi` 现在是
  模式：0 重启、1 关机、2 停机。

## 5. VFS 与 ramfs

- `sys/sys/vfs.h` 定义 `struct vnode`（含 `v_mode`）与 `v_op`（lookup/
  create/mkdir/read/write/readdir/inactive/remove/chmod 等）。
- `sys/kern/vfs_subr.c` 提供 `namei()`、`vfs_parent()`、`vfs_mountroot()`、
  vnode 缓存与引用计数；`/init` 与 `/bin/*` 都通过它解析。
- `sys/dev/md.c` 把 stage2 放在 `0x400000` 的 ramdisk 映射成只读内存盘
  `md0`，并提供块读取接口。
- `sys/kern/subr_tar.c` 从 `md0` 读 tar，解包进 ramfs。
- `sys/fs/ramfs.c` 是纯内存文件系统：目录项与文件数据都在内核堆上，
  支持创建/写入/截断/删除/chmod，但进程退出与重启都不会保留。
  `vn_open()` 的 `O_TRUNC` 通过一次“offset 0、长度 0”的写调用表达，
  ramfs 在该调用里释放全部数据块并把大小归零；其余读写路径对
  `donep == NULL` 也做了保护（历史 bug：该调用曾因解引用空 `donep`
  触发内核页故障）。删除会把节点从目录链摘除并释放数据块，节点与
  vnode 内存等到最后一个引用释放（`vn_inactive`）才回收。

## 5.1 电源控制与时钟

- `sys/dev/acpi.c`：启动时扫描 EBDA / `0xE0000–0xFFFFF` 的 RSDP，
  经 RSDT/XSDT 找到 FADT，再扫描 DSDT 的 `Name(\_S5_, Package(...))`
  得到 SLP_TYP。`acpi_poweroff()` 写 PM1a/PM1b 控制块进入 S5；
  `acpi_reboot()` 使用 FADT reset register（若存在）。
- `sys_reboot()` 的后备顺序：ACPI → 0xCF9 → 8042 `0xFE` → 空 IDT
  三重故障（重启）；ACPI → 0x4004/0xB004/0x604（关机）；`halt` 直接
  `cli; hlt`。
- `sys/dev/rtc.c` 从 CMOS 0x70/0x71 读时间，处理 UIP、BCD 与 12/24
  小时模式，并用 Sakamoto 公式计算星期；`date` 原样输出，无时区换算。

## 6. 设备模型

`sys/dev/autoconf.c` 是静态 autoconf：`mainbus` 下依次挂
`cpu0`、`vga0`、`com0`、`pit0`、`pckbc0`/`pckbd0`、`acpi0`、`rtc0`、
`md0`。设备探测顺序即初始化顺序，没有总线枚举。

- `cons.c`：VGA 80×25 文本 + COM1 115200 串口；串口用于无界面调试。
- `clock.c`：PIT 通道 0，100 Hz，注册 IRQ0。
- `pckbd.c`：i8042 键盘，注册 IRQ1。

## 7. 构建与镜像

`build.sh`：

1. clang（`x86_64-unknown-none-elf`）编译 bootsect/stage2/kernel/用户态；
2. zig 自带 `ld.lld` 链接（不构建 compiler_rt）；
3. `objcopy -O binary` 得到裸二进制；
4. `tools/mkramdisk.py` 生成 tar（`/init`、`/bin/*`、`/etc/motd`、
   `/packages/hello.pkg`）；示例包由 `pkg/hello/manifest`、编译出的
   `build/bin/hello` 与 `pkg/hello/README` 打成 ustar 归档；
5. `tools/mkimage.py` 组装 16 MB raw 镜像：
   LBA0 bootsect，LBA1–62 stage2，LBA64 起 kernel，LBA2048 起 ramdisk。

`run-vbox.sh`：`convertfromraw` 生成 `.vbox/e1ruBSD.vdi`，
配置 256 MB 内存、IDE、串口；`E1_UART_TCP=1` 时串口为 TCP 4555，
否则写 `.vbox/serial.log`。

## 8. 未实现与边界

| 项 | 现状 | 原因 |
| --- | --- | --- |
| ramfs 持久化 | 无 | 没有可写块设备驱动（只有只读 `md`）；需要 ATA/AHCI + 磁盘布局 + 写回 |
| 需求分页 | 无 | 需要 vnode-backed page cache 与 page-in 路径；exec 一次性映射已够用 |
| 信号 | 无 | 只用退出码 `128+signal` 表达；没有 handler/屏蔽 |
| SMP | 无 | 单 CPU 假设贯穿 GDT/TSS/调度/页池 |
| 网络 | 无 | 无 NIC 驱动与协议栈 |
| 用户态动态链接 | 无 | 用户程序全部静态链接到 `USER_TEXT` |
