/* libutils/json.c — 简化 JSON 解析与 pretty-print */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/json.h"
#include "meuos/utils.h"

/* === 解析器 === */

typedef struct {
    const char *p;
    const char *end;
    int lineno;
    int col;
    char err[128];
} parser_t;

static void skip_ws(parser_t *ps) {
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (c == '\n') { ps->lineno++; ps->col = 0; }
            ps->p++;
            ps->col++;
        } else break;
    }
}

static json_value_t *parse_value(parser_t *ps);

static char *parse_string(parser_t *ps, size_t *outlen) {
    if (*ps->p != '"') return NULL;
    ps->p++;
    ps->col++;
    const char *start = ps->p;
    /* 简化：不解析转义，直接找下一个 '"' */
    while (ps->p < ps->end && *ps->p != '"') {
        if (*ps->p == '\\' && ps->p + 1 < ps->end) ps->p++;
        ps->p++;
    }
    if (ps->p >= ps->end) {
        snprintf(ps->err, sizeof(ps->err),
                 "unterminated string at line %d", ps->lineno);
        return NULL;
    }
    size_t len = (size_t)(ps->p - start);
    char *s = xmalloc(len + 1);
    memcpy(s, start, len);
    s[len] = '\0';
    ps->p++;  /* skip closing " */
    *outlen = len;
    return s;
}

static json_value_t *make_string_value(char *s, size_t len) {
    json_value_t *v = xcalloc(1, sizeof(*v));
    v->type = JSON_STRING;
    v->u.string.data = s;
    v->u.string.len = len;
    return v;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static json_value_t *parse_string_value(parser_t *ps) {
    size_t len;
    char *raw = parse_string(ps, &len);
    if (!raw) return NULL;
    /* 处理 JSON 转义 */
    char *out = xmalloc(len + 1);
    size_t outlen = 0;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == '\\' && i + 1 < len) {
            char c = raw[++i];
            switch (c) {
            case 'n': out[outlen++] = '\n'; break;
            case 't': out[outlen++] = '\t'; break;
            case 'r': out[outlen++] = '\r'; break;
            case 'b': out[outlen++] = '\b'; break;
            case 'f': out[outlen++] = '\f'; break;
            case '"': out[outlen++] = '"'; break;
            case '\\': out[outlen++] = '\\'; break;
            case '/': out[outlen++] = '/'; break;
            case 'u': {
                if (i + 4 >= len) { free(raw); free(out); return NULL; }
                int cp = 0;
                for (int k = 0; k < 4; k++) {
                    int h = hexval(raw[++i]);
                    if (h < 0) { free(raw); free(out); return NULL; }
                    cp = cp * 16 + h;
                }
                /* 简化：仅支持 BMP，直接存 UTF-8 */
                if (cp < 0x80) out[outlen++] = (char)cp;
                else if (cp < 0x800) {
                    out[outlen++] = (char)(0xc0 | (cp >> 6));
                    out[outlen++] = (char)(0x80 | (cp & 0x3f));
                } else {
                    out[outlen++] = (char)(0xe0 | (cp >> 12));
                    out[outlen++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                    out[outlen++] = (char)(0x80 | (cp & 0x3f));
                }
                break;
            }
            default:
                out[outlen++] = c;
                break;
            }
        } else {
            out[outlen++] = raw[i];
        }
    }
    out[outlen] = '\0';
    free(raw);
    return make_string_value(out, outlen);
}

static json_value_t *parse_value_inner(parser_t *ps) {
    skip_ws(ps);
    if (ps->p >= ps->end) return NULL;
    char c = *ps->p;
    if (c == '"') return parse_string_value(ps);
    if (c == '{') {
        ps->p++;
        json_value_t *obj = xcalloc(1, sizeof(*obj));
        obj->type = JSON_OBJECT;
        skip_ws(ps);
        if (*ps->p == '}') { ps->p++; return obj; }
        while (1) {
            skip_ws(ps);
            size_t klen;
            char *k = parse_string(ps, &klen);
            if (!k) { json_value_free(obj); return NULL; }
            skip_ws(ps);
            if (*ps->p != ':') { free(k); json_value_free(obj); return NULL; }
            ps->p++;
            json_value_t *vv = parse_value(ps);
            if (!vv) { free(k); json_value_free(obj); return NULL; }
            obj->u.object.keys = xrealloc(obj->u.object.keys,
                                         sizeof(char *) * (obj->u.object.count + 1));
            obj->u.object.values = xrealloc(obj->u.object.values,
                                            sizeof(json_value_t *) * (obj->u.object.count + 1));
            obj->u.object.keys[obj->u.object.count] = k;
            obj->u.object.values[obj->u.object.count] = vv;
            obj->u.object.count++;
            skip_ws(ps);
            if (*ps->p == ',') { ps->p++; continue; }
            if (*ps->p == '}') { ps->p++; return obj; }
            json_value_free(obj);
            return NULL;
        }
    }
    if (c == '[') {
        ps->p++;
        json_value_t *arr = xcalloc(1, sizeof(*arr));
        arr->type = JSON_ARRAY;
        skip_ws(ps);
        if (*ps->p == ']') { ps->p++; return arr; }
        while (1) {
            json_value_t *vv = parse_value(ps);
            if (!vv) { json_value_free(arr); return NULL; }
            arr->u.array.items = xrealloc(arr->u.array.items,
                                          sizeof(json_value_t *) * (arr->u.array.count + 1));
            arr->u.array.items[arr->u.array.count++] = vv;
            skip_ws(ps);
            if (*ps->p == ',') { ps->p++; continue; }
            if (*ps->p == ']') { ps->p++; return arr; }
            json_value_free(arr);
            return NULL;
        }
    }
    if (c == 't' && ps->end - ps->p >= 4 && memcmp(ps->p, "true", 4) == 0) {
        ps->p += 4;
        json_value_t *v = xcalloc(1, sizeof(*v));
        v->type = JSON_BOOL;
        v->u.boolean = 1;
        return v;
    }
    if (c == 'f' && ps->end - ps->p >= 5 && memcmp(ps->p, "false", 5) == 0) {
        ps->p += 5;
        json_value_t *v = xcalloc(1, sizeof(*v));
        v->type = JSON_BOOL;
        v->u.boolean = 0;
        return v;
    }
    if (c == 'n' && ps->end - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        json_value_t *v = xcalloc(1, sizeof(*v));
        v->type = JSON_NULL;
        return v;
    }
    /* number */
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *endp;
        double d = strtod(ps->p, &endp);
        if (endp == ps->p) {
            snprintf(ps->err, sizeof(ps->err),
                     "invalid character '%c' at line %d", c, ps->lineno);
            return NULL;
        }
        ps->p = endp;
        json_value_t *v = xcalloc(1, sizeof(*v));
        v->type = JSON_NUMBER;
        v->u.number = d;
        return v;
    }
    snprintf(ps->err, sizeof(ps->err),
             "unexpected character '%c' at line %d", c, ps->lineno);
    return NULL;
}

static json_value_t *parse_value(parser_t *ps) { return parse_value_inner(ps); }

json_value_t *json_parse(const char *input, size_t len) {
    parser_t ps = { input, input + len, 1, 0, "" };
    json_value_t *v = parse_value(&ps);
    if (!v) {
        fprintf(stderr, "json_parse: %s\n", ps.err);
        return NULL;
    }
    skip_ws(&ps);
    if (ps.p < ps.end) {
        fprintf(stderr, "json_parse: trailing garbage at line %d\n", ps.lineno);
        json_value_free(v);
        return NULL;
    }
    return v;
}

/* === pretty-print === */

static void print_indent(FILE *fp, int n) {
    for (int i = 0; i < n; i++) fputc(' ', fp);
}

static void pretty_value(const json_value_t *v, FILE *fp, int indent, int level) {
    switch (v->type) {
    case JSON_NULL:   fputs("null", fp); break;
    case JSON_BOOL:   fputs(v->u.boolean ? "true" : "false", fp); break;
    case JSON_NUMBER: fprintf(fp, "%.17g", v->u.number); break;
    case JSON_STRING: {
        fputc('"', fp);
        for (size_t i = 0; i < v->u.string.len; i++) {
            unsigned char c = (unsigned char)v->u.string.data[i];
            if (c == '"') fputs("\\\"", fp);
            else if (c == '\\') fputs("\\\\", fp);
            else if (c == '\n') fputs("\\n", fp);
            else if (c == '\t') fputs("\\t", fp);
            else if (c < 0x20) fprintf(fp, "\\u%04x", c);
            else fputc(c, fp);
        }
        fputc('"', fp);
        break;
    }
    case JSON_ARRAY:
        if (v->u.array.count == 0) { fputs("[]", fp); break; }
        fputs("[\n", fp);
        for (size_t i = 0; i < v->u.array.count; i++) {
            print_indent(fp, indent * (level + 1));
            pretty_value(v->u.array.items[i], fp, indent, level + 1);
            fputs(i + 1 < v->u.array.count ? ",\n" : "\n", fp);
        }
        print_indent(fp, indent * level);
        fputc(']', fp);
        break;
    case JSON_OBJECT:
        if (v->u.object.count == 0) { fputs("{}", fp); break; }
        fputs("{\n", fp);
        for (size_t i = 0; i < v->u.object.count; i++) {
            print_indent(fp, indent * (level + 1));
            fputc('"', fp);
            fputs(v->u.object.keys[i], fp);
            fputs("\": ", fp);
            pretty_value(v->u.object.values[i], fp, indent, level + 1);
            fputs(i + 1 < v->u.object.count ? ",\n" : "\n", fp);
        }
        print_indent(fp, indent * level);
        fputc('}', fp);
        break;
    }
}

void json_pretty(const json_value_t *v, FILE *fp, int indent) {
    if (!v) return;
    pretty_value(v, fp, indent > 0 ? indent : 2, 0);
    fputc('\n', fp);
}

void json_compact(const json_value_t *v, FILE *fp) {
    pretty_value(v, fp, 0, 0);
}

char *json_escape_string(const char *s, size_t len) {
    /* 简化估算 */
    char *r = xmalloc(len * 2 + 4);
    size_t j = 0;
    r[j++] = '"';
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') { r[j++] = '\\'; r[j++] = '"'; }
        else if (c == '\\') { r[j++] = '\\'; r[j++] = '\\'; }
        else r[j++] = c;
    }
    r[j++] = '"';
    r[j] = '\0';
    return r;
}

void json_value_free(json_value_t *v) {
    if (!v) return;
    switch (v->type) {
    case JSON_STRING:
        free(v->u.string.data);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < v->u.array.count; i++) {
            json_value_free(v->u.array.items[i]);
        }
        free(v->u.array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < v->u.object.count; i++) {
            free(v->u.object.keys[i]);
            json_value_free(v->u.object.values[i]);
        }
        free(v->u.object.keys);
        free(v->u.object.values);
        break;
    default: break;
    }
    free(v);
}

const char *json_get_string(const json_value_t *v, const char *key) {
    if (!v || v->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < v->u.object.count; i++) {
        if (strcmp(v->u.object.keys[i], key) == 0) {
            return v->u.object.values[i]->u.string.data;
        }
    }
    return NULL;
}

double json_get_number(const json_value_t *v, const char *key, double def) {
    if (!v || v->type != JSON_OBJECT) return def;
    for (size_t i = 0; i < v->u.object.count; i++) {
        if (strcmp(v->u.object.keys[i], key) == 0) {
            return v->u.object.values[i]->u.number;
        }
    }
    return def;
}
