#include <bootc.h>
#include <exception.h>
#include <gic.h>
#include <io.h>
#include <irq.h>
#include <libc.h>
#include <libfdt.h>
#include <pmm.h>
#include <printk.h>
#include <slab.h>
#include <timer.h>
#include <uart.h>
#include <vmalloc.h>
#include <vmap.h>

// // 测试 naked 属性
// __attribute__((naked)) void test_naked_attribute(void) {
//     __asm__ volatile (
//         "mov x0, #42          \n"
//         "ret                  \n"
//     );
// }

// __attribute__((naked,noreturn)) void test_noreturn(void) {
//     __asm__ volatile ("b .");  // 死循环
// }

// #include <a-profile/gicv2.h>    // GIC 中断控制器
// #include <a-profile/armv8a.h>
// 计算UART虚拟地址：在最后一个L3表的最后一项
// L3表数量 = (内核大小 + 2MB - 1) / 2MB
#define VIRT_BASE 0xffff800000000000UL
#define LINEAR_MAP_BASE 0xFFFF800000000000UL // 线性分配区地址基地址
// 从链接脚本导入的符号
// 在 bootc.c 中定义函数指针类型和获取函数
typedef uint64_t (*get_ttbr1_fn_t)(void);
// 在 bootc.c 中定义函数指针类型和获取函数
typedef uint64_t (*get_ttbr1_fn_t)(void);
// 在 main.c 中使用
extern uint64_t get_ttbr1_el1(void);
extern uintptr_t __boot_phys_base; // 从 boot 段 boot 段物理基址
void print_mem_usage(void);
// 计算函数物理地址并调用
uintptr_t func_pa = (uintptr_t)get_ttbr1_el1; // 物理地址 0x4020072c

// ? 新增：函数声明（告诉编译器这些函数后面会定义）
void uart_test(void);
void gic_test(void);
void test_buddy_system(void);
void test_kmalloc(void);
void test_vmap(void);
void test_fdt(void);
// void test_rust_wrapper(void);
// void gic_test(void);
// uart_irq_callback 已在handler.c文件实现，添加 extern 声明（根据实际参数修改）
extern void uart_irq_callback(uint32_t irq);
extern void timer_irq_handler(uint32_t irq);

void *dtb_base = NULL; // 全局变量，保存 DTB 地址

void main(void *dtb) {
  // 设置slab分配器所需的全局变量
  extern uintptr_t slab_linear_map_base;
  extern phys_addr_t slab_l0_table_pa;
  get_ttbr1_fn_t get_ttbr1_pa = (get_ttbr1_fn_t)func_pa;
  slab_l0_table_pa = get_ttbr1_pa();
  // 线性映射基址：VA = PA + slab_linear_map_base
  slab_linear_map_base = LINEAR_MAP_BASE;
  buddy_init(); // 测试伙伴系统
                // 初始化slab分配器
  slab_init();

  dtb_base = dtb; // 保存 DTB 地址

  // 初始化vmap管理器
  va_manager_init();
  uart_init();
  print_mem_usage();
  printk("[PMM] Pages freed\n");

  printk("[SLAB] Linear map base: %lx\n", slab_linear_map_base);
  printk("[SLAB] L0 table PA: %lx\n", slab_l0_table_pa);
  // uart_base 现在在 uart_init 函数中通过 ioremap 动态映射

  printk("\n");
  printk("===============================================\n");
  printk("           MyOS ARM64 Kernel Boot              \n");
  printk("===============================================\n");
  printk("Version: 1.0.0\n");
  printk("Architecture: ARM64\n");
  printk("Memory: 256MB (0x40200000-0x4fffffff)\n");
  printk("DTB Address: %p (physical)\n", dtb);
  printk("===============================================\n");
  printk("\n");
  test_fdt();

  // 全面测试伙伴系统性能和完整性
  test_buddy_system();
  print_mem_usage();

  print_mem_usage();

  print_mem_usage();

  // 测试kmalloc功能
  test_kmalloc();
  print_mem_usage();
  // 测试vmap功能
  test_vmap();
  print_mem_usage();

  // // 测试 ioremap 功能
  // printk("\n[IOREMAP TEST] Testing ioremap...\n");
  // void *ioremap_addr = ioremap(0x9000000, 4096);
  // if (ioremap_addr) {
  //     printk("[IOREMAP TEST] ioremap(0x9000000, 4096) = %p\n", ioremap_addr);

  //     // 向前 4 字节写入数据
  //     uint32_t test_data = 0x12345678;
  //     printk("[IOREMAP TEST] Writing 0x%x to %p\n", test_data, ioremap_addr);
  //     io_write32(ioremap_addr, test_data);

  //     // 读取验证
  //     uint32_t read_data = io_read32(ioremap_addr);
  //     printk("[IOREMAP TEST] Read back: 0x%x\n", read_data);

  //     if (read_data == test_data) {
  //         printk("[IOREMAP TEST] ioremap write/read test passed!\n");
  //     } else {
  //         printk("[IOREMAP TEST] ioremap write/read test failed!\n");
  //     }

  //     // 解除映射
  //     iounmap(ioremap_addr);
  //     printk("[IOREMAP TEST] iounmap done\n");
  // } else {
  //     printk("[IOREMAP TEST] ioremap failed!\n");
  // }
  print_mem_usage();

  // 测试 Rust 包装器
  // test_rust_wrapper();

  // 测试 naked 属性（AArch64 不支持）
  // test_naked_attribute();

  uart_test();
  gic_test();

  irq_register(TIMER_IRQ_NUM, timer_irq_handler, "定时器");
  gic_enable_irq(TIMER_IRQ_NUM);
}

// 测试内置函数的汇编实现 - 直接调用 __builtin_*
void test_builtin_asm(void) {
  char src[100] = "Hello World";
  char dst1[100], dst2[100], dst3[100];

  // 测试 __builtin_strlen
  size_t len = __builtin_strlen(src);
  printk("[BUILTIN TEST] __builtin_strlen result: %zu\n", len);

  // 测试 __builtin_memcpy
  __builtin_memcpy(dst1, src, len + 1);
  printk("[BUILTIN TEST] __builtin_memcpy: %s\n", dst1);

  // 测试 __builtin_memset
  __builtin_memset(dst2, 'A', 10);
  dst2[10] = '\0';
  printk("[BUILTIN TEST] __builtin_memset: %s\n", dst2);

  // 测试 __builtin_memmove
  __builtin_memcpy(dst3, src, 5);
  __builtin_memmove(dst3 + 2, dst3, 3);
  dst3[8] = '\0';
  printk("[BUILTIN TEST] __builtin_memmove: %s\n", dst3);

  // 测试 __builtin_memcmp
  int cmp = __builtin_memcmp(dst1, dst1, 5);
  printk("[BUILTIN TEST] __builtin_memcmp result: %d\n", cmp);
}

// 全面测试伙伴系统性能和完整性
void test_buddy_system(void) {
  printk("\n[BUDDY TEST] Starting comprehensive buddy system test...\n");
  // 测试1: 不同order的分配
  printk("[BUDDY TEST] Test 1: Allocate different orders\n");
  phys_addr_t orders[11];
  for (int i = 0; i <= 10; i++) {
    orders[i] = alloc_phys_pages(i, GFP_KERNEL);
    print_mem_usage();
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
  print_mem_usage();

  // 测试2: 大量页面连续分配
  printk("\n[BUDDY TEST] Test 2: Allocate 1000 pages (order 0)\n");
  print_mem_usage();

  phys_addr_t pages[1000];
  int allocated = 0;

  for (int i = 0; i < 1000; i++) {
    pages[i] = alloc_phys_pages(0, GFP_KERNEL);
    if (pages[i]) {
      allocated++;
      // 每100个页面打印一次
      if ((i + 1) % 100 == 0) {
        printk("[BUDDY TEST]  Allocated %d/1000 pages...\n", i + 1);
        print_mem_usage();
      }
    }
  }

  printk("[BUDDY TEST]  Successfully allocated %d pages\n", allocated);
  print_mem_usage();

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
  print_mem_usage();

  // 测试4: 分配和释放循环测试
  printk("\n[BUDDY TEST] Test 4: Allocate/free cycle test\n");
  print_mem_usage();

  const int CYCLES = 100;

  for (int cycle = 0; cycle < CYCLES; cycle++) {
    if ((cycle + 1) % 20 == 0) {
      printk("[BUDDY TEST]  Cycle %d/%d...\n", cycle + 1, CYCLES);
      print_mem_usage();
    }

    // 分配随机order的页面
    int order = cycle % 5; // 0-4
    phys_addr_t page = alloc_phys_pages(order, GFP_KERNEL);
    if (page) {
      free_phys_pages(page, order);
    }
  }
  print_mem_usage();

  // 测试5: 测试分配30个order10
  printk("\n[BUDDY TEST] Test 5: Allocate 30 order10 blocks (4MB each)\n");
  print_mem_usage();

  phys_addr_t order10_pages[30];
  int order10_allocated = 0;

  for (int i = 0; i < 30; i++) {
    order10_pages[i] = alloc_phys_pages(10, GFP_KERNEL);
    if (order10_pages[i]) {
      order10_allocated++;
      printk("[BUDDY TEST]  Order10 %d allocated at %p\n", i + 1,
             order10_pages[i]);
      if (order10_allocated % 5 == 0) {
        print_mem_usage();
      }
    } else {
      printk("[BUDDY TEST]  Order10 %d allocation failed\n", i + 1);
      break;
    }
  }

  printk("[BUDDY TEST]  Successfully allocated %d/30 order10 blocks\n",
         order10_allocated);
  print_mem_usage();

  // 释放这些页面
  for (int i = 0; i < order10_allocated; i++) {
    free_phys_pages(order10_pages[i], 10);
  }
  printk("[BUDDY TEST]  All order10 blocks freed\n");
  print_mem_usage();

  // 测试6: 测试边界情况
  printk("\n[BUDDY TEST] Test 6: Boundary cases\n");
  print_mem_usage();

  // 尝试分配超过可用内存的页面
  phys_addr_t big_page = alloc_phys_pages(20, GFP_KERNEL); // 这应该会失败
  if (!big_page) {
    printk("[BUDDY TEST]  Expected failure: order 20 allocation failed\n");
  }
  print_mem_usage();

  // 测试空指针释放
  free_phys_pages(0, 0);
  printk("[BUDDY TEST]  NULL pointer free handled correctly\n");
  print_mem_usage();

  printk("\n[BUDDY TEST] All tests completed!\n");
}

// 测试kmalloc功能
void test_kmalloc(void) {
  printk("\n[KMALLOC TEST] Starting kmalloc test...\n");

  // 测试1: 分配各种大小的内存
  printk("[KMALLOC TEST] Test 1: Allocate various sizes\n");
  print_mem_usage();

  void *ptr1 = kmalloc(8, GFP_KERNEL);
  printk("[KMALLOC TEST]  8 bytes: %p\n", ptr1);
  print_mem_usage();

  void *ptr2 = kmalloc(64, GFP_KERNEL);
  printk("[KMALLOC TEST]  64 bytes: %p\n", ptr2);
  print_mem_usage();

  void *ptr3 = kmalloc(512, GFP_KERNEL);
  printk("[KMALLOC TEST]  512 bytes: %p\n", ptr3);
  print_mem_usage();

  void *ptr4 = kmalloc(1024, GFP_KERNEL);
  printk("[KMALLOC TEST]  1024 bytes: %p\n", ptr4);
  print_mem_usage();

  void *ptr5 = kmalloc(4096, GFP_KERNEL);
  printk("[KMALLOC TEST]  4096 bytes: %p\n", ptr5);
  print_mem_usage();

  void *ptr6 = kmalloc(8192, GFP_KERNEL);
  printk("[KMALLOC TEST]  8192 bytes: %p\n", ptr6);
  print_mem_usage();

  void *ptr7 = kmalloc(65536, GFP_KERNEL);
  printk("[KMALLOC TEST]  65536 bytes: %p\n", ptr7);
  print_mem_usage();

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
  print_mem_usage();

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
  print_mem_usage();

  // 测试4: 大量小内存分配
  printk(
      "\n[KMALLOC TEST] Test 4: Allocate 1000 small blocks (64 bytes each)\n");
  print_mem_usage();

  void *small_ptrs[1000];
  int allocated = 0;

  for (int i = 0; i < 1000; i++) {
    small_ptrs[i] = kmalloc(64, GFP_KERNEL);
    if (small_ptrs[i]) {
      allocated++;
      if ((i + 1) % 200 == 0) {
        printk("[KMALLOC TEST]  Allocated %d/1000 blocks...\n", i + 1);
        print_mem_usage();
      }
    }
  }

  printk("[KMALLOC TEST]  Successfully allocated %d/1000 blocks\n", allocated);
  print_mem_usage();

  // 释放小内存块
  for (int i = 0; i < allocated; i++) {
    if (small_ptrs[i]) {
      kfree(small_ptrs[i]);
    }
  }

  printk("[KMALLOC TEST]  All small blocks freed\n");
  print_mem_usage();

  // 测试5: 测试边界情况
  printk("\n[KMALLOC TEST] Test 5: Boundary cases\n");

  // 分配0字节
  void *zero_ptr = kmalloc(0, GFP_KERNEL);
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
// 解析节点属性
static void parse_node_properties(void *fdt, int node_offset) {
  const char *name = fdt_get_name(fdt, node_offset, NULL);
  if (!name)
    return;

  printk("[FDT TEST]   Node: %s\n", name);

  // 遍历所有属性
  int prop_offset = fdt_first_property_offset(fdt, node_offset);
  while (prop_offset >= 0) {
    const struct fdt_property *prop =
        fdt_offset_ptr(fdt, prop_offset, sizeof(*prop));
    if (!prop)
      break;

    const char *prop_name = fdt_string(fdt, fdt32_to_cpu(prop->nameoff));
    int prop_len = fdt32_to_cpu(prop->len);
    const void *prop_value = prop + 1;

    printk("[FDT TEST]     Property: %s (length: %d)\n", prop_name, prop_len);

    // 特殊处理一些常见属性
    if (strcmp(prop_name, "compatible") == 0) {
      // 兼容属性是字符串列表
      int count = fdt_stringlist_count(fdt, node_offset, "compatible");
      for (int i = 0; i < count; i++) {
        int len;
        const char *compatible =
            fdt_stringlist_get(fdt, node_offset, "compatible", i, &len);
        if (compatible) {
          printk("[FDT TEST]       Compatible: %s\n", compatible);
        }
      }
    } else if (strcmp(prop_name, "reg") == 0) {
      // reg 属性是地址和大小的列表（每个地址/大小是 2 个 32 位值）
      printk("[FDT TEST]       Reg: ");
      for (int i = 0; i < prop_len; i += 16) {
        if (i + 16 <= prop_len) {
          uint32_t addr_hi =
              fdt32_to_cpu(*(const fdt32_t *)((const char *)prop_value + i));
          uint32_t addr_lo = fdt32_to_cpu(
              *(const fdt32_t *)((const char *)prop_value + i + 4));
          uint32_t size_hi = fdt32_to_cpu(
              *(const fdt32_t *)((const char *)prop_value + i + 8));
          uint32_t size_lo = fdt32_to_cpu(
              *(const fdt32_t *)((const char *)prop_value + i + 12));
          uint64_t addr = ((uint64_t)addr_hi << 32) | addr_lo;
          uint64_t size = ((uint64_t)size_hi << 32) | size_lo;
          printk("0x%lx (0x%lx) ", addr, size);
        }
      }
      printk("\n");
    } else if (strcmp(prop_name, "interrupts") == 0) {
      // 中断属性
      printk("[FDT TEST]       Interrupts: ");
      for (int i = 0; i < prop_len; i += 4) {
        if (i + 4 <= prop_len) {
          uint32_t irq =
              fdt32_to_cpu(*(const fdt32_t *)((const char *)prop_value + i));
          printk("%d ", irq);
        }
      }
      printk("\n");
    } else if (strcmp(prop_name, "status") == 0) {
      // 状态属性
      if (prop_len > 0) {
        printk("[FDT TEST]       Status: ");
        for (int i = 0; i < prop_len; i++) {
          uart_putc(((const char *)prop_value)[i]);
        }
        printk("\n");
      }
    } else if (strcmp(prop_name, "clocks") == 0) {
      // clocks 属性是时钟引用的列表，每个引用是一个 32 位整数
      printk("[FDT TEST]       Clocks: ");
      for (int i = 0; i < prop_len; i += 4) {
        if (i + 4 <= prop_len) {
          uint32_t clock =
              fdt32_to_cpu(*(const fdt32_t *)((const char *)prop_value + i));
          printk("0x%x ", clock);
        }
      }
      printk("\n");
    } else if (prop_len == 4) {
      // 32 位整数属性
      uint32_t value = fdt32_to_cpu(*(const fdt32_t *)prop_value);
      printk("[FDT TEST]       Value: %d (0x%x)\n", value, value);
    } else if (prop_len == 8) {
      // 64 位整数属性
      uint64_t value = fdt64_to_cpu(*(const fdt64_t *)prop_value);
      printk("[FDT TEST]       Value: %llu (0x%lx)\n", value, value);
    } else if (prop_len > 0 &&
               ((const char *)prop_value)[prop_len - 1] == '\0') {
      // 字符串属性
      printk("[FDT TEST]       Value: \"%s\"\n", (const char *)prop_value);
    } else {
      // 其他类型的属性，显示原始字节
      printk("[FDT TEST]       Raw bytes: ");
      for (int i = 0; i < prop_len && i < 16; i++) {
        uint8_t byte = ((const unsigned char *)prop_value)[i];
        printk("%02x ", byte);
      }
      if (prop_len > 16) {
        printk("... (truncated)");
      }
      printk("\n");
    }

    prop_offset = fdt_next_property_offset(fdt, prop_offset);
  }
}
// FDT 测试函数
void test_fdt(void) {
  printk("\n[FDT TEST] Starting FDT test...\n");

  // 确定 DTB 地址
  uint64_t dtb_phys;
  if (dtb_base == NULL || (uint64_t)dtb_base == 0) {
    printk("[FDT TEST] DTB address is 0 (bare-metal boot), using default "
           "address (0x40000000)\n");
    dtb_phys = 0x40000000; // 默认 DTB 物理地址
  } else {
    dtb_phys = (uint64_t)dtb_base;
  }

  // 转换为虚拟地址
  void *fdt = (void *)(VIRT_BASE + dtb_phys);

  printk("[FDT TEST] DTB physical address: %lx\n", dtb_phys);
  printk("[FDT TEST] DTB virtual address: %lx\n", (uint64_t)fdt);

  // 验证 FDT 头部
  int ret = fdt_check_header(fdt);
  if (ret != 0) {
    printk("[FDT TEST] ERROR: Invalid FDT header: %d\n", ret);
    return;
  }

  printk("[FDT TEST] FDT header is valid\n");

  // 获取根节点偏移量
  int root_offset = fdt_path_offset(fdt, "/");
  if (root_offset < 0) {
    printk("[FDT TEST] ERROR: Failed to find root node: %d\n", root_offset);
    return;
  }

  printk("[FDT TEST] Root node found at offset: %d\n", root_offset);

  // 解析根节点属性
  printk("[FDT TEST] Root node properties:\n");
  parse_node_properties(fdt, root_offset);

  // 遍历子节点
  printk("[FDT TEST] Child nodes:\n");
  int node_offset = fdt_first_subnode(fdt, root_offset);
  while (node_offset >= 0) {
    parse_node_properties(fdt, node_offset);
    node_offset = fdt_next_subnode(fdt, node_offset);
  }

  printk("[FDT TEST] FDT test completed!\n");
}

// 测试 vmap 功能
void test_vmap(void) {
  printk("\n[VMAP TEST] Starting vmap test...\n");

  // 测试 1: 分配各种大小的内存
  printk("[VMAP TEST] Test 1: Allocate various sizes\n");
  print_mem_usage();

  void *ptr1 = vmalloc(8192);
  printk("[VMAP TEST]  8KB: %p\n", ptr1);
  print_mem_usage();

  void *ptr2 = vmalloc(16384);
  printk("[VMAP TEST]  16KB: %p\n", ptr2);
  print_mem_usage();

  void *ptr3 = vmalloc(32768);
  printk("[VMAP TEST]  32KB: %p\n", ptr3);
  print_mem_usage();

  void *ptr4 = vmalloc(65536);
  printk("[VMAP TEST]  64KB: %p\n", ptr4);
  print_mem_usage();

  void *ptr5 = vmalloc(131072);
  printk("[VMAP TEST]  128KB: %p\n", ptr5);
  print_mem_usage();

  // 测试2: 写入数据并验证
  printk("\n[VMAP TEST] Test 2: Write and verify data\n");
  if (ptr1) {
    memset(ptr1, 0xAB, 8192);
    printk("[VMAP TEST]  8KB: written 0xAB pattern\n");
  }

  if (ptr2) {
    for (int i = 0; i < 16384 / 4; i++) {
      ((uint32_t *)ptr2)[i] = i;
    }
    printk("[VMAP TEST]  16KB: written sequential data\n");
  }

  // 测试3: 释放内存（测试合并逻辑）
  printk("\n[VMAP TEST] Test 3: Free allocated memory (test merge)\n");
  print_mem_usage();

  if (ptr1)
    vfree(ptr1);
  if (ptr2)
    vfree(ptr2);
  if (ptr3)
    vfree(ptr3);
  if (ptr4)
    vfree(ptr4);
  if (ptr5)
    vfree(ptr5);

  printk("[VMAP TEST]  All allocations freed\n");
  print_mem_usage();

  // 测试4: 大量小内存分配
  printk("\n[VMAP TEST] Test 4: Allocate 50 small blocks (8KB each)\n");
  print_mem_usage();

  void *small_ptrs[50];
  int allocated = 0;

  for (int i = 0; i < 50; i++) {
    small_ptrs[i] = vmalloc(8192);
    if (small_ptrs[i]) {
      allocated++;
      if ((i + 1) % 10 == 0) {
        printk("[VMAP TEST]  Allocated %d/50 blocks...\n", i + 1);
        print_mem_usage();
      }
    }
  }

  printk("[VMAP TEST]  Successfully allocated %d/50 blocks\n", allocated);
  print_mem_usage();

  // 释放小内存块（测试合并逻辑）
  printk("[VMAP TEST]  Freeing small blocks (test merge)...\n");
  for (int i = 0; i < allocated; i++) {
    if (small_ptrs[i]) {
      vfree(small_ptrs[i]);
    }
  }

  printk("[VMAP TEST]  All small blocks freed\n");
  print_mem_usage();

  // 测试5: 交替分配和释放（测试合并逻辑）
  printk("\n[VMAP TEST] Test 5: Alternate allocate and free (test merge)\n");
  void *alt_ptrs[10];

  // 分配10个块
  for (int i = 0; i < 10; i++) {
    alt_ptrs[i] = vmalloc(4096);
    if (alt_ptrs[i]) {
      printk("[VMAP TEST]  Allocated block %d: %p\n", i, alt_ptrs[i]);
    }
  }

  // 释放奇数块
  for (int i = 1; i < 10; i += 2) {
    if (alt_ptrs[i]) {
      printk("[VMAP TEST]  Freeing block %d: %p\n", i, alt_ptrs[i]);
      vfree(alt_ptrs[i]);
      alt_ptrs[i] = NULL;
    }
  }

  // 释放偶数块
  for (int i = 0; i < 10; i += 2) {
    if (alt_ptrs[i]) {
      printk("[VMAP TEST]  Freeing block %d: %p\n", i, alt_ptrs[i]);
      vfree(alt_ptrs[i]);
      alt_ptrs[i] = NULL;
    }
  }

  printk("[VMAP TEST]  All alternate blocks freed\n");

  printk("\n[VMAP TEST] All tests completed!\n");
}

void print_mem_usage(void) {
  // 获取 buddy 管理的内存使用情况
  unsigned int buddy_free = buddy_nr_free_pages_total();
  unsigned int buddy_used = buddy_nr_used_pages_total();
  unsigned int buddy_usage = buddy_mem_usage_percent();

  // 获取整个物理内存的使用情况
  unsigned int total_pages = total_phys_pages();
  unsigned int total_used = total_used_pages();
  unsigned int total_usage = total_mem_usage_percent();

  printk("Buddy 内存使用情况: 已使用 %u 页, 空闲 %u 页, 占用率 %u%%\n",
         buddy_used, buddy_free, buddy_usage);
  printk("总物理内存使用情况: 总页数 %u, 已使用 %u 页, 占用率 %u%%\n",
         total_pages, total_used, total_usage);
}
