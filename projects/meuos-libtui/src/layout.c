/* layout.c — 布局树系统（v2，flex 布局）
 *
 * 提供 CSS-like flex 布局：
 *   - tui_layout_flex(direction, gap)            主轴方向 + 子项间距
 *   - tui_layout_justify(node, TUI_JUSTIFY_*)   主轴对齐
 *   - tui_layout_align(node, TUI_ALIGN_*)       交叉轴对齐
 *   - tui_layout_add_flex(parent, child, grow, basis)  子项 grow/basis
 *
 * 兼容 v1 vbox/hbox/weight API。
 * 纯 C11 + POSIX 实现，零外部依赖。
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── 布局节点类型 ─────────────────────────────────── */

enum tui_layout_kind {
    LAYOUT_VBOX,    /* 垂直排列子节点 (legacy) */
    LAYOUT_HBOX,    /* 水平排列子节点 (legacy) */
    LAYOUT_FLEX,    /* 通用 flex 容器 */
    LAYOUT_LEAF,    /* 叶子节点（实际内容） */
};

/* ── 子节点链表 ───────────────────────────────────── */

typedef struct tui_layout_child {
    struct tui_layout      *node;
    int                     weight;       /* legacy vbox/hbox 权重 */
    double                  grow;         /* flex grow (>=0) */
    int                     basis;        /* flex basis (>0=固定像素) */
    tui_align_t             align;        /* 单项交叉轴对齐 (-1=继承) */
    struct tui_layout_child *next;
} tui_layout_child_t;

/* ── 布局节点结构 ─────────────────────────────────── */

struct tui_layout {
    enum tui_layout_kind  kind;
    tui_layout_child_t   *children;
    int                   spacing;          /* 子节点间距（legacy） */
    int                   gap;              /* flex gap */
    tui_justify_t         justify;          /* 主轴对齐 */
    tui_align_t           align;            /* 交叉轴对齐 */
    tui_flex_dir_t        flex_dir;         /* FLEX 时主轴方向 */
    int                   pad_top, pad_right, pad_bottom, pad_left;

    /* 叶子节点专有 */
    tui_render_fn         render_fn;
    void                 *userdata;
    void                (*userdata_free)(void *);  /* 释放 userdata */
};

/* ══════════════════════════════════════════════════════
 *  创建 / 销毁
 * ══════════════════════════════════════════════════════ */

tui_layout_t *tui_layout_vbox(int spacing)
{
    tui_layout_t *l = (tui_layout_t *)calloc(1, sizeof(tui_layout_t));
    if (!l) return NULL;
    l->kind      = LAYOUT_VBOX;
    l->spacing   = spacing;
    l->justify   = TUI_JUSTIFY_START;
    l->align     = TUI_ALIGN_START;
    return l;
}

tui_layout_t *tui_layout_hbox(int spacing)
{
    tui_layout_t *l = (tui_layout_t *)calloc(1, sizeof(tui_layout_t));
    if (!l) return NULL;
    l->kind      = LAYOUT_HBOX;
    l->spacing   = spacing;
    l->justify   = TUI_JUSTIFY_START;
    l->align     = TUI_ALIGN_STRETCH;
    return l;
}

tui_layout_t *tui_layout_flex(tui_flex_dir_t dir, int gap)
{
    tui_layout_t *l = (tui_layout_t *)calloc(1, sizeof(tui_layout_t));
    if (!l) return NULL;
    l->kind      = LAYOUT_FLEX;
    l->flex_dir  = dir;
    l->gap       = gap < 0 ? 0 : gap;
    l->justify   = TUI_JUSTIFY_START;
    l->align     = (dir == TUI_FLEX_DIR_ROW) ? TUI_ALIGN_STRETCH : TUI_ALIGN_START;
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

tui_layout_t *tui_layout_leaf_with_free(tui_render_fn fn, void *userdata,
                                         void (*free_fn)(void *))
{
    tui_layout_t *l = tui_layout_leaf(fn, userdata);
    if (l) l->userdata_free = free_fn;
    return l;
}

/* ── 设置主轴/交叉轴对齐 ──────────────────────────── */

void tui_layout_justify(tui_layout_t *node, tui_justify_t j)
{
    if (node) node->justify = j;
}

void tui_layout_align(tui_layout_t *node, tui_align_t a)
{
    if (node) node->align = a;
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
    if (layout->userdata && layout->userdata_free)
        layout->userdata_free(layout->userdata);
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
    c->grow   = weight > 0 ? (double)weight : 0.0;
    c->basis  = 0;
    c->align  = (tui_align_t)-1;
    c->next   = NULL;

    if (!parent->children) {
        parent->children = c;
    } else {
        tui_layout_child_t *last = parent->children;
        while (last->next) last = last->next;
        last->next = c;
    }

    return TUI_OK;
}

int tui_layout_add_flex(tui_layout_t *parent, tui_layout_t *child,
                        double grow, int basis)
{
    if (!parent || !child) return TUI_ERR_PARAM;
    if (parent->kind == LAYOUT_LEAF) return TUI_ERR_PARAM;

    tui_layout_child_t *c = (tui_layout_child_t *)calloc(1, sizeof(tui_layout_child_t));
    if (!c) return TUI_ERR_MEM;

    c->node   = child;
    c->weight = 0;
    c->grow   = grow < 0 ? 0 : grow;
    c->basis  = basis < 0 ? 0 : basis;
    c->align  = (tui_align_t)-1;
    c->next   = NULL;

    if (!parent->children) {
        parent->children = c;
    } else {
        tui_layout_child_t *last = parent->children;
        while (last->next) last = last->next;
        last->next = c;
    }

    return TUI_OK;
}

void tui_layout_child_align(tui_layout_t *parent, int index, tui_align_t a)
{
    if (!parent) return;
    tui_layout_child_t *c = parent->children;
    int i = 0;
    while (c) {
        if (i == index) { c->align = a; return; }
        c = c->next;
        i++;
    }
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
    return layout->children != NULL;
}

/* ══════════════════════════════════════════════════════
 *  渲染 — 核心递归（flex 引擎）
 * ══════════════════════════════════════════════════════ */

static tui_rect_t apply_padding(const tui_rect_t *area,
                                int pt, int pr, int pb, int pl)
{
    tui_rect_t r = *area;
    if (r.rows > pt + pb) { r.row += pt; r.rows -= pt + pb; }
    if (r.cols > pl + pr) { r.col += pl; r.cols -= pl + pr; }
    return r;
}

/* 把 (basis, grow) 转成最终 main-axis 大小 */
static int resolve_main_size(tui_layout_child_t *c, int main_total, int fixed_total, int n_growable, int gap_total)
{
    /* main_total - gap_total = 可用空间，扣去 fixed 后按 grow 分配 */
    int avail = main_total - gap_total - fixed_total;
    if (avail < 0) avail = 0;

    if (c->basis > 0) return c->basis;
    if (c->grow > 0.0 && n_growable > 0) {
        double gsum = 0;
        for (tui_layout_child_t *p = c; p; p = p->next) {
            if (p->basis == 0 && p->grow > 0) gsum += p->grow;
            if (p == c) break;
        }
        /* 简化：直接按 grow 算（调用方需传入预计算总值） */
        return (int)(avail * c->grow / gsum);
    }
    return 0;  /* 内容自适应：渲染时再确定 */
}

/* 主入口：flex 容器渲染 */
static int render_flex(int fd, tui_layout_t *node, tui_rect_t area)
{
    if (!node || !tui_rect_valid(&area)) return TUI_ERR_PARAM;

    tui_rect_t inner = apply_padding(&area,
        node->pad_top, node->pad_right,
        node->pad_bottom, node->pad_left);

    if (!tui_rect_valid(&inner)) return TUI_OK;

    int n = 0;
    tui_layout_child_t *c;
    for (c = node->children; c; c = c->next) n++;
    if (n == 0) return TUI_OK;

    int is_row  = (node->kind == LAYOUT_HBOX) ||
                  (node->kind == LAYOUT_FLEX && node->flex_dir == TUI_FLEX_DIR_ROW);
    int main_total = is_row ? inner.cols : inner.rows;
    int cross_total = is_row ? inner.rows : inner.cols;

    int gap_total;
    if (node->kind == LAYOUT_FLEX) gap_total = (n - 1) * node->gap;
    else                            gap_total = (n - 1) * node->spacing;

    /* 1. 收集每个子项的 basis 大小、grow 总和 */
    int *sizes = (int *)calloc((size_t)n, sizeof(int));
    if (!sizes) return TUI_ERR_MEM;

    int fixed_total = 0;
    double grow_sum = 0;
    for (c = node->children; c; c = c->next) {
        int idx = 0;
        tui_layout_child_t *p;
        for (p = node->children; p && p != c; p = p->next) idx++;

        if (c->basis > 0) {
            sizes[idx] = c->basis;
            fixed_total += c->basis;
        } else if (c->weight > 0) {
            /* legacy vbox/hbox 权重：先按 weight 比例 */
            grow_sum += c->weight;
        } else if (c->grow > 0) {
            grow_sum += c->grow;
        }
        /* weight==0 grow==0 basis==0: 内容自适应，最后分配 1 单位 */
    }

    int avail = main_total - gap_total - fixed_total;
    if (avail < 0) avail = 0;

    /* 2. 按 grow/weight 分配剩余空间 */
    if (grow_sum > 0) {
        double acc = 0;
        int    distributed = 0;
        int i = 0;
        for (c = node->children; c; c = c->next, i++) {
            if (sizes[i] > 0) continue;
            double g = c->weight > 0 ? (double)c->weight : c->grow;
            int sz;
            if (c->next == NULL) {
                /* 最后一个：取余数，避免舍入误差 */
                sz = avail - distributed;
            } else {
                sz = (int)(avail * g / grow_sum);
            }
            if (sz < 1) sz = 1;
            sizes[i] = sz;
            distributed += sz;
        }
    } else {
        /* 全部 fixed 或 content */
        int i = 0;
        for (c = node->children; c; c = c->next, i++) {
            if (sizes[i] > 0) continue;
            /* content adaptive: 默认 1 */
            sizes[i] = 1;
        }
    }

    /* 3. 主轴对齐：计算主轴起点 + 每个子项 main 偏移 */
    int total_used = 0;
    for (int i = 0; i < n; i++) total_used += sizes[i];
    total_used += gap_total;

    int main_start;
    int extra_space = main_total - total_used;
    if (extra_space < 0) extra_space = 0;

    switch (node->justify) {
        case TUI_JUSTIFY_START:
        case TUI_JUSTIFY_BETWEEN:   /* between: 首项贴边 */
            main_start = is_row ? inner.col : inner.row;
            break;
        case TUI_JUSTIFY_CENTER:
            main_start = (is_row ? inner.col : inner.row) + extra_space / 2;
            break;
        case TUI_JUSTIFY_END:
            main_start = (is_row ? inner.col : inner.row) + extra_space;
            break;
        default:
            main_start = is_row ? inner.col : inner.row;
            break;
    }

    /* 4. 计算每个子项位置 */
    int *main_offsets = (int *)calloc((size_t)(n + 1), sizeof(int));
    if (!main_offsets) { free(sizes); return TUI_ERR_MEM; }

    main_offsets[0] = main_start;
    for (int i = 0; i < n; i++) {
        main_offsets[i + 1] = main_offsets[i] + sizes[i];
        if (i < n - 1) main_offsets[i + 1] +=
            (node->kind == LAYOUT_FLEX) ? node->gap : node->spacing;
    }

    /* justify=between: 重算每个 gap */
    if (node->justify == TUI_JUSTIFY_BETWEEN && n > 1 && extra_space > 0) {
        int per_gap = extra_space / (n - 1);
        for (int i = 1; i < n; i++) {
            main_offsets[i] = main_offsets[0] + i * per_gap;
            for (int k = 0; k < i; k++) {
                main_offsets[i] += sizes[k];
            }
        }
    }

    /* 5. 渲染每个子项 */
    int i = 0;
    for (c = node->children; c; c = c->next, i++) {
        tui_rect_t child_area = inner;
        tui_align_t ca = (c->align == (tui_align_t)-1) ? node->align : c->align;

        if (is_row) {
            child_area.col  = main_offsets[i];
            child_area.cols = sizes[i];
            child_area.rows = inner.rows;

            /* 交叉轴对齐 */
            if (ca == TUI_ALIGN_STRETCH) {
                /* full height */
            } else if (ca == TUI_ALIGN_CENTER) {
                /* 居中：高度不变，但让内容居中——子节点会用 inner.rows 全高 */
            } else if (ca == TUI_ALIGN_END) {
                /* end */
            }
        } else {
            child_area.row  = main_offsets[i];
            child_area.rows = sizes[i];
            child_area.cols = inner.cols;
        }

        if (c->node->kind == LAYOUT_LEAF) {
            if (c->node->render_fn && tui_rect_valid(&child_area))
                c->node->render_fn(fd, &child_area, c->node->userdata);
        } else {
            if (tui_rect_valid(&child_area))
                render_flex(fd, c->node, child_area);
        }
    }

    free(main_offsets);
    free(sizes);
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

    return render_flex(fd, root, area);
}

/* ══════════════════════════════════════════════════════
 *  显示模式模板
 * ══════════════════════════════════════════════════════ */

/* ── 全屏模式 ─────────────────────────────────────── */

tui_layout_t *tui_layout_fullscreen(tui_render_fn fn, void *data)
{
    return tui_layout_leaf(fn, data);
}

/* ── 居中模式 ─────────────────────────────────────── */

typedef struct {
    int         width, height;
    tui_render_fn content_fn;
    void       *content_data;
} centered_ctx_t;

static int centered_render(int fd, const tui_rect_t *area, void *userdata)
{
    centered_ctx_t *ctx = (centered_ctx_t *)userdata;
    if (!ctx || !area) return TUI_ERR_PARAM;

    int w = ctx->width  > 0 ? ctx->width  : area->cols - 4;
    int h = ctx->height > 0 ? ctx->height : area->rows - 4;

    if (w > area->cols) w = area->cols;
    if (h > area->rows) h = area->rows;
    if (w < 4) w = 4;
    if (h < 1) h = 1;

    int row = area->row + (area->rows - h) / 2;
    int col = area->col + (area->cols - w) / 2;

    /* 用黑色填充背景遮罩 */
    tui_set_bg(fd, TUI_COLOR_BLACK);
    int r;
    for (r = 0; r < h; r++) {
        tui_cursor_goto(fd, row + r, col);
        tui_spaces(fd, w);
    }
    tui_reset_style(fd);

    tui_rect_t inner = { row, col, h, w };
    if (ctx->content_fn)
        ctx->content_fn(fd, &inner, ctx->content_data);

    return TUI_OK;
}

tui_layout_t *tui_layout_centered(int width, int height,
                                  tui_render_fn fn, void *data)
{
    centered_ctx_t *ctx = (centered_ctx_t *)calloc(1, sizeof(centered_ctx_t));
    if (!ctx) return NULL;
    ctx->width       = width;
    ctx->height      = height;
    ctx->content_fn  = fn;
    ctx->content_data = data;
    return tui_layout_leaf(centered_render, ctx);
}

/* ── 向导模式 ─────────────────────────────────────── */

typedef struct {
    char        title[64];
    char        footer[64];
    tui_render_fn content_fn;
    void       *content_data;
} wizard_ctx_t;

static int wizard_render(int fd, const tui_rect_t *area, void *userdata)
{
    wizard_ctx_t *ctx = (wizard_ctx_t *)userdata;
    if (!ctx || !area) return TUI_ERR_PARAM;

    tui_rect_t inner = *area;

    int content_h = 0;
    if (ctx->content_fn) content_h = 3;

    int header_h = ctx->title[0] ? 1 : 0;
    int footer_h = ctx->footer[0] ? 1 : 0;
    int total_h  = header_h + footer_h + content_h;

    int start_row = inner.row + (inner.rows - total_h) / 2;
    if (start_row < inner.row) start_row = inner.row;

    int y = start_row;

    if (header_h) {
        tui_cursor_goto(fd, y, inner.col);
        tui_set_bg(fd, tui_meuos_theme.accent);
        int i;
        for (i = 0; i < inner.cols; i++) write(fd, " ", 1);
        tui_cursor_goto(fd, y, inner.col + 2);
        tui_set_fg(fd, TUI_COLOR_WHITE);
        tui_set_attr(fd, TUI_ATTR_BOLD);
        tui_write(fd, ctx->title);
        tui_reset_style(fd);
        y++;
    }

    if (ctx->content_fn) {
        tui_rect_t cr = { y, inner.col + 2, content_h, inner.cols - 4 };
        ctx->content_fn(fd, &cr, ctx->content_data);
        y += content_h;
    }

    if (footer_h) {
        tui_cursor_goto(fd, y, inner.col);
        tui_set_attr(fd, TUI_ATTR_DIM);
        tui_hline(fd, inner.col, inner.cols, '-', tui_meuos_theme.dim);
        tui_reset_style(fd);
        tui_cursor_goto(fd, y, inner.col + 2);
        tui_set_fg(fd, tui_meuos_theme.dim);
        tui_write(fd, ctx->footer);
        tui_reset_style(fd);
    }

    return TUI_OK;
}

tui_layout_t *tui_layout_wizard(const char *title,
                                tui_render_fn fn, void *data,
                                const char *footer)
{
    wizard_ctx_t *ctx = (wizard_ctx_t *)calloc(1, sizeof(wizard_ctx_t));
    if (!ctx) return NULL;
    if (title)  strncpy(ctx->title,  title,  sizeof(ctx->title) - 1);
    if (footer) strncpy(ctx->footer, footer, sizeof(ctx->footer) - 1);
    ctx->content_fn  = fn;
    ctx->content_data = data;
    return tui_layout_leaf(wizard_render, ctx);
}

/* ── 双栏模式 ─────────────────────────────────────── */

tui_layout_t *tui_layout_dual(int sidebar_width, const char *sidebar_title,
                              tui_render_fn side_fn, void *side_data,
                              tui_render_fn content_fn, void *content_data)
{
    tui_layout_t *root = tui_layout_hbox(0);
    if (!root) return NULL;

    tui_layout_t *side_panel = tui_box_new(
        sidebar_title ? sidebar_title : "",
        0, side_fn, side_data);
    if (!side_panel) { tui_layout_free(root); return NULL; }
    tui_layout_add(root, side_panel, sidebar_width);

    tui_layout_t *content_panel = tui_box_new(
        "",
        0, content_fn, content_data);
    if (!content_panel) { tui_layout_free(root); return NULL; }
    tui_layout_add(root, content_panel, 1);

    return root;
}
