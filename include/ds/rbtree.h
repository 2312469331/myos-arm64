#ifndef __RBTREE_H__
#define __RBTREE_H__

#include <types.h>
#include <utils.h> // 共用 container_of / offsetof

enum rb_color { RB_RED, RB_BLACK };

typedef struct rb_node {
  struct rb_node *parent;
  struct rb_node *left;
  struct rb_node *right;
  enum rb_color color;
} rb_node_t;

typedef struct rb_tree {
  struct rb_node *root;
  struct rb_node *nil;
  int (*compare)(rb_node_t *a, rb_node_t *b);
} rb_tree_t;

// -----------------------------------------------------------------------------
// 1. 节点 & 树初始化
// -----------------------------------------------------------------------------
void rb_node_init(rb_node_t *node);
void rb_tree_init(rb_tree_t *tree, int (*compare)(rb_node_t *a, rb_node_t *b));

// -----------------------------------------------------------------------------
// 2. 查找
// -----------------------------------------------------------------------------
rb_node_t *rb_tree_search(rb_tree_t *tree, rb_node_t *key_node);
rb_node_t *rb_tree_min(rb_tree_t *tree, rb_node_t *subtree);
rb_node_t *rb_tree_max(rb_tree_t *tree, rb_node_t *subtree);

// -----------------------------------------------------------------------------
// 3. 前驱 / 后继（通用迭代必备）
// -----------------------------------------------------------------------------
rb_node_t *rb_tree_next(rb_node_t *node);
rb_node_t *rb_tree_prev(rb_node_t *node);

// -----------------------------------------------------------------------------
// 4. 插入 / 删除
// -----------------------------------------------------------------------------
void rb_tree_insert(rb_tree_t *tree, rb_node_t *node);
void rb_tree_delete(rb_tree_t *tree, rb_node_t *node);

// -----------------------------------------------------------------------------
// 5. 遍历
// -----------------------------------------------------------------------------
void rb_tree_inorder(rb_tree_t *tree,
                     void (*visit)(rb_node_t *node, void *data), void *data);

// -----------------------------------------------------------------------------
// 6. 通用辅助宏（配套 container_of，同 list 风格）
// -----------------------------------------------------------------------------
#define rb_entry(ptr, type, member) container_of(ptr, type, member)

#endif
