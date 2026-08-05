#!/bin/sh
# arm runtime regression: compile + link + run arm static binaries
# through qemu-arm (user-mode).  Exercises the AAPCS base-standard
# vararg marshalling — 8-byte stack alignment (M1) and 64-bit Kl
# packing (M2) — see src/target/arm/arm_abi.c.
#
# Requires:
#   - the arm MeuOS sysroot (sysroot/arm at the MeuOS-Kit top level)
#   - mt/as + mt/ld (projects/meuos-toolchain/build/bin) to assemble
#     and link arm code without a host cross-compiler
#   - a qemu-arm user-mode binary (env/qemu/qemu-arm-static)
#
# Usage: regress.sh [mcc-binary] [sysroot] [qemu-arm]
# Skips gracefully when any dependency is missing.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
kitroot=$(CDPATH= cd -- "$(dirname -- "$root")/.." && pwd)
sysroot=${2:-"$kitroot/sysroot/arm"}
qemu=${3:-"$kitroot/env/qemu/qemu-arm-static"}

# Non-negative int constant returns must become immediate moves
# (movw #imm), NOT a load from [fp+slot].  A bare `ret (i64)const` has
# no stack slot (MV_CONST); the pre-fix arm mabi_selret emitted
# `add r12,r12,#-1; ldr r10,[r12]` reading uninitialized frame memory
# (and #-1 that mt/as arm_imm_encode cannot encode).  Compile-level
# gate: runs even when the sysroot/qemu runtime deps are absent.
asmtmp=${TMPDIR:-/tmp}/mcc-arm-retconst.$$.s
trap 'rm -f "$asmtmp"' EXIT HUP INT TERM
"$mcc" --target=arm -S -o "$asmtmp" "$root/test/arm/retconst.c"
grep -q 'movw[[:space:]]\+r10, #0x2a' "$asmtmp"
grep -q 'movw[[:space:]]\+r10, #0x3e8' "$asmtmp"
if grep -Eq 'ldr[[:space:]]+.*\[fp' "$asmtmp"; then
	printf '%s\n' 'arm constant return: FAIL (slot load of immediate constant)' >&2
	exit 1
fi
printf '%s\n' 'arm constant-return compile gate passed'

if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
	printf '%s\n' "arm runtime: sysroot not found at $sysroot, skipping"
	exit 0
fi
if [ ! -x "$qemu" ]; then
	printf '%s\n' "arm runtime: qemu-arm not found at $qemu, skipping"
	exit 0
fi
MT_AS=${MT_AS:-"$kitroot/projects/meuos-toolchain/build/bin/as"}
MT_LD=${MT_LD:-"$kitroot/projects/meuos-toolchain/build/bin/ld"}
if [ ! -x "$MT_AS" ] || [ ! -x "$MT_LD" ]; then
	printf '%s\n' "arm runtime: mt/as or mt/ld not found (MT_AS=$MT_AS MT_LD=$MT_LD), skipping"
	exit 0
fi
export MT_AS MT_LD MEUOS_SYSROOT="$sysroot"

work=${TMPDIR:-/tmp}/mcc-arm-runtime.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

fail=0
for t in varargs; do
	src="$root/test/arm/$t.c"
	out="$work/$t"
	printf '%s\n' "  arm runtime: $t"
	"$mcc" --target=arm --specs=meuos -static -o "$out" "$src"
	"$qemu" "$out" || fail=$?
done

if [ "$fail" -ne 0 ]; then
	printf '%s\n' "arm runtime: FAILED (exit $fail)"
	exit "$fail"
fi
printf '%s\n' 'arm runtime: all tests passed'
