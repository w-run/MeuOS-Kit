/* comm — 比较两个已排序的文件
 * 用法：comm [-1] [-2] [-3] FILE1 FILE2
 * 选项：-1 抑制只在 FILE1 的行, -2 抑制只在 FILE2 的行, -3 抑制共有行
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char version[] = "0.1.0-comm (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("comm %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) { printf("Usage: comm [-1] [-2] [-3] FILE1 FILE2\n"); return 0; }
    int suppress[4] = {0};  /* [1]=only1, [2]=only2, [3]=both */
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            if (*p == '1') suppress[1] = 1;
            else if (*p == '2') suppress[2] = 1;
            else if (*p == '3') suppress[3] = 1;
            else { fprintf(stderr, "comm: invalid option -%c\n", *p); return 2; }
        }
        argi++;
    }
    if (argc - argi < 2) { fprintf(stderr, "comm: needs 2 files\n"); return 2; }
    FILE *f1 = !strcmp(argv[argi], "-") ? stdin : fopen(argv[argi], "r");
    FILE *f2 = !strcmp(argv[argi+1], "-") ? stdin : fopen(argv[argi+1], "r");
    if (!f1) { fprintf(stderr, "comm: %s: %s\n", argv[argi], strerror(errno)); return 1; }
    if (!f2) { fprintf(stderr, "comm: %s: %s\n", argv[argi+1], strerror(errno)); return 1; }

    char *l1 = NULL, *l2 = NULL;
    size_t c1 = 0, c2 = 0;
    ssize_t n1 = getline(&l1, &c1, f1);
    ssize_t n2 = getline(&l2, &c2, f2);

    while (n1 >= 0 || n2 >= 0) {
        if (n1 < 0) {
            if (!suppress[2]) { fputs(l2, stdout); }
            n2 = getline(&l2, &c2, f2);
        } else if (n2 < 0) {
            if (!suppress[1]) { fputs(l1, stdout); }
            n1 = getline(&l1, &c1, f1);
        } else {
            int cmp = strcmp(l1, l2);
            if (cmp == 0) {
                if (!suppress[3]) { fputs(l1, stdout); }
                n1 = getline(&l1, &c1, f1);
                n2 = getline(&l2, &c2, f2);
            } else if (cmp < 0) {
                if (!suppress[1]) { fputs(l1, stdout); }
                n1 = getline(&l1, &c1, f1);
            } else {
                if (!suppress[2]) { fputs(l2, stdout); }
                n2 = getline(&l2, &c2, f2);
            }
        }
    }
    free(l1); free(l2);
    if (f1 != stdin) fclose(f1);
    if (f2 != stdin) fclose(f2);
    return 0;
}
