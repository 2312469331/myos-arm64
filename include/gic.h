#ifndef __GIC_H__
#define __GIC_H__
#include "types.h"
#include <stdint.h>

// GIC 基地址（初始化为 NULL，将在运行时通过 ioremap 映射）
extern volatile void *GICD_BASE; // 分发器
extern volatile void *GICC_BASE; // CPU接口

// QEMU virt平台 GICv2 物理基地址（默认值）
#define GICD_PHYS_BASE 0x08000000 // 分发器物理地址
#define GICC_PHYS_BASE 0x08010000 // CPU接口物理地址

#define GICD_ISPENDR(n) (GICD_BASE + 0x200 + (n) * 4) // 中断挂起组n (n=0~31)

// ------------------------------
// GICD 寄存器偏移宏（严格对齐 GICv2 布局）
// ------------------------------
#define GICD_CTLR (GICD_BASE + 0x000)                   // 控制寄存器
#define GICD_TYPER (GICD_BASE + 0x004)                  // 类型寄存器
#define GICD_ISENABLER(n) (GICD_BASE + 0x100 + (n) * 4) // 中断使能组n (n=0~31)
#define GICD_ICENABLER(n) (GICD_BASE + 0x200 + (n) * 4) // 中断禁用组n (n=0~31)
// 👇 新增缺失的三个关键宏！
#define GICD_IPRIORITYR(n)                                                     \
  (GICD_BASE + 0x400 + (n) * 4) // 优先级寄存器 (n=0~255)
#define GICD_ITARGETSR(n)                                                      \
  (GICD_BASE + 0x800 + (n) * 4)                     // 目标CPU寄存器 (n=0~255)
#define GICD_ICFGR(n) (GICD_BASE + 0xC00 + (n) * 4) // 触发方式寄存器 (n=0~63)
// --------------------------
// GICC 寄存器偏移宏
// --------------------------
#define GICC_CTLR (GICC_BASE + 0x000) // 控制寄存器
#define GICC_PMR (GICC_BASE + 0x004)  // 优先级掩码
#define GICC_BPR (GICC_BASE + 0x008)  // 断点寄存器
#define GICC_IAR (GICC_BASE + 0x00C)  // 中断应答寄存器
#define GICC_EOIR (GICC_BASE + 0x010) // 中断结束寄存器

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
