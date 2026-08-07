#!/bin/sh
# ld_ehframe_dedup.sh - CIE dedup regression gate.
#
# Verifies that ld merges identical CIEs when linking two objects
# whose .eh_frame sections have identical CIE+FDE content.
#
# Two test scenarios:
#   1. Identical CIEs → merged into one output CIE, both FDEs point to it
#   2. Distinct CIEs (different augmentation) → both CIEs preserved
set -eu

as="${1:?mt/as path required}"
ld="${2:?mt/ld path required}"
readelf_tool="${3:-/usr/bin/readelf}"
HOST_READELF=""
for c in "$readelf_tool" /usr/bin/readelf readelf; do
	if command -v "$c" >/dev/null 2>&1 && [ -x "$c" ]; then
		HOST_READELF="$c"
		break
	fi
done

[ -n "$HOST_READELF" ] || { echo "ld_ehframe_dedup: skipped (no host readelf)"; exit 0; }

work=$(mktemp -d /tmp/mt-ld-ehframe-dedup.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
fail=0

# Helper: count CIEs in .eh_frame of a linked binary
count_cies() {
	"$HOST_READELF" --debug-dump=frames-interp "$1" 2>/dev/null \
	| grep -cE '^\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE' || true
}

count_fdes() {
	"$HOST_READELF" --debug-dump=frames-interp "$1" 2>/dev/null \
	| grep -c FDE || true
}

# Common _start for exec testing
cat >"$work/start.s" <<'ASM'
.text
.globl _start
_start:
	xorl %eax, %eax
	movl $60, %eax
	syscall
ASM

# --- Scenario 1: two identical objects ---
echo "=== Scenario 1: identical CIEs ==="

"$as" -o "$work/start1.o" "$work/start.s"

cat >"$work/a1.s" <<'ASM'
.text
.globl a1
a1:
	.cfi_startproc
	pushq %rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq %rsp, %rbp
	.cfi_def_cfa_register 6
	popq %rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
ASM

cat >"$work/a2.s" <<'ASM'
.text
.globl a2
a2:
	.cfi_startproc
	pushq %rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq %rsp, %rbp
	.cfi_def_cfa_register 6
	popq %rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
ASM

"$as" -o "$work/a1.o" "$work/a1.s"
"$as" -o "$work/a2.o" "$work/a2.s"
"$ld" -o "$work/app1" "$work/start1.o" "$work/a1.o" "$work/a2.o"

cie_count_s1=$(count_cies "$work/app1")
fde_count_s1=$(count_fdes "$work/app1")
echo "  Output CIEs: $cie_count_s1  FDEs: $fde_count_s1"

if [ "$cie_count_s1" -ne 1 ]; then
	echo "  FAIL: expected exactly 1 CIE for identical objects (got $cie_count_s1)"
	fail=1
fi
if [ "$fde_count_s1" -lt 3 ]; then  # start.o contributes 0, a1=1 FDE, a2=1 FDE
	echo "  FAIL: expected ≥2 FDEs (got $fde_count_s1)"
	fail=1
fi
# Run it
if "$work/app1" 2>/dev/null; then
	echo "  Execution: PASS"
else
	echo "  Execution: FAIL (exit code $?)"
	fail=1
fi

# --- Scenario 2: distinct CIEs (different augmentation) ---
echo "=== Scenario 2: distinct CIEs ==="

"$as" -o "$work/start2.o" "$work/start.s"

cat >"$work/b1.s" <<'ASM'
.text
.globl b1
b1:
	.cfi_startproc
	pushq %rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq %rsp, %rbp
	.cfi_def_cfa_register 6
	popq %rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
ASM

cat >"$work/b2.s" <<'ASM'
.text
.globl b2
b2:
	.cfi_startproc
	.cfi_personality 0x8b, personality_handler
	.cfi_lsda 0x1b, except_table
	pushq %rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq %rsp, %rbp
	.cfi_def_cfa_register 6
	popq %rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.section .gcc_except_table,"a",@progbits
except_table:
	.byte 0
.section .text
personality_handler:
	ret
ASM

"$as" -o "$work/b1.o" "$work/b1.s"
"$as" -o "$work/b2.o" "$work/b2.s"
"$ld" -o "$work/app2" "$work/start2.o" "$work/b1.o" "$work/b2.o"

cie_count_s2=$(count_cies "$work/app2")
fde_count_s2=$(count_fdes "$work/app2")
echo "  Output CIEs: $cie_count_s2  FDEs: $fde_count_s2"

if [ "$cie_count_s2" -ne 2 ]; then
	echo "  FAIL: expected exactly 2 CIEs for distinct augmentations (got $cie_count_s2)"
	fail=1
fi
if [ "$fde_count_s2" -lt 3 ]; then
	echo "  FAIL: expected ≥2 FDEs (got $fde_count_s2)"
	fail=1
fi
if "$work/app2" 2>/dev/null; then
	echo "  Execution: PASS"
else
	echo "  Execution: FAIL (exit code $?)"
	fail=1
fi

echo "---"
if [ "$fail" -ne 0 ]; then
	echo "ld_ehframe_dedup: FAILED"
	exit 1
fi
echo "ld_ehframe_dedup: all checks PASS"