#!/bin/sh
set -eu

mcc=${1:-./mcc}
work=/tmp/mcc-driver-sysroot

mkdir -p "$work/include" "$work/lib" "$work/usr/include" "$work/usr/lib"
ar rcs "$work/usr/lib/libc-meuos.a"
printf '%s\n' '#define MCC_SYSROOT_VALUE 17' > "$work/include/mcc_sysroot_test.h"
printf '%s\n' '#include <mcc_sysroot_test.h>' \
    'int main(void) { return MCC_SYSROOT_VALUE != 17; }' > "$work/test.c"

# A sysroot on the command line selects MeuOS specs implicitly (see
# 42a53ed), which links -lc-meuos against a real CRT — this test's
# empty libc-meuos.a cannot satisfy that.  --specs=host keeps the host
# runtime while still exercising --sysroot's include lookup.
"$mcc" --specs=host --sysroot="$work" -o "$work/plain" "$work/test.c"
"$work/plain"
"$mcc" --specs=meuos --sysroot="$work" -E "$work/test.c" >/dev/null

if "$mcc" --nostdinc -E "$work/test.c" >/dev/null 2>&1; then
    echo "--nostdinc unexpectedly found sysroot include" >&2
    exit 1
fi
if "$mcc" --specs=meuos -E "$work/test.c" >/dev/null 2>&1; then
    echo "--specs=meuos unexpectedly accepted no sysroot" >&2
    exit 1
fi

echo "Driver sysroot/specs checks passed"
