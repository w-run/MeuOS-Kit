/* meuos/hash.h — 简单字符串哈希表
 *
 * 用途：gitignore 跳过集合、ignore patterns、shell 的变量表等。
 * 不是通用库，仅本项目用。
 */
#ifndef MEUOS_HASH_H
#define MEUOS_HASH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hash_set;
typedef struct hash_set hash_set_t;

hash_set_t *hash_set_new(void);
void hash_set_free(hash_set_t *s);

/* 添加字符串（拷贝）。 */
int hash_set_add(hash_set_t *s, const char *key);

/* 查询。 */
int hash_set_has(const hash_set_t *s, const char *key);

/* 元素数 */
size_t hash_set_count(const hash_set_t *s);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_HASH_H */
