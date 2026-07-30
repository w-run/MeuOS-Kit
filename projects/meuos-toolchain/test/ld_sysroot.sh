#!/bin/sh
set -eu

as=${1:?as path required}
ld=${2:?ld path required}
root=${3:-/workspace/MeuOS-Kit}
sysroot="$root/sysroot/x86_64"
mcc=$root/projects/mcc/mcc
work=$(mktemp -d /tmp/meuos-toolchain-sysroot-link.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat >"$work/hello.c" <<'CFILE'
#include <stdio.h>
int main(void) { printf("toolchain = %d\n", 42); return 0; }
CFILE
"$mcc" --specs=meuos --sysroot="$sysroot" -S -o "$work/hello.s" "$work/hello.c"
"$as" -o "$work/hello.o" "$work/hello.s"
"$ld" -o "$work/hello" \
    "$sysroot/usr/lib/crt1.o" \
    "$work/hello.o" \
    "$sysroot/usr/lib/libc-meuos.a" \
    "$sysroot/usr/lib/libatomic-meuos.a"
file "$work/hello" | grep -Eq 'ELF 64-bit.*executable'
"$work/hello"
printf '%s\n' 'mt ld x86_64 sysroot smoke: PASS'
