/* meuos/config.h — YAML 配置解析器公共 API
 *
 * 轻量 YAML 子集解析器，用于 msh 配置和工具配置。
 * 实现见 libutils/config.c。
 */
#ifndef MEUOS_CONFIG_H
#define MEUOS_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 配置值类型 */
typedef enum {
    CFG_NULL,
    CFG_STRING,
    CFG_INT,
    CFG_BOOL,
    CFG_LIST,
    CFG_TABLE,
} cfg_type_t;

/* 配置值节点 */
typedef struct cfg_value {
    cfg_type_t type;
    union {
        struct { char *data; size_t len; } string;
        long   integer;
        int    boolean;
        struct { char **items; size_t count; } list;
        struct { char **keys; struct cfg_value **values; size_t count; } table;
    } u;
} cfg_value_t;

/* 创建空节点 */
cfg_value_t *cfg_new(void);

/* 从字符串解析 YAML */
cfg_value_t *cfg_parse(const char *input, size_t len);

/* 从文件加载 YAML */
cfg_value_t *cfg_load_file(const char *path);

/* 保存到文件（仅支持 TABLE） */
int cfg_save_file(const cfg_value_t *v, const char *path);

/* 释放节点 */
void cfg_value_free(cfg_value_t *v);

/* 按 key 查找（TABLE 类型） */
const cfg_value_t *cfg_get(const cfg_value_t *root, const char *key);

/* 按点分路径查找（如 "a.b.c"） */
const cfg_value_t *cfg_get_path(const cfg_value_t *root, const char *dotpath);

/* 类型安全取值（带默认值） */
const char *cfg_string(const cfg_value_t *v, const char *def);
int cfg_int(const cfg_value_t *v, int def);
int cfg_bool(const cfg_value_t *v, int def);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_CONFIG_H */
