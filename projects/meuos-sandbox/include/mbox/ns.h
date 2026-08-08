#ifndef MBOX_NS_H
#define MBOX_NS_H

/* Architecture identifiers */
typedef enum {
    MBOX_ARCH_X86_64,
    MBOX_ARCH_AARCH64,
    MBOX_ARCH_RISCV64,
    MBOX_ARCH_LOONG64,
    MBOX_ARCH_I386,
    MBOX_ARCH_ARM,
    MBOX_ARCH_NATIVE,  /* auto-detect at runtime */
    MBOX_ARCH_UNKNOWN
} mbox_arch_t;

const char *arch_to_name(mbox_arch_t arch);
const char *arch_to_qemu_name(mbox_arch_t arch);
mbox_arch_t arch_from_name(const char *name);
mbox_arch_t host_arch(void);
int         arch_is_cross(mbox_arch_t arch);

/* Locate qemu-user binary for a given architecture */
const char *qemu_find(mbox_arch_t arch);

/*
 * Create namespace, chroot, and exec command.
 * Returns child exit code, or -1 on setup failure.
 *
 * If namespace creation fails, falls back to QEMU -L mode.
 */
int ns_enter_and_exec(const char *rootfs, mbox_arch_t arch,
                      int use_qemu, const char *qemu_bin,
                      char *const argv[], char *const envp[],
                      int timeout_secs);

#endif /* MBOX_NS_H */