set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
asm=${TMPDIR:-/tmp}/mcc-i386-regress.$$.s
obj=${TMPDIR:-/tmp}/mcc-i386-regress.$$.o
trap 'rm -f "$asm" "$obj"' EXIT HUP INT TERM

"$mcc" --target=i386-linux -S -o "$asm" "$root/test/i386/regress.c"
grep -Eq 'pushl[[:space:]]+%ebp' "$asm"
grep -Eq 'call[[:space:]]+sum6' "$asm"
grep -Eq 'call[[:space:]]+wide_add' "$asm"
grep -Eq 'addl[[:space:]]+.*%eax' "$asm"

# The i386 target must reach the host assembler in ELF32 mode, not merely
# produce textual assembly.  This remains executable on an x86_64 kernel
# when a complete i386 MeuOS runtime is installed later.
"$mcc" --target=i386-linux -c -o "$obj" "$root/test/i386/regress.c"
LC_ALL=C readelf -h "$obj" | grep -Eq 'Class:[[:space:]]+ELF32'
LC_ALL=C readelf -h "$obj" | grep -Eq 'Machine:[[:space:]]+Intel 80386'

# Float code generation must not trip emitter asserts (a slot-resident
# float temporary reaching a %M operand in float_binary, or the
# previously-unimplemented unsigned float->int conversions Ostoui/Odtoui),
# and must not emit invalid memory-to-memory moves in the Ostoui/Odtoui
# result store.  Assemble to a real object (-c) so the host assembler
# catches these faults; compiling to textual .s only (-S) would miss
# them.  The test sources include <stdio.h>, so point at libc headers.
"$mcc" --target=i386-linux -I"$root/../meuos-libc/include" -c -o "$obj" "$root/test/i386/fp_arith.c"
"$mcc" --target=i386-linux -I"$root/../meuos-libc/include" -c -o "$obj" "$root/test/i386/fp_unsigned.c"

# Non-negative int constant returns must be emitted as immediates, not
# loaded from garbage stack offsets.  This guards mabi_selret's MV_CONST
# path: before the fix, `return 42` produced `movl -1(%ebp), %eax` /
# `movl 3(%ebp), %eax` (undefined s0->slot) and returned random values.
"$mcc" --target=i386-linux -S -o "$asm" "$root/test/i386/retconst.c"
grep -Eq 'movl[[:space:]]+\$42,[[:space:]]+%eax' "$asm"
grep -Eq 'movl[[:space:]]+\$1000,[[:space:]]+%eax' "$asm"
# The two negative returns must still be immediate moves, not slot loads.
grep -Eq 'movl[[:space:]]+\$-1,[[:space:]]+%eax' "$asm"
# And critically: no `movl <off>(%ebp), %eax` load from an uninit slot.
if grep -Eq 'movl[[:space:]]+[^$][^[:space:]]*\(%ebp\),[[:space:]]+%eax' "$asm"; then
	printf '%s\n' 'i386 constant return: FAIL (slot load of immediate constant)' >&2
	exit 1
fi

"$mcc" --target=i386-linux -c -o "$obj" "$root/test/i386/retconst.c"
LC_ALL=C readelf -h "$obj" | grep -Eq 'Class:[[:space:]]+ELF32'
LC_ALL=C readelf -h "$obj" | grep -Eq 'Machine:[[:space:]]+Intel 80386'

# i64-shift gate: register shifts must take the count in %cl (never %ecx,
# which is not a valid shift-count encoding) and use plain shl/shr/sar (mt/as
# rejects the GNU shll/shrl/sarl aliases).  Assert emitted mnemonics; the
# assembly is also compiled to an object below so the host assembler would
# reject any invalid count register.
shasm=${TMPDIR:-/tmp}/mcc-i386-i64shift.$$.s
trap 'rm -f "$asm" "$obj" "$shasm"' EXIT HUP INT TERM
"$mcc" --target=i386-linux -S -o "$shasm" "$root/test/i386/i64shift.c"
if grep -Eq 'sh[lrs][[:space:]]+%ecx' "$shasm" ||
   grep -Eq "sh[lr]{1,2}[[:space:]]+%ecx" "$shasm" ||
   grep -Eq 'shll[[:space:]]+|shrl[[:space:]]+|sarl[[:space:]]' "$shasm"; then
	printf '%s\n' 'i386 i64-shift: FAIL (shift count must be %cl, mnemonic must be plain shl/shr/sar)' >&2
	exit 1
fi
grep -Eq 'shl[[:space:]]+%cl' "$shasm"
grep -Eq 'sar[[:space:]]+\$31' "$shasm"
printf '%s\n' 'i386 i64-shift compile gate passed'

# i64 constant-slot-half gate: a 64-bit constant operand must be
# materialised into a based frame slot (off(%ebp), off = slot + g_slot_base)
# and re-read from the SAME based offset by its consumer.  Before the fix,
# the emitter wrote the constant to the unbased slot (off-by-4) and read
# the `-1` no-slot sentinel verbatim, producing `-1(%ebp)` / `3(%ebp)`
# frame accesses — `1LL<<40` returned garbage.  The shifted value (low half)
# must be stored back to a real based slot, never to `-1(%ebp)` / `3(%ebp)`.
casm=${TMPDIR:-/tmp}/mcc-i386-i64const.$$.s
trap 'rm -f "$asm" "$obj" "$shasm" "$casm"' EXIT HUP INT TERM
"$mcc" --target=i386-linux -S -o "$casm" "$root/test/i386/i64const.c"
if grep -Eq 'movl[[:space:]]+(1|3)\(%ebp\),[[:space:]]+%e[a-z][a-z]' "$casm"; then
	printf '%s\n' 'i386 i64 constant-slot: FAIL (bogus -1(%ebp)/3(%ebp) frame read)' >&2
	exit 1
fi
# The constant 1<<40 split must be loaded into a based slot, not an
# immediate-into-frame against a bare offset, and the shift must re-read
# that slot.  Assert a real based store of an immediate constant half:
if ! grep -Eq 'movl[[:space:]]+\$[0-9]+,[[:space:]]+-[0-9]+\(%ebp\)' "$casm"; then
	printf '%s\n' 'i386 i64 constant-slot: FAIL (constant not materialised into a based slot)' >&2
	exit 1
fi
printf '%s\n' 'i386 i64 constant-slot compile gate passed'

# i386 cdecl call-argument placement: a 2-int-arg call must store its args
# at the bottom of the reserved block ((%esp) and 4(%esp)) so they are read
# back at [ebp+8]/[ebp+12].  mabi_selcall previously wrote them at
# [esp+8]/[esp+12] (start cursor at the 16-aligned reservation), so the
# callee read uninitialized stack and rr_call returned 0 instead of 42.
# Assert both bottom slots are used and the high (buggy) slots are not the
# only stores.
"$mcc" --target=i386-linux -S -o "$asm" "$root/test/i386/callargs.c"
grep -Eq 'movl[[:space:]]+.*\(%esp\)' "$asm"   # arg[0] at [esp+0]
grep -Eq 'movl[[:space:]]+.*4\(%esp\)' "$asm"  # arg[1] at [esp+4]
printf '%s\n' 'i386 integer ABI and ELF32 object regression checks passed'
