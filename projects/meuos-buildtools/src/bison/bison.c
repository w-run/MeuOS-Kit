/*
 * bison.c — bison subcommand for meuos-buildtools (skeleton)
 *
 * Phase 6d: LALR(1) parser generator.
 * Currently a stub that prints help and exits.
 *
 * Copyright (C) MeuOS Project
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Help text
 * ================================================================ */

static void print_help(void)
{
    printf("Usage: bison [OPTION]... FILE...\n");
    printf("GNU bison compatible LALR(1) parser generator (minimal subset).\n");
    printf("\n");
    printf("Options:\n");
    printf("  --help        display this help and exit\n");
    printf("\n");
    printf("Not yet implemented.\n");
}

/* ================================================================
 * bison_main — entry point called from buildtools dispatch
 * ================================================================ */

int bison_main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
    }

    fprintf(stderr, "bison: not yet implemented\n");
    fprintf(stderr, "Run 'bison --help' for usage information.\n");
    return 1;
}