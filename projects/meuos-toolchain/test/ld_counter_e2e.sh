#!/bin/sh
# test/ld_counter_e2e.sh - End-to-end counter=2000 multi-threaded test.
#
# Uses mcc to compile the AGENTS.md task-1 test program, mt/as to assemble,
# mt/ld to link against libc-meuos, and runs the result to verify
# counter = 2000. Optionally runs in QEMU.
#
# Usage: ld_counter_e2e.sh <as> <ld> <mcc> <sysroot> [qvm] [arch]
set -eu

as=${1:?as path required}
ld=${2:?ld path required}
mcc=${3:?mcc path required}
sysroot=${4:?sysroot path required}
qvm=${5:-}
arch=${6:-x86_64}

work=$(mktemp -d /tmp/mt-counter-e2e.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/counter.c" << 'CFILE'
#include <stdatomic.h>
#include <threads.h>
#include <stdio.h>

_Atomic int counter = 0;

int thread_func(void *arg) {
    for (int i = 0; i < 1000; i++) counter++;
    return 0;
}

int main() {
    thrd_t t1, t2;
    thrd_create(&t1, thread_func, NULL);
    thrd_create(&t2, thread_func, NULL);
    thrd_join(t1, NULL);
    thrd_join(t2, NULL);
    printf("counter = %d\n", counter);
    return counter == 2000 ? 0 : 1;
}
CFILE

# Compile with mcc, assemble with mt/as, link with mt/ld
"$mcc" --specs=meuos --sysroot="$sysroot" -S -o "$work/counter.s" "$work/counter.c"
"$as" -o "$work/counter.o" "$work/counter.s"
"$ld" -o "$work/counter" \
    "$sysroot/usr/lib/crt1.o" \
    "$work/counter.o" \
    "$sysroot/usr/lib/libc-meuos.a" \
    "$sysroot/usr/lib/libatomic-meuos.a"

# Verify ELF type
file "$work/counter" | grep -Eq 'ELF 64-bit.*executable' || {
    echo "mt ld counter e2e: FAIL (not an ELF executable)"
    exit 1
}

# Verify PT_TLS segment exists
readelf -l "$work/counter" | grep -q 'TLS' || {
    echo "mt ld counter e2e: FAIL (no PT_TLS segment)"
    exit 1
}

# Run the binary
if [ -n "$qvm" ]; then
    # Copy to QEMU share directory and run in VM
    share=$("$qvm" share)
    cp "$work/counter" "$share/counter_e2e"
    chmod +x "$share/counter_e2e"
    output=$("$qvm" run "$arch" "/mnt/host/counter_e2e; echo EXIT:\$?" 2>&1)
    echo "$output" | grep -q "counter = 2000" || {
        echo "mt ld counter e2e (QEMU $arch): FAIL"
        echo "$output"
        exit 1
    }
    echo "$output" | grep -q "EXIT:0" || {
        echo "mt ld counter e2e (QEMU $arch): FAIL (exit code)"
        echo "$output"
        exit 1
    }
    echo "mt ld counter e2e (QEMU $arch): PASS"
else
    # Run on host
    timeout 10 "$work/counter" || {
        echo "mt ld counter e2e: FAIL (run failed or timeout)"
        exit 1
    }
    echo "mt ld counter e2e: PASS"
fi
