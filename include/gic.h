#ifndef __GIC_H__
#define __GIC_H__
#include <types.h>
#include <stdint.h>

// GIC 基地址（初始化为 NULL，将在运行时通过 ioremap 映射）
extern volatile void *GICD_BASE; // 分发器
extern volatile void *GICC_BASE; // CPU接口

// QEMU virt平台 GICv2 物理基地址（默认值）
#define GICD_PHYS_BASE 0x08000000 // 分发器物理地址
#define GICC_PHYS_BASE 0x08010000 // CPU接口物理地址

// ------------------------------
// GICD 寄存器偏移宏（严格对齐 GICv2 布局）
// ------------------------------
#define GICD_CTLR (GICD_BASE + 0x000)                   // 控制寄存器
#define GICD_TYPER (GICD_BASE + 0x004)                  // 类型寄存器
#define GICD_IIDR (GICD_BASE + 0x008)                   // 识别寄存器
#define GICD_IGROUPR(n) (GICD_BASE + 0x80 + (n) * 4)    // 中断分组寄存器 (n=0~31)
#define GICD_ISENABLER(n) (GICD_BASE + 0x100 + (n) * 4) // 中断使能组n (n=0~31)
#define GICD_ICENABLER(n) (GICD_BASE + 0x180 + (n) * 4) // 中断禁用组n (n=0~31)
#define GICD_ISPENDR(n) (GICD_BASE + 0x200 + (n) * 4)   // 中断挂起组n (n=0~31)
#define GICD_ICPENDR(n) (GICD_BASE + 0x280 + (n) * 4)   // 中断清除挂起组n (n=0~31)
#define GICD_ISACTIVER(n) (GICD_BASE + 0x300 + (n) * 4) // 中断激活组n (n=0~31)
#define GICD_ICACTIVER(n) (GICD_BASE + 0x380 + (n) * 4) // 中断清除激活组n (n=0~31)
#define GICD_IPRIORITYR(n)                                                     \
  (GICD_BASE + 0x400 + (n) * 4) // 优先级寄存器 (n=0~255)
#define GICD_ITARGETSR(n)                                                      \
  (GICD_BASE + 0x800 + (n) * 4)                     // 目标CPU寄存器 (n=0~255)
#define GICD_ICFGR(n) (GICD_BASE + 0xC00 + (n) * 4) // 触发方式寄存器 (n=0~63)
#define GICD_NSACR(n) (GICD_BASE + 0xE00 + (n) * 4)  // 非安全访问控制寄存器 (n=0~31)
#define GICD_SGIR (GICD_BASE + 0xF00)                 // 软件生成中断寄存器
#define GICD_CPENDSGIR(n) (GICD_BASE + 0xF10 + (n) * 4) // SGI清除挂起寄存器 (n=0~3)
#define GICD_SPENDSGIR(n) (GICD_BASE + 0xF20 + (n) * 4) // SGI设置挂起寄存器 (n=0~3)
// --------------------------
// GICC 寄存器偏移宏（严格对齐 GICv2 布局）
// --------------------------
#define GICC_CTLR (GICC_BASE + 0x000)   // 控制寄存器
#define GICC_PMR (GICC_BASE + 0x004)    // 优先级掩码寄存器
#define GICC_BPR (GICC_BASE + 0x008)    // 二进制分割寄存器
#define GICC_IAR (GICC_BASE + 0x00C)    // 中断应答寄存器
#define GICC_EOIR (GICC_BASE + 0x010)   // 中断结束寄存器
#define GICC_RPR (GICC_BASE + 0x014)    // 运行优先级寄存器
#define GICC_HPPIR (GICC_BASE + 0x018)  // 最高优先级挂起中断寄存器
#define GICC_ABPR (GICC_BASE + 0x01C)   // 别名二进制分割寄存器
#define GICC_AIAR (GICC_BASE + 0x020)   // 别名中断应答寄存器
#define GICC_AEOIR (GICC_BASE + 0x024)  // 别名中断结束寄存器
#define GICC_AHPPIR (GICC_BASE + 0x028) // 别名最高优先级挂起中断寄存器
#define GICC_APR(n) (GICC_BASE + 0x0D0 + (n) * 4)    // 激活优先级寄存器 (n=0~3)
#define GICC_NSAPR(n) (GICC_BASE + 0x0E0 + (n) * 4)   // 非安全激活优先级寄存器 (n=0~3)
#define GICC_IIDR (GICC_BASE + 0x0FC)   // CPU接口识别寄存器
#define GICC_DIR (GICC_BASE + 0x1000)   // 去激活中断寄存器

// GIC初始化声明
void gic_init(void);
void gic_enable_irq(uint32_t irq_num);
// -------------------------- 新增：中断生命周期上层 API
// -------------------------- 应答中断：读取当前触发的中断号（对应 GICC_IAR
// 寄存器）
uint32_t gic_ack_irq(void);

// 结束中断：通知 GIC 中断处理完成（对应 GICC_EOIR 寄存器）
void gic_eoi_irq(uint32_t irq_num);

// 禁用指定中断（对应 GICD_ICENABLER 寄存器）
void gic_disable_irq(uint32_t irq_num);

// 设置中断优先级（irq_num: 中断号，priority: 0~255，值越小优先级越高）
void gic_set_irq_priority(uint32_t irq_num, uint8_t priority);

// 查询中断是否处于挂起状态（用于调试）
bool gic_is_irq_pending(uint32_t irq_num);

#endif // __GIC_H__
