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

printf '%s\n' 'i386 integer ABI and ELF32 object regression checks passed'
