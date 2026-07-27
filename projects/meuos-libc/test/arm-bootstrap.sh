#!/bin/sh
# arm-bootstrap.sh -- arm cross-compile self-check + optional qemu runtime gate.
#
# Goals:
#   1) Verify mcc + meuos-libc produce valid ELF32/ARM binaries.
#   2) Cross-compile hello / atomic / setjmp / phase2 / bare_tls /
#      malloc_threads; ELF32 header check (Class=ELF32, Machine=ARM) passing
#      means Phase-2 cross-compile is end-to-end usable.
#   3) With MEUOS_ARMV7_RUN=1 and qemu-arm available, run all binaries;
#      expect phase2 "counter = 2000", bare_tls
#      "tls main=5 child=9 errno=31/47", atomic / malloc_threads exit 0.
#
# arm differs from 64-bit targets in that it has VFP hard-float and uses
# set_tls.S (the main thread installs tp via `mcr p15,0,r0,c13,c0,3`).
#
# qemu lookup order: $MEUOS_ARMV7_QEMU > env/qemu/qemu-arm-static >
# env/qemu/qemu-arm > PATH (qemu-arm / qemu-arm-static).
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mcc=${MCC:-"$root/../mcc/mcc"}
ascc=${ASCC:-"arm-linux-gnu-gcc"}
ARM_ARCH=${ARM_ARCH:-armv7-a}
ARM_FLOAT_ABI=${ARM_FLOAT_ABI:-hard}
ARM_FPU=${ARM_FPU:-vfpv3-d16}
arch=${ARCH:-"-march=$ARM_ARCH -mfloat-abi=$ARM_FLOAT_ABI -mfpu=$ARM_FPU"}
mt_ld=${MT_LD:-"$root/../meuos-toolchain/build/bin/ld"}
mt_as=${MT_AS:-"$root/../meuos-toolchain/build/bin/as"}
build=${BUILD:-"$root/build/arm"}
sysroot=${SYSROOT:-"$root/../sysroot"}
qemu=${MEUOS_ARMV7_QEMU:-}
work=${TMPDIR:-/tmp}/meuos-arm-bootstrap.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

# Verify arm build artefacts exist.
if [ ! -f "$build/libc-meuos.a" ] || [ ! -f "$build/libatomic-meuos.a" ] || [ ! -f "$build/crt1.o" ]; then
	printf 'arm build artefacts missing under %s; run "make ARCH=arm" first\n' "$build" >&2
	exit 1
fi

# ===== Test sources =====
cat > "$work/hello.c" <<'CEOF'
#include <unistd.h>
int main(void) { if (getpid() <= 0) return 2; return write(1, "arm MeuOS libc\n", 18) == 18 ? 0 : 1; }
CEOF
cat > "$work/atomic.c" <<'CEOF'
#include <stdatomic.h>
_Atomic int value = 3;
_Atomic unsigned char byte_value = 1;
_Atomic unsigned short short_value = 2;
int main(void) { int expected = 5; unsigned char b = 1; unsigned short s = 2; if (atomic_fetch_add(&value, 2) != 3) return 1; if (!atomic_compare_exchange_strong(&value, &expected, 9)) return 2; if (!atomic_compare_exchange_strong(&byte_value, &b, 3)) return 3; if (!atomic_compare_exchange_strong(&short_value, &s, 4)) return 4; return atomic_load(&value) == 9 && atomic_load(&byte_value) == 3 && atomic_load(&short_value) == 4 ? 0 : 5; }
CEOF
cat > "$work/setjmp.c" <<'CEOF'
#include <setjmp.h>
#include <stdio.h>
static int deep(jmp_buf jb, int depth) { if (depth == 0) longjmp(jb, 42); return -1; }
int main(void) {
	jmp_buf jb;
	int r;
	r = setjmp(jb);
	if (r == 0) { deep(jb, 0); printf("longjmp did not return to setjmp\n"); return 1; }
	if (r != 42) { printf("longjmp value wrong: %d\n", r); return 2; }
	r = setjmp(jb);
	if (r == 0) { longjmp(jb, 0); return 3; }
	if (r != 1) return 4;
	puts("setjmp ok");
	return 0;
}
CEOF
cat > "$work/phase2.c" <<'CEOF'
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>
static _Atomic int counter;
static int worker(void *argument) { (void)argument; for (int i = 0; i < 1000; ++i) counter++; return 0; }
int main(void) { thrd_t first, second; if (thrd_create(&first, worker, 0) != thrd_success || thrd_create(&second, worker, 0) != thrd_success) return 1; if (thrd_join(first, 0) != thrd_success || thrd_join(second, 0) != thrd_success) return 1; printf("counter = %d\n", counter); return counter != 2000; }
CEOF
cat > "$work/bare_tls.c" <<'CEOF'
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
CEOF
cat > "$work/malloc_threads.c" <<'CEOF'
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
enum { worker_count = 4, iterations = 2000 };
static _Atomic int failed;
static int worker(void *argument) {
	int worker_index = (int)(long)argument;
	for (int iteration = 0; iteration < iterations; ++iteration) {
		size_t size = (size_t)(worker_index * 16 + iteration) % 256 + 16;
		void *memory = malloc(size);
		if (!memory) { atomic_fetch_add(&failed, 1); return 1; }
		((char *)memory)[0] = (char)iteration;
		((char *)memory)[size - 1] = (char)iteration;
		free(memory);
	}
	return 0;
}
int main(void) {
	thrd_t threads[worker_count];
	for (int index = 0; index < worker_count; ++index)
		if (thrd_create(&threads[index], worker, (void *)(long)index) != thrd_success) return 1;
	for (int index = 0; index < worker_count; ++index)
		if (thrd_join(threads[index], 0) != thrd_success) return 2;
	return atomic_load(&failed) != 0 ? 3 : 0;
}
CEOF

# ===== Compile =====
arch_runtime_objs() {
	for src in "$@"; do
		local base=$(basename "$src" .S)
		if [ "$base" = "crt1" ]; then
			"$ascc" $arch -c -o "$work/crt1.o" "$src"
		else
			"$ascc" $arch -c -o "$work/asm-$base.o" "$src"
		fi
	done
}
"$ascc" $arch -I"$root/include" -c -o "$work/hello.o" "$work/hello.c"
"$ascc" $arch -I"$root/include" -c -o "$work/atomic.o" "$work/atomic.c"
"$ascc" $arch -I"$root/include" -c -o "$work/setjmp.o" "$work/setjmp.c"
"$ascc" $arch -I"$root/include" -c -o "$work/phase2.o" "$work/phase2.c"
"$ascc" $arch -I"$root/include" -c -o "$work/bare_tls.o" "$work/bare_tls.c"
"$ascc" $arch -I"$root/include" -c -o "$work/malloc_threads.o" "$work/malloc_threads.c"
arch_runtime_objs \
	"$root/crt/arm/crt1.S" \
	"$root/src/internal/arch/arm/syscall.S" \
	"$root/src/arch/arm/atomic.S" \
	"$root/src/arch/arm/setjmp.S" \
	"$root/src/arch/arm/sigreturn.S" \
	"$root/src/arch/arm/thread_clone.S" \
	"$root/src/arch/arm/set_tls.S"

# ===== Link (via cross-gcc: host linker for arm) =====
link_full() {
	local out=$1 main=$2; shift 2
	"$ascc" $arch -nostdlib -static -o "$out" "$work/crt1.o" "$main" "$work/asm-syscall.o" "$work/asm-atomic.o" \
		"$work/asm-setjmp.o" "$work/asm-sigreturn.o" "$work/asm-thread_clone.o" "$work/asm-set_tls.o" \
		"$build/libc-meuos.a" "$build/libatomic-meuos.a" "$@"
}
link_full "$work/hello" "$work/hello.o"
link_full "$work/atomic-test" "$work/atomic.o"
link_full "$work/setjmp-test" "$work/setjmp.o"
link_full "$work/phase2" "$work/phase2.o"
link_full "$work/bare-tls" "$work/bare_tls.o"
link_full "$work/malloc-threads" "$work/malloc_threads.o"

# ===== ELF header check (ELF32 / ARM) =====
for bin in "$work/hello" "$work/atomic-test" "$work/setjmp-test" "$work/phase2" "$work/bare-tls" "$work/malloc-threads"; do
	LC_ALL=C readelf -h "$bin" 2>/dev/null | grep -Eq 'Class:[[:space:]]+ELF32' \
		|| { echo "FAIL: $bin is not ELF32" >&2; exit 1; }
	LC_ALL=C readelf -h "$bin" 2>/dev/null | grep -Eq 'Machine:[[:space:]]+ARM' \
		|| { echo "FAIL: $bin is not ARM" >&2; exit 1; }
done

# ===== Optional runtime gate =====
if [ "${MEUOS_ARMV7_RUN:-0}" = 1 ]; then
	if [ -z "$qemu" ]; then
		for candidate in "$root/env/qemu/qemu-arm-static" \
		                  "$root/env/qemu/qemu-arm" \
		                  "$root/../env/qemu/qemu-arm-static" \
		                  "$root/../env/qemu/qemu-arm" \
		                  qemu-arm qemu-arm-static; do
			if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then
				qemu="$candidate"; break
			fi
		done
	fi
	if [ -z "$qemu" ] || { [ ! -x "$qemu" ] && ! command -v "$qemu" >/dev/null 2>&1; }; then
		echo "MEUOS_ARMV7_RUN=1 but no qemu-arm found" >&2
		exit 1
	fi
	# 1) hello
	out=$("$qemu" "$work/hello" 2>&1) || { echo "hello failed: $out" >&2; exit 1; }
	[ "$out" = "arm MeuOS libc" ] || { echo "hello wrong output: $out" >&2; exit 1; }
	# 2) atomic
	"$qemu" "$work/atomic-test" || { echo "atomic-test failed" >&2; exit 1; }
	# 3) setjmp
	out=$("$qemu" "$work/setjmp-test" 2>&1) || { echo "setjmp failed: $out" >&2; exit 1; }
	[ "$out" = "setjmp ok" ] || { echo "setjmp wrong output: $out" >&2; exit 1; }
	# 4) phase2
	out=$("$qemu" "$work/phase2" 2>&1) || { echo "phase2 failed: $out" >&2; exit 1; }
	[ "$out" = "counter = 2000" ] || { echo "phase2 wrong output: $out" >&2; exit 1; }
	# 5) bare_tls
	out=$("$qemu" "$work/bare-tls" 2>&1) || { echo "bare-tls failed: $out" >&2; exit 1; }
	[ "$out" = "tls main=5 child=9 errno=31/47" ] || { echo "bare-tls wrong output: $out" >&2; exit 1; }
	# 6) malloc_threads
	"$qemu" "$work/malloc-threads" || { echo "malloc-threads failed" >&2; exit 1; }
fi

printf '%s\n' 'arm bootstrap ELF32/ARM check passed'