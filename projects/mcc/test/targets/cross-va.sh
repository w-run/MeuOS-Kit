#!/bin/sh
# cross-arch varargs/va_list matrix runner.
#
# Compiles test/targets/va_test.c with mcc for each target and runs it
# under the corresponding user-mode qemu (env/qemu/qemu-<arch>-static).
# i386 binaries run natively on the host kernel (CONFIG_IA32_EMULATION),
# x86_64 natively.
#
# Usage: cross-va.sh [mcc-binary]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
kitroot=$(CDPATH= cd -- "$(dirname -- "$root")/.." && pwd)
sysroot_root="${MEUOS_SYSROOT:-$kitroot/sysroot}"
qemu_dir="$kitroot/env/qemu"

work=${TMPDIR:-/tmp}/mcc-cross-va.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

src="$root/test/targets/va_test.c"

# target <---> sysroot/qemu mapping
# name:sysroot_dir:qemu(empty=native)
arches="x86_64::
aarch64:aarch64:qemu-aarch64-static
riscv64:riscv64:qemu-riscv64-static
i386:i386:
loongarch64:loongarch64:qemu-loongarch64-static
arm:arm:qemu-arm-static"

overall=0
for row in $arches; do
	arch=$(echo "$row" | cut -d: -f1)
	sysdir=$(echo "$row" | cut -d: -f2)
	qemu=$(echo "$row" | cut -d: -f3)

	sysroot="$sysroot_root/$sysdir"
	if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
		printf '%s\n' "  $arch: sysroot missing, SKIP"
		continue
	fi
	if [ -n "$qemu" ] && [ ! -x "$qemu_dir/$qemu" ]; then
		printf '%s\n' "  $arch: qemu '$qemu' missing, SKIP"
		continue
	fi

	out="$work/va-$arch"
	printf '%s\n' "=== $arch ==="
	"$mcc" --target="$arch" --specs=meuos --sysroot="$sysroot" \
		--nostdlib -static -o "$out" "$src" -L"$sysroot/usr/lib" -lc-meuos
	if [ -n "$qemu" ]; then
		"$qemu_dir/$qemu" "$out"
	else
		"$out"
	fi
	rc=$?
	if [ "$rc" -ne 0 ]; then
		printf '%s\n' "  $arch: FAILED (exit $rc)"
		overall=$rc
	else
		printf '%s\n' "  $arch: PASS"
	fi
done

if [ "$overall" -ne 0 ]; then
	printf '%s\n' "cross-va: FAILED"
	exit "$overall"
fi
printf '%s\n' "cross-va: all targets pass"
