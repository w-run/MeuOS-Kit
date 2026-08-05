#!/bin/sh
# loongarch64 runtime regression: compile and run a loongarch64 static MeuOS
# binary (global-array address path + basic ABI) under a loongarch64
# user-mode qemu.  Requires the loongarch64 MeuOS sysroot AND a user-mode
# qemu-loongarch64; skips (exit 0) when either is missing.
#
# Usage: runtime.sh [mcc-binary]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
# loongarch64 MeuOS sysroot under the Kit top-level sysroot/<arch> layout.
kit_sysroot="${MEUOS_SYSROOT:-$root/../sysroot}"
sysroot="$kit_sysroot/loongarch64"
# Prefer the Kit's bundled static user-mode qemu (env/qemu beside the
# top-level sysroot), else a PATH qemu-loongarch64; SKIP when unavailable.
if [ -x "$kit_sysroot/../env/qemu/qemu-loongarch64-static" ]; then
	qemu="$kit_sysroot/../env/qemu/qemu-loongarch64-static"
else
	qemu=${QEMU_LOONGARCH64:-qemu-loongarch64}
fi

if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
	printf '%s\n' "loongarch64 runtime: sysroot not found at $sysroot, skipping"
	exit 0
fi
if ! command -v "$qemu" >/dev/null 2>&1 && [ ! -x "$qemu" ]; then
	printf '%s\n' "loongarch64 runtime: user-mode '$qemu' not found, skipping"
	exit 0
fi

work=${TMPDIR:-/tmp}/mcc-loongarch64-runtime.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

fail=0
for t in runtime_arr; do
	src="$root/test/loongarch64/$t.c"
	out="$work/$t"
	printf '%s\n' "  loongarch64 runtime: $t"
	"$mcc" --target=loongarch64-linux --specs=meuos --sysroot="$sysroot" \
		--nostdlib -static -o "$out" "$src" -L"$sysroot/usr/lib" -lc-meuos
	"$qemu" "$out" || fail=$?
done

if [ "$fail" -ne 0 ]; then
	printf '%s\n' "loongarch64 runtime: FAILED (exit $fail)"
	exit "$fail"
fi
printf '%s\n' "loongarch64 runtime: all pass"
