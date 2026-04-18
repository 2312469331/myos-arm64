#include <ds/rbtree.h>
#include <slab.h>

static void rb_rotate_left(rb_tree_t *tree, rb_node_t *x) {
  rb_node_t *y = x->right;
  x->right = y->left;
  if (y->left != tree->nil) {
    y->left->parent = x;
  }
  y->parent = x->parent;
  if (x->parent == tree->nil) {
    tree->root = y;
  } else if (x == x->parent->left) {
    x->parent->left = y;
  } else {
    x->parent->right = y;
  }
  y->left = x;
  x->parent = y;
}

static void rb_rotate_right(rb_tree_t *tree, rb_node_t *y) {
  rb_node_t *x = y->left;
  y->left = x->right;
  if (x->right != tree->nil) {
    x->right->parent = y;
  }
  x->parent = y->parent;
  if (y->parent == tree->nil) {
    tree->root = x;
  } else if (y == y->parent->left) {
    y->parent->left = x;
  } else {
    y->parent->right = x;
  }
  x->right = y;
  y->parent = x;
}

static void rb_insert_fixup(rb_tree_t *tree, rb_node_t *z) {
  while (z->parent->color == RB_RED) {
    if (z->parent == z->parent->parent->left) {
      rb_node_t *y = z->parent->parent->right;
      if (y->color == RB_RED) {
        z->parent->color = RB_BLACK;
        y->color = RB_BLACK;
        z->parent->parent->color = RB_RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->right) {
          z = z->parent;
          rb_rotate_left(tree, z);
        }
        z->parent->color = RB_BLACK;
        z->parent->parent->color = RB_RED;
        rb_rotate_right(tree, z->parent->parent);
      }
    } else {
      rb_node_t *y = z->parent->parent->left;
      if (y->color == RB_RED) {
        z->parent->color = RB_BLACK;
        y->color = RB_BLACK;
        z->parent->parent->color = RB_RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->left) {
          z = z->parent;
          rb_rotate_right(tree, z);
        }
        z->parent->color = RB_BLACK;
        z->parent->parent->color = RB_RED;
        rb_rotate_left(tree, z->parent->parent);
      }
    }
  }
  tree->root->color = RB_BLACK;
}

static void rb_delete_fixup(rb_tree_t *tree, rb_node_t *x) {
  while (x != tree->root && x->color == RB_BLACK) {
    if (x == x->parent->left) {
      rb_node_t *w = x->parent->right;
      if (w->color == RB_RED) {
        w->color = RB_BLACK;
        x->parent->color = RB_RED;
        rb_rotate_left(tree, x->parent);
        w = x->parent->right;
      }
      if (w->left->color == RB_BLACK && w->right->color == RB_BLACK) {
        w->color = RB_RED;
        x = x->parent;
      } else {
        if (w->right->color == RB_BLACK) {
          w->left->color = RB_BLACK;
          w->color = RB_RED;
          rb_rotate_right(tree, w);
          w = x->parent->right;
        }
        w->color = x->parent->color;
        x->parent->color = RB_BLACK;
        w->right->color = RB_BLACK;
        rb_rotate_left(tree, x->parent);
        x = tree->root;
      }
    } else {
      rb_node_t *w = x->parent->left;
      if (w->color == RB_RED) {
        w->color = RB_BLACK;
        x->parent->color = RB_RED;
        rb_rotate_right(tree, x->parent);
        w = x->parent->left;
      }
      if (w->right->color == RB_BLACK && w->left->color == RB_BLACK) {
        w->color = RB_RED;
        x = x->parent;
      } else {
        if (w->left->color == RB_BLACK) {
          w->right->color = RB_BLACK;
          w->color = RB_RED;
          rb_rotate_left(tree, w);
          w = x->parent->left;
        }
        w->color = x->parent->color;
        x->parent->color = RB_BLACK;
        w->left->color = RB_BLACK;
        rb_rotate_right(tree, x->parent);
        x = tree->root;
      }
    }
  }
  x->color = RB_BLACK;
}

static rb_node_t *rb_tree_minimum(rb_tree_t *tree, rb_node_t *node) {
  while (node->left != tree->nil)
    node = node->left;
  return node;
}

// ======================================================================
//  简化接口：无 key/value，纯节点操作
// ======================================================================

void rb_tree_init(rb_tree_t *tree, int (*compare)(rb_node_t *a, rb_node_t *b)) {
  tree->nil = kmalloc(sizeof(rb_node_t));
  tree->nil->color = RB_BLACK;
  tree->nil->parent = tree->nil;
  tree->nil->left = tree->nil;
  tree->nil->right = tree->nil;

  tree->root = tree->nil;
  tree->compare = compare;
}

rb_node_t *rb_tree_search(rb_tree_t *tree, rb_node_t *key_node) {
  rb_node_t *node = tree->root;
  while (node != tree->nil) {
    int cmp = tree->compare(key_node, node);
    if (cmp == 0) {
      return node;
    } else if (cmp < 0) {
      node = node->left;
    } else {
      node = node->right;
    }
  }
  return NULL;
}

void rb_tree_insert(rb_tree_t *tree, rb_node_t *z) {
  rb_node_t *y = tree->nil;
  rb_node_t *x = tree->root;

  while (x != tree->nil) {
    y = x;
    if (tree->compare(z, x) < 0) {
      x = x->left;
    } else {
      x = x->right;
    }
  }

  z->parent = y;
  z->left = tree->nil;
  z->right = tree->nil;
  z->color = RB_RED;

  if (y == tree->nil) {
    tree->root = z;
  } else if (tree->compare(z, y) < 0) {
    y->left = z;
  } else {
    y->right = z;
  }

  rb_insert_fixup(tree, z);
}

void rb_tree_delete(rb_tree_t *tree, rb_node_t *z) {
  rb_node_t *y = z;
  rb_node_t *x;
  enum rb_color y_original_color = y->color;

  if (z->left == tree->nil) {
    x = z->right;
    if (z->parent == tree->nil) {
      tree->root = x;
    } else if (z == z->parent->left) {
      z->parent->left = x;
    } else {
      z->parent->right = x;
    }
    x->parent = z->parent;
  } else if (z->right == tree->nil) {
    x = z->left;
    if (z->parent == tree->nil) {
      tree->root = x;
    } else if (z == z->parent->left) {
      z->parent->left = x;
    } else {
      z->parent->right = x;
    }
    x->parent = z->parent;
  } else {
    y = rb_tree_minimum(tree, z->right);
    y_original_color = y->color;
    x = y->right;

    if (y->parent == z) {
      x->parent = y;
    } else {
      if (y->parent == tree->nil) {
        tree->root = x;
      } else if (y == y->parent->left) {
        y->parent->left = x;
      } else {
        y->parent->right = x;
      }
      x->parent = y->parent;
      y->right = z->right;
      y->right->parent = y;
    }

    if (z->parent == tree->nil) {
      tree->root = y;
    } else if (z == z->parent->left) {
      z->parent->left = y;
    } else {
      z->parent->right = y;
    }

    y->parent = z->parent;
    y->left = z->left;
    y->left->parent = y;
    y->color = z->color;
  }

  if (y_original_color == RB_BLACK) {
    rb_delete_fixup(tree, x);
  }
}

// 中序遍历
static void rb_tree_inorder_helper(rb_node_t *node, rb_tree_t *tree,
                                   void (*visit)(rb_node_t *, void *),
                                   void *data) {
  if (node != tree->nil) {
    rb_tree_inorder_helper(node->left, tree, visit, data);
    visit(node, data);
    rb_tree_inorder_helper(node->right, tree, visit, data);
  }
}

void rb_tree_inorder(rb_tree_t *tree, void (*visit)(rb_node_t *, void *),
                     void *data) {
  rb_tree_inorder_helper(tree->root, tree, visit, data);
}
