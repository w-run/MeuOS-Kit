/* msh/var/array.c — bash 风格数组支持
 * 实现：arr=(elem1 elem2) / arr[i]=val / ${arr[i]} / ${arr[@]} / ${#arr[@]}
 * 数组存储为独立的哈希表，不污染环境变量。
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARRAYS 256
#define MAX_ELEMENTS 1024

typedef struct {
    char *name;
    char **elems;
    int count;
    int cap;
} msh_array_t;

static msh_array_t arrays[MAX_ARRAYS];
static int num_arrays = 0;

static msh_array_t *find_array(const char *name) {
    for (int i = 0; i < num_arrays; i++) {
        if (arrays[i].name && strcmp(arrays[i].name, name) == 0)
            return &arrays[i];
    }
    return NULL;
}

static msh_array_t *create_array(const char *name) {
    if (num_arrays >= MAX_ARRAYS) return NULL;
    msh_array_t *a = &arrays[num_arrays++];
    a->name = strdup(name);
    a->elems = NULL;
    a->count = 0;
    a->cap = 0;
    return a;
}

/* Set array element at index */
int msh_array_set(const char *name, int idx, const char *val) {
    msh_array_t *a = find_array(name);
    if (!a) {
        a = create_array(name);
        if (!a) return -1;
    }
    if (idx < 0) return -1;
    if (idx >= MAX_ELEMENTS) return -1;
    
    /* Grow if needed */
    if (idx >= a->cap) {
        int newcap = idx + 16;
        a->elems = realloc(a->elems, newcap * sizeof(char*));
        for (int i = a->cap; i < newcap; i++) a->elems[i] = NULL;
        a->cap = newcap;
    }
    if (a->elems[idx]) free(a->elems[idx]);
    a->elems[idx] = val ? strdup(val) : NULL;
    if (idx + 1 > a->count) a->count = idx + 1;
    return 0;
}

/* Get array element at index, returns NULL if not set */
const char *msh_array_get(const char *name, int idx) {
    msh_array_t *a = find_array(name);
    if (!a || idx < 0 || idx >= a->cap) return NULL;
    return a->elems[idx];
}

/* Get all elements as a space-separated string (for ${arr[*]}) */
char *msh_array_get_all(const char *name, const char *sep) {
    msh_array_t *a = find_array(name);
    if (!a || a->count == 0) return strdup("");
    
    size_t total = 0;
    for (int i = 0; i < a->count; i++) {
        if (a->elems[i]) total += strlen(a->elems[i]) + strlen(sep);
    }
    char *buf = malloc(total + 1);
    buf[0] = '\0';
    int first = 1;
    for (int i = 0; i < a->count; i++) {
        if (a->elems[i]) {
            if (!first) strcat(buf, sep);
            strcat(buf, a->elems[i]);
            first = 0;
        }
    }
    return buf;
}

/* Get count of elements */
int msh_array_count(const char *name) {
    msh_array_t *a = find_array(name);
    if (!a) return 0;
    int count = 0;
    for (int i = 0; i < a->count; i++) {
        if (a->elems[i]) count++;
    }
    return count;
}

/* Check if a name is an array */
int msh_is_array(const char *name) {
    return find_array(name) != NULL;
}

/* Parse "arr=(elem1 elem2)" and set array */
int msh_array_parse_assign(const char *assignment) {
    /* Format: name=(elem1 elem2 ...) */
    const char *eq = strchr(assignment, '=');
    if (!eq) return -1;
    
    /* Check for ( */
    if (eq[1] != '(') return -1;
    
    /* Extract name */
    char name[256];
    size_t namelen = (size_t)(eq - assignment);
    if (namelen >= sizeof(name)) return -1;
    memcpy(name, assignment, namelen);
    name[namelen] = '\0';
    
    /* Validate name (alphanumeric + underscore) */
    for (size_t i = 0; i < namelen; i++) {
        if (!isalnum(name[i]) && name[i] != '_') return -1;
    }
    
    /* Parse elements inside () */
    const char *p = eq + 2; /* skip =( */
    msh_array_t *a = find_array(name);
    if (a) {
        /* Clear existing */
        for (int i = 0; i < a->count; i++) { free(a->elems[i]); a->elems[i] = NULL; }
        a->count = 0;
    } else {
        a = create_array(name);
        if (!a) return -1;
    }
    
    int idx = 0;
    while (*p && *p != ')') {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ')' || *p == '\0') break;
        
        /* Parse element (handle quotes) */
        char elem[4096];
        int el = 0;
        char quote = 0;
        
        while (*p && el < (int)sizeof(elem) - 1) {
            if (quote) {
                if (*p == quote) { quote = 0; p++; continue; }
                elem[el++] = *p++;
            } else {
                if (*p == ')') break;
                if (*p == ' ' || *p == '\t') break;
                if (*p == '"' || *p == '\'') { quote = *p++; continue; }
                elem[el++] = *p++;
            }
        }
        elem[el] = '\0';
        
        /* Skip trailing whitespace before ) */
        while (*p == ' ' || *p == '\t') p++;
        
        msh_array_set(name, idx, elem);
        idx++;
    }
    
    return 0;
}

/* Parse "arr[idx]=val" assignment */
int msh_array_parse_indexed(const char *assignment) {
    /* Format: name[idx]=val */
    const char *lb = strchr(assignment, '[');
    const char *rb = strchr(assignment, ']');
    const char *eq = strchr(assignment, '=');
    
    if (!lb || !rb || !eq || lb >= rb || rb >= eq) return -1;
    
    /* Extract name */
    char name[256];
    size_t namelen = (size_t)(lb - assignment);
    if (namelen >= sizeof(name)) return -1;
    memcpy(name, assignment, namelen);
    name[namelen] = '\0';
    
    /* Extract index */
    char idxbuf[32];
    size_t idxlen = (size_t)(rb - lb - 1);
    if (idxlen >= sizeof(idxbuf)) return -1;
    memcpy(idxbuf, lb + 1, idxlen);
    idxbuf[idxlen] = '\0';
    
    int idx = atoi(idxbuf);
    
    /* Extract value (after =) */
    const char *val = eq + 1;
    /* Strip surrounding quotes */
    char cleanval[4096];
    int vl = 0;
    char quote = 0;
    for (const char *p = val; *p && vl < (int)sizeof(cleanval) - 1; p++) {
        if (quote) {
            if (*p == quote) { quote = 0; continue; }
            cleanval[vl++] = *p;
        } else {
            if (*p == '"' || *p == '\'') { quote = *p; continue; }
            cleanval[vl++] = *p;
        }
    }
    cleanval[vl] = '\0';
    
    return msh_array_set(name, idx, cleanval);
}
