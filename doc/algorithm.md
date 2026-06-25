# 算法文档

> 每个算法前加一级标题 `## <算法名>`，内容自包含。

---

## buddy_init 内存划分算法

### 目标

给定一段可能不对齐、非 2 的幂次长度的物理内存范围，用最少的初始块将其挂入 buddy 系统的 free_area，保证后续分配/合并正确。

## 核心约束

buddy 系统要求：**一个 order=N 的空闲块，其 PFN 必须 2^N 对齐**。否则两块合并不上。

```
pfn_aligned_to_order(pfn, order):
    return (pfn & ((1 << order) - 1)) == 0
```

## 算法：从大到小试探

```
输入：起始 PFN = P，剩余页数 = R

while R > 0:
    order = MAX_ORDER（10，即 1024 页 / 4MB）
    
    loop:
        block = 2^order
        if block <= R && pfn_aligned_to_order(P, order):
            break           ← 边界 & 对齐同时满足，取这块
        if order == 0:
            break           ← 单页总能同时满足两边
        order--             ← 砍小再试

    取走 P 开始的 2^order 页 → 挂入 free_area[order]
    P += 2^order
    R -= 2^order
```

## 演示

### 场景 1：完美对齐

```
起始 PFN=0，剩余 256 页：

  order=8（256页）→ block=256, R=256, pfn%256=0 → ✅ 全拿走
  一次完成，零碎片
```

### 场景 2：偏移起始

```
起始 PFN=0x403df（从 0x403df000 开始），剩余 200 页：

  循环 1:
    order=10（4096）→ block>R → 砍
    order=9（2048）→ block>R → 砍
    ...
    order=0 （1页） → pfn%1=0 → ✅ 取单页
    P=0x403e0, R=199

  循环 2:
    order=10（4096）→ block>R → 砍
    ...
    order=5  （32页）→ block≤R, pfn%32=0 → ✅ 取 32 页
    P=0x40400, R=167

  循环 3:
    order=10（4096）→ block>R → 砍
    order=9（2048）→ block>R → 砍
    ...
    order=7（128页）→ block≤R, pfn%128=0 → ✅ 取 128 页
    P=0x40480, R=39

  循环 4:
    order=5（32页）→ block≤R, pfn%32=0 → ✅ 取 32 页
    P=0x404a0, R=7

  循环 5:
    order=2（4页）→ block≤R, pfn%4=0 → ✅ 取 4 页
    P=0x404a4, R=3

  循环 6:
    order=1（2页）→ block≤R, pfn%2=0 → ✅ 取 2 页
    P=0x404a6, R=1

  循环 7:
    order=0（1页）→ ✅
    完成
```

### 场景 3：尾部不够整块

```
起始 PFN=0x40000，剩余 1025 页：

  循环 1: order=10（1024页）→ ✅ 取 1024 页
  循环 2: order=0（1页）    → ✅ 取 1 页
  完成
```

## 结果特征

- **前几次循环消耗对齐偏移**：起始不对齐时，用小阶补到对齐边界
- **一旦对齐就跳大阶**：对齐后直接取当前剩余能取的最大 order
- **尾部不足时降阶**：末尾不够整块时自动降阶到能取的最大对齐块
- **初始碎片不超过对齐偏移对应的大小**，后续全部是规则 2^N 块
