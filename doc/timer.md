## 一、先把原文核心结论提炼（看懂结构）

1. Cortex‑A53 **每个核心内部** 自带 4 组独立定时器：
   - EL1 物理定时器
   - EL2 物理定时器
   - EL3 物理定时器
   - 虚拟定时器（Virtual Timer）
2. **CPU 内核本身没有系统计数器**
   全局统一的 **System Counter 系统计数器** 放在**外部 SoC 芯片外设**里。
3. SoC 通过外部 64 位总线 `CNTVALUEB[63:0]`
   把全局递增时钟计数值，同步发给每一个 CPU 核心。
4. 附带时钟同步信号 `CNTCLKEN` 做跨时钟域同步。
5. 四个定时器各自独立输出**低电平有效中断**，接到片上中断控制器（你的 GIC）。

***

## 二、极简结构图（对应手册 Figure 10-1 架构）

```text
┌─────────────────────────────────────────────────────────────┐
│                     外部 SoC 芯片                            │
│                                                             │
│  ┌───────────────────┐                                      │
│  │  System Counter    │  全局64位递增计数器                 │
│  │  系统计数器(外部)  │  A53 核内不含这个！                  │
│  └────────┬───────────┘                                      │
│           │
│           ▼  64位同步总线
│  CNTVALUEB[63:0]  全局计数值分发
│           │
│           │  同步时钟使能信号
│        CNTCLKEN
└───────────┬─────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                Cortex-A53 单个核心 Core                      │
│                                                             │
│  【核内私有 定时器寄存器组】                                   │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐          │
│  │EL1物理  │  │EL2物理  │  │EL3物理   │  │虚拟定时   │         │
│  │定时器   │  │定时器    │  │定时器    │  │器       │          │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘          │
│       │            │            │            │               │
│       ▼            ▼            ▼            ▼               │
│  独立中断      独立中断      独立中断      独立中断           │
│  输出信号      输出信号      输出信号      输出信号           │
└─────────────────────────────────────────────────────────────┘
            │                    │
            └────────┬───────────┘
                     ▼
          ┌───────────────────┐
          │  GIC 中断控制器    │
          │                   │
          └───────────────────┘
```

***

## 三、关键结构关系（帮你避坑）

1. **全局时间源不在 CPU 里**
   所有核心共用同一个 SoC 系统计数器，保证多核时间完全同步。
2. **定时器是每个核私有**
   EL1/EL2/EL3/虚拟定时器 都是当前 Core 独占，互不干扰。
3. **工作逻辑**
   - 外部 SoC 不断递增 `CNTVALUE` 全局计数器
   - 核心内定时器读取这个全局值
   - 你写加载比较值 → 计数器匹配时触发定时器中断
4. 所有定时器中断 → 最终接入 GIC 控制器统一管理

***

## 四、配套信号说明（Table10-1 内容）

- `CNTVALUEB[63:0]`：SoC → CPU，64bit 全局时钟计数值
- `CNTCLKEN`：时钟同步使能，保证跨时钟域采样稳定
- `Timer_IRQx`：每个定时器独立中断输出，**低电平有效**

***

## 五、一句话总结

- 全局时钟源：**外部 SoC 系统计数器**
- 定时比较器：**A53 每个核内部 4 个独立定时器**
- 中断归宿：全部接入 **GIC**
  完全独立于你之前的 GICC/GICD 架构，是另一套片上外设中断源。

# Table 10-1 Generic Timer signals

## 原版+中文翻译对照表

| 信号名称             | 功能中文描述                |
| ---------------- | --------------------- |
| nCNTPNSIRQ\[n:0] | 非安全态 物理定时器事件中断        |
| nCNTPSIRQ\[n:0]  | 安全态 物理定时器事件中断         |
| nCNTHPIRQ\[n:0]  | 虚拟化监管器(EL2) 物理定时器事件中断 |
| nCNTVIRQ\[n:0]   | 虚拟定时器事件中断             |

***

### 注释

a. `n` = 集群内核心总数 − 1，每个CPU核心拥有独立的一路定时器中断信号。

### 关键补充

1. 全部是**低电平有效**中断输出，最终接到 GIC ；
2. 这四组就是 A53 四个核内定时器 对应的硬件中断线；
3. 寄存器操作用 **ARM64 系统寄存器(mrs/msr)**，无需MMIO。

# Table 10-2 AArch64 Generic Timer registers

整理规整表格 + 保留原字段、注释、排版对齐

| 寄存器名             | Op0 | CRn | Op1 | CRm | Op2 | 复位值 Reset     | 位宽 Width | 功能描述 Description    |
| ---------------- | --- | --- | --- | --- | --- | ------------- | -------- | ------------------- |
| CNTKCTL\_EL1     | 3   | c14 | 0   | c1  | 0   | -<sup>a</sup> | 32-bit   | 计数器-定时器 内核控制寄存器     |
| CNTFRQ\_EL0      | 3   | c0  | 0   | —   | —   | UNK           | 32-bit   | 计数器-定时器 频率寄存器       |
| CNTPCT\_EL0      | 1   | —   | —   | —   | —   | UNK           | 64-bit   | 物理计数器只读寄存器          |
| CNTVCT\_EL0      | 2   | —   | —   | —   | —   | UNK           | 64-bit   | 虚拟计数器只读寄存器          |
| CNTP\_TVAL\_EL0  | —   | c2  | 0   | —   | —   | UNK           | 32-bit   | 物理定时器 倒计时值寄存器       |
| CNTP\_CTL\_EL0   | —   | 1   | —   | —   | —   | -<sup>b</sup> | 32-bit   | 物理定时器 控制寄存器         |
| CNTP\_CVAL\_EL0  | —   | 2   | —   | —   | —   | UNK           | 64-bit   | 物理定时器 比较阈值寄存器       |
| CNTV\_TVAL\_EL0  | —   | c3  | 0   | —   | —   | UNK           | 32-bit   | 虚拟定时器 倒计时值寄存器       |
| CNTV\_CTL\_EL0   | —   | 1   | —   | —   | —   | <sup>b</sup>  | 32-bit   | 虚拟定时器 控制寄存器         |
| CNTV\_CVAL\_EL0  | —   | 2   | —   | —   | —   | UNK           | 64-bit   | 虚拟定时器 比较阈值寄存器       |
| CNTVOFF\_EL2     | 4   | c0  | 3   | —   | —   | UNK           | 64-bit   | 虚拟计数器 偏移寄存器         |
| CNTHCTL\_EL2     | —   | c1  | 0   | —   | —   | -<sup>c</sup> | 32-bit   | 虚拟化管理器 定时器控制寄存器     |
| CNTHP\_TVAL\_EL2 | —   | c2  | 0   | —   | —   | UNK           | 32-bit   | 超visor物理定时器 倒计时值寄存器 |
| CNTHP\_CTL\_EL2  | —   | 1   | —   | —   | —   | <sup>b</sup>  | 32-bit   | 超visor物理定时器 控制寄存器   |
| CNTHP\_CVAL\_EL2 | —   | 2   | —   | —   | —   | UNK           | 64-bit   | 超visor物理定时器 比较阈值寄存器 |
| CNTPS\_TVAL\_EL1 | 7   | c2  | 0   | —   | —   | UNK           | 32-bit   | 安全态物理定时器 倒计时值寄存器    |
| CNTPS\_CTL\_EL1  | —   | 1   | —   | —   | —   | -<sup>b</sup> | 32-bit   | 安全态物理定时器 控制寄存器      |
| CNTPS\_CVAL\_EL1 | —   | 2   | —   | —   | —   | UNK           | 64-bit   | 安全态物理定时器 比较阈值寄存器    |

***

## 页脚注释（原版原文）

a. 复位值：`bits[9:8, 2:0] = 0b00000`
b. 复位值：`bit[0] = 0`
c. 复位值：`bit[2] = 0`，`bits[1:0] = 0b11`

***

### 关键一句话

✅ 这一整张表 **全部是 AArch64 系统寄存器**
✅ 只用 `mrs / msr` 指令访问
❌ **无物理地址、不是 MMIO、不需要外设基址**
纯 ARMv8 架构标准，所有 A53/A55/A76 通用。

# Table 10-3 AArch32 Generic Timer registers

| 寄存器名        | CRn | Op1 | CRm | Op2 | 复位值 Reset     | 位宽 Width | 功能描述 Description                                     |
| ----------- | --- | --- | --- | --- | ------------- | -------- | ---------------------------------------------------- |
| CNTFRQ      | c14 | 0   | c0  | 0   | UNK           | 32-bit   | Counter-timer Frequency register                     |
| CNTPCT      | —   | 0   | c14 | —   | UNK           | 64-bit   | Counter-timer Physical Count register                |
| CNTKCTL     | c14 | 0   | c1  | 0   | -<sup>a</sup> | 32-bit   | Counter-timer Kernel Control register                |
| CNTP\_TVAL  | c2  | 0   | —   | —   | UNK           | 32-bit   | Counter-timer Physical Timer TimerValue register     |
| CNTP\_CTL   | —   | 1   | —   | —   | -<sup>b</sup> | 32-bit   | Counter-timer Physical Timer Control register        |
| CNTV\_TVAL  | c3  | 0   | —   | —   | UNK           | 32-bit   | Counter-timer Virtual Timer TimerValue register      |
| CNTV\_CTL   | —   | 1   | —   | —   | <sup>b</sup>  | 32-bit   | Counter-timer Virtual Timer Control register         |
| CNTVCT      | —   | 1   | c14 | —   | UNK           | 64-bit   | Counter-timer Virtual Count register                 |
| CNTP\_CVAL  | —   | 2   | —   | —   | UNK           | 64-bit   | Counter-timer Physical Timer CompareValue register   |
| CNTV\_CVAL  | —   | 3   | —   | —   | UNK           | 64-bit   | Counter-timer Virtual Timer CompareValue register    |
| CNTVOFF     | —   | 4   | —   | —   | UNK           | 64-bit   | Counter-timer Virtual Offset register                |
| CNTHCTL     | c14 | 4   | c1  | 0   | -<sup>c</sup> | 32-bit   | Counter-timer Hyp Control register                   |
| CNTHP\_TVAL | c2  | 0   | —   | —   | UNK           | 32-bit   | Counter-timer Hyp Physical Timer TimerValue register |
| CNTHP\_CTL  | —   | 1   | —   | —   | <sup>b</sup>  | 32-bit   | Counter-timer Hyp Physical Timer Control register    |
| CNTHP\_CVAL | —   | 6   | c14 | —   | UNK           | 64-bit   | Counter-timer Hyp Physical CompareValue register     |

***

## 注释

a. 复位值：bits\[9:8, 2:0] = 0b00000
b. 复位值：bit\[0] = 0
c. 复位值：bit\[2] = 0，bits\[1:0] = 0b11

***

### 关键区分

- 本表：**AArch32 专用**，使用 CP15 `mcr / mrc` 指令访问
- 上一张 Table10-2：**AArch64 专用**，使用 `mrs / msr` 访问
- 你 ARM64 内核 **直接忽略本表**，只用 Table10-2 即可

AArch64 通用定时器 CNTFRQ\_EL0 权限 & 陷阱机制详解

1. 核心结论前置

1. CNTFRQ\_EL0：全局系统定时器频率寄存器
2. 只有 EL3 能写，EL0 / EL1 / EL2 禁止写入
3. EL1 / EL2 任意读，无限制
4. EL0 用户态 默认禁止直接读，读就触发系统访问陷阱
5. 固件 BL31/EL3 必须初始化写入频率，不写不会死机，但时间彻底错乱

 

1. MSR 写入规则（写 CNTFRQ\_EL0）

官方硬件伪代码：

plaintext

MSR CNTFRQ\_EL0, Xt
if IsHighestEL(PSTATE.EL) then
CNTFRQ\_EL0 = X\[t, 64];
else
UNDEFINED;
 

规则翻译

-  IsHighestEL  = 当前处于 EL3
- EL3：可以正常写入定时器频率
- EL0 / EL1 / EL2：执行写指令 → 未定义指令异常

关键点

- 整个系统只有 EL3 有权修改定时器基准频率
- Linux 内核、裸机系统 永远不能写 CNTFRQ\_EL0
- 上电必须由 EL3 固件 提前配置好频率

疑问：EL3 不写会不会炸？

- 硬件不会直接崩溃
- 寄存器复位值随机/为0
- 内核读取错误频率 → 时间计算错乱、sleep 异常、调度异常、vDSO 时间全部失效
- 属于功能性报废

 

1. MRS 读取规则（读 CNTFRQ\_EL0）

官方硬件伪代码：

plaintext

if PSTATE.EL == EL0 then
if !ELIsInHost(EL0) && CNTKCTL\_EL1.\<EL0PCTEN,EL0VCTEN> == '00' then
if EL2Enabled() && HCR\_EL2.TGE == '1' then
AArch64.SystemAccessTrap(EL2, 0x18);
else
AArch64.SystemAccessTrap(EL1, 0x18);
elsif ELIsInHost(EL0) && CNTHCTL\_EL2.\<EL0PCTEN,EL0VCTEN> == '00' then
AArch64.SystemAccessTrap(EL2, 0x18);
else
X\[t, 64] = CNTFRQ\_EL0;

elsif PSTATE.EL == EL1 || PSTATE.EL == EL2 || PSTATE.EL == EL3 then
X\[t, 64] = CNTFRQ\_EL0;
 

规则翻译

1. EL1 / EL2 / EL3
无条件直接读取，无任何陷阱、无权限限制
2. EL0 用户态
默认关闭：

-  CNTKCTL\_EL1  位： EL0PCTEN   EL0VCTEN  = 00
- 用户执行  MRS x0, CNTFRQ\_EL0 
- 硬件触发 SystemAccessTrap
- 异常码： 0x18 
- 陷入 EL1 / EL2 内核
  3. 放开权限后
  内核主动置位允许位，用户态才可以直接读

 

1. 为什么 Linux 要做寄存器编码比对？

1. 用户态执行：
asm

mrs x0, CNTFRQ\_EL0
 
2. 硬件触发异常，CPU 只会上报：

- ESR\_ELx.ISS 字段
- 原始 5 个编码： op0 op1 CRn CRm op2 
  3. 硬件不认寄存器名字，只认数字编码
  4. Linux 定义：
  c

\#define sys\_reg(op0,op1,CRn,CRm,op2) ...
\#define SYS\_CNTFRQ\_EL0 sys\_reg(3,3,14,0,0)
 
5. 内核异常处理：

- 取出 ISS 编码
- 和  SYS\_CNTFRQ\_EL0  比对
- 精准识别：用户在读定时器频率
- 内核代为读取、返回、拦截

 

1. 特权级权限总表

特权级 读 CNTFRQ\_EL0 写 CNTFRQ\_EL0
EL0 用户态 默认陷阱，需内核放行 未定义指令异常
EL1 内核态 自由读取 禁止写入
EL2 虚拟化 自由读取 禁止写入
EL3 安全固件 自由读取 唯一可写

 

1. 和你之前代码对应关系

1.  sys64\_hooks\[]  数组

- 专门捕获用户态  mrs  系统寄存器读
  2.  ESR\_ELx\_SYS64\_ISS\_SYS\_CNTFRQ 
- 就是  CNTFRQ\_EL0  的硬件5位编码
  3.  cntfrq\_read\_handler 
- 陷阱回调函数
- 内核代读寄存器，返回给用户态
  4. vDSO
- 用户态不能直接读定时器寄存器
- 依靠内核陷阱 + 代读 + 缓存频率实现高精度时间

 

1. 最简运行流程

1. EL3 固件上电： MSR CNTFRQ\_EL0, X0  写入真实频率
2. EL1 内核启动：只读一次 CNTFRQ\_EL0，缓存频率
3. 内核默认禁止 EL0 直接访问
4. 用户态调用时间函数 → 触发陷阱
5. 内核匹配编码 → 代读返回
6. vDSO 利用缓存频率计算用户态时间

 

1. 一句话终极总结

- 写权限独属于 EL3
- 内核 EL1 只准读、不准改
- 用户 EL0 读就被硬件拦截陷阱
-  op0/op1/CRn/CRm/op2  是硬件原始指令编码，用于内核异常匹配
- 不初始化 CNTFRQ\_EL0 ≠ 死机，时间子系统彻底失效

