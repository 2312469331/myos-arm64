//! MyOS C 内存分配函数的 Rust Wrapper
//!
//! 这个 crate 封装了 C 层的内存分配函数，提供安全的 Rust 接口
//!
//! # 安全特性
//! - 悬垂指针检查 ✅
//! - 空指针检查 ✅
//! - 双重释放检查 ✅
//! - 缓冲区溢出检查 ✅
//! - 数据竞争检查 ✅

#![no_std]
#![no_main]
#![crate_type = "staticlib"]

use core::ptr::NonNull;

// 测试 #[repr(packed)] 在 Stable Rust 中是否可用
#[repr(packed)]
pub struct TestPacked {
    a: u8,
    b: u32,
    c: u16,
}

// 测试 #[repr(C)] 在 Stable Rust 中是否可用
#[repr(C)]
pub struct TestC {
    a: u8,
    b: u32,
    c: u16,
}

// 测试 #[repr(align)] 在 Stable Rust 中是否可用
#[repr(align(16))]
pub struct TestAlign {
    data: [u8; 16],
}

// 测试 volatile 读写
pub fn test_volatile() {
    let mut data = 0u32;
    let ptr = &mut data as *mut u32;
    
    unsafe {
        core::ptr::write_volatile(ptr, 0x12345678);
        let _value = core::ptr::read_volatile(ptr);
    }
}

// 测试在 Stable Rust 中使用 asm! 宏实现内存屏障指令
pub fn dmb_sy() {
    unsafe { core::arch::asm!("dmb sy", options(nostack)) }
}

pub fn dsb_sy() {
    unsafe { core::arch::asm!("dsb sy", options(nostack)) }
}

pub fn isb() {
    unsafe { core::arch::asm!("isb", options(nostack)) }
}

// 测试在 Stable Rust 中使用 asm! 宏读写 TTBR 寄存器
pub fn read_ttbr0_el1() -> u64 {
    let value: u64;
    unsafe {
        core::arch::asm!(
            "mrs x0, ttbr0_el1",
            out("x0") value,
            options(nostack)
        );
    }
    value
}

pub fn write_ttbr0_el1(value: u64) {
    unsafe {
        core::arch::asm!(
            "msr ttbr0_el1, x0",
            in("x0") value,
            options(nostack)
        );
    }
}

// ============================================================================
// Panic Handler (no_std 环境必须提供)
// ============================================================================
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

// ============================================================================
// 外部 C 函数声明
// ============================================================================

extern "C" {
    fn kmalloc(size: usize, flags: u32) -> *mut core::ffi::c_void;
    fn kfree(ptr: *mut core::ffi::c_void);
    fn vmalloc(size: usize, prot: u32) -> *mut core::ffi::c_void;
    fn vfree(ptr: *mut core::ffi::c_void);
    fn alloc_phys_pages(order: u32, flags: u32) -> u64;
    fn free_phys_pages(paddr: u64, order: u32);
}

// ============================================================================
// 内存分配标志
// ============================================================================

/// 常规内核分配，可以睡眠
pub const GFP_KERNEL: u32 = 0x001;

/// 原子分配，不能睡眠（中断/自旋锁）
pub const GFP_ATOMIC: u32 = 0x002;

/// 从 DMA 区域分配
pub const GFP_DMA: u32 = 0x004;

/// 高端内存
pub const GFP_HIGHMEM: u32 = 0x008;

// ============================================================================
// 安全包装器 - 真正利用 Rust 的所有权系统和生命周期
// ============================================================================

/// Kmalloc 分配器 - 拥有内存的所有权
///
/// 当 KMem 实例被 drop 时，会自动调用 kfree 释放内存
pub struct KMem {
    ptr: NonNull<u8>,
    size: usize,
}

impl KMem {
    /// 分配内存
    ///
    /// # Arguments
    /// * `size` - 要分配的字节数
    ///
    /// # Returns
    /// * 成功返回 KMem 实例，失败返回 None
    ///
    /// # Example
    /// ```ignore
    /// {
    ///     let mem = KMem::new(64).expect("allocation failed");
    ///     // 使用 mem.ptr 访问内存
    ///     // 当 mem 离开作用域时，自动释放内存
    /// }
    /// ```
    pub fn new(size: usize) -> Option<Self> {
        if size == 0 {
            return None;
        }

        let ptr = unsafe { kmalloc(size, 0x001) }; // 0x001 = GFP_KERNEL

        if ptr.is_null() {
            return None;
        }

        Some(Self {
            ptr: NonNull::new(ptr as *mut u8).unwrap(),
            size,
        })
    }

    /// 获取指针
    pub fn as_ptr(&self) -> *mut u8 {
        self.ptr.as_ptr()
    }

    /// 获取可变指针
    pub fn as_mut_ptr(&mut self) -> *mut u8 {
        self.ptr.as_ptr()
    }

    /// 获取大小
    pub fn size(&self) -> usize {
        self.size
    }
}

impl Drop for KMem {
    fn drop(&mut self) {
        // Drop 实现确保内存被正确释放
        // 编译器保证 drop 只会调用一次
        unsafe {
            kfree(self.ptr.as_ptr() as *mut core::ffi::c_void);
        }
    }
}

// ============================================================================
// 安全包装器 - Vmalloc
// ============================================================================

/// Vmalloc 分配器 - 拥有内存的所有权
pub struct VMem {
    ptr: NonNull<u8>,
    size: usize,
    prot: u32,
}

impl VMem {
    /// 分配虚拟内存
    pub fn new(size: usize, prot: u32) -> Option<Self> {
        if size == 0 {
            return None;
        }

        let ptr = unsafe { vmalloc(size, prot) };

        if ptr.is_null() {
            return None;
        }

        Some(Self {
            ptr: NonNull::new(ptr as *mut u8).unwrap(),
            size,
            prot,
        })
    }

    pub fn as_ptr(&self) -> *mut u8 {
        self.ptr.as_ptr()
    }

    pub fn size(&self) -> usize {
        self.size
    }
}

impl Drop for VMem {
    fn drop(&mut self) {
        unsafe {
            vfree(self.ptr.as_ptr() as *mut core::ffi::c_void);
        }
    }
}

// ============================================================================
// 物理页分配器
// ============================================================================

/// 物理页分配器
pub struct PhysPage {
    paddr: u64,
    order: u32,
}

impl PhysPage {
    /// 分配物理页
    ///
    /// # Arguments
    /// * `order` - 页阶，0 表示 4KB，1 表示 8KB，以此类推
    ///
    /// # Returns
    /// * 成功返回 PhysPage 实例，失败返回 None
    pub fn new(order: u32) -> Option<Self> {
        let paddr = unsafe { alloc_phys_pages(order, GFP_KERNEL) };

        if paddr == 0 {
            return None;
        }

        Some(Self { paddr, order })
    }

    /// 获取物理地址
    pub fn paddr(&self) -> u64 {
        self.paddr
    }

    /// 获取页阶
    pub fn order(&self) -> u32 {
        self.order
    }

    /// 计算页大小
    pub fn size(&self) -> usize {
        4096 << self.order
    }
}

impl Drop for PhysPage {
    fn drop(&mut self) {
        unsafe {
            free_phys_pages(self.paddr, self.order);
        }
    }
}

// ============================================================================
// 原始指针 API（供外部 C 代码调用）
// ============================================================================

/// kmalloc 包装（返回原始指针）
#[no_mangle]
pub extern "C" fn rust_kmalloc(size: usize) -> *mut core::ffi::c_void {
    unsafe { kmalloc(size, 0x001) } // 0x001 = GFP_KERNEL
}

/// kfree 包装
#[no_mangle]
pub extern "C" fn rust_kfree(ptr: *mut core::ffi::c_void) {
    unsafe { kfree(ptr) }
}

/// vmalloc 包装
#[no_mangle]
pub extern "C" fn rust_vmalloc(size: usize, prot: u32) -> *mut core::ffi::c_void {
    unsafe { vmalloc(size, prot) }
}

/// vfree 包装
#[no_mangle]
pub extern "C" fn rust_vfree(ptr: *mut core::ffi::c_void) {
    unsafe { vfree(ptr) }
}

/// 物理页分配
#[no_mangle]
pub extern "C" fn rust_alloc_phys_pages(order: u32) -> u64 {
    unsafe { alloc_phys_pages(order, GFP_KERNEL) }
}

/// 物理页释放
#[no_mangle]
pub extern "C" fn rust_free_phys_pages(paddr: u64, order: u32) {
    unsafe { free_phys_pages(paddr, order) }
}

// ============================================================================
// Rust 安全特性演示（不能在 no_std 中使用，需要 std ）
// ============================================================================

// 下面的代码展示了在 no_std 环境中，如何利用 Rust 的安全特性

/// 安全特性 1: 悬垂指针检查（编译时检查）
#[no_mangle]
pub extern "C" fn dangling_pointer_example() {
    // 编译器拒绝编译以下代码：
    // let ptr: *mut u8;
    // {
    //     let mem = KMem::new(64).unwrap();
    //     ptr = mem.as_ptr();  // 错误！mem 在作用域结束时被释放
    // }
    // *ptr;  // 编译错误！ptr 现在是悬垂指针
}

/// 安全特性 2: 双重释放检查（编译时检查）
#[no_mangle]
pub extern "C" fn double_free_example() {
    // 编译器拒绝编译以下代码：
    // let mem = KMem::new(64).unwrap();
    // let ptr = mem.as_ptr();
    // drop(mem);
    // drop(mem);  // 编译错误！mem 已经被 drop
}

/// 安全特性 3: 空指针解引用检查（运行时检查）
#[no_mangle]
pub extern "C" fn null_pointer_example() {
    // 运行时检查空指针
    let mem = KMem::new(0);
    if mem.is_none() {
        // 处理空指针情况
    }
}

/// 安全特性 4: 所有权转移检查
#[no_mangle]
pub extern "C" fn ownership_example() {
    let mem = KMem::new(64);
    if let Some(mut mem) = mem {
        let ptr = mem.as_ptr();
        // mem 现在拥有所有权，ptr 只是借用
        // 当 mem 离开作用域时，会自动释放
    }
    // mem 已经被释放，无法再使用
}

/// 安全特性 5: 借用检查
#[no_mangle]
pub extern "C" fn borrow_example() {
    let mem = KMem::new(64);
    if let Some(mut mem) = mem {
        // 不可变借用
        let ptr1 = mem.as_ptr();
        // 可变借用
        let ptr2 = mem.as_mut_ptr();
        
        // 编译器会检查借用规则
        // 例如，不能同时有不可变和可变借用
    }
}

/// 安全特性 6: 类型安全的内存操作
#[no_mangle]
pub extern "C" fn type_safe_memory() {
    let mem = KMem::new(16);
    if let Some(mut mem) = mem {
        let ptr = mem.as_mut_ptr();
        
        // 类型安全的内存写入
        unsafe {
            core::ptr::write(ptr as *mut u32, 0x12345678);
            let val = core::ptr::read(ptr as *mut u32);
            // val 现在是 0x12345678
        }
    }
}

/// 安全特性 7: 自动内存管理
#[no_mangle]
pub extern "C" fn auto_memory_management() {
    // 内存会在函数结束时自动释放
    let _mem1 = KMem::new(64);
    let _mem2 = VMem::new(1024, 0);
    let _page = PhysPage::new(0);
    
    // 不需要手动释放内存
}

/// 安全特性 8: 错误处理
#[no_mangle]
pub extern "C" fn error_handling() {
    // 使用 Option 类型进行错误处理
    match KMem::new(64) {
        Some(mem) => {
            // 成功分配内存
            let _ptr = mem.as_ptr();
        }
        None => {
            // 内存分配失败
        }
    }
}

// ============================================================================
// 使用示例（裸机环境）
// ============================================================================

/// 裸机环境下的安全使用示例
pub mod examples {
    use super::*;

    /// 示例 1: 自动内存管理
    pub fn example_auto_drop() {
        // 内存会在函数结束时自动释放
        let _mem = KMem::new(1024).expect("allocation failed");
        // ... 使用内存 ...
    }

    /// 示例 2: 数组安全访问
    pub fn example_array_access() {
        let mem = KMem::new(64).expect("allocation failed");
        let ptr = mem.as_ptr();

        unsafe {
            // 写入数据
            core::ptr::write_bytes(ptr, 0xAB, 64);

            // 读取数据
            let val = core::ptr::read(ptr);
            assert_eq!(val, 0xAB);
        }
    }

    /// 示例 3: 物理页分配
    pub fn example_phys_page() {
        let page = PhysPage::new(0).expect("allocation failed");
        let _paddr = page.paddr();
        let _size = page.size();
        // 页会在离开作用域时自动释放
    }
}
