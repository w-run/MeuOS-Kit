#!/bin/sh
# i386 runtime regression: compile and execute i386 static binaries
# that exercise Kl decomposition, floating-point (x87), time64/stat,
# and cross-function va_list.  Requires the i386 MeuOS sysroot.
#
# Usage: runtime.sh [mcc-binary] [sysroot]
#
# If the i386 sysroot is not found, the script skips with a message.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
# The i386 MeuOS sysroot lives at the MeuOS-Kit top level under the
# multiarch layout sysroot/<arch> (two dirs up from mcc).
kitroot=$(CDPATH= cd -- "$(dirname -- "$root")/.." && pwd)
sysroot=${2:-"$kitroot/sysroot/i386"}

if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
	printf '%s\n' "i386 runtime: sysroot not found at $sysroot, skipping"
	exit 0
fi

work=${TMPDIR:-/tmp}/mcc-i386-runtime.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

fail=0
for t in runtime_kl runtime_fp runtime_time64 runtime_va fp_unsigned fp_arith tls; do
	src="$root/test/i386/$t.c"
	out="$work/$t"
	printf '%s\n' "  i386 runtime: $t"
	"$mcc" --target=i386 --specs=meuos --sysroot="$sysroot" \
		--nostdlib --static -o "$out" "$src" -l:libc-meuos.a
	"$out" || fail=$?
done

if [ "$fail" -ne 0 ]; then
	printf '%s\n' "i386 runtime: FAILED (exit $fail)"
	exit "$fail"
fi

printf '%s\n' "i386 runtime regression checks passed"
