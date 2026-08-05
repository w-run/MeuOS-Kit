#!/bin/sh
# as_arm_neg.sh - mt/as arm negative-immediate normalization gate
#
# Locks ARM data-processing negative-immediate handling:
#   mov rN,#-1     -> mvn rN,#0
#   add rN,rM,#-K  -> sub rN,rM,#K   (and sub #-K -> add #K)
#   encodable signed-rotation negatives (-128) accepted,
#   non-encodable immediates (e.g. 0x10203) rejected, never silently wrong.
#
# Usage: as_arm_neg.sh <as> [readelf]
set -eu

AS="${1:?usage: as_arm_neg.sh <as> [readelf]}"
READELF="${2:-readelf}"
# Resolve to absolute so cd $work doesn't break them.
AS=$(CDPATH= cd -- "$(dirname -- "$AS")" && pwd)/$(basename -- "$AS")
case "$READELF" in
	/*) ;; *) READELF=$(CDPATH= cd -- "$(dirname -- "$READELF")" && pwd)/$(basename -- "$READELF");;
esac

work=$(mktemp -d /tmp/mt-as-armneg.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$work"

# Assemble one instruction + bx lr, dump the first .text instruction word.
# Returns the little-endian word as uppercase hex via stdout.
inst_word() {
	ins=$1
	printf '.text\n.globl f\nf:\n\t%s\n\tbx lr\n' "$ins" > t.s
	"$AS" --target=arm -o t.o t.s 2>/dev/null || return 1
	off=$("$READELF" -S t.o 2>/dev/null | awk '/\.text/ {print $5}')
	[ -n "$off" ] || return 1
	bytes=$(od -An -tx1 -j $((0x$off)) -N 4 t.o | tr -d ' \n')
	# bytes are little-endian on disk; emit as a big-endian word
	echo "0x$(echo "$bytes" | sed 's/\(..\)\(..\)\(..\)\(..\)/\4\3\2\1/')"
}

fail=0
expect() {
	ins=$1; want=$2
	got=$(inst_word "$ins" 2>/dev/null)
	if [ "$got" = "$want" ]; then
		echo "  OK: $ins  -> $got"
	else
		echo "FAIL: $ins  expected $want got ${got:-<rejected>}"; fail=1
	fi
}

expect 'mov r0, #-1'     '0xE3E00000'   # mvn r0,#0
expect 'add r0, r0, #-5' '0xE2400005'   # sub r0,r0,#5
expect 'sub r0, r0, #-5' '0xE2800005'   # add r0,r0,#5
expect 'mov r0, #-128'   '0xE3E0007F'   # mvn r0,#0x7f

# Non-encodable 32-bit immediate must be rejected (no silent mis-encode).
if printf '.text\n.globl f\nf:\n\tmov r1, #0x10203\n\tbx lr\n' > bad.s \
   && "$AS" --target=arm -o bad.o bad.s >/dev/null 2>&1; then
	echo "FAIL: non-encodable imm 0x10203 should be rejected"; fail=1
else
	echo "  OK: non-encodable imm 0x10203 rejected"
fi

if [ "$fail" -ne 0 ]; then
	echo "mt/as arm negative-immediate: FAIL"
	exit 1
fi
echo "mt/as arm negative-immediate: all checks PASS"
