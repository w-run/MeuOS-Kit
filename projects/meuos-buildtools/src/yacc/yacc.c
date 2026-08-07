/*
 * yacc.c — yacc subcommand for meuos-buildtools (skeleton)
 *
 * yacc is an alias for bison; invokes bison_main().
 *
 * Copyright (C) MeuOS Project
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration — yacc dispatches to bison */
extern int bison_main(int argc, char **argv);

/* ================================================================
 * Help text
 * ================================================================ */

static void print_help(void)
{
    printf("Usage: yacc [OPTION]... FILE...\n");
    printf("yacc compatible LALR(1) parser generator (alias for bison).\n");
    printf("\n");
    printf("Options:\n");
    printf("  --help        display this help and exit\n");
    printf("\n");
    printf("Not yet implemented — delegates to bison.\n");
}

/* ================================================================
 * yacc_main — entry point called from buildtools dispatch
 * ================================================================ */

int yacc_main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
    }

    /* yacc delegates to bison */
    return bison_main(argc, argv);
}