/// 内核配置项
/// 修改此文件后重新编译即可生效，无需改代码

/// 调度器调试：每次 context switch 打印当前/下一个任务的寄存器信息
pub const SCHED_DEBUG: bool = false;

/// 系统调用调试：打印 SYS_READ/SYS_WRITE 的参数和返回值
pub const SYSCALL_DEBUG: bool = false;

/// 页错误调试：打印 fault 地址详情
pub const PAGE_FAULT_DEBUG: bool = false;

/// 信号调试：打印信号发送和处理细节
pub const SIGNAL_DEBUG: bool = false;

/// TTY 调试：打印每个输入字符
pub const TTY_DEBUG: bool = false;

/// fork 调试：打印 fork 各步骤和页表诊断
pub const FORK_DEBUG: bool = false;

/// 内存管理调试：打印 VMA 复制等细节
pub const MM_DEBUG: bool = false;

/// 时间片长度（tick 数），默认 50 ticks
/// 实际时间 = ticks / TIMER_HZ 秒
/// 例：50 ticks @ 100Hz = 0.5 秒
pub const TIME_SLICE_TICKS: i32 = 3;

/// 定时器中断频率（Hz），默认 100（每 10ms 一次）
pub const TIMER_HZ: u64 = 1;
