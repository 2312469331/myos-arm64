# 踩坑记录

## Virtio-blk 写操作数据覆盖 Header

**现象：** FAT*32 创建文件后读目录全是乱码，写*入的数据和读回来的不一致。

**原因：** `blk_request` 中写操作把数据复制到了 `va`（缓冲区开头），但 virtio 协议要求前 16 字节放请求 Header（type + sector），数据应该在 `va+16` 开始。16 字节 Header 被数据覆盖后，设备读到错误的扇区号，写到随机位置。

```rust
// 错误：复制到 va（覆盖 header）
core::ptr::copy_nonoverlapping(buf, va, len);

// 正确：复制到 va+16（header 之后的数据区）
core::ptr::copy_nonoverlapping(buf, va.add(16), len);
```

**为什么之前没触发：** 之前只有读操作（加载 ELF），没有写操作。第一次写 FAT 表时触发。

***

## Elf 加载器文件偏移计算错误

**现象：** 加载 busybox（2.1MB）时内核 panic，加载小 shell（2KB）正常。

**原因：** `load_elf` 中计算文件偏移时用了错误的比较条件：

```rust
// 错误：把文件偏移当虚拟地址比较
if va < seg.vaddr + seg.offset { ... }

// 正确：比较段内偏移
let in_seg = va - seg.vaddr;
if in_seg < seg.filesz { ... }
```

Segment 的 `offset` 是文件偏移，`vaddr` 是虚拟地址。两者相加没有意义。对于 busybox 第一个段 `offset=0`、`vaddr=0x400000`，条件永远不成立，整个代码段 2.1MB 的数据都没加载进去。

***

## Virtio 描述符索引越界

**现象：** QEMU 报 `Incorrect order for descriptors`，磁盘 I/O 失败。

**原因：** `blk_request` 使用 `desc.add(desc_idx + 1)` 和 `desc.add(desc_idx + 2)` 访问描述符表。当 `desc_idx` 接近队列尾（254/255）时，`desc_idx + 1`/`+2` 越过了表边界。

```rust
// 错误：不加模，可能越界
(*desc.add(desc_idx)).next = (desc_idx + 1) % VQ_SIZE;
(*desc.add(desc_idx + 1)).addr = data_pa;  // desc_idx=255 → desc[256] 越界

// 正确：访问和 next 都模 VQ_SIZE
let i0 = desc_idx % VQ_SIZE;
let i1 = (desc_idx + 1) % VQ_SIZE;
(*desc.add(i0)).next = i1 as u16;
(*desc.add(i1)).addr = data_pa;
```

***

## 进程入口寄存器未初始化

**现象：** busybox 启动到 `strlen` 时崩溃，访问地址 `0x100`。

**原因：** `start_first_task` 通过 `eret` 进入用户态时，`x0` 寄存器是内核代码的残留值（`TaskContext` 指针），不是 Linux ABI 要求的 0。glibc 的 `_start` 把内核地址当 argc 用，后续计算得到错误指针。

```rust
// 恢复寄存器后 x0 = TaskContext 指针 + 104（内核地址）
"ldp x19, x20, [x0], #16",
// ... 6 次 ldp + 1 次 ldr 后 x0 指向 TaskContext + 104

// 缺少 x0 清零
"eret"  // x0 带着内核地址进了用户态

// 修复：eret 前清零 x0-x2
"mov x0, xzr"
"mov x1, xzr"
"mov x2, xzr"
```

同时 `x1`（argv）、`x2`（envp）也没有设置，glibc 尝试解引用空指针或内核地址。

***

## 用户栈顶未分配导致缺页

**现象：** 用户进程第一条 `ldr x1, [sp]` 触发缺页中断。

**原因：** 栈顶在 `find_stack_top` 计算的位置（如 `0x80000000`），但栈物理页只映射到 `stack_top - 1`。SP 初始值指向了未映射的页。用户代码从 `[sp]` 读 argc 时触发缺页。

**修复：** 调整初始 SP 到 `stack_top - 16`（按页内偏移 16 字节对齐），在栈顶写入 `argc=0`。

```rust
let init_sp = (loaded.stack_top - 16) & !15;
// 写入 argc=0
let kva = phys_to_virt(loaded.stack_pa) as *mut u64;
core::ptr::write(kva.add(off / 8), 0u64);
```

***

## fork 子进程 fd 表为空

**现象：** 父进程创建 pipe 后 fork，子进程拿不到 pipe 的 fd。

**原因：** `do_fork` 中子进程的 `fd_table` 初始化为 `FdTable::new()`（新建空表），没有继承父进程的 fd。

```rust
// 错误：空表
fd_table: FdTable::new(),

// 修复：克隆父进程 fd 表
fd_table: {
    let mut child_fds = FdTable::new();
    child_fds.clone_from(&parent.fd_table);
    child_fds
},
```

***

## 用户态缺页时 Rust 侧 mm 指针为空

**现象：** 缺页处理时 `rust_vma_find` 收到 NULL mm，返回 0，进程被 SIGKILL。

**原因：** `current_mm` 由 `schedule()` 更新，但 `start_first_task` 直接 eret 到用户态，没有更新 C 侧的 `current_mm`。缺页时 C 代码读取 `current_mm` 为 NULL。

```rust
// schedule() 里会更新：
current_mm = task.current_mm_ptr();

// 但 start_first_task 没有！！！
```

**修复已应用？** ✅ 是的，`start_first_task` 调用前确保 `current_mm` 已设置。

***

## ALL\_TASKS 锁在 IRQ 上下文死锁

**现象：** 唤醒等待队列时在 IRQ 里尝试锁 `ALL_TASKS`，但锁可能被中断的代码持有。

**原因：** `wake_up` → `add_task` → `register_task` → `ALL_TASKS.lock()`。但 UART IRQ 里调用 `wake_up` 时，`ALL_TASKS` 可能被中断前的代码锁着。

**修复：** 分离 `add_task_ready`（只放就绪队列，不锁 ALL\_TASKS），IRQ 路径用 `add_task_ready`。

```rust
pub fn add_task_ready(task: Arc<Task>) {
    let prio = task.prio;
    q[prio].push_back(task);  // 不加 ALL_TASKS 锁
}
```

***

## irq\_table\_lock 在 IRQ 上下文死锁

**现象：** `register_irq` 时定时器 IRQ 触发，`el1_irq_handler` 也试图拿同一个 `irq_table_lock`。

**原因：** 定时器在 `main()` 中已启动。`irq_register` 写 `irq_table` + `gic_enable_irq` 时在临界区内，但 `uart_puts` 也可能触发 UART IRQ。

**当前状态：** `spin_lock_irqsave` 理论上已关中断，但仍有 crash。待进一步排查。

***

## FAT32 目录项 file\_size 不同步

**现象：** `echo hello > test.txt` 后 `cat test.txt` 输出为空。

**原因：** `write_at` 写入数据后更新了 inode 内存中的 `size` 字段，但目录项里的 `file_size` 字段没更新。`path_walk` 创建新 `Fat32Inode` 时从目录项读到 `size=0`，`read_at` 检查 `offset >= size` 直接返回 EOF。

**修复：** 添加 `update_entry_size` 函数扫描根目录项，将正确的 `file_size` 回写到目录项。

```rust
fn update_entry_size(&self, cluster: u32, size: u64) -> Result<(), &'static str> {
    // 扫描根目录项，匹配 first_cluster，更新 file_size
}
```

***

## brk 立即分配物理页

**现象：** 正确来说是设计问题，不是 bug。

**原设计：** `do_brk` 在扩展堆时立即分配物理页并清零。

**正规做法：** 只更新 VMA，物理页在缺页时按需分配。

**已修复：** ✅ `do_brk` 改为只建 VMA，缺页时 `page_fault_handler` 分配物理页。

***

## GlobalAlloc 返回 NULL

**现象：** Rust `Vec::with_capacity(2MB)` 时内核 panic。

**原因：** `allocator.rs` 中 `kmalloc` 失败后后备池只有 4KB，大块分配返回 NULL，触发 `handle_alloc_error` → panic。

**修复：** 大块分配（>64KB）直接走 buddy 分配器，不再依赖 kmalloc。

***

## 启动时 argc/argv/envp/auxv 设置不完整

**现象：** busybox 启动后访问 `AT_PHDR` 时崩溃，访问地址 `0x400040`。

**原因：** 栈上只放了 `argc=0`，没有 argv 终止符、envp、auxv。glibc 从栈上读 auxv 时跳到错误位置，解引用非法指针。

**修复：** `setup_user_stack` 在栈上写出完整的启动帧：

```
argc=1, argv[0]="/SHELL.ELF", argv[1]=NULL, envp=NULL,
AT_PHDR, AT_PHNUM, AT_PAGESZ, AT_ENTRY, AT_NULL
```

***

## 调度器 first\_run 未清零

**症状：** 用户进程每次被调度都从入口重新执行。

**原因：**

- `context_switch` 保存路径无条件设置 `first_run=1`
- `start_first_task` 的 `ldr x10, [x0]` 无后增，x0 停在 offset 96
- `str xzr, [x0, #24]` 写到了 offset 120 (= spsr\_el1)，不是 128 (= first\_run)

**修复：** 删 save 路径的 `mov x10, #1; str x10, [x8]`；`ldr x10, [x0]` → `ldr x10, [x0], #8`；加 `str xzr, [x0, #24]`。

***

## wait\_event 双调度竞态

**症状：** 任务出现在就绪队列两次，第二份拷贝破坏第一份的栈数据。

**原因：** `drop(guard)` 释放锁后 UART IRQ 的 `wake_up` 抢先设 RUNNING + add\_task，然后 `schedule()` 又 add\_task 一次。

**修复：** `drop(guard)` 后检查 `state == SLEEPING` 才调 `schedule()`。

***

## 嵌套 IRQ 破坏 ex\_type

**症状：** 异常处理打印未知异常类型（EC=0），ELR 显示内核地址。

**原因：** `el1_exception_entry` 中 `msr daifclr` 开中断后，嵌套 IRQ 覆写 ELR\_EL1/SPSR\_EL1 系统寄存器。x0（ex\_type）也被嵌套 IRQ + schedule 破坏。

**修复：** 用 x19（调用方保存寄存器）暂存 x0，`bl c_exception_handler_el1` 前恢复。

***

## SIGKILL 送不出去

**症状：** 页错误发 SIGKILL 后进程不退出，无限循环。

**原因：**

- `do_kill` 的 SIGKILL 分支调 `clear_all_pending()` 清掉了信号本身
- 没调 `signals.send(sig)`，pending 位图未设置

**修复：** 删 `clear_all_pending()`，加 `target.signals.send(sig)`。

***

## ioremap 非页对齐地址

**症状：** 读写 virtio MMIO 寄存器得到 `0xabababab`。

**原因：** `ioremap` 直接映射物理地址，遇到 `0xa003e00` 这种非页对齐地址，映射到了错误的物理页。

**修复：** ioremap 内部对齐到页基址，映射后返回 `va_start + page_offset`。

***

## Legacy MMIO 寄存器偏移错乱

**症状：** virtio 设备初始化失败（Status 写无效、QueuePFN 无效）。

**原因：** Legacy（Version=1）和 Modern（Version=2）的寄存器布局完全不同。查规范 Table 4.3。

**关键差异：**

| 寄存器           | Modern                | Legacy             |
| ------------- | --------------------- | ------------------ |
| Status        | 0x034                 | 0x070              |
| GuestFeatures | 0x024                 | 0x020              |
| QueuePFN      | 三组 desc/avail/used 地址 | 单一 QueuePFN（0x040） |

***

## 页表页未清零导致垃圾物理地址

**症状：** ELF 加载映射页表，`arm64_map_one_page` 分配 L1/L2/L3 页表页后，旧数据的 `pte_present` 被误判为合法映射，读出的物理地址为 `0x2babababa010`。

**修复：** 每次 `alloc_phys_pages` 后用 `memset(page, 0, 4096)` 清零。

***

## UART 接收中断阈值太高

**症状：** 打字不回显，SYS\_READ 永远阻塞。

**原因：** `UART_IFLS` 设成半满（8 字节）才触发中断。打字 1-7 字符不触发，超时定时器未启用。

**修复：** `RXIFLSEL_1_2` → `RXIFLSEL_1_8`（1 字节触发）。

***

## SYS\_EXECVE/READ/WRITE 调用约定不匹配

**症状：** 硬编码测试程序正常，ELF shell 崩溃或无反应。

**原因：** 硬编码程序用自定义 ABI（X0=buf），ELF shell 用 POSIX ABI（X0=fd, X1=buf, X2=len）。execve 还把 X0 当 `pt_regs*` 用。

**修复：** 按 `x0 <= 2` 判断 POSIX 还是旧版 ABI。execve 需 C handler 传 `regs_ptr`。

***

## println! 在调度路径中开中断

**症状：** 调试打印就绪队列时任务数对不上，timer IRQ 穿插执行 schedule 改了队列内容。

**原因：** `_print()` 内部 `enable_irq()` 无条件开中断。timer IRQ 趁机执行 `schedule()` 弹出任务。

**修复：** 调度相关调试用 `Console::write!` 或 `ffi::c_print_str` 直接输出，不经过 `println!`。

***

## Virtio used ring 轮询竞态（读后于通知）

**症状：** FAT32 读扇区概率性超时（一次能读一次不能）。

**原因：** `blk_request` 中先通知设备、再读 `used.idx` 计算 target。设备极快完成时 `used.idx` 已更新，target 算成了 `(新值)+1`，等不到 `used.idx == target`。

```rust
// 错误：通知后才读
regs.write32(REG_QUEUE_NOTIFY, 0);
let target = used.idx.wrapping_add(1);   // 读到已是新值，target 多 1
loop { if used.idx == target { break } } // 永远不等

// 正确：通知前保存
let last_used = used.idx;
fence();
regs.write32(REG_QUEUE_NOTIFY, 0);
loop { if used.idx != last_used { break } } // 变了就退出
```

***

## Makefile `?=` 尾随空格导致 ifeq 不匹配

**症状：** `CONFIG_DEBUG ?= y` 设了但 `-DDEBUG=1` 没生效。

**原因：** `y  # comment` 中 `y` 后面的两个空格被 `?=` 当作值的一部分。`ifeq ($(CONFIG_DEBUG),y)` 比的是 `"y  "` ≠ `"y"`，永不匹配。

```makefile
# 错误：空格被吞进值里
CONFIG_DEBUG ?= y  # comment  → 值="y  "

# 正确：y 紧贴 #
CONFIG_DEBUG ?= y# comment    → 值="y"
```

***

## `git clean -fd` 删未跟踪源码文件

**现象：** `git clean -fd` 删掉了 `virtio.rs`、`fs/` 目录、`elf.rs`、`user_bin/` 等未跟踪但正在用的文件。

**原因：** `git clean` 只删未跟踪文件（不在 git 里但存在于工作目录的）。在 git 里用 `git status` 先看清楚哪些文件是 untracked 但重要的。

```bash
# 查看未跟踪文件（绿色）
git status

# 只删 .o/.elf 等构建产物
git clean -fd -e "*.rs" -e "*.S" -e "*.ld"
# 或更安全：先 dry-run
git clean -fdn
```

**教训：** 永远先 `git clean -fdn`（dry-run）预览再执行。不熟悉的项目慎用 `-fd`。

***

## `copy_user_page_table` 遍历脏页表项崩溃

**症状：** fork 时 `copy_user_page_table` 在 `phys_to_virt(0)` 处 data abort。

**原因：** 页表页分配后未清零时，残留数据中某些位恰好 `VALID=1` 但物理地址为 0。`copy_user_page_table` 遍历到这些项时：

```c
if (!pte_present(src_l2[idx2])) continue;  // 误判为 present
phys_addr_t pa = pte_to_phys(src_l2[idx2]); // pa = 0
phys_to_virt(pa); // VIRT_BASE + 0 = 未映射区域 → data abort
```

**修复：** `arm64_map_one_page` 中每级页表分配后 `memset(page, 0, 4096)`。`copy_user_page_table` 中每级遍历加 `if (!pa) continue;` 保护。

***

## 调度器初始化顺序：FS 先于用户进程

**症状：** `[KERN] Failed to read SHELL.ELF: no mount for path`

**原因：** `rust_main` 中第 3 步启动用户进程、第 5 步才挂载 FAT32。ELF 加载时文件系统还没挂上。

```rust
// 错误顺序
step3: start_user_process();   // 读 SHELL.ELF → 找不到挂载点
step5: mount("/", fat32_fs);   // 挂载在用户进程之后

// 正确顺序
step3: virtio_init();
step4: mount("/", fat32_fs);
step5: start_user_process();   // 此时 FS 已就绪
```

***

## `ioremap` 非页对齐地址

参见上文第 261 行已记录 `ioremap 非页对齐地址`。

***

