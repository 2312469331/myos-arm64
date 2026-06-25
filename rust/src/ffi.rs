// src/ffi.rs
//! 与 C 底层交互的 FFI 声明

#![allow(unused)]

unsafe extern "C" {
    // ---- 打印 ----
    pub fn c_print_str(s: *const core::ffi::c_char);

    // ---- 内存分配 ----
    pub fn rust_kmalloc(size: usize) -> *mut u8;
    pub fn rust_kfree(ptr: *mut u8, size: usize);
    pub fn alloc_phys_pages(order: u32, flags: u32) -> u64;
    pub fn free_phys_pages(pa: u64, order: u32);
    pub fn get_page(pa: u64);
    pub fn put_page(pa: u64);

    // ---- 页表操作 ----
    /// 映射一个 4KB 页面（va → pa），自动根据地址选择 TTBR0/TTBR1
    pub fn arm64_map_one_page(va: usize, pa: u64, prot: u64) -> i32;
    /// 取消一个 4KB 页面映射
    pub fn arm64_unmap_one_page(va: usize);
    /// 从页表中读取虚拟地址对应的物理地址，未映射返回 0
    pub fn arm64_get_phys_from_va(va: usize) -> u64;
    /// 复制用户态页表树（共享物理页，用于 fork）
    pub fn copy_user_page_table(src_pgd_pa: u64, dst_pgd_pa: u64);
    /// 递归释放整个页表树（只释放页表页，不释放映射的数据页）
    pub fn free_page_table_tree(pgd_pa: u64);
    /// 调试：打印页表树状结构
    pub fn dump_page_table(pgd_pa: u64);
    /// 切换 TTBR0_EL1（用户态页表）
    pub fn switch_ttbr0(ttbr0_pa: u64);
    /// 读取当前 TTBR0_EL1
    pub fn read_ttbr0_el1() -> u64;
    /// 读取当前 TTBR1_EL1
    pub fn read_ttbr1_el1() -> u64;
    /// 刷新 TLB
    pub fn flush_tlb();

    // ---- 全局变量 ----
    /// 当前 L0 页表物理地址
    pub static mut slab_l0_table_pa: u64;
    /// 线性映射基地址（VA = base + PA）
    pub static mut slab_linear_map_base: usize;

    // ---- MMIO / ioremap ----
    /// ioremap 物理地址到内核虚拟地址
    pub fn ioremap(phys_addr: u64, size: usize) -> *mut u8;
    /// 从 FDT 找到第一个 virtio block 设备，返回 IRQ，MMIO 地址写入 out
    pub fn rust_find_virtio_blk(out_mmio_pa: *mut u64) -> u32;
}

// Opaque C struct bindings
#[repr(C)]
pub struct VmaArea {
    pub vm_start: u64,
    pub vm_end: u64,
    pub vm_flags: u64,
    pub vm_next: *mut VmaArea,
}

#[repr(C)]
pub struct MmStruct {
    pub mmap: *mut VmaArea,
    pub start_brk: u64,
    pub brk: u64,
    pub start_stack: u64,
    pub pgd: u64,
    // lock omitted (opaque to Rust)
}

/// 异常入口保存的完整寄存器上下文（对应 vector.S 的保存顺序）
/// vector.S 用 stp xN, xM, [sp, #-16]! 压栈，sp 递减
/// 所以最先保存的在最高地址，最后保存的在最低地址
/// 布局从低地址到高地址（sp → sp+272）：
///   elr_el1,spsr_el1 | _xzr,x30 | x29,x28 | x27,x26 | ... | x1,x0
/// 共 17 对 = 272 字节
#[repr(C)]
#[derive(Clone, Copy)]
pub struct PtRegs {
    pub elr_el1: u64,
    pub spsr_el1: u64,
    pub _xzr: u64,
    pub x30: u64,
    pub x29: u64,
    pub x28: u64,
    pub x27: u64,
    pub x26: u64,
    pub x25: u64,
    pub x24: u64,
    pub x23: u64,
    pub x22: u64,
    pub x21: u64,
    pub x20: u64,
    pub x19: u64,
    pub x18: u64,
    pub x17: u64,
    pub x16: u64,
    pub x15: u64,
    pub x14: u64,
    pub x13: u64,
    pub x12: u64,
    pub x11: u64,
    pub x10: u64,
    pub x9: u64,
    pub x8: u64,
    pub x7: u64,
    pub x6: u64,
    pub x5: u64,
    pub x4: u64,
    pub x3: u64,
    pub x2: u64,
    pub x1: u64,
    pub x0: u64,
}

impl PtRegs {
    pub const SIZE: usize = core::mem::size_of::<PtRegs>();
}

// el1_exception_exit 汇编函数（在 vector.S 中定义）
unsafe extern "C" {
    pub fn el1_exception_exit();
}

unsafe extern "C" {
    // ---- 进程内存管理（Rust mm 模块提供，C 不再直接调用） ----
    // mm_create, mm_destroy, vma_create, vma_find, do_brk, do_mmap, do_munmap, mm_copy
    // 已迁移到 rust/src/kernel/mm.rs（BTreeMap 实现）
    // page_fault.c 通过 rust_vma_find FFI 调用 Rust mm
}

// 2. 定义一个空结构体（必须要有一个类型，才能实现 Write trait）
pub struct Console;


//  只实现这一个！
impl core::fmt::Write for Console {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        // 仅做一件事：把 Rust 字符串 → 转成 C 语言需要的 \0 结尾字符串
        let mut buf = [0u8; 256];
        // 安全截断，防止溢出
        let len = core::cmp::min(s.len(), buf.len() - 1);
        // 复制字符串内容
        buf[..len].copy_from_slice(s.as_bytes());
        // C 语言字符串必须以 \0 结尾（必须加）
        buf[len] = 0;

        // 直接调用你现成的 C 打印接口
        unsafe { c_print_str(buf.as_ptr()) };

        Ok(())
    }
}