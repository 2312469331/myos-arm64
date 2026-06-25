/// 内存管理模块：VMA 用 BTreeMap 管理
use alloc::collections::BTreeMap;
use alloc::boxed::Box;
use alloc::vec::Vec;

use crate::ffi;

/// mmap 起始地址（用户空间内）
const MMAP_BASE: u64 = 0xffff000010000000;

/// VMA 标志位
pub const VM_READ: u64 = 1 << 0;
pub const VM_WRITE: u64 = 1 << 1;
pub const VM_EXEC: u64 = 1 << 2;
pub const VM_ANONYMOUS: u64 = 1 << 3;

/// 虚拟内存区域
#[derive(Clone)]
pub struct VmaArea {
    pub vm_start: u64,
    pub vm_end: u64,
    pub vm_flags: u64,
}

/// 进程地址空间
pub struct MmStruct {
    pub vmas: BTreeMap<u64, VmaArea>,
    pub start_brk: u64,
    pub brk: u64,
    pub start_stack: u64,
}

impl MmStruct {
    pub fn new() -> Self {
        Self {
            vmas: BTreeMap::new(),
            start_brk: 0,
            brk: 0,
            start_stack: 0,
        }
    }


    #[inline(never)]
    pub fn copy_from(src: &MmStruct) -> Self {
        let mut dst = MmStruct::new();
        dst.start_brk = src.start_brk;
        dst.brk = src.brk;
        dst.start_stack = src.start_stack;

        // 先把 src 的 VMA 条目全部读到 Vec 里，iter 完成后才进行 dst 的 insert
        // 防止 insert 分配的堆内存覆盖 src 的 BTreeMap 节点
        let entries: Vec<_> = src.vmas.iter().map(|(k, v)| (*k, v.clone())).collect();
        for (key, vma) in entries {
            dst.vmas.insert(key, vma);
        }

        dst
    }

    pub fn vma_find(&self, addr: u64) -> Option<&VmaArea> {
        self.vmas.range(..=addr).next_back()
            .map(|(_, vma)| vma)
            .filter(|vma| addr < vma.vm_end)
    }

    pub fn vma_create(&mut self, vm_start: u64, vm_end: u64, vm_flags: u64) -> Result<(), i32> {
        if vm_start >= vm_end {
            return Err(-1);
        }
        if let Some((_, vma)) = self.vmas.range(..vm_end).next_back() {
            if vma.vm_end > vm_start {
                return Err(-1);
            }
        }
        self.vmas.insert(vm_start, VmaArea { vm_start, vm_end, vm_flags });
        Ok(())
    }

    pub fn vma_remove(&mut self, vm_start: u64) -> Option<VmaArea> {
        self.vmas.remove(&vm_start)
    }

    fn find_free_area(&self, len: u64) -> u64 {
        let mut addr = MMAP_BASE;
        loop {
            let mut conflict = false;
            for (_, vma) in self.vmas.iter() {
                if addr < vma.vm_end && addr + len > vma.vm_start {
                    conflict = true;
                    addr = (vma.vm_end + 4095) & !4095;
                    break;
                }
            }
            if !conflict {
                return addr;
            }
        }
    }

    pub fn do_mmap(&mut self, addr: u64, len: u64, flags: u64) -> u64 {
        if len == 0 {
            return 0;
        }
        let len = (len + 4095) & !4095;

        let addr = if addr == 0 {
            self.find_free_area(len)
        } else {
            addr
        };

        if self.vma_create(addr, addr + len, flags | VM_ANONYMOUS).is_err() {
            return 0;
        }

        let mut cur = addr;
        while cur < addr + len {
            let pa = unsafe { ffi::alloc_phys_pages(0, 0) };
            if pa == 0 {
                while cur > addr {
                    cur -= 4096;
                    let rpa = unsafe { ffi::arm64_get_phys_from_va(cur as usize) };
                    unsafe { ffi::arm64_unmap_one_page(cur as usize) };
                    if rpa != 0 {
                        unsafe { ffi::put_page(rpa) };
                    }
                }
                self.vma_remove(addr);
                return 0;
            }
            let va = unsafe { crate::kernel::pgtbl::phys_to_virt(pa) };
            unsafe {
                core::ptr::write_bytes(va as *mut u8, 0, 4096);
            }
            let ret = unsafe { ffi::arm64_map_one_page(cur as usize, pa, crate::kernel::pgtbl::PAGE_USER_RW) };
            if ret != 0 {
                unsafe { ffi::free_phys_pages(pa, 0) };
                while cur > addr {
                    cur -= 4096;
                    let rpa = unsafe { ffi::arm64_get_phys_from_va(cur as usize) };
                    unsafe { ffi::arm64_unmap_one_page(cur as usize) };
                    if rpa != 0 {
                        unsafe { ffi::put_page(rpa) };
                    }
                }
                self.vma_remove(addr);
                return 0;
            }
            cur += 4096;
        }
        addr
    }

    pub fn do_munmap(&mut self, addr: u64, len: u64) -> i32 {
        if len == 0 {
            return -1;
        }
        let len = (len + 4095) & !4095;

        let vma_end = match self.vma_find(addr) {
            Some(vma) => vma.vm_end,
            None => return -1,
        };

        let vm_start = self.vma_find(addr).map(|v| v.vm_start).unwrap_or(0);
        self.vma_remove(vm_start);

        let mut cur = addr;
        while cur < addr + len && cur < vma_end {
            let pa = unsafe { ffi::arm64_get_phys_from_va(cur as usize) };
            unsafe { ffi::arm64_unmap_one_page(cur as usize) };
            if pa != 0 {
                unsafe { ffi::put_page(pa) };
            }
            cur += 4096;
        }
        0
    }

    pub fn do_brk(&mut self, new_brk: u64) -> u64 {
        let new_brk = (new_brk + 4095) & !4095;
        if new_brk == self.brk {
            return self.brk;
        }

        if new_brk > self.brk {
            // 扩展：只创建 VMA，不分配物理页（缺页时按需分配）
            let vm_flags = VM_READ | VM_WRITE | VM_ANONYMOUS;
            if self.vma_create(self.brk, new_brk, vm_flags).is_err() {
                return self.brk;
            }
        } else {
            // 收缩：释放物理页并移除 VMA
            let mut addr = new_brk;
            while addr < self.brk {
                let pa = unsafe { ffi::arm64_get_phys_from_va(addr as usize) };
                unsafe { ffi::arm64_unmap_one_page(addr as usize) };
                if pa != 0 {
                    unsafe { ffi::put_page(pa) };
                }
                addr += 4096;
            }
            let _ = self.vma_remove(self.brk);
            // 也可能需要调整最后一个 VMA 的结束地址
        }

        self.brk = new_brk;
        self.brk
    }
}

/// 从 C mm_struct 指针获取 Rust MmStruct 引用
pub unsafe fn mm_from_ptr(mm: *mut ffi::MmStruct) -> &'static mut MmStruct {
    unsafe { &mut *(mm as *mut MmStruct) }
}

/// C FFI：page_fault.c 调用，检查地址是否在某个 VMA 内
/// 返回 vm_flags（合法），0 = 非法/不在任何 VMA 内
#[unsafe(no_mangle)]
pub extern "C" fn rust_vma_find(mm: *mut ffi::MmStruct, addr: u64) -> u64 {
    if mm.is_null() {
        return 0;
    }
    let mm = unsafe { mm_from_ptr(mm) };
    match mm.vma_find(addr) {
        Some(vma) => vma.vm_flags,
        None => 0,
    }
}
