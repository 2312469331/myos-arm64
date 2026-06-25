/// 页表管理模块
/// 封装 C 页表操作函数，提供 Rust 安全接口

use crate::ffi;

/// 页表属性常量（对应 C 的 pgtbl.h 宏）
/// 内核可读写，EL0 不可访问
pub const PAGE_KERNEL_RW: u64 =
    (1 << 0)   // Valid
    | (1 << 1)  // Page
    | (1 << 10) // AF
    | (0 << 11) // nG（内核用全局）
    | (3 << 8)  // SH: Inner Shareable
    | (0 << 6)  // AP: EL1 RW, EL0 None
    | (0 << 5)  // NS
    | (0 << 2)  // AttrIndx: Normal
    | (1 << 54) // UXN
    | (1 << 53); // PXN

/// 内核可读写，EL0 也可读写（用户态映射用）
pub const PAGE_USER_RW: u64 =
    (1 << 0)   // Valid
    | (1 << 1)  // Page
    | (1 << 10) // AF
    | (1 << 11) // nG（用户态用非全局）
    | (3 << 8)  // SH: Inner Shareable
    | (1 << 6)  // AP: EL1 RW, EL0 RW
    | (0 << 5)  // NS
    | (0 << 2)  // AttrIndx: Normal
    | (1 << 54) // UXN
    | (1 << 53); // PXN

/// 内核只读，EL0 不可访问（代码段）
pub const PAGE_KERNEL_RX: u64 =
    (1 << 0)   // Valid
    | (1 << 1)  // Page
    | (1 << 10) // AF
    | (0 << 11) // nG
    | (3 << 8)  // SH: Inner Shareable
    | (2 << 6)  // AP: EL1 RO, EL0 None
    | (0 << 5)  // NS
    | (0 << 2)  // AttrIndx: Normal
    | (1 << 54) // UXN
    | (1 << 53); // PXN

/// 用户态可读可执行（代码段）
pub const PAGE_USER_RX: u64 =
    (1 << 0)   // Valid
    | (1 << 1)  // Page
    | (1 << 10) // AF
    | (1 << 11) // nG（用户态用非全局）
    | (3 << 8)  // SH: Inner Shareable
    | (3 << 6)  // AP: EL1 RO, EL0 RO
    | (0 << 5)  // NS
    | (0 << 2)  // AttrIndx: Normal
    | (0 << 54) // UXN = 0（允许EL0执行）
    | (1 << 53); // PXN = 1（禁止EL1执行）

/// 页表项掩码
pub const PTE_ADDR_MASK: u64 = 0x0000FFFFFFFFF000;

/// 页大小
pub const PAGE_SIZE: usize = 4096;

/// 分配一个物理页作为页表
fn alloc_page_table() -> Option<u64> {
    let pa = unsafe { ffi::alloc_phys_pages(0, 0) }; // order=0, GFP_KERNEL=0
    if pa == 0 {
        return None;
    }
    // 清零
    let va = phys_to_virt(pa);
    unsafe {
        core::ptr::write_bytes(va as *mut u8, 0, PAGE_SIZE);
    }
    Some(pa)
}

/// 物理地址 → 虚拟地址（通过线性映射）
pub fn phys_to_virt(pa: u64) -> usize {
    unsafe { ffi::slab_linear_map_base + pa as usize }
}

/// 虚拟地址 → 物理地址
pub fn virt_to_phys(va: usize) -> u64 {
    unsafe { (va - ffi::slab_linear_map_base) as u64 }
}

/// 获取当前 TTBR0（用户态页表物理地址）
pub fn current_pgd() -> u64 {
    unsafe { ffi::read_ttbr0_el1() }
}

/// 获取当前 TTBR1（内核态页表物理地址）
pub fn kernel_pgd() -> u64 {
    unsafe { ffi::read_ttbr1_el1() }
}

/// 映射一个 4KB 页面
pub fn map_page(va: usize, pa: u64, flags: u64) -> Result<(), i32> {
    let ret = unsafe { ffi::arm64_map_one_page(va, pa, flags) };
    if ret == 0 {
        Ok(())
    } else {
        Err(ret)
    }
}

/// 取消映射
pub fn unmap_page(va: usize) {
    unsafe { ffi::arm64_unmap_one_page(va) };
}

/// 切换到指定页表
pub fn switch_page_table(pgd_pa: u64) {
    unsafe {
        ffi::switch_ttbr0(pgd_pa);
    }
}

/// 刷新 TLB
pub fn tlb_flush() {
    unsafe { ffi::flush_tlb() };
}

/// 创建用户进程的页表（TTBR0 页表）
/// ARM64: 用户态地址 (VA[47]=0) 走 TTBR0，内核态地址 (VA[47]=1) 走 TTBR1
/// 用户页表只需映射用户态地址，内核地址由 TTBR1 自动处理
pub fn create_user_pgd() -> Option<u64> {
    // 分配新的 L0 页表（用户态 TTBR0）
    let new_l0_pa = alloc_page_table()?;
    // 不需要复制任何东西，用户页表只包含用户态映射
    // 内核映射在 TTBR1 中，ERET 到 EL0 后硬件自动用 TTBR1 翻译内核地址
    Some(new_l0_pa)
}

/// 为用户进程映射代码段
pub fn map_user_code(pgd_pa: u64, va: usize, pa: u64, size: usize) -> Result<(), i32> {
    // 临时切换到新页表进行映射
    let old_pgd = current_pgd();
    switch_page_table(pgd_pa);

    let mut offset = 0;
    while offset < size {
        let page_pa = pa + offset as u64;
        let page_va = va + offset;
        map_page(page_va, page_pa, PAGE_KERNEL_RX)?; // 先用内核权限，后续加用户权限
        offset += PAGE_SIZE;
    }

    // 恢复原页表
    switch_page_table(old_pgd);
    Ok(())
}

/// 为用户进程映射数据段
pub fn map_user_data(pgd_pa: u64, va: usize, pa: u64, size: usize) -> Result<(), i32> {
    let old_pgd = current_pgd();
    switch_page_table(pgd_pa);

    let mut offset = 0;
    while offset < size {
        let page_pa = pa + offset as u64;
        let page_va = va + offset;
        map_page(page_va, page_pa, PAGE_KERNEL_RW)?; // 先用内核权限
        offset += PAGE_SIZE;
    }

    switch_page_table(old_pgd);
    Ok(())
}

/// 为用户进程映射栈（分配新页）
pub fn map_user_stack(pgd_pa: u64, va: usize, pages: usize) -> Result<u64, i32> {
    let old_pgd = current_pgd();
    switch_page_table(pgd_pa);

    // 分配栈内存
    let stack_pa = unsafe { ffi::alloc_phys_pages(pages.ilog2() as u32, 0) };
    if stack_pa == 0 {
        switch_page_table(old_pgd);
        return Err(-1);
    }

    // 清零
    let stack_va = phys_to_virt(stack_pa);
    unsafe {
        core::ptr::write_bytes(stack_va as *mut u8, 0, pages * PAGE_SIZE);
    }

    // 映射栈（栈向高地址增长，实际 SP 在顶部）
    let mut offset = 0;
    while offset < pages * PAGE_SIZE {
        let page_pa = stack_pa + offset as u64;
        let page_va = va + offset;
        map_page(page_va, page_pa, PAGE_KERNEL_RW)?; // 先用内核权限
        offset += PAGE_SIZE;
    }

    switch_page_table(old_pgd);
    Ok(stack_pa)
}
