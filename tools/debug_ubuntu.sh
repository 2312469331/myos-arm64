#!/bin/bash


pkill cargo
pkill rustc
rm -rf ~/.cargo/.package-cache
rm -rf ./rust/target
# pkill -f "qemu-system-aarch64" 2>/dev/null
sleep 0.2
# 第一步：编译内核，失败直接退出，不启动QEMU
echo "🔨 开始编译内核..."
if ! make; then
    echo "❌ 编译失败！请修复代码后重试"
    exit 1
fi
pkill cargo
pkill rustc
rm -rf ~/.cargo/.package-cache
rm -rf ./rust/target
# 编译成功，才往下走
echo "✅ 编译成功！启动QEMU..."
# 👇 关键：--noprofile --norc 不加载 ROS 坏配置！
make debug -j$(nproc)

echo "✅ QEMU 启动成功"
echo "🔌 VSCode 按 F5 连接调试"