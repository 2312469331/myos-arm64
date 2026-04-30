#include <exception.h>
#include <gic.h>
#include <io.h>
#include <irq.h>
#include <printk.h>
#include <stdint.h>
#include <sync/spinlock.h>
#include <types.h>
#include <uart.h>
// 1. 声明 Rust 侧接口
extern uint64_t rust_check_and_schedule(void);
static spinlock_t irq_table_lock = SPIN_LOCK_UNLOCKED;

// ==============================
// AArch64 ESR_ELx 异常类型宏
// ==============================
#define ESR_ELx_EC_SHIFT 26
#define ESR_ELx_EC_MASK (0x3FUL << ESR_ELx_EC_SHIFT)

#define ESR_EC_UNKNOWN 0x00
#define ESR_EC_UNDEF_INSTR 0x01
#define ESR_EC_SVC_AARCH64 0x15
#define ESR_EC_IABORT 0x20
#define ESR_EC_IRQ 0x22
#define ESR_EC_FIQ 0x23
#define ESR_EC_DABORT 0x24
#define ESR_EC_SERROR 0x2F

// ==============================
// 读 AArch64 系统寄存器
// ==============================
static inline uint64_t read_esr_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, esr_el1" : "=r"(v));
  return v;
}
static inline uint64_t read_elr_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, elr_el1" : "=r"(v));
  return v;
}
static inline uint64_t read_far_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, far_el1" : "=r"(v));
  return v;
}
static inline uint64_t read_spsr_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, spsr_el1" : "=r"(v));
  return v;
}

// ==============================
// EL3 异常处理入口
// ==============================
void c_exception_handler_el3(void) {
  uint64_t esr, elr, spsr, x0, x1, x2, x3;

  asm volatile("mrs %0, esr_el3" : "=r"(esr));
  asm volatile("mrs %0, elr_el3" : "=r"(elr));
  asm volatile("mrs %0, spsr_el3" : "=r"(spsr));

  asm volatile("mov %0, x0" : "=r"(x0));
  asm volatile("mov %0, x1" : "=r"(x1));
  asm volatile("mov %0, x2" : "=r"(x2));
  asm volatile("mov %0, x3" : "=r"(x3));

  uint32_t smc_id = x0 & 0xFFFF;

  printk("[EL3] SMC Call, ID=0x%x, x1=0x%lx\n", smc_id, x1);

  switch (smc_id) {
  case 0x1000:
    printk("[EL3] SMC_GIC_ENABLE_IRQ called, irq=%lu\n", x1);
    if (x1 < 1024) {
      uint32_t idx = x1 / 32;
      uint32_t bit = x1 % 32;

      if (x1 < 32) {
        uint32_t nsacr = io_read32(GICD_BASE + 0xE0);
        nsacr |= (1 << (x1 * 2));
        io_write32(GICD_BASE + 0xE0, nsacr);
        printk("[EL3] NSACR=0x%x\n", nsacr);
      }

      uint32_t grp = io_read32(GICD_BASE + 0x80 + idx * 4);
      grp |= (1 << bit);
      io_write32(GICD_BASE + 0x80 + idx * 4, grp);
      printk("[EL3] IGROUPR[%d]=0x%x\n", idx, grp);

      io_write32(GICD_BASE + 0x100 + idx * 4, 1 << bit);
      printk("[EL3] ISENABLER[%d] bit%d set\n", idx, bit);
    }
    break;

  default:
    printk("[EL3] Unknown SMC ID: 0x%x\n", smc_id);
    break;
  }

  asm volatile("msr elr_el3, %0" : : "r"(elr));
  asm volatile("msr spsr_el3, %0" : : "r"(spsr));
  asm volatile("eret");
}

// ==============================
// EL2 异常处理入口
// ==============================
void c_exception_handler_el2(void) {
  uint64_t esr, elr, spsr;

  asm volatile("mrs %0, esr_el2" : "=r"(esr));
  asm volatile("mrs %0, elr_el2" : "=r"(elr));
  asm volatile("mrs %0, spsr_el2" : "=r"(spsr));

  printk("[EL2 Exception] ESR=0x%lx, ELR=0x%lx, SPSR=0x%lx\n", esr, elr, spsr);

  while (1)
    ;
}
// 2. pt_regs 结构体（顺序必须和你汇编 stp 的顺序严格对应！
// 第一个必须是 elr 和 spsr，因为你是在存完它们之后才 bl 到 C 的
struct pt_regs {
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t x30;
    uint64_t x29;
    uint64_t x28;
    uint64_t x27;
    uint64_t x26;
    uint64_t x25;
    uint64_t x24;
    uint64_t x23;
    uint64_t x22;
    uint64_t x21;
    uint64_t x20;
    uint64_t x19;
    uint64_t x18;
    uint64_t x17;
    uint64_t x16;
    uint64_t x15;
    uint64_t x14;
    uint64_t x13;
    uint64_t x12;
    uint64_t x11;
    uint64_t x10;
    uint64_t x9;
    uint64_t x8;
    uint64_t x7;
    uint64_t x6;
    uint64_t x5;
    uint64_t x4;
    uint64_t x3;
    uint64_t x2;
    uint64_t x1;
    uint64_t x0;  // 原始的 x0 保存在栈的最底下
};
// ==============================
// EL1 异常处理入口
// x0: 异常类型标识
//     0 = 同步异常
//     1 = IRQ 中断
//     2 = FIQ 中断
//     3 = SError 系统错误
// ==============================

void c_exception_handler_el1(uint64_t x0) {
  uint64_t esr = read_esr_el1();
  uint64_t elr = read_elr_el1();
  uint64_t far = read_far_el1();
  uint64_t spsr = read_spsr_el1();
  uint64_t ec = (esr & ESR_ELx_EC_MASK) >> ESR_ELx_EC_SHIFT;

  switch (x0) {
  case 0:
    printk("[Exception] Synchronous exception\n");
    printk("[Exception] ELR (Fault PC): 0x%lx\n", elr);
    printk("[Exception] FAR (Fault Address): 0x%lx\n", far);
    printk("[Exception] ESR (Exception Syndrome): 0x%lx\n", esr);
    printk("[Exception] ESR EC (Exception Class): 0x%lx\n", ec);
    printk("[Exception] SPSR (PSTATE): 0x%lx\n", spsr);

    switch (ec) {
    case ESR_EC_UNDEF_INSTR:
      printk("[Exception] Undefined instruction at PC: 0x%lx\n", elr);
      panic("Undefined instruction");
      break;

    case ESR_EC_SVC_AARCH64: {
      uint32_t svc_number = esr & 0xFFFF;
      printk("[Exception] SVC system call #%u at PC: 0x%lx\n", svc_number, elr);
      panic("System call not implemented");
    } break;

    case ESR_EC_IABORT: {
      printk("[Exception] Instruction abort at PC: 0x%lx\n", elr);
      printk("[Exception] Fault address: 0x%lx\n", far);
      uint64_t iss = esr & 0x1FFFFFF;
      printk("[Exception] ISS: 0x%lx\n", iss);
      panic("Instruction abort");
    } break;

    case ESR_EC_DABORT: {
      printk("[Exception] Data abort at PC: 0x%lx\n", elr);
      printk("[Exception] Fault address: 0x%lx\n", far);
      uint64_t iss = esr & 0x1FFFFFF;
      printk("[Exception] ISS: 0x%lx\n", iss);

      uint8_t dfsc = (iss >> 0) & 0x3F;
      printk("[Exception] DFSC: 0x%x\n", dfsc);

      switch (dfsc) {
      case 0x04:
        printk("[Exception] Translation fault - Level 3\n");
        break;
      default:
        printk("[Exception] Unknown data fault\n");
        break;
      }

      panic("Data abort");
    } break;

    case ESR_EC_SERROR:
      printk("[Exception] SError hardware/bus error\n");
      panic("SError");
      break;

    default:
      printk("[Exception] Unknown synchronous exception (EC: 0x%lx)\n", ec);
      panic("Unknown exception");
      break;
    }
    break;

  case 1:
    // printk("[Exception] IRQ interrupt\n");
    // IRQ 中断处理
    el1_irq_handler(); // 你的具体中断处理逻辑
    
    // 【唯一的调度检查点】
    // 如果这里返回 1，说明 Rust 已经偷换了 SP 并跳到汇编恢复新现场了，不会 return。
    if (rust_check_and_schedule()) {
        __builtin_unreachable(); 
    }
    break;

  case 2:
    printk("[Exception] FIQ interrupt\n");
    panic("FIQ interrupt not handled");
    break;

  case 3:
    printk("[Exception] SError system error\n");
    printk("[Exception] ESR: 0x%lx\n", esr);
    printk("[Exception] FAR: 0x%lx\n", far);
    panic("SError");
    break;

  default:
    printk("[Exception] Unknown exception type: %lu\n", x0);
    panic("Unknown exception type");
    break;
  }
}

static irq_handler_t irq_table[1024] = {NULL};

void irq_register(uint32_t irq, irq_handler_t handler, const char *name) {
  unsigned long flags;
  spin_lock_irqsave(&irq_table_lock, flags);

  if (irq < 1024 && handler) {
    irq_table[irq] = handler;
    gic_enable_irq(irq);
    uart_puts("[IRQ] Register: ");
    uart_puts(name);
    uart_puts("\n");
  }

  spin_unlock_irqrestore(&irq_table_lock, flags);
}

void el1_sync_handler() {
  uint64_t esr = read_sysreg(esr_el1);
  uint64_t far = read_sysreg(far_el1);
  uint32_t ec = (esr >> 26) & 0x3F;

  printk("\n[EL1 Sync Exception]\n");
  printk("ESR: %x\n", esr);
  printk("FAR: %x\n", far);

  if (ec == 0x00) {
    uart_puts("[Error] Undefined Instruction!\n");
    while (1)
      ;
  }

  while (1)
    ;
}

void el1_irq_handler() {
  uint32_t iar = io_read32((volatile void *)GICC_IAR);
  uint32_t irq_num = iar & 0x3FF;

  if (irq_num < 1024) {
    unsigned long flags;
    spin_lock_irqsave(&irq_table_lock, flags);
    irq_handler_t handler = irq_table[irq_num];
    spin_unlock_irqrestore(&irq_table_lock, flags);

    if (handler) {
      handler(irq_num);
    }
  }

  io_write32((volatile void *)GICC_EOIR, iar);
}

__attribute__((weak)) void uart_irq_callback(uint32_t irq) {
  (void)irq;

  if (!uart_rx_ready()) {
    return;
  }

  uart_error_t err;
  char ch;
  if (uart_getc_nonblock(&ch, &err) && err == UART_ERR_NONE) {
    uart_putc(ch);
  } else if (err != UART_ERR_NONE) {
    uart_clear_error();
  }
}

__attribute__((weak)) void timer_irq_handler(uint32_t irq) { (void)irq; }
