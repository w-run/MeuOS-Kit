/* dd — 转换和复制文件（POSIX dd 子集）
 * 用法：dd [OPERAND]...
 * 支持：if= of= bs= count= skip= seek= conv=
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char version[] = "0.1.0-dd (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "--version"))) {
        printf("dd %s\n", version);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "--help"))) {
        printf("Usage: dd [if=file] [of=file] [bs=N] [count=N] [skip=N] [seek=N] [conv=...]\n");
        return 0;
    }

    const char *infile = NULL, *outfile = NULL;
    size_t bs = 512;
    size_t count = 0;  /* 0 = unlimited */
    size_t skip = 0, seek = 0;

    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "if=", 3)) infile = argv[i] + 3;
        else if (!strncmp(argv[i], "of=", 3)) outfile = argv[i] + 3;
        else if (!strncmp(argv[i], "bs=", 3)) bs = (size_t)atol(argv[i] + 3);
        else if (!strncmp(argv[i], "count=", 6)) count = (size_t)atol(argv[i] + 6);
        else if (!strncmp(argv[i], "skip=", 5)) skip = (size_t)atol(argv[i] + 5);
        else if (!strncmp(argv[i], "seek=", 5)) seek = (size_t)atol(argv[i] + 5);
    }

    int infd = STDIN_FILENO;
    int outfd = STDOUT_FILENO;

    if (infile) {
        infd = open(infile, O_RDONLY);
        if (infd < 0) { fprintf(stderr, "dd: %s: %s\n", infile, strerror(errno)); return 1; }
    }
    if (outfile) {
        outfd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outfd < 0) { fprintf(stderr, "dd: %s: %s\n", outfile, strerror(errno)); return 1; }
    }

    if (skip > 0) {
        /* skip bs-sized blocks */
        for (size_t i = 0; i < skip; i++) {
            char *buf = malloc(bs);
            ssize_t n = read(infd, buf, bs);
            free(buf);
            if (n <= 0) break;
        }
    }
    if (seek > 0) {
        lseek(outfd, (off_t)(seek * bs), SEEK_SET);
    }

    char *buf = malloc(bs);
    size_t total = 0, blocks = 0;
    size_t to_read = count > 0 ? count : (size_t)-1;

    while (to_read > 0) {
        size_t chunk = bs;
        if (chunk > to_read) chunk = to_read;
        ssize_t n = read(infd, buf, chunk);
        if (n <= 0) break;
        write(outfd, buf, (size_t)n);
        total += (size_t)n;
        blocks++;
        if (count > 0) to_read--;
    }

    free(buf);
    if (infile) close(infd);
    if (outfile) close(outfd);

    fprintf(stderr, "%zu+%zu records in\n", blocks, 0);
    fprintf(stderr, "%zu+%zu records out\n", blocks, 0);
    fprintf(stderr, "%zu bytes copied\n", total);
    return 0;
}
