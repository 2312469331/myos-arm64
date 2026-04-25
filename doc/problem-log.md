你刚刚踩的这些坑，**不是你的问题，完全是 Rust 社区从旧版本迁移到 2024 Edition（1.85+）时集体踩过的经典大坑**。
很多网上的 OS 开发教程还是 2018/2021 年的写法，如果直接抄，在 1.95 上必然全红。
我把这几十个报错提炼成了 **4 条核心法则**，建议你作为写“裸机 Rust”的检查清单保存下来：
---
### 法则一：2024 Edition 对 `unsafe` 的极度严苛
2024 版本修复了以前 `unsafe` 泛滥的问题，编译器变得更“防杠”了。
*   **坑 1：`extern` 块必须加 `unsafe`**
    *   ❌ 旧写法：`extern "C" { ... }` （报错：`extern blocks must be unsafe`）
    *   ✅ 新写法：`unsafe extern "C" { ... }`
*   **坑 2：`unsafe fn` 不再默认继承整个函数体的 unsafe 权限**
    *   ❌ 旧写法：在 `unsafe fn alloc()` 里直接调用其他 unsafe 函数（报警告：`unsafe_op_in_unsafe_fn`）
    *   ✅ 新写法：在 `unsafe fn` 的函数体里面，还得再包一层 `unsafe { ... }`。它的逻辑变成了：“虽然我是个危险函数，但我内部的具体操作还得单独声明危险”。
*   **坑 3：可见性修饰符 `pub` 的位置**
    *   ❌ 旧写法：`pub unsafe extern "C" { fn test(); }` （报错：`visibility qualifiers are not permitted here`）
    *   ✅ 新写法：`unsafe extern "C" { pub fn test(); }` （`pub` 必须写在具体的条目上）
### 法则二：`no_std` 下 `core` 和 `alloc` 的严格界限
在裸机下，没有标准库，只有你自己编译的底层库，它们的职责分得很清：
*   **坑 1：OOM 处理函数已经不存在了**
    *   ❌ 旧写法：用 `#[alloc_error_handler]` 定义 OOM（报错：unstable feature）
    *   ✅ 新写法：**什么都不用写**。在 2024 版本中，如果你的 `Cargo.toml` 里设了 `panic = "abort"`，当内存耗尽时，Rust 会自动触发 `handle_alloc_error`，然后直接走 `#[panic_handler]` 崩溃，不需要你手动写 Lang Item。
*   **坑 2：`CString` 不在 `core` 里**
    *   ❌ 错觉：既然是基础类型，应该在 `core::ffi::CString`？ （报错：找不到）
    *   ✅ 真相：`CString` 需要在**堆上分配内存**，所以它属于 `alloc` 库。必须写 `use alloc::ffi::CString;`。`core` 里只有不带所有权的 `CStr`。
*   **坑 3：`vec!` 宏的引入**
    *   ❌ 写法：只写 `use alloc::vec::Vec;`，然后直接用 `vec![]`。（报错：找不到宏）
    *   ✅ 写法：必须引入宏所在的模块 `use alloc::vec;`，宏才能用。
### 法则三：跨语言 FFI 指针的“类型对齐”法则
C 语言和 Rust 对字符指针的理解有微小差异，Clippy 会强迫你统一。
*   **坑 1：永远不要让 Rust 直接调 C 的可变参数函数（如 `printk("%s", ...)`）**
    *   跨语言传 `...` 是未定义行为（UB），极易栈溢出。
    *   ✅ **必须在 C 侧封装**：`void c_print_str(const char *s) { printk("%s", s); }`
*   **坑 2：`*const u8` 和 `*const i8` 的世纪大战**
    *   C 的 `char *` 在大多数 ARM 平台上是 `i8`。Rust 的字节是 `u8`。
    *   如果你 FFI 定义的是 `*const u8`，每次传 `CStr::as_ptr()` 都要写 `as *const u8`，Clippy 会疯掉（报错：`unnecessary_cast`）。
    *   ✅ **终极解法**：FFI 声明直接写 `*const core::ffi::c_char`。Rust 的 `c"..."` 和 `CString` 的 `.as_ptr()` 天然就是这个类型，直接传，一行转换代码都不用写。
### 法则四：Clippy 教你的现代惯用法
这些不影响编译，但能让你代码看起来像老手。
*   **坑 1：手动拼接 `\0` 是土鳖写法**
    *   ❌ 旧写法：`b"hello\0".as_ptr()`
    *   ✅ 新写法：`c"hello".as_ptr()` （Rust 1.77 稳定的 C 字符串字面量，自动带 `\0`，类型安全）
*   **坑 2：滥用 `vec!` 导致无谓的堆分配**
    *   ❌ 写法：`let tmp = vec![1, 2, 3];` 紧接着不用了。（报警：`useless_vec`）
    *   ✅ 写法：如果只是临时用一下，直接写数组 `let tmp = [1, 2, 3];`。堆分配（你的 Slab）很贵，能省则省。
*   **坑 3：静态库不需要 `main.rs`**
    *   因为你让 C 做主入口，Rust 只是提供 `staticlib`（被 C 调用的 `.a` 文件），所以 `src/main.rs` 必须删掉，只留 `src/lib.rs`，否则编译器会发疯找 `main` 函数。
---
**一句话总结你的架构：**
C 语言掌控绝对生杀大权（MMU、中断、物理页），通过 `unsafe extern` 暴露纯粹的“动作（指针+大小）”；Rust 在上层通过 `#[global_allocator]` 把 C 的 Slab 包装成水管，接管所有复杂的业务逻辑。两者通过 `*const c_char` 和 `.as_ptr()` 完美握手。

