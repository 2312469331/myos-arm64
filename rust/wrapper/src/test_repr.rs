#![no_std]

// 测试 #[repr(packed)] 在 Stable Rust 中是否可用
#[repr(packed)]
struct TestPacked {
    a: u8,
    b: u32,
    c: u16,
}

// 测试 #[repr(C)] 在 Stable Rust 中是否可用
#[repr(C)]
struct TestC {
    a: u8,
    b: u32,
    c: u16,
}

// 测试 #[repr(align)] 在 Stable Rust 中是否可用
#[repr(align(16))]
struct TestAlign {
    data: [u8; 16],
}

// 测试 core::arch::aarch64 在 Stable Rust 中是否可用
#[cfg(target_arch = "aarch64")]
pub fn test_arch_aarch64() {
    // 测试内存屏障指令
    unsafe {
        core::arch::aarch64::dmb(core::arch::aarch64::SY);
        core::arch::aarch64::dsb(core::arch::aarch64::SY);
        core::arch::aarch64::isb();
    }
}

// 测试 volatile 读写
pub fn test_volatile() {
    let mut data = 0u32;
    let ptr = &mut data as *mut u32;
    
    unsafe {
        core::ptr::write_volatile(ptr, 0x12345678);
        let value = core::ptr::read_volatile(ptr);
        // value 现在是 0x12345678
    }
}
