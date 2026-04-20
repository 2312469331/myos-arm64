#!/bin/bash
# Rust Wrapper 构建脚本
# 用于 ARM64 裸机环境的 Rust 静态库

set -e

# 检查 Rust 目标
check_rust_target() {
    if ! rustup target list --installed | grep -q aarch64-unknown-none; then
        echo "安装 aarch64-unknown-none 目标..."
        rustup target add aarch64-unknown-none
    fi
}

# 构建库
build() {
    cd "$(dirname "$0")/wrapper"
    check_rust_target
    cargo build --target aarch64-unknown-none --release
    echo "构建完成: rust/wrapper/target/aarch64-unknown-none/release/libmyos_wrapper.a"
}

# 显示帮助
show_help() {
    echo "用法: $0 [命令]"
    echo ""
    echo "命令:"
    echo "  build     构建 Rust 静态库"
    echo "  check     检查代码"
    echo "  clean     清理构建产物"
    echo "  help      显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 build"
}

case "${1:-build}" in
    build)
        build
        ;;
    check)
        cd "$(dirname "$0")/wrapper"
        cargo check --target aarch64-unknown-none
        ;;
    clean)
        cd "$(dirname "$0")/wrapper"
        cargo clean
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        echo "未知命令: $1"
        show_help
        exit 1
        ;;
esac
