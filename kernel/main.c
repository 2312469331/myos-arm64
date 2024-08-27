#include "exception.h"
#include "gic.h"
#include "irq.h"
#include "printk.h"
#include "timer.h"
#include "uart.h"
#include <stdint.h>

#define PAGE_SIZE 4096
#define PA_BASE 0x40000000
#define PA_SIZE 0x10000000 // 映射256MB物理内存
// 四级页表（全部 4KB 对齐）
__attribute__((aligned(PAGE_SIZE), section(".pagetable"))) uint64_t pgd[512] = {
    0};
__attribute__((aligned(PAGE_SIZE), section(".pagetable"))) uint64_t pud[512] = {
    0};
__attribute__((aligned(PAGE_SIZE), section(".pagetable"))) uint64_t pmd[512] = {
    0};
__attribute__((aligned(PAGE_SIZE), section(".pagetable"))) uint64_t pte[512] = {
    0};

// 内存属性（和 boot.S 里 MAIR_EL1 对应）
#define PROT_NORMAL (0 << 8) // Normal 可缓存内存
#define PROT_DEVICE (1 << 8) // Device 不可缓存内存（外设用）

// PTE 页表项标志（4KB 页）
// bit0: 1 = 有效
// bit1: 1 = 页映射（不是块）
// bit2: 1 = 可执行
// bit3: 1 = 可写
// bit4: 0 = 内核态访问
// bit10: 1 = 已访问
// bit11: 1 = 已脏
#define PTE_NORMAL                                                             \
  (1 | (1 << 1) | (1 << 2) | (1 << 3) | (0 << 4) | (1 << 10) | (1 << 11) |     \
   PROT_NORMAL)
#define PTE_DEVICE                                                             \
  (1 | (1 << 1) | (1 << 2) | (0 << 3) | (0 << 4) | (1 << 10) | (1 << 11) |     \
   PROT_DEVICE)
void init_boot_pgt(void) {
  uint64_t va = PA_BASE;
  uint64_t pa = PA_BASE;
  // 遍历所有需要映射的4KB物理页（1:1映射）
  for (; pa < PA_BASE + PA_SIZE; pa += PAGE_SIZE, va += PAGE_SIZE) {
    // 计算虚拟地址的各级索引
    uint64_t pgd_idx = (va >> 39) & 0x1FF; // bit47~39
    uint64_t pud_idx = (va >> 30) & 0x1FF; // bit38~30
    uint64_t pmd_idx = (va >> 21) & 0x1FF; // bit29~21
    uint64_t pte_idx = (va >> 12) & 0x1FF; // bit20~12
    // 1. 填写PGD项：指向PUD页表（页表项类型=3）
    pgd[pgd_idx] = (uint64_t)pud | 3;
    // 2. 填写PUD项：指向PMD页表（页表项类型=3）
    pud[pud_idx] = (uint64_t)pmd | 3;
    // 3. 填写PMD项：指向PTE页表（页表项类型=3）
    pmd[pmd_idx] = (uint64_t)pte | 3;
    // 4. 填写PTE项：指向物理页（Normal内存属性）
    pte[pte_idx] = (pa & ~0xFFF) | // 物理页基地址（4KB对齐）
                   (1 << 10) |     // AF=1（已访问）
                   (3 << 8) |      // SH=11（内外共享）
                   (1 << 6) |      // AP=01（EL1读写，EL0不可访问）
                   (1 << 2) |      // AttrIndx=1（对应MAIR的Normal内存）
                   1;              // Valid=1（有效页项）
  }
}
// 辅助函数：获取或创建 PUD 页表
uint64_t *get_pud(uint64_t virt) {
  uint64_t pgd_idx = (virt >> 39) & 0x1FF;
  if (!(pgd[pgd_idx] & 1)) {
    // PGD 项不存在，创建指向 PUD 的项
    pgd[pgd_idx] = ((uint64_t)pud & ~(PAGE_SIZE - 1)) | 1;
  }
  return pud;
}

// 辅助函数：获取或创建 PMD 页表
uint64_t *get_pmd(uint64_t virt) {
  uint64_t *pud = get_pud(virt);
  uint64_t pud_idx = (virt >> 30) & 0x1FF;
  if (!(pud[pud_idx] & 1)) {
    // PUD 项不存在，创建指向 PMD 的项
    pud[pud_idx] = ((uint64_t)pmd & ~(PAGE_SIZE - 1)) | 1;
  }
  return pmd;
}

// 辅助函数：获取或创建 PTE 页表
uint64_t *get_pte(uint64_t virt) {
  uint64_t *pmd = get_pmd(virt);
  uint64_t pmd_idx = (virt >> 21) & 0x1FF;
  if (!(pmd[pmd_idx] & 1)) {
    // PMD 项不存在，创建指向 PTE 的项
    pmd[pmd_idx] = ((uint64_t)pte & ~(PAGE_SIZE - 1)) | 1;
  }
  return pte;
}

// --------------------------
// ioremap：精确映射 4KB 物理页到虚拟地址（核心！）
// --------------------------
void ioremap(uint64_t virt, uint64_t phys, uint64_t size) {
  while (size >= PAGE_SIZE) {
    // 1. 获取 PTE 页表
    uint64_t *pte = get_pte(virt);
    // 2. 计算 PTE 索引
    uint64_t pte_idx = (virt >> 12) & 0x1FF;
    // 3. 填 PTE 项：物理页基地址 + Device 属性
    pte[pte_idx] = (phys & ~(PAGE_SIZE - 1)) | PTE_DEVICE;

    // 推进到下一页
    virt += PAGE_SIZE;
    phys += PAGE_SIZE;
    size -= PAGE_SIZE;
  }
}

// --------------------------
// 映射普通内存（1:1 映射，4KB 页）
// --------------------------
void map_memory(uint64_t virt, uint64_t phys, uint64_t size) {
  while (size >= PAGE_SIZE) {
    uint64_t *pte = get_pte(virt);
    uint64_t pte_idx = (virt >> 12) & 0x1FF;
    pte[pte_idx] = (phys & ~(PAGE_SIZE - 1)) | PTE_NORMAL;

    virt += PAGE_SIZE;
    phys += PAGE_SIZE;
    size -= PAGE_SIZE;
  }
}

// 刷新单个虚拟地址的 TLB（EL1 内核态）
static void tlb_flush(uint64_t virt) {
  __asm__ volatile("tlbi vaae1, %0\n" // 刷该虚拟地址的 TLB 表项(EL1)
                   "dsb sy\n"         // 同步内存
                   "isb\n"            // 同步指令流
                   :
                   : "r"(virt >> 12) // TLB 指令要填 虚拟地址>>12
                   : "memory");
}
// 取消 ioremap 的映射（4KB 粒度）
void iounmap(uint64_t virt, uint64_t size) {
  while (size >= PAGE_SIZE) {
    // 找到 PTE
    uint64_t *pte = get_pte(virt);
    uint64_t pte_idx = (virt >> 12) & 0x1FF;

    // 清空页表项 = 取消映射
    pte[pte_idx] = 0;

    // 必须刷 TLB！
    tlb_flush(virt);

    virt += PAGE_SIZE;
    size -= PAGE_SIZE;
  }
}
void mem_unmap(uint64_t virt, uint64_t size) {
  while (size >= PAGE_SIZE) {
    uint64_t *pte = get_pte(virt);
    uint64_t pte_idx = (virt >> 12) & 0x1FF;

    pte[pte_idx] = 0;
    tlb_flush(virt);

    virt += PAGE_SIZE;
    size -= PAGE_SIZE;
  }
}

// 👇 新增：函数声明（告诉编译器这些函数后面会定义）
void uart_test(void);
void gic_test(void);
// uart_irq_callback 已在handler.c文件实现，添加 extern 声明（根据实际参数修改）
extern void uart_irq_callback(uint32_t irq, exception_ctx_t *ctx);
extern void timer_irq_handler(uint32_t irq, exception_ctx_t *ctx);
void main(void) {
  uart_test();
  gic_test();
  irq_register(TIMER_IRQ_NUM, timer_irq_handler, "定时器");
  gic_enable_irq(TIMER_IRQ_NUM);
  while (1)
    ;
}
void gic_test(void) {

  uart_puts("OS Boot Success!\n");

  // 1. 设置异常向量表基址,boot.S已经加载完向量表，无需加载
  // write_sysreg(vbar_el1, (uint64_t)&vector_table_el1);
  uart_puts("[Init] Exception Vector Table Done\n");

  // 2. 初始化GIC中断控制器
  gic_init();
  uart_puts("[Init] GICv2 Done\n");

  // 3. 注册UART中断
  irq_register(IRQ_UART0, uart_irq_callback, "UART0");
  gic_enable_irq(IRQ_UART0);
  // 4. 启用全局中断
  enable_irq();
  uart_puts("[Init] Global IRQ Enabled\n");

  // 测试：故意触发对齐异常（验证异常处理）
  // volatile uint64_t *p = (uint64_t *)0x40000fac;
  // uint64_t val = *p;
}

// OS 主函数
void uart_test(void) {
  uart_error_t err;
  char c;

  // 1. 初始化UART
  uart_init();

  // 2. 发送测试字符串
  uart_puts("=== PL011 UART Driver Test (QEMU virt ARM) ===\n");
  uart_puts("Input a character (echo mode): ");

  // 3. 回显模式：接收一个字符并发送回去
  c = uart_getc(&err);
  if (err != UART_ERR_NONE) {
    uart_puts("\nReceive error: ");
    uart_putc('0' + err);
  } else {
    uart_puts("\nYou input: ");
    uart_putc(c);
  }

  uart_puts("\nDriver test done!\n");
  printk("=== MyOS ARM64 Boot Successful ===\n");
  printk("UART initialized\n");
  printk("Test: %s, %d, %x, %c\n", "Hello Kernel", 1234, 0x42, 'X');

  // 测试 panic（可选，注释掉先验证 printk）
  // panic("Test panic: %s", "Something went wrong!");
  // 死循环（OS 无退出）
}
// 强符号：覆盖 timer.c 里的弱符号
void timer_irq_handler(uint32_t irq, exception_ctx_t *ctx) {
  // 1. 标记参数（消除警告）
  (void)irq;
  (void)ctx;
  // 1. 重载定时器，保证持续 tick（必须写，否则中断只触发一次）
  cntp_set_tval(TIMER_LOAD_VAL);
  // 2. 系统时间++
  system_tick++;
}
void uart_irq_callback(uint32_t irq, exception_ctx_t *ctx) {
  printk("%s", "sbgxr????");
  // 1. 标记参数（消除警告）
  (void)irq;
  (void)ctx;
  // el1_irq_handler(exception_ctx_t *ctx)自动应答不需要多此一举
  // // 改造后（使用 GIC 上层 API，更规范）
  // uint32_t irq_num = gic_ack_irq(); // 应答中断
  // // ... 处理 UART 数据 ...
  // gic_eoi_irq(irq_num); // 结束中断

  // 3. 检查UART接收FIFO是否有数据（非阻塞）
  if (!uart_rx_ready()) {
    return; // 无数据直接返回，不浪费中断时间
  }
  // 4. 非阻塞读+错误处理（规范）
  uart_error_t err;
  char ch;
  uart_getc_nonblock(&ch, &err); // 现在 &ch 是 char*，和函数参数匹配
  if (err != UART_ERR_NONE) {
    // p
    uart_clear_error(); // 清除错误标志，避免卡死
    return;
  }
  // 5. 回显数据（仅做业务处理，快速退出）
  // 修正后（正确）
  uart_getc_nonblock(&ch, &err); // 现在 &ch 是 char*，和函数参数匹配
  if (err != UART_ERR_NONE) {
    // p
    uart_clear_error(); // 清除错误标志，避免卡死
    return;
  }
}
