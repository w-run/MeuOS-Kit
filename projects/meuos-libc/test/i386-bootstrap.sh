#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mcc=${MCC:-"$root/../mcc/mcc"}
cc=${CC:-cc}
work=${TMPDIR:-/tmp}/meuos-i386-bootstrap.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

cat > "$work/hello.c" <<'EOF'
#include <unistd.h>
int main(void) { if (getpid() <= 0) return 2; return write(1, "i386 MeuOS libc\n", 16) == 16 ? 0 : 1; }
EOF
cat > "$work/atomic.c" <<'EOF'
#include <stdatomic.h>
_Atomic int value = 3;
_Atomic unsigned char byte_value = 1;
_Atomic unsigned short short_value = 2;
int main(void) { int expected = 5; unsigned char b = 1; unsigned short s = 2; if (atomic_fetch_add(&value, 2) != 3) return 1; if (!atomic_compare_exchange_strong(&value, &expected, 9)) return 2; if (!atomic_compare_exchange_strong(&byte_value, &b, 3)) return 3; if (!atomic_compare_exchange_strong(&short_value, &s, 4)) return 4; return atomic_load(&value) == 9 && atomic_load(&byte_value) == 3 && atomic_load(&short_value) == 4 ? 0 : 5; }
EOF

"$mcc" --target=i386 -I"$root/include" -c -o "$work/hello.o" "$work/hello.c"
"$mcc" --target=i386 -I"$root/include" -I"$root/src" -c -o "$work/write.o" "$root/src/syscall/write.c"
"$mcc" --target=i386 -I"$root/include" -I"$root/src" -c -o "$work/getpid.o" "$root/src/syscall/getpid.c"
"$mcc" --target=i386 -I"$root/include" -c -o "$work/errno.o" "$root/src/errno.c"
"$cc" -m32 -c -o "$work/crt1.o" "$root/crt/i386/crt1.S"
"$cc" -m32 -c -o "$work/syscall.o" "$root/src/internal/i386/syscall.S"
"$cc" -m32 -c -o "$work/atomic.o" "$root/src/i386/atomic.S"
"$mcc" --target=i386 -I"$root/include" -I"$root/src" -c -o "$work/tls.o" "$root/src/i386/tls.c"
"$mcc" --target=i386 -I"$root/include" -c -o "$work/memory.o" "$root/src/string/memory.c"
"$cc" -m32 -c -o "$work/load_gs.o" "$root/src/i386/load_gs.S"
"$cc" -m32 -nostdlib -static -o "$work/hello" \
	"$work/crt1.o" "$work/hello.o" "$work/write.o" "$work/getpid.o" "$work/errno.o" "$work/syscall.o" "$work/tls.o" "$work/memory.o" "$work/load_gs.o"

LC_ALL=C readelf -h "$work/hello" | grep -Eq 'Class:[[:space:]]+ELF32'
LC_ALL=C readelf -h "$work/hello" | grep -Eq 'Machine:[[:space:]]+Intel 80386'
"$mcc" --target=i386 -I"$root/include" -c -o "$work/atomic-test.o" "$work/atomic.c"
"$cc" -m32 -nostdlib -static -o "$work/atomic-test" "$work/crt1.o" "$work/atomic-test.o" "$work/atomic.o" "$work/tls.o" "$work/memory.o" "$work/load_gs.o" "$work/syscall.o"

if [ "${MEUOS_I386_RUN:-0}" = 1 ]; then
	"$work/hello"
	"$work/atomic-test"
fi

printf '%s\n' 'i386 bootstrap ELF32 check passed'
