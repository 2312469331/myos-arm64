/// 信号模块
use core::sync::atomic::{AtomicU64, Ordering};

/// 信号号
pub const SIGHUP: usize = 1;
pub const SIGINT: usize = 2;
pub const SIGQUIT: usize = 3;
pub const SIGKILL: usize = 9;
pub const SIGSEGV: usize = 11;
pub const SIGTERM: usize = 15;
pub const SIGUSR1: usize = 10;
pub const SIGUSR2: usize = 12;
pub const SIGCHLD: usize = 17;
pub const SIGSTOP: usize = 19;
pub const SIGCONT: usize = 18;

pub const MAX_SIGNALS: usize = 64;

/// 信号默认动作
#[derive(Clone, Copy, PartialEq)]
pub enum SigAction {
    Terminate,   // 终止进程
    Ignore,      // 忽略
    Core,        // 终止 + core dump（暂等同 Terminate）
    Stop,        // 暂停进程
    Cont,        // 继续进程
    Handler,     // 用户自定义 handler
}

/// 获取信号默认动作
pub fn default_action(sig: usize) -> SigAction {
    match sig {
        SIGHUP | SIGINT | SIGKILL | SIGTERM | SIGUSR1 | SIGUSR2 => SigAction::Terminate,
        SIGQUIT | SIGSEGV => SigAction::Core,
        SIGSTOP => SigAction::Stop,
        SIGCONT => SigAction::Cont,
        SIGCHLD => SigAction::Ignore,
        _ => SigAction::Terminate,
    }
}

/// 进程信号掩码（pending + blocked + handler）
pub struct SignalState {
    /// 待处理信号位图（每个 bit 代表一个信号）
    pub pending: AtomicU64,
    /// 已屏蔽信号位图
    pub blocked: AtomicU64,
    /// 用户自定义 handler 地址（0 = 使用默认动作）
    pub handler: [AtomicU64; MAX_SIGNALS],
}

impl SignalState {
    pub const fn new() -> Self {
        // 用 const fn 初始化 AtomicU64 数组
        Self {
            pending: AtomicU64::new(0),
            blocked: AtomicU64::new(0),
            handler: const { {
                // workaround: 用一个临时数组
                let mut arr: [AtomicU64; MAX_SIGNALS] = unsafe { core::mem::zeroed() };
                let mut i = 0;
                while i < MAX_SIGNALS {
                    arr[i] = AtomicU64::new(0);
                    i += 1;
                }
                arr
            } },
        }
    }

    /// 发送信号：设置 pending 位
    pub fn send(&self, sig: usize) {
        if sig == 0 || sig >= MAX_SIGNALS {
            return;
        }
        self.pending.fetch_or(1u64 << sig, Ordering::Relaxed);
    }

    /// 屏蔽信号
    pub fn block(&self, sig: usize) {
        if sig == 0 || sig >= MAX_SIGNALS {
            return;
        }
        self.blocked.fetch_or(1u64 << sig, Ordering::Relaxed);
    }

    /// 取消屏蔽
    pub fn unblock(&self, sig: usize) {
        if sig == 0 || sig >= MAX_SIGNALS {
            return;
        }
        self.blocked.fetch_and(!(1u64 << sig), Ordering::Relaxed);
    }

    /// 检查是否有未被屏蔽的待处理信号，返回信号号（0=无）
    pub fn check_pending(&self) -> usize {
        let pending = self.pending.load(Ordering::Relaxed);
        let blocked = self.blocked.load(Ordering::Relaxed);
        let ready = pending & !blocked;
        if ready == 0 {
            return 0;
        }
        // 返回最低位的信号号
        ready.trailing_zeros() as usize
    }

    /// 清除指定信号的 pending 位
    pub fn clear_pending(&self, sig: usize) {
        if sig < MAX_SIGNALS {
            self.pending.fetch_and(!(1u64 << sig), Ordering::Relaxed);
        }
    }

    /// 清除所有 pending
    pub fn clear_all_pending(&self) {
        self.pending.store(0, Ordering::Relaxed);
    }

    /// 检查是否有需要终止/停止进程的未屏蔽信号（用于中断阻塞操作）
    pub fn has_fatal_pending(&self) -> bool {
        let pending = self.pending.load(Ordering::Relaxed);
        let blocked = self.blocked.load(Ordering::Relaxed);
        let ready = pending & !blocked;
        if ready == 0 {
            return false;
        }
        // 检查每个 pending 信号的默认动作
        let mut mask = 1u64;
        for sig in 0..MAX_SIGNALS {
            if ready & mask != 0 {
                match default_action(sig) {
                    SigAction::Terminate | SigAction::Core | SigAction::Stop => return true,
                    _ => {}
                }
            }
            mask <<= 1;
        }
        false
    }
}
