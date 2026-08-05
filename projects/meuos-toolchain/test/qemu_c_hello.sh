#!/bin/sh
# test/qemu_c_hello.sh - QEMU C hello world integration test
#
# Tests the full mcc→as→ld→qemu pipeline for a cross-compiled C program.
#
# Usage: qemu_c_hello.sh <mcc> <as> <ld> <qemu> <sysroot_arch> <arch_name>
#
# Reports per-stage results and returns 0 on success (all stages),
# 1 if any stage fails.
set -eu

mcc=${1:?mcc path required}
as=${2:?as path required}
ld=${3:?ld path required}
qemu=${4:?qemu path required}
sysroot_arch=${5:?sysroot_arch path required}
arch=${6:?arch name required}

work=$(mktemp -d /tmp/meuos-qemu-c.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail=0

echo "=== $arch: mcc -S (cross-compile C → .s) ==="
$mcc -target "$arch" --specs=meuos --sysroot="$sysroot_arch" -S -o "$work/hello.s" "$(dirname "$0")/hello.c" 2>&1 || {
	echo "  FAIL: mcc cross-compile"
	fail=1
	exit 1
}
echo "  PASS"

echo "=== $arch: mt/as (assemble .s → .o) ==="
$as --target="$arch" -o "$work/hello.o" "$work/hello.s" 2>&1 || {
	echo "  FAIL: mt/as assembly"
	fail=1
	exit 1
}
echo "  PASS"

echo "=== $arch: mt/ld (link .o → ELF executable) ==="
# Try linking with crt1.o, libc-meuos, libatomic-meuos
if $ld --target="$arch" -o "$work/hello.elf" \
	"$work/hello.o" \
	"$sysroot_arch/usr/lib/crt1.o" \
	"$sysroot_arch/usr/lib/libc-meuos.a" \
	"$sysroot_arch/usr/lib/libatomic-meuos.a" 2>&1; then
	echo "  PASS"
else
	# ld may fail for cross-arch due to unsupported relocations or missing symbols
	echo "  SKIP (ld linking failed — check sysroot completeness for $arch)"
	fail=1
	exit 1
fi

# Verify the ELF is for the correct architecture.
# Normalize `file` output to lowercase and match a per-arch pattern so it
# covers the real `file` naming (e.g. "x86-64", "Intel 80386", "ARM EABI").
arch_lower=$(file "$work/hello.elf" 2>/dev/null | tr '[:upper:]' '[:lower:]')
case "$arch" in
	x86_64)      re='x86-64|amd64';;
	i386)        re='intel 80386|i386';;
	arm)         re='arm, *eabi';;
	riscv64)     re='risc-v';;
	loongarch64) re='loongarch';;
	aarch64)     re='aarch64';;
	*)           re="$arch";;
esac
printf '%s' "$arch_lower" | grep -qE "$re" || {
	echo "  FAIL: unexpected ELF architecture ($arch)"
	file "$work/hello.elf"
	fail=1
	exit 1
}

echo "=== $arch: QEMU runtime ==="
if [ ! -x "$qemu" ]; then
	echo "  SKIP (QEMU binary not found: $qemu)"
	exit "$fail"
fi

# The test program is designed to exit 42 (nonzero). Under `set -e` a bare
# invocation would terminate the shell before we can capture/compare rc, so
# capture it via `|| rc=$?` (the right-hand side is exempt from set -e).
rc=0
"$qemu" "$work/hello.elf" || rc=$?
if [ "$rc" = "42" ]; then
	echo "  PASS (exit=$rc)"
else
	echo "  FAIL (exit=$rc, expected 42)"
	fail=1
	exit 1
fi

echo "$arch C hello: PASS"
exit 0
