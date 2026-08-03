/* gzip/gunzip — DEFLATE 压缩/解压工具 (薄壳)
 *
 * 所有压缩/解压逻辑由 libmz (meuos-compress) 提供：
 *   - mz_gzip_compress() / mz_gzip_decompress() — gzip 格式封装
 *   - mz_crc32() — CRC32 校验
 *   - mz_deflate_compress() / mz_deflate_decompress() — raw DEFLATE
 *
 * 用法：
 *   gzip [options] [files...]    压缩（原文件替换为 .gz）
 *   gzip -d [options] [files...] 解压（.gz 替换为原文件）
 *   gzip -c  写到 stdout
 *   gzip -k  保留原文件
 *   gzip -f  强制覆盖
 *   gzip -t  测试完整性
 *   gzip -l  列出内容
 *   --help / --version
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

#include "mz.h"
#include "meuos/utils.h"

static const char *prog = "gzip";

/* Read entire file into memory */
static int read_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc(sz > 0 ? sz : 1);
    *size = fread(*data, 1, sz, f);
    fclose(f);
    return 0;
}

static int do_compress(const char *filename, int to_stdout, int keep, int force)
{
    uint8_t *data = NULL;
    size_t size = 0;
    if (read_file(filename, &data, &size) != 0)
        return 1;

    /* Compress via libmz */
    void *gz_out = NULL;
    size_t gz_len = 0;
    int rc = mz_gzip_compress(data, size, &gz_out, &gz_len);
    if (rc <= 0) {
        fprintf(stderr, "%s: %s: compression failed (%s)\n", prog, filename, mz_strerror(rc));
        free(data);
        return 1;
    }

    if (to_stdout) {
        fwrite(gz_out, 1, gz_len, stdout);
    } else {
        char outname[4096];
        snprintf(outname, sizeof(outname), "%s.gz", filename);
        if (!force && access(outname, F_OK) == 0) {
            fprintf(stderr, "%s: %s already exists; use -f to overwrite\n", prog, outname);
            free(data);
            free(gz_out);
            return 1;
        }
        FILE *f = fopen(outname, "wb");
        if (!f) {
            fprintf(stderr, "%s: %s: %s\n", prog, outname, strerror(errno));
            free(data);
            free(gz_out);
            return 1;
        }
        fwrite(gz_out, 1, gz_len, f);
        fclose(f);
        if (!keep)
            unlink(filename);
    }

    free(data);
    free(gz_out);
    return 0;
}

static int do_decompress(const char *filename, int to_stdout, int keep, int force)
{
    uint8_t *data = NULL;
    size_t size = 0;
    if (read_file(filename, &data, &size) != 0)
        return 1;

    /* Decompress via libmz */
    void *raw_out = NULL;
    size_t raw_len = 0;
    int rc = mz_gzip_decompress(data, size, &raw_out, &raw_len);
    if (rc <= 0) {
        fprintf(stderr, "%s: %s: decompression failed (%s)\n", prog, filename, mz_strerror(rc));
        free(data);
        return 1;
    }

    if (to_stdout) {
        fwrite(raw_out, 1, raw_len, stdout);
    } else {
        /* Determine output filename: remove .gz extension */
        char outname[4096];
        size_t len = strlen(filename);
        if (len > 3 && strcmp(filename + len - 3, ".gz") == 0) {
            memcpy(outname, filename, len - 3);
            outname[len - 3] = '\0';
        } else if (len > 4 && strcmp(filename + len - 4, ".gzip") == 0) {
            memcpy(outname, filename, len - 4);
            outname[len - 4] = '\0';
        } else {
            snprintf(outname, sizeof(outname), "%s.out", filename);
        }

        if (!force && access(outname, F_OK) == 0) {
            fprintf(stderr, "%s: %s already exists; use -f to overwrite\n", prog, outname);
            free(data);
            free(raw_out);
            return 1;
        }

        FILE *f = fopen(outname, "wb");
        if (!f) {
            fprintf(stderr, "%s: %s: %s\n", prog, outname, strerror(errno));
            free(data);
            free(raw_out);
            return 1;
        }
        fwrite(raw_out, 1, raw_len, f);
        fclose(f);
        if (!keep)
            unlink(filename);
    }

    free(data);
    free(raw_out);
    return 0;
}

static int do_test(const char *filename)
{
    uint8_t *data = NULL;
    size_t size = 0;
    if (read_file(filename, &data, &size) != 0)
        return 1;

    void *raw_out = NULL;
    size_t raw_len = 0;
    int rc = mz_gzip_decompress(data, size, &raw_out, &raw_len);
    free(data);
    if (rc <= 0) {
        fprintf(stderr, "%s: %s: decompression failed\n", prog, filename);
        return 1;
    }
    free(raw_out);
    printf("%s\tOK\n", filename);
    return 0;
}

static int do_list(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, filename, strerror(errno));
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t header[10];
    if (fread(header, 1, 10, f) < 10 ||
        header[0] != 0x1f || header[1] != 0x8b) {
        fprintf(stderr, "%s: %s: not a gzip file\n", prog, filename);
        fclose(f);
        return 1;
    }

    /* Read original size from footer */
    fseek(f, -4, SEEK_END);
    uint8_t sz[4];
    fread(sz, 1, 4, f);
    uint32_t orig_size = sz[0] | (sz[1] << 8) | (sz[2] << 16) | (sz[3] << 24);

    printf("  compressed  uncompressed  ratio  uncompressed_name\n");
    printf("%10ld  %12u  %3.0f%%  %s\n",
           fsize, orig_size,
           orig_size > 0 ? (100.0 * (1.0 - (double)fsize / orig_size)) : 0.0,
           filename);

    fclose(f);
    return 0;
}

static void usage(void)
{
    printf(
        "gzip — compression tool (meuos-utils)\n"
        "\n"
        "usage: gzip [options] [files...]\n"
        "       gzip -d [options] [files...]\n"
        "\n"
        "options:\n"
        "  -c    write to stdout, keep original\n"
        "  -d    decompress\n"
        "  -k    keep original file\n"
        "  -f    force overwrite\n"
        "  -t    test integrity\n"
        "  -l    list contents\n"
        "  -N    compression level (1-9, uses stored blocks)\n"
        "  --help     show help\n"
        "  --version  show version\n");
}

int main(int argc, char **argv)
{
    int decompress = 0;
    int to_stdout = 0;
    int keep = 0;
    int force = 0;
    int test_mode = 0;
    int list_mode = 0;
    int oi = 1;

    while (oi < argc && argv[oi][0] == '-' && argv[oi][1] != '\0') {
        const char *opt = argv[oi];
        if (strcmp(opt, "--help") == 0) { usage(); return 0; }
        if (strcmp(opt, "--version") == 0) { printf("gzip (meuos-utils)\n"); return 0; }

        int j = 1;
        while (opt[j]) {
            switch (opt[j]) {
            case 'd': decompress = 1; break;
            case 'c': to_stdout = 1; break;
            case 'k': keep = 1; break;
            case 'f': force = 1; break;
            case 't': test_mode = 1; break;
            case 'l': list_mode = 1; break;
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                break;
            default:
                fprintf(stderr, "%s: unknown option -%c\n", prog, opt[j]);
                return 2;
            }
            j++;
        }
        oi++;
    }

    if (oi >= argc) {
        /* No files: use stdin/stdout */
        fseek(stdin, 0, SEEK_END);
        long fsize = ftell(stdin);
        fseek(stdin, 0, SEEK_SET);
        uint8_t *data = malloc(fsize > 0 ? fsize : 1);
        size_t nread = fread(data, 1, fsize, stdin);

        if (decompress) {
            void *out = NULL;
            size_t out_len = 0;
            if (mz_gzip_decompress(data, nread, &out, &out_len) > 0) {
                fwrite(out, 1, out_len, stdout);
                free(out);
            }
        } else {
            void *out = NULL;
            size_t out_len = 0;
            if (mz_gzip_compress(data, nread, &out, &out_len) > 0) {
                fwrite(out, 1, out_len, stdout);
                free(out);
            }
        }
        free(data);
        return 0;
    }

    int ret = 0;
    for (int i = oi; i < argc; i++) {
        int rc;
        if (test_mode) {
            rc = do_test(argv[i]);
        } else if (list_mode) {
            rc = do_list(argv[i]);
        } else if (decompress) {
            rc = do_decompress(argv[i], to_stdout, keep, force);
        } else {
            rc = do_compress(argv[i], to_stdout, keep, force);
        }
        if (rc != 0) ret = rc;
    }

    return ret;
}
