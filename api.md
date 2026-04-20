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

### 1.4 Spinlock Types
```c
typedef struct spinlock {
    volatile int locked;
} spinlock_t;
```

### 1.5 Page Types
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

### 2.2 RBTree Macros
| Macro | Purpose |
|-------|---------|
| `rb_entry(ptr, type, member)` | Get container struct from rb_node |

### 2.3 List Macros
| Macro | Purpose |
|-------|---------|
| `list_for_each(pos, head)` | Iterate over list |
| `list_for_each_entry(pos, head, member)` | Iterate and get container |

### 2.4 Memory Constants
| Macro | Value | Description |
|-------|-------|-------------|
| `PAGE_SIZE` | 4096 | Page size |
| `PAGE_SHIFT` | 12 | Page shift |
| `PAGE_MASK` | ~4095 | Page mask |
| `BUDDY_MEM_START` | 0x40288000 | Physical memory start |
| `BUDDY_MEM_END` | 0x4FFFFFFF | Physical memory end |
| `KERNEL_SPACE_START` | 0xffff800000000000 | Kernel VA start |

### 2.5 Permission Flags
| Flag | Value |
|------|-------|
| `PROT_READ` | 1<<0 |
| `PROT_WRITE` | 1<<1 |
| `PROT_EXEC` | 1<<2 |
| `MAP_ANONYMOUS` | 1<<3 |
| `MAP_FILE` | 1<<4 |

### 2.6 Slab Constants
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

### 3.3 Spinlock API
| Function | Signature |
|----------|-----------|
| spin_lock | `void spin_lock(spinlock_t *lock)` |
| spin_trylock | `int spin_trylock(spinlock_t *lock)` |
| spin_unlock | `void spin_unlock(spinlock_t *lock)` |
| spin_is_locked | `int spin_is_locked(spinlock_t *lock)` |

### 3.4 Slab API
| Function | Signature |
|----------|-----------|
| slab_init | `void slab_init(void)` |
| kmalloc | `void *kmalloc(size_t size)` |
| kfree | `void kfree(void *va)` |

### 3.5 Buddy API
| Function | Signature |
|----------|-----------|
| buddy_init | `void buddy_init(void)` |
| alloc_phys_pages | `phys_addr_t alloc_phys_pages(unsigned int order)` |
| free_phys_pages | `void free_phys_pages(phys_addr_t pa, unsigned int order)` |
| buddy_nr_free_blocks | `unsigned int buddy_nr_free_blocks(unsigned int order)` |
| buddy_nr_free_pages_total | `unsigned int buddy_nr_free_pages_total(void)` |
| buddy_get_last_error | `enum buddy_error buddy_get_last_error(void)` |

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