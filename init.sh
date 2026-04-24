#!/bin/bash

# 适配目录：include/ 和 kernel/
files=(
  "include/mm.h"
  "include/pmm.h"
  "include/vmm.h"
  "include/kheap.h"
  "include/io.h"
  "kernel/mm.c"
  "kernel/pmm.c"
  "kernel/vmm.c"
  "kernel/kheap.c"
  "kernel/io.c"
)

# 创建文件
for file in "${files[@]}"; do
  dir=$(dirname "$file")
  mkdir -p "$dir"
  touch "$file"
  echo "Created: $file"
done

echo -e "\nDone! Your kernel now has these files:"
ls -1 include/ kernel/
