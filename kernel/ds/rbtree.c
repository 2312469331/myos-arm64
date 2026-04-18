#include <ds/rbtree.h>
#include <slab.h>   // 内核内存分配

#define RB_NIL(tree) ((tree)->nil)

static void rb_left_rotate(rb_tree_t *tree, rb_node_t *x);
static void rb_right_rotate(rb_tree_t *tree, rb_node_t *y);
static void rb_insert_fixup(rb_tree_t *tree, rb_node_t *z);
static void rb_delete_fixup(rb_tree_t *tree, rb_node_t *x);
static void rb_transplant(rb_tree_t *tree, rb_node_t *u, rb_node_t *v);

// -----------------------------------------------------------------------------
// 节点初始化
// -----------------------------------------------------------------------------
void rb_node_init(rb_node_t *node)
{
    // 初始化时还不知道 tree，先置空，插入时会被正确设置
    node->parent = NULL;
    node->left   = NULL;
    node->right  = NULL;
    node->color  = RB_RED;
}

// -----------------------------------------------------------------------------
// 红黑树初始化（内核版：kmalloc）
// -----------------------------------------------------------------------------
void rb_tree_init(rb_tree_t *tree, int (*compare)(rb_node_t *a, rb_node_t *b))
{
    tree->nil = kmalloc(sizeof(rb_node_t));
    tree->nil->parent = tree->nil;
    tree->nil->left   = tree->nil;
    tree->nil->right  = tree->nil;
    tree->nil->color  = RB_BLACK;

    tree->root = tree->nil;
    tree->compare = compare;
}

// -----------------------------------------------------------------------------
// 最小值 / 最大值
// -----------------------------------------------------------------------------
rb_node_t *rb_tree_min(rb_tree_t *tree, rb_node_t *subtree)
{
    while (subtree->left != RB_NIL(tree))
        subtree = subtree->left;
    return subtree;
}

rb_node_t *rb_tree_max(rb_tree_t *tree, rb_node_t *subtree)
{
    while (subtree->right != RB_NIL(tree))
        subtree = subtree->right;
    return subtree;
}

// -----------------------------------------------------------------------------
// 后继 / 前驱
// -----------------------------------------------------------------------------
rb_node_t *rb_tree_next(rb_node_t *node)
{
    // 从 node 反推 rb_tree 结构（内核标准做法）
    rb_node_t *nil_node = node->parent;
    while (nil_node->parent != nil_node)
        nil_node = nil_node->parent;

    rb_tree_t *tree = container_of(nil_node, rb_tree_t, nil);

    if (node->right != RB_NIL(tree))
        return rb_tree_min(tree, node->right);

    rb_node_t *p = node->parent;
    while (p != RB_NIL(tree) && node == p->right) {
        node = p;
        p = p->parent;
    }
    return p;
}

rb_node_t *rb_tree_prev(rb_node_t *node)
{
    rb_node_t *nil_node = node->parent;
    while (nil_node->parent != nil_node)
        nil_node = nil_node->parent;

    rb_tree_t *tree = container_of(nil_node, rb_tree_t, nil);

    if (node->left != RB_NIL(tree))
        return rb_tree_max(tree, node->left);

    rb_node_t *p = node->parent;
    while (p != RB_NIL(tree) && node == p->left) {
        node = p;
        p = p->parent;
    }
    return p;
}

// -----------------------------------------------------------------------------
// 查找（精确匹配）
// -----------------------------------------------------------------------------
rb_node_t *rb_tree_search(rb_tree_t *tree, rb_node_t *key_node)
{
    rb_node_t *cur = tree->root;
    while (cur != RB_NIL(tree)) {
        int cmp = tree->compare(key_node, cur);
        if (cmp == 0)
            return cur;
        else if (cmp < 0)
            cur = cur->left;
        else
            cur = cur->right;
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// 插入
// -----------------------------------------------------------------------------
void rb_tree_insert(rb_tree_t *tree, rb_node_t *z)
{
    rb_node_t *y = RB_NIL(tree);
    rb_node_t *x = tree->root;

    while (x != RB_NIL(tree)) {
        y = x;
        if (tree->compare(z, x) < 0)
            x = x->left;
        else
            x = x->right;
    }

    z->parent = y;
    if (y == RB_NIL(tree))
        tree->root = z;
    else if (tree->compare(z, y) < 0)
        y->left = z;
    else
        y->right = z;

    z->left  = RB_NIL(tree);
    z->right = RB_NIL(tree);
    z->color = RB_RED;

    rb_insert_fixup(tree, z);
}

// -----------------------------------------------------------------------------
// 删除
// -----------------------------------------------------------------------------
void rb_tree_delete(rb_tree_t *tree, rb_node_t *z)
{
    rb_node_t *y = z;
    rb_node_t *x;
    enum rb_color y_original_color = y->color;

    if (z->left == RB_NIL(tree)) {
        x = z->right;
        rb_transplant(tree, z, z->right);
    } else if (z->right == RB_NIL(tree)) {
        x = z->left;
        rb_transplant(tree, z, z->left);
    } else {
        y = rb_tree_min(tree, z->right);
        y_original_color = y->color;
        x = y->right;

        if (y->parent == z) {
            x->parent = y;
        } else {
            rb_transplant(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }

        rb_transplant(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    if (y_original_color == RB_BLACK)
        rb_delete_fixup(tree, x);
}

// -----------------------------------------------------------------------------
// 中序遍历
// -----------------------------------------------------------------------------
static void __inorder(rb_tree_t *tree, rb_node_t *node,
                      void (*visit)(rb_node_t *, void *), void *data)
{
    if (node != RB_NIL(tree)) {
        __inorder(tree, node->left, visit, data);
        visit(node, data);
        __inorder(tree, node->right, visit, data);
    }
}

void rb_tree_inorder(rb_tree_t *tree,
                     void (*visit)(rb_node_t *node, void *data), void *data)
{
    __inorder(tree, tree->root, visit, data);
}

// -----------------------------------------------------------------------------
// 内部：旋转 & 修复
// -----------------------------------------------------------------------------
static void rb_left_rotate(rb_tree_t *tree, rb_node_t *x)
{
    rb_node_t *y = x->right;
    x->right = y->left;

    if (y->left != RB_NIL(tree))
        y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == RB_NIL(tree))
        tree->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;

    y->left = x;
    x->parent = y;
}

static void rb_right_rotate(rb_tree_t *tree, rb_node_t *y)
{
    rb_node_t *x = y->left;
    y->left = x->right;

    if (x->right != RB_NIL(tree))
        x->right->parent = y;

    x->parent = y->parent;

    if (y->parent == RB_NIL(tree))
        tree->root = x;
    else if (y == y->parent->left)
        y->parent->left = x;
    else
        y->parent->right = x;

    x->right = y;
    y->parent = x;
}

static void rb_insert_fixup(rb_tree_t *tree, rb_node_t *z)
{
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
                    rb_left_rotate(tree, z);
                }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rb_right_rotate(tree, z->parent->parent);
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
                    rb_right_rotate(tree, z);
                }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rb_left_rotate(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = RB_BLACK;
}

static void rb_transplant(rb_tree_t *tree, rb_node_t *u, rb_node_t *v)
{
    if (u->parent == RB_NIL(tree))
        tree->root = v;
    else if (u == u->parent->left)
        u->parent->left = v;
    else
        u->parent->right = v;
    v->parent = u->parent;
}

static void rb_delete_fixup(rb_tree_t *tree, rb_node_t *x)
{
    while (x != tree->root && x->color == RB_BLACK) {
        if (x == x->parent->left) {
            rb_node_t *w = x->parent->right;

            if (w->color == RB_RED) {
                w->color = RB_BLACK;
                x->parent->color = RB_RED;
                rb_left_rotate(tree, x->parent);
                w = x->parent->right;
            }

            if (w->left->color == RB_BLACK && w->right->color == RB_BLACK) {
                w->color = RB_RED;
                x = x->parent;
            } else {
                if (w->right->color == RB_BLACK) {
                    w->left->color = RB_BLACK;
                    w->color = RB_RED;
                    rb_right_rotate(tree, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = RB_BLACK;
                w->right->color = RB_BLACK;
                rb_left_rotate(tree, x->parent);
                x = tree->root;
            }
        } else {
            rb_node_t *w = x->parent->left;

            if (w->color == RB_RED) {
                w->color = RB_BLACK;
                x->parent->color = RB_RED;
                rb_right_rotate(tree, x->parent);
                w = x->parent->left;
            }

            if (w->right->color == RB_BLACK && w->left->color == RB_BLACK) {
                w->color = RB_RED;
                x = x->parent;
            } else {
                if (w->left->color == RB_BLACK) {
                    w->right->color = RB_BLACK;
                    w->color = RB_RED;
                    rb_left_rotate(tree, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = RB_BLACK;
                w->left->color = RB_BLACK;
                rb_right_rotate(tree, x->parent);
                x = tree->root;
            }
        }
    }
    x->color = RB_BLACK;
}