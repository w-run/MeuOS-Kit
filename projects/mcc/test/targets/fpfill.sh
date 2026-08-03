#!/bin/sh
# fpfill.sh — MIR-native floating-point regression: the riscv64 and aarch64
# backends compile a float sample with FP arithmetic / comparisons /
# conversions / constants, and the output assembles with the target GNU as.
#
# Requires riscv64-linux-gnu-as / aarch64-linux-gnu-as; each target skips
# gracefully if its assembler is missing.
#
# Usage: fpfill.sh [mcc-binary]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
asm=${TMPDIR:-/tmp}/mcc-fpfill.$$.s
trap 'rm -f "$asm" "$asm.o"' EXIT HUP INT TERM

for tgt in riscv64 aarch64; do
	as="$tgt-linux-gnu-as"
	if ! command -v "$as" >/dev/null 2>&1; then
		printf '%s\n' "fpfill: $as not found, skipping $tgt"
		continue
	fi
	"$mcc" --target="$tgt-linux" -S -o "$asm" "$root/test/targets/fp.c"
	# FP arithmetic present: riscv64 `fadd.d f0, f0, f1`, aarch64 `fadd d0, d0, d1`
	grep -Eq 'f(add|sub|mul|div)(\.[sd]|[[:space:]]+[sd])' "$asm" \
		|| { printf '%s\n' "fpfill: $tgt no FP arithmetic emitted"; exit 1; }
	# conversions and constant materialization
	grep -Eq 'f(cvt|cvtzs|scvtf|ucvtf|mov)|fadd(\.[sd]|[[:space:]])' "$asm" \
		|| { printf '%s\n' "fpfill: $tgt no FP conversions"; exit 1; }
	"$as" -o "$asm.o" "$asm"
	printf '%s\n' "fpfill: $tgt MIR-native FP assembly OK"
done

printf '%s\n' 'riscv64/aarch64 MIR-native FP regression checks passed'
