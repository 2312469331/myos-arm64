#!/data/data/com.termux/files/usr/bin/bash

echo "正在安装 aarch64-unknown-none 裸机 Rust 系统库..."
mkdir -p $PREFIX/lib/rustlib
tar -zxf aarch64-unknown-none-rustlib.tar.gz -C $PREFIX/lib/rustlib/

echo "安装完成！"
rustc --target aarch64-unknown-none --version
