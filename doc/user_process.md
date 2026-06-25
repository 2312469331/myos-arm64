让我详细分析用户态进程的页表创建和内存分配过程：


        
            
toolName: view_files
            
status: success
          
            
filePath: /home/liuweiji/code/myos-arm64/rust/src/kernel/scheduler.rs
          
让我查看用户程序的二进制数据保存在哪里：


        
            
toolName: search_by_regex
            
status: success
          
query: HELLO_USER
          

        
            
toolName: view_files
            
status: success
          
            
filePath: /home/liuweiji/code/myos-arm64/rust/src/kernel/user_bin.rs
          
## 用户态进程页表和内存分配详解

### 一、用户程序代码保存位置

用户程序的二进制代码**编译进内核镜像**中，定义在：

```rust
// 文件：rust/src/kernel/user_bin.rs
pub static HELLO_USER: [u8; 24] = [
    // MOV X8, #0      ; 系统调用号 0 = print
    0x08, 0x00, 0x80, 0xD2,
    // MOV X0, #1      ; 参数1 = 1 (stdout)
    0x20, 0x00, 0x80, 0xD2,
    // SVC #0           ; 触发系统调用 → EL1
    0x01, 0x00, 0x00, 0xD4,
    // MOV X8, #1      ; 系统调用号 1 = exit
    0x28, 0x00, 0x80, 0xD2,
    // SVC #0           ; 触发系统调用 → EL1
    0x01, 0x00, 0x00, 0xD4,
    // B .              ; 死循环
    0x00, 0x00, 0x00, 0x14,
];
```

这是一个 24 字节的 ARM64 汇编程序，实现简单的系统调用。

---

### 二、内存分配流程

#### 1. **创建用户页表**

```rust
let user_pgd = match pgtbl::create_user_pgd() {
    Some(pgd) => pgd,
    None => { /* 失败处理 */ }
};
```

**关键**：`create_user_pgd()` 会**复制内核映射**到用户页表，这样用户进程可以访问内核空间（但有权限限制）。

#### 2. **分配代码页并复制程序**

```rust
// 分配 1 个物理页
let code_pa = unsafe { ffi::alloc_phys_pages(0, 0) };

// 转换为内核虚拟地址
let code_va = pgtbl::phys_to_virt(code_pa);

// 复制用户程序到物理页
unsafe {
    core::ptr::copy_nonoverlapping(
        HELLO_USER.as_ptr(),     // 源：内核中的用户程序
        code_va as *mut u8,      // 目标：物理页的虚拟地址
        HELLO_USER.len(),        // 长度：24 字节
    );
}
```

#### 3. **映射代码页到用户空间**

```rust
// 切换到用户页表
pgtbl::switch_page_table(user_pgd);

// 创建映射：用户虚拟地址 → 物理地址
pgtbl::map_page(user_code_va, code_pa, pgtbl::PAGE_USER_RX);
// 用户虚拟地址：0x0000_0040_0000
// 权限：用户可读可执行 (USER_RX)
```

#### 4. **分配并映射栈页**

```rust
// 分配 2 个物理页 (8KB)
let stack_pa = unsafe { ffi::alloc_phys_pages(1, 0) };

// 清零栈内存
unsafe {
    core::ptr::write_bytes(stack_va as *mut u8, 0, stack_size * 4096);
}

// 映射到用户空间（栈向下增长）
for i in 0..stack_size {
    let page_pa = stack_pa + (i as u64) * 4096;
    let page_va = user_stack_va - (i + 1) * 4096;
    pgtbl::map_page(page_va, page_pa, pgtbl::PAGE_USER_RW);
    // 用户虚拟地址：0x0000_0080_0000 - 8192
    // 权限：用户可读可写 (USER_RW)
}
```

---

### 三、内存布局

```
┌─────────────────────────────────────────────────────────────────┐
│                      用户虚拟地址空间                           │
├─────────────────────────────────────────────────────────────────┤
│  0xFFFF_0000_0000_0000 ────────────────────────────────────────┤ ← 内核空间（共享）
│                           ...                                  │
├─────────────────────────────────────────────────────────────────┤
│  0x0000_0080_0000 ────── 用户栈 (8KB) ─────── 0x0000_007F_FF00 │
│    ↓                                                           │
│  栈顶初始值: 0x0000_0080_0000                                  │
├─────────────────────────────────────────────────────────────────┤
│  0x0000_0040_0000 ────── 用户代码 (1页) ───── 0x0000_0040_0FFF │
│    ↓                                                           │
│  入口地址: 0x0000_0040_0000                                    │
├─────────────────────────────────────────────────────────────────┤
│  0x0000_0000_0000 ──────────────────────────── 0x0000_003F_FFFF│
│                           (未映射)                              │
└─────────────────────────────────────────────────────────────────┘
```

### 四、地址映射关系

| 描述 | 用户虚拟地址 | 物理地址 | 权限 |
|-----|------------|---------|-----|
| 用户代码 | `0x0000_0040_0000` | `code_pa` | USER_RX |
| 用户栈（页1） | `0x0000_007F_FF00` | `stack_pa` | USER_RW |
| 用户栈（页2） | `0x0000_007F_FE00` | `stack_pa + 4096` | USER_RW |

### 五、总结

1. **代码存储**：用户程序编译进内核，通过 `HELLO_USER` 静态数组访问
2. **页表创建**：调用 `create_user_pgd()` 创建，包含内核映射
3. **内存分配**：
   - 代码页：1 页，只读执行
   - 栈页：2 页（8KB），可读可写
4. **地址空间**：
   - 代码：256MB 处（`0x400000`）
   - 栈：512MB 处（`0x800000`），向下增长

这样用户进程就有了独立的地址空间，可以安全地在 EL0 用户态执行！