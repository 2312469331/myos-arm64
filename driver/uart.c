#include <uart.h>
#include <io.h> // 新增：引入封装好的IO API
#include <types.h>
#include <vmalloc.h> // 新增：引入 ioremap 函数
#include <libfdt.h> // 新增：引入设备树访问函数
#include <libc.h>
// 全局UART基地址，在uart_init函数中使用ioremap映射
volatile void *uart_base = NULL;

// UART物理地址 (QEMU virt ARM PL011 UART0)
#define UART0_PHYS_BASE 0x9000000UL

// 辅助宏：计算UART寄存器地址
#define UART_REG(reg) ((volatile void *)((uint64_t)(uart_base) + (uint64_t)(reg)))

// 内部函数：计算波特率寄存器值并配置
static void uart_set_baudrate(uint32_t baudrate) {
  // 公式：BAUDDIV = 时钟频率 / (16 * 波特率)
  uint32_t bauddiv = UART_CLOCK / (16 * baudrate);
  // 整数部分
  io_write32(UART_REG(UART_IBRD), bauddiv / 64);
  // 小数部分 (0~63)
  io_write32(UART_REG(UART_FBRD), bauddiv % 64);
}

void uart_init(void) {
  // 1. 使用ioremap映射UART物理地址
  if (!uart_base) {
    uint64_t uart_phys_base = UART0_PHYS_BASE; // 默认值

    // 尝试从设备树获取 UART 物理地址
    extern void *dtb_base;
    bool found_uart = false;
      // 确定 DTB 地址
  if (dtb_base == NULL || (uint64_t)dtb_base == 0) {
    // printk("[FDT TEST] DTB address is 0 (bare-metal boot), using default "
    //        "address (0x40000000)\n");
    dtb_base = (void *)0x40000000; // 默认 DTB 物理地址
  } 
    if (dtb_base) {
      // 1. 首先尝试从 chosen 节点获取 stdout-path
      int chosen = fdt_path_offset(dtb_base, "/chosen");
      if (chosen >= 0) {
        const char *stdout_path = fdt_getprop(dtb_base, chosen, "stdout-path", NULL);
        if (stdout_path) {
          int uart_node = fdt_path_offset(dtb_base, stdout_path);
          if (uart_node >= 0) {
            int len;
            const fdt32_t *prop = fdt_getprop(dtb_base, uart_node, "reg", &len);
            if (prop && len >= 8) {
              uint32_t addr_hi = fdt32_to_cpu(prop[0]);
              uint32_t addr_lo = fdt32_to_cpu(prop[1]);
              uart_phys_base = ((uint64_t)addr_hi << 32) | addr_lo;
              found_uart = true;
            }
          }
        }
      }
      
      // 2. 如果没有找到，遍历设备树，查找合适的 UART 节点
      if (!found_uart) {
        int root = fdt_path_offset(dtb_base, "/");
        if (root >= 0) {
          int node = fdt_first_subnode(dtb_base, root);
          while (node >= 0) {
            // 检查兼容属性
            const char *compatible = fdt_getprop(dtb_base, node, "compatible", NULL);
            if (compatible && strstr(compatible, "arm,pl011")) {
              // 检查状态属性，跳过禁用的节点
              const char *status = fdt_getprop(dtb_base, node, "status", NULL);
              if (!status || strcmp(status, "okay") == 0) {
                // 检查安全状态，跳过安全世界专用的节点
                const char *secure_status = fdt_getprop(dtb_base, node, "secure-status", NULL);
                if (!secure_status) {
                  // 找到合适的 UART 节点，获取 reg 属性
                  int len;
                  const fdt32_t *prop = fdt_getprop(dtb_base, node, "reg", &len);
                  if (prop && len >= 8) {
                    uint32_t addr_hi = fdt32_to_cpu(prop[0]);
                    uint32_t addr_lo = fdt32_to_cpu(prop[1]);
                    uart_phys_base = ((uint64_t)addr_hi << 32) | addr_lo;
                    found_uart = true;
                    break;
                  }
                }
              }
            }
            node = fdt_next_subnode(dtb_base, node);
          }
        }
      }
    }

    uart_base = ioremap(uart_phys_base, 4096);
    if (!uart_base) {
      // 映射失败，无法初始化UART
      return;
    }
  }

  // 2. 先关闭UART，避免配置过程中异常
  uint32_t cr_val = io_read32(UART_REG(UART_CR));
  io_write32(UART_REG(UART_CR), cr_val & ~UART_CR_UARTEN);

  // 2. 配置波特率 (默认115200)
  uart_set_baudrate(DEFAULT_BAUDRATE);

  // 3. 配置数据格式：8位数据、1位停止、无校验、关闭FIFO (裸机简化)
  io_write32(UART_REG(UART_LCR_H), UART_LCR_H_WLEN8);

  // 4. 清除接收错误标志
  uart_clear_error();
  // 配置接收中断
  uart_irq_init();
  // 5. 使能UART + 发送 + 接收
  io_write32(UART_REG(UART_CR), UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
}

void uart_putc(char c) {
  // 处理换行：\n 自动补 \r，解决终端换行错位
  if (c == '\n') {
    while ((io_read32(UART_REG(UART_FR)) & UART_FR_TXFE) == 0)
      ; // 等待发送FIFO空
    io_write32(UART_REG(UART_DR), '\r');
  }

  // 阻塞等待发送FIFO空
  while ((io_read32(UART_REG(UART_FR)) & UART_FR_TXFE) == 0)
    ;
  io_write32(UART_REG(UART_DR), (uint32_t)c);
}

void uart_puts(const char *s) {
  while (*s != '\0') {
    uart_putc(*s++);
  }
}

char uart_getc(uart_error_t *err) {
  // 阻塞等待接收FIFO非空
  while ((io_read32(UART_REG(UART_FR)) & UART_FR_RXFE) != 0)
    ;

  // 读取错误码 (若需要)
  if (err != NULL) {
    uint32_t rsr_val = io_read32(UART_REG(UART_RSR_ECR));
    *err = (uart_error_t)(rsr_val & 0x0F);
    uart_clear_error(); // 读取后立即清除错误
  }

  // 返回接收的字符
  uint32_t dr_val = io_read32(UART_REG(UART_DR));
  return (char)(dr_val & 0xFF);
}

bool uart_putc_nonblock(char c) {
  // 检查发送FIFO是否满
  if ((io_read32(UART_REG(UART_FR)) & UART_FR_TXFF) != 0) {
    return false;
  }

  io_write32(UART_REG(UART_DR), (uint32_t)c);
  return true;
}

bool uart_getc_nonblock(char *c, uart_error_t *err) {
  if (c == NULL)
    return false;

  // 检查接收FIFO是否空
  if ((io_read32(UART_REG(UART_FR)) & UART_FR_RXFE) != 0) {
    return false;
  }

  // 读取错误码
  if (err != NULL) {
    uint32_t rsr_val = io_read32(UART_REG(UART_RSR_ECR));
    *err = (uart_error_t)(rsr_val & 0x0F);
    uart_clear_error();
  }

  uint32_t dr_val = io_read32(UART_REG(UART_DR));
  *c = (char)(dr_val & 0xFF);
  return true;
}

bool uart_tx_ready(void) {
  return (io_read32(UART_REG(UART_FR)) & UART_FR_TXFF) == 0;
}

bool uart_rx_ready(void) {
  return (io_read32(UART_REG(UART_FR)) & UART_FR_RXFE) == 0;
}

void uart_clear_error(void) {
  // 写入任意值清除所有错误标志
  io_write32(UART_REG(UART_RSR_ECR), 0x0F);
}

void uart_set_loopback(bool enable) {
  uint32_t cr_val = io_read32(UART_REG(UART_CR));
  if (enable) {
    cr_val |= UART_CR_LBE;
  } else {
    cr_val &= ~UART_CR_LBE;
  }

  io_write32(UART_REG(UART_CR), cr_val);
}
// 配置 UART 接收中断（半满触发）
void uart_irq_init(void) {
  // 设置接收FIFO半满触发
  io_write32(UART_REG(UART_IFLS),
             UART_IFLS_RXIFLSEL_1_2 | UART_IFLS_TXIFLSEL_1_2);

  // 使能接收中断
  io_write32(UART_REG(UART_IMSC), UART_IMSC_RXIM);

  // 清除接收中断标志
  io_write32(UART_REG(UART_ICR), UART_ICR_RXIC);
}
