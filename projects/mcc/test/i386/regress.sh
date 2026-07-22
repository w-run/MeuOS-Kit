set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
asm=${TMPDIR:-/tmp}/mcc-i386-regress.$$.s
obj=${TMPDIR:-/tmp}/mcc-i386-regress.$$.o
trap 'rm -f "$asm" "$obj"' EXIT HUP INT TERM

"$mcc" --target=i386-linux -S -o "$asm" "$root/test/i386/regress.c"
grep -Eq 'pushl %ebp' "$asm"
grep -Eq 'call sum6' "$asm"
grep -Eq 'call wide_add' "$asm"
grep -Eq 'addl .*%eax' "$asm"

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

printf '%s\n' 'i386 integer ABI and ELF32 object regression checks passed'
