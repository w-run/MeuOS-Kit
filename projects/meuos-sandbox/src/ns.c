#include "mbox/ns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

/* ── arch helpers ──────────────────────────────────────────────────────── */

const char *arch_to_name(mbox_arch_t arch) {
    switch (arch) {
    case MBOX_ARCH_X86_64:     return "x86_64";
    case MBOX_ARCH_AARCH64:    return "aarch64";
    case MBOX_ARCH_RISCV64:    return "riscv64";
    case MBOX_ARCH_LOONG64:    return "loongarch64";
    case MBOX_ARCH_I386:       return "i386";
    case MBOX_ARCH_ARM:        return "arm";
    case MBOX_ARCH_NATIVE:
        return arch_to_name(host_arch());
    default:                   return "unknown";
    }
}

mbox_arch_t arch_from_name(const char *name) {
    if (strcmp(name, "x86_64") == 0)     return MBOX_ARCH_X86_64;
    if (strcmp(name, "aarch64") == 0)    return MBOX_ARCH_AARCH64;
    if (strcmp(name, "riscv64") == 0)    return MBOX_ARCH_RISCV64;
    if (strcmp(name, "loongarch64") == 0 ||
        strcmp(name, "loong64") == 0)    return MBOX_ARCH_LOONG64;
    if (strcmp(name, "i386") == 0)       return MBOX_ARCH_I386;
    if (strcmp(name, "arm") == 0)        return MBOX_ARCH_ARM;
    if (strcmp(name, "native") == 0)     return MBOX_ARCH_NATIVE;
    return MBOX_ARCH_UNKNOWN;
}

const char *arch_to_qemu_name(mbox_arch_t arch) {
    switch (arch) {
    case MBOX_ARCH_X86_64:     return "qemu-x86_64-static";
    case MBOX_ARCH_AARCH64:    return "qemu-aarch64-static";
    case MBOX_ARCH_RISCV64:    return "qemu-riscv64-static";
    case MBOX_ARCH_LOONG64:    return "qemu-loongarch64-static";
    case MBOX_ARCH_I386:       return "qemu-i386-static";
    case MBOX_ARCH_ARM:        return "qemu-arm-static";
    default:                   return NULL;
    }
}

mbox_arch_t host_arch(void) {
    static int cached = -1;
    if (cached >= 0) return (mbox_arch_t)cached;

    struct utsname u;
    uname(&u);
    const char *m = u.machine;
    cached = MBOX_ARCH_UNKNOWN;

    if (strcmp(m, "x86_64") == 0)               cached = MBOX_ARCH_X86_64;
    else if (strcmp(m, "aarch64") == 0)          cached = MBOX_ARCH_AARCH64;
    else if (strcmp(m, "riscv64") == 0)          cached = MBOX_ARCH_RISCV64;
    else if (strcmp(m, "loongarch64") == 0 ||
             strcmp(m, "loong64") == 0)          cached = MBOX_ARCH_LOONG64;
    else if (strcmp(m, "i386") == 0 ||
             strcmp(m, "i486") == 0 ||
             strcmp(m, "i586") == 0 ||
             strcmp(m, "i686") == 0)             cached = MBOX_ARCH_I386;
    else if (strcmp(m, "armv7l") == 0 ||
             strcmp(m, "armv8l") == 0)           cached = MBOX_ARCH_ARM;

    return (mbox_arch_t)cached;
}

int arch_is_cross(mbox_arch_t arch) {
    if (arch == MBOX_ARCH_NATIVE || arch == MBOX_ARCH_UNKNOWN)
        return 0;
    return arch != host_arch();
}

/* ── QEMU binary locator ───────────────────────────────────────────────── */

const char *qemu_find(mbox_arch_t arch) {
    const char *name = arch_to_qemu_name(arch);
    if (!name) return NULL;

    static char path[512];
    const char *env_qemu_dir = getenv("MEUOS_ENV");
    if (!env_qemu_dir) env_qemu_dir = "/workspace/MeuOS-Kit/env";

    snprintf(path, sizeof(path), "%s/qemu/%s", env_qemu_dir, name);
    if (access(path, X_OK) == 0) return path;

    const char *syspath = getenv("PATH");
    if (!syspath) syspath = "/usr/local/bin:/usr/bin:/bin";

    char copy[1024];
    strncpy(copy, syspath, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    for (char *tok = strtok(copy, ":"); tok; tok = strtok(NULL, ":")) {
        snprintf(path, sizeof(path), "%s/%s", tok, name);
        if (access(path, X_OK) == 0) return path;
    }

    return NULL;
}

/* ── mount + chroot helper (same-arch namespace isolation) ─────────────── */

static int bind_mount_dir(const char *host_path, const char *target) {
    struct stat st;
    if (stat(target, &st) < 0)
        mkdir(target, 0755);
    return mount(host_path, target, NULL, MS_BIND | MS_REC, NULL);
}

static int setup_mount_and_chroot(const char *rootfs,
                                  const mbox_share_t *shares, int nshares,
                                  const char *cdrom, int usb,
                                  const char *net) {
    struct stat st;
    char buf[512];

    snprintf(buf, sizeof(buf), "%s/proc", rootfs);
    if (stat(buf, &st) < 0) mkdir(buf, 0555);
    snprintf(buf, sizeof(buf), "%s/sys", rootfs);
    if (stat(buf, &st) < 0) mkdir(buf, 0555);
    snprintf(buf, sizeof(buf), "%s/dev", rootfs);
    if (stat(buf, &st) < 0) mkdir(buf, 0755);
    snprintf(buf, sizeof(buf), "%s/tmp", rootfs);
    if (stat(buf, &st) < 0) mkdir(buf, 01777);

    /* Bind-mount rootfs to make it a proper mount point (best-effort) */
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) < 0) {
        fprintf(stderr, "mbox: bind mount rootfs: %s (continuing)\n", strerror(errno));
    }

    /* Mount virtual filesystems (best-effort) */
    snprintf(buf, sizeof(buf), "%s/proc", rootfs);
    if (mount("proc", buf, "proc", 0, NULL) < 0) {
        fprintf(stderr, "mbox: mount proc: %s (continuing)\n", strerror(errno));
    }
    snprintf(buf, sizeof(buf), "%s/sys", rootfs);
    if (mount("sysfs", buf, "sysfs", 0, NULL) < 0) {
        /* non-fatal */
    }
    snprintf(buf, sizeof(buf), "%s/dev", rootfs);
    if (mount("tmpfs", buf, "tmpfs", 0, "mode=0755,size=4M") < 0) {
        /* non-fatal */
    }

    /* ── --share-dir ──────────────────────────────────────────────── */
    for (int i = 0; i < nshares; i++) {
        snprintf(buf, sizeof(buf), "%s%s", rootfs, shares[i].guest);
        if (bind_mount_dir(shares[i].host, buf) < 0) {
            fprintf(stderr, "mbox: share mount %s → %s: %s\n",
                    shares[i].host, shares[i].guest, strerror(errno));
        }
    }

    /* ── --cdrom ──────────────────────────────────────────────────── */
    if (cdrom && cdrom[0]) {
        snprintf(buf, sizeof(buf), "%s/mnt/cdrom", rootfs);
        if (stat(buf, &st) < 0) mkdir(buf, 0555);
        if (mount(cdrom, buf, "iso9660", MS_RDONLY, "") < 0) {
            if (mount(cdrom, buf, "auto", MS_RDONLY, "") < 0) {
                fprintf(stderr, "mbox: cdrom mount %s: %s\n", cdrom, strerror(errno));
            }
        }
    }

    /* ── --usb ────────────────────────────────────────────────────── */
    if (usb) {
        snprintf(buf, sizeof(buf), "%s/dev/bus", rootfs);
        mkdir(buf, 0755);
        snprintf(buf, sizeof(buf), "%s/dev/bus/usb", rootfs);
        mkdir(buf, 0755);
        if (bind_mount_dir("/dev/bus/usb", buf) < 0) {
            fprintf(stderr, "mbox: usb mount: %s\n", strerror(errno));
        }
    }

    /* ── --net=user ───────────────────────────────────────────────── */
    (void)net;  /* network setup happens before chroot; placeholder */

    /* chroot (best-effort; may fail without privileges) */
    if (chroot(rootfs) < 0) {
        fprintf(stderr, "mbox: chroot(%s): %s (falling back to prefix)\n",
                rootfs, strerror(errno));
        return -1;
    }
    chdir("/");
    return 0;
}

/* ── Same-arch: namespace isolation + chroot ──────────────────────────── */

static void exec_native_ns(const char *rootfs,
                           const mbox_share_t *shares, int nshares,
                           const char *cdrom, int usb, const char *net,
                           char *argv[], char *envp[]) {
    if (setup_mount_and_chroot(rootfs, shares, nshares, cdrom, usb, net) < 0)
        _exit(127);
    execve(argv[0], argv, envp);
    fprintf(stderr, "mbox: execve(%s): %s\n", argv[0], strerror(errno));
    _exit(127);
}

static void exec_native_no_ns(const char *rootfs, char *argv[], char *envp[]) {
    char full[1024];
    snprintf(full, sizeof(full), "%s%s", rootfs, argv[0]);
    argv[0] = full;
    execve(argv[0], argv, envp);
    fprintf(stderr, "mbox: execve(%s): %s\n", argv[0], strerror(errno));
    _exit(127);
}

/* ── Cross-arch: QEMU user-mode -L (no namespace needed) ──────────────── */

static void exec_qemu(const char *qemu_bin, const char *rootfs,
                      char *argv[], char *envp[]) {
    int argc = 0;
    while (argv[argc]) argc++;

    char **new_argv = malloc((size_t)(argc + 6) * sizeof(char *));
    if (!new_argv) _exit(127);

    new_argv[0] = (char *)qemu_bin;
    new_argv[1] = "-0";
    char cmd_copy[256];
    strncpy(cmd_copy, argv[0], sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    char *bn = cmd_copy;
    for (char *cp = cmd_copy; *cp; cp++)
        if (*cp == '/') bn = cp + 1;
    new_argv[2] = bn;
    new_argv[3] = "-L";
    new_argv[4] = (char *)rootfs;
    char binpath[1024];
    snprintf(binpath, sizeof(binpath), "%s%s", rootfs, argv[0]);
    new_argv[5] = binpath;
    for (int i = 1; i <= argc; i++)
        new_argv[i + 5] = argv[i];

    execve(qemu_bin, new_argv, envp);
    fprintf(stderr, "mbox: execve(%s): %s\n", qemu_bin, strerror(errno));
    free(new_argv);
    _exit(127);
}

/* ── namespace + exec entry point ──────────────────────────────────────── */

int ns_enter_and_exec(const char *rootfs, mbox_arch_t arch,
                      int use_qemu, const char *qemu_bin,
                      const mbox_share_t *shares, int nshares,
                      const char *cdrom, int usb, const char *net,
                      char *const argv[], char *const envp[],
                      int timeout_secs) {
    (void)arch;
    (void)timeout_secs;

    struct stat st;
    if (stat(rootfs, &st) < 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "mbox: rootfs '%s' not found\n", rootfs);
        return -1;
    }

    int pid = fork();
    if (pid < 0) {
        perror("mbox: fork");
        return -1;
    }

    if (pid == 0) {
        char **m_argv = (char **)argv;
        char **m_envp = (char **)envp;

        if (use_qemu && qemu_bin) {
            exec_qemu(qemu_bin, rootfs, m_argv, m_envp);
        } else {
            int ns_ok = (unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWIPC) == 0);
            if (ns_ok)
                exec_native_ns(rootfs, shares, nshares, cdrom, usb, net,
                               m_argv, m_envp);
            else
                exec_native_no_ns(rootfs, m_argv, m_envp);
        }
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("mbox: waitpid");
        return -1;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "mbox: child killed by signal %d\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return -1;
}