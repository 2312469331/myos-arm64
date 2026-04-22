#ifndef _VMALLOC_H
#define _VMALLOC_H

#include <vmap.h>
#include <stddef.h>

// 装修记录单：记录虚拟地址对应的物理页数组
struct vm_struct {
    void *addr;             // 虚拟地址起始
    unsigned long size;     // 大小
    unsigned int nr_pages;  // 占用几个物理页
    void **pages;           // 动态分配的数组，存着每一个物理页的地址
    unsigned long flags;    // 从 vmap_area 抄过来的标志位
    list_head_t list;       // 挂在全局的 vm_struct 链表上
};

// ================= 上层 API 壳 =================
void *vmalloc(unsigned long size);
void *vzalloc(unsigned long size);
void vfree(const void *addr);

// 硬件 IO 映射
void *ioremap(uint64_t phys_addr, unsigned long size);
void iounmap(const void *addr);

#endif
