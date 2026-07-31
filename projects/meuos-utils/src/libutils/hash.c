/* libutils/hash.c — 简单字符串哈希表 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/hash.h"
#include "meuos/utils.h"

#define HASH_INITIAL_SIZE 64

typedef struct hash_entry {
    char *key;
    struct hash_entry *next;
} hash_entry_t;

struct hash_set {
    hash_entry_t **buckets;
    size_t nbuckets;
    size_t count;
};

/* djb2 字符串哈希（公共域） */
static unsigned long hash_str(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = *s++)) h = ((h << 5) + h) + (unsigned char)c;
    return h;
}

hash_set_t *hash_set_new(void) {
    hash_set_t *s = xcalloc(1, sizeof(*s));
    s->nbuckets = HASH_INITIAL_SIZE;
    s->buckets = xcalloc(s->nbuckets, sizeof(hash_entry_t *));
    return s;
}

static void hash_set_insert_internal(hash_set_t *s, const char *key,
                                     int copy, int *added) {
    unsigned long h = hash_str(key) % s->nbuckets;
    hash_entry_t *e = s->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            *added = 0;
            return;
        }
        e = e->next;
    }
    e = xcalloc(1, sizeof(*e));
    if (copy) {
        e->key = xstrdup(key);
    }
    e->next = s->buckets[h];
    s->buckets[h] = e;
    s->count++;
    *added = 1;
}

int hash_set_add(hash_set_t *s, const char *key) {
    int added = 0;
    hash_set_insert_internal(s, key, 1, &added);
    return added;
}

int hash_set_has(const hash_set_t *s, const char *key) {
    if (!s) return 0;
    unsigned long h = hash_str(key) % s->nbuckets;
    hash_entry_t *e = s->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) return 1;
        e = e->next;
    }
    return 0;
}

size_t hash_set_count(const hash_set_t *s) {
    return s ? s->count : 0;
}

void hash_set_free(hash_set_t *s) {
    if (!s) return;
    for (size_t i = 0; i < s->nbuckets; i++) {
        hash_entry_t *e = s->buckets[i];
        while (e) {
            hash_entry_t *next = e->next;
            free(e->key);
            free(e);
            e = next;
        }
    }
    free(s->buckets);
    free(s);
}
