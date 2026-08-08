#ifndef MBOX_NS_H
#define MBOX_NS_H

#include "mbox/conf.h"

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

/* A single share mount: host → guest */
typedef struct {
    char host[MBOX_SHARE_PATH_LEN];
    char guest[MBOX_SHARE_PATH_LEN];
} mbox_share_t;

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
 * When namespace isolation succeeds:
 *   - share dirs are bind-mounted into rootfs before chroot
 *   - cdrom ISO is loop-mounted into rootfs/mnt/cdrom
 *   - USB devices are bind-mounted into rootfs/dev/bus/usb
 *   - veth pair is created for --net=user
 *
 * If namespace creation fails, falls back to QEMU -L mode
 * (share/cdrom/usb/net not available in fallback mode).
 */
int ns_enter_and_exec(const char *rootfs, mbox_arch_t arch,
                      int use_qemu, const char *qemu_bin,
                      const mbox_share_t *shares, int nshares,
                      const char *cdrom, int usb, const char *net,
                      char *const argv[], char *const envp[],
                      int timeout_secs);

#endif /* MBOX_NS_H */