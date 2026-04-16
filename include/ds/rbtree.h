#ifndef __RBTREE_H__
#define __RBTREE_H__

#include <types.h>

// 红黑树节点颜色
enum rb_color {
    RB_RED,
    RB_BLACK
};

// 红黑树节点结构
typedef struct rb_node {
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    enum rb_color color;
    void *key;
    void *value;
} rb_node_t;

// 红黑树结构
typedef struct rb_tree {
    struct rb_node *root;
    struct rb_node *nil;
    int (*compare)(const void *key1, const void *key2);
} rb_tree_t;

// 初始化红黑树
void rb_tree_init(rb_tree_t *tree, int (*compare)(const void *key1, const void *key2));

// 查找节点
rb_node_t *rb_tree_search(rb_tree_t *tree, const void *key);

// 插入节点
int rb_tree_insert(rb_tree_t *tree, void *key, void *value);

// 删除节点
int rb_tree_delete(rb_tree_t *tree, const void *key);

// 遍历节点（中序遍历）
void rb_tree_inorder(rb_tree_t *tree, void (*visit)(rb_node_t *node, void *data), void *data);

#endif /* __RBTREE_H__ */
