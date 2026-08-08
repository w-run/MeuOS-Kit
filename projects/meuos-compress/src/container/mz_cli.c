/* mz_cli.c — mz 统一 CLI 工具
 *
 * 单一二进制，通过 argv[0] 软链识别调用模式：
 *   mz          → 子命令模式 (c/d/l/t)
 *   gunzip      → ≡ mz d <file> (解压，按扩展名/magic 识别)
 *   unzip       → ≡ mz d <file>
 *   bunzip2     → ≡ mz d <file> (stub)
 *   tar         → 根据参数转 mz c 或 mz d
 *   zstd/unzstd → 根据参数转 mz c 或 mz d
 *
 * 子命令:
 *   mz c [-l LV] [-o OUTFILE] <files...>   压缩
 *   mz d <archive>                          解压（自动嗅探格式）
 *   mz l <archive>                          列出内容
 *   mz t <archive>                          测试完整性
 *
 * 自动格式识别（解压时）:
 *   1. 取文件名后缀 .mz .gz .bz2 .zst .zip .tar
 *   2. 未识别后缀 → 读文件头 magic
 *
 * 自动格式确定（压缩时）:
 *   若 -o 指定输出文件，按其扩展名决定容器格式
 *   未指定 → 自研 MZv2 格式
 */
#include "mz.h"
#include "mxa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

/* ===================================================================
 * 格式枚举 + 嗅探
 * =================================================================== */

enum mz_fmt {
    FMT_UNKNOWN = 0,
    FMT_MZ,       /* 自研 MZv2 */
    FMT_MXA,      /* MxA 归档 */
    FMT_GZIP,     /* .gz */
    FMT_BZ2,      /* .bz2 (stub) */
    FMT_ZSTD,     /* .zst (stub) */
    FMT_ZIP,      /* .zip */
    FMT_TAR       /* .tar */
};

/* Portable case-insensitive strcmp (C11, no POSIX) */
static int
stricmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (*a) ? 1 : (*b) ? -1 : 0;
}

/* 根据文件名后缀猜测格式 */
static enum mz_fmt
fmt_from_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return FMT_UNKNOWN;

    if (stricmp(dot, ".mz") == 0)  return FMT_MZ;
    if (stricmp(dot, ".mxa") == 0) return FMT_MXA;
    if (stricmp(dot, ".gz") == 0)  return FMT_GZIP;
    if (stricmp(dot, ".gzip") == 0)return FMT_GZIP;
    if (stricmp(dot, ".bz2") == 0) return FMT_BZ2;
    if (stricmp(dot, ".bzip2") == 0)return FMT_BZ2;
    if (stricmp(dot, ".zst") == 0) return FMT_ZSTD;
    if (stricmp(dot, ".zstd") == 0)return FMT_ZSTD;
    if (stricmp(dot, ".zip") == 0) return FMT_ZIP;
    if (stricmp(dot, ".tar") == 0) return FMT_TAR;
    if (stricmp(dot, ".tgz") == 0) return FMT_GZIP; /* .tar.gz = tgz */
    return FMT_UNKNOWN;
}

/* 根据文件头 magic 猜测格式 */
static enum mz_fmt
fmt_from_magic(const uint8_t *data, size_t len)
{
    if (len < 4) return FMT_UNKNOWN;

    /* MZv2: "MZv2" */
    if (data[0] == 'M' && data[1] == 'Z' && data[2] == 'v' && data[3] == '2')
        return FMT_MZ;

    /* MxA1: "MxA1" */
    if (data[0] == 'M' && data[1] == 'x' && data[2] == 'A' && data[3] == '1')
        return FMT_MXA;

    /* gzip: 0x1F 0x8B */
    if (len >= 2 && data[0] == 0x1F && data[1] == 0x8B)
        return FMT_GZIP;

    /* bzip2: "BZh" */
    if (len >= 3 && data[0] == 'B' && data[1] == 'Z' && data[2] == 'h')
        return FMT_BZ2;

    /* zstd: 0x28 0xB5 0x2F 0xFD */
    if (len >= 4 && data[0] == 0x28 && data[1] == 0xB5 &&
        data[2] == 0x2F && data[3] == 0xFD)
        return FMT_ZSTD;

    /* zip: PK\x03\x04 */
    if (len >= 4 && data[0] == 'P' && data[1] == 'K' &&
        data[2] == 0x03 && data[3] == 0x04)
        return FMT_ZIP;

    /* tar: ustar magic at offset 257 */
    if (len >= 262 && memcmp(data + 257, "ustar", 5) == 0)
        return FMT_TAR;

    return FMT_UNKNOWN;
}

static const char *
fmt_name(enum mz_fmt fmt)
{
    switch (fmt) {
    case FMT_MZ:   return "MZv2";
    case FMT_MXA:  return "MxA";
    case FMT_GZIP: return "Gzip";
    case FMT_BZ2:  return "Bzip2";
    case FMT_ZSTD: return "Zstd";
    case FMT_ZIP:  return "Zip";
    case FMT_TAR:  return "Tar";
    default:       return "Unknown";
    }
}

/* ===================================================================
 * 文件 I/O 工具
 * =================================================================== */

static void *
read_whole(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mz: cannot open '%s': %s\n", path, strerror(errno)); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long flen = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)(flen > 0 ? flen : 1));
    if (!buf) { fprintf(stderr, "mz: out of memory\n"); fclose(f); return NULL; }
    if (flen > 0 && fread(buf, 1, (size_t)flen, f) != (size_t)flen) {
        fprintf(stderr, "mz: short read on '%s'\n", path);
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)(flen > 0 ? flen : 0);
    return buf;
}

static int
write_buf(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "mz: cannot create '%s': %s\n", path, strerror(errno)); return 1; }
    if (len > 0) fwrite(data, 1, len, f);
    fclose(f);
    return 0;
}

/* ===================================================================
 * 格式写入器（压缩时按格式输出）
 * =================================================================== */

static int
write_mz_container(const char *outfile, int nfiles, char **filenames, int level)
{
    struct mz_params params;
    memset(&params, 0, sizeof(params));
    params.level = level;
    params.flags = 0;

    void *ctx = NULL;
    int rc = mz2_create(&ctx, NULL, &params);
    if (rc != MZ_OK) { fprintf(stderr, "mz: mz2_create: %s\n", mz_strerror(rc)); return 1; }

    for (int fi = 0; fi < nfiles; fi++) {
        size_t sz;
        void *d = read_whole(filenames[fi], &sz);
        if (!d) return 1;
        const char *name = strrchr(filenames[fi], '/');
        name = name ? name + 1 : filenames[fi];
        rc = mz2_add_file(ctx, name, d, sz, 0644);
        free(d);
        if (rc != MZ_OK) { fprintf(stderr, "mz: mz2_add_file('%s'): %s\n", name, mz_strerror(rc)); return 1; }
    }

    void *result; size_t result_len;
    rc = mz2_finish(ctx, &result, &result_len);
    if (rc != MZ_OK) { fprintf(stderr, "mz: mz2_finish: %s\n", mz_strerror(rc)); return 1; }

    int r = write_buf(outfile, result, result_len);
    fprintf(stderr, "mz: created %s (%zu bytes, %d file(s))\n", outfile, result_len, nfiles);
    free(result);
    return r;
}

static int
write_gzip(const char *outfile, int nfiles, char **filenames)
{
    if (nfiles != 1) { fprintf(stderr, "mz: gzip only supports single input file\n"); return 1; }
    size_t sz; void *d = read_whole(filenames[0], &sz);
    if (!d) return 1;
    void *result; size_t result_len;
    int rc = mz_gzip_compress(d, sz, &result, &result_len);
    free(d);
    if (rc <= 0) { fprintf(stderr, "mz: gzip compress: %s\n", mz_strerror(rc)); return 1; }
    int r = write_buf(outfile, result, result_len);
    fprintf(stderr, "mz: created %s (%zu bytes)\n", outfile, result_len);
    free(result);
    return r;
}

static int
write_mxa(const char *outfile, int nfiles, char **filenames, int level)
{
    struct mxa_params params;
    memset(&params, 0, sizeof(params));
    params.level = level;
    params.flags = 0;

    void *ctx = NULL;
    int rc = mxa_create(&ctx, &params);
    if (rc != MXA_OK) { fprintf(stderr, "mz: mxa_create: %s\n", mxa_strerror(rc)); return 1; }

    for (int fi = 0; fi < nfiles; fi++) {
        size_t sz; void *d = read_whole(filenames[fi], &sz);
        if (!d) return 1;
        const char *name = strrchr(filenames[fi], '/');
        name = name ? name + 1 : filenames[fi];
        rc = mxa_add_file(ctx, name, d, sz, 0644, 0, 0, 0);
        free(d);
        if (rc != MXA_OK) { fprintf(stderr, "mz: mxa_add_file('%s'): %s\n", name, mxa_strerror(rc)); return 1; }
    }

    void *result; size_t result_len;
    rc = mxa_finish(ctx, &result, &result_len);
    if (rc != MXA_OK) { fprintf(stderr, "mz: mxa_finish: %s\n", mxa_strerror(rc)); return 1; }
    int r = write_buf(outfile, result, result_len);
    fprintf(stderr, "mz: created %s (%zu bytes, %d file(s))\n", outfile, result_len, nfiles);
    free(result);
    return r;
}

static int
write_tar(const char *outfile, int nfiles, char **filenames)
{
    FILE *f = fopen(outfile, "wb");
    if (!f) { fprintf(stderr, "mz: cannot create '%s': %s\n", outfile, strerror(errno)); return 1; }

    char zero[1024];
    memset(zero, 0, sizeof(zero));

    for (int fi = 0; fi < nfiles; fi++) {
        size_t sz; void *d = read_whole(filenames[fi], &sz);
        if (!d) { fclose(f); return 1; }
        const char *name = strrchr(filenames[fi], '/');
        name = name ? name + 1 : filenames[fi];

        char hdr[512];
        memset(hdr, 0, sizeof(hdr));
        size_t nlen = strlen(name);
        if (nlen > 99) nlen = 99;
        memcpy(hdr, name, nlen);
        snprintf(hdr + 100, 8, "%07o", 0644);
        snprintf(hdr + 108, 8, "%07o", 0);
        snprintf(hdr + 116, 8, "%07o", 0);
        snprintf(hdr + 124, 12, "%011llo", (unsigned long long)sz);
        snprintf(hdr + 136, 12, "%011llo", (unsigned long long)0);
        memset(hdr + 148, ' ', 8);
        hdr[156] = '0';
        memcpy(hdr + 257, "ustar", 5);
        memcpy(hdr + 263, "00", 2);

        unsigned chk = 0;
        for (int j = 0; j < 512; j++) chk += (unsigned char)hdr[j];
        snprintf(hdr + 148, 8, "%06o", chk);
        hdr[154] = ' '; hdr[155] = ' ';

        fwrite(hdr, 1, 512, f);
        if (sz > 0) fwrite(d, 1, sz, f);
        size_t pad = (512 - (sz % 512)) % 512;
        if (pad > 0) fwrite(zero, 1, pad, f);
        free(d);
    }

    fwrite(zero, 1, 1024, f);
    fclose(f);
    fprintf(stderr, "mz: created %s (%d file(s))\n", outfile, nfiles);
    return 0;
}

/* ===================================================================
 * 解压/提取
 * =================================================================== */

static int
extract_mz(const uint8_t *data, size_t len, const char *path)
{
    (void)path;
    void *ctx = NULL;
    int rc = mz2_open(data, len, &ctx);
    if (rc != MZ_OK) { fprintf(stderr, "mz: mz2_open: %s\n", mz_strerror(rc)); return 1; }

    struct mz_file_entry *fent = NULL;
    int nf = 0;
    mz2_list_files(ctx, &fent, &nf);
    fprintf(stderr, "mz: %d file(s) in archive\n", nf);

    for (int fi = 0; fi < nf; fi++) {
        void *fd; size_t fsz;
        rc = mz2_read_file(ctx, fent[fi].name, &fd, &fsz);
        if (rc != MZ_OK) {
            fprintf(stderr, "mz: skip '%s': %s\n", fent[fi].name, mz_strerror(rc));
            continue;
        }
        const char *ename = fent[fi].name;
        if (ename[0] == '/' || strstr(ename, "..")) {
            fprintf(stderr, "mz: skip '%s': unsafe path\n", ename);
            free(fd); continue;
        }
        /* Create parent dir */
        const char *slash = strrchr(ename, '/');
        if (slash) {
            char dir[1024]; size_t dlen = (size_t)(slash - ename);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
            memcpy(dir, ename, dlen); dir[dlen] = '\0';
            mkdir(dir, 0755);
        }
        FILE *f = fopen(ename, "wb");
        if (!f) {
            fprintf(stderr, "mz: skip '%s': cannot create: %s\n", ename, strerror(errno));
            free(fd); continue;
        }
        if (fsz > 0) fwrite(fd, 1, fsz, f);
        fclose(f);
        fprintf(stderr, "mz:   extracted: %s (%zu bytes)\n", ename, fsz);
        free(fd);
    }
    mz2_close(ctx);
    return 0;
}

static int
extract_mxa(const uint8_t *data, size_t len, const char *path)
{
    (void)path;
    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
    if (rc != MXA_OK) { fprintf(stderr, "mz: mxa_open: %s\n", mxa_strerror(rc)); return 1; }

    struct mxa_file_entry *entries = NULL;
    int count = 0;
    mxa_list_files(ctx, &entries, &count);
    fprintf(stderr, "mz: %d file(s) in archive\n", count);

    for (int i = 0; i < count; i++) {
        const char *name = entries[i].name;
        if (name[0] == '/' || strstr(name, "..")) {
            fprintf(stderr, "mz: skip '%s': unsafe path\n", name);
            continue;
        }
        void *fd; size_t fsz;
        rc = mxa_read_file(ctx, name, &fd, &fsz);
        if (rc != MXA_OK) { fprintf(stderr, "mz: skip '%s': %s\n", name, mxa_strerror(rc)); continue; }

        const char *slash = strrchr(name, '/');
        if (slash) {
            char dir[1024]; size_t dlen = (size_t)(slash - name);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
            memcpy(dir, name, dlen); dir[dlen] = '\0';
            mkdir(dir, 0755);
        }
        FILE *f = fopen(name, "wb");
        if (!f) { fprintf(stderr, "mz: skip '%s': %s\n", name, strerror(errno)); free(fd); continue; }
        if (fsz > 0) fwrite(fd, 1, fsz, f);
        fclose(f);
        fprintf(stderr, "mz:   extracted: %s (%zu bytes)\n", name, fsz);
        free(fd);
    }
    free(entries);
    mxa_close(ctx);
    return 0;
}

static int
extract_gzip(const uint8_t *data, size_t len)
{
    void *decomp = NULL; size_t decomp_len = 0;
    int rc = mz_gzip_decompress(data, len, &decomp, &decomp_len);
    if (rc <= 0) { fprintf(stderr, "mz: gzip decompress: %s\n", mz_strerror(rc)); return 1; }
    fprintf(stderr, "mz: decompressed %zu bytes\n", decomp_len);
    fwrite(decomp, 1, decomp_len, stdout);
    free(decomp);
    return 0;
}

static int
extract_zip(const uint8_t *data, size_t len)
{
    struct mz_zip_reader *reader = NULL;
    int rc = mz_zip_reader_open(&reader, data, len);
    if (rc != MZ_OK) { fprintf(stderr, "mz: zip open: %s\n", mz_strerror(rc)); return 1; }

    int count = mz_zip_reader_count(reader);
    fprintf(stderr, "mz: %d file(s) in zip archive\n", count);

    for (int i = 0; i < count; i++) {
        const struct mz_zip_entry *e = mz_zip_reader_entry(reader, i);
        if (!e) continue;
        void *fd; size_t fsz;
        rc = mz_zip_reader_extract(reader, i, &fd, &fsz);
        if (rc <= 0) { fprintf(stderr, "mz: skip '%s': extract failed\n", e->name); continue; }

        const char *slash = strrchr(e->name, '/');
        if (slash) {
            char dir[1024]; size_t dlen = (size_t)(slash - e->name);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
            memcpy(dir, e->name, dlen); dir[dlen] = '\0';
            mkdir(dir, 0755);
        }
        FILE *f = fopen(e->name, "wb");
        if (!f) { fprintf(stderr, "mz: skip '%s': %s\n", e->name, strerror(errno)); free(fd); continue; }
        if (fsz > 0) fwrite(fd, 1, fsz, f);
        fclose(f);
        fprintf(stderr, "mz:   extracted: %s (%zu bytes)\n", e->name, fsz);
        free(fd);
    }
    mz_zip_reader_close(reader);
    return 0;
}

static int
extract_tar(const uint8_t *data, size_t len)
{
    const uint8_t *p = data;
    size_t pos = 0;
    int fcount = 0;
    while (pos + 512 <= len) {
        const uint8_t *hdr = p + pos;
        int zero = 1;
        for (int j = 0; j < 512; j++) { if (hdr[j] != 0) { zero = 0; break; } }
        if (zero) break;
        if (memcmp(hdr + 257, "ustar", 5) != 0) {
            fprintf(stderr, "mz: non-ustar entry at offset %zu\n", pos);
            break;
        }
        unsigned long long fsize = 0;
        sscanf((const char *)hdr + 124, "%011llo", &fsize);
        char name[101];
        memcpy(name, hdr, 100); name[100] = '\0';
        if (name[0] == '\0') { pos += 512; continue; }
        pos += 512;
        if (pos + fsize > len) { fprintf(stderr, "mz: truncated entry '%s'\n", name); break; }

        const char *slash = strrchr(name, '/');
        if (slash) {
            char dir[1024]; size_t dlen = (size_t)(slash - name);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
            memcpy(dir, name, dlen); dir[dlen] = '\0';
            mkdir(dir, 0755);
        }
        FILE *f = fopen(name, "wb");
        if (!f) { fprintf(stderr, "mz: skip '%s': %s\n", name, strerror(errno)); }
        else {
            if (fsize > 0) fwrite(p + pos, 1, (size_t)fsize, f);
            fclose(f);
        }
        pos += (size_t)fsize;
        pos += (512 - ((size_t)fsize % 512)) % 512;
        fcount++;
    }
    fprintf(stderr, "mz: extracted %d file(s)\n", fcount);
    return 0;
}

/* ===================================================================
 * 列表（列出内容）
 * =================================================================== */

static int
list_mz(const uint8_t *data, size_t len)
{
    void *ctx = NULL;
    int rc = mz2_open(data, len, &ctx);
    if (rc != MZ_OK) { fprintf(stderr, "mz: mz2_open: %s\n", mz_strerror(rc)); return 1; }

    struct mz_file_entry *fent = NULL;
    int nf = 0;
    mz2_list_files(ctx, &fent, &nf);
    printf("%-30s %10s %10s %6s\n", "Name", "Size", "Csize", "Ratio");
    printf("---------------------------------------------------------------\n");
    for (int i = 0; i < nf; i++) {
        double ratio = fent[i].size > 0
            ? 100.0 * (double)fent[i].csize / (double)fent[i].size
            : 0.0;
        printf("%-30s %10u %10u %5.1f%%\n",
               fent[i].name, fent[i].size, fent[i].csize, ratio);
    }
    printf("Total: %d file(s)\n", nf);
    mz2_close(ctx);
    return 0;
}

static int
list_mxa(const uint8_t *data, size_t len)
{
    (void)len;
    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
    if (rc != MXA_OK) { fprintf(stderr, "mz: mxa_open: %s\n", mxa_strerror(rc)); return 1; }

    struct mxa_file_entry *entries = NULL;
    int count = 0;
    mxa_list_files(ctx, &entries, &count);
    printf("%-30s %10s %10s %6s  %s\n", "Name", "Size", "Csize", "Ratio", "Codec");
    printf("---------------------------------------------------------------\n");
    for (int i = 0; i < count; i++) {
        double ratio = entries[i].size > 0
            ? 100.0 * (double)entries[i].csize / (double)entries[i].size
            : 0.0;
        const char *codec_str = (entries[i].codec == MXA_CODEC_STORED) ? "STORED"
                               : (entries[i].codec == MXA_CODEC_MEUOS) ? "MEUOS" : "?";
        printf("%-30s %10llu %10llu %5.1f%%  %s\n",
               entries[i].name,
               (unsigned long long)entries[i].size,
               (unsigned long long)entries[i].csize, ratio, codec_str);
    }
    free(entries);
    mxa_close(ctx);
    return 0;
}

static int
list_zip(const uint8_t *data, size_t len)
{
    struct mz_zip_reader *reader = NULL;
    int rc = mz_zip_reader_open(&reader, data, len);
    if (rc != MZ_OK) { fprintf(stderr, "mz: zip open: %s\n", mz_strerror(rc)); return 1; }

    int count = mz_zip_reader_count(reader);
    printf("%-30s %10s %6s\n", "Name", "Size", "Method");
    printf("------------------------------------------\n");
    for (int i = 0; i < count; i++) {
        const struct mz_zip_entry *e = mz_zip_reader_entry(reader, i);
        if (!e) continue;
        const char *method = (e->method == 0) ? "STORE" : (e->method == 8) ? "DEFLATE" : "?";
        printf("%-30s %10u %s\n", e->name, e->uncompressed_size, method);
    }
    mz_zip_reader_close(reader);
    return 0;
}

static int
list_tar(const uint8_t *data, size_t len)
{
    const uint8_t *p = data;
    size_t pos = 0;
    int fcount = 0;
    printf("%-30s %10s\n", "Name", "Size");
    printf("------------------------------------------\n");
    while (pos + 512 <= len) {
        const uint8_t *hdr = p + pos;
        int zero = 1;
        for (int j = 0; j < 512; j++) { if (hdr[j] != 0) { zero = 0; break; } }
        if (zero) break;
        if (memcmp(hdr + 257, "ustar", 5) != 0) break;

        unsigned long long fsize = 0;
        sscanf((const char *)hdr + 124, "%011llo", &fsize);
        char name[101];
        memcpy(name, hdr, 100); name[100] = '\0';
        if (name[0]) {
            printf("%-30s %10llu\n", name, fsize);
            fcount++;
        }
        pos += 512;
        pos += (size_t)fsize;
        pos += (512 - ((size_t)fsize % 512)) % 512;
    }
    printf("Total: %d file(s)\n", fcount);
    return 0;
}

/* ===================================================================
 * 测试完整性
 * =================================================================== */

static int
test_archive(const uint8_t *data, size_t len, enum mz_fmt fmt)
{
    fprintf(stderr, "mz: testing %s format\n", fmt_name(fmt));

    switch (fmt) {
    case FMT_MZ: {
        void *ctx = NULL;
        int rc = mz2_open(data, len, &ctx);
        if (rc != MZ_OK) { fprintf(stderr, "FAIL: mz2_open: %s\n", mz_strerror(rc)); return 1; }
        struct mz_file_entry *fent = NULL; int nf = 0;
        mz2_list_files(ctx, &fent, &nf);
        int ok = 1;
        for (int fi = 0; fi < nf; fi++) {
            void *fd; size_t fsz;
            if (mz2_read_file(ctx, fent[fi].name, &fd, &fsz) != MZ_OK) {
                fprintf(stderr, "  FAIL: '%s'\n", fent[fi].name); ok = 0;
            } else { free(fd); }
        }
        mz2_close(ctx);
        if (ok) printf("PASS: all %d file(s) readable\n", nf);
        else    fprintf(stderr, "FAIL: some files unreadable\n");
        return ok ? 0 : 1;
    }
    case FMT_MXA: {
        void *ctx = NULL;
        int rc = mxa_open(data, len, &ctx);
        if (rc != MXA_OK) { fprintf(stderr, "FAIL: mxa_open: %s\n", mxa_strerror(rc)); return 1; }
        struct mxa_file_entry *entries = NULL; int count = 0;
        mxa_list_files(ctx, &entries, &count);
        int ok = 1;
        for (int i = 0; i < count; i++) {
            void *fd; size_t fsz;
            if (mxa_read_file(ctx, entries[i].name, &fd, &fsz) != MXA_OK) {
                fprintf(stderr, "  FAIL: '%s'\n", entries[i].name); ok = 0;
            } else { free(fd); }
        }
        free(entries); mxa_close(ctx);
        if (ok) printf("PASS: all %d file(s) readable\n", count);
        else    fprintf(stderr, "FAIL: some files unreadable\n");
        return ok ? 0 : 1;
    }
    case FMT_GZIP: {
        void *decomp = NULL; size_t decomp_len = 0;
        int rc = mz_gzip_decompress(data, len, &decomp, &decomp_len);
        if (rc > 0) { printf("PASS: crc ok (%zu bytes)\n", decomp_len); free(decomp); return 0; }
        else        { fprintf(stderr, "FAIL: %s\n", mz_strerror(rc)); return 1; }
    }
    case FMT_ZIP: {
        struct mz_zip_reader *reader = NULL;
        int rc = mz_zip_reader_open(&reader, data, len);
        if (rc != MZ_OK) { fprintf(stderr, "FAIL: zip open: %s\n", mz_strerror(rc)); return 1; }
        int count = mz_zip_reader_count(reader);
        int ok = 1;
        for (int i = 0; i < count; i++) {
            void *fd; size_t fsz;
            if (mz_zip_reader_extract(reader, i, &fd, &fsz) <= 0) {
                fprintf(stderr, "  FAIL: entry %d\n", i); ok = 0;
            } else { free(fd); }
        }
        mz_zip_reader_close(reader);
        if (ok) printf("PASS: all %d file(s) extracted ok\n", count);
        else    fprintf(stderr, "FAIL: some entries failed\n");
        return ok ? 0 : 1;
    }
    case FMT_TAR: {
        const uint8_t *p = data; size_t pos = 0;
        int ok = 1, fcount = 0;
        while (pos + 512 <= len) {
            const uint8_t *hdr = p + pos;
            int zero = 1;
            for (int j = 0; j < 512; j++) { if (hdr[j] != 0) { zero = 0; break; } }
            if (zero) break;
            if (memcmp(hdr + 257, "ustar", 5) != 0) { ok = 0; break; }
            unsigned long long fsize = 0;
            sscanf((const char *)hdr + 124, "%011llo", &fsize);
            pos += 512;
            if (pos + (size_t)fsize > len) { ok = 0; break; }
            pos += (size_t)fsize;
            pos += (512 - ((size_t)fsize % 512)) % 512;
            fcount++;
        }
        if (ok) printf("PASS: %d file(s), structure valid\n", fcount);
        else    fprintf(stderr, "FAIL: corrupt or truncated tar\n");
        return ok ? 0 : 1;
    }
    default:
        fprintf(stderr, "FAIL: test not supported for this format\n");
        return 1;
    }
}

/* ===================================================================
 * 自动格式检测（打开文件后，从扩展名或 magic 确定格式）
 * =================================================================== */

static enum mz_fmt
detect_format(const char *path, const uint8_t *data, size_t len)
{
    enum mz_fmt fmt = fmt_from_ext(path);
    if (fmt != FMT_UNKNOWN) return fmt;
    return fmt_from_magic(data, len);
}

/* ===================================================================
 * 子命令: c (compress)
 * =================================================================== */

static int
cmd_compress(int argc, char *argv[])
{
    const char *outfile = NULL;
    int level = 6;
    int arg_i;

    /* Parse options */
    for (arg_i = 0; arg_i < argc; arg_i++) {
        if (strcmp(argv[arg_i], "-o") == 0) {
            if (++arg_i >= argc) { fprintf(stderr, "mz c: -o needs argument\n"); return 1; }
            outfile = argv[arg_i];
        } else if (strcmp(argv[arg_i], "-l") == 0) {
            if (++arg_i >= argc) { fprintf(stderr, "mz c: -l needs argument\n"); return 1; }
            level = atoi(argv[arg_i]);
            if (level < 0 || level > 9) { fprintf(stderr, "mz c: level must be 0-9\n"); return 1; }
        } else if (argv[arg_i][0] == '-') {
            fprintf(stderr, "mz c: unknown option '%s'\n", argv[arg_i]);
            return 1;
        } else {
            break;
        }
    }

    int nfiles = argc - arg_i;
    char **filenames = argv + arg_i;

    if (nfiles == 0) { fprintf(stderr, "mz c: no input files\n"); return 1; }

    /* 确定输出格式 */
    enum mz_fmt out_fmt = FMT_MZ; /* default: MZv2 */
    if (outfile) {
        enum mz_fmt ext_fmt = fmt_from_ext(outfile);
        if (ext_fmt != FMT_UNKNOWN) out_fmt = ext_fmt;
    }

    switch (out_fmt) {
    case FMT_MZ:   return write_mz_container(outfile, nfiles, filenames, level);
    case FMT_GZIP: return write_gzip(outfile, nfiles, filenames);
    case FMT_MXA:  return write_mxa(outfile, nfiles, filenames, level);
    case FMT_TAR:  return write_tar(outfile, nfiles, filenames);
    default:
        fprintf(stderr, "mz c: unsupported output format for '%s'\n", outfile ? outfile : "(default)");
        return 1;
    }
}

/* ===================================================================
 * 子命令: d (decompress/extract)
 * =================================================================== */

static int
cmd_decompress(int argc, char *argv[])
{
    if (argc < 1) { fprintf(stderr, "mz d: <archive> required\n"); return 1; }
    const char *path = argv[0];

    size_t len;
    void *data = read_whole(path, &len);
    if (!data) return 1;

    enum mz_fmt fmt = detect_format(path, (const uint8_t *)data, len);
    fprintf(stderr, "mz: detected %s format\n", fmt_name(fmt));

    int rc;
    switch (fmt) {
    case FMT_MZ:   rc = extract_mz(data, len, path);   break;
    case FMT_MXA:  rc = extract_mxa(data, len, path);  break;
    case FMT_GZIP: rc = extract_gzip(data, len);        break;
    case FMT_ZIP:  rc = extract_zip(data, len);         break;
    case FMT_TAR:  rc = extract_tar(data, len);         break;
    default:
        fprintf(stderr, "mz: unknown or unsupported format\n");
        rc = 1;
        break;
    }

    free(data);
    return rc;
}

/* ===================================================================
 * 子命令: l (list)
 * =================================================================== */

static int
cmd_list(int argc, char *argv[])
{
    if (argc < 1) { fprintf(stderr, "mz l: <archive> required\n"); return 1; }
    const char *path = argv[0];

    size_t len;
    void *data = read_whole(path, &len);
    if (!data) return 1;

    enum mz_fmt fmt = detect_format(path, (const uint8_t *)data, len);
    printf("Format: %s\n", fmt_name(fmt));
    printf("Size:   %zu bytes\n", len);

    int rc;
    switch (fmt) {
    case FMT_MZ:   rc = list_mz(data, len);   break;
    case FMT_MXA:  rc = list_mxa(data, len);  break;
    case FMT_GZIP: printf("Gzip archive (single stream)\n"); rc = 0; break;
    case FMT_ZIP:  rc = list_zip(data, len);  break;
    case FMT_TAR:  rc = list_tar(data, len);  break;
    default:
        fprintf(stderr, "Unknown format\n");
        rc = 1;
        break;
    }

    free(data);
    return rc;
}

/* ===================================================================
 * 子命令: t (test)
 * =================================================================== */

static int
cmd_test(int argc, char *argv[])
{
    if (argc < 1) { fprintf(stderr, "mz t: <archive> required\n"); return 1; }
    const char *path = argv[0];

    size_t len;
    void *data = read_whole(path, &len);
    if (!data) return 1;

    enum mz_fmt fmt = detect_format(path, (const uint8_t *)data, len);
    printf("Format: %s\n", fmt_name(fmt));
    printf("Size:   %zu bytes\n", len);

    int rc = test_archive((const uint8_t *)data, len, fmt);

    free(data);
    return rc;
}

/* ===================================================================
 * argv[0] 软链识别
 * =================================================================== */

enum mz_mode {
    MODE_MZ,        /* mz c/d/l/t — 子命令模式 */
    MODE_GUNZIP,    /* gunzip → mz d file.gz */
    MODE_UNZIP,     /* unzip → mz d file.zip */
    MODE_BUNZIP2,   /* bunzip2 → mz d file.bz2 */
    MODE_TAR,       /* tar → mz c 或 mz d */
    MODE_ZSTD,      /* zstd/unzstd → mz c 或 mz d */
};

static enum mz_mode
detect_mode(const char *argv0)
{
    /* Extract basename in case of path */
    const char *base = strrchr(argv0, '/');
    base = base ? base + 1 : argv0;

    if (strcmp(base, "gunzip") == 0 || strcmp(base, "gunzip2") == 0)
        return MODE_GUNZIP;
    if (strcmp(base, "unzip") == 0)
        return MODE_UNZIP;
    if (strcmp(base, "bunzip2") == 0 || strcmp(base, "bunzip") == 0)
        return MODE_BUNZIP2;
    if (strcmp(base, "tar") == 0)
        return MODE_TAR;
    if (strcmp(base, "zstd") == 0 || strcmp(base, "unzstd") == 0 ||
        strcmp(base, "zstdcat") == 0 || strcmp(base, "zstdmt") == 0)
        return MODE_ZSTD;

    return MODE_MZ; /* default: subcommand mode */
}

/* ===================================================================
 * 用法帮助
 * =================================================================== */

static void
print_usage(const char *prog)
{
    const char *base = strrchr(prog, '/');
    base = base ? base + 1 : prog;

    fprintf(stderr,
        "Usage: %s <command> [options]\n"
        "\n"
        "Commands:\n"
        "  c [-l LV] [-o OUTFILE] <files...>   Compress\n"
        "  d <archive>                          Decompress/extract\n"
        "  l <archive>                          List archive contents\n"
        "  t <archive>                          Test archive integrity\n"
        "\n"
        "Options:\n"
        "  -l LV    Compression level (0-9, 0=auto, 6=default)\n"
        "  -o FILE  Output file (extention determines format)\n"
        "\n"
        "Formats (auto-detected by extension or magic):\n"
        "  .mz      MZv2 (self-describing MeuOS container)\n"
        "  .mxa     MxA (MeuOS Archive)\n"
        "  .gz      Gzip\n"
        "  .zip     Zip\n"
        "  .tar     Tar\n"
        "\n"
        "argv[0] soft-link modes:\n"
        "  gunzip   ≡ mz d <file>\n"
        "  unzip    ≡ mz d <file>\n"
        "  tar      ≡ mz c / mz d based on arguments\n"
        "  zstd     ≡ mz c / mz d based on arguments\n",
        base);
}

/* ===================================================================
 * main
 * =================================================================== */

int main(int argc, char *argv[])
{
    if (argc < 2) { print_usage(argv[0]); return 1; }

    /* 检查 argv[0] 软链模式 */
    enum mz_mode mode = detect_mode(argv[0]);

    if (mode == MODE_MZ) {
        /* 子命令模式 */
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        const char *cmd = argv[1];
        int cmd_argc = argc - 2;
        char **cmd_argv = argv + 2;

        if (strcmp(cmd, "c") == 0)
            return cmd_compress(cmd_argc, cmd_argv);
        if (strcmp(cmd, "d") == 0)
            return cmd_decompress(cmd_argc, cmd_argv);
        if (strcmp(cmd, "l") == 0)
            return cmd_list(cmd_argc, cmd_argv);
        if (strcmp(cmd, "t") == 0)
            return cmd_test(cmd_argc, cmd_argv);

        fprintf(stderr, "mz: unknown command '%s'\n", cmd);
        print_usage(argv[0]);
        return 1;
    }

    if (mode == MODE_GUNZIP) {
        /* gunzip file.gz → mz d file.gz */
        return cmd_decompress(argc - 1, argv + 1);
    }

    if (mode == MODE_UNZIP) {
        /* unzip file.zip → mz d file.zip */
        return cmd_decompress(argc - 1, argv + 1);
    }

    if (mode == MODE_BUNZIP2) {
        /* bunzip2 file.bz2 → mz d (但 bz2 尚未在 libmz 实现) */
        fprintf(stderr, "mz: bzip2 support not yet in libmz (CLI layer only)\n");
        return 1;
    }

    if (mode == MODE_TAR) {
        /* tar xf → extract, tar cf → compress */
        /* Parse first argument: if "x" + "f" or "xf" → extract */
        /* If "c" + "f" or "cf" → compress */
        int tar_argc = argc - 1;
        char **tar_argv = argv + 1;

        /* Look for -x (extract), -c (create), -t (list) */
        int has_x = 0, has_c = 0, has_t = 0, has_f = 0;
        const char *tar_file = NULL;
        int file_start = 0;

        for (int i = 0; i < tar_argc; i++) {
            if (tar_argv[i][0] == '-') {
                const char *opt = tar_argv[i] + 1;
                while (*opt) {
                    if (*opt == 'x') has_x = 1;
                    else if (*opt == 'c') has_c = 1;
                    else if (*opt == 't') has_t = 1;
                    else if (*opt == 'f') has_f = 1;
                    else if (*opt == 'v') { /* verbose, ignore */ }
                    opt++;
                }
            } else if (has_f && !tar_file) {
                tar_file = tar_argv[i];
                file_start = i + 1;
            } else if (!tar_file) {
                /* First non-option is the file if -f already seen */
                /* Actually GNU tar: after -f, the next arg is the archive */
                file_start = i;
                break;
            }
        }

        if (!tar_file) {
            fprintf(stderr, "mz: tar mode needs -f <archive>\n");
            return 1;
        }

        if (has_t) {
            /* tar tf archive → mz l archive */
            int new_argc = 1;
            char *new_argv[2] = { (char *)tar_file, NULL };
            return cmd_list(new_argc, new_argv);
        }
        if (has_x) {
            /* tar xf archive → mz d archive */
            int new_argc = 1;
            char *new_argv[2] = { (char *)tar_file, NULL };
            return cmd_decompress(new_argc, new_argv);
        }
        if (has_c) {
            /* tar cf archive files... → mz c -o archive files... */
            /* Build argc/argv for compress */
            int new_argc = 2 + (tar_argc - file_start);
            char **new_argv = malloc((size_t)(new_argc + 1) * sizeof(char *));
            new_argv[0] = "-o";
            new_argv[1] = (char *)tar_file;
            for (int i = file_start; i < tar_argc; i++)
                new_argv[i - file_start + 2] = tar_argv[i];
            int rc = cmd_compress(new_argc, new_argv);
            free(new_argv);
            return rc;
        }

        fprintf(stderr, "mz: tar mode: use 'tar xf' or 'tar cf'\n");
        return 1;
    }

    if (mode == MODE_ZSTD) {
        /* zstd file → compress to file.zst */
        /* unzstd file.zst → decompress */
        const char *base = strrchr(argv[0], '/');
        base = base ? base + 1 : argv[0];
        int is_compress = (strcmp(base, "zstd") == 0);

        /* Simple: first non-option arg is the file */
        const char *target_file = NULL;
        int has_d = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decompress") == 0)
                has_d = 1;
            else if (argv[i][0] != '-' && !target_file)
                target_file = argv[i];
        }

        if (!target_file) {
            fprintf(stderr, "mz: zstd mode needs an input file\n");
            return 1;
        }

        if (has_d || !is_compress) {
            /* Decompress */
            char *new_argv[2] = { (char *)target_file, NULL };
            return cmd_decompress(1, new_argv);
        } else {
            /* Compress: auto output name */
            char outname[1024];
            snprintf(outname, sizeof(outname), "%s.zst", target_file);
            char *new_argv[4] = { "-o", outname, (char *)target_file, NULL };
            return cmd_compress(3, new_argv);
        }
    }

    print_usage(argv[0]);
    return 1;
}