/* diff — unified/context diff 工具
 *
 * 使用 LCS DP 算法计算差异，输出标准 unified 或 context 格式。
 *
 * 用法：
 *   diff [options] FILE1 FILE2
 *   -u           unified 格式（默认）
 *   -c           context 格式
 *   -C N         context 格式，N 行上下文
 *   -U N         unified 格式，N 行上下文
 *   --no-color   禁用彩色
 *   --classic    纯文本
 *   --help / --version
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "meuos/color.h"
#include "meuos/utils.h"

#define MAX_LINES 8192

/* === File reading === */
static char **read_lines(const char *path, int *n_out) {
    FILE *fp = stdin;
    if (strcmp(path, "-") != 0) {
        fp = fopen(path, "r");
        if (!fp) { perror(path); return NULL; }
    }
    char **lines = xmalloc(sizeof(char*) * MAX_LINES);
    int n = 0;
    char *line = NULL;
    size_t lcap = 0;
    ssize_t len;
    while ((len = getline(&line, &lcap, fp)) > 0 && n < MAX_LINES) {
        /* Strip trailing newline */
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        lines[n++] = xstrdup(line);
    }
    free(line);
    if (fp != stdin) fclose(fp);
    *n_out = n;
    return lines;
}

static void free_lines(char **lines, int n) {
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
}

/* === LCS DP diff === */
typedef struct {
    char type;  /* '=', '-', '+' */
    char *line;
    int a_line; /* 1-based line number in A (for '=' and '-') */
    int b_line; /* 1-based line number in B (for '=' and '+') */
} diff_op_t;

static diff_op_t *compute_diff(char **A, int na, char **B, int nb, int *nops) {
    /* DP table: dp[i][j] = LCS length of A[0..i-1] and B[0..j-1] */
    int *dp = xmalloc((size_t)(na + 1) * (nb + 1) * sizeof(int));
    for (int i = 0; i <= na; i++) dp[i * (nb + 1)] = 0;
    for (int j = 0; j <= nb; j++) dp[j] = 0;
    
    for (int i = 1; i <= na; i++) {
        for (int j = 1; j <= nb; j++) {
            if (strcmp(A[i-1], B[j-1]) == 0)
                dp[i * (nb + 1) + j] = dp[(i-1) * (nb + 1) + (j-1)] + 1;
            else {
                int a = dp[(i-1) * (nb + 1) + j];
                int b = dp[i * (nb + 1) + (j-1)];
                dp[i * (nb + 1) + j] = (a > b ? a : b);
            }
        }
    }
    
    /* Backtrack to build edit script */
    diff_op_t *ops = xmalloc(sizeof(diff_op_t) * (na + nb + 1));
    int n = 0;
    int i = na, j = nb;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && strcmp(A[i-1], B[j-1]) == 0) {
            ops[n].type = '=';
            ops[n].line = A[i-1];
            ops[n].a_line = i;
            ops[n].b_line = j;
            n++; i--; j--;
        } else if (j > 0 && (i == 0 || dp[i * (nb + 1) + (j-1)] >= dp[(i-1) * (nb + 1) + j])) {
            ops[n].type = '+';
            ops[n].line = B[j-1];
            ops[n].a_line = 0;
            ops[n].b_line = j;
            n++; j--;
        } else if (i > 0) {
            ops[n].type = '-';
            ops[n].line = A[i-1];
            ops[n].a_line = i;
            ops[n].b_line = 0;
            n++; i--;
        } else break;
    }
    
    /* Reverse */
    for (int k = 0; k < n / 2; k++) {
        diff_op_t tmp = ops[k]; ops[k] = ops[n-1-k]; ops[n-1-k] = tmp;
    }
    
    free(dp);
    *nops = n;
    return ops;
}

/* === Unified diff output === */
static void output_unified(diff_op_t *ops, int nops,
                           const char *path_a, const char *path_b,
                           int context, int colored) {
    printf("--- %s\n", path_a);
    printf("+++ %s\n", path_b);
    
    int k = 0;
    while (k < nops) {
        /* Skip equal lines at the beginning */
        while (k < nops && ops[k].type == '=') k++;
        if (k >= nops) break;
        
        /* Find the end of this change block */
        int start = k;
        /* Include context lines before */
        int ctx_start = (start - context > 0) ? start - context : 0;
        /* Adjust to skip leading context that's already been output */
        while (ctx_start < start && ops[ctx_start].type == '=') ctx_start++;
        if (ctx_start < start) ctx_start = start - context;
        if (ctx_start < 0) ctx_start = 0;
        
        int end = k;
        while (end < nops && ops[end].type != '=') end++;
        
        /* Include context lines after */
        int ctx_end = end + context;
        if (ctx_end > nops) ctx_end = nops;
        
        /* Calculate line numbers for hunk header */
        int old_start = 0, old_count = 0;
        int new_start = 0, new_count = 0;
        
        for (int p = ctx_start; p < ctx_end; p++) {
            if (ops[p].type == '=' || ops[p].type == '-') {
                if (old_start == 0) old_start = ops[p].a_line;
                old_count++;
            }
            if (ops[p].type == '=' || ops[p].type == '+') {
                if (new_start == 0) new_start = ops[p].b_line;
                new_count++;
            }
        }
        if (old_start == 0) { old_start = 1; old_count = 0; }
        if (new_start == 0) { new_start = 1; new_count = 0; }
        
        printf("@@ -%d,%d +%d,%d @@\n", old_start, old_count, new_start, new_count);
        
        for (int p = ctx_start; p < ctx_end; p++) {
            if (ops[p].type == '=') {
                printf(" %s\n", ops[p].line);
            } else if (ops[p].type == '-') {
                if (colored) fputs(color_named(1), stdout);
                printf("-%s\n", ops[p].line);
                if (colored) fputs(color_reset(), stdout);
            } else if (ops[p].type == '+') {
                if (colored) fputs(color_named(2), stdout);
                printf("+%s\n", ops[p].line);
                if (colored) fputs(color_reset(), stdout);
            }
        }
        
        k = ctx_end;
    }
}

/* === Context diff output === */
static void output_context(diff_op_t *ops, int nops,
                           const char *path_a, const char *path_b,
                           int context, int colored) {
    printf("*** %s\n", path_a);
    printf("--- %s\n", path_b);
    
    int k = 0;
    while (k < nops) {
        while (k < nops && ops[k].type == '=') k++;
        if (k >= nops) break;
        
        int start = k;
        int ctx_start = (start - context > 0) ? start - context : 0;
        if (ctx_start < 0) ctx_start = 0;
        
        int end = k;
        while (end < nops && ops[end].type != '=') end++;
        int ctx_end = end + context;
        if (ctx_end > nops) ctx_end = nops;
        
        int old_start = 0, old_count = 0;
        int new_start = 0, new_count = 0;
        
        for (int p = ctx_start; p < ctx_end; p++) {
            if (ops[p].type == '=' || ops[p].type == '-') {
                if (old_start == 0) old_start = ops[p].a_line;
                old_count++;
            }
            if (ops[p].type == '=' || ops[p].type == '+') {
                if (new_start == 0) new_start = ops[p].b_line;
                new_count++;
            }
        }
        if (old_start == 0) { old_start = 1; old_count = 0; }
        if (new_start == 0) { new_start = 1; new_count = 0; }
        
        printf("***************\n");
        printf("*** %d,%d\n", old_start, old_start + old_count - 1);
        
        for (int p = ctx_start; p < ctx_end; p++) {
            if (ops[p].type == '=') {
                printf("  %s\n", ops[p].line);
            } else if (ops[p].type == '-') {
                if (colored) fputs(color_named(1), stdout);
                printf("- %s\n", ops[p].line);
                if (colored) fputs(color_reset(), stdout);
            }
            /* '+' lines not shown in old file section */
        }
        
        printf("--- %d,%d\n", new_start, new_start + new_count - 1);
        
        for (int p = ctx_start; p < ctx_end; p++) {
            if (ops[p].type == '=') {
                printf("  %s\n", ops[p].line);
            } else if (ops[p].type == '+') {
                if (colored) fputs(color_named(2), stdout);
                printf("+ %s\n", ops[p].line);
                if (colored) fputs(color_reset(), stdout);
            }
            /* '-' lines not shown in new file section */
        }
        
        k = ctx_end;
    }
}

/* === Main === */
static void usage(void) {
    printf("Usage: %s [OPTIONS] FILE1 FILE2\n", program_name);
    printf("\n");
    printf("Diff tool with unified and context format.\n\n");
    printf("  -u              unified format (default)\n");
    printf("  -c              context format\n");
    printf("  -C N             context format with N lines of context\n");
    printf("  -U N             unified format with N lines of context\n");
    printf("      --no-color  disable color\n");
    printf("      --classic   plain text\n");
    printf("      --help      show this help\n");
    printf("      --version   show version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    color_enable();
    
    int format = 0;  /* 0=unified, 1=context */
    int context = 3; /* default 3 lines of context */
    int colored = 1;
    
    static const struct option longopts[] = {
        { "no-color",  no_argument, NULL, 1000 },
        { "classic",  no_argument, NULL, 1001 },
        { "help",     no_argument, NULL, 'h' },
        { "version",  no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "ucC:U:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'u': format = 0; break;
        case 'c': format = 1; break;
        case 'C': format = 1; context = atoi(optarg); break;
        case 'U': format = 0; context = atoi(optarg); break;
        case 1000: color_disable(); colored = 0; break;
        case 1001: colored = 0; break;
        case 'h': usage(); return 0;
        case 'V': version(); return 0;
        default: return 2;
        }
    }
    
    if (optind + 2 != argc) {
        fprintf(stderr, "%s: need exactly 2 files\n", program_name);
        usage();
        return 2;
    }
    
    const char *path_a = argv[optind];
    const char *path_b = argv[optind + 1];
    
    int na = 0, nb = 0;
    char **A = read_lines(path_a, &na);
    char **B = read_lines(path_b, &nb);
    if (!A || !B) return 1;
    
    int nops = 0;
    diff_op_t *ops = compute_diff(A, na, B, nb, &nops);
    
    if (format == 0)
        output_unified(ops, nops, path_a, path_b, context, colored);
    else
        output_context(ops, nops, path_a, path_b, context, colored);
    
    free(ops);
    free_lines(A, na);
    free_lines(B, nb);
    
    return 0;
}
