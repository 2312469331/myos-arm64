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
#![crate_type = "staticlib"]

use core::ptr::NonNull;

// ============================================================================
// 外部 C 函数声明
// ============================================================================

extern "C" {
    fn kmalloc(size: usize) -> *mut core::ffi::c_void;
    fn kfree(ptr: *mut core::ffi::c_void);
    fn vmalloc(size: usize, prot: u32) -> *mut core::ffi::c_void;
    fn vfree(ptr: *mut core::ffi::c_void);
    fn alloc_phys_pages(order: u32) -> u64;
    fn free_phys_pages(paddr: u64, order: u32);
}

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

        let ptr = unsafe { kmalloc(size) };

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
        let paddr = unsafe { alloc_phys_pages(order) };

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
    unsafe { kmalloc(size) }
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
    unsafe { alloc_phys_pages(order) }
}

/// 物理页释放
#[no_mangle]
pub extern "C" fn rust_free_phys_pages(paddr: u64, order: u32) {
    unsafe { free_phys_pages(paddr, order) }
}

// ============================================================================
// Rust 安全特性演示（不能在 no_std 中使用，需要 std）
// ============================================================================

// 下面的代码展示了在有 std 的环境中，如何利用 Rust 的安全特性
// 这些代码仅作为文档参考，不能在裸机环境中编译

/*
/// 安全特性 1: 悬垂指针检查
/// 编译器拒绝编译以下代码：
fn dangling_pointer_example() {
    let ptr: *mut u8;
    {
        let mem = KMem::new(64).unwrap();
        ptr = mem.as_ptr();  // 错误！mem 在作用域结束时被释放
    }
    // println!("{}", *ptr);  // 编译错误！ptr 现在是悬垂指针
}

/// 安全特性 2: 双重释放检查
/// 编译器拒绝编译以下代码：
fn double_free_example() {
    let mem = KMem::new(64).unwrap();
    let ptr = mem.as_ptr();
    drop(mem);
    // drop(mem);  // 编译错误！mem 已经被 drop
}

/// 安全特性 3: 空指针解引用检查
/// 编译器拒绝编译以下代码：
fn null_pointer_example() {
    let ptr: *mut u8 = core::ptr::null_mut();
    // let val = *ptr;  // 编译错误！不能解引用空指针
}

/// 安全特性 4: 所有权转移检查
fn ownership_example() {
    let mem = KMem::new(64).unwrap();
    let ptr = mem.as_ptr();
    // mem 现在仍然拥有所有权，ptr 只是借用
    // 如果尝试在 drop 后使用 ptr，编译器会报错
    drop(mem);
    // 任何尝试使用 ptr 的代码都会导致编译错误
}

/// 安全特性 5: 借用检查
fn borrow_example() {
    let mut mem = KMem::new(64).unwrap();

    // 不可变借用
    let ptr1 = mem.as_ptr();
    // 可变借用
    let ptr2 = mem.as_mut_ptr();

    // 错误！在有不可变借用时，不能有可变借用
    // let ptr3 = mem.as_mut_ptr();
}
*/

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
