#!/bin/bash

# ARM64 虚拟地址索引计算器
# 输入：64位虚拟地址（十六进制）
# 输出：L0、L1、L2、L3 各级索引值

echo "ARM64 虚拟地址索引计算器"
echo "==========================="

# 检查输入参数
if [ $# -ne 1 ]; then
    echo "用法: $0 <虚拟地址>"
    echo "例如: $0 0xFFFF800040000000"
    exit 1
fi

# 读取虚拟地址
VA=$1

# 提取地址部分（移除 0x 前缀）
ADDR=${VA#0x}

# 确保地址长度为 16 个十六进制字符
while [ ${#ADDR} -lt 16 ]; do
    ADDR="0$ADDR"
done

# 计算各级索引的函数
calculate_index() {
    local hex=$1
    local shift=$2
    
    # 转换为十进制（使用 bash 的内置功能）
    local dec=$((0x$hex))
    
    # 计算索引
    local index=$(( (dec >> $shift) & 0x1FF ))
    echo $index
}

# 计算各级索引
l0=$(calculate_index "$ADDR" 39)
l1=$(calculate_index "$ADDR" 30)
l2=$(calculate_index "$ADDR" 21)
l3=$(calculate_index "$ADDR" 12)

# 输出结果
echo "输入虚拟地址: $VA"
echo ""
echo "L0 索引 (bit 47-39): $l0"
echo "L1 索引 (bit 38-30): $l1"
echo "L2 索引 (bit 29-21): $l2"
echo "L3 索引 (bit 20-12): $l3"
echo ""
echo "==========================="
