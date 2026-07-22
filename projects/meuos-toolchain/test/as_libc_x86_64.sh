#!/bin/sh
set -eu

as=${1:?as path required}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
libc="$root/projects/meuos-libc"
work=$(mktemp -d /tmp/meuos-toolchain-libc-as.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

set -- \
  "$libc/crt/x86_64/crt1.S" \
  "$libc/src/arch/x86_64/atomic.S" \
  "$libc/src/arch/x86_64/setjmp.S" \
  "$libc/src/arch/x86_64/sigreturn.S" \
  "$libc/src/arch/x86_64/thread_clone.S" \
  "$libc/src/internal/arch/x86_64/syscall.S"
for source do
  output="$work/$(basename "$source" .S).o"
  "$as" -o "$output" "$source"
  readelf -h "$output" | grep -Eq 'Type:[[:space:]]+REL'
done
printf '%s\n' 'mt as x86_64 MeuOS libc assembly: PASS'
