***

# 一、GICv2（Cortex-A53 用的）核心结构图

```text
┌───────────────────────────────────────────────────────────┐
│                      你的 Cortex-A53 多核 CPU              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │
│  │  CPU Core 0 │  │  CPU Core 1 │  │  CPU Core N │       │  每个核都有自己的：
│  │             │  │             │  │             │       │  - 私有中断（SGI/PPI）
│  │ ┌─────────┐ │  │ ┌─────────┐ │  │ ┌─────────┐ │       │  - 私有 CPU 接口
│  │ │ 私有GICC│ │  │ │ 私有GICC│ │  │ │ 私有GICC│ │       │
│  │ │(CPU接口)│ │  │ │(CPU接口)│ │  │ │(CPU接口)│ │       │
│  │ └─────────┘ │  │ └─────────┘ │  │ └─────────┘ │       │
│  └─────────────┘  └─────────────┘  └─────────────┘       │
└───────────────────────────────────────────────────────────┘
        │                   │                   │
        └───────────┬───────┴───────────┐───────┘
                    │                   │
            ┌───────▼───────┐   ┌───────▼───────┐
            │  GICD 分发器  │   │  （可选）虚拟化模块 │
            │ （全局公共）  │   │  GICH/GICV       │
            └───────────────┘   └─────────────────┘
                    │
                    ▼
            所有外设中断源（SPI）
```

***

# 二、按模块说死：必须用什么方式访问

我分模块讲，每个模块标清楚「必须用什么」，没有模糊地带。

***

## 1. 全局模块：GICD 分发器（Distributor）

✅ **必须 100% 用 MMIO 地址访问，没有指令方式！**

- 基址：你的芯片手册会给，比如很多平台是 `PERIPHBASE + 0x1000`
- 作用：
  - 管理所有全局外设中断（SPI）
  - 设置中断使能、优先级、目标CPU、触发方式（电平/边沿）
- 例子：
  ```c
  #define GICD_BASE 0x2C001000
  #define GICD_ISENABLER0  (*(volatile uint32_t *)(GICD_BASE + 0x100))
  ```
- 结论：你没有别的选择，只能用MMIO。

***

## 2. 每个核私有模块：GICC CPU 接口（CPU Interface）

⚠️ **这里是你最乱的地方：它有两套独立的访问方式，你二选一就行！**
GICC 是每个CPU核自己私有的，用来管理当前核的中断，它有两套寄存器，完全不冲突：

### 方式A：通用 MMIO 寄存器（你第一张表 Table 9-2 的那些）

✅ **所有架构（AArch32/AArch64）都能用，必须用** **`ldr/str`** **地址访问！**

- 基址：`PERIPHBASE + 0x00000`（你第一张图的 `0x00000-0x01FFF` 段）
- 常用寄存器：
  | 偏移   | 寄存器名        | 作用                    |
  | ---- | ----------- | --------------------- |
  | 0x00 | `GICC_CTLR` | CPU接口控制（开/关中断接收）      |
  | 0x04 | `GICC_PMR`  | 中断优先级掩码（屏蔽低优先级中断）     |
  | 0x0C | `GICC_IAR`  | 中断确认（读这个获取当前最高优先级中断号） |
  | 0x10 | `GICC_EOIR` | 中断结束（写这个标记中断处理完成）     |
- 优点：C语言直接写，不用汇编，调试方便，跨架构通用。
- 缺点：比指令方式慢一点，要走内存总线。

### 方式B：AArch64 专用系统寄存器（你第四张表 Table 9-4 的那些）

✅ **只能用AArch64的** **`mrs/msr`** **指令访问，没有地址，不能用MMIO！**

- 这些是AArch64特有的系统寄存器，直接用CPU指令操作，不走内存。
- 常用寄存器：
  | 寄存器名            | 指令操作                    | 对应MMIO寄存器   | 作用    |
  | --------------- | ----------------------- | ----------- | ----- |
  | `ICC_PMR_EL1`   | `msr ICC_PMR_EL1, x0`   | `GICC_PMR`  | 优先级掩码 |
  | `ICC_IAR0_EL1`  | `mrs x0, ICC_IAR0_EL1`  | `GICC_IAR`  | 中断确认  |
  | `ICC_EOIR0_EL1` | `msr ICC_EOIR0_EL1, x0` | `GICC_EOIR` | 中断结束  |
- 优点：速度快，不用访问内存，内核里用很方便。
- 缺点：必须用汇编指令，C语言要内嵌asm，只能AArch64用。

### 划重点：GICC的两套方式，**你二选一就行，不用都用！**

比如你初始化的时候：

- 要么全用MMIO：写`GICC_CTLR`、`GICC_PMR`这些地址。
- 要么全用指令：写`ICC_PMR_EL1`、`ICC_IAR0_EL1`这些系统寄存器。
- 不建议混着用，容易搞乱状态。

***

## 3. 虚拟化相关模块：GICH/GICV（Virtual Interface）

❌ **你不搞虚拟化的话，完全不用管！**

- 这些是给Hypervisor用的，用来管理虚拟机的虚拟中断。
- 访问方式：**纯MMIO地址访问**，没有指令方式。
- 普通裸机内核直接忽略这部分，不用碰。

***

# 三、终极总结表，一眼看懂

| 模块                   | 访问方式  | 必须用什么？           | 你用不用得上？   |
| -------------------- | ----- | ---------------- | --------- |
| GICD 分发器             | 纯MMIO | 必须用MMIO地址访问      | ✅ 必须用     |
| GICC CPU接口（通用版）      | MMIO  | 必须用MMIO地址访问      | ✅ 可选，简单好写 |
| GICC CPU接口（AArch64版） | 指令    | 必须用`mrs/msr`指令访问 | ✅ 可选，性能高  |
| GICH/GICV 虚拟化模块      | 纯MMIO | 必须用MMIO地址访问      | ❌ 裸机不用    |

***

# 四、给你个最稳的入门方案（适合你现在的情况）

如果你刚写内核，不想搞汇编，直接用**全MMIO方式**，就不会乱：

1. 初始化GICD：设置中断使能、优先级、目标CPU。
2. 初始化GICC：用MMIO写`GICC_CTLR`、`GICC_PMR`、`GICC_BPR`。
3. 中断处理：读`GICC_IAR`获取中断号，处理完写`GICC_EOIR`。
4. 完全不用碰那些`ICC_*_EL1`的指令寄存器，也不用管虚拟化部分。

等你驱动跑通了，再想优化性能，再换成AArch64的指令方式也不迟。

***

要不要我帮你写一份**全MMIO方式的GIC初始化代码模板**，你直接改个基址就能用？

### 原文提取

***

#### 英文原文

The Cortex-A53 GIC CPU Interface implements a memory-mapped interface. The memory-mapped interface is offset from PERIPHBASE. Table 9-1 lists the address ranges.

**Table 9-1 Memory Map**

| Address range   | Functional block               |
| --------------- | ------------------------------ |
| 0x00000–0x01FFF | CPU Interface                  |
| 0x02000–0x0FFFF | Reserved                       |
| 0x10000–0x10FFF | Virtual Interface Control      |
| 0x11000–0x1FFFF | Reserved                       |
| 0x20000–0x21FFF | Virtual CPU Interface          |
| 0x22000–0x2EFFF | Reserved                       |
| 0x2F000–0x30FFF | Alias of Virtual CPU Interface |
| 0x31000–0x3FFFF | Reserved                       |

Note
These registers are not available if GICCDISABLE is asserted.

***

#### 中文翻译

Cortex‑A53 的 GIC CPU 接口采用\*\*内存映射（MMIO）\*\*方式实现。
该内存映射区域以 `PERIPHBASE` 为基地址进行偏移寻址，下表 9-1 列出了完整地址范围。

**表9-1 内存映射分布**

| 地址范围            | 功能模块                 |
| --------------- | -------------------- |
| 0x00000–0x01FFF | CPU 硬件接口（GICC 物理寄存器） |
| 0x02000–0x0FFFF | 保留区域                 |
| 0x10000–0x10FFF | 虚拟化接口控制模块            |
| 0x11000–0x1FFFF | 保留区域                 |
| 0x20000–0x21FFF | 虚拟 CPU 接口            |
| 0x22000–0x2EFFF | 保留区域                 |
| 0x2F000–0x30FFF | 虚拟 CPU 接口 别名映射       |
| 0x31000–0x3FFFF | 保留区域                 |

> 备注：
> 若 `GICCDISABLE` 信号有效，以上所有映射寄存器都会失效、不可访问。

***

### 对你最重要结论（划重点）

1. `CPU Interface` 👉 **0x00000\~0x01FFF**
   👉 就是你 **Table9-2 全部 GICC 寄存器** 的MMIO基址段
2. 虚拟化模块你裸机内核**完全不用**
3. 再次实锤：

- GICC 物理寄存器 = **PERIPHBASE + 偏移 → MMIO地址访问**
- 和 `mrs/msr` 系统指令寄存器 是**两套完全独立**的东西

# 一、Table 9-2 完整整理表格（你给的原版）

| 偏移 Offset | 寄存器名 Name    | 读写 Type | 复位值 Reset          | 功能简述 Description |
| --------- | ------------ | ------- | ------------------ | ---------------- |
| 0x0000    | GICC\_CTLR   | RW      | 0x00000000         | CPU 接口控制寄存器      |
| 0x0004    | GICC\_PMR    | RW      | 0x00000000         | 中断优先级掩码寄存器       |
| 0x0008    | GICC\_BPR    | RW      | 安全:0x02 / 非安全:0x03 | 二进制分割寄存器         |
| 0x000C    | GICC\_IAR    | RO      | —                  | 中断确认寄存器          |
| 0x0010    | GICC\_EOIR   | WO      | —                  | 中断结束寄存器          |
| 0x0014    | GICC\_RPR    | RO      | 0x000000FF         | 当前运行优先级寄存器       |
| 0x0018    | GICC\_HPPIR  | RO      | 0x000003FF         | 最高待处理中断寄存器       |
| 0x001C    | GICC\_ABPR   | RW      | 0x00000003         | 别名二进制分割寄存器       |
| 0x0020    | GICC\_AIAR   | RO      | —                  | 别名中断确认寄存器        |
| 0x0024    | GICC\_AEOIR  | WO      | —                  | 别名中断结束寄存器        |
| 0x0028    | GICC\_AHPPIR | RO      | 0x000003FF         | 别名最高待处理中断寄存器     |
| 0x00D0    | GICC\_APR0   | RW      | 0x00000000         | 激活优先级寄存器         |
| 0x00E0    | GICC\_NSAPR0 | RW      | 0x00000000         | 非安全激活优先级寄存器      |
| 0x00FC    | GICC\_IIDR   | RO      | 0x0034443B         | CPU 接口版本/ID 寄存器  |
| 0x1000    | GICC\_DIR    | WO      | —                  | 中断灭活寄存器          |

***

# 二、关键结论（专治你混乱）

## 1️⃣ 上面这一整张表里所有寄存器

✅ **全部：MMIO 物理地址访问**
✅ **全部：基址 + 偏移，`volatile`** **指针 / LDR/STR 读写**
❌ **完全不能用 mrs/msr 指令**

***

## 2️⃣ 再给你一刀切划分界线（ARM64 GICv2）

### ① 必须【MMIO 地址访问】（只用 ldr/str）

1. `GICD` 全局分发器 所有寄存器
2. 本表全部 `GICC_xxx` 寄存器（Table9-2）
3. `GICH/GICV` 虚拟化相关寄存器

### ② 必须【专用指令访问】（只用 mrs/msr，无物理地址）

`ICC_xxx_EL1` 系列系统寄存器

- `ICC_PMR_EL1`
- `ICC_IAR0_EL1`
- `ICC_EOIR0_EL1`
  …
  👉 这一组**没有任何地址、没有偏移**，只能汇编指令操作

***

# 三、最简选择方案（你写内核直接照抄）

1. 新手、C语言好写、不出错
   👉 **只用 上面这张表的 MMIO GICC 寄存器**
2. 高性能、纯汇编/内嵌汇编
   👉 丢掉这张表，改用 `ICC_xxx_EL1` 指令寄存器

**二选一，绝对不要混用两套！**

## 完整汉化翻译版 · Table 9-3

### 标题：**AArch32 架构 GIC CPU 接口 系统寄存器访问表**

> 备注：
> 本表**全是32位ARM专属指令访问**（`mcr/mrc`）
> 无物理地址、**不是MMIO**，你的 **ARM64 内核直接无视、不用**

| 寄存器名称        | CRn | op1 | CRm | op2 | 读写 | 功能中文描述          |
| ------------ | --- | --- | --- | --- | -- | --------------- |
| ICC\_PMR     | c4  | 0   | c6  | 0   | 读写 | 中断优先级掩码寄存器      |
| ICC\_IAR0    | c12 | 0   | c0  | 0   | 只读 | 组0 中断确认寄存器      |
| ICC\_EOIR0   | c8  | 1   | c0  | 0   | 只写 | 组0 中断结束寄存器      |
| ICC\_HPPIR0  | c12 | 0   | c2  | 0   | 只读 | 组0 最高优先级挂起中断寄存器 |
| ICC\_BPR0    | c12 | 0   | c3  | 0   | 读写 | 组0 二分点寄存器       |
| ICC\_AP0R0   | c12 | 0   | c4  | 0   | 读写 | 组0 激活优先级寄存器     |
| ICC\_AP1R0   | c9  | 0   | c0  | 0   | 读写 | 组1 激活优先级寄存器     |
| ICC\_DIR     | c11 | 1   | c0  | 0   | 只写 | 中断解除激活寄存器       |
| ICC\_RPR     | c12 | 0   | c3  | 0   | 只读 | 当前运行中断优先级寄存器    |
| ICC\_IAR1    | c12 | 0   | c0  | 0   | 只读 | 组1 中断确认寄存器      |
| ICC\_EOIR1   | c8  | 1   | c0  | 0   | 只写 | 组1 中断结束寄存器      |
| ICC\_HPPIR1  | c12 | 0   | c2  | 0   | 只读 | 组1 最高优先级挂起中断寄存器 |
| ICC\_BPR1    | c12 | 0   | c3  | 0   | 读写 | 组1 二分点寄存器       |
| ICC\_CTLR    | c12 | 0   | c4  | 0   | 读写 | CPU 接口控制寄存器     |
| ICC\_SRE     | c12 | 0   | c5  | 0   | 读写 | 系统寄存器访问使能寄存器    |
| ICC\_IGRPEN0 | c12 | 0   | c6  | 0   | 读写 | 组0 中断全局使能       |
| ICC\_IGRPEN1 | c12 | 0   | c7  | 0   | 读写 | 组1 中断全局使能       |
| ICC\_SGI1R   | —   | —   | —   | —   | 读写 | 组1 软件生成中断寄存器    |
| ICC\_ASGI1R  | c12 | 0   | c0  | 0   | 只写 | 别名 组1 软件中断寄存器   |
| ICC\_SGI0R   | c12 | 0   | c2  | 0   | 只写 | 组0 软件生成中断寄存器    |
| ICC\_MCTLR   | c12 | 0   | c6  | 4   | 读写 | 监控模式控制寄存器       |
| ICC\_MSRE    | c12 | 0   | c5  | 0   | 读写 | 监控模式 系统寄存器访问使能  |
| ICC\_MGRPEN1 | c12 | 0   | c7  | 0   | 读写 | 监控模式 组1 中断使能    |

***

## 关键区分（彻底治乱）

1. **Table 9-2（你上一张）**
   `GICC_xxx` → **MMIO 地址/内存访问** ✅ 你ARM64可用
2. **Table 9-3（本表）**
   `ICC_xxx` + CRn/op1 配置 → **32位专属CP15指令** ❌ 你完全不用
3. **Table 9-4（下一张）**
   `ICC_xxx_EL1` → **ARM64专属 mrs/msr 指令** ✅ 你64位真正要用

需要我顺便把 **Table 9-4 ARM64指令那张** 也整理+翻译吗？

# Table 9-4 中英对照完整版（AArch64 专用）

标题：**AArch64 架构 GIC CPU 接口 系统寄存器访问表**
✅ 这张 = **你的 ARM64 内核真正能用的指令寄存器**
✅ 全部：`mrs / msr` 专用指令访问
❌ 无物理地址、**不是 MMIO**

| 寄存器名              | 读写 | 中文翻译描述             |
| ----------------- | -- | ------------------ |
| ICC\_PMR\_EL1     | RW | 中断优先级掩码寄存器         |
| ICC\_IAR0\_EL1    | RO | 组0 中断确认寄存器         |
| ICC\_EOIR0\_EL1   | WO | 组0 中断结束寄存器         |
| ICC\_HPPIR0\_EL1  | RO | 组0 最高待处理中断寄存器      |
| ICC\_BPR0\_EL1    | RW | 组0 二分点寄存器          |
| ICC\_AP0R0\_EL1   | RW | 组0 激活优先级寄存器        |
| ICC\_AP1R0\_EL1   | RW | 组1 激活优先级寄存器        |
| ICC\_DIR\_EL1     | WO | 中断解除激活寄存器          |
| ICC\_RPR\_EL1     | RO | 当前运行优先级寄存器         |
| ICC\_SGI1R\_EL1   | WO | 组1 软件中断生成寄存器       |
| ICC\_ASGI1R\_EL1  | WO | 别名 组1 软件中断寄存器      |
| ICC\_SGI0R\_EL1   | WO | 组0 软件中断生成寄存器       |
| ICC\_IAR1\_EL1    | RO | 组1 中断确认寄存器         |
| ICC\_EOIR1\_EL1   | WO | 组1 中断结束寄存器         |
| ICC\_HPPIR1\_EL1  | RO | 组1 最高待处理中断寄存器      |
| ICC\_BPR1\_EL1    | RW | 组1 二分点寄存器          |
| ICC\_CTLR\_EL1    | RW | CPU 接口控制寄存器（EL1）   |
| ICC\_SRE\_EL1     | RW | 系统寄存器访问模式使能（EL1）   |
| ICC\_IGRPEN0\_EL1 | RW | 组0 中断全局使能寄存器       |
| ICC\_IGRPEN1\_EL1 | RW | 组1 中断全局使能寄存器       |
| ICC\_CTLR         | RW | EL3 层级 CPU 接口控制寄存器 |
| ICC\_SRE\_EL3     | RW | EL3 系统寄存器访问使能      |
| ICC\_GRPEN1\_EL3  | RW | EL3 组1 中断使能        |

***

# 三张表终极区分（从此彻底不混乱）

1. **Table 9-2（GICC\_xxx）**
   👉 纯 **MMIO 地址访问**（基址+偏移）
   👉 C 语言直接写，ARM64 可用
2. **Table 9-3（AArch32 ICC）**
   👉 纯 **32位 CP15 指令 mcr/mrc**
   👉 **你的 ARM64 完全不用**
3. **Table 9-4（AArch64 ICC\_xxx\_EL1）**
   👉 纯 **64位 系统指令 mrs/msr**
   👉 **你写纯 ARM64 内核首选、性能更高**

***

# 最简一句话选择

- 想简单快速写驱动、纯C：用 **Table9-2 MMIO**
- 想标准ARM64写法、高性能：用 **Table9-4 指令寄存器**
- 永远别碰：**Table9-3**

# 4.1.5 小节 完整精翻 + 裸机/GICv2 实操提炼

（适配你当前 **Cortex-A53 + GICv2 + 安全扩展 + EL1 非安全** 环境）

***

## 本节主题

GIC 分发器(Distributor)、CPU 接口(CPU Interface) 的**使能/关闭规则**，
以及**中断分组（Group0/Group1）** 带来的行为差异。

***

## 一、支持中断分组的硬件（你的设备就是）

> 所有 **GICv2**、带安全扩展的 GICv1 都支持中断分组
>
> - Group0：**安全中断**（Secure）
> - Group1：**非安全中断**（Non-Secure，你 Linux/裸机 EL1 用的就是这组）

### 两组独立开关（核心）

1. 分发器端 `GICD_CTLR`

- `EnableGrp0`：允许 Group0 中断 从分发器转发到CPU接口
- `EnableGrp1`：允许 Group1 中断 从分发器转发到CPU接口

1. CPU 接口端 `GICC_CTLR`

- `EnableGrp0`：CPU 接口是否向核上报 Group0 中断
- `EnableGrp1`：CPU 接口是否向核上报 Group1 中断

***

## 二、分发器 开关规则

### 1）`EnableGrp0 = 0 && EnableGrp1 = 0`

- 分发器**不转发任何挂起中断**到 CPU 接口
- 读取 `GICC_IAR / GICC_HPPIR` 等寄存器，全部返回**虚假中断号（Spurious）**
- 软件仍可正常读写 GICD 配置寄存器
- 边沿中断是否置挂起、SGI 是否可以触发：由芯片厂商自定义

### 2）只开其中一组、另一组关闭

举例：

- 只开 Grp1，但若**当前最高优先级挂起中断是 Grp0**
- ➜ 分发器**直接不转发任何中断**

### 安全隔离关键建议（ARM 强制规范）

1. 所有 **Group0 安全中断** 优先级 **必须高于** Group1 非安全中断
2. 防止非安全软件清空 `EnableGrp1` 恶意阻塞安全中断
3. 安全侧开启 Grp1 时，要么同时开 Grp0，要么确保没有 Grp0 挂起中断

***

## 三、CPU 接口 开关规则（重点，你写中断初始化必用）

### 情况1：`GICC_CTLR.AckCtl = 0`（**推荐、标配、裸机必用**）

ARM 已废弃 AckCtl=1，**统一设为 0**

- `EnableGrp0 = 0`
  Grp0 中断不上报CPU，读 IAR 返回虚假中断
- `EnableGrp0 = 1`
  正常接收上报 Grp0 中断
- `EnableGrp1 = 0`
  Grp1 中断不上报CPU
- `EnableGrp1 = 1`
  正常接收上报 Grp1 中断（**你的定时器PPI、普通外设中断都靠这个**）

***

## 四、不支持中断分组的硬件（了解即可，你不用）

仅无安全扩展的老旧 GICv1：

- 只有全局总开关：
  - `GICD_CTLR.Enable` 分发器总使能
  - `GICC_CTLR.Enable` CPU接口总使能
    无 Group0/Group1 划分。

***

## 五、复位默认行为（非常重要）

- 复位后：**非安全域的 GICD\_CTLR / GICC\_CTLR 中** **`EnableGrp1`** **默认清零=0**
  👉 所以你**裸机初始化 GIC 必须手动置位 EnableGrp1**，否则所有非安全中断全部收不到。

***

# ✅ 给你提炼【你写代码直接照抄的初始化逻辑】

适配：A53 + GICv2 + 非安全EL1 + 定时器PPI(Grp1)

1. 初始化中断分组：所有外设/PPI/SGI 划归 **Group1**
2. 分发器：
   ```
   GICD_CTLR |= (1 << 1);  // EnableGrp1 = 1
   ```
3. 每个核的 CPU 接口：
   ```
   GICC_CTLR |= (1 << 1);  // EnableGrp1 = 1
   GICC_CTLR &= ~(1 << 2);// AckCtl = 0 标准配置
   ```
4. 再配置中断优先级、触发方式、目标核，定时器中断就能正常触发。

***

# 极简背诵版

1. GICv2 分两组：Grp0 安全、Grp1 非安全
2. 分发器 + CPU接口 各有独立 Grp0/Grp1 使能位
3. 非安全裸机只需要打开 **EnableGrp1**
4. `AckCtl` 固定写 0，不要碰
5. 复位默认 Grp1 关闭，**不手动开就没有任何中断**

# 给你直接划重点：**裸机 ARM64 + GICv2 + 定时器/外设中断 必学必写寄存器**

只记**必须用的**，剩下全部不用看、不用管、不用碰。

***

## 一、GICD 分发器寄存器（全局、所有核共用）

### ✅ 必看必写（核心 9 个）

1. **GICD\_CTLR**
   分发器总控制
   开 Group0 / Group1 中断全局使能，你必须开 `EnableGrp1`
2. **GICD\_IGROUPRn**
   中断分组
   把 定时器PPI、外设中断 全部设为 **Group1（非安全）**
3. **GICD\_ISENABLERn**
   中断**使能**（置1打开中断）
   你的 Timer PPI 0x0a 就在这里开启
4. **GICD\_ICENABLERn**
   关闭中断，初始化/关闭设备用
5. **GICD\_IPRIORITYRn**
   中断优先级配置
   决定谁先抢占谁
6. **GICD\_ITARGETSRn**
   中断目标核
   把定时器/PPI 绑定到对应 CPU 核心
7. **GICD\_ICFGRn**
   中断触发方式
   边沿/电平触发，定时器、GPIO 必配
8. **GICD\_ISPENDRn / GICD\_ICPENDRn**
   手动置挂起、清挂起，调试排错必备
9. **GICD\_TYPER**
   只读，查看最大中断数、PPI/SGI 数量，调试用

### ❌ 完全不用管

GICD\_IIDR、NSACR、SGIR、SGI 挂起寄存器、识别寄存器
裸机简单中断驱动 **一律跳过**

***

## 二、GICC CPU 接口寄存器（**每核独立**，非常重要）

### ✅ 必看必写（核心 6 个）

1. **GICC\_CTLR**
   CPU 接口控制
   开启本核 Group1 中断接收、固定 `AckCtl=0`
2. **GICC\_PMR**
   优先级掩码
   设置「最低允许响应的中断级别」
   裸机一般直接全开：`PMR = 0xFF`
3. **GICC\_BPR**
   二进制分割寄存器
   优先级分组分割，简单裸机固定设为 **3**
4. **GICC\_IAR**
   中断应答寄存器
   **中断进来第一件事：读这个寄存器 → 拿到中断号**
5. **GICC\_EOIR**
   中断结束寄存器
   **中断处理完最后一步：写这个 → 清除中断**
6. **GICC\_HPPIR**
   最高优先级挂起中断，调试排错神器

### ❌ 完全不用管

ABPR/AIAR/AEOIR/AHPPIR、APR/NSAPR、IIDR、DIR
虚拟化、安全隔离、高级优先级抢占才用，你现在**完全用不上**

***

# 三、给你一套「最简 GIC 初始化流程」

按这个顺序配，你的 timer 中断直接能跑：

1. GICD\_CTLR 开启 Grp1 全局转发
2. GICD\_IGROUPR：所有中断设为 Group1
3. GICD\_ICFGR：配置定时器PPI 触发方式
4. GICD\_IPRIORITYR：设置中断优先级
5. GICD\_ITARGETSR：绑定到当前CPU
6. GICD\_ISENABLER：使能对应中断号
7. 本核 GICC\_CTLR：开启 Grp1 接收、AckCtl=0
8. GICC\_PMR = 0xFF 放行所有优先级
9. GICC\_BPR = 3 固定配置

***

# 四、极简背诵版

- **GICD** = 全局总开关+路由+配置（所有中断统一管理）
- **GICC** = 单个CPU核心的中断接收+应答+结束
- 只背：
  `CTLR` `IGROUPR` `ISENABLER` `IPRIORITYR` `ITARGETSR` `ICFGR`
  `IAR` `EOIR` `PMR` `BPR`

需要我把这几个寄存器**关键位定义 + 裸机标准配置代码**直接写给你吗？

# 我直接给你**定时器中断不触发的终极排查顺序**

你现在的情况：
**CNTP\_TVAL\_EL0 倒计时到 0 → 但 CPU 完全没反应、不进 IRQ**

按照下面 **顺序一步一步查**，100% 能定位问题！
（从最容易错 → 最核心 → 最后才是硬件）

***

# 🔥 第一步（90% 人错在这里）：检查**定时器控制寄存器**

## 寄存器：`CNTP_CTL_EL0`

必须等于 **0b11**（使能+中断使能）

```
bit[0] = 1  → 定时器使能
bit[1] = 1  → 中断使能
bit[2]      → 状态位（到0会变1）
```

### GDB 查看命令：

```gdb
i r cntp_ctl_el0
```

### 必须看到：

```
cntp_ctl_el0 = 0x3
```

如果不是 **0x3** → **定时器根本没开！**

***

# 🔥 第二步：检查定时器是否真的到 0（ISTATUS）

## 还是看 `CNTP_CTL_EL0` 的 **bit\[2]**

```
bit[2] == 1  表示：计数器已到0，中断已触发！
```

### GDB：

```gdb
i r cntp_ctl_el0
```

### 如果 bit2=1

说明：**定时器工作正常，问题出在 GIC 或 CPU 中断屏蔽！**

### 如果 bit2=0

说明：**计数器根本没减到 0，你值写太大/时钟不对**

***

# 🔥 第三步：检查 CPU 是否关闭了中断（DAIF）

## 寄存器：DAIF（中断掩码）

```
bit[I] == 1  → IRQ 被屏蔽！！！
```

### GDB 查看：

```gdb
i r daif
```

### 如果看到 **I=1**

```
daif = 0xxx1xxxx
```

→ **IRQ 被关了，CPU 不收任何中断！**

### 解决：开中断

```asm
msr daifclr, #2
```

***

# 🔥 第四步：检查 GICD 分发器是否使能了该中断

## 寄存器：GICD\_ISENABLERn

你的定时器中断是 **PPI 10（0x0a）**

```
GICD_ISENABLER[10] 必须 = 1
```

### GDB 查看（假设 GICD\_BASE=0x80000000）

```gdb
x /w 0x80000100
```

### bit10 必须是 1

如果是 0 → **GIC 没开这个中断**

***

# 🔥 第五步：检查 GICD\_CTLR（全局开关）

## 寄存器：GICD\_CTLR

```
bit[1] = 1 → EnableGrp1
```

```gdb
x /w 0x80000000
```

必须是 **0x2** 或 **0x3**

***

# 🔥 第六步：检查 GICC 接口使能

## 寄存器：GICC\_CTLR

```
bit[1] = 1 → Group1 中断使能
```

```gdb
x /w 0x80001000
```

***

# 🔥 第七步：检查 GICC\_PMR（优先级掩码）

## 必须设为 0xFF（全开）

```gdb
x /w 0x80001004
```

必须 = `0xFF`

***

# 🔥 第八步：检查 GICC\_BPR

## 必须设为 3

```gdb
x /w 0x80001008
```

必须 = `3`

***

# 🔥 第九步：检查中断是否进入 GIC（GICC\_HPPIR）

## 寄存器：GICC\_HPPIR

```
读取当前最高优先级中断
```

```gdb
x /w 0x80001014
```

### 如果看到 **10**

说明：**中断已经到 GIC，但 CPU 没收！**
问题：DAIF 关中断 / VBAR 异常向量表错

***

# 🔥 第十步：检查异常向量表 VBAR\_EL1

```gdb
i r vbar_el1
```

必须指向你的 `vector_table_el1`

***

# 我给你 **最快排查口诀（背下来）**

1. 看 `CNTP_CTL_EL0` 是不是 0x3
2. 看 `CNTP_CTL_EL0` bit2 是否 1（触发）
3. 看 `daif` 是否 I=1（屏蔽）
4. 看 `GICD_ISENABLER` bit10 是否 1
5. 看 `GICD_CTLR` bit1 是否 1
6. 看 `GICC_CTLR` bit1 是否 1
7. 看 `GICC_PMR` 是否 0xFF
8. 看 `GICC_BPR` 是否 3
9. 看 `GICC_HPPIR` 是否 10
10. 看 `VBAR_EL1` 是否正确

***

# 你现在只需要告诉我：

## 1）`cntp_ctl_el0` 的值

## 2）`daif` 的值

## 3）`GICC_HPPIR` 的值

我能**立刻告诉你问题在哪**！
