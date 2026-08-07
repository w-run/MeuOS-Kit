/*
 * m4.c -- m4 subcommand for meuos-buildtools
 *
 * Uses m4_engine library for the macro processing engine.
 * This file handles CLI entry point (file reading, output).
 *
 * Supported options:
 *   --help      Print help and exit
 *   -D name=val Pre-define a macro
 *   -U name     Undefine a macro
 *
 * Copyright (C) MeuOS Project
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../meow/src/libm4/m4_engine.h"

/* ================================================================
 * Help text
 * ================================================================ */

static void print_help(void)
{
    printf("Usage: m4 [OPTION]... [FILE]...\n");
    printf("GNU m4 compatible macro processor (minimal subset).\n");
    printf("\n");
    printf("Options:\n");
    printf("  --help        display this help and exit\n");
    printf("  -D NAME=VAL   define macro NAME as VAL\n");
    printf("  -U NAME       undefine macro NAME\n");
    printf("\n");
    printf("If no FILE is given, or if FILE is '-', read standard input.\n");
    printf("Macro definitions are processed before any input file.\n");
}

/* ================================================================
 * Read all of a FILE* into a heap buffer
 * ================================================================ */

static char *read_all(FILE *fp, size_t *out_len)
{
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    size_t n;
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) { free(buf); return NULL; }
            buf = newbuf;
        }
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

/* ================================================================
 * Parse -D NAME=VAL into m4_define call
 * ================================================================ */

static int parse_define(const char *arg)
{
    /* arg is "NAME=VAL" */
    const char *eq = strchr(arg, '=');
    if (!eq) {
        /* -D NAME without value — define as empty */
        m4_define(arg, "");
        return 0;
    }

    size_t namelen = (size_t)(eq - arg);
    char *name = malloc(namelen + 1);
    if (!name) return -1;
    memcpy(name, arg, namelen);
    name[namelen] = '\0';

    m4_define(name, eq + 1);
    free(name);
    return 0;
}

/* ================================================================
 * m4_main — entry point called from buildtools dispatch
 * ================================================================ */

int m4_main(int argc, char **argv)
{
    int exit_code = 0;

    m4_init();

    /* Parse flags first, collecting file args */
    int file_count = 0;
    char **files = NULL;
    files = malloc(sizeof(char *) * (size_t)(argc > 0 ? argc : 1));
    if (!files) {
        fprintf(stderr, "m4: out of memory\n");
        m4_reset();
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            free(files);
            m4_reset();
            return 0;
        } else if (strcmp(argv[i], "-D") == 0) {
            /* -D NAME=VAL (next arg) */
            if (i + 1 >= argc) {
                fprintf(stderr, "m4: -D requires an argument\n");
                exit_code = 1;
                goto done;
            }
            i++;
            if (parse_define(argv[i]) != 0) {
                fprintf(stderr, "m4: out of memory parsing -D\n");
                exit_code = 1;
                goto done;
            }
        } else if (strncmp(argv[i], "-D", 2) == 0) {
            /* -DNAME=VAL (attached) */
            if (parse_define(argv[i] + 2) != 0) {
                fprintf(stderr, "m4: out of memory parsing -D\n");
                exit_code = 1;
                goto done;
            }
        } else if (strcmp(argv[i], "-U") == 0) {
            /* -U NAME (next arg) */
            if (i + 1 >= argc) {
                fprintf(stderr, "m4: -U requires an argument\n");
                exit_code = 1;
                goto done;
            }
            i++;
            m4_define(argv[i], "");
        } else if (strncmp(argv[i], "-U", 2) == 0) {
            /* -UNAME (attached) */
            m4_define(argv[i] + 2, "");
        } else {
            /* Must be a file argument */
            files[file_count++] = argv[i];
        }
    }

    /* Read all input into a single buffer */
    char *input_buf = NULL;
    size_t input_len = 0;

    if (file_count > 0) {
        /* Concatenate all file arguments using read_all for each */
        size_t cap = 8192, len = 0;
        input_buf = malloc(cap);
        if (!input_buf) {
            fprintf(stderr, "m4: out of memory\n");
            exit_code = 1;
            goto cleanup;
        }

        for (int i = 0; i < file_count; i++) {
            FILE *fp;
            if (strcmp(files[i], "-") == 0) {
                fp = stdin;
                m4_set_file("stdin");
            } else {
                fp = fopen(files[i], "r");
                if (!fp) {
                    fprintf(stderr, "m4: %s: %s\n", files[i], strerror(errno));
                    exit_code = 1;
                    goto cleanup;
                }
                m4_set_file(files[i]);
            }

            size_t n;
            while ((n = fread(input_buf + len, 1, cap - len, fp)) > 0) {
                len += n;
                if (len >= cap) {
                    cap *= 2;
                    char *newbuf = realloc(input_buf, cap);
                    if (!newbuf) {
                        fprintf(stderr, "m4: out of memory\n");
                        free(input_buf);
                        input_buf = NULL;
                        exit_code = 1;
                        goto cleanup;
                    }
                    input_buf = newbuf;
                }
            }
            if (fp != stdin) fclose(fp);
        }
        input_buf[len] = '\0';
        input_len = len;
    } else {
        /* Read stdin */
        input_buf = read_all(stdin, &input_len);
        if (!input_buf) {
            fprintf(stderr, "m4: out of memory\n");
            exit_code = 1;
            goto cleanup;
        }
        m4_set_file("stdin");
    }

    /* Allocate output buffer */
    /* 4MB output buffer should suffice for buildtools use */
    size_t outsz = 4 * 1024 * 1024;
    char *output = malloc(outsz);
    if (!output) {
        fprintf(stderr, "m4: out of memory\n");
        exit_code = 1;
        goto cleanup;
    }
    output[0] = '\0';

    /* Process input */
    exit_code = m4_process(input_buf, output, outsz);

    /* Write output to stdout */
    printf("%s", output);

    free(output);

cleanup:
    free(input_buf);

done:
    free(files);
    m4_reset();
    return exit_code;
}