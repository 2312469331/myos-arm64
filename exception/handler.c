#include <exception.h>
#include <gic.h>
#include <io.h>
#include <irq.h>
#include <printk.h>
#include <stdint.h>
#include <sync/spinlock.h>
#include <types.h>
#include <uart.h>
#include <page_fault.h>
// 1. 声明 Rust 侧接口
extern uint64_t rust_check_and_schedule(void);
extern uint64_t rust_handle_syscall(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern int rust_check_signals(void);
extern int rust_deliver_signal_to_user(struct pt_regs *regs);
extern void get_current_task_info(uint64_t *pid_out, uint64_t *ppid_out, uint64_t *pgd_out, const char **name_out);
static spinlock_t irq_table_lock = SPIN_LOCK_UNLOCKED;

// 无锁打印：异常处理中不能用 printk（会死锁）
static void safe_print_str(const char *s) {
    while (*s) uart_putc(*s++);
}
static void safe_print_hex(uint64_t val) {
    uart_putc('0'); uart_putc('x');
    for (int i = 60; i >= 0; i -= 4) {
        int digit = (val >> i) & 0xF;
        uart_putc(digit < 10 ? '0' + digit : 'a' + digit - 10);
    }
}

// ==============================
// AArch64 ESR_ELx 异常类型宏
// ==============================
#define ESR_ELx_EC_SHIFT 26
#define ESR_ELx_EC_MASK (0x3FUL << ESR_ELx_EC_SHIFT)

#define ESR_EC_UNKNOWN 0x00
#define ESR_EC_UNDEF_INSTR 0x01
#define ESR_EC_BRK 0x3C
#define ESR_EC_SVC_AARCH64 0x15
#define ESR_EC_IABORT 0x20
#define ESR_EC_IABORT_LOW 0x21
#define ESR_EC_IRQ 0x22
#define ESR_EC_FIQ 0x23
#define ESR_EC_DABORT 0x24
#define ESR_EC_DABORT_LOW 0x25
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

// ==============================
// EL1 异常处理入口
// x0: 异常类型标识
//     0 = 同步异常
//     1 = IRQ 中断
//     2 = FIQ 中断
//     3 = SError 系统错误
// ==============================

void c_exception_handler_el1(uint64_t ex_type, struct pt_regs *regs){
  uint64_t esr = read_esr_el1();
  uint64_t elr = read_elr_el1();
  uint64_t far = read_far_el1();
  uint64_t spsr = read_spsr_el1();
  uint64_t ec = (esr & ESR_ELx_EC_MASK) >> ESR_ELx_EC_SHIFT;

  switch (ex_type) {
  case 0:
    // SVC 系统调用是正常路径，不打印 debug 信息
    if (ec != ESR_EC_SVC_AARCH64) {
      safe_print_str("[Exception] Synchronous exception\n");
      safe_print_str("[Exception] ESR=");
      safe_print_hex(esr);
      safe_print_str(" EC=");
      safe_print_hex(ec);
      safe_print_str(" FAR=");
      safe_print_hex(far);
      safe_print_str(" ELR=");
      safe_print_hex(elr);
      safe_print_str("\n");

      {
          uint64_t pid, ppid, pgd;
          const char *name;
          get_current_task_info(&pid, &ppid, &pgd, &name);
          safe_print_str("[Exception] Current task: pid=");
          safe_print_hex(pid);
          safe_print_str(" ppid=");
          safe_print_hex(ppid);
          safe_print_str(" pgd=");
          safe_print_hex(pgd);
          safe_print_str(" name=");
          if (name) safe_print_str(name);
          else safe_print_str("(null)");
          safe_print_str("\n");
      }
    }

    switch (ec) {
    case ESR_EC_BRK:
      safe_print_str("[Exception] Breakpoint instruction\n");
      // {
      //   uint64_t *stack;
      //   asm volatile("mov %0, sp" : "=r"(stack));
      //   stack[1] += 4;
      // }
      return;
      break;

    case ESR_EC_UNDEF_INSTR:
      safe_print_str("[Exception] Undefined instruction\n");
      panic("Undefined instruction");
      break;

    case ESR_EC_SVC_AARCH64: {
      uint64_t syscall_num = regs->x8;
      uint64_t arg0 = regs->x0;
      uint64_t arg1 = regs->x1;
      uint64_t arg2 = regs->x2;
      uint64_t arg3 = regs->x3;

      uint64_t ret;
      if (syscall_num == 220) {
        // SYS_FORK: 需要传递 pt_regs 指针
        ret = rust_handle_syscall(syscall_num, (uint64_t)regs, 0, 0, 0);
      } else if (syscall_num == 221) {
        // SYS_EXECVE: 需要传递 pt_regs 指针以修改 ELR/寄存器
        ret = rust_handle_syscall(syscall_num, arg0, (uint64_t)regs, arg2, arg3);
      } else {
        ret = rust_handle_syscall(syscall_num, arg0, arg1, arg2, arg3);
      }

      // 系统调用返回值写入 x0
      regs->x0 = ret;

      // 信号检查：如果进程被终止，不再继续执行用户态代码
      if (rust_check_signals()) {
          // 进程被信号终止，执行 do_exit() 清理资源并调度到下一个进程
          // do_exit() 内部调用 schedule()，context_switch 会切换到新任务的栈
          // 之后 C 代码在新任务栈上继续执行，最终通过 el1_exception_exit 恢复新任务现场
          extern void rust_do_exit(void);
          rust_do_exit();
      }
    } break;

    case ESR_EC_IABORT:
    case ESR_EC_IABORT_LOW: {
      uint64_t iss = esr & 0x1FFFFFF;
      uint8_t dfsc = (iss >> 0) & 0x3F;
      if (dfsc >= 0x04 && dfsc <= 0x0F) {
        page_fault_handler(far, esr, far);
        /* 直接检查任务是否已被标记 DEAD（SIGKILL 已设 TASK_DEAD 但 pending 被清） */
        extern void rust_do_exit_if_dead(void);
        rust_do_exit_if_dead();
        /* 另外检查信号 pending（SIGSEGV 等走信号路径） */
        if (rust_check_signals()) {
            extern void rust_do_exit(void);
            rust_do_exit();
        }
      } else {
        safe_print_str("[Exception] Instruction abort\n");
        safe_print_str("[Exception] DFSC=");
        safe_print_hex(dfsc);
        safe_print_str("\n");
        panic("Instruction abort");
      }
    } break;

    case ESR_EC_DABORT:
    case ESR_EC_DABORT_LOW: {
      uint64_t iss = esr & 0x1FFFFFF;
      uint8_t dfsc = (iss >> 0) & 0x3F;

      if (dfsc >= 0x04 && dfsc <= 0x0F) {
        page_fault_handler(far, esr, far);
        /* 直接检查任务是否已被标记 DEAD */
        extern void rust_do_exit_if_dead(void);
        rust_do_exit_if_dead();
        /* 另外检查信号 pending */
        if (rust_check_signals()) {
            extern void rust_do_exit(void);
            rust_do_exit();
        }
      } else {
        safe_print_str("[Exception] Data abort\n");
        panic("Data abort");
      }
    } break;

    case ESR_EC_SERROR:
      safe_print_str("[Exception] SError\n");
      panic("SError");
      break;

    default:
      safe_print_str("[Exception] Unknown exception\n");
      panic("Unknown exception");
      break;
    }
    break;

  case 1:
    // IRQ 中断处理
    el1_irq_handler();

    // 信号检查（确保纯用户态循环也能处理 Ctrl+C 等信号）
    if (rust_check_signals()) {
        extern void rust_do_exit(void);
        rust_do_exit();
    }

    // 调度检查
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
    printk("[Exception] Unknown exception type: %lu\n", ex_type);
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
