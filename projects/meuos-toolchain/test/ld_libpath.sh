#!/bin/sh
# test/ld_libpath.sh - ld -L/-l/--sysroot and -l: syntax test.
set -eu
as=${1:?as path required}
ld=${2:?ld path required}
mcc=${3:?mcc path required}
sysroot=${4:?sysroot path required}
work=$(mktemp -d /tmp/mt-libpath-test.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/counter.c" << 'CFILE'
#include <stdatomic.h>
#include <threads.h>
#include <stdio.h>
_Atomic int counter = 0;
int thread_func(void *arg) { for(int i=0;i<1000;i++) counter++; return 0; }
int main() {
    thrd_t t1,t2;
    thrd_create(&t1, thread_func, NULL);
    thrd_create(&t2, thread_func, NULL);
    thrd_join(t1, NULL); thrd_join(t2, NULL);
    printf("counter = %d\n", counter);
    return counter == 2000 ? 0 : 1;
}
CFILE

"$mcc" --specs=meuos --sysroot="$sysroot" -S -o "$work/counter.s" "$work/counter.c"
"$as" -o "$work/counter.o" "$work/counter.s"

# Test 1: -L + -l
"$ld" -o "$work/app1" "$sysroot/usr/lib/crt1.o" "$work/counter.o" \
    -L"$sysroot/usr/lib" -lc-meuos -latomic-meuos
timeout 5 "$work/app1" | grep -q "counter = 2000" || {
    echo "mt ld libpath (-L/-l): FAIL"; exit 1; }

# Test 2: --sysroot auto-search (no -L)
"$ld" -o "$work/app2" --sysroot="$sysroot" \
    "$sysroot/usr/lib/crt1.o" "$work/counter.o" -lc-meuos -latomic-meuos
timeout 5 "$work/app2" | grep -q "counter = 2000" || {
    echo "mt ld libpath (--sysroot): FAIL"; exit 1; }

# Test 3: -l: full filename syntax
"$ld" -o "$work/app3" --sysroot="$sysroot" \
    "$sysroot/usr/lib/crt1.o" "$work/counter.o" \
    -l:libc-meuos.a -l:libatomic-meuos.a
timeout 5 "$work/app3" | grep -q "counter = 2000" || {
    echo "mt ld libpath (-l:): FAIL"; exit 1; }

echo "mt ld libpath: PASS"
