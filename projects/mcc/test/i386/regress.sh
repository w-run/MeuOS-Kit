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

# i64 stack-param reception gate (x86-i64param): a `long long` parameter
# arrives at [ebp+8]/[ebp+12] (and +16/+20 for a second).  mabi_selpar must
# load these into the parameter's real frame slots.  Before the fix it
# hard-wired the LOAD destinations to dst->slot (== -1 until regalloc), so
# the reception read `movl -1(%ebp)/3(%ebp)/-5(%ebp)` garbage.  Assert the
# incoming halves are read from the real arg offsets and that no bogus
# 1(%ebp)/3(%ebp)/-1(%ebp) frame reads appear.
pasm=${TMPDIR:-/tmp}/mcc-i386-i64param.$$.s
trap 'rm -f "$asm" "$obj" "$shasm" "$casm" "$pasm"' EXIT HUP INT TERM
"$mcc" --target=i386-linux -S -o "$pasm" "$root/test/i386/i64param.c"
# both i64 params: low half at [ebp+8] (first) and [ebp+16] (second)
grep -Eq 'movl[[:space:]]+8\(%ebp\),[[:space:]]+%eax' "$pasm"
grep -Eq 'movl[[:space:]]+16\(%ebp\),[[:space:]]+%eax' "$pasm"
# no bogus unbased/bogus frame reads from the old dst->slot=-1 sentinel
if grep -Eq 'movl[[:space:]]+(1|3|5)\(%ebp\),[[:space:]]+%e[a-z][a-z]' "$pasm"; then
	printf '%s\n' 'i386 i64 stack-param: FAIL (bogus -1/3/5(%ebp) frame read)' >&2
	exit 1
fi
printf '%s\n' 'i386 i64 stack-param compile gate passed'

# i64 compare truth-value gate (defect #16): the EQ branch of emit_setccr
# must zero-extend `sete %al` with `movzbl %al,%eax` so the i64== result is
# a clean 0/1.  Without it the %eax high 24 bits stay stale (from the
# preceding `movl a.lo,%eax`), producing a garbage "truth" value that
# corrupts boolean/phi logic.
eqasm=${TMPDIR:-/tmp}/mcc-i386-i64cmpeq.$$.s
trap 'rm -f "$asm" "$obj" "$shasm" "$casm" "$pasm" "$eqasm"' EXIT HUP INT TERM
"$mcc" --target=i386-linux -S -o "$eqasm" "$root/test/i386/i64cmpeq.c"
grep -Eq 'sete[[:space:]]+%al' "$eqasm"
# movzbl must immediately follow the sete (zero-extend the eq result)
if ! awk '/^[[:space:]]*sete[[:space:]]+%al/{getline; if ($0 ~ /^[[:space:]]*movzbl[[:space:]]+%al, %eax/) found=1}
           END{exit !found}' "$eqasm"; then
	printf '%s\n' 'i386 i64 compare: FAIL (sete %al not zero-extended with movzbl)' >&2
	exit 1
fi
printf '%s\n' 'i386 i64 compare truth-value compile gate passed'

# byte/half-narrowing return gate (defect #22a): an i8/i16 -> i32 source
# in MMOP_MOVZX fell through into the MMOP_LEA case and emitted the
# address-0 `leal 0,%eax`, zeroing eax and clobbering the narrowed return
# value.  Assert the asm contains no `leal 0,` (the buggy placeholder) and
# at least one movzbl/movzwl (the correct narrow + zero-extend).
bnasm=${TMPDIR:-/tmp}/mcc-i386-byteret.$$.s
trap 'rm -f "$asm" "$obj" "$shasm" "$casm" "$pasm" "$eqasm" "$bnasm"' EXIT HUP INT TERM
"$mcc" --target=i386-linux -S -o "$bnasm" "$root/test/i386/byte_return.c"
if grep -Eq 'leal[[:space:]]+0,' "$bnasm"; then
	printf '%s\n' 'i386 byte-narrowing return: FAIL (leal 0,%eax clobbers narrowed value)' >&2
	exit 1
fi
if ! grep -Eq 'movzbl[[:space:]]+%al, %eax' "$bnasm" && \
   ! grep -Eq 'movzwl[[:space:]]+%ax, %eax' "$bnasm"; then
	printf '%s\n' 'i386 byte-narrowing return: FAIL (no movzbl/movzwl for i8/i16 narrow)' >&2
	exit 1
fi
printf '%s\n' 'i386 byte-narrowing return compile gate passed'

# signed-byte constant-fold gate (defect #22b): a local `signed char x = -56;
# return x;` folds to `sext (i32) (i8)200`; the i386 MOVSX emit must
# sign-extend from the operand's source width (movsbl %al,%eax), not use the
# widened destination dtype (which would drop the extension and return the
# unsigned low byte 200 instead of -56).  Assert the asm contains a movsbl
# (the sign extension) and does NOT return the bare $200 from sb_neg56.
sbfasm=${TMPDIR:-/tmp}/mcc-i386-sbfold.$$.s
"$mcc" --target=i386-linux -O2 -S -o "$sbfasm" "$root/test/i386/signed_byte_fold.c"
if ! grep -Eq 'movsbl[[:space:]]+%al, %eax' "$sbfasm"; then
	printf '%s\n' 'i386 signed-byte fold: FAIL (no movsbl for folded sext (i8)200)' >&2
	exit 1
fi
# The folded constant must be sign-extended: sb_neg56 must emit `movl $200`
# followed by a `movsbl` before its `ret` (the unfixed bug emitted $200 and
# returned it directly with no movsbl in between).
if awk '
  /sb_neg56:/{f=1}
  f&&/movl[[:space:]]+\$200,[[:space:]]+%eax/{saw200=1}
  f&&/movsbl/{saw200=0}            # extension present: clears the bare-$200 flag
  f&&/ret/{
    if(saw200) bad=1;             # $200 reached ret with no movsbl in between
    f=0
  }
  END{exit bad?1:0}' "$sbfasm"; then
	:
else
	printf '%s\n' 'i386 signed-byte fold: FAIL (sb_neg56 returns bare $200, sign extension lost)' >&2
	exit 1
fi
printf '%s\n' 'i386 signed-byte constant-fold compile gate passed'

# i64 NEG/NOT half-store register-name gate (emit-layer operand formatting,
# same systematic class as #22a/#22b): i386_memit.c stored the i64 lo/hi
# halves via scratch_to_dst_i64_lo/hi with a BARE register name, which
# i64_store_half printed verbatim -> `movl edx, off(%ebp)` (no `%`).  GNU as
# accepted it as a zero operand and produced garbage.  Assert the asm for an
# i64 NEG/NOT source contains no un-percented GPR as a movl operand.
nnasm=${TMPDIR:-/tmp}/mcc-i386-negnot.$$.s
"$mcc" --target=i386-linux -O2 -S -o "$nnasm" "$root/test/i386/i64_neg_not.c"
if grep -Eq 'movl[[:space:]]+(edx|ecx|ebx|eax|esi|edi)(,|$|[[:space:]])' "$nnasm" \
   || grep -Eq 'movl[[:space:]]+[^,%]*[[:space:]]*,[[:space:]]+(edx|ecx|ebx|eax|esi|edi)(,|$|[[:space:]])' "$nnasm"; then
	printf '%s\n' 'i386 i64 NEG/NOT: FAIL (bare/un-percented GPR in movl)' >&2
	exit 1
fi
printf '%s\n' 'i386 i64 NEG/NOT half-store compile gate passed'

# i64 memory-load high-half address gate (emit_load i64 branch): loading a
# 64-bit value from memory into a slot-resident temp emits two 32-bit loads
# at addr and addr+4.  The old code built the +4 address with
# snprintf("%d+%s", a.off+4, a.base ? "" : "") -> `movl 4+, %eax` (malformed;
# the host assembler rejects it).  The high half must now be a properly
# formed `movl 4(%base), %eax`.  Assert no `movl <digits>+,` malformed addr.
ldasm=${TMPDIR:-/tmp}/mcc-i386-i64load.$$.s
trap 'rm -f "$asm" "$obj" "$shasm" "$casm" "$pasm" "$eqasm" "$bnasm" "$sbfasm" "$nnasm" "$ldasm"' EXIT HUP INT TERM
"$mcc" --target=i386-linux -O2 -S -o "$ldasm" "$root/test/i386/i64_load_mem.c"
if grep -Eq 'movl[[:space:]]+[0-9]+\+,' "$ldasm"; then
	printf '%s\n' 'i386 i64 memory load: FAIL (malformed `movl 4+,` high-half address)' >&2
	exit 1
fi
# The high half must be a real based load at +4 from the base register.
grep -Eq 'movl[[:space:]]+4\(%[a-z]+\),[[:space:]]+%eax' "$ldasm"
printf '%s\n' 'i386 i64 memory-load high-half address compile gate passed'

# i64 memory-store high-half address gate (emit_store i64 branch, same class
# as the emit_load 4+ bug): storing an i64 to a non-register-based address must
# build addr+4 via emit_addr_str.  The old code did snprintf("%lld", a.off+4)
# for a non-REG base -> `movl %eax, 4` (bare offset, no base register) — a
# corrupt high-half store.  Assert no `movl %eax, <digits>` bare-offset store.
stasm=${TMPDIR:-/tmp}/mcc-i386-i64store.$$.s
trap 'rm -f "$asm" "$obj" "$shasm" "$casm" "$pasm" "$eqasm" "$bnasm" "$sbfasm" "$nnasm" "$ldasm" "$stasm"' EXIT HUP INT TERM
"$mcc" --target=i386-linux -O2 -S -o "$stasm" "$root/test/i386/i64_store_mem.c"
if grep -Eq 'movl[[:space:]]+%eax,[[:space:]]+[0-9]+$' "$stasm"; then
	printf '%s\n' 'i386 i64 memory store: FAIL (bare high-half store `movl %eax, <off>` with no base)' >&2
	exit 1
fi
# The high half must be a real based store at +4 from the base register.
grep -Eq 'movl[[:space:]]+%eax,[[:space:]]+4\(%[a-z]+\)' "$stasm"
printf '%s\n' 'i386 i64 memory-store high-half address compile gate passed'

# i64 multiply gate (same systematic class as #22a/#22b: an i64 op previously
# fell through to its 32-bit counterpart).  i386 has no 64-bit multiply, so a
# correct i64 MUL must emit the unsigned 32x32->64 `mull` triple (cross terms),
# not two independent 32-bit `imull`s of the halves.  Assert `mull` appears in
# the i64 multiply and that no `imull %ecx, %eax` (the 32-bit fallback) is used
# for it.
mulasm=${TMPDIR:-/tmp}/mcc-i386-i64mul.$$.s
trap 'rm -f "$asm" "$obj" "$shasm" "$casm" "$pasm" "$eqasm" "$bnasm" "$sbfasm" "$nnasm" "$ldasm" "$stasm" "$mulasm"' EXIT HUP INT TERM
"$mcc" --target=i386-linux -O2 -S -o "$mulasm" "$root/test/i386/i64_mul.c"
if ! grep -Eq 'mull[[:space:]]+%ecx' "$mulasm"; then
	printf '%s\n' 'i386 i64 multiply: FAIL (no 32x32->64 `mull` for i64 MUL)' >&2
	exit 1
fi
if grep -Eq 'imull[[:space:]]+%ecx, %eax' "$mulasm"; then
	printf '%s\n' 'i386 i64 multiply: FAIL (32-bit imull fallback used for i64 MUL)' >&2
	exit 1
fi
printf '%s\n' 'i386 i64 multiply compile gate passed'
