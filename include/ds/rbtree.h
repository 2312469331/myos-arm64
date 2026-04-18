#ifndef __RBTREE_H__
#define __RBTREE_H__

#include <types.h>

enum rb_color { RB_RED, RB_BLACK };

// 纯链接节点，无数据
typedef struct rb_node {
  struct rb_node *parent;
  struct rb_node *left;
  struct rb_node *right;
  enum rb_color color;
} rb_node_t;

typedef struct rb_tree {
  struct rb_node *root;
  struct rb_node *nil;
  // 比较函数：直接比较两个 rb_node 所在结构体的 key
  int (*compare)(rb_node_t *a, rb_node_t *b);
} rb_tree_t;

// 初始化
void rb_tree_init(rb_tree_t *tree, int (*compare)(rb_node_t *a, rb_node_t *b));

// 查找：根据比较函数查找
rb_node_t *rb_tree_search(rb_tree_t *tree, rb_node_t *key_node);

// 插入：直接插节点
void rb_tree_insert(rb_tree_t *tree, rb_node_t *node);

// 删除：直接删节点
void rb_tree_delete(rb_tree_t *tree, rb_node_t *node);

// 遍历
void rb_tree_inorder(rb_tree_t *tree,
                     void (*visit)(rb_node_t *node, void *data), void *data);

#endif
