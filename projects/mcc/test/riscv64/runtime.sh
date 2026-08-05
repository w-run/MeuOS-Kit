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
# Prefer the Kit's bundled static user-mode qemu (env/qemu beside the
# top-level sysroot), else a PATH qemu-riscv64; falls through to SKIP when
# neither is available.
kit_sysroot="${MEUOS_SYSROOT:-$root/../sysroot}"
if [ -x "$kit_sysroot/../env/qemu/qemu-riscv64-static" ]; then
	qemu="$kit_sysroot/../env/qemu/qemu-riscv64-static"
else
	qemu=${QEMU_RISCV64:-qemu-riscv64}
fi
sysroot="$kit_sysroot/riscv64"

if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
	printf '%s\n' "riscv64 runtime: sysroot not found at $sysroot, skipping"
	exit 0
fi
if ! command -v "$qemu" >/dev/null 2>&1 && [ ! -x "$qemu" ]; then
	printf '%s\n' "riscv64 runtime: user-mode '$qemu' not found, skipping"
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
