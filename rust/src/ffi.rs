// src/ffi.rs
//! 与 C 底层交互的 FFI 声明
//! 
//! 【要求你的 C 代码必须提供以下三个函数】：
//! 1. void c_print_str(const char *s);
//! 2. void *kmalloc(size_t size);
//! 3. void kfree(void *ptr, size_t size);  <-- 注意这里的 size，很多自写 slab 需要 size 来找索引

// src/ffi.rs

// 2024 edition：extern 块要写 unsafe；可见性写在条目上，不要写在块上。


// src/ffi.rs
#![allow(unused)]

unsafe extern "C" {
    // C 侧实现：void c_print_str(const char *s);
    pub fn c_print_str(s: *const core::ffi::c_char);

    // C 侧实现：void *rust_kmalloc(size_t size);
    pub fn rust_kmalloc(size: usize) -> *mut u8;

    // C 侧实现：void rust_kfree(void *ptr, size_t size);
    pub fn rust_kfree(ptr: *mut u8, size: usize);
}

// 2. 定义一个空结构体（必须要有一个类型，才能实现 Write trait）
pub struct Console;


//  只实现这一个！
impl core::fmt::Write for Console {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        // 仅做一件事：把 Rust 字符串 → 转成 C 语言需要的 \0 结尾字符串
        let mut buf = [0u8; 256];
        // 安全截断，防止溢出
        let len = core::cmp::min(s.len(), buf.len() - 1);
        // 复制字符串内容
        buf[..len].copy_from_slice(s.as_bytes());
        // C 语言字符串必须以 \0 结尾（必须加）
        buf[len] = 0;

        // 直接调用你现成的 C 打印接口
        unsafe { c_print_str(buf.as_ptr()) };

        Ok(())
    }
}