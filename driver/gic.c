#include "gic.h"
#include "io.h" // 引入你封装的IO API

void gic_init(void) {
  // 1. 初始化GICD分发器
  // 先禁用GICD
  io_write32(GICD_CTLR, 0);

  // 禁用所有中断：遍历32组ICENABLER
  for (int i = 0; i < 32; i++) {
    io_write32(GICD_ICENABLER(i), 0xFFFFFFFF);
  }

  // 启用GICD
  io_write32(GICD_CTLR, 1);

  // 2. 初始化GICC CPU接口
  // 允许所有优先级中断
  io_write32(GICC_PMR, 0xFF);
  // 启用GICC
  io_write32(GICC_CTLR, 1);
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
