#include "exception.h"
#include "gic.h"
#include "irq.h"
#include "pmm.h"
#include "printk.h"
#include "slab.h"
#include "timer.h"
#include "uart.h"
#include <bootc.h>
#include <libc.h>
#include <stddef.h>
#include <stdint.h>
// 计算UART虚拟地址：在最后一个L3表的最后一项
// L3表数量 = (内核大小 + 2MB - 1) / 2MB
#define PHYS_BASE 0x40000000UL
#define VIRT_BASE 0xffff800000000000UL
#define LINEAR_MAP_BASE 0xFFFF800000000000UL // 线性分配区地址基地址
// 从链接脚本导入的符号
// 在 bootc.c 中定义函数指针类型和获取函数
typedef uint64_t (*get_ttbr1_fn_t)(void);
// 在 bootc.c 中定义函数指针类型和获取函数
typedef uint64_t (*get_ttbr1_fn_t)(void);
// 在 main.c 中使用
extern uint64_t get_ttbr1_el1(void);
extern uintptr_t __boot_phys_base; // 从链接脚本获取 boot 段物理基址

// 计算函数物理地址并调用
uintptr_t func_pa = (uintptr_t)get_ttbr1_el1 + VIRT_BASE;

// 👇 新增：函数声明（告诉编译器这些函数后面会定义）
void uart_test(void);
void gic_test(void);
void test_buddy_system(void);
void test_kmalloc(void);
// void gic_test(void);
// uart_irq_callback 已在handler.c文件实现，添加 extern 声明（根据实际参数修改）
extern void uart_irq_callback(uint32_t irq);
extern void timer_irq_handler(uint32_t irq);
void main(void) {

  if (L3_TABLES_NEEDED > 0) {
    uint64_t last_table_idx = L3_TABLES_NEEDED - 1;
    uint64_t uart_va = VIRT_BASE + PHYS_BASE +
                       (last_table_idx * L3_TABLE_MAP_SIZE) + (511 * 4096);
    uart_base = (volatile void *)uart_va;
  }

  printk("\n");
  printk("===============================================\n");
  printk("           MyOS ARM64 Kernel Boot              \n");
  printk("===============================================\n");
  printk("Version: 1.0.0\n");
  printk("Architecture: ARM64\n");
  printk("Memory: 256MB (0x40000000-0x4fffffff)\n");
  printk("===============================================\n");
  printk("\n");
  buddy_init(); // 测试伙伴系统
  // 全面测试伙伴系统性能和完整性
  test_buddy_system();

  printk("[PMM] Pages freed\n");

  // 设置slab分配器所需的全局变量
  extern uintptr_t slab_linear_map_base;
  extern phys_addr_t slab_l0_table_pa;
  get_ttbr1_fn_t get_ttbr1_pa = (get_ttbr1_fn_t)func_pa;
  slab_l0_table_pa = get_ttbr1_pa();
  // 线性映射基址：VA = PA + slab_linear_map_base
  slab_linear_map_base = LINEAR_MAP_BASE;

  printk("[SLAB] Linear map base: %lx\n", slab_linear_map_base);
  printk("[SLAB] L0 table PA: %lx\n", slab_l0_table_pa);

  // 初始化slab分配器
  slab_init();

  // 测试kmalloc功能
  test_kmalloc();

  uart_test();
  gic_test();
  irq_register(TIMER_IRQ_NUM, timer_irq_handler, "定时器");
  gic_enable_irq(TIMER_IRQ_NUM);
}

// 全面测试伙伴系统性能和完整性
void test_buddy_system(void) {
  printk("\n[BUDDY TEST] Starting comprehensive buddy system test...\n");
  // 测试1: 不同order的分配
  printk("[BUDDY TEST] Test 1: Allocate different orders\n");
  phys_addr_t orders[11];
  for (int i = 0; i <= 10; i++) {
    orders[i] = alloc_phys_pages(i);
    if (orders[i]) {
      printk("[BUDDY TEST]  Order %d: allocated at %p\n", i, orders[i]);
    } else {
      printk("[BUDDY TEST]  Order %d: allocation failed\n", i);
    }
  }

  // 释放这些页面
  for (int i = 0; i <= 10; i++) {
    if (orders[i]) {
      free_phys_pages(orders[i], i);
    }
  }

  // 测试2: 大量页面连续分配
  printk("\n[BUDDY TEST] Test 2: Allocate 1000 pages (order 0)\n");
  phys_addr_t pages[1000];
  int allocated = 0;

  for (int i = 0; i < 1000; i++) {
    pages[i] = alloc_phys_pages(0);
    if (pages[i]) {
      allocated++;
      // 每100个页面打印一次
      if ((i + 1) % 100 == 0) {
        printk("[BUDDY TEST]  Allocated %d/1000 pages...\n", i + 1);
      }
    }
  }

  printk("[BUDDY TEST]  Successfully allocated %d pages\n", allocated);

  // 测试3: 检查地址范围
  printk(
      "\n[BUDDY TEST] Test 3: Verify address range (0x40200000-0x4fffffff)\n");
  for (int i = 0; i < allocated; i++) {
    if (pages[i] < 0x40200000 || pages[i] > 0x4fffffff) {
      printk("[BUDDY TEST]  ERROR: Page at %p is out of range!\n", pages[i]);
    }
  }
  printk("[BUDDY TEST]  All pages are within valid range\n");

  // 释放页面
  for (int i = 0; i < allocated; i++) {
    if (pages[i]) {
      free_phys_pages(pages[i], 0);
    }
  }

  // 测试4: 分配和释放循环测试
  printk("\n[BUDDY TEST] Test 4: Allocate/free cycle test\n");
  const int CYCLES = 100;

  for (int cycle = 0; cycle < CYCLES; cycle++) {
    if ((cycle + 1) % 20 == 0) {
      printk("[BUDDY TEST]  Cycle %d/%d...\n", cycle + 1, CYCLES);
    }

    // 分配随机order的页面
    int order = cycle % 5; // 0-4
    phys_addr_t page = alloc_phys_pages(order);
    if (page) {
      free_phys_pages(page, order);
    }
  }

  // 测试5: 测试分配30个order10
  printk("\n[BUDDY TEST] Test 5: Allocate 30 order10 blocks (4MB each)\n");
  phys_addr_t order10_pages[30];
  int order10_allocated = 0;

  for (int i = 0; i < 30; i++) {
    order10_pages[i] = alloc_phys_pages(10);
    if (order10_pages[i]) {
      order10_allocated++;
      printk("[BUDDY TEST]  Order10 %d allocated at %p\n", i + 1,
             order10_pages[i]);
    } else {
      printk("[BUDDY TEST]  Order10 %d allocation failed\n", i + 1);
      break;
    }
  }

  printk("[BUDDY TEST]  Successfully allocated %d/30 order10 blocks\n",
         order10_allocated);

  // 释放这些页面
  for (int i = 0; i < order10_allocated; i++) {
    free_phys_pages(order10_pages[i], 10);
  }
  printk("[BUDDY TEST]  All order10 blocks freed\n");

  // 测试6: 测试边界情况
  printk("\n[BUDDY TEST] Test 6: Boundary cases\n");

  // 尝试分配超过可用内存的页面
  phys_addr_t big_page = alloc_phys_pages(20); // 这应该会失败
  if (!big_page) {
    printk("[BUDDY TEST]  Expected failure: order 20 allocation failed\n");
  }

  // 测试空指针释放
  free_phys_pages(0, 0);
  printk("[BUDDY TEST]  NULL pointer free handled correctly\n");

  printk("\n[BUDDY TEST] All tests completed!\n");
}

// 测试kmalloc功能
void test_kmalloc(void) {
  printk("\n[KMALLOC TEST] Starting kmalloc test...\n");

  // 测试1: 分配各种大小的内存
  printk("[KMALLOC TEST] Test 1: Allocate various sizes\n");
  void *ptr1 = kmalloc(8);
  void *ptr2 = kmalloc(64);
  void *ptr3 = kmalloc(512);
  void *ptr4 = kmalloc(1024);
  void *ptr5 = kmalloc(4096);
  void *ptr6 = kmalloc(8192);
  void *ptr7 = kmalloc(65536);
  printk("[KMALLOC TEST]  8 bytes: %p\n", ptr1);
  printk("[KMALLOC TEST]  64 bytes: %p\n", ptr2);
  printk("[KMALLOC TEST]  512 bytes: %p\n", ptr3);
  printk("[KMALLOC TEST]  1024 bytes: %p\n", ptr4);
  printk("[KMALLOC TEST]  4096 bytes: %p\n", ptr5);
  printk("[KMALLOC TEST]  8192 bytes: %p\n", ptr6);
  printk("[KMALLOC TEST]  65536 bytes: %p\n", ptr7);

  // 测试2: 写入数据并验证
  printk("\n[KMALLOC TEST] Test 2: Write and verify data\n");
  if (ptr1) {
    *(uint64_t *)ptr1 = 0x123456789ABCDEF0;
    printk("[KMALLOC TEST]  8 bytes: written 0x123456789ABCDEF0, read %lx\n",
           *(uint64_t *)ptr1);
  }

  if (ptr2) {
    memset(ptr2, 0xAA, 64);
    printk("[KMALLOC TEST]  64 bytes: written 0xAA pattern\n");
  }

  if (ptr3) {
    for (int i = 0; i < 512 / 4; i++) {
      ((uint32_t *)ptr3)[i] = i;
    }
    printk("[KMALLOC TEST]  512 bytes: written sequential data\n");
  }

  // 测试3: 释放内存
  printk("\n[KMALLOC TEST] Test 3: Free allocated memory\n");
  if (ptr1)
    kfree(ptr1);
  if (ptr2)
    kfree(ptr2);
  if (ptr3)
    kfree(ptr3);
  if (ptr4)
    kfree(ptr4);
  if (ptr5)
    kfree(ptr5);
  if (ptr6)
    kfree(ptr6);
  if (ptr7)
    kfree(ptr7);
  printk("[KMALLOC TEST]  All allocations freed\n");

  // 测试4: 大量小内存分配
  printk(
      "\n[KMALLOC TEST] Test 4: Allocate 1000 small blocks (64 bytes each)\n");
  void *small_ptrs[1000];
  int allocated = 0;

  for (int i = 0; i < 1000; i++) {
    small_ptrs[i] = kmalloc(64);
    if (small_ptrs[i]) {
      allocated++;
      if ((i + 1) % 200 == 0) {
        printk("[KMALLOC TEST]  Allocated %d/1000 blocks...\n", i + 1);
      }
    }
  }

  printk("[KMALLOC TEST]  Successfully allocated %d/1000 blocks\n", allocated);

  // 释放小内存块
  for (int i = 0; i < allocated; i++) {
    if (small_ptrs[i]) {
      kfree(small_ptrs[i]);
    }
  }
  printk("[KMALLOC TEST]  All small blocks freed\n");

  // 测试5: 测试边界情况
  printk("\n[KMALLOC TEST] Test 5: Boundary cases\n");

  // 分配0字节
  void *zero_ptr = kmalloc(0);
  if (!zero_ptr) {
    printk("[KMALLOC TEST]  Expected failure: kmalloc(0) returned NULL\n");
  }

  // 释放NULL指针
  kfree(NULL);
  printk("[KMALLOC TEST]  NULL pointer free handled correctly\n");

  printk("\n[KMALLOC TEST] All tests completed!\n");
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
void timer_irq_handler(uint32_t irq) {
  // 1. 标记参数（消除警告）
  (void)irq;
  // 1. 重载定时器，保证持续 tick（必须写，否则中断只触发一次）
  cntp_set_tval(TIMER_LOAD_VAL);
  // 2. 系统时间++
  system_tick++;
}
void uart_irq_callback(uint32_t irq) {
  printk("%s", "sbgxr????");
  // 1. 标记参数（消除警告）
  (void)irq;

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
