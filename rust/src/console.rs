//rust\src\console.rs
/// 内核控制台打印，基于 `core::fmt::Write` 手搓宏。
/// 打印期间关中断，防止抢占导致底层状态错乱。

use core::fmt;
use crate::arch::aarch64::cpu;

use crate::ffi;
// use core::ffi::CStr;
pub struct Console;

impl fmt::Write for Console {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        // 仅做一件事：把 Rust 字符串 → 转成 C 语言需要的 \0 结尾字符串
        let mut buf = [0u8; 256];
        // 安全截断，防止溢出
        let len = core::cmp::min(s.len(), buf.len() - 1);
        // 复制字符串内容
        buf[..len].copy_from_slice(s.as_bytes());
        // C 语言字符串必须以 \0 结尾（必须加）
        buf[len] = 0;

        // 直接调用你现成的 C 打印接口
        unsafe { ffi::c_print_str(buf.as_ptr()) };

        Ok(())
        // let _ = s;
        // Ok(())
    }
}

#[doc(hidden)]
pub fn _print(args: fmt::Arguments) {
    cpu::disable_irq();
    // 实例化 Console 并格式化输出
    let mut console = Console;
    let _ = fmt::write(&mut console, args);
    cpu::enable_irq();
}

/// 内核 `print!` 宏
#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => {{
        $crate::console::_print(format_args!($($arg)*));
    }};
}

/// 内核 `println!` 宏（附加换行）
#[macro_export]
macro_rules! println {
    () => ($crate::print!("\n"));
    ($($arg:tt)*) => {{
        $crate::console::_print(format_args!($($arg)*));
        $crate::console::_print(format_args!("\n"));
    }};
}