# MyOS ARM64

这是一个基于 ARM64 架构的裸机操作系统项目，支持 QEMU virt 平台。

## 项目特性

- **架构**: ARM64 (AArch64)
- **CPU**: Cortex-A53
- **平台**: QEMU virt
- **内存管理**: 支持物理内存管理（Buddy 系统）、虚拟内存管理（vmalloc/vmap）
- **中断**: 支持 GICv2 中断控制器
- **设备树**: 支持 FDT 设备树解析
- **Rust 支持**: 集成 Rust 代码（可选）

## 编译说明

### 环境要求

#### 1. 交叉编译工具链

根据您的操作系统，需要安装以下工具链之一：

**Windows (推荐)**:

# ARM GNU Toolchain: `aarch64-none-elf-gcc`

- 下载地址: <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads>

**Ubuntu/Linux**:

```bash
sudo apt-get install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

**Termux (Android)**:

```bash
pkg install clang lld
```

#### 2. QEMU 模拟器

**Windows**:

- 下载 QEMU for Windows: <https://www.qemu.org/download/#windows>
- 或使用 MSYS2: `pacman -S mingw-w64-x86_64-qemu`

**Ubuntu/Linux**:

```bash
sudo apt-get install qemu-system-arm
```

**Termux**:

```bash
pkg install qemu-system-aarch64
```

#### 3. 设备树编译器 (可选)

```bash
# Ubuntu/Linux
sudo apt-get install device-tree-compiler

# Windows (MSYS2)
pacman -S dtc
```

### 编译命令

#### 基本编译

```bash
# 编译内核 ELF 文件
make

# 或使用多线程加速编译
make -j4
```

#### 运行 QEMU

```bash
# 正常运行
make run

# GDB 调试（阻塞等待）
make debug

# GDB 调试（不阻塞）
make debug-no-suspend

# 串口独立调试
make serial
```

#### 生成设备树

```bash
# 导出 QEMU 设备树并转换为 DTS
make dtb
```

#### 清理编译结果

```bash
# 清理编译生成的文件
make clean

# 清理设备树文件
make clean-dtb
```

#### 查看 ELF 信息

```bash
# 查看 ELF 文件段信息
make readelf-l

# 查看 ELF 文件节信息
make readelf-S

# 查看 ELF 文件符号表
make readelf-s

# 查看所有 ELF 信息
make readelf
```

### 编译选项

#### 架构选择

默认编译 ARM64 架构，可以通过命令行指定：

```bash
make ARCH=arm64
```

#### 功能配置

项目支持通过 `config.mk` 文件进行条件编译，可以启用或禁用特定功能。

### 编译输出

编译完成后，生成的文件位于 `build/` 目录：

- `build/kernel.elf` - ELF 格式的内核镜像（默认目标）
- `build/kernel.img` - 二进制格式的内核镜像

### 调试

#### GDB 调试

1. 启动 QEMU 调试模式：

```bash
make debug
```

1. 在另一个终端启动 GDB：

```bash
aarch64-none-elf-gdb build/kernel.elf
```

1. 在 GDB 中连接 QEMU：

```gdb
target remote localhost:1234
```

### 常见问题

#### 1. 找不到交叉编译器

确保交叉编译器已安装，并且路径已添加到系统 PATH 环境变量中。

**Windows**: 将 `aarch64-none-elf-gcc` 的 bin 目录添加到 PATH

**Linux**: 确保 `gcc-aarch64-linux-gnu` 已正确安装

#### 2. 多线程编译失败

如果遇到目录创建错误，可以尝试：

```bash
# 先创建目录
make build_dir

# 再编译
make -j4
```

#### 3. QEMU 无法启动

确保 QEMU 已正确安装，并且路径已添加到系统 PATH 环境变量中。

## Rust 开发指南

### 环境要求

本项目基于 **Rust 1.95.0 stable** 开发，支持 Windows、Linux 和 Termux 环境。

#### Windows / Linux

推荐使用 `rustup` 安装 Rust 工具链：

```bash
# 安装 Rust（如果没有）
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# 安装 ARM64-none-none 目标（裸机目标）
rustup target add aarch64-unknown-none

# 安装 LLVM 工具（用于链接）
rustup component add llvm-tools-preview

# 验证安装
rustc --version
cargo --version
rustup target list --installed
```

#### Termux (Android)

Termux 没有 `rustup`，需要从项目分支安装 target：

```bash
# 克隆 rust-lib 分支
git clone -b rust-lib https://github.com/2312469331/myos-arm64.git

# 切换分支
cd myos-arm64
git checkout rust-lib

# 执行安装脚本
bash install-none.sh

# 或手动设置
# cp -r none-* ~/.rustup/toolchains/*/lib/rustlib/
```

### Rust 代码结构

```
rust/
├── src/
│   ├── lib.rs          # 库入口（包含 rust_main）
│   ├── allocator.rs    # 全局内存分配器
│   ├── ffi.rs         # FFI 接口定义
│   ├── irq.rs         # 中断管理接口
│   └── ...
├── Cargo.toml          # Rust 项目配置
├── build.rs            # 构建脚本
└── install-none.sh     # Termux 安装脚本
```

### Rust 编译命令

```bash
# 编译 Rust 代码
cd rust
cargo build

# 清理 Rust 编译结果
cargo clean

# 查看 Rust 代码大小
cargo size --release 2>/dev/null || echo "Build required first"
```

### Rust 语言项要求

由于本项目使用 `#![no_std]` 和 `#![no_main]`，需要定义以下语言项：

```rust
// #[panic_handler] - 必须：处理所有 panic 情况
#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    // 打印错误信息并进入死循环
    loop {}
}

// #[lang = "eh_personality"] - 必须：异常处理 personality 函数
#[lang = "eh_personality"]
extern "C" fn eh_personality() {}

// #[global_allocator] - 可选：自定义全局内存分配器
#[global_allocator]
static ALLOCATOR: KernelAllocator = KernelAllocator;
```

### 内存分配策略

本项目的 Rust 内存分配器采用**永不返回 NULL** 的设计：

1. **优先使用 kmalloc**：正常情况下调用 C 侧的 `rust_kmalloc`
2. **备用内存池**：当 kmalloc 失败时，从 4KB 备用池分配
3. **永不返回 NULL**：确保 `handle_alloc_error` 永远不会被调用，避免 panic

### FFI 接口

Rust 代码通过 FFI 调用 C 函数：

```rust
// 声明 FFI 函数
unsafe extern "C" {
    fn c_print_str(s: *const c_char);
    fn rust_kmalloc(size: usize) -> *mut u8;
    fn rust_kfree(ptr: *mut u8, size: usize);
}
```

### 常见问题

#### 1. Rust 编译错误：`unknown target aarch64-unknown-none`

```bash
# 添加 ARM64 裸机目标
rustup target add aarch64-unknown-none
```

#### 2. 链接错误：`rust_main` 符号未找到

确保在 `main.c` 中调用了 `rust_main()`：

```c
extern void rust_main(void);
rust_main();
```

#### 3. 内存分配失败导致 panic

检查 C 侧的 `rust_kmalloc` 是否正确实现，确保内存分配器已初始化。

### 相关文档

- [Rust 语法说明](doc/rust.md) - Rust 1.95.0 裸机开发语法特性
- [GIC 中断说明](doc/gic.md) - GICv2 中断控制器配置

## 项目结构

```
myos-arm64/
├── arch/arm64/          # ARM64 架构相关代码
│   ├── boot/           # 启动代码
│   ├── Core/           # CMSIS-Core 代码
│   └── device/         # 设备特定代码
├── kernel/             # 内核代码
│   ├── main.c          # 主函数
│   ├── pmm.c           # 物理内存管理
│   ├── vmalloc.c       # 虚拟内存分配
│   └── ...
├── driver/             # 驱动代码
│   ├── uart.c          # UART 驱动
│   ├── gic.c           # GIC 中断控制器驱动
│   ├── timer.c         # 定时器驱动
│   └── fdt/            # FDT 库
├── include/            # 头文件
├── doc/                # 文档
├── rust/               # Rust 代码（可选）
├── Makefile            # 主 Makefile
├── config.mk           # 配置选项
└── virt.dts            # 设备树源文件
```

## 文档

- [FDT 库使用说明](doc/fdt_usage.md)
- [Rust 语法说明](doc/rust.md)
- [GIC 中断说明](doc/gic.md)
- [定时器说明](doc/timer.md)

## 许可证

本项目采用 MIT 许可证。

## 贡献

欢迎提交 Issue 和 Pull Request！
