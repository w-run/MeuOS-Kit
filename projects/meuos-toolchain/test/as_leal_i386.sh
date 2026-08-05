#!/bin/sh
# as_leal_i386.sh - mt/as i386 `leal` absolute-address gate.
#
# GNU as accepts `leal addr, %reg`, and a bare number (no '$') is an
# absolute-address operand (`leal 0, %eax` loads address 0 → disp32=0).
# mt/as's lea branch only accepted OP_MEM/OP_SYMBOL sources, so mcc's i386
# register-materialisation fallback (`leal 0, %eax`, e.g. the unsigned-char
# narrowing path) was rejected as "unsupported instruction: leal".  This gate
# locks `leal 0` encoding (8D /r with mod=00 rm=101 + disp32) and guards that
# a true immediate `leal $N` stays rejected (lea has no immediate form).
#
# Usage: as_leal_i386.sh <as> [objdump]
set -eu
AS="${1:?usage: as_leal_i386.sh <as>}"
OBJDUMP="${2:-objdump}"
AS=$(CDPATH= cd -- "$(dirname -- "$AS")" && pwd)/$(basename -- "$AS")

work=$(mktemp -d /tmp/mt-as-leali386.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$work"

cat > l.s <<'EOF'
.text
.globl f
f:
	leal 0, %eax
	leal 8(%esp), %ecx
	lea 12(%ebx), %edx
	ret
EOF

"$AS" --target=i386 -o l.o l.s 2>/dev/null || {
	echo "FAIL: i386 leal/lea rejected"; exit 1; }

bytes=$(od -An -tx1 -v l.o | tr -d ' \n' | tr 'a-f' 'A-F')
# leal 0,%eax = 8D 05 00 00 00 00  (mod=00 rm=101 + disp32)
echo "$bytes" | grep -q '8D0500000000' || { echo "FAIL: leal 0 (8d 05 00 00 00 00) missing"; exit 1; }
# leal 8(%esp),%ecx = 8D 4C 24 08
echo "$bytes" | grep -q '8D4C2408' || { echo "FAIL: leal 8(%esp) (8d 4c 24 08) missing"; exit 1; }
# lea 12(%ebx),%edx = 8D 53 0C
echo "$bytes" | grep -q '8D530C' || { echo "FAIL: lea 12(%ebx) (8d 53 0c) missing"; exit 1; }

# True immediate form must stay rejected (lea has no immediate operand).
cat > i.s <<'EOF'
.text
.globl g
g:
	leal $5, %eax
EOF
if "$AS" --target=i386 -o i.o i.s >/dev/null 2>&1; then
	echo "FAIL: leal \$5 (immediate) should be rejected"; exit 1; fi

echo "mt/as i386 leal absolute-address + lea forms: all checks PASS"
