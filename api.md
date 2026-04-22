# API Reference

## 1. Data Types

### 1.1 Core Types
| Type | Definition | Description |
|------|------------|-------------|
| `u8` | `unsigned char` | 8-bit unsigned integer |
| `u16` | `unsigned short` | 16-bit unsigned integer |
| `u32` | `unsigned int` | 32-bit unsigned integer |
| `u64` | `unsigned long` | 64-bit unsigned integer |
| `size_t` | `unsigned long` | Size type |
| `phys_addr_t` | `unsigned long` | Physical address |

### 1.2 RBTree Types
```c
typedef struct rb_node {
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    enum rb_color color; // RB_RED, RB_BLACK
} rb_node_t;

typedef struct rb_tree {
    struct rb_node *root;
    struct rb_node *nil;
    int (*compare)(rb_node_t *a, rb_node_t *b);
} rb_tree_t;
```

### 1.3 List Types
```c
typedef struct list_head {
    struct list_head *next;
    struct list_head *prev;
} list_head_t;
```

### 1.4 VMap Types
```c
struct vmap_area {
    uint64_t va_start;       // 区间的起始虚拟地址 [包含]
    uint64_t va_end;         // 区间的结束虚拟地址 [不包含]
    unsigned long flags;     // 区间的属性标签
    struct list_head free_list;
    struct rb_node free_rb_node;
    struct list_node busy_list;
};

struct vmap_manager {
    uint64_t start;          // 池子起始地址
    uint64_t end;            // 池子结束地址
    struct rb_root free_tree;    // 空闲区间红黑树根节点
    struct list_head free_list;  // 空闲区间双向链表头
    struct list_head busy_list;  // 忙碌区间双向链表头
    spinlock_t lock;         // 并发控制自旋锁
    uint64_t total_free;     // 当前总空闲虚拟空间大小
    uint64_t nr_busy;       // 当前已分配的区间数量
};
```

### 1.5 Spinlock Types
```c
typedef struct {
    arch_spinlock_t raw_lock;
    unsigned int magic;
} spinlock_t;
```

### 1.6 Atomic Types
```c
typedef struct {
    volatile int counter;
} atomic_t;
```

### 1.7 Semaphore Types
```c
struct semaphore {
    atomic_t count;         // 计数器
    struct wait_queue_head wait; // 等待队列
};
```

### 1.8 Mutex Types
```c
struct mutex {
    atomic_t count;       // 1:空闲, 0:被占用, 负数:有等待者
    spinlock_t wait_lock; // 保护等待队列的自旋锁
    struct wait_queue_head wait_list;
    void *owner;          // 记录谁拿了锁，用于死锁检测
};
```

### 1.9 Seqlock Types
```c
typedef struct {
    unsigned int sequence;
    spinlock_t lock;
} seqlock_t;
```

### 1.10 Page Types
```c
struct page {
    u16 order;
    u16 private;
    u32 flags;           // PAGE_FREE, PAGE_ALLOCATED, PAGE_RESERVED
    struct list_head node;
    uint64_t ref_count;
    void *virtual_address;
};
```

---

## 2. Macros

### 2.1 Utility Macros
| Macro | Purpose |
|-------|---------|
| `offsetof(TYPE, MEMBER)` | Get offset of member in struct |
| `container_of(ptr, type, member)` | Convert member ptr to struct ptr |
| `barrier()` | Compiler memory barrier |
| `mb()` | Memory barrier |

### 2.2 RBTree Macros
| Macro | Purpose |
|-------|---------|
| `rb_entry(ptr, type, member)` | Get container struct from rb_node |

### 2.3 List Macros
| Macro | Purpose |
|-------|---------|
| `list_for_each(pos, head)` | Iterate over list |
| `list_for_each_entry(pos, head, member)` | Iterate and get container |

### 2.4 VMap Macros
| Macro | Value | Description |
|-------|-------|-------------|
| `VA_ALIGN_DEFAULT` | 0x1000 | 默认对齐要求 (4KB) |
| `VA_FLAG_VMALLOC` | 1<<0 | vmalloc 使用 |
| `VA_FLAG_VMAP` | 1<<1 | vmap 使用 (如内核栈) |
| `VA_FLAG_IOREMAP` | 1<<2 | ioremap 使用 |

### 2.5 Spinlock Macros
| Macro | Purpose |
|-------|---------|
| `SPIN_LOCK_UNLOCKED` | Static spinlock initializer |
| `spin_lock_init(lock)` | Dynamic spinlock initializer |
| `local_irq_disable()` | Disable local interrupts |
| `local_irq_enable()` | Enable local interrupts |
| `local_irq_save(flags)` | Save interrupt state and disable |
| `local_irq_restore(flags)` | Restore interrupt state |

### 2.6 Atomic Macros
| Macro | Purpose |
|-------|---------|
| `ATOMIC_INIT(value)` | Atomic variable initializer |
| `atomic_inc(v)` | Increment atomic variable |
| `atomic_dec(v)` | Decrement atomic variable |

### 2.7 Semaphore Macros
| Macro | Purpose |
|-------|---------|
| `__SEMAPHORE_INITIALIZER(name, count)` | Semaphore initializer |

### 2.8 Mutex Macros
| Macro | Purpose |
|-------|---------|
| `MUTEX_INITIALIZER(name)` | Mutex initializer |

### 2.9 Seqlock Macros
| Macro | Purpose |
|-------|---------|
| `SEQLOCK_UNLOCKED` | Seqlock initializer |

### 2.10 Memory Constants
| Macro | Value | Description |
|-------|-------|-------------|
| `PAGE_SIZE` | 4096 | Page size |
| `PAGE_SHIFT` | 12 | Page shift |
| `PAGE_MASK` | ~4095 | Page mask |
| `BUDDY_MEM_START` | 0x40288000 | Physical memory start |
| `BUDDY_MEM_END` | 0x4FFFFFFF | Physical memory end |
| `KERNEL_SPACE_START` | 0xffff800000000000 | Kernel VA start |

### 2.11 Permission Flags
| Flag | Value |
|------|-------|
| `PROT_READ` | 1<<0 |
| `PROT_WRITE` | 1<<1 |
| `PROT_EXEC` | 1<<2 |
| `MAP_ANONYMOUS` | 1<<3 |
| `MAP_FILE` | 1<<4 |

### 2.12 Slab Constants
| Macro | Value |
|-------|-------|
| `SZ_8K` | 8192 |
| `SZ_4M` | 4194304 |
| `SLAB_CACHE_COUNT` | 11 |
| `MAX_SLAB_PAGES` | 4096 |
| `MAX_LARGE_ALLOCS` | 2048 |

---

## 3. Functions

### 3.1 RBTree API
| Function | Signature |
|----------|-----------|
| rb_node_init | `void rb_node_init(rb_node_t *node)` |
| rb_tree_init | `void rb_tree_init(rb_tree_t *tree, int (*compare)(rb_node_t*, rb_node_t*))` |
| rb_tree_search | `rb_node_t *rb_tree_search(rb_tree_t *tree, rb_node_t *key_node)` |
| rb_tree_min | `rb_node_t *rb_tree_min(rb_tree_t *tree, rb_node_t *subtree)` |
| rb_tree_max | `rb_node_t *rb_tree_max(rb_tree_t *tree, rb_node_t *subtree)` |
| rb_tree_next | `rb_node_t *rb_tree_next(rb_node_t *node)` |
| rb_tree_prev | `rb_node_t *rb_tree_prev(rb_node_t *node)` |
| rb_tree_insert | `void rb_tree_insert(rb_tree_t *tree, rb_node_t *node)` |
| rb_tree_delete | `void rb_tree_delete(rb_tree_t *tree, rb_node_t *node)` |
| rb_tree_inorder | `void rb_tree_inorder(rb_tree_t *tree, void (*visit)(rb_node_t*, void*), void*)` |

### 3.2 List API
| Function | Signature |
|----------|-----------|
| INIT_LIST_HEAD | `void INIT_LIST_HEAD(struct list_head *list)` |
| list_empty | `int list_empty(const struct list_head *head)` |
| list_add | `void list_add(struct list_head *node, struct list_head *head)` |
| list_add_tail | `void list_add_tail(struct list_head *node, struct list_head *head)` |
| list_del | `void list_del(struct list_head *entry)` |

### 3.3 VMap API
| Function | Signature |
|----------|-----------|
| va_manager_init | `void va_manager_init(void)` |
| va_alloc | `void *va_alloc(unsigned long size, unsigned long align, unsigned long flags)` |
| va_free | `void va_free(void *addr)` |

### 3.4 Spinlock API
| Function | Signature |
|----------|-----------|
| spin_lock | `void spin_lock(spinlock_t *lock)` |
| spin_unlock | `void spin_unlock(spinlock_t *lock)` |
| spin_lock_irqsave | `unsigned long spin_lock_irqsave(spinlock_t *lock)` |
| spin_unlock_irqrestore | `void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)` |

### 3.5 Atomic API
| Function | Signature |
|----------|-----------|
| atomic_add | `void atomic_add(int i, atomic_t *v)` |
| atomic_sub | `void atomic_sub(int i, atomic_t *v)` |
| atomic_read | `int atomic_read(const atomic_t *v)` |
| atomic_set | `void atomic_set(atomic_t *v, int i)` |
| atomic_inc | `void atomic_inc(atomic_t *v)` |
| atomic_dec | `void atomic_dec(atomic_t *v)` |

### 3.6 Semaphore API
| Function | Signature |
|----------|-----------|
| down | `void down(struct semaphore *sem)` |
| up | `void up(struct semaphore *sem)` |

### 3.7 Mutex API
| Function | Signature |
|----------|-----------|
| mutex_lock | `void mutex_lock(struct mutex *mtx)` |
| mutex_unlock | `void mutex_unlock(struct mutex *mtx)` |

### 3.8 Seqlock API
| Function | Signature |
|----------|-----------|
| write_seqlock | `void write_seqlock(seqlock_t *sl)` |
| write_sequnlock | `void write_sequnlock(seqlock_t *sl)` |
| read_seqbegin | `unsigned int read_seqbegin(const seqlock_t *sl)` |
| read_seqretry | `int read_seqretry(const seqllock_t *sl, unsigned int seq)` |

### 3.9 Slab API
| Function | Signature |
|----------|-----------|
| slab_init | `void slab_init(void)` |
| kmalloc | `void *kmalloc(size_t size)` |
| kfree | `void kfree(void *va)` |

### 3.10 Buddy API
| Function | Signature |
|----------|-----------|
| buddy_init | `void buddy_init(void)` |
| alloc_phys_pages | `phys_addr_t alloc_phys_pages(unsigned int order)` |
| free_phys_pages | `void free_phys_pages(phys_addr_t pa, unsigned int order)` |
| buddy_nr_free_blocks | `unsigned int buddy_nr_free_blocks(unsigned int order)` |
| buddy_nr_free_pages_total | `unsigned int buddy_nr_free_pages_total(void)` |
| buddy_get_last_error | `enum buddy_error buddy_get_last_error(void)` |

### 3.11 IO API
| Function | Signature |
|----------|-----------|
| io_read8 | `uint8_t io_read8(const volatile void *addr)` |
| io_read16 | `uint16_t io_read16(const volatile void *addr)` |
| io_read32 | `uint32_t io_read32(const volatile void *addr)` |
| io_write8 | `void io_write8(volatile void *addr, uint8_t val)` |
| io_write16 | `void io_write16(volatile void *addr, uint16_t val)` |
| io_write32 | `void io_write32(volatile void *addr, uint32_t val)` |
| io_write32_log | `void io_write32_log(volatile void *addr, uint32_t val, const char *name)` |

---

## 4. Globals

### 4.1 Slab Globals
| Variable | Type | Purpose |
|----------|------|---------|
| `slab_linear_map_base` | `uintptr_t` | Linear map base (VA = base + PA) |
| `slab_l0_table_pa` | `phys_addr_t` | L0 page table physical address |

### 4.2 Memory Globals
| Variable | Type | Purpose |
|----------|------|---------|
| `init_mm` | `struct mm_struct` | Kernel address space |

---

## 5. Error Codes (Buddy)

| Code | Value | Description |
|------|-------|-------------|
| `BUDDY_OK` | 0 | Success |
| `BUDDY_ERR_BAD_ORDER` | 1 | Invalid order |
| `BUDDY_ERR_OUT_OF_RANGE` | 2 | PA out of range |
| `BUDDY_ERR_UNALIGNED` | 3 | Not page aligned |
| `BUDDY_ERR_DOUBLE_FREE` | 4 | Double free |
| `BUDDY_ERR_NOT_ALLOCATED` | 5 | Not allocated |
| `BUDDY_ERR_NO_MEMORY` | 6 | Out of memory |

---

## 6. Usage Notes

1. **Slab Initialization**: Set `slab_linear_map_base` and `slab_l0_table_pa` before calling `slab_init()`
2. **Buddy Order**: Valid range is 0-10 (1 page to 1024 pages)
3. **RBTree Compare**: Must provide comparison function during tree init
4. **Container Access**: Use `rb_entry`/`list_for_each_entry` with `container_of`
5. **Spinlock Usage**: Use `spin_lock_irqsave`/`spin_unlock_irqrestore` for interrupt safety
6. **Seqlock Usage**: Readers may need to retry if write occurs during read
7. **IO Access**: Always use `mb()` after writes to ensure completion
8. **VMap Allocation**: va_alloc uses best-fit algorithm, va_free merges adjacent free areas