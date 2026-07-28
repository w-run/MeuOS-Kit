#!/bin/sh
# ld_defsym.sh — test mt/ld --defsym absolute symbol definition.
#
# Verifies that --defsym=SYM=VAL defines an absolute symbol VAL that
# satisfies an undefined reference and is addressable at the given address.
set -eu

ld=${1:?ld path required}
work=$(mktemp -d /tmp/meuos-toolchain-ld-defsym.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat >"$work/ref.c" <<'CEOF'
extern int marker;          /* defined via --defsym=marker=VAL */
int main(void) { return (long)(&marker) != 0x1234; }
CEOF

# Compile with host gcc: emits an undefined `marker` and a main that
# returns its address compared to the expected absolute value.
gcc -c -fno-pic -o "$work/ref.o" "$work/ref.c"

# Without --defsym the symbol is undefined and linking must fail.
if "$ld" -e main -o "$work/fail.elf" "$work/ref.o" 2>/dev/null; then
	printf '%s\n' 'FAIL: link succeeded without --defsym (expected undefined symbol error)'
	exit 1
fi
printf '%s\n' 'mt ld --defsym: undefined-without-defsym correctly rejected'

# With --defsym=marker=0x1234 the symbol resolves to absolute address.
if "$ld" -e main --defsym=marker=0x1234 -o "$work/ok.elf" "$work/ref.o" 2>/dev/null; then
	printf '%s\n' 'mt ld --defsym: PASS (link with hex absolute symbol OK)'
else
	printf '%s\n' 'FAIL: --defsym link failed'
	exit 1
fi

# Decimal equivalence check (4660 == 0x1234).
"$ld" -e main --defsym=marker=4660 -o "$work/dec.elf" "$work/ref.o" 2>/dev/null || {
	printf '%s\n' 'FAIL: --defsym decimal link failed'
	exit 1
}
printf '%s\n' 'mt ld --defsym: decimal equivalent PASS'

# Multiple --defsym instances (GNU ld compatible).
cat >"$work/ref2.c" <<'CEOF'
extern int a, b;
int main(void) { return (long)(&a) != 0x10 || (long)(&b) != 0x20; }
CEOF
gcc -c -fno-pic -o "$work/ref2.o" "$work/ref2.c"
"$ld" -e main --defsym=a=0x10 --defsym=b=0x20 -o "$work/multi.elf" "$work/ref2.o" 2>/dev/null || {
	printf '%s\n' 'FAIL: multiple --defsym link failed'
	exit 1
}
printf '%s\n' 'mt ld --defsym: multiple instances PASS'

printf '%s\n' 'mt ld --defsym: all checks PASS'
