#ifndef __LIST_H__
#define __LIST_H__

#include <types.h>

// 链表节点结构
typedef struct list_head {
    struct list_head *next;
    struct list_head *prev;
} list_head_t;

// 初始化链表头
static inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

// 静态编译期初始化链表头
#define LIST_HEAD(name) \
    struct list_head name = { .next = &name, .prev = &name }

// 检查链表是否为空
static inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

// 内部添加节点函数
static inline void __list_add(struct list_head *node, struct list_head *prev, struct list_head *next) {
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

// 在链表头后添加节点
static inline void list_add(struct list_head *node, struct list_head *head) {
    __list_add(node, head, head->next);
}

// 在链表尾前添加节点
static inline void list_add_tail(struct list_head *node, struct list_head *head) {
    __list_add(node, head->prev, head);
}

// 内部删除节点函数
static inline void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    prev->next = next;
}

// 删除节点
static inline void list_del(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    entry->next = entry;
    entry->prev = entry;
}

// 容器_of 宏，用于从链表节点获取包含它的结构体
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t)&(((TYPE *)0)->MEMBER))
#endif
// 1. 核心：container_of —— 通过成员指针找到整个结构体指针
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

// 2. list_entry 就是 container_of 的别名（内核原生定义）
// 作用：通过链表节点 free_list，找到它所属的 struct vmap_area 结构体
#define list_entry(ptr, type, member) container_of(ptr, type, member)
// 遍历链表
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

// 遍历链表并获取包含节点的结构体
#define list_for_each_entry(pos, head, member) \
    for (pos = container_of((head)->next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = container_of(pos->member.next, typeof(*pos), member))

#endif /* __LIST_H__ */
