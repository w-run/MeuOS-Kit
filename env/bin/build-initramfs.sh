#!/bin/bash
# Build a minimal Alpine-based initramfs for each arch.
# Loads 9p modules so a host directory can be shared at /mnt/host.
set -e
ENV=/workspace/MeuOS-Kit/env
KVER=6.6.142-0-virt
# Module load order (netfs -> fscache -> 9pnet -> 9p -> 9pnet_virtio),
# resolved from the kernel modules.dep.
MODULES="
kernel/fs/netfs/netfs.ko.gz
kernel/fs/fscache/fscache.ko.gz
kernel/net/9p/9pnet.ko.gz
kernel/fs/9p/9p.ko.gz
kernel/net/9p/9pnet_virtio.ko.gz
"

for arch in x86_64 aarch64 i386 riscv64; do
  echo "=== building initramfs for $arch ==="
  stage="$ENV/rootfs/$arch-stage"
  rm -rf "$stage"
  mkdir -p "$stage"
  # 1. Alpine minirootfs (busybox + musl + apk base infrastructure only).
  tar xzf "$ENV/rootfs/minirootfs-$arch.tar.gz" -C "$stage"
  # 2. Extract the 5 needed kernel modules, decompress .ko.gz -> .ko.
  moddir="$stage/lib/modules/$KVER"
  mkdir -p "$moddir"
  for m in $MODULES; do
    tar xzf "$ENV/kernels/linux-virt-$arch.apk" -O "lib/modules/$KVER/$m" 2>/dev/null | gzip -d > "$moddir/$(basename ${m%.gz})" 2>/dev/null || true
  done
  # 3. init script: mount pseudo-fs, load 9p modules, mount host share, drop to sh.
  cat > "$stage/init" <<'INIT'
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc none /proc 2>/dev/null
mount -t sysfs none /sys 2>/dev/null
mount -t devtmpfs none /dev 2>/dev/null
# Load 9p stack so the host shared directory is reachable.
for m in netfs fscache 9pnet 9p 9pnet_virtio; do
  insmod /lib/modules/6.6.142-0-virt/$m.ko 2>/dev/null
done
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host 2>/dev/null && echo "[init] host share mounted at /mnt/host" || echo "[init] no host share (use qvm with -virtfs)"
hostname meuos-test 2>/dev/null
echo ""
echo "=== MeuOS Kit test environment (Alpine base, kernel 6.6.142) ==="
echo "=== arch: $(uname -m)  host share: /mnt/host ==="
echo ""
exec /bin/sh
INIT
  chmod +x "$stage/init"
  # 4. Pack as gzip cpio (newc format). DEVTMPFS_MOUNT=y means the kernel
  #    auto-mounts devtmpfs at /dev before /init, so no device nodes needed.
  ( cd "$stage" && find . | cpio -o -H newc 2>/dev/null | gzip -9 ) \
    > "$ENV/rootfs/initramfs-$arch.cpio.gz"
  echo "  -> initramfs-$arch.cpio.gz: $(du -h $ENV/rootfs/initramfs-$arch.cpio.gz | cut -f1)"
done
echo "=== all initramfs images built ==="
