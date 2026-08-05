#!/bin/sh
# as_sse_i386.sh - mt/as i386 SSE2 (movsd/mulsd/cvttsd2si) gate
#
# Locks the SSE2 encoding mt/as now emits for double arithmetic that mcc
# targets on 32-bit (the subset used by i386 FP lowering).  i386 has no
# REX, so these are prefix 0xF2 + 0F + opcode2 + modrm.
#
# Usage: as_sse_i386.sh <as> [objdump]
set -eu
AS="${1:?usage: as_sse_i386.sh <as>}"
OBJDUMP="${2:-objdump}"
AS=$(CDPATH= cd -- "$(dirname -- "$AS")" && pwd)/$(basename -- "$AS")

work=$(mktemp -d /tmp/mt-as-ssei386.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$work"

cat > s.s <<'EOF'
.text
.globl f
f:
	pushl %ebp
	subl $8, %esp
	movsd 8(%esp), %xmm0
	mulsd %xmm1, %xmm0
	movsd %xmm0, %xmm1
	cvttsd2si %xmm0, %eax
	addl $8, %esp
	popl %ebp
	ret
EOF

"$AS" --target=i386 -o s.o s.s 2>/dev/null || {
	echo "FAIL: i386 SSE assembly rejected"; exit 1; }

# Verify the SSE byte patterns are present in .text (od emits lowercase).
bytes=$(od -An -tx1 -v s.o | tr -d ' \n' | tr 'a-f' 'A-F')
# movsd load = f2 0f 10, mulsd = f2 0f 59, cvttsd2si = f2 0f 2c
echo "$bytes" | grep -q 'F20F10' || { echo "FAIL: movsd (load) missing"; exit 1; }
echo "$bytes" | grep -q 'F20F59' || { echo "FAIL: mulsd missing"; exit 1; }
echo "$bytes" | grep -q 'F20F2C' || { echo "FAIL: cvttsd2si missing"; exit 1; }

# Register-count shift (mcc emits bare `shl %cl,%eax`): the trailing 'l'
# in `shl`/`sal` is intrinsic, NOT a width suffix — regression guard.
cat > sh.s <<'EOF'
.text
.globl g
g:
	shl %cl, %eax
	shr %cl, %ebx
	sar %cl, %edx
	ret
EOF
"$AS" --target=i386 -o sh.o sh.s 2>/dev/null || {
	echo "FAIL: i386 shl %cl rejected"; exit 1; }
sbytes=$(od -An -tx1 -v sh.o | tr -d ' \n' | tr 'a-f' 'A-F')
echo "$sbytes" | grep -q 'D3E0' || { echo "FAIL: shl %cl,%eax (d3 e0) missing"; exit 1; }
echo "$sbytes" | grep -q 'D3EB' || { echo "FAIL: shr %cl,%ebx (d3 eb) missing"; exit 1; }
echo "mt/as i386 SSE2 + shl/shr/sar %cl: all checks PASS"
