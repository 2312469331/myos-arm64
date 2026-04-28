#include <gic.h>
#include <io.h>
#include <vmalloc.h>
#include <compiler.h>
volatile void *GICD_BASE = NULL;
volatile void *GICC_BASE = NULL;

void gic_init(void) {
  GICD_BASE = ioremap(GICD_PHYS_BASE, 0x10000);
  GICC_BASE = ioremap(GICC_PHYS_BASE, 0x10000);
  
  if (!GICD_BASE || !GICC_BASE) {
    GICD_BASE = (volatile void *)GICD_PHYS_BASE;
    GICC_BASE = (volatile void *)GICC_PHYS_BASE;
  }

  io_write32(GICC_CTLR, 0x1); // 非安全端写0x1即可，开启Group1
  for (int i = 0; i < 32; i++) {
    io_write32(GICD_ICENABLER(i), 0xFFFFFFFF);
  }

  for (int i = 0; i < 1024; i++) {
    uint32_t grp_idx = i / 32;
    uint32_t grp_bit = i % 32;
    io_write32(GICD_IGROUPR(grp_idx),
               io_read32(GICD_IGROUPR(grp_idx)) | (1 << grp_bit));

    uint32_t prio_idx = i / 4;
    uint32_t prio_shift = (i % 4) * 8;
    io_write32(GICD_IPRIORITYR(prio_idx),
               (io_read32(GICD_IPRIORITYR(prio_idx)) & ~(0xFF << prio_shift)) |
                   (0xA0 << prio_shift));

    uint32_t target_idx = i / 4;
    uint32_t target_shift = (i % 4) * 8;
    io_write32(
        GICD_ITARGETSR(target_idx),
        (io_read32(GICD_ITARGETSR(target_idx)) & ~(0xFF << target_shift)) |
            (0x01 << target_shift));

    uint32_t cfg_idx = i / 16;
    uint32_t cfg_shift = (i % 16) * 2;
    io_write32(GICD_ICFGR(cfg_idx),
               io_read32(GICD_ICFGR(cfg_idx)) & ~(0x3 << cfg_shift));
  }

  //当前非安全状态，bit1根本写不进去
  /*
 bit[0]  0 interrupts not forwarded.
         1 interrupts forwarded, subject to the priority rules.
  */
  // 开启Group1中断转发（非安全端只能写0x1）
  io_write32(GICD_CTLR, 0x1);
  // 开启CPU接口对Group1中断的信号发送
  io_write32(GICC_CTLR, 0x1); // 非安全端写0x1即可，开启Group1

  io_write32(GICC_PMR, 0xFF);
  io_write32(GICC_BPR, 3);

  __enable_irq();
}

void gic_enable_irq(uint32_t irq_num) {
  uint32_t idx = irq_num / 32;
  uint32_t bit = irq_num % 32;
  // // 先读当前值
  // uint32_t nsacr1 = io_read32(GICD_BASE + 0x0E0);
  // // 把 bit10 置1，允许非安全使用该中断
  // nsacr1 |= (1 << bit);
  // io_write32(GICD_BASE + 0x0E0, nsacr1);
      uint32_t val;

  val=io_read32(GICD_IGROUPR(0));
  io_write32(GICD_IGROUPR(0), val|(1<<bit));
  io_write32(GICD_ISENABLER(idx), 1 << bit);
}

uint32_t gic_ack_irq(void) { return io_read32((volatile void *)GICC_IAR); }

void gic_eoi_irq(uint32_t irq_num) {
  io_write32((volatile void *)GICC_EOIR, irq_num);
}

void gic_disable_irq(uint32_t irq_num) {
  uint32_t n = irq_num / 32;
  uint32_t bit = irq_num % 32;
  io_write32((volatile void *)(GICD_ICENABLER(n)), 1U << bit);
}

void gic_set_irq_priority(uint32_t irq_num, uint8_t priority) {
  volatile void *reg_addr =
      (volatile void *)((uint8_t *)GICD_BASE + 0x400 + (irq_num * 4));
  io_write32(reg_addr, priority);
}

bool gic_is_irq_pending(uint32_t irq_num) {
  uint32_t n = irq_num / 32;
  uint32_t bit = irq_num % 32;
  uint32_t reg_val = io_read32(GICD_ISPENDR(n));
  return (reg_val & (1U << bit)) != 0;
}