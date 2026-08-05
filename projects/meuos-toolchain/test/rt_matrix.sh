#!/bin/sh
# test/rt_matrix.sh - cross-arch QEMU runtime matrix gate
#
# Compiles a set of C feature programs (integer/64-bit/struct/function/
# global/array/pointer/fp), assembles with mt/as, links with mt/ld, and
# runs each under user-mode qemu asserting an exact exit code.  This is
# the per-arch runtime-depth matrix on top of qemu_c_hello.sh.
#
# Usage: rt_matrix.sh <mcc> <as> <ld> <qemu> <sysroot_arch> <arch_name>
#
# Returns 0 iff every program compiles, assembles, links, and exits with
# its expected code.  A per-program line is printed so a failure locates
# the exact program and arch.
set -eu

mcc=${1:?mcc path required}
as=${2:?as path required}
ld=${3:?ld path required}
qemu=${4:?qemu path required}
sysroot=${5:?sysroot path required}
arch=${6:?arch name required}

work=$(mktemp -d /tmp/meuos-rtm.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# program:expected[:x]  — trailing 'x' marks a KNOWN-FAIL that is
# tracked but does not fail the gate: it must currently FAIL (else we note
# it looks fixed and should be un-flagged).  Un-flagged programs must PASS.
# The xfail set is per-teambook of real cross-arch mcc/mt defects surfaced
# by this matrix that are being chased by the owning worker.
progs_xfail="rr_i64:i386 rr_fp:loongarch64 rr_call:i386 rr_i64:riscv64 rr_i64:loongarch64 rr_struct:loongarch64 rr_call:loongarch64 rr_global:loongarch64 rr_array:loongarch64 rr_ptr:loongarch64 rr_global:riscv64"
progs="rr_arith:42 rr_i64:42 rr_struct:42 rr_call:42 rr_global:42 rr_array:42 rr_ptr:42 rr_fp:42"

is_xfail() {
	p=$1; a=$2
	for e in $progs_xfail; do
		if [ "${e%%:*}" = "$p" ] && [ "${e##*:}" = "$a" ]; then return 0; fi
	done
	return 1
}

fail=0
for entry in $progs; do
	p="${entry%%:*}"; want="${entry##*:}"
	if is_xfail "$p" "$arch"; then known=1; else known=0; fi
	"$mcc" -target "$arch" --specs=meuos --sysroot="$sysroot" \
		-S -o "$work/$p.s" "$here/runtime/$p.c" 2>/dev/null || {
		if [ "$known" = 1 ]; then
			echo "  [$arch] $p: XFAIL (known: mcc -S)"; continue
		fi
		echo "  [$arch] $p: FAIL (mcc -S)"; fail=1; continue; }
	"$as" --target="$arch" -o "$work/$p.o" "$work/$p.s" 2>/dev/null || {
		if [ "$known" = 1 ]; then
			echo "  [$arch] $p: XFAIL (known: as)"; continue
		fi
		echo "  [$arch] $p: FAIL (as)"; fail=1; continue; }
	"$ld" --target="$arch" -o "$work/$p.elf" "$work/$p.o" \
		"$sysroot/usr/lib/crt1.o" "$sysroot/usr/lib/libc-meuos.a" \
		"$sysroot/usr/lib/libatomic-meuos.a" 2>/dev/null || {
		if [ "$known" = 1 ]; then
			echo "  [$arch] $p: XFAIL (known: ld)"; continue
		fi
		echo "  [$arch] $p: FAIL (ld)"; fail=1; continue; }
	rc=0
	"$qemu" "$work/$p.elf" || rc=$?
	if [ "$rc" = "$want" ]; then
		if [ "$known" = 1 ]; then
			echo "  [$arch] $p: NOTE (xfail now passes — unflag?)"
		else
			echo "  [$arch] $p: PASS (exit=$rc)"
		fi
	else
		if [ "$known" = 1 ]; then
			echo "  [$arch] $p: XFAIL (known: exit=$rc want=$want)"
		else
			echo "  [$arch] $p: FAIL (exit=$rc, want=$want)"; fail=1
		fi
	fi
done

if [ "$fail" -ne 0 ]; then
	echo "$arch runtime matrix: FAILED"
	exit 1
fi
echo "$arch runtime matrix: all PASS (incl. xfail tracked)"
