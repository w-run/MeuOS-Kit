#!/bin/sh
# x86_64 runtime regression: compile and execute x86_64 static binaries
# on the host (the build machine is x86_64).  Exercises the static-global
# array address path plus integer arithmetic.  Requires the x86_64 MeuOS
# sysroot.
#
# Usage: runtime.sh [mcc-binary]
#
# If the x86_64 sysroot is not found, the script skips with a message.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
# x86_64 is the default arch: use the MEUOS_SYSROOT (Kit-top sysroot/) or
# fall back to the repo sysroot/ without a per-arch subdir.
sysroot=${2:-"${MEUOS_SYSROOT:-$root/../sysroot}"}

if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
	printf '%s\n' "x86_64 runtime: sysroot not found at $sysroot, skipping"
	exit 0
fi

work=${TMPDIR:-/tmp}/mcc-x86_64-runtime.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

fail=0
for t in runtime_arr; do
	src="$root/test/x86_64/$t.c"
	out="$work/$t"
	printf '%s\n' "  x86_64 runtime: $t"
	"$mcc" --target=x86_64 --specs=meuos --sysroot="$sysroot" \
		--nostdlib --static -o "$out" "$src" -l:libc-meuos.a
	"$out" || fail=$?
done

if [ "$fail" -ne 0 ]; then
	printf '%s\n' "x86_64 runtime: FAILED (exit $fail)"
	exit "$fail"
fi
printf '%s\n' "x86_64 runtime: all pass"
