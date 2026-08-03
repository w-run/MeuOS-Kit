/* libutils/config.c — 简化 YAML 解析器
 *
 * 支持子集：
 *   - 块格式（key: value）
 *   - 列表格式（"- item"）
 *   - 嵌套映射（缩进）
 *   - 字符串/整数/布尔/null
 *
 * 不支持：
 *   - 锚点 & 别名
 *   - 复杂 flow style（{a: b}）
 *   - 多文档（---）
 *
 * 这够 meou.yaml 用。
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/config.h"
#include "meuos/utils.h"

/* ----- helpers ----- */

static char *strip_dup(const char *s, size_t len) {
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    while (len > 0 && (s[0] == ' ' || s[0] == '\t')) { s++; len--; }
    char *r = xmalloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

/* 判断一个 trimmed line 是否空行 / 全注释 / 全空白 */
static int line_is_blank(const char *line, size_t len) {
    while (len > 0 && (line[0] == ' ' || line[0] == '\t')) { line++; len--; }
    if (len == 0) return 1;
    if (line[0] == '#') return 1;
    return 0;
}

/* 数缩进空格数（忽略 tab） */
static int count_indent(const char *line, size_t len) {
    int n = 0;
    while (n < (int)len && line[n] == ' ') n++;
    return n;
}

/* 解析缩进结束：返回指向 line 起始的指针并设置 *out_indent。
 * line 末尾已剥除 \n */
static const char *line_indent(const char *line, size_t len, int *out_indent) {
    int ind = count_indent(line, len);
    *out_indent = ind;
    return line + ind;
}

/* 解析一行：拆出 key 与 value（如果有）。
 * 返回 1 如果是 `key: value` 形式；返回 0 如果是列表 `- item` 形式或空。
 * key/value 内部缓冲区调用者负责 free。 */
static int parse_kv(const char *content, size_t len,
                    char **key, char **value) {
    /* 找 : */
    const char *colon = memchr(content, ':', len);
    if (!colon) return 0;
    const char *keystart = content;
    const char *keyend = colon;
    *key = strip_dup(keystart, (size_t)(keyend - keystart));

    const char *vstart = colon + 1;
    size_t vlen = len - (size_t)(vstart - content);
    /* 跳过行内注释：以 # 开始（前面空白） */
    int in_str = 0;
    char quote = 0;
    for (size_t i = 0; i < vlen; i++) {
        char c = vstart[i];
        if (in_str) {
            if (c == '\\' && i + 1 < vlen) { i++; continue; }
            if (c == quote) in_str = 0;
        } else {
            if (c == '"' || c == '\'') { in_str = 1; quote = c; }
            else if (c == '#' && (i == 0 || vstart[i-1] == ' ')) {
                vlen = i;
                break;
            }
        }
    }
    *value = strip_dup(vstart, vlen);
    return 1;
}

static int try_int(const char *s, long *out) {
    if (!s || !*s) return 0;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0') return 0;
    *out = v;
    return 1;
}

static int try_bool(const char *s, int *out) {
    if (!s) return 0;
    if (strcmp(s, "true") == 0 || strcmp(s, "yes") == 0
        || strcmp(s, "on") == 0 || strcmp(s, "True") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(s, "false") == 0 || strcmp(s, "no") == 0
        || strcmp(s, "off") == 0 || strcmp(s, "False") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

/* ----- 节点构造 ----- */

cfg_value_t *cfg_new(void) {
    cfg_value_t *v = xcalloc(1, sizeof(*v));
    v->type = CFG_NULL;
    return v;
}

static cfg_value_t *make_string(const char *s, size_t len) {
    /* 去首尾引号（YAML 双/单引号） */
    while (len > 0 && (s[0] == ' ' || s[0] == '\t')) { s++; len--; }
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    if (len >= 2 && (s[0] == '"' || s[0] == '\'') && s[len-1] == s[0]) {
        s++; len -= 2;
    }
    cfg_value_t *v = xcalloc(1, sizeof(*v));
    v->type = CFG_STRING;
    v->u.string.data = xmalloc(len + 1);
    memcpy(v->u.string.data, s, len);
    v->u.string.data[len] = '\0';
    v->u.string.len = len;
    return v;
}

static cfg_value_t *make_int(long i) {
    cfg_value_t *v = xcalloc(1, sizeof(*v));
    v->type = CFG_INT;
    v->u.integer = i;
    return v;
}

static cfg_value_t *make_bool(int b) {
    cfg_value_t *v = xcalloc(1, sizeof(*v));
    v->type = CFG_BOOL;
    v->u.boolean = b;
    return v;
}

static cfg_value_t *make_list(void) {
    cfg_value_t *v = xcalloc(1, sizeof(*v));
    v->type = CFG_LIST;
    return v;
}

static cfg_value_t *make_table(void) {
    cfg_value_t *v = xcalloc(1, sizeof(*v));
    v->type = CFG_TABLE;
    return v;
}

static void list_append(cfg_value_t *list, const char *item, size_t len) {
    char *s = strip_dup(item, len);
    list->u.list.items = xrealloc(list->u.list.items,
                                  sizeof(char *) * (list->u.list.count + 1));
    list->u.list.items[list->u.list.count++] = s;
}

static void table_set(cfg_value_t *tbl, const char *key, cfg_value_t *val) {
    /* 简化：不检查重复 key */
    tbl->u.table.keys = xrealloc(tbl->u.table.keys,
                                  sizeof(char *) * (tbl->u.table.count + 1));
    tbl->u.table.values = xrealloc(tbl->u.table.values,
                                    sizeof(cfg_value_t *) * (tbl->u.table.count + 1));
    tbl->u.table.keys[tbl->u.table.count] = xstrdup(key);
    tbl->u.table.values[tbl->u.table.count] = val;
    tbl->u.table.count++;
}

/* 解析 value 字符串到对应类型 */
static cfg_value_t *parse_scalar(const char *s, size_t len) {
    char *trimmed = strip_dup(s, len);
    size_t tlen = strlen(trimmed);
    if (tlen == 0) {
        free(trimmed);
        return cfg_new();
    }
    if (strcmp(trimmed, "null") == 0 || strcmp(trimmed, "~") == 0) {
        free(trimmed);
        return cfg_new();
    }
    int b;
    if (try_bool(trimmed, &b)) { free(trimmed); return make_bool(b); }
    long i;
    if (try_int(trimmed, &i)) { free(trimmed); return make_int(i); }
    /* 字符串 */
    cfg_value_t *v = make_string(trimmed, tlen);
    free(trimmed);
    return v;
}

/* ----- 主解析 ----- */

/* 一行缓冲区 */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    int indent;
    int is_list_item;     /* "- xxx" 形式 */
} line_t;

typedef struct {
    const char *input;
    size_t len;
    size_t pos;
    int lineno;
} cfg_parser_t;

static int next_line(cfg_parser_t *p, line_t *out) {
    out->buf = NULL; out->len = 0; out->cap = 0;
    out->indent = 0; out->is_list_item = 0;
    if (p->pos >= p->len) return 0;
    size_t start = p->pos;
    while (p->pos < p->len && p->input[p->pos] != '\n') p->pos++;
    size_t end = p->pos;
    if (p->pos < p->len) p->pos++;  /* skip \n */
    p->lineno++;
    /* 提取 indent */
    while (start + out->indent < end && p->input[start + out->indent] == ' ') {
        out->indent++;
    }
    size_t content_off = start + out->indent;
    /* 检查列表项 */
    if (content_off + 1 < end && p->input[content_off] == '-'
        && (p->input[content_off + 1] == ' '
            || p->input[content_off + 1] == '\t'
            || content_off + 1 == end)) {
        out->is_list_item = 1;
        content_off += 2;
    }
    /* 跳过空行 / 注释行直接返回 0（无内容） */
    if (line_is_blank(p->input + content_off, end - content_off)) return 0;
    out->len = end - content_off;
    out->buf = xmalloc(out->len + 1);
    memcpy(out->buf, p->input + content_off, out->len);
    out->buf[out->len] = '\0';
    return 1;
}

/* 递归解析：以 indent 解析一个 mapping（table）。
 * 注意：列表项作为 mapping 的子元素。 */
static cfg_value_t *parse_table_at(cfg_parser_t *p, int parent_indent);
static cfg_value_t *parse_list_at(cfg_parser_t *p, int parent_indent);

static cfg_value_t *parse_mapping_until(cfg_parser_t *p, int indent, int stop_at_indent) {
    cfg_value_t *tbl = make_table();
    line_t ln;
    while (1) {
        size_t saved_pos = p->pos;
        int saved_lineno = p->lineno;
        if (!next_line(p, &ln)) {
            free(ln.buf);
            break;
        }
        if (ln.indent < indent) {
            /* 该行是父级的内容，回退 */
            p->pos = saved_pos;
            p->lineno = saved_lineno;
            free(ln.buf);
            break;
        }
        if (stop_at_indent >= 0 && ln.indent > stop_at_indent) {
            /* 缩进比预期深，跳过（应该有上层处理） */
            free(ln.buf);
            continue;
        }
        char *key = NULL, *value = NULL;
        if (!parse_kv(ln.buf, ln.len, &key, &value)) {
            /* 无 : 形式，作为键名（值为 null）*/
            key = strip_dup(ln.buf, ln.len);
            value = xstrdup("");
        }
        cfg_value_t *child = NULL;
        if (ln.is_list_item || (value && value[0] == '\0')) {
            /* 下一个缩进行作为子节点（列表/映射） */
            size_t saved_pos2 = p->pos;
            int saved_lineno2 = p->lineno;
            line_t peek;
            child = NULL;
            if (next_line(p, &peek)) {
                if (peek.indent > ln.indent) {
                    /* 子节点 */
                    if (peek.is_list_item) {
                        /* 回退到 peek 前的位置，让 parse_list_at 从第一项开始 */
                        p->pos = saved_pos2;
                        p->lineno = saved_lineno2;
                        child = parse_list_at(p, peek.indent);
                    } else {
                        p->pos = saved_pos2;
                        p->lineno = saved_lineno2;
                        child = parse_mapping_until(p, peek.indent, -1);
                    }
                } else {
                    p->pos = saved_pos2;
                    p->lineno = saved_lineno2;
                }
                free(peek.buf);
            }
            if (!child) child = cfg_new();
        } else {
            child = parse_scalar(value, strlen(value));
        }
        table_set(tbl, key, child);
        free(key); free(value);
        free(ln.buf);
    }
    return tbl;
}

static cfg_value_t *parse_list_at(cfg_parser_t *p, int parent_indent) {
    /* 简化：列表项只能是 scalar 或 单嵌套 table */
    cfg_value_t *list = make_list();
    line_t ln;
    while (1) {
        size_t saved = p->pos;
        int saved_line = p->lineno;
        if (!next_line(p, &ln)) { free(ln.buf); break; }
        if (ln.indent < parent_indent) {
            p->pos = saved; p->lineno = saved_line;
            free(ln.buf); break;
        }
        if (!ln.is_list_item) {
            /* 不是列表项，父级应处理 */
            p->pos = saved; p->lineno = saved_line;
            free(ln.buf); break;
        }
        /* item 内容 = "key: value" 或纯文本 */
        char *key = NULL, *value = NULL;
        if (parse_kv(ln.buf, ln.len, &key, &value)) {
            cfg_value_t *item = make_table();
            cfg_value_t *v = parse_scalar(value, strlen(value));
            table_set(item, key, v);
            /* 处理嵌套 children */
            size_t sp2 = p->pos;
            int sl2 = p->lineno;
            line_t peek;
            if (next_line(p, &peek) && peek.indent > ln.indent) {
                p->pos = sp2; p->lineno = sl2;
                cfg_value_t *inner = parse_mapping_until(p, peek.indent, -1);
                /* 把 inner 的 keys 合并到 item */
                for (size_t i = 0; i < inner->u.table.count; i++) {
                    table_set(item, inner->u.table.keys[i], inner->u.table.values[i]);
                }
                inner->u.table.count = 0;  /* 转移所有权 */
                cfg_value_free(inner);
            } else {
                p->pos = sp2; p->lineno = sl2;
            }
            free(key); free(value); free(ln.buf); free(peek.buf);
            list->u.list.items = xrealloc(list->u.list.items,
                                          sizeof(char *) * (list->u.list.count + 1));
            list->u.list.items[list->u.list.count++] = (char *)item;
        } else {
            char *txt = strip_dup(ln.buf, ln.len);
            list->u.list.items = xrealloc(list->u.list.items,
                                          sizeof(char *) * (list->u.list.count + 1));
            list->u.list.items[list->u.list.count++] = txt;
            free(ln.buf);
        }
    }
    return list;
}

cfg_value_t *cfg_parse(const char *input, size_t len) {
    cfg_parser_t p = { input, len, 0, 0 };
    return parse_mapping_until(&p, 0, -1);
}

cfg_value_t *cfg_load_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = xmalloc(sz + 1);
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);
    cfg_value_t *v = cfg_parse(buf, sz);
    free(buf);
    return v;
}

int cfg_save_file(const cfg_value_t *v, const char *path) {
    /* 仅支持保存 TABLE。 */
    if (!v || v->type != CFG_TABLE) return -1;
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    for (size_t i = 0; i < v->u.table.count; i++) {
        const cfg_value_t *cv = v->u.table.values[i];
        fprintf(fp, "%s: ", v->u.table.keys[i]);
        switch (cv->type) {
        case CFG_STRING: fprintf(fp, "%s\n", cv->u.string.data); break;
        case CFG_INT:    fprintf(fp, "%ld\n", cv->u.integer); break;
        case CFG_BOOL:   fprintf(fp, "%s\n", cv->u.boolean ? "true" : "false"); break;
        case CFG_NULL:   fprintf(fp, "null\n"); break;
        case CFG_LIST:
            fputc('\n', fp);
            for (size_t j = 0; j < cv->u.list.count; j++) {
                fprintf(fp, "  - %s\n", cv->u.list.items[j]);
            }
            break;
        case CFG_TABLE:
            /* 简化：只写 KV */
            fputc('\n', fp);
            for (size_t j = 0; j < cv->u.table.count; j++) {
                fprintf(fp, "  %s: %s\n", cv->u.table.keys[j],
                       cv->u.table.values[j]->u.string.data);
            }
            break;
        }
    }
    fclose(fp);
    return 0;
}

void cfg_value_free(cfg_value_t *v) {
    if (!v) return;
    switch (v->type) {
    case CFG_STRING: free(v->u.string.data); break;
    case CFG_LIST:
        for (size_t i = 0; i < v->u.list.count; i++) {
            /* items may be string OR embedded cfg_value_t* — we store only strings here. */
            free(v->u.list.items[i]);
        }
        free(v->u.list.items);
        break;
    case CFG_TABLE:
        for (size_t i = 0; i < v->u.table.count; i++) {
            free(v->u.table.keys[i]);
            cfg_value_free(v->u.table.values[i]);
        }
        free(v->u.table.keys);
        free(v->u.table.values);
        break;
    default: break;
    }
    free(v);
}

const cfg_value_t *cfg_get(const cfg_value_t *root, const char *key) {
    if (!root || root->type != CFG_TABLE) return NULL;
    for (size_t i = 0; i < root->u.table.count; i++) {
        if (strcmp(root->u.table.keys[i], key) == 0) {
            return root->u.table.values[i];
        }
    }
    return NULL;
}

const cfg_value_t *cfg_get_path(const cfg_value_t *root, const char *dotpath) {
    if (!root) return NULL;
    const cfg_value_t *cur = root;
    const char *s = dotpath;
    char buf[128];
    while (*s && cur) {
        size_t n = 0;
        while (*s && *s != '.' && n < sizeof(buf) - 1) buf[n++] = *s++;
        buf[n] = '\0';
        if (n == 0) break;
        cur = cfg_get(cur, buf);
        if (*s == '.') s++;
    }
    return cur;
}

const char *cfg_string(const cfg_value_t *v, const char *def) {
    if (!v) return def;
    if (v->type == CFG_STRING) return v->u.string.data;
    if (v->type == CFG_INT) {
        static char buf[32];
        snprintf(buf, sizeof(buf), "%ld", v->u.integer);
        return buf;
    }
    return def;
}

int cfg_int(const cfg_value_t *v, int def) {
    if (!v) return def;
    if (v->type == CFG_INT) return (int)v->u.integer;
    if (v->type == CFG_BOOL) return v->u.boolean;
    if (v->type == CFG_STRING) {
        long n = strtol(v->u.string.data, NULL, 10);
        return (int)n;
    }
    return def;
}

int cfg_bool(const cfg_value_t *v, int def) {
    if (!v) return def;
    if (v->type == CFG_BOOL) return v->u.boolean;
    if (v->type == CFG_INT) return v->u.integer != 0;
    if (v->type == CFG_STRING) {
        if (strcmp(v->u.string.data, "true") == 0
            || strcmp(v->u.string.data, "yes") == 0
            || strcmp(v->u.string.data, "on") == 0) return 1;
        return 0;
    }
    return def;
}
