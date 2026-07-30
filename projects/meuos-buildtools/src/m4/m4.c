/*
 * m4.c -- m4 subcommand for meuos-buildtools
 *
 * Uses m4_engine library for the macro processing engine.
 * This file handles CLI entry point (file reading, output).
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
 * m4_main — entry point called from buildtools dispatch
 * ================================================================ */

int m4_main(int argc, char **argv)
{
    int exit_code = 0;

    m4_init();

    /* Read all input into a single buffer */
    char *input_buf = NULL;
    size_t input_len = 0;

    if (argc > 1) {
        /* Concatenate all file arguments */
        size_t total = 0;
        /* First, compute total size */
        for (int i = 1; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) {
                fprintf(stderr, "m4: %s: %s\n", argv[i], strerror(errno));
                exit_code = 1;
                goto cleanup;
            }
            fseek(fp, 0, SEEK_END);
            total += (size_t)ftell(fp) + 1;
            fclose(fp);
        }
        input_buf = malloc(total + 1);
        if (!input_buf) {
            fprintf(stderr, "m4: out of memory\n");
            exit_code = 1;
            goto cleanup;
        }
        size_t pos = 0;
        for (int i = 1; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) continue;
            size_t n;
            while ((n = fread(input_buf + pos, 1, total - pos, fp)) > 0)
                pos += n;
            fclose(fp);
        }
        input_buf[pos] = '\0';
        input_len = pos;
        m4_set_file(argv[1]);
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
    m4_reset();
    return exit_code;
}
