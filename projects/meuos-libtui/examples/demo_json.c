/* demo_json.c — TUI JSON 格式化/查看器
 *
 * 功能：读取 JSON 文本，带语法高亮地显示，支持树形折叠浏览
 *
 * 运行: make demo_json && ./build/demo_json
 *       echo '{"name":"MeuOS","version":1.0,"features":["libc","mcc"]}' | ./build/demo_json
 *       ./build/demo_json example.json
 *
 * 按键:
 *   ↑↓     - 滚动浏览
 *   Enter  - 折叠/展开当前节点
 *   Tab    - 下一个节点
 *   +/-    - 全部展开/折叠
 *   q/ESC  - 退出
 */

#define _XOPEN_SOURCE 700

#include "meuos/libtui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════
 *  简易 JSON 解析器（递归下降）
 * ══════════════════════════════════════════════════════ */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} json_type_t;

typedef struct json_node {
    json_type_t type;
    char        key[128];      /* 对象属性名 */
    char        str_val[512];  /* 字符串/数字/布尔值 */
    struct json_node *children; /* 数组/对象的子节点 */
    int         n_children;
    int         folded;         /* 是否折叠 */
} json_node_t;

/* ── 解析上下文 ── */
typedef struct {
    const char *src;
    int         pos;
    int         len;
} json_parser_t;

static void skip_ws(json_parser_t *p)
{
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            p->pos++;
        else
            break;
    }
}

static json_node_t *parse_value(json_parser_t *p);

static char *parse_string_raw(json_parser_t *p, char *out, int max)
{
    skip_ws(p);
    if (p->pos >= p->len || p->src[p->pos] != '"')
        return NULL;
    p->pos++; /* skip " */

    int i = 0;
    while (p->pos < p->len && p->src[p->pos] != '"') {
        char c = p->src[p->pos++];
        if (c == '\\' && p->pos < p->len) {
            char e = p->src[p->pos++];
            switch (e) {
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case 'r':  c = '\r'; break;
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            default:   c = e;    break;
            }
        }
        if (i < max - 1) out[i++] = c;
    }
    if (p->pos < p->len) p->pos++; /* skip closing " */
    out[i] = '\0';
    return out;
}

static json_node_t *parse_object(json_parser_t *p)
{
    p->pos++; /* skip { */
    skip_ws(p);

    json_node_t *nodes = NULL;
    int count = 0;
    int cap = 0;

    while (p->pos < p->len && p->src[p->pos] != '}') {
        skip_ws(p);
        if (p->src[p->pos] == '}') break;

        /* 解析 key */
        char key[128];
        if (!parse_string_raw(p, key, sizeof(key))) break;

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ':') p->pos++;

        /* 解析 value */
        json_node_t *val = parse_value(p);
        if (!val) break;

        strncpy(val->key, key, sizeof(val->key) - 1);

        /* 添加到数组 */
        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            nodes = realloc(nodes, (size_t)cap * sizeof(json_node_t));
        }
        nodes[count++] = *val;
        free(val);

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ',') p->pos++;
    }
    if (p->pos < p->len) p->pos++; /* skip } */

    json_node_t *node = calloc(1, sizeof(json_node_t));
    node->type = JSON_OBJECT;
    node->children = nodes;
    node->n_children = count;
    return node;
}

static json_node_t *parse_array(json_parser_t *p)
{
    p->pos++; /* skip [ */
    skip_ws(p);

    json_node_t *nodes = NULL;
    int count = 0;
    int cap = 0;

    while (p->pos < p->len && p->src[p->pos] != ']') {
        skip_ws(p);
        if (p->src[p->pos] == ']') break;

        json_node_t *val = parse_value(p);
        if (!val) break;

        /* 数组元素的 key 是索引 */
        snprintf(val->key, sizeof(val->key), "[%d]", count);

        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            nodes = realloc(nodes, (size_t)cap * sizeof(json_node_t));
        }
        nodes[count++] = *val;
        free(val);

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ',') p->pos++;
    }
    if (p->pos < p->len) p->pos++; /* skip ] */

    json_node_t *node = calloc(1, sizeof(json_node_t));
    node->type = JSON_ARRAY;
    node->children = nodes;
    node->n_children = count;
    return node;
}

static json_node_t *parse_value(json_parser_t *p)
{
    skip_ws(p);
    if (p->pos >= p->len) return NULL;

    char c = p->src[p->pos];
    json_node_t *node = calloc(1, sizeof(json_node_t));

    if (c == '{') {
        free(node);
        return parse_object(p);
    }
    if (c == '[') {
        free(node);
        return parse_array(p);
    }
    if (c == '"') {
        node->type = JSON_STRING;
        parse_string_raw(p, node->str_val, sizeof(node->str_val));
        return node;
    }
    if (c == 't' || c == 'f') {
        node->type = JSON_BOOL;
        if (p->pos + 4 <= p->len && strncmp(p->src + p->pos, "true", 4) == 0) {
            strcpy(node->str_val, "true");
            p->pos += 4;
        } else if (p->pos + 5 <= p->len && strncmp(p->src + p->pos, "false", 5) == 0) {
            strcpy(node->str_val, "false");
            p->pos += 5;
        }
        return node;
    }
    if (c == 'n') {
        node->type = JSON_NULL;
        if (p->pos + 4 <= p->len && strncmp(p->src + p->pos, "null", 4) == 0) {
            strcpy(node->str_val, "null");
            p->pos += 4;
        }
        return node;
    }
    /* number */
    {
        int start = p->pos;
        while (p->pos < p->len) {
            char ch = p->src[p->pos];
            if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.' ||
                ch == 'e' || ch == 'E' || ch == '+') {
                p->pos++;
            } else {
                break;
            }
        }
        int n = p->pos - start;
        if (n > 0 && n < (int)sizeof(node->str_val)) {
            memcpy(node->str_val, p->src + start, (size_t)n);
            node->str_val[n] = '\0';
            node->type = JSON_NUMBER;
        }
        return node;
    }
}

static json_node_t *json_parse(const char *src, int len)
{
    json_parser_t p = { .src = src, .pos = 0, .len = len };
    return parse_value(&p);
}

static void json_free(json_node_t *node)
{
    if (!node) return;
    if (node->children) {
        for (int i = 0; i < node->n_children; i++)
            json_free(&node->children[i]);
        free(node->children);
    }
    /* children are embedded in the struct array, already freed above */
    free(node);
}

/* ══════════════════════════════════════════════════════
 *  JSON 树形渲染
 * ══════════════════════════════════════════════════════ */

typedef struct {
    int  depth;
    int  is_last;       /* 是否是同级最后一个 */
    int  path[64];      /* 从根到当前的路径 */
    int  path_len;
} render_ctx_t;

typedef struct {
    json_node_t *root;
    int          scroll;
    int          cursor_line;
    int          total_lines;
} json_view_t;

/* 收集所有可显示行（扁平化树） */
typedef struct {
    json_node_t *node;
    int          depth;
    int          is_last;
    char         prefix[256];  /* 缩进前缀 */
} flat_line_t;

#define MAX_FLAT 512
static flat_line_t flat_lines[MAX_FLAT];
static int flat_count = 0;

static void flatten(json_node_t *node, int depth, int is_last,
                    const char *prefix)
{
    if (!node || flat_count >= MAX_FLAT) return;

    /* 当前节点 */
    flat_lines[flat_count].node = node;
    flat_lines[flat_count].depth = depth;
    flat_lines[flat_count].is_last = is_last;
    strncpy(flat_lines[flat_count].prefix, prefix, sizeof(flat_lines[flat_count].prefix) - 1);
    flat_lines[flat_count].prefix[sizeof(flat_lines[flat_count].prefix) - 1] = '\0';
    flat_count++;

    /* 子节点 */
    if ((node->type == JSON_OBJECT || node->type == JSON_ARRAY) &&
        !node->folded && node->n_children > 0) {
        char child_prefix[256];
        for (int i = 0; i < node->n_children; i++) {
            int child_is_last = (i == node->n_children - 1);
            /* 构建子前缀 */
            snprintf(child_prefix, sizeof(child_prefix), "%s%s",
                     prefix, is_last ? "  " : "│ ");
            flatten(&node->children[i], depth + 1, child_is_last, child_prefix);
        }
    }
}

static void rebuild_flat(json_node_t *root)
{
    flat_count = 0;
    flatten(root, 0, 1, "");
}

/* ── 颜色辅助 ── */
static tui_color_t value_color(json_type_t type)
{
    switch (type) {
    case JSON_STRING:  return TUI_COLOR_GREEN;
    case JSON_NUMBER:  return TUI_COLOR_YELLOW;
    case JSON_BOOL:    return TUI_COLOR_MAGENTA;
    case JSON_NULL:    return TUI_COLOR_RED;
    case JSON_OBJECT:  return TUI_COLOR_CYAN;
    case JSON_ARRAY:   return TUI_COLOR_BLUE;
    default:           return TUI_COLOR_DEFAULT;
    }
}

static const char *type_name(json_type_t type)
{
    switch (type) {
    case JSON_STRING:  return "string";
    case JSON_NUMBER:  return "number";
    case JSON_BOOL:    return "bool";
    case JSON_NULL:    return "null";
    case JSON_OBJECT:  return "object";
    case JSON_ARRAY:   return "array";
    default:           return "?";
    }
}

/* ══════════════════════════════════════════════════════
 *  渲染
 * ══════════════════════════════════════════════════════ */

static int json_render(int fd, const tui_rect_t *area, void *udata)
{
    json_view_t *view = (json_view_t *)udata;

    /* 边框 */
    tui_rect_t inner = *area;
    tui_draw_border(fd, &inner, "  JSON Viewer  ", 0, tui_meuos_theme.border);

    if (!tui_rect_valid(&inner)) return TUI_OK;

    int max_lines = inner.rows;
    view->total_lines = flat_count;

    /* 计算可见范围 */
    int start = view->scroll;
    if (start < 0) start = 0;
    if (start > flat_count - max_lines && flat_count > max_lines)
        start = flat_count - max_lines;
    if (start < 0) start = 0;

    int visible = flat_count - start;
    if (visible > max_lines) visible = max_lines;

    int y = inner.row;
    int x = inner.col;

    for (int i = 0; i < visible; i++) {
        int idx = start + i;
        if (idx >= flat_count) break;

        flat_line_t *fl = &flat_lines[idx];
        json_node_t *node = fl->node;
        int is_cursor = (idx == view->cursor_line);

        tui_cursor_goto(fd, y + i, x);

        /* 选中行高亮 */
        if (is_cursor) {
            tui_set_bg(fd, tui_meuos_theme.highlight);
            tui_set_fg(fd, TUI_COLOR_WHITE);
            tui_set_attr(fd, TUI_ATTR_BOLD);
            tui_spaces(fd, inner.cols);
            tui_cursor_goto(fd, y + i, x);
        }

        /* 前缀（树形缩进） */
        tui_set_fg(fd, tui_meuos_theme.dim);
        tui_set_attr(fd, TUI_ATTR_DIM);
        if (!is_cursor) tui_reset_style(fd);
        tui_write(fd, fl->prefix);

        /* 折叠/展开标记 */
        if (node->type == JSON_OBJECT || node->type == JSON_ARRAY) {
            if (is_cursor) {
                tui_set_bg(fd, tui_meuos_theme.highlight);
                tui_set_fg(fd, TUI_COLOR_YELLOW);
                tui_set_attr(fd, TUI_ATTR_BOLD);
            } else {
                tui_set_fg(fd, TUI_COLOR_YELLOW);
                tui_set_attr(fd, TUI_ATTR_BOLD);
            }
            tui_write(fd, node->folded ? "▶ " : "▼ ");
        } else {
            tui_write(fd, "  ");
        }

        /* key */
        if (!is_cursor) tui_reset_style(fd);
        if (node->key[0]) {
            if (is_cursor) {
                tui_set_bg(fd, tui_meuos_theme.highlight);
                tui_set_fg(fd, TUI_COLOR_CYAN);
                tui_set_attr(fd, TUI_ATTR_BOLD);
            } else {
                tui_set_fg(fd, TUI_COLOR_CYAN);
            }
            tui_write(fd, node->key);
            if (is_cursor) {
                tui_set_bg(fd, tui_meuos_theme.highlight);
            }
            tui_write(fd, ": ");
        }

        /* value 或类型摘要 */
        if (!is_cursor) tui_reset_style(fd);
        tui_color_t vc = value_color(node->type);

        if (is_cursor) {
            tui_set_bg(fd, tui_meuos_theme.highlight);
            tui_set_fg(fd, vc);
            tui_set_attr(fd, TUI_ATTR_BOLD);
        } else {
            tui_set_fg(fd, vc);
        }

        switch (node->type) {
        case JSON_STRING:
            tui_write(fd, "\"");
            {
                int bytes = tui_truncate(node->str_val, inner.cols - 20);
                write(fd, node->str_val, (size_t)bytes);
            }
            tui_write(fd, "\"");
            break;
        case JSON_NUMBER:
        case JSON_BOOL:
        case JSON_NULL:
            tui_write(fd, node->str_val);
            break;
        case JSON_OBJECT:
            if (node->folded) {
                char summary[32];
                snprintf(summary, sizeof(summary), "{...} (%d)", node->n_children);
                tui_write(fd, summary);
            } else {
                tui_write(fd, "{");
            }
            break;
        case JSON_ARRAY:
            if (node->folded) {
                char summary[32];
                snprintf(summary, sizeof(summary), "[...] (%d)", node->n_children);
                tui_write(fd, summary);
            } else {
                tui_write(fd, "[");
            }
            break;
        }

        tui_reset_style(fd);
    }

    /* 剩余行清空 */
    for (int i = visible; i < max_lines; i++) {
        tui_cursor_goto(fd, y + i, x);
        tui_clear_eol(fd);
    }

    /* 滚动指示器 */
    if (flat_count > max_lines) {
        tui_cursor_goto(fd, inner.row + inner.rows - 1, inner.col + inner.cols - 16);
        tui_set_fg(fd, TUI_COLOR_YELLOW);
        tui_set_attr(fd, TUI_ATTR_DIM);
        char info[32];
        int pct = (start + visible) * 100 / flat_count;
        snprintf(info, sizeof(info), " %d/%d (%d%%) ", start + visible, flat_count, pct);
        tui_write(fd, info);
        tui_reset_style(fd);
    }

    return TUI_OK;
}

/* ══════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════ */

/* 示例 JSON 数据 */
static const char *example_json =
    "{\"project\":\"MeuOS Kit\",\"version\":\"1.0.0\",\"license\":\"RFL v1.0\","
    "\"components\":{\"compiler\":\"mcc\",\"libc\":\"meuos-libc\","
    "\"build_system\":\"meow\",\"shell\":\"msh\","
    "\"toolchain\":[\"as\",\"ld\",\"ar\",\"nm\",\"readelf\",\"strip\",\"objcopy\",\"objdump\"]},"
    "\"features\":[\"self-bootstrapping\",\"zero-gnu\",\"zero-llvm\",\"posix-compliant\"],"
    "\"architectures\":[\"x86_64\",\"aarch64\",\"riscv64\",\"i386\",\"loongarch64\",\"arm\"],"
    "\"stats\":{\"lines_of_code\":150000,\"test_coverage\":78,\"bootstrap_phases\":7},"
    "\"active\":true,\"kernel_ready\":false,\"maintainer\":\"Meituan Engineering\"}";

int main(int argc, char *argv[])
{
    /* 读取 JSON 输入 */
    char *json_text = NULL;
    int json_len = 0;

    if (argc > 1) {
        /* 从文件读取 */
        FILE *f = fopen(argv[1], "r");
        if (!f) {
            fprintf(stderr, "Cannot open: %s\n", argv[1]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        json_len = (int)ftell(f);
        fseek(f, 0, SEEK_SET);
        json_text = malloc((size_t)json_len + 1);
        fread(json_text, 1, (size_t)json_len, f);
        json_text[json_len] = '\0';
        fclose(f);
    } else {
        /* 从 stdin 读取（仅当 stdin 不是终端时） */
        if (isatty(0)) {
            /* 在终端中直接运行，使用示例数据 */
            json_text = strdup(example_json);
            json_len = (int)strlen(example_json);
        } else {
            char buf[4096];
            int total = 0;
            json_text = malloc(4096);
            while (!feof(stdin)) {
                int n = (int)fread(buf, 1, sizeof(buf), stdin);
                if (n <= 0) break;
                json_text = realloc(json_text, (size_t)(total + n + 1));
                memcpy(json_text + total, buf, (size_t)n);
                total += n;
            }
            json_text[total] = '\0';
            json_len = total;

            if (json_len == 0) {
                /* 无输入则使用示例 */
                json_text = strdup(example_json);
                json_len = (int)strlen(example_json);
            }
        }
    }

    /* 解析 JSON */
    json_node_t *root = json_parse(json_text, json_len);
    if (!root) {
        fprintf(stderr, "JSON parse error\n");
        free(json_text);
        return 1;
    }

    /* 初始化视图 */
    json_view_t view;
    memset(&view, 0, sizeof(view));
    view.root = root;
    view.scroll = 0;
    view.cursor_line = 0;
    rebuild_flat(root);

    /* 初始化终端 */
    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_clear_screen(0);
    tui_cursor_show(0, 0);

    tui_size_t scr;
    if (tui_get_size(0, &scr) != TUI_OK) {
        scr.rows = 30;
        scr.cols = 80;
    }

    /* 事件循环 */
    tui_event_t ev;
    int running = 1;

    while (running) {
        /* 标题栏 */
        tui_rect_t header_area = { 1, 1, 1, scr.cols - 1 };
        tui_cursor_goto(0, header_area.row, header_area.col);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_attr(0, TUI_ATTR_BOLD);
        tui_spaces(0, header_area.cols - 1);
        tui_cursor_goto(0, header_area.row, header_area.col + 2);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_write(0, "JSON Viewer — MeuOS Kit");
        const char *hint = " q=quit  ↑↓=scroll  Enter=fold  +=expand  -=collapse ";
        int hw = tui_strwidth(hint);
        tui_cursor_goto(0, header_area.row, header_area.col + header_area.cols - hw - 1);
        tui_set_fg(0, TUI_COLOR_YELLOW);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_write(0, hint);
        tui_reset_style(0);

        /* JSON 视图 */
        tui_rect_t json_area = { 3, 1, scr.rows - 5, scr.cols - 1 };
        json_render(0, &json_area, &view);

        /* 状态栏 */
        tui_rect_t status_area = { scr.rows, 1, 1, scr.cols - 1 };
        tui_cursor_goto(0, status_area.row, status_area.col);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_set_attr(0, TUI_ATTR_BOLD);

        char status[256];
        snprintf(status, sizeof(status), " Lines: %d | Type: %s | Cursor: %d ",
                 flat_count, type_name(root->type), view.cursor_line);
        tui_write(0, status);

        const char *sright = " libtui v1.0 ";
        int rw = tui_strwidth(sright);
        int pad = status_area.cols - 1 - tui_strwidth(status) - rw;
        if (pad > 0) tui_spaces(0, pad);
        tui_write(0, sright);

        tui_reset_style(0);

        /* 输入处理 */
        if (tui_getkey(0, &ev) == TUI_OK) {
            switch (ev.key) {
            case 'q':
            case TUI_KEY_ESC:
                running = 0;
                break;

            case TUI_KEY_UP:
                if (view.cursor_line > 0) {
                    view.cursor_line--;
                    if (view.cursor_line < view.scroll)
                        view.scroll = view.cursor_line;
                }
                break;

            case TUI_KEY_DOWN:
                if (view.cursor_line < flat_count - 1) {
                    view.cursor_line++;
                    if (view.cursor_line >= view.scroll + (scr.rows - 7))
                        view.scroll = view.cursor_line - (scr.rows - 7) + 1;
                }
                break;

            case TUI_KEY_CR:
            case TUI_KEY_LF:
                /* 折叠/展开当前节点 */
                if (view.cursor_line < flat_count) {
                    json_node_t *node = flat_lines[view.cursor_line].node;
                    if (node->type == JSON_OBJECT || node->type == JSON_ARRAY) {
                        node->folded = !node->folded;
                        rebuild_flat(root);
                    }
                }
                break;

            case '+':
                /* 全部展开 */
                {
                    void expand_all(json_node_t * n);
                    /* 内联展开 */
                    for (int i = 0; i < flat_count; i++) {
                        if (flat_lines[i].node->type == JSON_OBJECT ||
                            flat_lines[i].node->type == JSON_ARRAY)
                            flat_lines[i].node->folded = 0;
                    }
                    rebuild_flat(root);
                }
                break;

            case '-':
                /* 全部折叠（只保留根节点） */
                {
                    for (int i = 0; i < flat_count; i++) {
                        if (flat_lines[i].node->type == JSON_OBJECT ||
                            flat_lines[i].node->type == JSON_ARRAY)
                            flat_lines[i].node->folded = 1;
                    }
                    if (root->type == JSON_OBJECT || root->type == JSON_ARRAY)
                        root->folded = 0;  /* 根节点保持展开 */
                    rebuild_flat(root);
                }
                break;

            case TUI_KEY_HOME:
                view.cursor_line = 0;
                view.scroll = 0;
                break;

            case TUI_KEY_END:
                view.cursor_line = flat_count - 1;
                break;

            default:
                break;
            }
        }
    }

    /* 清理 */
    tui_cursor_show(0, 1);
    tui_clear_screen(0);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);

    json_free(root);
    free(json_text);

    return 0;
}
