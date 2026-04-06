#ifndef __IO_H__
#define __IO_H__

#include "printk.h"
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif

// 内存屏障：保证设备访问完成
#define mb() __asm__ __volatile__("dsb sy; isb" ::: "memory")
// 编译器屏障：禁止 GCC 重排/优化
#define barrier() __asm__ __volatile__("" ::: "memory")

/**
 * io_read8/16/32 - 从设备地址读取
 * @addr: 设备寄存器物理地址
 */
static inline uint8_t io_read8(const volatile void *addr) {
  uint8_t val = *(const volatile uint8_t *)addr;
  barrier();
  return val;
}

static inline uint16_t io_read16(const volatile void *addr) {
  uint16_t val = *(const volatile uint16_t *)addr;
  barrier();
  return val;
}

static inline uint32_t io_read32(const volatile void *addr) {
  uint32_t val = *(const volatile uint32_t *)addr;
  barrier();
  return val;
}

/**
 * io_write8/16/32 - 写入设备寄存器
 * @addr: 设备寄存器物理地址
 * @val:  要写入的值
 */
static inline void io_write8(volatile void *addr, uint8_t val) {
  *(volatile uint8_t *)addr = val;
  mb();
}

static inline void io_write16(volatile void *addr, uint16_t val) {
  *(volatile uint16_t *)addr = val;
  mb();
}

static inline void io_write32(volatile void *addr, uint32_t val) {
  *(volatile uint32_t *)addr = val;
  mb();
}

static inline void io_write32_log(volatile void *addr, uint32_t val,
                                  const char *name) {
  printk("[IO WRITE] %s (%lx) = %x\n", name, (uintptr_t)addr, val);
  io_write32(addr, val);
}

#ifdef __cplusplus
}
#endif

#endif // __IO_H__
