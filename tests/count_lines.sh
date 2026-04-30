#!/bin/bash

echo "========================================"
echo "      内核代码统计 - 终极版"
echo "========================================"

total_code=0

count_code() {
    local name="$1"
    local dir="$2"
    local lines=$(find "$dir" \
        -type f \( \
            -name "*.c" -o -name "*.h" -o -name "*.S" -o -name "*.rs" \
        \) \
        | xargs wc -l 2>/dev/null \
        | tail -n 1 \
        | awk '{print $1}')
    lines=${lines:-0}
    printf "%-20s %d 行\n" "$name" "$lines"
    total_code=$((total_code + lines))
}

count_code "驱动"          driver
count_code "Rust 内核"     rust/src
count_code "头文件"        include
count_code "C 内核核心"    kernel
count_code "异常处理"      exception
count_code "ARM64 启动"    arch/arm64/boot
count_code "ARM64 头文件"  arch/arm64/include

echo "----------------------------------------"
printf "%-20s %d 行\n" "🔥 代码总行数" "$total_code"

# ====================== 项目总文本行数（纯 Git 超快版）======================
total_text=$(git ls-files | xargs wc -l 2>/dev/null | tail -n 1 | awk '{print $1}')
total_text=${total_text:-0}

echo "========================================"
printf "%-20s %d 行\n" "📄 项目总文本行数" "$total_text"
echo "========================================"