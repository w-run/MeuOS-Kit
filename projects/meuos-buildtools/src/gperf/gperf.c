/*
 * gperf.c — minimal GNU gperf-compatible perfect hash function generator
 *
 * Reads keyword lists from stdin and generates a C source file on stdout
 * containing perfect_hash() and in_word_set().
 *
 * Compile: cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -o build/gperf src/gperf.c
 *
 * Algorithm:
 *   1. Compute djb2 hash for each keyword.
 *   2. Find smallest table size M ≥ N where all (djb2(k) % M) are unique.
 *   3. Build a lookup table mapping M-wide hash → keyword index.
 *   4. If M gets too large, try seeding djb2 differently.
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- tunables ---------- */
#define MAX_KEYWORDS  100
#define MAX_KW_LEN    256
#define MAX_LINE_LEN  4096
#define TABLE_SEARCH  (MAX_KEYWORDS * 50 + 1000)

/* ---------- globals ---------- */
static const char  *wordlist[MAX_KEYWORDS];
static int          kw_count;
static unsigned int djb2_vals[MAX_KEYWORDS];
static int          min_len = 999;
static int          max_len;

/* output identifiers */
static char lookup_name[128] = "in_word_set";
static char hash_name[128]   = "perfect_hash";
static int  has_struct_type;

/* ---------- djb2 string hash ---------- */
static unsigned int
djb2(const char *s, unsigned int len)
{
    unsigned int h = 5381;
    for (unsigned int i = 0; i < len; i++)
        h = h * 33 + (unsigned char)s[i];
    return h;
}

/* ---------- input parser ---------- */
static int
parse_input(void)
{
    char line[MAX_LINE_LEN];
    int  in_keyword_section = 0;

    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        /* ---- directive section ---- */
        if (!in_keyword_section && line[0] == '%') {
            if (line[1] == '#')                  /* %# comment */
                continue;
            if (strcmp(line, "%%") == 0) {       /* end of directives */
                in_keyword_section = 1;
                break;
            }
            if (strncmp(line, "%define", 7) == 0) {
                char var[128], val[128];
                if (sscanf(line + 7, "%127s %127s", var, val) == 2) {
                    if      (strcmp(var, "lookup-function-name") == 0)
                        snprintf(lookup_name, sizeof lookup_name, "%s", val);
                    else if (strcmp(var, "hash-function-name") == 0)
                        snprintf(hash_name,   sizeof hash_name,   "%s", val);
                }
            } else if (strcmp(line, "%struct-type") == 0) {
                has_struct_type = 1;
            }
            /* other directives are silently accepted */
            continue;
        }

        /* ---- keywords (no %% prefix) ---- */
        char *comma = strchr(line, ',');
        if (comma) *comma = '\0';                /* drop parameters */

        /* trim trailing space */
        char *e = line + strlen(line) - 1;
        while (e >= line && isspace((unsigned char)*e)) *e-- = '\0';

        if (line[0] == '\0') continue;

        if (kw_count >= MAX_KEYWORDS) {
            fprintf(stderr, "error: too many keywords (max %d)\n",
                    MAX_KEYWORDS);
            return -1;
        }

        wordlist[kw_count] = strdup(line);
        if (!wordlist[kw_count]) {
            fprintf(stderr, "error: out of memory\n");
            return -1;
        }
        unsigned int l = (unsigned int)strlen(line);
        if ((int)l < min_len) min_len = (int)l;
        if ((int)l > max_len) max_len = (int)l;
        kw_count++;
    }

    /* ---- keywords after %% ---- */
    if (in_keyword_section) {
        char kw[MAX_LINE_LEN];
        while (fgets(kw, sizeof kw, stdin)) {
            size_t len = strlen(kw);
            while (len > 0 && (kw[len - 1] == '\n' || kw[len - 1] == '\r'))
                kw[--len] = '\0';
            if (len == 0) continue;

            char *comma = strchr(kw, ',');
            if (comma) *comma = '\0';

            char *end = kw + strlen(kw) - 1;
            while (end >= kw && isspace((unsigned char)*end)) *end-- = '\0';
            if (kw[0] == '\0') continue;

            if (kw_count >= MAX_KEYWORDS) {
                fprintf(stderr, "error: too many keywords\n");
                return -1;
            }
            wordlist[kw_count] = strdup(kw);
            unsigned int l = (unsigned int)strlen(kw);
            if ((int)l < min_len) min_len = (int)l;
            if ((int)l > max_len) max_len = (int)l;
            kw_count++;
        }
    }

    return kw_count;
}

/* ---------- perfect hash search ---------- */
/*
 * Find a table size M (≥ N) where (djb2(k) % M) is unique for all keywords.
 * If the basic search fails, re-seed djb2 with up to 1000 different seeds.
 *
 * On success fills *table_size, *lookup, and returns 0.
 * On failure returns -1.
 */
static int
find_perfect_hash(unsigned int *table_size, unsigned int **lookup)
{
    /* Phase 1 — standard djb2 */
    for (int i = 0; i < kw_count; i++)
        djb2_vals[i] = djb2(wordlist[i],
                             (unsigned int)strlen(wordlist[i]));

    int found = 0;

    for (unsigned int m = (unsigned int)kw_count;
         m <= (unsigned int)kw_count * 50 + 1000;
         m++)
    {
        unsigned int *used = calloc(m, sizeof (unsigned int));
        if (!used) return -1;

        found = 1;
        for (int i = 0; i < kw_count; i++) {
            unsigned int slot = djb2_vals[i] % m;
            if (used[slot]) { found = 0; break; }
            used[slot] = 1;
        }
        free(used);
        if (found) {
            *table_size = m;
            goto build_lookup;
        }
    }

    /* Phase 2 — try different seeds */
    for (unsigned int seed = 0; seed < 1000 && !found; seed++) {
        for (int i = 0; i < kw_count; i++) {
            unsigned int h = seed;
            const char *s = wordlist[i];
            unsigned int l = (unsigned int)strlen(s);
            for (unsigned int j = 0; j < l; j++)
                h = h * 33 + (unsigned char)s[j];
            djb2_vals[i] = h;
        }

        for (unsigned int m = (unsigned int)kw_count;
             m <= (unsigned int)kw_count + 100 && !found;
             m++)
        {
            unsigned int *used = calloc(m, sizeof (unsigned int));
            if (!used) return -1;

            found = 1;
            for (int i = 0; i < kw_count; i++) {
                unsigned int slot = djb2_vals[i] % m;
                if (used[slot]) { found = 0; break; }
                used[slot] = 1;
            }
            free(used);
            if (found) *table_size = m;
        }
    }

    if (!found) {
        fprintf(stderr, "error: could not find perfect hash\n");
        return -1;
    }

build_lookup:
    *lookup = calloc(*table_size, sizeof (unsigned int));
    if (!*lookup) return -1;
    /* sentinel = kw_count means "no keyword at this slot" */
    for (unsigned int i = 0; i < *table_size; i++)
        (*lookup)[i] = (unsigned int)kw_count;

    for (int i = 0; i < kw_count; i++) {
        unsigned int slot = djb2_vals[i] % *table_size;
        (*lookup)[slot] = (unsigned int)i;
    }

    return 0;
}

/* ---------- code generation ---------- */
static void
emit_code(unsigned int table_size, const unsigned int *lookup)
{
    /* header */
    printf("/* C code produced by gperf (minimal replacement) */\n");
    printf("#include <string.h>\n");
    printf("#if !((' ' == 32) && ('!' == 33) && ('\"' == 34)"
           " && ('#' == 35) && ('%%' == 37) && ('&' == 38))\n"
           "#error \"ASCII character set required\"\n"
           "#endif\n\n");

    printf("#define TOTAL_KEYWORDS %-4d\n", kw_count);
    printf("#define MIN_WORD_LENGTH %-4d\n", min_len);
    printf("#define MAX_WORD_LENGTH %-4d\n", max_len);
    printf("#define MAX_HASH_VALUE  %-4u\n\n", table_size - 1);

    /* word list */
    printf("static const unsigned char %s_length_table[] =\n{",
           lookup_name);
    for (int i = 0; i < kw_count; i++) {
        if (i % 10 == 0) printf("\n  ");
        printf("%3u,", (unsigned int)strlen(wordlist[i]));
    }
    printf("\n};\n\n");

    printf("static const char *%s_word_list[] =\n{", lookup_name);
    for (int i = 0; i < kw_count; i++) {
        if (i % 4 == 0) printf("\n  ");
        printf("\"%s\", ", wordlist[i]);
    }
    printf("\n};\n\n");

    /* hash lookup table */
    printf("static const unsigned char %s_lookup[] =\n{", lookup_name);
    for (unsigned int i = 0; i < table_size; i++) {
        if (i % 10 == 0) printf("\n  ");
        printf("%3u,", lookup[i]);
    }
    printf("\n};\n\n");

    /* hash function */
    printf("static unsigned int\n"
           "%s(const char *str, unsigned int len)\n"
           "{\n"
           "  unsigned int h = 5381;\n"
           "  unsigned int i;\n"
           "  for (i = 0; i < len; i++)\n"
           "    h = h * 33 + (unsigned char)str[i];\n"
           "  return h %% %u;\n"
           "}\n\n",
           hash_name, table_size);

    /* lookup function */
    if (has_struct_type) {
        /* struct pointer return */
        printf("const char *\n"
               "%s(const char *str, unsigned int len)\n"
               "{\n"
               "  if (len < %d || len > %d)\n"
               "    return 0;\n"
               "  unsigned int h = %s(str, len);\n"
               "  unsigned int idx = %s_lookup[h];\n"
               "  if (idx >= %d)\n"
               "    return 0;\n"
               "  if (len != %s_length_table[idx])\n"
               "    return 0;\n"
               "  if (strcmp(str, %s_word_list[idx]) == 0)\n"
               "    return %s_word_list[idx];\n"
               "  return 0;\n"
               "}\n",
               lookup_name, min_len, max_len,
               hash_name, lookup_name, kw_count,
               lookup_name, lookup_name, lookup_name);
    } else {
        /* char * return */
        printf("const char *\n"
               "%s(const char *str, unsigned int len)\n"
               "{\n"
               "  if (len < %d || len > %d)\n"
               "    return 0;\n"
               "  unsigned int h = %s(str, len);\n"
               "  unsigned int idx = %s_lookup[h];\n"
               "  if (idx >= %d)\n"
               "    return 0;\n"
               "  if (len != %s_length_table[idx])\n"
               "    return 0;\n"
               "  if (strcmp(str, %s_word_list[idx]) == 0)\n"
               "    return %s_word_list[idx];\n"
               "  return 0;\n"
               "}\n",
               lookup_name, min_len, max_len,
               hash_name, lookup_name, kw_count,
               lookup_name, lookup_name, lookup_name);
    }
}

/* ---------- help ---------- */
static void print_gperf_help(void)
{
    printf("Usage: gperf [OPTION]...\n");
    printf("Minimal GNU gperf-compatible perfect hash function generator.\n");
    printf("\n");
    printf("Options:\n");
    printf("  --help        display this help and exit\n");
    printf("\n");
    printf("Reads keyword list from stdin, generates C code on stdout.\n");
}

/* ---------- main ---------- */
int
gperf_main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_gperf_help();
            return 0;
        }
    }

    if (parse_input() <= 0) {
        if (kw_count == 0)
            fprintf(stderr, "error: no keywords\n");
        return 1;
    }

    unsigned int       table_size;
    unsigned int      *lookup = NULL;

    if (find_perfect_hash(&table_size, &lookup) != 0)
        return 1;

    emit_code(table_size, lookup);
    free(lookup);
    return 0;
}
