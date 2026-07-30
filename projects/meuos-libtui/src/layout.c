/* layout.c — 布局树系统
 *
 * 提供可组合的 vbox/hbox 布局，支持权重分配、内边距和递归渲染。
 * 纯 C11 + POSIX 实现，零外部依赖。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdlib.h>
#include <string.h>

/* ── 布局节点类型 ─────────────────────────────────── */

enum tui_layout_kind {
    LAYOUT_VBOX,    /* 垂直排列子节点 */
    LAYOUT_HBOX,    /* 水平排列子节点 */
    LAYOUT_LEAF,    /* 叶子节点（实际内容） */
};

/* ── 子节点链表 ───────────────────────────────────── */

typedef struct tui_layout_child {
    struct tui_layout      *node;
    int                     weight;    /* 0 = 填满剩余空间 */
    struct tui_layout_child *next;
} tui_layout_child_t;

/* ── 布局节点结构 ─────────────────────────────────── */

struct tui_layout {
    enum tui_layout_kind  kind;
    tui_layout_child_t   *children;
    int                   spacing;   /* 子节点间距 */
    int                   pad_top, pad_right, pad_bottom, pad_left;

    /* 叶子节点专有 */
    tui_render_fn         render_fn;
    void                 *userdata;
};

/* ══════════════════════════════════════════════════════
 *  创建 / 销毁
 * ══════════════════════════════════════════════════════ */

tui_layout_t *tui_layout_vbox(int spacing)
{
    tui_layout_t *l = (tui_layout_t *)calloc(1, sizeof(tui_layout_t));
    if (!l) return NULL;
    l->kind    = LAYOUT_VBOX;
    l->spacing = spacing;
    return l;
}

tui_layout_t *tui_layout_hbox(int spacing)
{
    tui_layout_t *l = (tui_layout_t *)calloc(1, sizeof(tui_layout_t));
    if (!l) return NULL;
    l->kind    = LAYOUT_HBOX;
    l->spacing = spacing;
    return l;
}

tui_layout_t *tui_layout_leaf(tui_render_fn fn, void *userdata)
{
    tui_layout_t *l = (tui_layout_t *)calloc(1, sizeof(tui_layout_t));
    if (!l) return NULL;
    l->kind      = LAYOUT_LEAF;
    l->render_fn = fn;
    l->userdata  = userdata;
    return l;
}

/* ── 递归释放 ─────────────────────────────────────── */

void tui_layout_free(tui_layout_t *layout)
{
    if (!layout) return;

    tui_layout_child_t *c = layout->children;
    while (c) {
        tui_layout_child_t *next = c->next;
        tui_layout_free(c->node);
        free(c);
        c = next;
    }
    free(layout);
}

/* ══════════════════════════════════════════════════════
 *  节点操作
 * ══════════════════════════════════════════════════════ */

int tui_layout_add(tui_layout_t *parent, tui_layout_t *child, int weight)
{
    if (!parent || !child) return TUI_ERR_PARAM;
    if (parent->kind == LAYOUT_LEAF) return TUI_ERR_PARAM;

    tui_layout_child_t *c = (tui_layout_child_t *)calloc(1, sizeof(tui_layout_child_t));
    if (!c) return TUI_ERR_MEM;

    c->node   = child;
    c->weight = weight < 0 ? 0 : weight;
    c->next   = NULL;

    /* 追加到链表末尾 */
    if (!parent->children) {
        parent->children = c;
    } else {
        tui_layout_child_t *last = parent->children;
        while (last->next) last = last->next;
        last->next = c;
    }

    return TUI_OK;
}

void tui_layout_pad(tui_layout_t *node, int t, int r, int b, int l)
{
    if (!node) return;
    node->pad_top    = t > 0 ? t : 0;
    node->pad_right  = r > 0 ? r : 0;
    node->pad_bottom = b > 0 ? b : 0;
    node->pad_left   = l > 0 ? l : 0;
}

/* ── 递归检查 ─────────────────────────────────────── */

int tui_layout_valid(const tui_layout_t *layout)
{
    if (!layout) return 0;
    if (layout->kind == LAYOUT_LEAF)
        return layout->render_fn != NULL;
    /* 容器节点必须有子节点 */
    return layout->children != NULL;
}

/* ══════════════════════════════════════════════════════
 *  渲染 — 核心递归
 * ══════════════════════════════════════════════════════ */

/* 内部：应用内边距后返回内容区域 */
static tui_rect_t apply_padding(const tui_rect_t *area,
                                int pt, int pr, int pb, int pl)
{
    tui_rect_t r = *area;
    if (r.rows > pt + pb) { r.row += pt; r.rows -= pt + pb; }
    if (r.cols > pl + pr) { r.col += pl; r.cols -= pl + pr; }
    return r;
}

/* 内部：容器渲染 */
static int render_container(int fd, tui_layout_t *node, tui_rect_t area)
{
    if (!node || !tui_rect_valid(&area)) return TUI_ERR_PARAM;

    /* 应用内边距 */
    tui_rect_t inner = apply_padding(&area,
        node->pad_top, node->pad_right,
        node->pad_bottom, node->pad_left);

    if (!tui_rect_valid(&inner)) return TUI_OK;  /* 无内容空间，静默返回 */

    /* 计算子节点数量 */
    int n = 0;
    tui_layout_child_t *c;
    for (c = node->children; c; c = c->next) n++;
    if (n == 0) return TUI_OK;

    /* 计算总权重和固定尺寸 */
    int total_weight = 0;
    int fixed_count = 0;
    int spacing_total = (n - 1) * node->spacing;

    for (c = node->children; c; c = c->next) {
        if (c->weight > 0) {
            total_weight += c->weight;
        } else {
            fixed_count++;
        }
    }

    int is_vertical = (node->kind == LAYOUT_VBOX);
    int total_size  = is_vertical ? inner.rows : inner.cols;
    int avail       = total_size - spacing_total;

    /* 每个固定权重子节点至少 1 行/列 */
    if (avail < fixed_count) avail = fixed_count;

    /* 分配位置 */
    int pos = is_vertical ? inner.row : inner.col;

    for (c = node->children; c; c = c->next) {
        tui_rect_t child_area = inner;
        int child_size;

        if (c->weight > 0 && total_weight > 0) {
            child_size = (avail * c->weight) / total_weight;
        } else {
            /* weight==0 的节点均分剩余空间 */
            child_size = fixed_count > 0 ? avail / fixed_count : 0;
            if (child_size < 1) child_size = 1;
        }

        /* 限制最小值 */
        if (child_size < 1) child_size = 1;

        if (is_vertical) {
            child_area.row    = pos;
            child_area.rows   = child_size;
            child_area.cols   = inner.cols;
        } else {
            child_area.col    = pos;
            child_area.cols   = child_size;
            child_area.rows   = inner.rows;
        }

        /* 渲染子节点 */
        if (c->node->kind == LAYOUT_LEAF) {
            if (c->node->render_fn && tui_rect_valid(&child_area))
                c->node->render_fn(fd, &child_area, c->node->userdata);
        } else {
            if (tui_rect_valid(&child_area))
                render_container(fd, c->node, child_area);
        }

        pos += child_size + node->spacing;
    }

    return TUI_OK;
}

/* ── 外部入口 ─────────────────────────────────────── */

int tui_layout_render(int fd, tui_layout_t *root, tui_rect_t area)
{
    if (!root) return TUI_ERR_PARAM;
    if (!tui_rect_valid(&area)) return TUI_ERR_PARAM;

    if (root->kind == LAYOUT_LEAF) {
        if (root->render_fn)
            return root->render_fn(fd, &area, root->userdata);
        return TUI_OK;
    }

    return render_container(fd, root, area);
}
