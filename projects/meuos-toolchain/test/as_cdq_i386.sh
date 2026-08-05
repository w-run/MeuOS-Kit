#!/bin/sh
# as_cdq_i386.sh - mt/as i386 cdq/cltd alias gate.
#
# GNU as accepts both `cdq` (Intel name) and `cltd` (AT&T name) for the
# same sign-extend-eax-into-edx:eax instruction (0x99).  mt/as supports the
# AT&T `cltd` via a dedicated branch, and `cdq` is registered as an alias.
# But the trailing 'q' in `cdq` looks like a width suffix (AT&T b/w/l/q),
# so the mnemonic-suffix stripper used to turn it into an unknown "cd" base
# and the branch was unreachable ("unsupported instruction: cdq").  This gate
# locks both spellings down to the common 0x99 encoding.
#
# Usage: as_cdq_i386.sh <as>
set -eu
AS="${1:?usage: as_cdq_i386.sh <as>}"
AS=$(CDPATH= cd -- "$(dirname -- "$AS")" && pwd)/$(basename -- "$AS")

work=$(mktemp -d /tmp/mt-as-cdqi386.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$work"

cat > c.s <<'EOF'
.text
.globl f
f:
	cltd
	cdq
	ret
EOF

"$AS" --target=i386 -o c.o c.s 2>/dev/null || {
	echo "FAIL: i386 cltd/cdq assembly rejected"; exit 1; }

bytes=$(od -An -tx1 -v c.o | tr -d ' \n' | tr 'a-f' 'A-F')
# cltd = 99, cdq = 99, ret = c3
echo "$bytes" | grep -q '99' || { echo "FAIL: cltd/cdq (99) missing"; exit 1; }
# two 99s then c3
count=$(printf '%s' "$bytes" | grep -o '99' | wc -l)
[ "$count" -ge 2 ] || { echo "FAIL: expected 2x 99 (cltd + cdq)"; exit 1; }
echo "$bytes" | grep -q '99C3' || { echo "FAIL: trailing addi ret (c3) adjacency"; exit 1; }

echo "mt/as i386 cdq/cltd alias: all checks PASS"
