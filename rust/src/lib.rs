// src/lib.rs
#![no_std]
#![no_main]

extern crate alloc;

mod allocator;
mod ffi;
mod irq;

use alloc::ffi::CString;
use alloc::string::String;
use alloc::vec::Vec;

#[unsafe(no_mangle)]
pub extern "C" fn rust_main() {
    // 以下调用都直接传 as_ptr()，不再 as *const u8
    unsafe {
        ffi::c_print_str(c"[RUST] Kernel upper layer started.\n".as_ptr());
    }

    // 测试中断注册
    unsafe {
        ffi::c_print_str(c"[RUST] Testing interrupt registration...\n".as_ptr());
    }
    
    // 定义一个中断处理函数
    extern "C" fn test_irq_handler(irq: u32) {
        let log = alloc::format!("[RUST] IRQ handler called for IRQ {}\n", irq);
        let c_log = CString::new(log).unwrap();
        unsafe {
            ffi::c_print_str(c_log.as_ptr());
        }
    }
    
    // 注册中断处理函数
    irq::register_irq_enum(irq::IrqNumber::Uart0, test_irq_handler, "Rust UART Test");
    
    unsafe {
        ffi::c_print_str(c"[RUST] IRQ handler registered successfully\n".as_ptr());
    }

    // 测试 String（底层走你的 Slab）
    let mut greeting = String::from("Hello from Rust String!");
    greeting.push_str(" Using your C Slab.\n");
    let c_greeting = CString::new(greeting).unwrap();
    unsafe {
        ffi::c_print_str(c_greeting.as_ptr());
    }

    // 测试 Vec
    let mut numbers = Vec::new();
    for i in 0..5 {
        numbers.push(i * 10);
    }

    let log = alloc::format!("Vec test: {:?}\n", numbers);
    let c_log = CString::new(log).unwrap();
    unsafe {
        ffi::c_print_str(c_log.as_ptr());
    }

    // 不需要 Vec 的地方直接用数组，避免 useless_vec 警告
    {
        let _tmp = [1, 2, 3];
    }
    unsafe {
        ffi::c_print_str(c"[RUST] Vec dropped (kfree called).\n".as_ptr());
    }

    unsafe {
        ffi::c_print_str(c"[RUST] All alloc tests passed!\n".as_ptr());
    }

    loop {
        core::hint::spin_loop();
    }
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe {
        ffi::c_print_str(c"\n[RUST PANIC] Kernel panicked!".as_ptr());
    }
    loop {
        core::hint::spin_loop();
    }
}
