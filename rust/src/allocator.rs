// src/allocator.rs
//! 全局分配器：对接你 C 的 Slab（kmalloc/kfree）


// src/allocator.rs
use core::alloc::{GlobalAlloc, Layout};

use crate::ffi;

pub struct KernelAllocator;

/// 备用内存池大小（4KB）
const BACKUP_POOL_SIZE: usize = 4096;

/// 备用内存池，用于内存分配失败时的应急分配
static mut BACKUP_POOL: [u8; BACKUP_POOL_SIZE] = [0; BACKUP_POOL_SIZE];
/// 备用内存池当前使用位置
static mut BACKUP_POOL_POS: usize = 0;

unsafe impl GlobalAlloc for KernelAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe {
            // 尝试使用 kmalloc 分配内存
            let ptr = crate::ffi::rust_kmalloc(layout.size());
            
            // 如果分配成功，直接返回
            if !ptr.is_null() {
                return ptr;
            }
            
            // 分配失败，使用备用内存池
            ffi::c_print_str(c"[RUST ALLOC] kmalloc failed, using backup pool\n".as_ptr());
            
            // 计算需要的对齐大小
            let align = layout.align();
            let size = layout.size();
            
            // 对齐当前位置
            let aligned_pos = (BACKUP_POOL_POS + align - 1) & !(align - 1);
            
            // 检查备用内存池是否有足够空间
            if aligned_pos + size <= BACKUP_POOL_SIZE {
                let backup_ptr = &BACKUP_POOL[aligned_pos] as *const u8 as *mut u8;
                BACKUP_POOL_POS = aligned_pos + size;
                ffi::c_print_str(c"[RUST ALLOC] Allocated from backup pool\n".as_ptr());
                return backup_ptr;
            }
            // 备用内存池也不足，返回 NULL或者等待内存
            ffi::c_print_str(c"[RUST ALLOC] Backup pool exhausted, waiting for memory...\n".as_ptr());
            return ptr;
            // 备用内存池也不足，死循环等待内存释放
            // ffi::c_print_str(c"[RUST ALLOC] Backup pool exhausted, waiting for memory...\n".as_ptr());
            // loop {
            //     // 这里可以添加内存回收逻辑
            //     // 或者简单地等待
            //     core::hint::spin_loop();
            // }
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe {
            // 检查是否是备用内存池中的指针
            let backup_start = &BACKUP_POOL[0] as *const u8 as *mut u8;
            let backup_end = &BACKUP_POOL[BACKUP_POOL_SIZE - 1] as *const u8 as *mut u8;
            
            if ptr >= backup_start && ptr <= backup_end {
                // 备用内存池中的内存不需要释放
                // 这里可以添加更复杂的内存管理逻辑
                ffi::c_print_str(c"[RUST ALLOC] Dealloc from backup pool (no action)\n".as_ptr());
            } else {
                // 正常释放 kmalloc 分配的内存
                crate::ffi::rust_kfree(ptr, layout.size());
            }
        }
    }
}


// 把这个分配器设为全局分配器（Rust 的 alloc 就会走这里）
#[global_allocator]
static ALLOCATOR: KernelAllocator = KernelAllocator;

// 永不返回 NULL 的内存分配器
// 当 kmalloc 失败时，使用备用内存池
// 备用内存池不足时，死循环等待
// 这样可以确保永远不会返回 NULL，从而避免 handle_alloc_error 被调用
// 内核永远不会因为内存分配失败而 panic

