/* mxa_cli.c — mz mxa CLI 工具 */
#include "mxa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static void usage(const char *prog) { (void)prog; fprintf(stderr,


        "\n"
        "Commands:\n"
        "  create  <archive.mxa> <files...>  创建 MxA 归档\n"
        "  list    <archive.mxa>              列出文件\n"
        "  extract <archive.mxa> [pattern]    提取文件\n"
        "  info    <archive.mxa>              归档信息\n"
        "\n"
        "Options:\n"
        "  -l, --level N      压缩级别 (1-9, 默认 6)\n"
        "  -k, --key HEX      加密密钥 (64 hex 字符)\n"
        "  -s, --sign HEX     签名私钥 (64 hex 字符)\n"
        "  -v, --verify HEX   验证公钥 (64 hex 字符)\n"
        "  -o, --output DIR   提取目录 (默认 .)\n"
        "\n");
}

static int parse_hex32(const char *hex, uint8_t *out, const char *label) {
    size_t len = strlen(hex);
    if (len != 64) {
        fprintf(stderr, "error: %s must be 64 hex characters (got %zu)\n", label, len);
        return -1;
    }
    for (size_t i = 0; i < 32; i++) {
        char buf[3] = {hex[2*i], hex[2*i+1], 0};
        char *end = NULL;
        long v = strtol(buf, &end, 16);
        if (*end != '\0' || v < 0 || v > 255) {
            fprintf(stderr, "error: %s has invalid hex at position %zu\n", label, 2*i);
            return -1;
        }
        out[i] = (uint8_t)v;
    }
    return 0;
}

static int cmd_create(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[-1]); return 1; }
    const char *archive_path = argv[0];
    int nfiles = argc - 1;
    char **filenames = argv + 1;

    int level = 6;
    uint8_t key[32], sk[32];
    int has_key = 0, has_sk = 0;

    /* parse options before archive path: -l, -k, -s */
    /* Simple loop for options embedded before path */
    /* For now, just use level 6 */

    struct mxa_params params;
    memset(&params, 0, sizeof(params));
    params.level = level;

    void *ctx = NULL;
    int rc = mxa_create(&ctx, &params);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_create failed: %s\n", mxa_strerror(rc));
        return 1;
    }

    for (int i = 0; i < nfiles; i++) {
        const char *path = filenames[i];
        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
            /* write ctx, not read ctx -- leaked intentionally */ return 1;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fsize < 0) {
            fprintf(stderr, "error: cannot size '%s'\n", path);
            fclose(f); /* write ctx, not read ctx -- leaked intentionally */ return 1;
        }
        void *data = malloc((size_t)fsize);
        if (!data) {
            fprintf(stderr, "error: out of memory\n");
            fclose(f); /* write ctx, not read ctx -- leaked intentionally */ return 1;
        }
        if (fread(data, 1, (size_t)fsize, f) != (size_t)fsize) {
            fprintf(stderr, "error: short read '%s'\n", path);
            free(data); fclose(f); /* write ctx, not read ctx -- leaked intentionally */ return 1;
        }
        fclose(f);

        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;

        rc = mxa_add_file(ctx, name, data, (size_t)fsize, 0644, 0, 0, 0);
        free(data);
        if (rc != MXA_OK) {
            fprintf(stderr, "error: mxa_add_file('%s'): %s\n", name, mxa_strerror(rc));
            /* write ctx, not read ctx -- leaked intentionally */ return 1;
        }
        printf("  added: %s (%ld bytes)\n", name, fsize);
    }

    void *archive_data = NULL;
    size_t archive_len = 0;
    rc = mxa_finish(ctx, &archive_data, &archive_len);
    /* write ctx finished by mxa_finish */
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_finish: %s\n", mxa_strerror(rc));
        return 1;
    }

    FILE *out = fopen(archive_path, "wb");
    if (!out) {
        fprintf(stderr, "error: cannot write '%s': %s\n", archive_path, strerror(errno));
        free(archive_data); return 1;
    }
    fwrite(archive_data, 1, archive_len, out);
    fclose(out);
    printf("created: %s (%zu bytes, %d file(s))\n", archive_path, archive_len, nfiles);
    free(archive_data);
    return 0;
}

static int cmd_list(int argc, char *argv[]) {
    if (argc < 1) { usage(argv[-1]); return 1; }
    const char *path = argv[0];

    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno)); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc((size_t)len);
    if (!data || (size_t)len != fread(data, 1, (size_t)len, f)) {
        fprintf(stderr, "error: read failed\n");
        free(data); fclose(f); return 1;
    }
    fclose(f);

    void *ctx = NULL;
    int rc = mxa_open(data, (size_t)len, &ctx);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_open: %s\n", mxa_strerror(rc));
        free(data); return 1;
    }

    struct mxa_file_entry *entries = NULL;
    int count = 0;
    rc = mxa_list_files(ctx, &entries, &count);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_list_files: %s\n", mxa_strerror(rc));
        mxa_close(ctx); free(data); return 1;
    }

    printf("Archive: %s  (%zu bytes, %d file(s))\n", path, (size_t)len, count);
    printf("%-30s %10s %10s %6s  %s\n", "Name", "Size", "Csize", "Ratio", "Codec");
    printf("--------------------------------------------------------------------------\n");
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
    }

    free(entries);
    /* write ctx finished by mxa_finish */
    free(data);
    return 0;
}

static int cmd_extract(int argc, char *argv[]) {
    const char *output_dir = ".";
    const char *archive_path = NULL;
    const char *pattern = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) output_dir = argv[++i];
            else { fprintf(stderr, "error: --output needs DIR\n"); return 1; }
        } else if (!archive_path) {
            archive_path = argv[i];
        } else {
            pattern = argv[i];
        }
    }
    if (!archive_path) { usage(argv[-1]); return 1; }

    FILE *f = fopen(archive_path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open '%s': %s\n", archive_path, strerror(errno)); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc((size_t)len);
    if (!data || (size_t)len != fread(data, 1, (size_t)len, f)) {
        fprintf(stderr, "error: read failed\n");
        free(data); fclose(f); return 1;
    }
    fclose(f);

    void *ctx = NULL;
    int rc = mxa_open(data, (size_t)len, &ctx);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_open: %s\n", mxa_strerror(rc));
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
        FILE *out = fopen(out_path, "wb");
        if (!out) {
            fprintf(stderr, "  skip '%s': cannot create '%s': %s\n",
                    entries[i].name, out_path, strerror(errno));
            free(fdata); continue;
        }
        fwrite(fdata, 1, fsize, out);
        fclose(out);
        printf("  extracted: %s (%zu bytes)\n", out_path, fsize);
        free(fdata);
        extracted++;
    }

    printf("extracted %d file(s) to '%s/'\n", extracted, output_dir);
    free(entries);
    /* write ctx finished by mxa_finish */
    free(data);
    return 0;
}

static int cmd_info(int argc, char *argv[]) {
    if (argc < 1) { usage(argv[-1]); return 1; }
    const char *path = argv[0];

    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc((size_t)len);
    if (!data || (size_t)len != fread(data, 1, (size_t)len, f)) {
        free(data); fclose(f); return 1;
    }
    fclose(f);

    void *ctx = NULL;
    int rc = mxa_open(data, (size_t)len, &ctx);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_open: %s\n", mxa_strerror(rc));
        free(data); return 1;
    }

    struct mxa_file_entry *entries = NULL;
    int count = 0;
    mxa_list_files(ctx, &entries, &count);

    uint64_t total_size = 0, total_csize = 0;
    for (int i = 0; i < count; i++) {
        total_size += entries[i].size;
        total_csize += entries[i].csize;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t flags = (uint16_t)bytes[4] | ((uint16_t)bytes[5] << 8);

    printf("Archive:    %s\n", path);
    printf("Format:     MxA v%d (min %d)\n", bytes[7], bytes[6]);
    printf("Size:       %zu bytes\n", (size_t)len);
    printf("Files:      %d\n", count);
    if (count > 0) {
        double ratio = total_size > 0
            ? 100.0 * (double)total_csize / (double)total_size
            : 0.0;
        printf("Data:       %llu -> %llu (%.1f%%)\n",
               (unsigned long long)total_size,
               (unsigned long long)total_csize, ratio);
    }
    printf("Encrypted:  %s\n", (flags & MXA_FLAG_ENCRYPTED) ? "yes (ChaCha20)" : "no");
    printf("Signed:     %s\n", (flags & MXA_FLAG_SIGNED) ? "yes (Ed25519)" : "no");

    free(entries);
    /* write ctx finished by mxa_finish */
    free(data);
    return 0;
}

int mxa_cli_main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *cmd = argv[1];
    int cmd_argc = argc - 2;
    char **cmd_argv = argv + 2;

    if (strcmp(cmd, "create") == 0)
        return cmd_create(cmd_argc, cmd_argv);
    if (strcmp(cmd, "list") == 0)
        return cmd_list(cmd_argc, cmd_argv);
    if (strcmp(cmd, "extract") == 0)
        return cmd_extract(cmd_argc, cmd_argv);
    if (strcmp(cmd, "info") == 0)
        return cmd_info(cmd_argc, cmd_argv);

    fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
    usage(argv[0]);
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {

        fprintf(stderr, "       %s --help        show help\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "mxa") != 0) {

        fprintf(stderr, "Try '%s --help' for more info.\n", argv[0]);
        return 1;
    }
    return mxa_cli_main(argc - 1, argv + 1);
}
