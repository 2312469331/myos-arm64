/// 内存管理模块：VMA 用 BTreeMap 管理
use alloc::collections::BTreeMap;
use alloc::boxed::Box;
use alloc::vec::Vec;

use crate::ffi;
use crate::sync::KMutex;
use crate::kernel::phys_page::PhysPage;

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
    lock: KMutex<()>,
    pub vmas: BTreeMap<u64, VmaArea>,
    pub start_brk: u64,
    pub brk: u64,
    pub start_stack: u64,
}

impl MmStruct {
    pub fn new() -> Self {
        Self {
            lock: KMutex::new(()),
            vmas: BTreeMap::new(),
            start_brk: 0,
            brk: 0,
            start_stack: 0,
        }
    }


    #[inline(never)]
    pub fn copy_from(src: &MmStruct) -> Self {
        let _g = src.lock.lock();
        let mut dst = MmStruct::new();
        dst.start_brk = src.start_brk;
        dst.brk = src.brk;
        dst.start_stack = src.start_stack;

        let entries: Vec<_> = src.vmas.iter().map(|(k, v)| (*k, v.clone())).collect();
        for (key, vma) in entries {
            dst.vmas.insert(key, vma);
        }
        drop(_g);

        dst
    }

    pub fn vma_find(&self, addr: u64) -> Option<VmaArea> {
        let lock = &self.lock;
        let _g = lock.lock();
        self.vmas.range(..=addr).next_back()
            .map(|(_, vma)| vma.clone())
            .filter(|vma| addr < vma.vm_end)
    }

    pub fn vma_create(&mut self, vm_start: u64, vm_end: u64, vm_flags: u64) -> Result<(), i32> {
        if vm_start >= vm_end {
            return Err(-1);
        }
        {
            let lock = &self.lock;
            let _g = lock.lock();
            if let Some((_, vma)) = self.vmas.range(..vm_end).next_back() {
                if vma.vm_end > vm_start {
                    return Err(-1);
                }
            }
            self.vmas.insert(vm_start, VmaArea { vm_start, vm_end, vm_flags });
        }
        Ok(())
    }

    pub fn vma_remove(&mut self, vm_start: u64) -> Option<VmaArea> {
        let __lk = &self.lock;
        let _g = __lk.lock();
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

        let lock = &self.lock;
        let _g = lock.lock();
        let addr = if addr == 0 {
            self.find_free_area(len)
        } else {
            addr
        };
        if let Some((_, vma)) = self.vmas.range(..addr + len).next_back() {
            if vma.vm_end > addr {
                return 0;
            }
        }
        self.vmas.insert(addr, VmaArea {
            vm_start: addr,
            vm_end: addr + len,
            vm_flags: flags | VM_ANONYMOUS,
        });
        drop(_g);

        let mut cur = addr;
        while cur < addr + len {
            let page = match PhysPage::alloc() {
                Some(p) => p,
                None => {
                    while cur > addr {
                        cur -= 4096;
                        let rpa = unsafe { ffi::get_phys_from_va(cur as usize) };
                        unsafe { ffi::unmap_page(cur as usize) };
                        if rpa != 0 { unsafe { ffi::put_page(rpa) }; }
                    }
                    let lock = &self.lock;
                    let _g = lock.lock();
                    self.vmas.remove(&addr);
                    return 0;
                }
            };
            let pa = page.pa();
            let va = unsafe { crate::kernel::pgtbl::phys_to_virt(pa) };
            unsafe { core::ptr::write_bytes(va as *mut u8, 0, 4096); }
            let ret = unsafe { ffi::map_page(cur as usize, pa, crate::kernel::pgtbl::PAGE_USER_RW) };
            if ret != 0 {
                drop(page);  // auto free
                while cur > addr {
                    cur -= 4096;
                    let rpa = unsafe { ffi::get_phys_from_va(cur as usize) };
                    unsafe { ffi::unmap_page(cur as usize) };
                    if rpa != 0 { unsafe { ffi::put_page(rpa) }; }
                }
                let lock = &self.lock;
                let _g = lock.lock();
                self.vmas.remove(&addr);
                return 0;
            }
            page.leak();  // 所有权转给页表
            cur += 4096;
        }
        addr
    }

    pub fn do_munmap(&mut self, addr: u64, len: u64) -> i32 {
        if len == 0 {
            return -1;
        }
        let len = (len + 4095) & !4095;

        let vma_end = {
            let lock = &self.lock;
            let _g = lock.lock();
            let vma_end = self.vmas.range(..=addr).next_back()
                .map(|(_, vma)| vma.clone())
                .filter(|vma| addr < vma.vm_end)
                .map(|vma| vma.vm_end);
            let vma_end = match vma_end {
                Some(end) => end,
                None => return -1,
            };
            let vm_start = self.vmas.range(..=addr).next_back()
                .map(|(_, vma)| vma.clone())
                .filter(|vma| addr < vma.vm_end)
                .map(|vma| vma.vm_start)
                .unwrap_or(0);
            self.vmas.remove(&vm_start);
            vma_end
        };

        let mut cur = addr;
        while cur < addr + len && cur < vma_end {
            let pa = unsafe { ffi::get_phys_from_va(cur as usize) };
            unsafe { ffi::unmap_page(cur as usize) };
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
            let vm_flags = VM_READ | VM_WRITE | VM_ANONYMOUS;
            {
                let lock = &self.lock;
                let _g = lock.lock();
                if let Some((_, vma)) = self.vmas.range(..new_brk).next_back() {
                    if vma.vm_end > self.brk {
                        return self.brk;
                    }
                }
                self.vmas.insert(self.brk, VmaArea {
                    vm_start: self.brk,
                    vm_end: new_brk,
                    vm_flags,
                });
            }
        } else {
            {
                let lock = &self.lock;
                let _g = lock.lock();
                self.vmas.remove(&self.brk);
            }
            let mut addr = new_brk;
            while addr < self.brk {
                let pa = unsafe { ffi::get_phys_from_va(addr as usize) };
                unsafe { ffi::unmap_page(addr as usize) };
                if pa != 0 {
                    unsafe { ffi::put_page(pa) };
                }
                addr += 4096;
            }
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
