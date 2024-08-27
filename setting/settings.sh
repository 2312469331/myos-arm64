#!/data/data/com.termux/files/usr/bin/bash

set -e

# 颜色
green() { echo -e "\033[32m$*\033[0m"; }
yellow() { echo -e "\033[33m$*\033[0m"; }
red() { echo -e "\033[31m$*\033[0m"; }

clear
green "==================== 自动配置脚本 ===================="
green "1. 安装 git which fd"
green "2. 配置 git 用户：liuweiji / 2312469331@qq.com"
green "3. 克隆 myos-arm64 到 ~/myos-arm64"
green "4. 解压 nvim.tar nvims.tar font.tar 到 ~/"
green "5. 不额外安装字体（使用你自己的 font.tar）"
green "6. 询问是否安装 OS 开发工具链"
green "======================================================="
echo

# --------------------------
# 1. 更新并安装基础包
# --------------------------
yellow "[+] 更新并安装 git which fd..."
pkg update -y
pkg install -y git which fd

# --------------------------
# 2. 配置 git
# --------------------------
yellow "[+] 配置 git 用户名和邮箱..."
git config --global user.name "liuweiji"
git config --global user.email "2312469331@qq.com"
green "✓ Git 配置完成"

# --------------------------
# 3. 克隆项目
# --------------------------
cd ~
if [ -d myos-arm64 ]; then
  red "! 已存在 ~/myos-arm64"
  read -p "是否删除并重新克隆？(y/n) " ans
  if [ "$ans" = "y" ]; then
    rm -rf myos-arm64
    yellow "[+] 重新克隆..."
    git clone https://gitee.com/gaminglee/myos-arm64
  else
    green "✓ 保留原有目录"
  fi
else
  yellow "[+] 克隆项目..."
  git clone https://gitee.com/gaminglee/myos-arm64
fi

# --------------------------
# 4. 解压三个 tar：nvim.tar nvims.tar font.tar
# --------------------------
cd ~/myos-arm64/setting
green "✓ 进入目录：$(pwd)"

echo
yellow "==== 准备解压 nvim.tar nvims.tar font.tar 到 ~/ ===="
yellow "注意：包内已包含 .config .local .termux，会直接解压到根目录"
read -p "确定要解压吗？会覆盖同名文件！(y/n) " ans
if [ "$ans" = "y" ]; then
  yellow "[+] 解压 nvim.tar ..."
  tar -xvf nvim.tar -C ~/

  yellow "[+] 解压 nvims.tar ..."
  tar -xvf nvims.tar -C ~/

  yellow "[+] 解压 font.tar ..."
  tar -xvf font.tar -C ~/

  # 👉 这里加上了字体重载
  yellow "[+] 重载 Termux 设置..."
  termux-reload-settings

  green "✓ 解压完成！"
else
  red "✗ 取消解压"
fi

# --------------------------
# 5. 原来的字体安装已删除
# --------------------------
echo
green "✓ 使用你自己的字体文件，不额外安装字体"

# --------------------------
# 6. 询问是否安装开发工具链
# --------------------------
echo
yellow "==== 是否安装 Termux OS 开发工具链？===="
yellow "包含：gcc vim make cmake python curl wget 等"
read -p "安装？(y/n) " ans
if [ "$ans" = "y" ]; then
  yellow "[+] 开始安装开发工具..."
  pkg install -y \
    vim gcc g++ make cmake \
    python python-dev \
    curl wget zip unzip \
    clang llvm binutils

  green "✓ 开发工具链安装完成！"
else
  green "✓ 不安装开发工具链"
fi

echo
green "======================================================"
green "                全部任务执行完毕！"
green "  NVim 配置已部署：~/.config/nvim  ~/.local/share/nvim"
green "  字体已部署：~/.termux/font.ttf"
green "======================================================"
