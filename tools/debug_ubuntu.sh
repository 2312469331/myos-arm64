#!/bin/bash
# run-qemu-debug.sh - QEMU ARM64 内核调试启动脚本（后台运行）
# 编译内核（确保最新）
pkill -f "qemu-system-aarch64" || true

make debug 2>&1 

echo "QEMU 调试服务器已启动"
echo "PID: $!"
echo "日志: /tmp/qemu-output.log"