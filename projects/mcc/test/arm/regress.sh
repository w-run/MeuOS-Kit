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

# i64-shift compile gate: a 64-bit shift on arm must move bits across the
# two 32-bit halves.  `1<<40` (s>=32) must emit `sub r12,r12,#32` before
# feeding the high half, and a <32 shift (asr7) must emit `rsb r12,r12,#32`
# for the (32-s) carry contribution.  The pre-fix per-half fallback had
# neither, silently clearing `1<<40` to 0.  Const shift counts (<<40/>>33)
# must also be materialised as immediates (movw #0x28/#0x21), not read from
# an uninitialised slot.
shtmp=${TMPDIR:-/tmp}/mcc-arm-i64shift.$$.s
trap 'rm -f "$asmtmp" "$shtmp"' EXIT HUP INT TERM
"$mcc" --target=arm -S -o "$shtmp" "$root/test/arm/i64shift.c"
if ! grep -Eq 'sub[[:space:]]+r12, r12, #32' "$shtmp" ||
   ! grep -Eq 'rsb[[:space:]]+r12, r12, #32' "$shtmp" ||
   ! grep -Eq 'movw[[:space:]]+r10, #0x28' "$shtmp" ||
   ! grep -Eq 'movw[[:space:]]+r10, #0x21' "$shtmp"; then
	printf '%s\n' 'arm i64-shift: FAIL (missing s>=32 adjustment / <32 carry / const-shift materialization)' >&2
	exit 1
fi
printf '%s\n' 'arm i64-shift compile gate passed'

# const-argument call gate: `add(20,22)` must materialize each literal as
# an immediate (movw #0x14 / #0x16) into r0/r1 before `bl add`.  The pre-fix
# arm MMOP_MOV treated i32 dest + i64-typed const as an i64 slot move, so no
# immediate was loaded and the call read garbage.  RR_call (expect 42) hit it.
cctmp=${TMPDIR:-/tmp}/mcc-arm-constcall.$$.s
trap 'rm -f "$asmtmp" "$shtmp" "$cctmp"' EXIT HUP INT TERM
"$mcc" --target=arm -S -o "$cctmp" "$root/test/arm/constcall.c"
if ! grep -Eq 'movw[[:space:]]+r10, #0x14' "$cctmp" ||
   ! grep -Eq 'movw[[:space:]]+r10, #0x16' "$cctmp" ||
   grep -qE 'ldr[[:space:]]+r10, \[fp, #-3' "$cctmp"; then
	printf '%s\n' 'arm const-arg call: FAIL (constants not materialized as immediates)' >&2
	exit 1
fi
printf '%s\n' 'arm const-arg call compile gate passed'

# i64 sign/zero-extension gate: `long long y = 5;` (i64 sext of a 32-bit
# const) must materialise BOTH 32-bit halves of the result.  The pre-fix
# emitter only wrote the low half (hi uninitialised), so an i64 constant's
# high word was garbage.  Assert a movw#imm for the value and a separate
# register-zero store for the high half.
extmp=${TMPDIR:-/tmp}/mcc-arm-i64sext.$$.s
trap 'rm -f "$asmtmp" "$shtmp" "$cctmp" "$extmp"' EXIT HUP INT TERM
"$mcc" --target=arm -S -o "$extmp" "$root/test/arm/i64sext.c"
if ! grep -Eq 'movw[[:space:]]+r10, #0x5' "$extmp" ||
   ! grep -Eq '\bmov[[:space:]]+r10, #0' "$extmp"; then
	printf '%s\n' 'arm i64 sext/zext: FAIL (high half of i64 constant not materialised)' >&2
	exit 1
fi
printf '%s\n' 'arm i64 sext/zext compile gate passed'

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
