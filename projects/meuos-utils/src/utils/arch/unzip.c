/* unzip — PKZIP 解压工具 (薄壳)
 *
 * 所有 PKZIP 格式解析和解压逻辑由 libmz (meuos-compress) 提供：
 *   - mz_zip_reader_open/close/count/entry/extract/find
 *   - mz_deflate_decompress() — DEFLATE 解压
 *   - mz_crc32() — CRC32 校验
 *
 * 用法：
 *   unzip [options] file.zip [files...]
 *   -d DIR    解压到指定目录
 *   -l        列出内容
 *   -t        测试完整性
 *   -o        覆盖已有文件
 *   -q        安静模式
 *   --help / --version
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mz.h"
#include "meuos/utils.h"

static const char *prog = "unzip";

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

/* mkdir -p style */
static void mkdir_p(const char *path)
{
    char *copy = strdup(path);
    for (char *p = copy + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(copy, 0755);
            *p = '/';
        }
    }
    mkdir(copy, 0755);
    free(copy);
}

/* Extract a single entry */
static int extract_entry(struct mz_zip_reader *reader, int idx,
                         const char *dest_dir, int overwrite, int quiet)
{
    const struct mz_zip_entry *e = mz_zip_reader_entry(reader, idx);

    /* Construct output path */
    char outpath[4096];
    if (dest_dir)
        snprintf(outpath, sizeof(outpath), "%s/%s", dest_dir, e->name);
    else
        snprintf(outpath, sizeof(outpath), "%s", e->name);

    /* Directory entry (name ends with /) */
    size_t namelen = strlen(e->name);
    if (namelen > 0 && e->name[namelen - 1] == '/') {
        mkdir_p(outpath);
        return 0;
    }

    /* Check overwrite */
    if (!overwrite && access(outpath, F_OK) == 0) {
        fprintf(stderr, "%s: %s already exists\n", prog, outpath);
        return 1;
    }

    /* Create parent directories */
    char *path_copy = strdup(outpath);
    char *last_slash = strrchr(path_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (path_copy[0] != '\0')
            mkdir_p(path_copy);
    }
    free(path_copy);

    /* Extract data via libmz */
    void *out_data = NULL;
    size_t out_len = 0;
    int rc = mz_zip_reader_extract(reader, idx, &out_data, &out_len);
    if (rc <= 0) {
        fprintf(stderr, "%s: %s: %s\n", prog, e->name, mz_strerror(rc));
        return 1;
    }

    if (!quiet)
        printf("  inflating: %s\n", e->name);

    /* Write output file */
    FILE *f = fopen(outpath, "wb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, outpath, strerror(errno));
        free(out_data);
        return 1;
    }
    fwrite(out_data, 1, out_len, f);
    fclose(f);
    free(out_data);

    return 0;
}

/* List entries */
static int list_entries(struct mz_zip_reader *reader)
{
    int count = mz_zip_reader_count(reader);
    printf("  Length    Date    Time   Name\n");
    printf("---------  ---------- -----   ----\n");
    uint32_t total = 0;
    for (int i = 0; i < count; i++) {
        const struct mz_zip_entry *e = mz_zip_reader_entry(reader, i);
        printf("%9u  %s   %s\n", e->uncompressed_size, "----/--/--", e->name);
        total += e->uncompressed_size;
    }
    printf("---------                     -------\n");
    printf("%9u                     %d files\n", total, count);
    return 0;
}

/* Test entries */
static int test_entries(struct mz_zip_reader *reader)
{
    int count = mz_zip_reader_count(reader);
    int errors = 0;
    for (int i = 0; i < count; i++) {
        const struct mz_zip_entry *e = mz_zip_reader_entry(reader, i);
        void *out_data = NULL;
        size_t out_len = 0;
        int rc = mz_zip_reader_extract(reader, i, &out_data, &out_len);
        if (rc <= 0) {
            fprintf(stderr, "%s: %s: %s\n", prog, e->name, mz_strerror(rc));
            errors++;
            continue;
        }

        /* CRC is already verified during extract by mz_zip_reader_extract
         * (via mz_deflate_decompress). For stored entries, verify CRC explicitly. */
        uint32_t crc = mz_crc32(out_data, out_len);
        free(out_data);

        if (crc != e->crc32) {
            fprintf(stderr, "%s: %s: CRC mismatch\n", prog, e->name);
            errors++;
        }
    }

    if (errors == 0)
        printf("No errors detected in %d entries\n", count);
    else
        printf("%d error(s) detected\n", errors);

    return errors > 0 ? 1 : 0;
}

static void usage(void)
{
    printf(
        "unzip — PKZIP archive extractor (meuos-utils)\n"
        "\n"
        "usage: unzip [options] file.zip [files...]\n"
        "\n"
        "options:\n"
        "  -d DIR    extract to directory\n"
        "  -l        list contents\n"
        "  -t        test integrity\n"
        "  -o        overwrite existing files\n"
        "  -q        quiet mode\n"
        "  --help    show help\n"
        "  --version show version\n");
}

int main(int argc, char **argv)
{
    int mode_list = 0;
    int mode_test = 0;
    int overwrite = 0;
    int quiet = 0;
    const char *dest_dir = NULL;
    int oi = 1;

    while (oi < argc) {
        const char *opt = argv[oi];
        if (strcmp(opt, "--help") == 0) { usage(); return 0; }
        if (strcmp(opt, "--version") == 0) { printf("unzip (meuos-utils)\n"); return 0; }
        if (strcmp(opt, "-l") == 0) { mode_list = 1; oi++; continue; }
        if (strcmp(opt, "-t") == 0) { mode_test = 1; oi++; continue; }
        if (strcmp(opt, "-o") == 0) { overwrite = 1; oi++; continue; }
        if (strcmp(opt, "-q") == 0) { quiet = 1; oi++; continue; }
        if (strcmp(opt, "-d") == 0) {
            if (++oi >= argc) { fprintf(stderr, "%s: -d requires argument\n", prog); return 2; }
            dest_dir = argv[oi]; oi++; continue;
        }
        if (opt[0] == '-' && opt[1] != '\0') {
            fprintf(stderr, "%s: unknown option %s\n", prog, opt);
            return 2;
        }
        break;
    }

    if (oi >= argc) {
        fprintf(stderr, "%s: missing zip file\n", prog);
        usage();
        return 2;
    }

    const char *zip_path = argv[oi++];

    /* Read zip file */
    uint8_t *zip_data = NULL;
    size_t zip_size = 0;
    if (read_file(zip_path, &zip_data, &zip_size) != 0)
        return 1;

    /* Open via libmz */
    struct mz_zip_reader *reader = NULL;
    int rc = mz_zip_reader_open(&reader, zip_data, zip_size);
    if (rc != MZ_OK) {
        fprintf(stderr, "%s: %s: %s\n", prog, zip_path, mz_strerror(rc));
        free(zip_data);
        return 1;
    }

    int entry_count = mz_zip_reader_count(reader);
    if (entry_count == 0) {
        fprintf(stderr, "%s: no entries found in %s\n", prog, zip_path);
        mz_zip_reader_close(reader);
        free(zip_data);
        return 1;
    }

    /* Filter: if specific files are listed, only extract those */
    const char **want = NULL;
    int want_count = 0;
    if (oi < argc && !mode_list && !mode_test) {
        want = (const char **)(argv + oi);
        want_count = argc - oi;
    }

    int ret = 0;

    if (mode_list) {
        list_entries(reader);
    } else if (mode_test) {
        ret = test_entries(reader);
    } else {
        /* Extract */
        if (dest_dir)
            mkdir(dest_dir, 0755);

        for (int i = 0; i < entry_count; i++) {
            /* Check if this entry is wanted */
            if (want_count > 0) {
                const struct mz_zip_entry *e = mz_zip_reader_entry(reader, i);
                int found = 0;
                for (int j = 0; j < want_count; j++) {
                    if (strcmp(e->name, want[j]) == 0) {
                        found = 1; break;
                    }
                }
                if (!found) continue;
            }

            if (extract_entry(reader, i, dest_dir, overwrite, quiet) != 0)
                ret = 1;
        }
    }

    mz_zip_reader_close(reader);
    free(zip_data);

    return ret;
}
