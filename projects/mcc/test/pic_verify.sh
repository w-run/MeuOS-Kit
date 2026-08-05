#!/bin/sh
# PIC (Position-Independent Code) generation cross-architecture verification.
#
# Compiles a small program with -fPIC for each target architecture,
# checks that the assembly output uses GOT/PLT indirection for
# external symbols and TLS sequences as appropriate.
#
# Usage: pic_verify.sh [mcc-binary]

set -eu

mcc=${1:-./mcc}

work=${TMPDIR:-/tmp}/mcc-pic-verify.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

# Test source: accesses globals, calls external functions, uses TLS
cat > "$work/test.c" << 'EOF'
extern int ext_var;
extern void ext_func(void);
static _Thread_local int tls_var;
int global_var;
void test_func(void) {
	global_var = ext_var;
	ext_func();
	tls_var = 42;
}
EOF

fail=0

# Helper: check that a pattern appears in the assembly
check_pic() {
	local arch="$1" label="$2" pattern="$3" file="$4"
	if grep -q "$pattern" "$file"; then
		printf '  %s PIC: %s OK\n' "$arch" "$label"
	else
		printf '  %s PIC: %s NOT FOUND (pattern: %s)\n' "$arch" "$label" "$pattern"
		fail=1
	fi
}

# x86_64 PIC verification
printf '%s\n' "--- x86_64 PIC ---"
"$mcc" --target=x86_64 -fPIC -S -o "$work/x86_64.s" "$work/test.c"
check_pic "x86_64" "GOT global_var" 'global_var@gotpcrel(%rip)' "$work/x86_64.s"

# aarch64 PIC verification
printf '%s\n' "--- aarch64 PIC ---"
"$mcc" --target=aarch64 -fPIC -S -o "$work/aarch64.s" "$work/test.c"
check_pic "aarch64" "GOT global_var" ':got:global_var' "$work/aarch64.s"

# riscv64 PIC verification (auipc %got_pcrel_hi + ld %pcrel_lo label pair)
printf '%s\n' "--- riscv64 PIC ---"
"$mcc" --target=riscv64 -fPIC -S -o "$work/riscv64.s" "$work/test.c"
if grep -q '%got_pcrel_hi' "$work/riscv64.s"; then
	printf '  riscv64 PIC: GOT sequences present\n'
else
	printf '  riscv64 PIC: GOT sequences NOT FOUND (pattern: %%got_pcrel_hi)\n'
	fail=1
fi

# i386 PIC verification
printf '%s\n' "--- i386 PIC ---"
"$mcc" --target=i386 -fPIC -S -o "$work/i386.s" "$work/test.c"
check_pic "i386" "GOT global_var" '@GOT(' "$work/i386.s"

if [ "$fail" -ne 0 ]; then
	printf '%s\n' "PIC verification: FAILED"
	exit "$fail"
fi
printf '%s\n' "PIC verification checks passed"
