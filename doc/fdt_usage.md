# FDT 库使用说明

## 1. 概述

FDT（Flattened Device Tree）库是一个用于解析和操作设备树（Device Tree）的工具库，主要用于在裸机或操作系统启动时获取硬件设备的配置信息。本文档详细介绍如何在 MyOS 中使用 FDT 库。

## 2. 基本概念

### 2.1 设备树

设备树是一种描述硬件设备的数据结构，以树状结构组织，包含了设备的名称、属性和子节点。每个节点可以包含多个属性，每个属性由名称和值组成。

### 2.2 FDT 结构

FDT 库操作的是编译后的设备树二进制文件（.dtb），其结构包括：
- 头部（header）：包含魔数、大小等信息
- 内存预留块（memory reserve map）：指定需要预留的内存区域
- 结构块（structure block）：包含设备树的节点和属性
- 字符串块（string block）：存储节点和属性的名称

## 3. 常用函数

### 3.1 初始化和基本操作

#### 3.1.1 检查设备树有效性

```c
#include <libfdt.h>

int fdt_check_header(const void *fdt);
```

**功能**：检查设备树头部的有效性
**参数**：
- `fdt`：设备树的地址
**返回值**：
- 0：成功
- 负数：错误代码

#### 3.1.2 获取根节点偏移量

```c
int fdt_path_offset(const void *fdt, const char *path);
```

**功能**：根据路径获取节点的偏移量
**参数**：
- `fdt`：设备树的地址
- `path`：节点路径（如 "/chosen"）
**返回值**：
- 正数：节点的偏移量
- 负数：错误代码

### 3.2 节点操作

#### 3.2.1 获取子节点

```c
int fdt_first_subnode(const void *fdt, int offset);
int fdt_next_subnode(const void *fdt, int offset);
```

**功能**：获取节点的第一个子节点和下一个子节点
**参数**：
- `fdt`：设备树的地址
- `offset`：父节点的偏移量
**返回值**：
- 正数：子节点的偏移量
- -FDT_ERR_NOTFOUND：没有子节点
- 其他负数：错误代码

### 3.3 属性操作

#### 3.3.1 获取属性值

```c
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp);
```

**功能**：获取节点的属性值
**参数**：
- `fdt`：设备树的地址
- `nodeoffset`：节点的偏移量
- `name`：属性名称
- `lenp`：输出参数，用于存储属性值的长度
**返回值**：
- 非空指针：属性值的地址
- NULL：属性不存在

#### 3.3.2 获取字符串列表属性

```c
const char *fdt_stringlist_get(const void *fdt, int nodeoffset, const char *name, int index, int *lenp);
```

**功能**：获取字符串列表属性中的第 index 个字符串
**参数**：
- `fdt`：设备树的地址
- `nodeoffset`：节点的偏移量
- `name`：属性名称
- `index`：字符串索引
- `lenp`：输出参数，用于存储字符串的长度
**返回值**：
- 非空指针：字符串的地址
- NULL：索引超出范围或属性不存在

### 3.4 数据转换

#### 3.4.1 大小端转换

```c
#define fdt32_to_cpu(x) ((uint32_t)bswap_32((x)))
#define fdt64_to_cpu(x) ((uint64_t)bswap_64((x)))
```

**功能**：将设备树中的大端数据转换为本地端序
**参数**：
- `x`：设备树中的数据
**返回值**：转换后的数据

## 4. 示例代码

### 4.1 遍历设备树

```c
#include <libfdt.h>

void traverse_device_tree(const void *dtb) {
    int root = fdt_path_offset(dtb, "/");
    if (root < 0) {
        printk("Failed to find root node: %d\n", root);
        return;
    }

    int node = fdt_first_subnode(dtb, root);
    while (node >= 0) {
        const char *name = fdt_get_name(dtb, node, NULL);
        if (name) {
            printk("Node: %s\n", name);
        }
        node = fdt_next_subnode(dtb, node);
    }
}
```

### 4.2 获取 UART 物理地址

```c
#include <libfdt.h>

uint64_t get_uart_phys_address(const void *dtb) {
    // 首先尝试从 chosen 节点获取 stdout-path
    int chosen = fdt_path_offset(dtb, "/chosen");
    if (chosen >= 0) {
        const char *stdout_path = fdt_getprop(dtb, chosen, "stdout-path", NULL);
        if (stdout_path) {
            int uart_node = fdt_path_offset(dtb, stdout_path);
            if (uart_node >= 0) {
                int len;
                const fdt32_t *prop = fdt_getprop(dtb, uart_node, "reg", &len);
                if (prop && len >= 8) {
                    uint32_t addr_hi = fdt32_to_cpu(prop[0]);
                    uint32_t addr_lo = fdt32_to_cpu(prop[1]);
                    return ((uint64_t)addr_hi << 32) | addr_lo;
                }
            }
        }
    }

    // 如果没有找到，遍历设备树
    int root = fdt_path_offset(dtb, "/");
    if (root >= 0) {
        int node = fdt_first_subnode(dtb, root);
        while (node >= 0) {
            const char *compatible = fdt_getprop(dtb, node, "compatible", NULL);
            if (compatible && strstr(compatible, "arm,pl011")) {
                const char *status = fdt_getprop(dtb, node, "status", NULL);
                if (!status || strcmp(status, "okay") == 0) {
                    const char *secure_status = fdt_getprop(dtb, node, "secure-status", NULL);
                    if (!secure_status) {
                        int len;
                        const fdt32_t *prop = fdt_getprop(dtb, node, "reg", &len);
                        if (prop && len >= 8) {
                            uint32_t addr_hi = fdt32_to_cpu(prop[0]);
                            uint32_t addr_lo = fdt32_to_cpu(prop[1]);
                            return ((uint64_t)addr_hi << 32) | addr_lo;
                        }
                    }
                }
            }
            node = fdt_next_subnode(dtb, node);
        }
    }

    return 0; // 未找到
}
```

### 4.3 获取内存信息

```c
#include <libfdt.h>

void get_memory_info(const void *dtb) {
    int memory = fdt_path_offset(dtb, "/memory@40000000");
    if (memory < 0) {
        printk("Failed to find memory node: %d\n", memory);
        return;
    }

    int len;
    const fdt32_t *prop = fdt_getprop(dtb, memory, "reg", &len);
    if (prop) {
        for (int i = 0; i < len / (sizeof(fdt32_t) * 2); i++) {
            uint32_t addr_hi = fdt32_to_cpu(prop[i * 2]);
            uint32_t addr_lo = fdt32_to_cpu(prop[i * 2 + 1]);
            uint32_t size_hi = fdt32_to_cpu(prop[i * 2 + 2]);
            uint32_t size_lo = fdt32_to_cpu(prop[i * 2 + 3]);
            uint64_t addr = ((uint64_t)addr_hi << 32) | addr_lo;
            uint64_t size = ((uint64_t)size_hi << 32) | size_lo;
            printk("Memory: 0x%lx - 0x%lx (0x%lx bytes)\n", addr, addr + size - 1, size);
        }
    }
}
```

## 5. 注意事项

1. **设备树地址**：在裸机环境中，设备树通常加载到固定地址（如 0x40000000），需要根据实际情况调整。

2. **错误处理**：FDT 库函数返回负数表示错误，需要适当处理这些错误。

3. **内存访问**：设备树位于内存中，需要确保内存区域可访问。

4. **属性值格式**：不同属性的值格式可能不同，需要根据属性的定义正确解析。

5. **字符串处理**：设备树中的字符串通常以 null 结尾，但需要注意字符串的长度。

6. **大小端转换**：设备树中的数据使用大端格式，需要使用 `fdt32_to_cpu` 和 `fdt64_to_cpu` 转换为本地端序。

7. **节点路径**：节点路径使用 "/" 分隔，如 "/chosen"、"/memory@40000000"。

8. **属性名称**：属性名称是大小写敏感的，需要与设备树中的定义一致。

## 6. 常见问题

### 6.1 设备树地址不正确

**问题**：`fdt_check_header` 返回错误。
**解决方法**：确认设备树的加载地址是否正确，检查设备树文件是否损坏。

### 6.2 节点或属性不存在

**问题**：`fdt_path_offset` 或 `fdt_getprop` 返回错误。
**解决方法**：检查设备树中是否存在对应的节点或属性，确认路径和名称是否正确。

### 6.3 属性值解析错误

**问题**：属性值解析后不符合预期。
**解决方法**：确认属性值的格式，正确使用 `fdt32_to_cpu` 或 `fdt64_to_cpu` 进行转换。

## 7. 总结

FDT 库是一个强大的工具，用于解析和操作设备树，为裸机和操作系统提供硬件设备的配置信息。通过本文档介绍的函数和示例，您应该能够在 MyOS 中成功使用 FDT 库来获取硬件设备的信息，如 UART 物理地址、内存布局等。

在使用 FDT 库时，需要注意错误处理、内存访问、属性值解析等问题，确保代码的健壮性和可靠性。