use alloc::collections::VecDeque;
use alloc::vec::Vec;
use crate::kernel::wait::{WaitQueue, wait_event, wake_up};
use core::sync::atomic::{AtomicBool, AtomicI32, Ordering};

/// 单个块 I/O 请求
pub struct Bio {
    pub sector: u64,
    pub buf: *mut u8,
    pub len: u32,
    pub is_write: bool,
    pub wq: WaitQueue,
    pub done: AtomicBool,
    pub result: AtomicI32,
}

unsafe impl Send for Bio {}
unsafe impl Sync for Bio {}

impl Bio {
    pub fn new(sector: u64, buf: *mut u8, len: u32, is_write: bool) -> Self {
        Bio {
            sector,
            buf,
            len,
            is_write,
            wq: WaitQueue::new(),
            done: AtomicBool::new(false),
            result: AtomicI32::new(0),
        }
    }

    pub fn wait(&self) -> i32 {
        if self.done.load(Ordering::Acquire) { return self.result.load(Ordering::Relaxed); }
        wait_event(&self.wq, || self.done.load(Ordering::Acquire));
        self.result.load(Ordering::Relaxed)
    }

    pub fn complete(&self, result: i32) {
        self.result.store(result, Ordering::Relaxed);
        self.done.store(true, Ordering::Release);
        wake_up(&self.wq);
    }
}

/// I/O 请求队列
pub struct BioQueue {
    pending: VecDeque<Bio>,
}

impl BioQueue {
    pub const fn new() -> Self {
        BioQueue {
            pending: VecDeque::new(),
        }
    }

    pub fn push(&mut self, bio: Bio) {
        self.pending.push_back(bio);
    }

    pub fn pop_pending(&mut self, max: usize) -> Vec<Bio> {
        let count = core::cmp::min(self.pending.len(), max);
        self.pending.drain(..count).collect()
    }

    pub fn has_pending(&self) -> bool {
        !self.pending.is_empty()
    }
}
