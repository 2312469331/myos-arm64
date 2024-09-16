/**
 * @file io.h
 * @brief ARM64 I/O 操作接口
 * 
 * 提供内存映射I/O (MMIO) 操作函数，包括寄存器读写、
 * 内存屏障、DMA操作等。适用于设备驱动开发。
 */

#ifndef _IO_H
#define _IO_H

#include "mm.h"

/*============================================================================
 *                              ARM64 内存屏障
 *============================================================================*/

/**
 * @brief 数据内存屏障 (DMB)
 * 确保所有内存访问在屏障前后正确排序
 */
static inline void dmb(void) {
    __asm__ volatile ("dmb sy" ::: "memory");
}

/**
 * @brief 数据同步屏障 (DSB)
 * 确保所有内存访问完成
 */
static inline void dsb(void) {
    __asm__ volatile ("dsb sy" ::: "memory");
}

/**
 * @brief 指令同步屏障 (ISB)
 * 刷新流水线，确保后续指令从缓存/内存重新获取
 */
static inline void isb(void) {
    __asm__ volatile ("isb" ::: "memory");
}

/**
 * @brief 读内存屏障
 */
static inline void rmb(void) {
    __asm__ volatile ("dmb ld" ::: "memory");
}

/**
 * @brief 写内存屏障
 */
static inline void wmb(void) {
    __asm__ volatile ("dmb st" ::: "memory");
}

/**
 * @brief 编译器屏障
 */
#define barrier()   __asm__ volatile ("" ::: "memory")

/*============================================================================
 *                              MMIO 读写操作
 *============================================================================*/

/**
 * @brief 读8位寄存器
 * @param addr 寄存器地址
 * @return 寄存器值
 */
static inline uint8_t readb(volatile void *addr) {
    uint8_t val;
    __asm__ volatile ("ldrb %w0, [%1]" 
                      : "=r" (val) 
                      : "r" (addr) 
                      : "memory");
    return val;
}

/**
 * @brief 读16位寄存器
 * @param addr 寄存器地址
 * @return 寄存器值
 */
static inline uint16_t readw(volatile void *addr) {
    uint16_t val;
    __asm__ volatile ("ldrh %w0, [%1]" 
                      : "=r" (val) 
                      : "r" (addr) 
                      : "memory");
    return val;
}

/**
 * @brief 读32位寄存器
 * @param addr 寄存器地址
 * @return 寄存器值
 */
static inline uint32_t readl(volatile void *addr) {
    uint32_t val;
    __asm__ volatile ("ldr %w0, [%1]" 
                      : "=r" (val) 
                      : "r" (addr) 
                      : "memory");
    return val;
}

/**
 * @brief 读64位寄存器
 * @param addr 寄存器地址
 * @return 寄存器值
 */
static inline uint64_t readq(volatile void *addr) {
    uint64_t val;
    __asm__ volatile ("ldr %0, [%1]" 
                      : "=r" (val) 
                      : "r" (addr) 
                      : "memory");
    return val;
}

/**
 * @brief 写8位寄存器
 * @param val 要写入的值
 * @param addr 寄存器地址
 */
static inline void writeb(uint8_t val, volatile void *addr) {
    __asm__ volatile ("strb %w0, [%1]" 
                      : 
                      : "r" (val), "r" (addr) 
                      : "memory");
}

/**
 * @brief 写16位寄存器
 * @param val 要写入的值
 * @param addr 寄存器地址
 */
static inline void writew(uint16_t val, volatile void *addr) {
    __asm__ volatile ("strh %w0, [%1]" 
                      : 
                      : "r" (val), "r" (addr) 
                      : "memory");
}

/**
 * @brief 写32位寄存器
 * @param val 要写入的值
 * @param addr 寄存器地址
 */
static inline void writel(uint32_t val, volatile void *addr) {
    __asm__ volatile ("str %w0, [%1]" 
                      : 
                      : "r" (val), "r" (addr) 
                      : "memory");
}

/**
 * @brief 写64位寄存器
 * @param val 要写入的值
 * @param addr 寄存器地址
 */
static inline void writeq(uint64_t val, volatile void *addr) {
    __asm__ volatile ("str %0, [%1]" 
                      : 
                      : "r" (val), "r" (addr) 
                      : "memory");
}

/*============================================================================
 *                              带屏障的MMIO操作
 *============================================================================*/

/**
 * @brief 读32位寄存器 (带屏障)
 */
static inline uint32_t readl_relaxed(volatile void *addr) {
    return readl(addr);
}

/**
 * @brief 写32位寄存器 (带屏障)
 */
static inline void writel_relaxed(uint32_t val, volatile void *addr) {
    writel(val, addr);
}

/**
 * @brief 读32位寄存器 (严格有序)
 */
static inline uint32_t readl_strict(volatile void *addr) {
    uint32_t val;
    rmb();
    val = readl(addr);
    rmb();
    return val;
}

/**
 * @brief 写32位寄存器 (严格有序)
 */
static inline void writel_strict(uint32_t val, volatile void *addr) {
    wmb();
    writel(val, addr);
    wmb();
}

/*============================================================================
 *                              位操作
 *============================================================================*/

/**
 * @brief 设置寄存器位
 * @param addr 寄存器地址
 * @param mask 位掩码
 */
static inline void setbits32(volatile void *addr, uint32_t mask) {
    writel(readl(addr) | mask, addr);
}

/**
 * @brief 清除寄存器位
 * @param addr 寄存器地址
 * @param mask 位掩码
 */
static inline void clrbits32(volatile void *addr, uint32_t mask) {
    writel(readl(addr) & ~mask, addr);
}

/**
 * @brief 切换寄存器位
 * @param addr 寄存器地址
 * @param mask 位掩码
 */
static inline void togglebits32(volatile void *addr, uint32_t mask) {
    writel(readl(addr) ^ mask, addr);
}

/**
 * @brief 修改寄存器位
 * @param addr 寄存器地址
 * @param clear 清除掩码
 * @param set 设置掩码
 */
static inline void clrsetbits32(volatile void *addr, uint32_t clear, uint32_t set) {
    writel((readl(addr) & ~clear) | set, addr);
}

/**
 * @brief 设置64位寄存器位
 */
static inline void setbits64(volatile void *addr, uint64_t mask) {
    writeq(readq(addr) | mask, addr);
}

/**
 * @brief 清除64位寄存器位
 */
static inline void clrbits64(volatile void *addr, uint64_t mask) {
    writeq(readq(addr) & ~mask, addr);
}

/**
 * @brief 修改64位寄存器位
 */
static inline void clrsetbits64(volatile void *addr, uint64_t clear, uint64_t set) {
    writeq((readq(addr) & ~clear) | set, addr);
}

/*============================================================================
 *                              内存拷贝和填充
 *============================================================================*/

/**
 * @brief 内存拷贝
 * @param dest 目标地址
 * @param src 源地址
 * @param n 字节数
 * @return 目标地址
 */
void* memcpy(void *dest, const void *src, size_t n);

/**
 * @brief 内存填充
 * @param dest 目标地址
 * @param c 填充值
 * @param n 字节数
 * @return 目标地址
 */
void* memset(void *dest, int c, size_t n);

/**
 * @brief 内存比较
 * @param s1 第一个地址
 * @param s2 第二个地址
 * @param n 字节数
 * @return 比较结果
 */
int memcmp(const void *s1, const void *s2, size_t n);

/**
 * @brief 内存移动 (处理重叠)
 * @param dest 目标地址
 * @param src 源地址
 * @param n 字节数
 * @return 目标地址
 */
void* memmove(void *dest, const void *src, size_t n);

/**
 * @brief 内存清零
 * @param dest 目标地址
 * @param n 字节数
 */
static inline void memzero(void *dest, size_t n) {
    memset(dest, 0, n);
}

/*============================================================================
 *                              字符串操作
 *============================================================================*/

/**
 * @brief 字符串长度
 * @param s 字符串
 * @return 长度
 */
size_t strlen(const char *s);

/**
 * @brief 字符串拷贝
 * @param dest 目标地址
 * @param src 源字符串
 * @return 目标地址
 */
char* strcpy(char *dest, const char *src);

/**
 * @brief 字符串比较
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @return 比较结果
 */
int strcmp(const char *s1, const char *s2);

/*============================================================================
 *                              DMA 操作
 *============================================================================*/

/**
 * @brief DMA缓存清理 (写回)
 * 确保CPU写缓冲区数据刷新到内存
 * @param addr 起始地址
 * @param size 大小
 */
void dma_cache_clean(void *addr, size_t size);

/**
 * @brief DMA缓存无效化
 * 使缓存行无效，确保下次读取从内存获取
 * @param addr 起始地址
 * @param size 大小
 */
void dma_cache_invalidate(void *addr, size_t size);

/**
 * @brief DMA缓存同步 (清理+无效化)
 * @param addr 起始地址
 * @param size 大小
 */
void dma_cache_sync(void *addr, size_t size);

/*============================================================================
 *                              I/O 内存映射
 *============================================================================*/

/**
 * @brief 请求I/O内存区域
 * @param phys 物理基地址
 * @param size 区域大小
 * @param name 区域名称
 * @return 0 成功, 负数失败
 */
int io_request_region(phys_addr_t phys, size_t size, const char *name);

/**
 * @brief 释放I/O内存区域
 * @param phys 物理基地址
 * @param size 区域大小
 */
void io_release_region(phys_addr_t phys, size_t size);

/**
 * @brief 映射I/O内存到虚拟地址空间
 * @param phys 物理基地址
 * @param size 区域大小
 * @return 虚拟地址, NULL表示失败
 */
void* ioremap(phys_addr_t phys, size_t size);

/**
 * @brief 取消I/O内存映射
 * @param addr 虚拟地址
 * @param size 区域大小
 */
void iounmap(void *addr, size_t size);

/**
 * @brief 映射I/O内存 (无缓存)
 * @param phys 物理基地址
 * @param size 区域大小
 * @return 虚拟地址, NULL表示失败
 */
void* ioremap_nocache(phys_addr_t phys, size_t size);

/**
 * @brief 映射I/O内存 (设备类型)
 * @param phys 物理基地址
 * @param size 区域大小
 * @return 虚拟地址, NULL表示失败
 */
void* ioremap_device(phys_addr_t phys, size_t size);

/*============================================================================
 *                              端口I/O (用于兼容)
 *============================================================================*/

/**
 * @brief 读8位端口
 * @param port 端口号
 * @return 端口值
 */
static inline uint8_t inb(uint16_t port) {
    /* ARM64通常不使用端口I/O，这里提供兼容接口 */
    return 0;
}

/**
 * @brief 写8位端口
 * @param port 端口号
 * @param val 要写入的值
 */
static inline void outb(uint16_t port, uint8_t val) {
    /* ARM64通常不使用端口I/O，这里提供兼容接口 */
    (void)port;
    (void)val;
}

/*============================================================================
 *                              延时函数
 *============================================================================*/

/**
 * @brief 纳秒级延时
 * @param ns 纳秒数
 */
void ndelay(uint64_t ns);

/**
 * @brief 微秒级延时
 * @param us 微秒数
 */
void udelay(uint64_t us);

/**
 * @brief 毫秒级延时
 * @param ms 毫秒数
 */
void mdelay(uint64_t ms);

/*============================================================================
 *                              原子操作
 *============================================================================*/

/**
 * @brief 原子读取32位值
 */
static inline uint32_t atomic_read32(volatile uint32_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}

/**
 * @brief 原子写入32位值
 */
static inline void atomic_write32(volatile uint32_t *addr, uint32_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}

/**
 * @brief 原子加
 */
static inline uint32_t atomic_add32(volatile uint32_t *addr, uint32_t val) {
    return __atomic_fetch_add(addr, val, __ATOMIC_SEQ_CST);
}

/**
 * @brief 原子减
 */
static inline uint32_t atomic_sub32(volatile uint32_t *addr, uint32_t val) {
    return __atomic_fetch_sub(addr, val, __ATOMIC_SEQ_CST);
}

/**
 * @brief 原子比较交换
 */
static inline bool atomic_cas32(volatile uint32_t *addr, uint32_t expected, uint32_t new_val) {
    return __atomic_compare_exchange_n(addr, &expected, new_val, 
                                       false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

/**
 * @brief 原子交换
 */
static inline uint32_t atomic_swap32(volatile uint32_t *addr, uint32_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}

/**
 * @brief 原子读取64位值
 */
static inline uint64_t atomic_read64(volatile uint64_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}

/**
 * @brief 原子写入64位值
 */
static inline void atomic_write64(volatile uint64_t *addr, uint64_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}

/**
 * @brief 原子比较交换64位
 */
static inline bool atomic_cas64(volatile uint64_t *addr, uint64_t expected, uint64_t new_val) {
    return __atomic_compare_exchange_n(addr, &expected, new_val, 
                                       false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#endif /* _IO_H */
