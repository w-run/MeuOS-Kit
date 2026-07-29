#!/bin/sh
# aarch64 runtime regression: compile + link + run aarch64 static binaries
# that exercise threads/atomics, floating-point, TLS, time64/stat, and
# cross-function va_list.
#
# Requires the aarch64 MeuOS sysroot AND the env/bin/qvm aarch64 VM
# (bootstrapped with `qvm boot aarch64`).  Skips gracefully if either
# is unavailable.
#
# Usage: runtime.sh [mcc-binary] [sysroot]

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
kitroot=$(CDPATH= cd -- "$(dirname -- "$root")/.." && pwd)
sysroot=${2:-"$kitroot/sysroot/aarch64"}
qvm=${QVM:-"$kitroot/env/bin/qvm"}

if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
	printf '%s\n' "aarch64 runtime: sysroot not found at $sysroot, skipping"
	exit 0
fi

if ! "$qvm" status 2>/dev/null | grep -q '^  aarch64 .* RUNNING'; then
	printf '%s\n' "aarch64 runtime: aarch64 VM not running (start with: $qvm boot aarch64), skipping"
	exit 0
fi

share=$("$qvm" share)
work=${TMPDIR:-/tmp}/mcc-aarch64-runtime.$$
trap 'rm -rf "$work" "$share"/rt-* 2>/dev/null || true' EXIT HUP INT TERM
mkdir -p "$work"

# Unique tag per run so concurrent runs don't collide in /mnt/host.
tag=rt-$$

fail=0
for t in runtime_threads runtime_fp runtime_tls runtime_time64 runtime_va; do
	src="$root/test/aarch64/$t.c"
	out="$work/$t"
	printf '%s\n' "  aarch64 runtime: $t"

	# Compile + link the aarch64 static binary.  libc-meuos.a provides
	# the CRT startup, libatomic-meuos.a provides the C11 atomic helpers
	# (aarch64 ldadd/stlr wrappers) referenced by _Atomic fetch-add.
	"$mcc" --target=aarch64 --specs=meuos --sysroot="$sysroot" \
		--nostdlib --static -o "$out" "$src" \
		-l:libc-meuos.a -l:libatomic-meuos.a

	# Drop the binary in the 9p share so the guest can exec it.
	cp "$out" "$share/$tag-$t"

	# Run inside the guest; redirect output to a file in the share so
	# we can read it back reliably (the qvm run serial path interleaves
	# shell echo with command output).
	"$qvm" run aarch64 \
		"/mnt/host/$tag-$t > /mnt/host/$tag-$t.out 2>&1; echo \$? > /mnt/host/$tag-$t.exit" \
		>/dev/null 2>&1 || true

	# Give the 9p cache a moment to settle.
	sleep 1

	if [ ! -f "$share/$tag-$t.exit" ]; then
		printf '%s\n' "  (no exit marker; timed out?)"
		fail=99
		continue
	fi
	rc=$(cat "$share/$tag-$t.exit")
	if [ "$rc" != "0" ]; then
		printf '%s\n' "  FAILED (exit $rc):"
		sed 's/^/    /' "$share/$tag-$t.out" 2>/dev/null || true
		fail=$rc
	else
		printf '%s\n' "  $(head -1 "$share/$tag-$t.out" 2>/dev/null || echo ok)"
	fi

	rm -f "$share/$tag-$t" "$share/$tag-$t.out" "$share/$tag-$t.exit"
done

if [ "$fail" -ne 0 ]; then
	printf '%s\n' "aarch64 runtime: FAILED (exit $fail)"
	exit "$fail"
fi

printf '%s\n' "aarch64 runtime regression checks passed"
