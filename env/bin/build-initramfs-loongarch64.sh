#!/bin/bash
# Build loongarch64 initramfs from source.
# Unlike other arches (Alpine-based), LoongArch has no Alpine port,
# so this script builds musl + busybox from source and packs the initramfs.
set -e
BUILD=$(mktemp -d)
MUSL_VER=1.2.5
BUSYBOX_VER=1.36.1
INSTALL=/workspace/MeuOS-Kit/env/rootfs
CROSS=loongarch64-linux-gnu

echo "=== Building loongarch64 initramfs ==="

# Download musl
cd "$BUILD"
if [ ! -f "musl-$MUSL_VER.tar.gz" ]; then
  echo "Downloading musl..."
  curl -sL --retry 5 -o "musl-$MUSL_VER.tar.gz" \
    "https://musl.libc.org/releases/musl-$MUSL_VER.tar.gz"
fi
tar xzf "musl-$MUSL_VER.tar.gz"
cd "musl-$MUSL_VER"
CC="${CROSS}-gcc" AR="${CROSS}-ar" RANLIB="${CROSS}-ranlib" \
  ./configure --target="loongarch64-linux-musl" --prefix="$BUILD/musl-install" --disable-shared
make -j$(nproc)
make install
cd "$BUILD"

# Download busybox
if [ ! -f "busybox-$BUSYBOX_VER.tar.bz2" ]; then
  echo "Downloading busybox..."
  curl -sL --retry 5 -o "busybox-$BUSYBOX_VER.tar.bz2" \
    "https://busybox.net/downloads/busybox-$BUSYBOX_VER.tar.bz2"
fi
tar xjf "busybox-$BUSYBOX_VER.tar.bz2"
cd "busybox-$BUSYBOX_VER"

# Create musl-gcc wrapper
MUSL_DIR="$BUILD/musl-install"
cat > "$BUILD/musl-gcc" <<MUSLGCC
#!/bin/sh
exec loongarch64-linux-gnu-gcc \
  -static \
  -nostdinc -I"$MUSL_DIR/include" \
  -B"$MUSL_DIR/lib" -L"$MUSL_DIR/lib" \
  "\$@"
MUSLGCC
chmod +x "$BUILD/musl-gcc"

# Configure busybox
make defconfig >/dev/null 2>&1
perl -0pi -e 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
# Disable TC (kernel header version mismatch with busybox 1.36)
sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config

# Install Linux kernel headers (arch-independent) for compile
for d in linux asm-generic mtd sound video drm; do
  [ -d "/usr/include/$d" ] && cp -r "/usr/include/$d" "$BUILD/musl-install/include/"
done
# Create loongarch64 asm/byteorder.h (arch-specific)
cat > "$BUILD/musl-install/include/asm/byteorder.h" <<'EOF'
#ifndef _ASM_LOONGARCH_BYTEORDER_H
#define _ASM_LOONGARCH_BYTEORDER_H
#include <linux/byteorder/little_endian.h>
#endif
EOF
# Symlink asm-generic headers to asm/ for generic ones
mkdir -p "$BUILD/musl-install/include/asm"
for h in "$BUILD/musl-install/include/asm-generic/"*.h; do
  base=$(basename "$h")
  [ ! -f "$BUILD/musl-install/include/asm/$base" ] && \
    ln -sf "../asm-generic/$base" "$BUILD/musl-install/include/asm/$base" 2>/dev/null || true
done

# Build busybox
echo "Building busybox..."
make -j$(nproc) CC="$BUILD/musl-gcc" STRIP="${CROSS}-strip" 2>&1 | grep -v 'warning.*_REDIR_TIME64\|warning.*__cplusplus\|warning.*-W' || true

# Verify
"${CROSS}-readelf" -h busybox | grep -q 'Machine:.*LoongArch' || { echo "Build failed: not LoongArch ELF"; exit 1; }
echo "Busybox: $(file busybox | sed 's/.*: //') ($(stat -c%s busybox) bytes)"

# Create initramfs stage
STAGE="$BUILD/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/sbin" "$STAGE/dev" "$STAGE/proc" "$STAGE/sys" "$STAGE/mnt/host" "$STAGE/etc"
cp busybox "$STAGE/bin/busybox"
chmod 755 "$STAGE/bin/busybox"

# Symlinks
for app in sh ls cat echo mount umount uname mkdir ln cp mv rm touch date ps \
            kill vi clear env hostname df du pidof printf sed sleep tail head \
            sort wc cut grep tr test tty pwd yes tee readlink rmdir; do
  ln -sf /bin/busybox "$STAGE/bin/$app"
done
ln -sf /bin/busybox "$STAGE/sbin/mount"
ln -sf /bin/busybox "$STAGE/sbin/init"

# /init script
cat > "$STAGE/init" <<'INIT'
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc none /proc 2>/dev/null
mount -t sysfs none /sys 2>/dev/null
mount -t devtmpfs none /dev 2>/dev/null
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host 2>/dev/null && echo "[init] host share mounted at /mnt/host" || echo "[init] no host share"
hostname meuos-test 2>/dev/null
echo ""
echo "=== MeuOS Kit test environment (busybox based, kernel 6.6.142 loongarch64) ==="
echo "=== arch: \$(uname -m)  host share: /mnt/host ==="
echo ""
exec /bin/sh
INIT
chmod 755 "$STAGE/init"

# Pack initramfs
(cd "$STAGE" && find . | cpio -o -H newc 2>/dev/null | gzip -9) > "$INSTALL/initramfs-loongarch64.cpio.gz"
echo " -> initramfs-loongarch64.cpio.gz: $(du -h "$INSTALL/initramfs-loongarch64.cpio.gz" | cut -f1)"
echo "=== Done ==="
