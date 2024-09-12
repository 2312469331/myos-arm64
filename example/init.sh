#!/bin/bash

# 项目根目录
ROOT="cortex-a53-os-c"

# 创建目录结构
mkdir -p "$ROOT"
mkdir -p "$ROOT/include"
mkdir -p "$ROOT/src"
mkdir -p "$ROOT/arch/aarch64"

# 创建顶层文件
touch "$ROOT/Makefile"
touch "$ROOT/linker.ld"
touch "$ROOT/run.sh"
touch "$ROOT/debug.sh"
touch "$ROOT/README.md"

# 创建 include 头文件
touch "$ROOT/include/types.h"
touch "$ROOT/include/uart.h"
touch "$ROOT/include/mm.h"
touch "$ROOT/include/gic.h"
touch "$ROOT/include/timer.h"

# 创建 src 源文件
touch "$ROOT/src/main.c"
touch "$ROOT/src/uart.c"
touch "$ROOT/src/mm.c"
touch "$ROOT/src/gic.c"
touch "$ROOT/src/timer.c"

# 创建 arch/aarch64 汇编启动文件
touch "$ROOT/arch/aarch64/boot.S"

# 完成提示
echo "✅ 项目初始化完成！"
echo "📁 项目结构：$ROOT/"
tree "$ROOT"

