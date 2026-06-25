/// virtio-blk 驱动
/// 基于 virtio v1.4 规范，MMIO Legacy 传输层

use crate::ffi;
use crate::kernel::fs::vfs::BlockDevice;
use crate::kernel::wait::{WaitQueue, wait_event, wake_up};
use crate::kernel::task::CURRENT_TASK;
use crate::kernel::bio::{Bio, BioQueue};
use crate::kernel::phys_page::{PhysPage, DmaBuf, gfp};
use crate::sync::KMutex;
use alloc::sync::Arc;
use alloc::vec::Vec;
use core::sync::atomic::Ordering;

// ── MMIO 寄存器偏移（legacy 模式，Version=1，Table 4.3） ──
const REG_MAGIC: u64 = 0x000;
const REG_VERSION: u64 = 0x004;
const REG_DEVICE_ID: u64 = 0x008;
const REG_VENDOR_ID: u64 = 0x00C;
const REG_HOST_FEATURES: u64 = 0x010;
const REG_HOST_FEATURES_SEL: u64 = 0x014;
const REG_GUEST_FEATURES: u64 = 0x020;
const REG_GUEST_FEATURES_SEL: u64 = 0x024;
const REG_GUEST_PAGE_SIZE: u64 = 0x028;
const REG_QUEUE_SEL: u64 = 0x030;
const REG_QUEUE_SIZE_MAX: u64 = 0x034;
const REG_QUEUE_SIZE: u64 = 0x038;
const REG_QUEUE_ALIGN: u64 = 0x03C;
const REG_QUEUE_PFN: u64 = 0x040;
const REG_QUEUE_NOTIFY: u64 = 0x050;
const REG_INTERRUPT_STATUS: u64 = 0x060;
const REG_INTERRUPT_ACK: u64 = 0x064;
const REG_STATUS: u64 = 0x070;
const REG_CONFIG: u64 = 0x100;

// ── Status 位 ──
const STATUS_ACK: u8 = 0x01;
const STATUS_DRIVER: u8 = 0x02;
const STATUS_DRIVER_OK: u8 = 0x04;
const STATUS_FEATURES_OK: u8 = 0x08;
const STATUS_FAILED: u8 = 0x80;

// ── virtio-blk 设备 ──
const DEVICE_ID_BLOCK: u32 = 2;

/// MMIO 寄存器访问器
struct MmioRegs {
    base: *mut u8,
}

impl MmioRegs {
    unsafe fn new(base: *mut u8) -> Self {
        MmioRegs { base }
    }

    fn read32(&self, offset: u64) -> u32 {
        unsafe { core::ptr::read_volatile((self.base.add(offset as usize)) as *const u32) }
    }

    fn write32(&self, offset: u64, val: u32) {
        unsafe { core::ptr::write_volatile((self.base.add(offset as usize)) as *mut u32, val) }
    }
}

/// 扫描并打印 virtio 设备信息（实际初始化由 VirtioBlk::new 完成）
pub fn init() {
    let mut mmio_pa = 0u64;
    let irq_num = unsafe { crate::ffi::rust_find_virtio_blk(&mut mmio_pa as *mut u64) };
    if irq_num == 0 || mmio_pa == 0 {
        crate::println!("[VIRTIO] no block device found in FDT");
        return;
    }
    crate::println!("[VIRTIO] block device at {:#x}, IRQ {}", mmio_pa, irq_num);
}

// ── Virtqueue 结构 ──

/// 描述符（16 字节）
#[repr(C)]
struct VqDesc {
    addr: u64,      // 数据缓冲物理地址
    len: u32,
    flags: u16,
    next: u16,
}

/// 可用环
#[repr(C)]
struct VqAvail {
    flags: u16,
    idx: u16,
    ring: [u16; 256],  // queue_size
}

/// 使用环
#[repr(C)]
struct VqUsedElem {
    id: u32,
    len: u32,
}

#[repr(C)]
struct VqUsed {
    flags: u16,
    idx: u16,
    ring: [VqUsedElem; 256],
}

const VQ_SIZE: u16 = 256;

// ── 描述符标志 ──
const VRING_DESC_F_NEXT: u16 = 1;
const VRING_DESC_F_WRITE: u16 = 2;

// ── blk 请求类型 ──
const BLK_T_IN: u32 = 0;   // read
const BLK_T_OUT: u32 = 1;  // write

/// 初始化 virtqueue（legacy 模式使用 QueuePFN），返回 (pa, va, DmaBuf)
fn setup_virtqueue(regs: &MmioRegs, qidx: u32) -> Result<(u64, usize, DmaBuf), &'static str> {
    regs.write32(REG_QUEUE_SEL, qidx);

    let max = regs.read32(REG_QUEUE_SIZE_MAX);
    if max == 0 || max < VQ_SIZE as u32 {
        return Err("bad queue size");
    }

    // 分配 DMA 一致性内存（Non-cacheable，CPU 和设备共享）
    let dma = DmaBuf::alloc(16384).ok_or("alloc dma failed")?;
    let queue_pa = dma.pa;
    let queue_va = dma.va as *mut u8;
    unsafe { core::ptr::write_bytes(queue_va, 0, 16384); }

    regs.write32(REG_QUEUE_SIZE, VQ_SIZE as u32);
    regs.write32(REG_QUEUE_ALIGN, 4096);
    regs.write32(REG_QUEUE_PFN, (queue_pa >> 12) as u32);

    Ok((queue_pa, queue_va as usize, dma))
}

// ── 块设备读写 ──

/// 提交 blk 请求并等待完成
fn blk_request(regs: &MmioRegs, queue_pa: u64, queue_va: usize, is_write: bool, sector: u64, buf: *mut u8, len: u32, wq: Option<&WaitQueue>) -> Result<(), &'static str> {
    if queue_va == 0 { return Err("vq not init"); }

    // 分配 DMA 一致性数据缓冲（Non-cacheable，设备直接读写）
    let dma = DmaBuf::alloc((16 + len as usize + 1) as usize).ok_or("alloc dma buf failed")?;
    let va = dma.va as *mut u8;
    let buf_pa = dma.pa;

    if is_write {
        unsafe { core::ptr::copy_nonoverlapping(buf, va.add(16), len as usize); }
    }

    // 布局: va+0: header(16B), va+16: data, va+16+len: status(1B)
    unsafe {
        let h = va as *mut u32;
        h.write_unaligned(if is_write { BLK_T_OUT } else { BLK_T_IN });
        h.add(1).write_unaligned(0u32);
        *(h.add(2) as *mut u64) = sector;
    }

    let header_pa = buf_pa;
    let data_pa = buf_pa + 16;
    let status_pa = buf_pa + 16 + (len as u64);

    // avail ring (offset 4096: 256 desc × 16 字节)
    let avail = (queue_va + 4096) as *mut VqAvail;
    let idx = unsafe { (*avail).idx };
    let desc_idx = idx % VQ_SIZE;

    // desc 表（3 个 desc 链，所有下标模 VQ_SIZE 防止越界）
    let desc = queue_va as *mut VqDesc;
    let vsize = VQ_SIZE as usize;
    let i0 = desc_idx as usize % vsize;
    let i1 = (desc_idx as usize + 1) % vsize;
    let i2 = (desc_idx as usize + 2) % vsize;
    unsafe {
        // desc[i0]: header (device-readable)
        (*desc.add(i0)).addr = header_pa;
        (*desc.add(i0)).len = 16;
        (*desc.add(i0)).flags = VRING_DESC_F_NEXT;
        (*desc.add(i0)).next = i1 as u16;

        // desc[i1]: data
        let w = if is_write { 0u16 } else { VRING_DESC_F_WRITE };
        (*desc.add(i1)).addr = data_pa;
        (*desc.add(i1)).len = len;
        (*desc.add(i1)).flags = VRING_DESC_F_NEXT | w;
        (*desc.add(i1)).next = i2 as u16;

        // desc[i2]: status (device-writable)
        (*desc.add(i2)).addr = status_pa;
        (*desc.add(i2)).len = 1;
        (*desc.add(i2)).flags = VRING_DESC_F_WRITE;
        (*desc.add(i2)).next = 0;

        // avail ring: 放入 desc_idx
        let ring = &mut (*avail).ring;
        ring[idx as usize % VQ_SIZE as usize] = desc_idx;
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
        (*avail).idx = (*avail).idx.wrapping_add(1);
    }

    // 通知前保存 used.idx
    let used = (queue_va + 8192) as *mut VqUsed;
    let last_used = unsafe { (*used).idx };

    core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    regs.write32(REG_QUEUE_NOTIFY, 0);

    // 等待设备完成（先快速轮询，不行就等中断）
    for _ in 0..1000 {
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
        if unsafe { (*used).idx } != last_used { break; }
    }
    if unsafe { (*used).idx } == last_used {
        if let Some(wq) = wq {
            wait_event(wq, || unsafe { (*used).idx != last_used });
        } else {
            // fallback: 轮询
            loop {
                core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
                if unsafe { (*used).idx } != last_used { break; }
            }
        }
    }

    // 读状态
    let result = unsafe { *(va.add(16 + len as usize)) };
    if result != 0 {
        return Err("io err");
    }

    // 读：拷贝数据回调用者
    if !is_write {
        unsafe { core::ptr::copy_nonoverlapping(va.add(16), buf, len as usize); }
    }

    Ok(())
    // dma drop → dma_free_coherent → unmap + free_phys_pages
}

/// virtio-blk 块设备适配器，实现 BlockDevice trait
pub struct VirtioBlk {
    regs: MmioRegs,
    wq: WaitQueue,
    irq: u32,
    io_lock: KMutex<()>,   // 串行化 blk_request，防止多任务踩描述符
    queue_pa: u64,
    queue_va: usize,
    _dma: DmaBuf,           // DMA 一致性映射，Drop 时自动释放
}

unsafe impl Send for VirtioBlk {}
unsafe impl Sync for VirtioBlk {}

static VIRTIO_BLK_INSTANCE: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);

extern "C" fn virtio_irq_handler(_irq: u32) {
    let ptr = VIRTIO_BLK_INSTANCE.load(core::sync::atomic::Ordering::Relaxed);
    if ptr == 0 { return; }
    let blk = unsafe { &*(ptr as *const VirtioBlk) };

    let status = blk.regs.read32(REG_INTERRUPT_STATUS);
    if status != 0 {
        blk.regs.write32(REG_INTERRUPT_ACK, status);
    }
    wake_up(&blk.wq);
}

impl VirtioBlk {
    pub fn new(mmio_pa: u64, irq_num: u32) -> Result<Arc<Self>, &'static str> {
        let vm = unsafe { ffi::ioremap(mmio_pa, 0x200) };
        if vm.is_null() { return Err("ioremap failed"); }
        let regs = unsafe { MmioRegs::new(vm) };
        let magic = regs.read32(REG_MAGIC);
        if magic != 0x74726976 { return Err("bad virtio magic"); }
        let devid = regs.read32(REG_DEVICE_ID);
        if devid != DEVICE_ID_BLOCK { return Err("not a block device"); }
        regs.write32(REG_STATUS, 0);
        regs.write32(REG_STATUS, STATUS_ACK as u32);
        regs.write32(REG_STATUS, (STATUS_ACK | STATUS_DRIVER) as u32);
        regs.write32(REG_HOST_FEATURES_SEL, 0);
        let _ = regs.read32(REG_HOST_FEATURES);
        regs.write32(REG_GUEST_FEATURES_SEL, 0);
        regs.write32(REG_GUEST_FEATURES, 0);
        regs.write32(REG_STATUS, (STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK) as u32);
        regs.write32(REG_GUEST_PAGE_SIZE, 4096);
        let (qpa, qva, qdma) = setup_virtqueue(&regs, 0).map_err(|_| "vq init failed")?;
        regs.write32(REG_STATUS, (STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_DRIVER_OK) as u32);
        crate::irq::register_irq(irq_num, virtio_irq_handler, "virtio-blk");
        let blk = Arc::new(VirtioBlk {
            regs, wq: WaitQueue::new(), irq: irq_num,
            io_lock: KMutex::new(()),
            queue_pa: qpa, queue_va: qva,
            _dma: qdma,
        });
        // 保存实例指针供 IRQ handler 使用
        let ptr = Arc::as_ptr(&blk) as usize;
        VIRTIO_BLK_INSTANCE.store(ptr, core::sync::atomic::Ordering::Relaxed);
        Ok(blk)
    }
}

impl VirtioBlk {
    /// 调度器就绪前用轮询，就绪后用中断等待
    fn wq_or_poll(&self) -> Option<&WaitQueue> {
        if CURRENT_TASK.load(Ordering::Relaxed).is_null() {
            None  // 调度器未就绪 → 轮询
        } else {
            Some(&self.wq)  // 调度器就绪 → 可中断等待
        }
    }
}

impl BlockDevice for VirtioBlk {
    fn read_sector(&self, sector: u64, buf: &mut [u8]) -> Result<(), &'static str> {
        let _lock = self.io_lock.lock();
        blk_request(&self.regs, self.queue_pa, self.queue_va, false,
                    sector, buf.as_mut_ptr(), buf.len() as u32, self.wq_or_poll())
    }
    fn write_sector(&self, sector: u64, buf: &[u8]) -> Result<(), &'static str> {
        let _lock = self.io_lock.lock();
        blk_request(&self.regs, self.queue_pa, self.queue_va, true,
                    sector, buf.as_ptr() as *mut u8, buf.len() as u32, self.wq_or_poll())
    }
    fn sector_size(&self) -> u64 { 512 }
}
