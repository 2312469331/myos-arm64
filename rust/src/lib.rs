// src/lib.rs
#![no_std]
#![no_main]
#![allow(unused)]

extern crate alloc;

pub mod console;
pub mod sync;
pub mod config;

pub mod arch;

pub mod kernel;
mod allocator;
mod ffi;
mod irq;

use alloc::ffi::CString;
use alloc::string::String;
use alloc::vec::Vec;
use core::fmt::Write;
use ffi::Console;



#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    unsafe {
        ffi::c_print_str(c"\n[RUST PANIC] Kernel panicked!\n".as_ptr());
    }
    // 尝试打印 panic 位置
    if let Some(loc) = info.location() {
        let loc_str = alloc::format!("[RUST PANIC] at {}:{}\n", loc.file(), loc.line());
        let c_loc = alloc::ffi::CString::new(loc_str).unwrap();
        unsafe { ffi::c_print_str(c_loc.as_ptr()); }
    }
    loop {
        core::hint::spin_loop();
    }
}
