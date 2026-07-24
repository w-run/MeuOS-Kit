#!/bin/sh
# i386-bootstrap.sh -- i386 cross-compile self-check + optional runtime gate.
#
# Mirrors test/aarch64-bootstrap.sh. Goals:
#   1) Verify mcc + meuos-libc produce valid ELF32/i386 binaries.
#   2) By default cross-compile hello / atomic / setjmp; ELF32 header check
#      (Class=ELF32, Machine=Intel 80386) passing means Phase-2 cross-compile
#      is end-to-end usable.
#   3) With MEUOS_I386_RUN=1 (or on x86_64 with IA32 emulation), run all
#      binaries; phase2 "counter = 2000", bare_tls "tls main=5 child=9
#      errno=31/47", atomic exit 0.
#
# Runtime gate: on x86_64 hosts with IA32 emulation, binaries run directly
# under the host kernel (CONFIG_IA32_EMULATION).  For non-x86 hosts, set
# MEUOS_I386_RUN=1 and provide qemu-i386-static or an i386 chroot.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mcc=${MCC:-"$root/../mcc/mcc"}
cc=${CC:-cc}
sysroot=${SYSROOT:-"$root/../sysroot-i386"}
qemu=${MEUOS_I386_QEMU:-}
work=${TMPDIR:-/tmp}/meuos-i386-bootstrap.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

# Detect whether we can run i386 binaries natively (x86_64 + IA32 emulation)
os=$(uname -m)
can_run=0
if [ "$os" = x86_64 ]; then
	# /proc/sys/abi/vsyscall32 exists on kernels with IA32 emulation
	if [ -f /proc/sys/abi/vsyscall32 ]; then
		can_run=1
	fi
fi
# MEUOS_I386_RUN=1  or  native IA32  →  run tests
run_tests=0
if [ "${MEUOS_I386_RUN:-0}" = 1 ] || [ "$can_run" = 1 ]; then
	run_tests=1
fi

# ===== i386 runtime =====
# Compile and link a test against the sysroot libc archive.
i386_link() {
	local src="$1" out="$2" extra="${3:-}"
	"$mcc" --target=i386 -I"$root/include" -c -o "$work/$(basename "$src" .c).o" "$src"
	# Build a minimal archive with just the objects needed
	# The linker needs crt1 + test.o + libc-meuos.a
	"$cc" -m32 -nostdlib -static -o "$out" \
		"$sysroot/usr/lib/crt1.o" \
		"$work/$(basename "$src" .c).o" \
		$extra \
		"$sysroot/usr/lib/libc-meuos.a"
}
i386_check_elf() {
	LC_ALL=C readelf -h "$1" | grep -Eq 'Class:[[:space:]]+ELF32'
	LC_ALL=C readelf -h "$1" | grep -Eq 'Machine:[[:space:]]+Intel 80386'
}

# ===== Test 1: hello world =====
cat > "$work/hello.c" <<'EOF'
#include <unistd.h>
int main(void) { if (getpid() <= 0) return 2; return write(1, "i386 MeuOS libc\n", 16) == 16 ? 0 : 1; }
EOF
"$mcc" --target=i386 -I"$root/include" -c -o "$work/hello.o" "$work/hello.c"
"$mcc" --target=i386 -I"$root/include" -I"$root/src" -c -o "$work/write.o" "$root/src/syscall/write.c"
"$mcc" --target=i386 -I"$root/include" -I"$root/src" -c -o "$work/getpid.o" "$root/src/syscall/getpid.c"
"$mcc" --target=i386 -I"$root/include" -I"$root/src" -c -o "$work/errno.o" "$root/src/errno/errno.c"
"$cc" -m32 -c -o "$work/crt1.o" "$root/crt/i386/crt1.S"
"$cc" -m32 -c -o "$work/syscall.o" "$root/src/internal/arch/i386/syscall.S"
"$cc" -m32 -c -o "$work/atomic.o" "$root/src/arch/i386/atomic.S"
"$mcc" --target=i386 -I"$root/include" -I"$root/src" -c -o "$work/tls.o" "$root/src/arch/i386/tls.c"
"$mcc" --target=i386 -I"$root/include" -c -o "$work/memory.o" "$root/src/string/memory.c"
"$cc" -m32 -c -o "$work/load_gs.o" "$root/src/arch/i386/load_gs.S"
"$cc" -m32 -nostdlib -static -o "$work/hello" \
	"$work/crt1.o" "$work/hello.o" "$work/write.o" "$work/getpid.o" "$work/errno.o" \
	"$work/syscall.o" "$work/tls.o" "$work/load_gs.o" \
	"$sysroot/usr/lib/libc-meuos.a"
i386_check_elf "$work/hello"

# ===== Test 2: C11 atomics =====
cat > "$work/atomic.c" <<'EOF'
#include <stdatomic.h>
_Atomic int value = 3;
_Atomic unsigned char byte_value = 1;
_Atomic unsigned short short_value = 2;
int main(void) { int expected = 5; unsigned char b = 1; unsigned short s = 2; if (atomic_fetch_add(&value, 2) != 3) return 1; if (!atomic_compare_exchange_strong(&value, &expected, 9)) return 2; if (!atomic_compare_exchange_strong(&byte_value, &b, 3)) return 3; if (!atomic_compare_exchange_strong(&short_value, &s, 4)) return 4; return atomic_load(&value) == 9 && atomic_load(&byte_value) == 3 && atomic_load(&short_value) == 4 ? 0 : 5; }
EOF
"$mcc" --target=i386 -I"$root/include" -c -o "$work/atomic-test.o" "$work/atomic.c"
"$cc" -m32 -nostdlib -static -o "$work/atomic-test" "$work/crt1.o" "$work/atomic-test.o" "$work/atomic.o" \
	"$sysroot/usr/lib/libc-meuos.a"
i386_check_elf "$work/atomic-test"

# ===== Test 3: setjmp/longjmp =====
cat > "$work/setjmp.c" <<'EOF'
#include <setjmp.h>
#include <stdio.h>
static int deep(jmp_buf jb, int depth) { if (depth == 0) longjmp(jb, 42); return -1; }
int main(void) {
	jmp_buf jb;
	int r;
	r = setjmp(jb);
	if (r == 0) { deep(jb, 0); printf("longjmp did not return to setjmp\n"); return 1; }
	if (r != 42) { printf("longjmp value wrong: %d\n", r); return 2; }
	printf("setjmp ok\n");
	return 0;
}
EOF
"$cc" -m32 -c -o "$work/setjmp.o" "$root/src/arch/i386/setjmp.S"
"$mcc" --target=i386 -I"$root/include" -c -o "$work/setjmp-test.o" "$work/setjmp.c"
"$cc" -m32 -nostdlib -static -o "$work/setjmp-test" \
	"$work/crt1.o" "$work/setjmp-test.o" "$work/setjmp.o" \
	"$sysroot/usr/lib/libc-meuos.a"
i386_check_elf "$work/setjmp-test"

# ===== Test 4: phase2_counter (2 threads, 1000 increments each) =====
cat > "$work/phase2.c" <<'EOF'
#include <stdio.h>
#include <threads.h>
_Atomic int counter = 0;
int worker(void *arg) {
    for (int i = 0; i < 1000; i++) counter++;
    return 0;
}
int main(void) {
    thrd_t t1, t2;
    if (thrd_create(&t1, worker, NULL) != thrd_success) return 1;
    if (thrd_create(&t2, worker, NULL) != thrd_success) return 2;
    thrd_join(t1, NULL);
    thrd_join(t2, NULL);
    printf("counter = %d\n", counter);
    return counter == 2000 ? 0 : 3;
}
EOF
"$mcc" --target=i386 -I"$root/include" -c -o "$work/phase2.o" "$work/phase2.c"
"$cc" -m32 -nostdlib -static -o "$work/phase2-test" \
	"$sysroot/usr/lib/crt1.o" "$work/phase2.o" \
	"$sysroot/usr/lib/libc-meuos.a"
i386_check_elf "$work/phase2-test"

# ===== Test 5: bare_tls (_Thread_local + errno isolation) =====
cat > "$work/bare_tls.c" <<'EOF'
#include <errno.h>
#include <stdio.h>
#include <threads.h>
static _Thread_local int local_value = 5;
static int worker_value, worker_errno;
static int worker(void *arg) { (void)arg; local_value = 9; errno = 47; worker_value = local_value; worker_errno = errno; return 0; }
int main(void) {
	thrd_t thread;
	errno = 31;
	if (thrd_create(&thread, worker, 0) != thrd_success || thrd_join(thread, 0) != thrd_success) return 1;
	printf("tls main=%d child=%d errno=%d/%d\n", local_value, worker_value, errno, worker_errno);
	return local_value != 5 || worker_value != 9 || errno != 31 || worker_errno != 47;
}
EOF
"$mcc" --target=i386 -I"$root/include" -c -o "$work/bare_tls.o" "$work/bare_tls.c"
"$cc" -m32 -nostdlib -static -o "$work/bare-tls" \
	"$sysroot/usr/lib/crt1.o" "$work/bare_tls.o" \
	"$sysroot/usr/lib/libc-meuos.a"
i386_check_elf "$work/bare-tls"
if [ "$run_tests" = 1 ]; then
	printf '%s\n' "i386 runtime: hello"
	"$work/hello"

	printf '%s\n' "i386 runtime: atomic"
	"$work/atomic-test"

	printf '%s\n' "i386 runtime: setjmp"
	"$work/setjmp-test"

	printf '%s\n' "i386 runtime: phase2"
	"$work/phase2-test"

	printf '%s\n' "i386 runtime: bare_tls"
	"$work/bare-tls"
fi
# NOTE: bare_tls test is now included.  mcc's i386 backend emits LE
# relocs (@ntpoff) for static _Thread_local variables, which the
# static linker handles correctly.  Non-static _Thread_local globals
# still get IE (@gotntpoff) which the linker cannot transition in
# fully-static builds — see projects/mcc/.todo/gd-tls.md.

printf '%s\n' 'i386 bootstrap ELF32 check passed'
