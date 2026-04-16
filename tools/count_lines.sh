#!/bin/bash

echo "开始统计内核代码行数..."
echo ""

# 统计 C 文件
echo "=== C 文件统计 ==="
c_files=$(find . -name "*.c" -type f | grep -E "(arch|driver|exception|kernel)")
c_total=0
for file in $c_files; do
    lines=$(cat "$file" | wc -l)
    valid=$(cat "$file" | grep -v "^[[:space:]]*//" | grep -v "^[[:space:]]*/\*" | grep -v "^[[:space:]]*\*/" | grep -v "^[[:space:]]*$" | wc -l)
    echo "$file: $lines 行，有效: $valid 行"
    c_total=$((c_total + lines))
done
echo "C 文件总行数: $c_total"

# 统计汇编文件
echo ""
echo "=== 汇编文件统计 ==="
asm_files=$(find . -name "*.S" -type f | grep -E "(arch|driver|exception|kernel)")
asm_total=0
for file in $asm_files; do
    lines=$(cat "$file" | wc -l)
    valid=$(cat "$file" | grep -v "^[[:space:]]*#" | grep -v "^[[:space:]]*$" | wc -l)
    echo "$file: $lines 行，有效: $valid 行"
    asm_total=$((asm_total + lines))
done
echo "汇编文件总行数: $asm_total"

# 统计头文件
echo ""
echo "=== 头文件统计 (.h) ==="
h_files=$(find . -name "*.h" -type f | grep -E "(arch|driver|exception|kernel|include)")
h_total=0
for file in $h_files; do
    lines=$(cat "$file" | wc -l)
    valid=$(cat "$file" | grep -v "^[[:space:]]*//" | grep -v "^[[:space:]]*/\*" | grep -v "^[[:space:]]*\*/" | grep -v "^[[:space:]]*$" | wc -l)
    echo "$file: $lines 行，有效: $valid 行"
    h_total=$((h_total + lines))
done
echo "头文件总行数: $h_total"

# 总计
echo ""
echo "=== 总计 ==="
total=$((c_total + asm_total + h_total))
echo "总行数: $total"
echo "  - C 文件: $c_total"
echo "  - 汇编文件: $asm_total"
echo "  - 头文件: $h_total"
echo ""
echo "统计完成！"
