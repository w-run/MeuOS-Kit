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
    "    -l, --level N     压缩级别 (1-9, 默认 6)\n"
    "    -k, --key HEX     加密密钥 (64 hex 字符, 启用加密)\n"
    "    -s, --sign HEX    签名私钥 (64 hex 字符, 启用签名)\n"
    "  list    <archive.mxa>              列出文件\n"
    "    -v, --verify HEX  验证公钥 (64 hex 字符)\n"
    "  extract <archive.mxa> [pattern]    提取文件\n"
    "    -v, --verify HEX  验证公钥\n"
    "    -o, --output DIR  输出目录 (默认 .)\n"
    "  info    <archive.mxa>              归档信息\n"
    "  verify  <archive.mxa>              验证归档完整性\n"
    "    -v, --verify HEX  验证公钥 (必填)\n"
    "\n");
}

static int hex_decode(const char *hex, uint8_t *out, int out_len) {
    int hex_len = (int)strlen(hex);
    if (hex_len != out_len * 2) return -1;
    static const signed char hex_tab[256] = {
        ['0']=0,['1']=1,['2']=2,['3']=3,['4']=4,['5']=5,['6']=6,['7']=7,
        ['8']=8,['9']=9,['a']=10,['b']=11,['c']=12,['d']=13,['e']=14,['f']=15,
        ['A']=10,['B']=11,['C']=12,['D']=13,['E']=14,['F']=15,
    };
    for (int i = 0; i < out_len; i++) {
        int hi = hex_tab[(unsigned char)hex[i*2]];
        int lo = hex_tab[(unsigned char)hex[i*2+1]];
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int cmd_create(int argc, char *argv[]) {
    int level = 6;
    uint8_t key[32], sk[32];
    int has_key = 0, has_sk = 0;
    int flags = 0;

    int opt_idx = 0;
    while (opt_idx < argc && argv[opt_idx][0] == '-') {
        if (strcmp(argv[opt_idx], "-l") == 0 || strcmp(argv[opt_idx], "--level") == 0) {
            if (opt_idx + 1 >= argc) { fprintf(stderr, "error: -l needs argument\n"); return 1; }
            level = atoi(argv[++opt_idx]);
            if (level < 1 || level > 9) { fprintf(stderr, "error: level must be 1-9\n"); return 1; }
        } else if (strcmp(argv[opt_idx], "-k") == 0 || strcmp(argv[opt_idx], "--key") == 0) {
            if (opt_idx + 1 >= argc) { fprintf(stderr, "error: -k needs argument\n"); return 1; }
            if (hex_decode(argv[++opt_idx], key, 32) != 0) { fprintf(stderr, "error: bad encryption key (need 64 hex chars)\n"); return 1; }
            has_key = 1;
            flags |= MXA_FLAG_ENCRYPTED;
        } else if (strcmp(argv[opt_idx], "-s") == 0 || strcmp(argv[opt_idx], "--sign") == 0) {
            if (opt_idx + 1 >= argc) { fprintf(stderr, "error: -s needs argument\n"); return 1; }
            if (hex_decode(argv[++opt_idx], sk, 32) != 0) { fprintf(stderr, "error: bad signing key (need 64 hex chars)\n"); return 1; }
            has_sk = 1;
            flags |= MXA_FLAG_SIGNED;
        } else {
            fprintf(stderr, "error: unknown option '%s'\n\n", argv[opt_idx]);
            usage(NULL);
            return 1;
        }
        opt_idx++;
    }

    int remaining = argc - opt_idx;
    if (remaining < 2) { usage(NULL); return 1; }
    const char *archive_path = argv[opt_idx];
    int nfiles = remaining - 1;
    char **filenames = argv + opt_idx + 1;

    struct mxa_params params;
    memset(&params, 0, sizeof(params));
    params.level = level;
    params.flags = flags;
    if (has_key) params.key = key;
    if (has_sk)  params.sk = sk;

    void *ctx = NULL;
    int rc = mxa_create(&ctx, &params);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_create: %s\n", mxa_strerror(rc));
        return 1;
    }

    for (int i = 0; i < nfiles; i++) {
        const char *path = filenames[i];
        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
            return 1;
        }
        if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 1; }
        long fsize = ftell(f);
        if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 1; }

        void *data = NULL;
        if (fsize > 0) {
            data = malloc((size_t)fsize);
            if (!data) { fprintf(stderr, "error: out of memory\n"); fclose(f); return 1; }
            if (fread(data, 1, (size_t)fsize, f) != (size_t)fsize) {
                fprintf(stderr, "error: short read '%s'\n", path);
                free(data); fclose(f); return 1;
            }
        }
        fclose(f);

        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;

        rc = mxa_add_file(ctx, name, data, (size_t)(fsize > 0 ? fsize : 0), 0644, 0, 0, 0);
        if (data) free(data);
        if (rc != MXA_OK) {
            fprintf(stderr, "error: mxa_add_file('%s'): %s\n", name, mxa_strerror(rc));
            return 1;
        }
        printf("  added: %s (%ld bytes)\n", name, fsize > 0 ? fsize : 0);
    }

    void *archive_data = NULL;
    size_t archive_len = 0;
    rc = mxa_finish(ctx, &archive_data, &archive_len);
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

static void *read_whole_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno)); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *data = malloc((size_t)(len > 0 ? len : 1));
    if (!data) { fprintf(stderr, "error: out of memory\n"); fclose(f); return NULL; }
    if (len > 0 && fread(data, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "error: short read\n");
        free(data); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)(len > 0 ? len : 0);
    return data;
}

static int cmd_list(int argc, char *argv[]) {
    uint8_t pk[32], dk[32];
    int has_pk = 0, has_dk = 0;

    int opt_idx = 0;
    while (opt_idx < argc && argv[opt_idx][0] == '-') {
        if (strcmp(argv[opt_idx], "-v") == 0 || strcmp(argv[opt_idx], "--verify") == 0) {
            if (opt_idx + 1 >= argc) { fprintf(stderr, "error: -v needs argument\n"); return 1; }
            if (hex_decode(argv[++opt_idx], pk, 32) != 0) { fprintf(stderr, "error: bad public key hex\n"); return 1; }
            has_pk = 1;
        } else if (strcmp(argv[opt_idx], "-k") == 0 || strcmp(argv[opt_idx], "--key") == 0) {
            if (opt_idx + 1 >= argc) { fprintf(stderr, "error: -k needs argument\n"); return 1; }
            if (hex_decode(argv[++opt_idx], dk, 32) != 0) { fprintf(stderr, "error: bad key hex\n"); return 1; }
            has_dk = 1;
        } else {
            fprintf(stderr, "error: unknown option '%s'\n", argv[opt_idx]);
            return 1;
        }
        opt_idx++;
    }
    if (opt_idx >= argc) { usage(NULL); return 1; }
    const char *path = argv[opt_idx];

    size_t len = 0;
    void *data = read_whole_file(path, &len);
    if (!data) return 1;

    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_open: %s\n", mxa_strerror(rc));
        free(data); return 1;
    }

    if (has_dk) mxa_set_key(ctx, dk);

    if (has_pk) {
        int vrc = mxa_verify(ctx, pk);
        if (vrc != MXA_OK)
            fprintf(stderr, "WARNING: signature verification FAILED\n");
        else
            printf("Signature: valid\n");
    }

    struct mxa_file_entry *entries = NULL;
    int count = 0;
    rc = mxa_list_files(ctx, &entries, &count);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_list_files: %s\n", mxa_strerror(rc));
        mxa_close(ctx); free(data); return 1;
    }

    printf("Archive: %s  (%zu bytes, %d file(s))\n", path, len, count);
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
    mxa_close(ctx);
    free(data);
    return 0;
}

static int cmd_extract(int argc, char *argv[]) {
    const char *output_dir = ".";
    const char *archive_path = NULL;
    const char *pattern = NULL;
    uint8_t pk[32], dk[32];
    int has_pk = 0, has_dk = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) output_dir = argv[++i];
            else { fprintf(stderr, "error: --output needs DIR\n"); return 1; }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verify") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: -v needs argument\n"); return 1; }
            if (hex_decode(argv[++i], pk, 32) != 0) { fprintf(stderr, "error: bad public key hex\n"); return 1; }
            has_pk = 1;
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--key") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: -k needs argument\n"); return 1; }
            if (hex_decode(argv[++i], dk, 32) != 0) { fprintf(stderr, "error: bad key hex\n"); return 1; }
            has_dk = 1;
        } else if (!archive_path) {
            archive_path = argv[i];
        } else {
            pattern = argv[i];
        }
    }
    if (!archive_path) { usage(NULL); return 1; }

    size_t len = 0;
    void *data = read_whole_file(archive_path, &len);
    if (!data) return 1;

    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_open: %s\n", mxa_strerror(rc));
        free(data); return 1;
    }

    if (has_dk) mxa_set_key(ctx, dk);

    if (has_pk) {
        int vrc = mxa_verify(ctx, pk);
        if (vrc != MXA_OK)
            fprintf(stderr, "WARNING: signature verification FAILED\n");
        else
            printf("Signature: valid\n");
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
        if (fsize > 0) fwrite(fdata, 1, fsize, out);
        fclose(out);
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

static int cmd_info(int argc, char *argv[]) {
    if (argc < 1) { usage(NULL); return 1; }
    const char *path = argv[0];

    size_t len = 0;
    void *data = read_whole_file(path, &len);
    if (!data) return 1;

    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
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
    printf("Size:       %zu bytes\n", len);
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
    mxa_close(ctx);
    free(data);
    return 0;
}

static int cmd_verify(int argc, char *argv[]) {
    uint8_t pk[32];
    int has_pk = 0;

    int opt_idx = 0;
    while (opt_idx < argc && argv[opt_idx][0] == '-') {
        if (strcmp(argv[opt_idx], "-v") == 0 || strcmp(argv[opt_idx], "--verify") == 0) {
            if (opt_idx + 1 >= argc) { fprintf(stderr, "error: -v needs argument\n"); return 1; }
            if (hex_decode(argv[++opt_idx], pk, 32) != 0) { fprintf(stderr, "error: bad public key hex\n"); return 1; }
            has_pk = 1;
        } else {
            fprintf(stderr, "error: unknown option '%s'\n", argv[opt_idx]);
            return 1;
        }
        opt_idx++;
    }
    if (!has_pk) {
        fprintf(stderr, "error: 'verify' requires -v <public_key_hex>\n");
        return 1;
    }
    if (opt_idx >= argc) { usage(NULL); return 1; }
    const char *path = argv[opt_idx];

    size_t len = 0;
    void *data = read_whole_file(path, &len);
    if (!data) return 1;

    void *ctx = NULL;
    int rc = mxa_open(data, len, &ctx);
    if (rc != MXA_OK) {
        fprintf(stderr, "error: mxa_open: %s\n", mxa_strerror(rc));
        free(data); return 1;
    }

    int vrc = mxa_verify(ctx, pk);
    mxa_close(ctx);
    free(data);

    if (vrc != MXA_OK) {
        printf("FAIL: signature invalid or data corrupted\n");
        return 1;
    }
    printf("PASS: signature valid, archive intact\n");
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
    if (strcmp(cmd, "verify") == 0)
        return cmd_verify(cmd_argc, cmd_argv);

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
