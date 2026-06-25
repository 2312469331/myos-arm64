/// TTY 模块：环形缓冲区 + 行规程 + 等待队列
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use crate::kernel::wait::{WaitQueue, wait_event, wake_up};

/// 缓冲区大小
const INPUT_BUF_SIZE: usize = 256;
const LINE_BUF_SIZE: usize = 256;

/// 特殊字符
const CTRL_C: u8 = 3;
const CTRL_D: u8 = 4;
const CTRL_Z: u8 = 26;
const BACKSPACE: u8 = 8;
const DEL: u8 = 127;
const CR: u8 = 13;
const LF: u8 = 10;

/// TTY 内部可变状态
struct TtyInner {
    /// 行缓冲区
    line_buf: [u8; LINE_BUF_SIZE],
    line_len: usize,
}

/// TTY 实例
pub struct Tty {
    /// 输入环形缓冲区（环形无需 UnsafeCell，用 atomic 操作）
    buf: UnsafeCell<[u8; INPUT_BUF_SIZE]>,
    head: AtomicUsize,
    tail: AtomicUsize,
    /// 行缓冲区（只有 input_char 调用者访问，单生产者）
    inner: UnsafeCell<TtyInner>,
    /// 行就绪标志
    line_ready: AtomicBool,
    /// 前台进程组 PID
    foreground_pgid: AtomicUsize,
    /// 等待输入的进程队列
    wq: WaitQueue,
}

unsafe impl Sync for Tty {}

/// 单例 TTY
pub static TTY0: Tty = Tty::new();

impl Tty {
    pub const fn new() -> Self {
        Self {
            buf: UnsafeCell::new([0u8; INPUT_BUF_SIZE]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
            inner: UnsafeCell::new(TtyInner {
                line_buf: [0u8; LINE_BUF_SIZE],
                line_len: 0,
            }),
            line_ready: AtomicBool::new(false),
            foreground_pgid: AtomicUsize::new(0),
            wq: WaitQueue::new(),
        }
    }

    fn put_byte(&self, byte: u8) -> bool {
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Relaxed);
        let next = (head + 1) % INPUT_BUF_SIZE;
        if next == tail {
            return false;
        }
        unsafe { (*self.buf.get())[head] = byte; }
        self.head.store(next, Ordering::Release);
        true
    }

    fn get_byte(&self) -> Option<u8> {
        let tail = self.tail.load(Ordering::Relaxed);
        let head = self.head.load(Ordering::Acquire);
        if tail == head {
            return None;
        }
        let byte = unsafe { (*self.buf.get())[tail] };
        self.tail.store((tail + 1) % INPUT_BUF_SIZE, Ordering::Release);
        Some(byte)
    }

    /// UART IRQ 调用：接收字符并处理行规程
    pub fn input_char(&self, ch: u8) {
        // Ctrl+C
        if ch == CTRL_C {
            let pgid = self.foreground_pgid.load(Ordering::Relaxed);
            if pgid != 0 {
                if let Some(task) = crate::kernel::scheduler::find_task_by_pid(pgid) {
                    task.signals.send(crate::kernel::signal::SIGINT);
                }
            }
            uart_print_str("\r\n^C\r\n");
            return;
        }

        // Ctrl+Z
        if ch == CTRL_Z {
            let pgid = self.foreground_pgid.load(Ordering::Relaxed);
            if pgid != 0 {
                if let Some(task) = crate::kernel::scheduler::find_task_by_pid(pgid) {
                    task.signals.send(crate::kernel::signal::SIGSTOP);
                }
            }
            uart_print_str("\r\n^Z\r\n");
            return;
        }

        if ch == CTRL_D {
            return;
        }

        // 退格/DEL
        if ch == BACKSPACE || ch == DEL {
            let inner = unsafe { &mut *self.inner.get() };
            if inner.line_len > 0 {
                inner.line_len -= 1;
                uart_print_str("\x08 \x08");
            }
            return;
        }

        // 回车/换行：提交行
        if ch == CR || ch == LF {
            self.line_ready.store(true, Ordering::Release);
            // 唤醒等待输入的进程（UART IRQ 上下文，安全：wait_event 已 drop lock）
            wake_up(&self.wq);
            uart_putc(b'\r');
            uart_putc(b'\n');
            return;
        }

        // 普通字符：放入行缓冲区并回显
        let inner = unsafe { &mut *self.inner.get() };
        if inner.line_len < LINE_BUF_SIZE - 1 {
            inner.line_buf[inner.line_len] = ch;
            inner.line_len += 1;
            uart_putc(ch);
        }
    }

    /// 从行缓冲区读取一行（可被信号中断）
    /// 使用 wait_event 实现非阻塞调度：
    /// - 有数据或有致命信号 → 立即返回
    /// - 无数据 → 让出 CPU，UART IRQ 收到数据后 wake_up 唤醒
    /// 返回 0 表示被信号中断
    pub fn read_line(&self, buf: &mut [u8]) -> usize {
        // 等待行数据就绪或被信号中断
        wait_event(&self.wq, || {
            if self.line_ready.load(Ordering::Acquire) {
                return true;
            }
            // 检查致命信号（Ctrl+C / kill -9）
            if let Some(task) = crate::kernel::task::current_task_ref() {
                if task.signals.has_fatal_pending() {
                    return true;
                }
            }
            false
        });

        // 被信号中断且无数据
        if !self.line_ready.load(Ordering::Acquire) {
            if let Some(task) = crate::kernel::task::current_task_ref() {
                if task.signals.has_fatal_pending() {
                    return 0;
                }
            }
        }

        let inner = unsafe { &mut *self.inner.get() };
        let len = inner.line_len;
        let copy_len = core::cmp::min(len, buf.len());
        buf[..copy_len].copy_from_slice(&inner.line_buf[..copy_len]);
        inner.line_len = 0;
        self.line_ready.store(false, Ordering::Relaxed);
        copy_len
    }

    /// 写入输出
    pub fn write(&self, buf: &[u8]) -> usize {
        for &ch in buf {
            uart_putc(ch);
        }
        buf.len()
    }

    /// 设置前台进程组
    pub fn set_foreground(&self, pgid: usize) {
        self.foreground_pgid.store(pgid, Ordering::Relaxed);
    }

    pub fn get_foreground(&self) -> usize {
        self.foreground_pgid.load(Ordering::Relaxed)
    }
}

fn uart_putc(ch: u8) {
    unsafe {
        unsafe extern "C" { fn uart_putc(c: u8); }
        uart_putc(ch);
    }
}

fn uart_print_str(s: &str) {
    for &ch in s.as_bytes() {
        uart_putc(ch);
    }
}

pub fn tty_init() {
    TTY0.set_foreground(1);
}

/// C FFI：UART IRQ 调用
#[unsafe(no_mangle)]
pub extern "C" fn rust_tty_input_char(ch: u8) {
    TTY0.input_char(ch);
}
