#include "mbox/conf.h"
#include "mbox/ns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] ./rootfs-dir [command...]\n"
        "\n"
        "Architecture options:\n"
        "  --x86_64           Target x86_64 (default if host is x86_64)\n"
        "  --aarch64          Target AArch64 (requires QEMU)\n"
        "  --riscv64          Target RISC-V 64 (requires QEMU)\n"
        "  --loong64          Target LoongArch 64 (requires QEMU)\n"
        "  --i386             Target i386 (requires QEMU)\n"
        "  --arm              Target ARM (requires QEMU)\n"
        "\n"
        "Hardware options:\n"
        "  --share-dir=HOST:GUEST  Bind-mount host dir into sandbox\n"
        "  --net=user         User-mode network (veth pair, requires namespace)\n"
        "  --net=none         No network (default)\n"
        "  --cdrom=/path/to.iso   Mount ISO into sandbox /mnt/cdrom\n"
        "  --usb              Passthrough USB devices into sandbox\n"
        "\n"
        "Interface options:\n"
        "  --mcp              Start MCP server on Unix socket (future)\n"
        "  --webpty=PORT      Start WebPTY on HTTP port (future)\n"
        "\n"
        "Other options:\n"
        "  --help, -h         Show this help\n"
        "\n"
        "If no command given, runs /bin/sh.\n"
        "If rootfs-dir/mbox.conf exists, it is loaded automatically.\n"
        "CLI flags override mbox.conf values.\n"
        , prog);
}

int main(int argc, char *argv[], char *envp[]) {
    /* Early help check — work without rootfs */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    /* ── defaults ─────────────────────────────────────────────────── */
    mbox_config cfg;
    conf_init(&cfg);
    strcpy(cfg.arch, "native");     /* default: match host */
    strcpy(cfg.net, "none");

    char *cdrom = NULL;
    int  usb = 0;

    char *rootfs = NULL;
    int  user_argc = 0;
    char *user_argv[256];

    int flag_help = 0;

    /* ── arg parsing ──────────────────────────────────────────────── */
    /* First pass: find rootfs-dir (positional, does not start with --) */
    int rootfs_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        rootfs_idx = i;
        break;
    }

    if (rootfs_idx < 0) {
        usage(argv[0]);
        return 1;
    }
    rootfs = argv[rootfs_idx];

    /* Parse options before rootfs */
    for (int i = 1; i < rootfs_idx; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--x86_64") == 0)           strcpy(cfg.arch, "x86_64");
        else if (strcmp(a, "--aarch64") == 0)     strcpy(cfg.arch, "aarch64");
        else if (strcmp(a, "--riscv64") == 0)     strcpy(cfg.arch, "riscv64");
        else if (strcmp(a, "--loong64") == 0)     strcpy(cfg.arch, "loongarch64");
        else if (strcmp(a, "--i386") == 0)        strcpy(cfg.arch, "i386");
        else if (strcmp(a, "--arm") == 0)         strcpy(cfg.arch, "arm");

        else if (strncmp(a, "--net=", 6) == 0)    strncpy(cfg.net, a + 6, sizeof(cfg.net) - 1);
        else if (strcmp(a, "--mcp") == 0)          cfg.mcp_port = 1;
        else if (strncmp(a, "--webpty=", 8) == 0)  cfg.webpty_port = atoi(a + 8);

        else if (strncmp(a, "--share-dir=", 11) == 0) {
            const char *val = a + 11;
            const char *colon = strchr(val, ':');
            if (colon && cfg.share_count < MBOX_MAX_SHARE) {
                size_t hlen = (size_t)(colon - val);
                if (hlen < MBOX_SHARE_PATH_LEN) {
                    memcpy(cfg.share_host[cfg.share_count], val, hlen);
                    cfg.share_host[cfg.share_count][hlen] = '\0';
                    strncpy(cfg.share_guest[cfg.share_count], colon + 1,
                            MBOX_SHARE_PATH_LEN - 1);
                    cfg.share_count++;
                }
            }
        }
        else if (strncmp(a, "--cdrom=", 7) == 0) {
            cdrom = (char *)(a + 7);
        }
        else if (strcmp(a, "--usb") == 0) {
            usb = 1;
        }
        else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            flag_help = 1;
        }
        else {
            fprintf(stderr, "mbox: unknown option: %s\n", a);
            return 1;
        }
    }

    if (flag_help) {
        usage(argv[0]);
        return 0;
    }

    /* Collect command args (positional after rootfs) */
    for (int i = rootfs_idx + 1; i < argc && user_argc < 255; i++)
        user_argv[user_argc++] = argv[i];
    user_argv[user_argc] = NULL;

    if (user_argc == 0) {
        user_argv[0] = "/bin/sh";
        user_argv[1] = NULL;
        user_argc = 1;
    }

    /* ── load mbox.conf ───────────────────────────────────────────── */
    char conf_path[1024];
    snprintf(conf_path, sizeof(conf_path), "%s/mbox.conf", rootfs);

    mbox_config file_cfg;
    conf_init(&file_cfg);
    if (conf_load(conf_path, &file_cfg) == 0) {
        if (strcmp(cfg.arch, "native") == 0 && file_cfg.arch[0])
            snprintf(cfg.arch, sizeof(cfg.arch), "%s", file_cfg.arch);
        if (cfg.timeout < 0 && file_cfg.timeout >= 0)
            cfg.timeout = file_cfg.timeout;
        if (cfg.mcp_port == 0 && file_cfg.mcp_port > 0)
            cfg.mcp_port = file_cfg.mcp_port;
        if (cfg.webpty_port == 0 && file_cfg.webpty_port > 0)
            cfg.webpty_port = file_cfg.webpty_port;
        for (int i = 0; i < file_cfg.env_count && cfg.env_count < MBOX_MAX_ENV; i++) {
            strncpy(cfg.env_keys[cfg.env_count], file_cfg.env_keys[i], MBOX_ENV_KEY_LEN - 1);
            strncpy(cfg.env_vals[cfg.env_count], file_cfg.env_vals[i], MBOX_ENV_VAL_LEN - 1);
            cfg.env_count++;
        }
        (void)cfg.webpty_readonly;
    }

    /* ── resolve architecture ─────────────────────────────────────── */
    mbox_arch_t arch;
    if (strcmp(cfg.arch, "native") == 0)
        arch = MBOX_ARCH_NATIVE;
    else
        arch = arch_from_name(cfg.arch);

    if (arch == MBOX_ARCH_UNKNOWN) {
        fprintf(stderr, "mbox: unknown architecture: %s\n", cfg.arch);
        return 1;
    }

    int cross = arch_is_cross(arch);
    const char *qemu_path = cross ? qemu_find(arch) : NULL;

    if (cross && !qemu_path) {
        fprintf(stderr, "mbox: QEMU user-mode binary for %s not found\n",
                arch_to_name(arch));
        return 1;
    }

    /* ── build share array ────────────────────────────────────────── */
    mbox_share_t shares[MBOX_MAX_SHARE];
    int nshares = 0;
    for (int i = 0; i < cfg.share_count && i < MBOX_MAX_SHARE; i++) {
        strncpy(shares[i].host, cfg.share_host[i], MBOX_SHARE_PATH_LEN - 1);
        shares[i].host[MBOX_SHARE_PATH_LEN - 1] = '\0';
        strncpy(shares[i].guest, cfg.share_guest[i], MBOX_SHARE_PATH_LEN - 1);
        shares[i].guest[MBOX_SHARE_PATH_LEN - 1] = '\0';
        nshares++;
    }

    /* ── execute ──────────────────────────────────────────────────── */
    return ns_enter_and_exec(rootfs, arch, cross ? 1 : 0, qemu_path,
                             shares, nshares,
                             cdrom, usb, cfg.net,
                             user_argv, envp, cfg.timeout);
}