/* mxz_cli.c — mxz 统一 CLI 工具
 *
 * 语法:
 *   mxz c [--format=FORMAT] [-o outfile] <files...>
 *   mxz d <archive>
 *   mxz l <archive>
 *   mxz t <archive>
 *   mxz --help
 *
 * 格式自动识别（解压/列表/测试）:
 *   MZv2 ("MZv2"), MxA1 ("MxA1"),
 *   gzip (0x1F 0x8B), zip (PK\x03\x04), tar (ustar),
 *   raw LZ77 ("mZ" / "MZ")
 */
#include "mz.h"
#include "mxa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

/* -------------------------------------------------------------------
 * 格式嗅探
 * ------------------------------------------------------------------- */

enum mxz_format {
    FMT_UNKNOWN = 0,
    FMT_MZv2,
    FMT_MxA1,
    FMT_GZIP,
    FMT_ZIP,
    FMT_TAR,
    FMT_RAW_LZ77
};

static enum mxz_format
sniff_format(const uint8_t *data, size_t len)
{
    if (len < 4) return FMT_UNKNOWN;

    /* MZv2: "MZv2" */
    if (data[0] == 'M' && data[1] == 'Z' && data[2] == 'v' && data[3] == '2')
        return FMT_MZv2;

    /* MxA1: "MxA1" (0x3141784D in LE) */
    if (data[0] == 'M' && data[1] == 'x' && data[2] == 'A' && data[3] == '1')
        return FMT_MxA1;

    /* gzip: 0x1F 0x8B */
    if (len >= 2 && data[0] == 0x1F && data[1] == 0x8B)
        return FMT_GZIP;

    /* zip: PK\x03\x04 */
    if (len >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04)
        return FMT_ZIP;

    /* tar: ustar magic at offset 257 */
    if (len >= 262 && memcmp(data + 257, "ustar", 5) == 0)
        return FMT_TAR;

    /* raw LZ77 v1/v2: "MZ" / "mZ" */
    if ((data[0] == 'M' && data[1] == 'Z') ||
        (data[0] == 'm' && data[1] == 'Z'))
        return FMT_RAW_LZ77;

    return FMT_UNKNOWN;
}

static const char *
format_name(enum mxz_format fmt)
{
    switch (fmt) {
    case FMT_MZv2:     return "MZv2";
    case FMT_MxA1:     return "MxA";
    case FMT_GZIP:     return "Gzip";
    case FMT_ZIP:      return "Zip";
    case FMT_TAR:      return "Tar";
    case FMT_RAW_LZ77: return "LZ77";
    default:           return "Unknown";
    }
}

/* -------------------------------------------------------------------
 * 文件 I/O 工具
 * ------------------------------------------------------------------- */

static void *
read_whole(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mxz: cannot open '%s': %s\n", path, strerror(errno)); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long flen = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)(flen > 0 ? flen : 1));
    if (!buf) { fprintf(stderr, "mxz: out of memory\n"); fclose(f); return NULL; }
    if (flen > 0 && fread(buf, 1, (size_t)flen, f) != (size_t)flen) {
        fprintf(stderr, "mxz: short read on '%s'\n", path);
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
    if (!f) { fprintf(stderr, "mxz: cannot create '%s': %s\n", path, strerror(errno)); return 1; }
    if (len > 0) fwrite(data, 1, len, f);
    fclose(f);
    return 0;
}

/* -------------------------------------------------------------------
 * 命令: mxz c — 压缩
 * ------------------------------------------------------------------- */

static int
cmd_compress(int argc, char *argv[])
{
    const char *outfile = NULL;
    const char *format = "mz";  /* default: MZv2 */
    int level = 6;

    /* Parse options */
    int i = 0;
    while (i < argc && argv[i][0] == '-') {
        if (strncmp(argv[i], "--format=", 9) == 0) {
            format = argv[i] + 9;
        } else if (strncmp(argv[i], "-o", 2) == 0) {
            if (i + 1 < argc && argv[i][2] == '\0')
                outfile = argv[++i];
            else if (argv[i][2] != '\0')
                outfile = argv[i] + 2;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fprintf(stderr, "usage: mxz c [--format=FMT] [-l LV] -o OUTFILE FILES...\n");
            fprintf(stderr, "  formats: mz, gz, mxa, tar\n");
            return 0;
        } else {
            fprintf(stderr, "mxz c: unknown option '%s'\n", argv[i]);
            return 1;
        }
        i++;
    }

    if (!outfile) { fprintf(stderr, "mxz c: -o OUTFILE is required\n"); return 1; }
    if (i >= argc) { fprintf(stderr, "mxz c: no input files\n"); return 1; }

    /* Read all input files */
    int nfiles = argc - i;
    char **filenames = argv + i;

    if (strcmp(format, "mz") == 0 || strcmp(format, "mzv2") == 0) {
        /* MZv2 container */
        struct mz_params params;
        memset(&params, 0, sizeof(params));
        params.level = level;
        params.flags = 0;

        void *ctx = NULL;
        int rc = mz2_create(&ctx, NULL, &params);
        if (rc != MZ_OK) { fprintf(stderr, "mxz: mz2_create: %s\n", mz_strerror(rc)); return 1; }

        for (int fi = 0; fi < nfiles; fi++) {
            size_t sz;
            void *d = read_whole(filenames[fi], &sz);
            if (!d) return 1;
            const char *name = strrchr(filenames[fi], '/');
            name = name ? name + 1 : filenames[fi];
            rc = mz2_add_file(ctx, name, d, sz, 0644);
            free(d);
            if (rc != MZ_OK) { fprintf(stderr, "mxz: mz2_add_file('%s'): %s\n", name, mz_strerror(rc)); return 1; }
        }

        void *result; size_t result_len;
        rc = mz2_finish(ctx, &result, &result_len);
        if (rc != MZ_OK) { fprintf(stderr, "mxz: mz2_finish: %s\n", mz_strerror(rc)); return 1; }

        int r = write_buf(outfile, result, result_len);
        free(result);
        return r;
    } else if (strcmp(format, "gz") == 0 || strcmp(format, "gzip") == 0) {
        /* Gzip: compress single file */
        if (nfiles != 1) { fprintf(stderr, "mxz: gzip only supports single input file\n"); return 1; }

        size_t sz; void *d = read_whole(filenames[0], &sz);
        if (!d) return 1;
        void *result; size_t result_len;
        int rc = mz_gzip_compress(d, sz, &result, &result_len);
        free(d);
        if (rc <= 0) { fprintf(stderr, "mxz: gzip compress failed: %s\n", mz_strerror(rc)); return 1; }
        int r = write_buf(outfile, result, result_len);
        free(result);
        return r;
    } else if (strcmp(format, "mxa") == 0) {
        /* MxA archive */
        struct mxa_params params;
        memset(&params, 0, sizeof(params));
        params.level = level;
        params.flags = 0;

        void *ctx = NULL;
        int rc = mxa_create(&ctx, &params);
        if (rc != MXA_OK) { fprintf(stderr, "mxz: mxa_create: %s\n", mxa_strerror(rc)); return 1; }

        for (int fi = 0; fi < nfiles; fi++) {
            size_t sz; void *d = read_whole(filenames[fi], &sz);
            if (!d) return 1;
            const char *name = strrchr(filenames[fi], '/');
            name = name ? name + 1 : filenames[fi];
            rc = mxa_add_file(ctx, name, d, sz, 0644, 0, 0, 0);
            free(d);
            if (rc != MXA_OK) { fprintf(stderr, "mxz: mxa_add_file('%s'): %s\n", name, mxa_strerror(rc)); return 1; }
        }

        void *result; size_t result_len;
        rc = mxa_finish(ctx, &result, &result_len);
        if (rc != MXA_OK) { fprintf(stderr, "mxz: mxa_finish: %s\n", mxa_strerror(rc)); return 1; }

        int r = write_buf(outfile, result, result_len);
        free(result);
        return r;
    } else if (strcmp(format, "tar") == 0) {
        /* Tar: write POSIX ustar tar */
        FILE *f = fopen(outfile, "wb");
        if (!f) { fprintf(stderr, "mxz: cannot create '%s': %s\n", outfile, strerror(errno)); return 1; }

        char zero[1024];
        memset(zero, 0, sizeof(zero));

        for (int fi = 0; fi < nfiles; fi++) {
            size_t sz; void *d = read_whole(filenames[fi], &sz);
            if (!d) { fclose(f); return 1; }
            const char *name = strrchr(filenames[fi], '/');
            name = name ? name + 1 : filenames[fi];

            /* ustar header (512 bytes) */
            char hdr[512];
            memset(hdr, 0, sizeof(hdr));

            size_t nlen = strlen(name);
            if (nlen > 99) nlen = 99;
            memcpy(hdr, name, nlen);
            /* mode (8 oct) */
            snprintf(hdr + 100, 8, "%07o", 0644);
            /* uid */
            snprintf(hdr + 108, 8, "%07o", 0);
            /* gid */
            snprintf(hdr + 116, 8, "%07o", 0);
            /* size */
            snprintf(hdr + 124, 12, "%011llo", (unsigned long long)sz);
            /* mtime */
            snprintf(hdr + 136, 12, "%011llo", (unsigned long long)0);
            /* checksum (blank) */
            memset(hdr + 148, ' ', 8);
            /* typeflag = '0' regular */
            hdr[156] = '0';
            /* magic + version */
            memcpy(hdr + 257, "ustar", 5);
            memcpy(hdr + 263, "00", 2);

            /* Compute checksum */
            unsigned chk = 0;
            for (int j = 0; j < 512; j++) chk += (unsigned char)hdr[j];
            snprintf(hdr + 148, 8, "%06o", chk);
            hdr[154] = ' ';
            hdr[155] = ' ';

            fwrite(hdr, 1, 512, f);
            if (sz > 0) fwrite(d, 1, sz, f);
            /* pad to 512 boundary */
            size_t pad = (512 - (sz % 512)) % 512;
            if (pad > 0) fwrite(zero, 1, pad, f);
            free(d);
        }

        /* Two 512-byte zero blocks = end marker */
        fwrite(zero, 1, 1024, f);
        fclose(f);
        return 0;
    } else {
        fprintf(stderr, "mxz c: unknown format '%s'\n", format);
        return 1;
    }
}

/* -------------------------------------------------------------------
 * 命令: mxz d — 解压
 * ------------------------------------------------------------------- */

static int
cmd_decompress(int argc, char *argv[])
{
    if (argc < 1) { fprintf(stderr, "usage: mxz d <archive>\n"); return 1; }
    const char *path = argv[0];

    size_t len;
    void *data = read_whole(path, &len);
    if (!data) return 1;

    enum mxz_format fmt = sniff_format((const uint8_t *)data, len);
    printf("mxz: detected format: %s\n", format_name(fmt));

    int rc = 0;

    switch (fmt) {
    case FMT_MZv2: {
        void *ctx = NULL;
        rc = mz2_open(data, len, &ctx);
        if (rc != MZ_OK) { fprintf(stderr, "mxz: mz2_open: %s\n", mz_strerror(rc)); free(data); return 1; }

        struct mz_file_entry *fent = NULL;
        int nf = 0;
        mz2_list_files(ctx, &fent, &nf);
        printf("mxz: %d file(s) in archive\n", nf);

        /* Decompress each file */
        for (int fi = 0; fi < nf; fi++) {
            void *fd; size_t fsz;
            rc = mz2_read_file(ctx, fent[fi].name, &fd, &fsz);
            if (rc != MZ_OK) {
                fprintf(stderr, "  skip '%s': %s\n", fent[fi].name, mz_strerror(rc));
                continue;
            }
            /* Extract to file named after entry */
            const char *ename = fent[fi].name;
            if (ename[0] == '/' || strstr(ename, "..")) {
                fprintf(stderr, "  skip '%s': unsafe path\n", ename);
                free(fd); continue;
            }
            FILE *f = fopen(ename, "wb");
            if (!f) {
                fprintf(stderr, "  skip '%s': cannot create: %s\n", ename, strerror(errno));
                free(fd); continue;
            }
            if (fsz > 0) fwrite(fd, 1, fsz, f);
            fclose(f);
            printf("  extracted: %s (%zu bytes)\n", ename, fsz);
            free(fd);
        }
        mz2_close(ctx);
        break;
    }
    case FMT_MxA1: {
        void *ctx = NULL;
        rc = mxa_open(data, len, &ctx);
        if (rc != MXA_OK) { fprintf(stderr, "mxz: mxa_open: %s\n", mxa_strerror(rc)); free(data); return 1; }

        struct mxa_file_entry *entries = NULL;
        int count = 0;
        mxa_list_files(ctx, &entries, &count);
        printf("mxz: %d file(s) in archive\n", count);

        for (int i = 0; i < count; i++) {
            const char *name = entries[i].name;
            /* Sanitize name */
            if (name[0] == '/' || strstr(name, "..")) {
                fprintf(stderr, "  skip '%s': unsafe path\n", name);
                continue;
            }
            void *fd; size_t fsz;
            rc = mxa_read_file(ctx, name, &fd, &fsz);
            if (rc != MXA_OK) {
                fprintf(stderr, "  skip '%s': %s\n", name, mxa_strerror(rc));
                continue;
            }
            /* Ensure parent dir exists */
            const char *slash = strrchr(name, '/');
            if (slash) {
                char dir[1024];
                size_t dlen = (size_t)(slash - name);
                if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
                memcpy(dir, name, dlen); dir[dlen] = '\0';
                mkdir(dir, 0755);
            }
            FILE *f = fopen(name, "wb");
            if (!f) {
                fprintf(stderr, "  skip '%s': cannot create: %s\n", name, strerror(errno));
                free(fd); continue;
            }
            if (fsz > 0) fwrite(fd, 1, fsz, f);
            fclose(f);
            printf("  extracted: %s (%zu bytes)\n", name, fsz);
            free(fd);
        }
        free(entries);
        mxa_close(ctx);
        break;
    }
    case FMT_GZIP: {
        void *decomp = NULL; size_t decomp_len = 0;
        rc = mz_gzip_decompress(data, len, &decomp, &decomp_len);
        if (rc <= 0) { fprintf(stderr, "mxz: gzip decompress: %s\n", mz_strerror(rc)); free(data); return 1; }
        fprintf(stderr, "mxz: decompressed %zu bytes to stdout\n", decomp_len);
        fwrite(decomp, 1, decomp_len, stdout);
        free(decomp);
        break;
    }
    case FMT_ZIP: {
        struct mz_zip_reader *reader = NULL;
        rc = mz_zip_reader_open(&reader, data, len);
        if (rc != MZ_OK) { fprintf(stderr, "mxz: zip open: %s\n", mz_strerror(rc)); free(data); return 1; }

        int count = mz_zip_reader_count(reader);
        printf("mxz: %d file(s) in zip archive\n", count);

        for (int i = 0; i < count; i++) {
            const struct mz_zip_entry *e = mz_zip_reader_entry(reader, i);
            if (!e) continue;
            void *fd; size_t fsz;
            rc = mz_zip_reader_extract(reader, i, &fd, &fsz);
            if (rc <= 0) {
                fprintf(stderr, "  skip '%s': extract failed\n", e->name);
                continue;
            }
            /* Ensure parent dir */
            const char *slash = strrchr(e->name, '/');
            if (slash) {
                char dir[1024];
                size_t dlen = (size_t)(slash - e->name);
                if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
                memcpy(dir, e->name, dlen); dir[dlen] = '\0';
                mkdir(dir, 0755);
            }
            FILE *f = fopen(e->name, "wb");
            if (!f) {
                fprintf(stderr, "  skip '%s': cannot create: %s\n", e->name, strerror(errno));
                free(fd); continue;
            }
            if (fsz > 0) fwrite(fd, 1, fsz, f);
            fclose(f);
            printf("  extracted: %s (%zu bytes)\n", e->name, fsz);
            free(fd);
        }
        mz_zip_reader_close(reader);
        break;
    }
    case FMT_TAR: {
        /* Parse ustar tar: walk 512-byte blocks */
        const uint8_t *p = (const uint8_t *)data;
        size_t pos = 0;
        while (pos + 512 <= len) {
            const uint8_t *hdr = p + pos;
            /* Check for end marker (all zero) */
            int zero = 1;
            for (int j = 0; j < 512; j++) { if (hdr[j] != 0) { zero = 0; break; } }
            if (zero) break;

            /* Validate ustar magic */
            if (memcmp(hdr + 257, "ustar", 5) != 0) {
                fprintf(stderr, "  skip: non-ustar entry at offset %zu\n", pos);
                break;
            }

            /* Parse size (octal) */
            unsigned long long fsize = 0;
            sscanf((const char *)hdr + 124, "%011llo", &fsize);

            /* Name */
            char name[101];
            memcpy(name, hdr, 100); name[100] = '\0';
            /* Skip trailing slashes / empty names */
            if (name[0] == '\0') { pos += 512; continue; }

            pos += 512;
            if (pos + fsize > len) { fprintf(stderr, "  truncated entry '%s'\n", name); break; }

            /* Create parent dir */
            const char *slash = strrchr(name, '/');
            if (slash) {
                char dir[1024];
                size_t dlen = (size_t)(slash - name);
                if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
                memcpy(dir, name, dlen); dir[dlen] = '\0';
                mkdir(dir, 0755);
            }

            FILE *f = fopen(name, "wb");
            if (!f) {
                fprintf(stderr, "  skip '%s': cannot create: %s\n", name, strerror(errno));
            } else {
                if (fsize > 0) fwrite(p + pos, 1, (size_t)fsize, f);
                fclose(f);
                printf("  extracted: %s (%llu bytes)\n", name, fsize);
            }

            pos += (size_t)fsize;
            /* Pad to 512 boundary */
            size_t pad = (512 - ((size_t)fsize % 512)) % 512;
            pos += pad;
        }
        break;
    }
    case FMT_RAW_LZ77: {
        void *decomp = NULL; size_t decomp_len = 0;
        rc = mz_decompress(data, len, &decomp, &decomp_len, MZ_CODEC_LZ77);
        if (rc <= 0) { fprintf(stderr, "mxz: LZ77 decompress: %s\n", mz_strerror(rc)); free(data); return 1; }
        fprintf(stderr, "mxz: decompressed %zu bytes to stdout\n", decomp_len);
        fwrite(decomp, 1, decomp_len, stdout);
        free(decomp);
        break;
    }
    default:
        fprintf(stderr, "mxz: unknown or unsupported format\n");
        rc = 1;
        break;
    }

    free(data);
    return rc ? rc : 0;
}

/* -------------------------------------------------------------------
 * 命令: mxz l — 列出内容
 * ------------------------------------------------------------------- */

static int
cmd_list(int argc, char *argv[])
{
    if (argc < 1) { fprintf(stderr, "usage: mxz l <archive>\n"); return 1; }
    const char *path = argv[0];

    size_t len;
    void *data = read_whole(path, &len);
    if (!data) return 1;

    enum mxz_format fmt = sniff_format((const uint8_t *)data, len);
    printf("Format: %s\n", format_name(fmt));
    printf("Size:   %zu bytes\n", len);

    int rc = 0;

    switch (fmt) {
    case FMT_MZv2: {
        void *ctx = NULL;
        rc = mz2_open(data, len, &ctx);
        if (rc != MZ_OK) { fprintf(stderr, "  mz2_open: %s\n", mz_strerror(rc)); break; }

        struct mz_file_entry *fent = NULL;
        int nf = 0;
        mz2_list_files(ctx, &fent, &nf);
        printf("%-30s %10s %10s %6s\n", "Name", "Size", "Csize", "Ratio");
        printf("--------------------------------------------------------------------------\n");
        for (int i = 0; i < nf; i++) {
            double ratio = fent[i].size > 0
                ? 100.0 * (double)fent[i].csize / (double)fent[i].size
                : 0.0;
            printf("%-30s %10u %10u %5.1f%%\n",
                   fent[i].name, fent[i].size, fent[i].csize, ratio);
        }
        printf("Total: %d file(s)\n", nf);
        mz2_close(ctx);
        rc = 0;
        break;
    }
    case FMT_MxA1: {
        void *ctx = NULL;
        rc = mxa_open(data, len, &ctx);
        if (rc != MXA_OK) { fprintf(stderr, "  mxa_open: %s\n", mxa_strerror(rc)); break; }

        struct mxa_file_entry *entries = NULL;
        int count = 0;
        mxa_list_files(ctx, &entries, &count);

        printf("%-30s %10s %10s %6s  %s\n", "Name", "Size", "Csize", "Ratio", "Codec");
        printf("--------------------------------------------------------------------------\n");
        for (int i = 0; i < count; i++) {
            double ratio = entries[i].size > 0
                ? 100.0 * (double)entries[i].csize / (double)entries[i].size
                : 0.0;
            const char *codec_str = (entries[i].codec == MXA_CODEC_STORED) ? "STORED"
                                   : (entries[i].codec == MXA_CODEC_MEUOS) ? "MEUOS" : "?";
            printf("%-30s %10llu %10llu %5.1f%%  %s\n",
                   entries[i].name,
                   (unsigned long long)entries[i].size,
                   (unsigned long long)entries[i].csize,
                   ratio, codec_str);
        }
        free(entries);
        mxa_close(ctx);
        break;
    }
    case FMT_GZIP: {
        printf("Gzip archive (single stream)\n");
        break;
    }
    case FMT_ZIP: {
        struct mz_zip_reader *reader = NULL;
        rc = mz_zip_reader_open(&reader, data, len);
        if (rc != MZ_OK) { fprintf(stderr, "  zip open: %s\n", mz_strerror(rc)); break; }

        int count = mz_zip_reader_count(reader);
        printf("%-30s %10s %6s\n", "Name", "Size", "Method");
        printf("------------------------------------------------------\n");
        for (int i = 0; i < count; i++) {
            const struct mz_zip_entry *e = mz_zip_reader_entry(reader, i);
            if (!e) continue;
            const char *method = (e->method == 0) ? "STORE" : (e->method == 8) ? "DEFLATE" : "?";
            printf("%-30s %10u %s\n", e->name, e->uncompressed_size, method);
        }
        mz_zip_reader_close(reader);
        break;
    }
    case FMT_TAR: {
        const uint8_t *p = (const uint8_t *)data;
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
        break;
    }
    case FMT_RAW_LZ77: {
        printf("Raw LZ77 stream\n");
        break;
    }
    default:
        fprintf(stderr, "Unknown format\n");
        rc = 1;
    }

    free(data);
    return rc;
}

/* -------------------------------------------------------------------
 * 命令: mxz t — 测试完整性
 * ------------------------------------------------------------------- */

static int
cmd_test(int argc, char *argv[])
{
    if (argc < 1) { fprintf(stderr, "usage: mxz t <archive>\n"); return 1; }
    const char *path = argv[0];

    size_t len;
    void *data = read_whole(path, &len);
    if (!data) return 1;

    enum mxz_format fmt = sniff_format((const uint8_t *)data, len);
    printf("Format: %s\n", format_name(fmt));
    printf("Size:   %zu bytes\n", len);

    int rc = MZ_OK;

    switch (fmt) {
    case FMT_MZv2: {
        void *ctx = NULL;
        rc = mz2_open(data, len, &ctx);
        if (rc != MZ_OK) { printf("FAIL: mz2_open: %s\n", mz_strerror(rc)); break; }

        struct mz_file_entry *fent = NULL;
        int nf = 0;
        mz2_list_files(ctx, &fent, &nf);

        int ok = 1;
        for (int fi = 0; fi < nf; fi++) {
            void *fd; size_t fsz;
            if (mz2_read_file(ctx, fent[fi].name, &fd, &fsz) != MZ_OK) {
                printf("  FAIL: '%s'\n", fent[fi].name);
                ok = 0;
            } else { free(fd); }
        }
        mz2_close(ctx);
        if (ok) printf("PASS: all %d file(s) readable\n", nf);
        else    printf("FAIL: some files unreadable\n");
        break;
    }
    case FMT_MxA1: {
        void *ctx = NULL;
        rc = mxa_open(data, len, &ctx);
        if (rc == MXA_OK) {
            struct mxa_file_entry *entries = NULL;
            int count = 0;
            mxa_list_files(ctx, &entries, &count);
            int ok = 1;
            for (int i = 0; i < count; i++) {
                void *fd; size_t fsz;
                if (mxa_read_file(ctx, entries[i].name, &fd, &fsz) != MXA_OK) {
                    printf("  FAIL: '%s'\n", entries[i].name);
                    ok = 0;
                } else { free(fd); }
            }
            if (ok) printf("PASS: all %d file(s) readable\n", count);
            free(entries);
            mxa_close(ctx);
        } else {
            printf("FAIL: %s\n", mxa_strerror(rc));
        }
        break;
    }
    case FMT_GZIP: {
        void *decomp = NULL; size_t decomp_len = 0;
        rc = mz_gzip_decompress(data, len, &decomp, &decomp_len);
        if (rc > 0) { printf("PASS: crc ok (%zu bytes)\n", decomp_len); free(decomp); }
        else        { printf("FAIL: %s\n", mz_strerror(rc)); }
        break;
    }
    case FMT_ZIP: {
        struct mz_zip_reader *reader = NULL;
        rc = mz_zip_reader_open(&reader, data, len);
        if (rc == MZ_OK) {
            int count = mz_zip_reader_count(reader);
            int ok = 1;
            for (int i = 0; i < count; i++) {
                void *fd; size_t fsz;
                if (mz_zip_reader_extract(reader, i, &fd, &fsz) <= 0) {
                    printf("  FAIL: entry %d\n", i);
                    ok = 0;
                } else { free(fd); }
            }
            if (ok) printf("PASS: all %d file(s) extracted ok\n", count);
            mz_zip_reader_close(reader);
        } else {
            printf("FAIL: %s\n", mz_strerror(rc));
        }
        break;
    }
    case FMT_TAR: {
        /* Tar test: walk all entries, ensure sizes match */
        const uint8_t *p = (const uint8_t *)data;
        size_t pos = 0;
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
        else    printf("FAIL: corrupt or truncated tar\n");
        break;
    }
    case FMT_RAW_LZ77: {
        void *decomp = NULL; size_t decomp_len = 0;
        rc = mz_decompress(data, len, &decomp, &decomp_len, MZ_CODEC_LZ77);
        if (rc > 0) { printf("PASS: LZ77 roundtrip ok (%zu bytes)\n", decomp_len); free(decomp); }
        else        { printf("FAIL: %s\n", mz_strerror(rc)); }
        break;
    }
    default:
        printf("FAIL: unknown format\n");
        rc = MZ_ERR_DATA;
    }

    free(data);
    return (rc > 0 || rc == MZ_OK) ? 0 : 1;
}

/* -------------------------------------------------------------------
 * 命令: mxz ls (list raw) — not a subcommand of root
 * ------------------------------------------------------------------- */

static void
print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <command> [options]\n"
        "\n"
        "Commands:\n"
        "  c                       Compress files into archive\n"
        "    --format=FMT          Output format: mz (default), gz, mxa, tar\n"
        "    -o FILE               Output file (required)\n"
        "    <files...>            Input files\n"
        "\n"
        "  d <archive>             Decompress (auto-detect format)\n"
        "\n"
        "  l <archive>             List archive contents (auto-detect)\n"
        "\n"
        "  t <archive>             Test archive integrity (auto-detect)\n"
        "\n"
        "  --help, -h              Show this help\n"
        "\n"
        "Supported formats (auto-detected): MZv2, MxA, Gzip, Zip, Tar\n",
        prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { print_usage(argv[0]); return 1; }

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

    fprintf(stderr, "mxz: unknown command '%s'\n", cmd);
    print_usage(argv[0]);
    return 1;
}