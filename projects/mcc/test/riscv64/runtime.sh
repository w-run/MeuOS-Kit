#!/bin/sh
# riscv64 runtime regression: compile and run a riscv64 static MeuOS binary
# (global-array address path + basic ABI) under a riscv64 user-mode qemu.
# Requires the riscv64 MeuOS sysroot AND a user-mode qemu-riscv64; skips
# (exit 0) when either is missing.
#
# Usage: runtime.sh [mcc-binary]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
# riscv64 MeuOS sysroot under the Kit top-level sysroot/<arch> layout.
sysroot=${2:-"${MEUOS_SYSROOT:-$root/../sysroot}/riscv64"}
qemu=${QEMU_RISCV64:-qemu-riscv64}

if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
	printf '%s\n' "riscv64 runtime: sysroot not found at $sysroot, skipping"
	exit 0
fi
if ! command -v "$qemu" >/dev/null 2>&1; then
	printf '%s\n' "riscv64 runtime: user-mode 'qemu-riscv64' not found, skipping (cross-toolchain present, not exercised)"
	exit 0
fi

work=${TMPDIR:-/tmp}/mcc-riscv64-runtime.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

fail=0
for t in runtime_arr; do
	src="$root/test/riscv64/$t.c"
	out="$work/$t"
	printf '%s\n' "  riscv64 runtime: $t"
	"$mcc" --target=riscv64-linux --specs=meuos --sysroot="$sysroot" \
		--nostdlib -static -o "$out" "$src" -L"$sysroot/usr/lib" -lc-meuos
	"$qemu" "$out" || fail=$?
done

if [ "$fail" -ne 0 ]; then
	printf '%s\n' "riscv64 runtime: FAILED (exit $fail)"
	exit "$fail"
fi
printf '%s\n' "riscv64 runtime: all pass"
