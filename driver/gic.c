#include "gic.h"
#include "io.h" // 引入你封装的IO API

void gic_init(void) {
  // 1. 初始化 GICD 分发器
  io_write32(GICD_CTLR, 0); // 先禁用 GICD

  // 禁用所有中断
  for (int i = 0; i < 32; i++) {
    io_write32(GICD_ICENABLER(i), 0xFFFFFFFF);
  }

  // --- 补全：默认优先级 + 目标 CPU + 触发方式（可选，也可以在注册时单独设置）
  for (int i = 0; i < 1024; i++) {
    // 设置默认优先级为 0xA0
    uint32_t prio_idx = i / 4;
    uint32_t prio_shift = (i % 4) * 8;
    io_write32(GICD_IPRIORITYR(prio_idx),
               (io_read32(GICD_IPRIORITYR(prio_idx)) & ~(0xFF << prio_shift)) |
                   (0xA0 << prio_shift));

    // 设置目标 CPU 为 CPU0
    uint32_t target_idx = i / 4;
    uint32_t target_shift = (i % 4) * 8;
    io_write32(
        GICD_ITARGETSR(target_idx),
        (io_read32(GICD_ITARGETSR(target_idx)) & ~(0xFF << target_shift)) |
            (0x01 << target_shift));

    // 设置默认触发方式为电平触发
    uint32_t cfg_idx = i / 16;
    uint32_t cfg_shift = (i % 16) * 2;
    io_write32(GICD_ICFGR(cfg_idx),
               io_read32(GICD_ICFGR(cfg_idx)) & ~(0x3 << cfg_shift));
  }

  // 使能 GICD 组 0 + 组 1（EL1 非安全必须）
  io_write32(GICD_CTLR, 0x3);

  // 2. 初始化 GICC CPU 接口
  io_write32(GICC_PMR, 0xFF); // 允许所有优先级
  io_write32(GICC_CTLR, 1);   // 启用 GICC
}

void gic_enable_irq(uint32_t irq_num) {
  uint32_t idx = irq_num / 32; // 计算中断所在组号
  uint32_t bit = irq_num % 32; // 计算组内bit位
  // 使能指定中断
  io_write32(GICD_ISENABLER(idx), 1 << bit);
}
// 应答中断：获取当前中断号
uint32_t gic_ack_irq(void) { return io_read32((volatile void *)GICC_IAR); }

// 结束中断：通知 GIC 处理完成
void gic_eoi_irq(uint32_t irq_num) {
  io_write32((volatile void *)GICC_EOIR, irq_num);
}

// 禁用指定中断
void gic_disable_irq(uint32_t irq_num) {
  uint32_t n = irq_num / 32; // 每组 32 个中断
  uint32_t bit = irq_num % 32;
  io_write32((volatile void *)(GICD_ICENABLER(n)), 1U << bit);
}

// 设置中断优先级
void gic_set_irq_priority(uint32_t irq_num, uint8_t priority) {
  // GICD_IPRIORITYR 寄存器：每个中断占 8 位，偏移为 irq_num * 4
  volatile void *reg_addr =
      (volatile void *)((uint8_t *)GICD_BASE + 0x400 + (irq_num * 4));
  io_write32(reg_addr, priority);
}

// 查询中断是否挂起
bool gic_is_irq_pending(uint32_t irq_num) {
  uint32_t n = irq_num / 32;
  uint32_t bit = irq_num % 32;
  uint32_t reg_val = io_read32(GICD_ISPENDR(n));
  return (reg_val & (1U << bit)) != 0;
}
