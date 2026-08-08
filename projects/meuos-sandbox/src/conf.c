#include "mbox/conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void conf_init(mbox_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->timeout = -1;
}

/*
 * Minimal JSON key-value parser for mbox.conf.
 * Only handles flat string/number/bool values — no nested objects/arrays.
 * If any key is a nested object/array, it is silently skipped.
 */
int conf_load(const char *path, mbox_config *cfg) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (n == 0) return -1;
    buf[n] = '\0';

    const char *p = buf;

    /* find opening { */
    while (*p && *p != '{') p++;
    if (!*p) return -1;
    p++; /* skip { */

    while (*p) {
        /* skip whitespace, comma, semicolon, newlines */
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (*p == '}' || !*p) break;

        /* read quoted key */
        if (*p != '"') break;
        p++; /* skip opening " */
        char key[128];
        int ki = 0;
        while (*p && *p != '"' && ki < (int)sizeof(key) - 1) {
            if (*p == '\\' && *(p+1)) { p++; }
            key[ki++] = *p++;
        }
        key[ki] = '\0';
        if (*p == '"') p++; /* skip closing " */

        /* skip colon */
        while (*p && (isspace((unsigned char)*p) || *p == ':')) p++;

        /* skip nested object/array entirely */
        if (*p == '{' || *p == '[') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '{' || *p == '[') depth++;
                else if (*p == '}' || *p == ']') depth--;
                if (*p == '\\' && *(p+1)) p++;
                p++;
            }
            continue;
        }

        /* ---- string value ---- */
        if (*p == '"') {
            p++;
            char val[512];
            int vi = 0;
            while (*p && *p != '"' && vi < (int)sizeof(val) - 1) {
                if (*p == '\\' && *(p+1)) {
                    p++;
                    if (*p == 'n') val[vi++] = '\n';
                    else if (*p == 't') val[vi++] = '\t';
                    else val[vi++] = *p;
                } else {
                    val[vi++] = *p;
                }
                p++;
            }
            val[vi] = '\0';
            if (*p == '"') p++;

            if (strcmp(key, "arch") == 0)
                snprintf(cfg->arch, sizeof(cfg->arch), "%.31s", val);
            else if (strcmp(key, "net") == 0)
                snprintf(cfg->net, sizeof(cfg->net), "%.31s", val);
            else if (strcmp(key, "timeout") == 0)
                cfg->timeout = atoi(val);
            else if (strcmp(key, "mcp_port") == 0)
                cfg->mcp_port = atoi(val);
            else if (strcmp(key, "webpty_port") == 0)
                cfg->webpty_port = atoi(val);

        /* ---- number value ---- */
        } else if (isdigit((unsigned char)*p) || *p == '-') {
            char num[32];
            int ni = 0;
            while ((isdigit((unsigned char)*p) || *p == '-') && ni < 31)
                num[ni++] = *p++;
            num[ni] = '\0';

            if (strcmp(key, "timeout") == 0)
                cfg->timeout = atoi(num);
            else if (strcmp(key, "mcp_port") == 0)
                cfg->mcp_port = atoi(num);
            else if (strcmp(key, "webpty_port") == 0)
                cfg->webpty_port = atoi(num);

        /* ---- boolean value ---- */
        } else if (strncmp(p, "true", 4) == 0) {
            if (strcmp(key, "webpty_readonly") == 0)
                cfg->webpty_readonly = 1;
            p += 4;
        } else if (strncmp(p, "false", 5) == 0) {
            if (strcmp(key, "webpty_readonly") == 0)
                cfg->webpty_readonly = 0;
            p += 5;
        } else {
            break;
        }
    }

    return 0;
}