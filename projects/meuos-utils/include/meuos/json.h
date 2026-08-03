/* meuos/json.h — 简化 JSON 解析与 pretty-print
 *
 * 设计原则：
 *   - 仅支持 JSON 子集：对象/数组/字符串/数字/布尔/null
 *   - 解析器入口 json_parse(input, len) → json_value_t*
 *   - 输出器 json_pretty(v, fp, indent=2)
 *   - 内存由 json_value_free() 释放
 *
 * 注意：本模块**不**实现 JSON5/JSONC 等扩展。
 */
#ifndef MEUOS_JSON_H
#define MEUOS_JSON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} json_type_t;

struct json_value;
typedef struct json_value json_value_t;

struct json_value {
    json_type_t type;
    union {
        int boolean;
        double number;
        struct {
            char *data;
            size_t len;
        } string;
        struct {
            json_value_t **items;
            size_t count;
        } array;
        struct {
            char **keys;
            json_value_t **values;
            size_t count;
        } object;
    } u;
};

/* 解析。返回 NULL 表示错误（stderr 输出错误位置）。 */
json_value_t *json_parse(const char *input, size_t len);

/* pretty-print 到 fp */
void json_pretty(const json_value_t *v, FILE *fp, int indent);

/* 单行输出 */
void json_compact(const json_value_t *v, FILE *fp);

/* 字符串字面值转 JSON 字符串（含引号）。返回 malloc 的串，调用者 free() */
char *json_escape_string(const char *s, size_t len);

/* 释放 */
void json_value_free(json_value_t *v);

/* 访问工具 */
const char *json_get_string(const json_value_t *v, const char *key);
double json_get_number(const json_value_t *v, const char *key, double def);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_JSON_H */
