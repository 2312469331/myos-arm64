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

