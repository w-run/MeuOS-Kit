#!/bin/sh
# Run the C23 negative (must-fail-to-compile) test corpus.
#
# Each test/c23/neg/*.neg.c is expected to be REJECTED by mcc.  A test that
# compiles is reported as a regression (the compiler failed to diagnose the
# invalid construct) and causes a non-zero exit.
#
# NOTE: these live in the test/c23/neg/ subdirectory on purpose.  The
# `check-c23` gate globs test/c23/*.c (non-recursive), so this directory is
# excluded from the positive gate.  A dedicated `check-c23-neg` target
# (mirroring the C++ `check-cpp-neg`) should eventually drive this script.
set -u
MCC="${1:-./mcc}"
NEG_DIR="$(cd "$(dirname "$0")" && pwd)"
: "${MEUOS_SYSROOT:=../sysroot}"
export MEUOS_SYSROOT
fail=0
for t in "$NEG_DIR"/*.neg.c; do
	[ -e "$t" ] || continue
	base="$(basename "$t" .neg.c)"
	if "$MCC" -o "/tmp/mcc-neg-$base" "$t" >"/tmp/mcc-neg-$base.log" 2>&1; then
		echo "REGRESSION: $base compiled (expected rejection)"; fail=1
	else
		echo "OK: $base rejected"
	fi
done
if [ "$fail" -ne 0 ]; then
	echo "neg corpus: FAIL"; exit 1
fi
echo "neg corpus: PASS"
