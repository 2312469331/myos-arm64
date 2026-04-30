// src/lib.rs
#![no_std]
#![no_main]
#![allow(unused)]

extern crate alloc;

pub mod console;
pub mod sync;

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
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe {
        ffi::c_print_str(c"\n[RUST PANIC] Kernel panicked!".as_ptr());
    }
    loop {
        core::hint::spin_loop();
    }
}
