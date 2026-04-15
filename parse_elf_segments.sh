#!/bin/bash
set -euo pipefail

# 检查参数
if [ $# -ne 1 ]; then
    echo "Usage: $0 <elf_file>"
    echo "Example: $0 build/kernel.elf"
    exit 1
fi

ELF_FILE="$1"

# 检查 readelf 工具
if ! command -v readelf &> /dev/null; then
    echo "Error: readelf not found, please install binutils"
    exit 1
fi

# 检查 ELF 文件
if [ ! -f "$ELF_FILE" ]; then
    echo "Error: File $ELF_FILE not found"
    exit 1
fi

# 用 readelf -l 解析，提取 LOAD 段
echo "================================================"
echo "ELF Segment Analysis for: $ELF_FILE"
echo "Entry point: $(readelf -h "$ELF_FILE" | grep 'Entry point address' | awk '{print $4}')"
echo "================================================"
printf "%-8s %-18s %-18s %-10s %-10s %-4s %s\n" "Type" "VirtAddr" "EndAddr" "FileSiz" "MemSiz" "Flg" "Desc"
echo "================================================"

# 初始化总大小
total_file=0
total_mem=0

# 解析 LOAD 段
readelf -l "$ELF_FILE" | awk '
BEGIN {
    # 16进制转10进制的bc计算
    print "scale=0; obase=10; ibase=16" > "/tmp/hex2dec.bc"
}
$1 == "LOAD" {
    # 提取字段: Type Offset VirtAddr PhysAddr FileSiz MemSiz Flg Align
    offset = $2
    virt = $3
    phys = $4
    file_siz = $5
    mem_siz = $6
    flg = $7
    align = $8

    # 计算结束地址: virt + mem_siz
    virt_no_prefix = substr(virt, 3)  # 去掉 0x
    mem_siz_no_prefix = substr(mem_siz, 3)
    print virt_no_prefix "+" mem_siz_no_prefix >> "/tmp/hex2dec.bc"
    end_dec = $(getline < "/tmp/hex2dec.bc")
    close("/tmp/hex2dec.bc")
    # 转回16进制
    printf "0x%016x", end_dec > "/tmp/dec2hex.bc"
    print "scale=0; obase=16; ibase=10; " end_dec >> "/tmp/dec2hex.bc"
    end_hex = $(getline < "/tmp/dec2hex.bc")
    close("/tmp/dec2hex.bc")
    end_hex = "0x" tolower(end_hex)

    # 计算大小(10进制)
    file_siz_dec = 0
    mem_siz_dec = 0
    if (file_siz ~ /^0x/) {
        file_siz_no_prefix = substr(file_siz, 3)
        print file_siz_no_prefix >> "/tmp/hex2dec.bc"
        file_siz_dec = $(getline < "/tmp/hex2dec.bc")
        close("/tmp/hex2dec.bc")
    } else {
        file_siz_dec = file_siz
    }
    if (mem_siz ~ /^0x/) {
        mem_siz_no_prefix = substr(mem_siz, 3)
        print mem_siz_no_prefix >> "/tmp/hex2dec.bc"
        mem_siz_dec = $(getline < "/tmp/hex2dec.bc")
        close("/tmp/hex2dec.bc")
    } else {
        mem_siz_dec = mem_siz
    }

    # 描述段类型
    desc = ""
    if (flg ~ /R/ && flg ~ /E/) desc = "代码段 (.text/.boot)"
    else if (flg ~ /R/ && flg ~ /W/) desc = "数据段 (.data/.bss/.pagetable/.stacks)"
    else if (flg ~ /R/) desc = "只读数据段 (.rodata)"
    else if (flg ~ /W/) desc = "可写段 (.bss/堆)"
    else if (flg ~ /E/) desc = "可执行段"
    else desc = "未知段"

    # 输出行
    printf "%-8s %-18s %-18s 0x%-8s %-10s %-4s %s\n", $1, virt, end_hex, file_siz, mem_siz, flg, desc

    # 累加总大小
    total_file += file_siz_dec
    total_mem += mem_siz_dec
}
END {
    # 输出汇总
    print "================================================"
    printf "%-8s %-18s %-18s %-10s %-10s\n", "TOTAL", "", "", sprintf("0x%x", total_file), sprintf("0x%x", total_mem)
    printf "总磁盘占用: %d Bytes (%.2f KB / %.2f MB)\n", total_file, total_file/1024, total_file/1024/1024
    printf "总内存占用: %d Bytes (%.2f KB / %.2f MB)\n", total_mem, total_mem/1024, total_mem/1024/1024
    close("/tmp/hex2dec.bc")
    close("/tmp/dec2hex.bc")
    system("rm -f /tmp/hex2dec.bc /tmp/dec2hex.bc")
}'

