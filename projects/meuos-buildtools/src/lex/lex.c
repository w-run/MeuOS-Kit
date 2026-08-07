/*
 * lex.c — lex subcommand for meuos-buildtools (skeleton)
 *
 * lex is an alias for flex; invokes flex_main().
 *
 * Copyright (C) MeuOS Project
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration — lex dispatches to flex */
extern int flex_main(int argc, char **argv);

/* ================================================================
 * Help text
 * ================================================================ */

static void print_help(void)
{
    printf("Usage: lex [OPTION]... [FILE]...\n");
    printf("POSIX lex compatible lexer generator (alias for flex).\n");
    printf("\n");
    printf("Options:\n");
    printf("  --help        display this help and exit\n");
    printf("\n");
    printf("Not yet implemented — delegates to flex.\n");
}

/* ================================================================
 * lex_main — entry point called from buildtools dispatch
 * ================================================================ */

int lex_main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
    }

    /* lex delegates to flex */
    return flex_main(argc, argv);
}