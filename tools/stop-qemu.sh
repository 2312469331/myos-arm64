#!/bin/bash
pkill -f "qemu-system-aarch64" || true
echo "✅ QEMU 已停止"