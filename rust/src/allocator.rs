// src/allocator.rs
//! 全局分配器：对接你 C 的 Slab（kmalloc/kfree）


// src/allocator.rs
use core::alloc::{GlobalAlloc, Layout};

pub struct KernelAllocator;

unsafe impl GlobalAlloc for KernelAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe {
            crate::ffi::kmalloc(layout.size())
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe {
            crate::ffi::kfree(ptr, layout.size());
        }
    }
}


// 把这个分配器设为全局分配器（Rust 的 alloc 就会走这里）
#[global_allocator]
static ALLOCATOR: KernelAllocator = KernelAllocator;

// 不再需要 #[alloc_error_handler]：
// - no_std + panic=abort 时，OOM 会被 handle_alloc_error 转成 panic，然后 abort。

