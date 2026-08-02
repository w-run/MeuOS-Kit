/* mz — MeuOS 原生压缩/解压工具
 *
 * 封装 meuos-compress (libmz) 库，提供统一的压缩/解压/列表/提取接口。
 *
 * 支持两种格式：
 *   .mz  — 原始压缩流（mz_compress / mz_decompress）
 *   .mxa — 归档容器（mxa_create / mxa_open，含多文件/加密/签名）
 *
 * 用法：
 *   mz c|compress  [-l N] [-o OUT] FILE        压缩文件 → .mz
 *   mz d|decompress [-o OUT] FILE              解压 .mz → 原文件
 *   mz l|list      FILE.mxa                    列出归档内容
 *   mz x|extract   [-o DIR] FILE.mxa [PAT]    提取归档文件
 *   mz t|test      FILE                        测试完整性
 *   mz a|archive   [-l N] OUT.mxa FILES...    创建归档
 *   --help / --version
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mz.h"
#include "mxa.h"

static const char *prog = "mz";

/* === 通用文件读写 === */
static void *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno)); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *data = malloc((size_t)(sz > 0 ? sz : 1));
    if (!data) { fprintf(stderr, "%s: out of memory\n", prog); fclose(f); return NULL; }
    if (sz > 0 && fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "%s: %s: short read\n", prog, path);
        free(data); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)(sz > 0 ? sz : 0);
    return data;
}

static int write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno)); return -1; }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fprintf(stderr, "%s: %s: short write\n", prog, path);
        fclose(f); return -1;
    }
    fclose(f);
    return 0;
}

/* === 命令：compress === */
static int cmd_compress(int argc, char **argv) {
    int level = 6;
    const char *outfile = NULL;
    const char *infile = NULL;
    int oi = 0;
    while (oi < argc && argv[oi][0] == '-') {
        if (strcmp(argv[oi], "-l") == 0 || strcmp(argv[oi], "--level") == 0) {
            if (oi + 1 >= argc) { fprintf(stderr, "%s: -l needs argument\n", prog); return 1; }
            level = atoi(argv[++oi]);
            if (level < 1 || level > 9) { fprintf(stderr, "%s: level must be 1-9\n", prog); return 1; }
            oi++;
        } else if (strcmp(argv[oi], "-o") == 0 || strcmp(argv[oi], "--output") == 0) {
            if (oi + 1 >= argc) { fprintf(stderr, "%s: -o needs argument\n", prog); return 1; }
            outfile = argv[++oi];
            oi++;
        } else {
            fprintf(stderr, "%s: unknown option %s\n", prog, argv[oi]);
            return 2;
        }
    }
    if (oi >= argc) { fprintf(stderr, "%s: compress: missing input file\n", prog); return 2; }
    infile = argv[oi];

    size_t in_len = 0;
    void *in_data = read_file(infile, &in_len);
    if (!in_data) return 1;

    void *out_data = NULL;
    size_t out_len = 0;
    int rc = mz_compress(in_data, in_len, &out_data, &out_len, MZ_CODEC_MEUOS, level);
    free(in_data);
    if (rc <= 0) {
        fprintf(stderr, "%s: compression failed: %s\n", prog, mz_strerror(rc));
        return 1;
    }

    char auto_out[4096];
    if (!outfile) {
        snprintf(auto_out, sizeof(auto_out), "%s.mz", infile);
        outfile = auto_out;
    }

    if (write_file(outfile, out_data, out_len) != 0) {
        free(out_data);
        return 1;
    }
    printf("compressed: %s -> %s (%zu -> %zu, %.1f%%)\n",
           infile, outfile, in_len, out_len,
           in_len > 0 ? 100.0 * (1.0 - (double)out_len / (double)in_len) : 0.0);
    free(out_data);
    return 0;
}

/* === 命令：decompress === */
static int cmd_decompress(int argc, char **argv) {
    const char *outfile = NULL;
    const char *infile = NULL;
    int oi = 0;
    while (oi < argc && argv[oi][0] == '-') {
        if (strcmp(argv[oi], "-o") == 0 || strcmp(argv[oi], "--output") == 0) {
            if (oi + 1 >= argc) { fprintf(stderr, "%s: -o needs argument\n", prog); return 1; }
            outfile = argv[++oi];
            oi++;
        } else {
            fprintf(stderr, "%s: unknown option %s\n", prog, argv[oi]);
            return 2;
        }
    }
    if (oi >= argc) { fprintf(stderr, "%s: decompress: missing input file\n", prog); return 2; }
    infile = argv[oi];

    size_t in_len = 0;
    void *in_data = read_file(infile, &in_len);
    if (!in_data) return 1;

    void *out_data = NULL;
    size_t out_len = 0;
    /* 尝试 MEUOS combo 格式（mz_compress 输出），再回退到 raw LZ77 */
    int rc = mz_decompress(in_data, in_len, &out_data, &out_len, MZ_CODEC_MEUOS);
    if (rc <= 0) {
        free(out_data); out_data = NULL; out_len = 0;
        rc = mz_decompress(in_data, in_len, &out_data, &out_len, MZ_CODEC_LZ77);
    }
    free(in_data);
    if (rc <= 0) {
        fprintf(stderr, "%s: decompression failed: %s\n", prog, mz_strerror(rc));
        return 1;
    }

    char auto_out[4096];
    if (!outfile) {
        size_t ilen = strlen(infile);
        if (ilen > 3 && strcmp(infile + ilen - 3, ".mz") == 0) {
            memcpy(auto_out, infile, ilen - 3);
            auto_out[ilen - 3] = '\0';
        } else {
            snprintf(auto_out, sizeof(auto_out), "%s.out", infile);
        }
        outfile = auto_out;
    }

    if (write_file(outfile, out_data, out_len) != 0) {
        free(out_data);
        return 1;
    }
    printf("decompressed: %s -> %s (%zu -> %zu bytes)\n",
           infile, outfile, in_len, out_len);
    free(out_data);
    return 0;
}

/* === 命令：archive (create mxa) === */
static int cmd_archive(int argc, char **argv) {
    int level = 6;
    int oi = 0;
    while (oi < argc && argv[oi][0] == '-') {
        if (strcmp(argv[oi], "-l") == 0 || strcmp(argv[oi], "--level") == 0) {
            if (oi + 1 >= argc) { fprintf(stderr, "%s: -l needs argument\n", prog); return 1; }
            level = atoi(argv[++oi]);
            if (level < 1 || level > 9) { fprintf(stderr, "%s: level must be 1-9\n", prog); return 1; }
            oi++;
        } else {
            fprintf(stderr, "%s: unknown option %s\n", prog, argv[oi]);
            return 2;
        }
    }
    if (oi + 1 >= argc) {
        fprintf(stderr, "%s: archive: usage: mz a [-l N] OUT.mxa FILES...\n", prog);
        return 2;
    }
    const char *archive_path = argv[oi++];
    int nfiles = argc - oi;

    struct mxa_params params;
    memset(&params, 0, sizeof(params));
    params.level = level;

    void *ctx = NULL;
    int rc = mxa_create(&ctx, &params);
    if (rc != MXA_OK) {
        fprintf(stderr, "%s: mxa_create: %s\n", prog, mxa_strerror(rc));
        return 1;
    }

    for (int i = 0; i < nfiles; i++) {
        const char *path = argv[oi + i];
        size_t fsize = 0;
        void *fdata = read_file(path, &fsize);
        if (!fdata && fsize == 0) {
            /* 空文件允许 */
            fdata = malloc(1);
        }
        if (!fdata) {
            return 1;
        }
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        rc = mxa_add_file(ctx, name, fdata, fsize, 0644, 0, 0, 0);
        free(fdata);
        if (rc != MXA_OK) {
            fprintf(stderr, "%s: mxa_add_file('%s'): %s\n", prog, name, mxa_strerror(rc));
            return 1;
        }
    }

    void *archive_data = NULL;
    size_t archive_len = 0;
    rc = mxa_finish(ctx, &archive_data, &archive_len);
    /* mxa_finish 分配新缓冲区给 archive_data。
     * 写上下文内部缓冲在进程退出时自动回收，无需 mxa_close（仅用于读上下文）。 */
    if (rc != MXA_OK) {
        fprintf(stderr, "%s: mxa_finish: %s\n", prog, mxa_strerror(rc));
        return 1;
    }

    if (write_file(archive_path, archive_data, archive_len) != 0) {
        free(archive_data);
        return 1;
    }
    printf("created: %s (%zu bytes, %d file(s))\n", archive_path, archive_len, nfiles);
    free(archive_data);
    return 0;
}

/* === 命令：list (mxa) === */
static int cmd_list(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "%s: list: missing archive\n", prog); return 2; }
    const char *path = argv[0];

    size_t len = 0;
    void *data = read_file(path, &len);
    if (!data) return 1;

    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
    if (rc != MXA_OK) {
        fprintf(stderr, "%s: mxa_open: %s\n", prog, mxa_strerror(rc));
        free(data); return 1;
    }

    struct mxa_file_entry *entries = NULL;
    int count = 0;
    rc = mxa_list_files(ctx, &entries, &count);
    if (rc != MXA_OK) {
        fprintf(stderr, "%s: mxa_list_files: %s\n", prog, mxa_strerror(rc));
        mxa_close(ctx); free(data); return 1;
    }

    printf("Archive: %s (%zu bytes, %d file(s))\n", path, len, count);
    printf("%-30s %10s %10s %6s  %s\n", "Name", "Size", "Csize", "Ratio", "Codec");
    printf("--------------------------------------------------------------------------\n");
    uint64_t total_size = 0, total_csize = 0;
    for (int i = 0; i < count; i++) {
        double ratio = entries[i].size > 0
            ? 100.0 * (double)entries[i].csize / (double)entries[i].size
            : 0.0;
        const char *codec_str = (entries[i].codec == MXA_CODEC_STORED) ? "STORED"
                              : (entries[i].codec == MXA_CODEC_MEUOS) ? "MEUOS"
                              : "?";
        printf("%-30s %10llu %10llu %5.1f%%  %s\n",
               entries[i].name,
               (unsigned long long)entries[i].size,
               (unsigned long long)entries[i].csize,
               ratio, codec_str);
        total_size += entries[i].size;
        total_csize += entries[i].csize;
    }
    if (count > 0) {
        printf("--------------------------------------------------------------------------\n");
        printf("%-30s %10llu %10llu %5.1f%%\n",
               "TOTAL",
               (unsigned long long)total_size,
               (unsigned long long)total_csize,
               total_size > 0 ? 100.0 * (double)total_csize / (double)total_size : 0.0);
    }

    free(entries);
    mxa_close(ctx);
    free(data);
    return 0;
}

/* === 命令：extract (mxa) === */
static int cmd_extract(int argc, char **argv) {
    const char *output_dir = ".";
    const char *archive_path = NULL;
    const char *pattern = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) output_dir = argv[++i];
            else { fprintf(stderr, "%s: --output needs DIR\n", prog); return 1; }
        } else if (!archive_path) {
            archive_path = argv[i];
        } else {
            pattern = argv[i];
        }
    }
    if (!archive_path) { fprintf(stderr, "%s: extract: missing archive\n", prog); return 2; }

    size_t len = 0;
    void *data = read_file(archive_path, &len);
    if (!data) return 1;

    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
    if (rc != MXA_OK) {
        fprintf(stderr, "%s: mxa_open: %s\n", prog, mxa_strerror(rc));
        free(data); return 1;
    }

    struct mxa_file_entry *entries = NULL;
    int count = 0;
    mxa_list_files(ctx, &entries, &count);

    int extracted = 0;
    for (int i = 0; i < count; i++) {
        if (pattern && !strstr(entries[i].name, pattern))
            continue;

        void *fdata = NULL; size_t fsize = 0;
        rc = mxa_read_file(ctx, entries[i].name, &fdata, &fsize);
        if (rc != MXA_OK) {
            fprintf(stderr, "  skip '%s': %s\n", entries[i].name, mxa_strerror(rc));
            continue;
        }

        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%s/%s", output_dir, entries[i].name);

        /* 创建父目录 */
        char *p = out_path;
        while (*++p) {
            if (*p == '/') {
                *p = '\0';
                mkdir(out_path, 0755);
                *p = '/';
            }
        }

        FILE *f = fopen(out_path, "wb");
        if (!f) {
            fprintf(stderr, "  skip '%s': %s\n", entries[i].name, strerror(errno));
            free(fdata); continue;
        }
        if (fsize > 0) fwrite(fdata, 1, fsize, f);
        fclose(f);
        printf("  extracted: %s (%zu bytes)\n", out_path, fsize);
        free(fdata);
        extracted++;
    }

    printf("extracted %d file(s) to '%s/'\n", extracted, output_dir);
    free(entries);
    mxa_close(ctx);
    free(data);
    return 0;
}

/* === 命令：test === */
static int cmd_test(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "%s: test: missing file\n", prog); return 2; }
    const char *path = argv[0];

    size_t len = 0;
    void *data = read_file(path, &len);
    if (!data) return 1;

    /* 尝试作为 .mxa 归档测试 */
    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
    if (rc == MXA_OK) {
        struct mxa_file_entry *entries = NULL;
        int count = 0;
        mxa_list_files(ctx, &entries, &count);
        int errors = 0;
        for (int i = 0; i < count; i++) {
            void *fdata = NULL; size_t fsize = 0;
            rc = mxa_read_file(ctx, entries[i].name, &fdata, &fsize);
            if (rc != MXA_OK) {
                fprintf(stderr, "  FAIL: %s: %s\n", entries[i].name, mxa_strerror(rc));
                errors++;
            } else {
                free(fdata);
            }
        }
        free(entries);
        mxa_close(ctx);
        free(data);
        if (errors == 0) {
            printf("%s\tOK (%d files)\n", path, count);
            return 0;
        }
        printf("%s\t%d error(s)\n", path, errors);
        return 1;
    }

    /* 尝试作为 .mz 压缩流测试 */
    void *out_data = NULL;
    size_t out_len = 0;
    rc = mz_decompress(data, len, &out_data, &out_len, MZ_CODEC_MEUOS);
    if (rc <= 0) {
        free(out_data); out_data = NULL; out_len = 0;
        rc = mz_decompress(data, len, &out_data, &out_len, MZ_CODEC_LZ77);
    }
    free(data);
    if (rc <= 0) {
        fprintf(stderr, "%s: not a valid mz/mxa file: %s\n", prog, mz_strerror(rc));
        return 1;
    }
    free(out_data);
    printf("%s\tOK (%zu -> %zu bytes)\n", path, len, out_len);
    return 0;
}

/* === 用法 === */
static void usage(void) {
    printf(
        "mz — MeuOS native compression tool (meuos-utils)\n"
        "\n"
        "usage: mz <command> [options] [files...]\n"
        "\n"
        "commands:\n"
        "  c, compress    compress FILE to .mz\n"
        "    -l, --level N   compression level (1-9, default 6)\n"
        "    -o, --output F   output filename\n"
        "  d, decompress  decompress .mz file\n"
        "    -o, --output F   output filename\n"
        "  a, archive     create .mxa archive from files\n"
        "    -l, --level N   compression level (1-9, default 6)\n"
        "  l, list        list contents of .mxa archive\n"
        "  x, extract     extract files from .mxa archive\n"
        "    -o, --output D   output directory (default .)\n"
        "  t, test        test integrity of .mz or .mxa file\n"
        "\n"
        "examples:\n"
        "  mz compress -l 9 -o data.mz data.txt\n"
        "  mz d data.mz\n"
        "  mz a -l 6 pkg.mxa file1.c file2.c\n"
        "  mz l pkg.mxa\n"
        "  mz x -o /tmp/out pkg.mxa\n"
        "  mz t data.mz\n"
        "\n"
        "  --help     show help\n"
        "  --version  show version\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(); return 0;
    }
    if (strcmp(argv[1], "--version") == 0) {
        printf("mz (meuos-utils)\n");
        return 0;
    }

    const char *cmd = argv[1];
    int sub_argc = argc - 2;
    char **sub_argv = argv + 2;

    if (strcmp(cmd, "c") == 0 || strcmp(cmd, "compress") == 0)
        return cmd_compress(sub_argc, sub_argv);
    if (strcmp(cmd, "d") == 0 || strcmp(cmd, "decompress") == 0)
        return cmd_decompress(sub_argc, sub_argv);
    if (strcmp(cmd, "a") == 0 || strcmp(cmd, "archive") == 0)
        return cmd_archive(sub_argc, sub_argv);
    if (strcmp(cmd, "l") == 0 || strcmp(cmd, "list") == 0)
        return cmd_list(sub_argc, sub_argv);
    if (strcmp(cmd, "x") == 0 || strcmp(cmd, "extract") == 0)
        return cmd_extract(sub_argc, sub_argv);
    if (strcmp(cmd, "t") == 0 || strcmp(cmd, "test") == 0)
        return cmd_test(sub_argc, sub_argv);

    fprintf(stderr, "%s: unknown command '%s'\n", prog, cmd);
    usage();
    return 1;
}
