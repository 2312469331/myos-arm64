// src/allocator.rs
//! 全局分配器：对接 C 的 Slab（kmalloc/kfree），大块回退到 buddy 物理页分配

use core::alloc::{GlobalAlloc, Layout};
use crate::ffi;

pub struct KernelAllocator;

/// 大块分配阈值：超过此值直接使用 buddy 分配器（物理页）
const LARGE_THRESHOLD: usize = 65536; // 64KB

/// 记录已分配的物理页块链表头（简易实现，不支持释放大块）
/// 对于 ELF 加载这类一次性使用场景已足够
const MAX_PAGE_ALLOCS: usize = 1024;
struct PageAlloc {
    ptr: *mut u8,
    pages: u32,
}
static mut PAGE_ALLOCS: [core::mem::MaybeUninit<PageAlloc>; MAX_PAGE_ALLOCS] =
    unsafe { core::mem::MaybeUninit::uninit().assume_init() };
static mut PAGE_ALLOC_COUNT: usize = 0;

unsafe impl GlobalAlloc for KernelAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe {
            // 大块分配：直接使用 buddy 分配物理页
            if layout.size() >= LARGE_THRESHOLD {
                let pages = (layout.size() + 4095) / 4096;
                let order = (pages.next_power_of_two().trailing_zeros() as u32).min(10);
                let pa = ffi::alloc_phys_pages(order, 0);
                if pa != 0 {
                    let va = crate::kernel::pgtbl::phys_to_virt(pa) as *mut u8;
                    if PAGE_ALLOC_COUNT < MAX_PAGE_ALLOCS {
                        PAGE_ALLOCS[PAGE_ALLOC_COUNT] = core::mem::MaybeUninit::new(PageAlloc {
                            ptr: va, pages: 1u32 << order,
                        });
                        PAGE_ALLOC_COUNT += 1;
                    }
                    return va;
                }
                // 如果大块分配失败，尝试 kmalloc 保底
            }

            // 尝试使用 kmalloc 分配内存
            let ptr = crate::ffi::rust_kmalloc(layout.size());
            if !ptr.is_null() {
                return ptr;
            }

            // kmalloc 也失败，返回 NULL
            ffi::c_print_str(c"[RUST ALLOC] kmalloc failed, returning NULL\n".as_ptr());
            core::ptr::null_mut()
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe {
            // 检查是否是物理页分配
            for i in 0..PAGE_ALLOC_COUNT {
                let pa_ref = &*PAGE_ALLOCS[i].as_ptr();
                if pa_ref.ptr == ptr {
                    let pages = (layout.size() + 4095) / 4096;
                    let order = (pages.next_power_of_two().trailing_zeros() as u32).min(10);
                    let phys = crate::kernel::pgtbl::virt_to_phys(ptr as usize);
                    ffi::free_phys_pages(phys as u64, order);
                    return;
                }
            }
            // 正常释放 kmalloc 分配的内存
            crate::ffi::rust_kfree(ptr, layout.size());
        }
    }
}

#[global_allocator]
static ALLOCATOR: KernelAllocator = KernelAllocator;
