use crate::ffi;

/// GFP 标志，与 C 侧 gfp.h 保持一致
pub mod gfp {
    pub const KERNEL: u32 = 0x001;
    pub const ATOMIC: u32 = 0x002;
    pub const DMA:    u32 = 0x004;
    pub const HIGHMEM: u32 = 0x008;
}

/// DMA 一致性缓冲：分配物理页并映射为 Non-cacheable
pub struct DmaBuf {
    pub va: usize,
    pub pa: u64,
    pub size: usize,
}

impl DmaBuf {
    pub fn alloc(size: usize) -> Option<Self> {
        let mut dma_pa: u64 = 0;
        let va = unsafe { ffi::dma_alloc_coherent(size as u64, &mut dma_pa as *mut u64) };
        if va.is_null() { return None; }
        Some(DmaBuf { va: va as usize, pa: dma_pa, size })
    }
}

impl Drop for DmaBuf {
    fn drop(&mut self) {
        unsafe { ffi::dma_free_coherent(self.va as *mut u8); }
    }
}

/// 引用计数的物理页，Drop 时自动释放。
#[derive(Debug)]
pub struct PhysPage {
    pa: u64,
    order: u32,
}

impl PhysPage {
    /// 分配连续物理页（2^order 页），失败返回 None
    pub fn alloc_with(order: u32, flags: u32) -> Option<Self> {
        let pa = unsafe { ffi::alloc_phys_pages(order, flags) };
        if pa == 0 { None } else { Some(PhysPage { pa, order }) }
    }

    /// 分配一页常规内存
    pub fn alloc() -> Option<Self> {
        Self::alloc_with(0, gfp::KERNEL)
    }

    /// 分配一页 DMA 内存
    pub fn alloc_dma() -> Option<Self> {
        Self::alloc_with(0, gfp::DMA)
    }

    pub fn pa(&self) -> u64 { self.pa }
    pub fn order(&self) -> u32 { self.order }
}

impl PhysPage {
    /// 放弃所有权（页表/其他人已接管），不释放
    pub fn leak(self) {
        core::mem::forget(self);
    }

    /// 递增引用计数
    pub fn ref_inc(&self) {
        unsafe { ffi::get_page(self.pa); }
    }

    /// 手动递减引用计数
    pub fn ref_dec(&self) {
        unsafe { ffi::put_page(self.pa); }
    }

    /// 物理地址是否全零（未分配）
    pub fn is_null(&self) -> bool { self.pa == 0 }
}

impl Clone for PhysPage {
    fn clone(&self) -> Self {
        unsafe { ffi::get_page(self.pa); }
        PhysPage { pa: self.pa, order: 0 }
    }
}

impl Drop for PhysPage {
    fn drop(&mut self) {
        if self.order == 0 {
            unsafe { ffi::put_page(self.pa); }
        } else {
            unsafe { ffi::free_phys_pages(self.pa, self.order); }
        }
    }
}

unsafe impl Send for PhysPage {}
unsafe impl Sync for PhysPage {}
