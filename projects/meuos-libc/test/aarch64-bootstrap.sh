#!/bin/sh
# aarch64-bootstrap.sh -- aarch64 cross-compile self-check + optional qemu runtime gate.
#
# Mirrors test/i386-bootstrap.sh structure. Goals:
#   1) Verify mcc + meuos-libc produce valid ELF64/AArch64 binaries.
#   2) By default cross-compile hello / atomic / setjmp / phase2 / bare_tls /
#      malloc_threads; ELF header check (Class=ELF64, Machine=AArch64) passing
#      means Phase-2 cross-compile is end-to-end usable.
#   3) With MEUOS_AARCH64_RUN=1 and qemu-aarch64-static available, run all
#      binaries; expect phase2 "counter = 2000", bare_tls
#      "tls main=5 child=9 errno=31/47", atomic / malloc_threads exit 0.
#
# qemu lookup order: $MEUOS_AARCH64_QEMU > env/qemu/qemu-aarch64-static >
# PATH (qemu-aarch64 / qemu-aarch64-static).
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mcc=${MCC:-"$root/../mcc/mcc"}
ascc=${ASCC:-aarch64-linux-gnu-gcc}
build=${BUILD:-"$root/build/aarch64"}
sysroot=${SYSROOT:-"$root/../sysroot/aarch64"}
qemu=${MEUOS_AARCH64_QEMU:-}
# Repo-root env/qemu (the $root-relative paths below are shallow / wrong in
# linked-worktree checkouts; derive from git so the gate auto-finds qemu).
QEMU_ROOT=""
if command -v git >/dev/null 2>&1; then
	QEMU_ROOT="$(dirname "$(git rev-parse --git-common-dir 2>/dev/null)")/env/qemu"
fi
work=${TMPDIR:-/tmp}/meuos-aarch64-bootstrap.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

# Verify aarch64 build artefacts exist; Makefile check-aarch64-bootstrap triggers
# ARCH=aarch64 build when missing, but running the script directly should also fail loudly.
if [ ! -f "$build/libc-meuos.a" ] || [ ! -f "$build/libatomic-meuos.a" ] || [ ! -f "$build/crt1.o" ]; then
	printf 'aarch64 build artefacts missing under %s; run "make ARCH=aarch64" first\n' "$build" >&2
	exit 1
fi

# # ===== Test sources =====
# 1) hello world via write(2) - exercise the syscall gate + write wrapper
cat > "$work/hello.c" <<'CEOF'
#include <unistd.h>
int main(void) { if (getpid() <= 0) return 2; return write(1, "aarch64 MeuOS libc\n", 19) == 19 ? 0 : 1; }
CEOF
# 2) Three C11 atomic widths (int / uchar / ushort) - exercise libatomic-meuos.a
#    8/16/32/64-bit atomics. mcc aarch64 backend only emits 64-bit load/store
#    sequences; sub-64 widths go through zero-extension, this test guards coverage.
cat > "$work/atomic.c" <<'CEOF'
#include <stdatomic.h>
_Atomic int value = 3;
_Atomic unsigned char byte_value = 1;
_Atomic unsigned short short_value = 2;
int main(void) { int expected = 5; unsigned char b = 1; unsigned short s = 2; if (atomic_fetch_add(&value, 2) != 3) return 1; if (!atomic_compare_exchange_strong(&value, &expected, 9)) return 2; if (!atomic_compare_exchange_strong(&byte_value, &b, 3)) return 3; if (!atomic_compare_exchange_strong(&short_value, &s, 4)) return 4; return atomic_load(&value) == 9 && atomic_load(&byte_value) == 3 && atomic_load(&short_value) == 4 ? 0 : 5; }
CEOF
# 3) setjmp/longjmp - exercise jmp_buf 22-word layout and d8-d15 FP register save
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
# 4) phase2_counter - two C11 threads each +1000, expect counter == 2000
cat > "$work/phase2.c" <<'CEOF'
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>
static _Atomic int counter;
static int worker(void *argument) { (void)argument; for (int i = 0; i < 1000; ++i) counter++; return 0; }
int main(void) { thrd_t first, second; if (thrd_create(&first, worker, 0) != thrd_success || thrd_create(&second, worker, 0) != thrd_success) return 1; if (thrd_join(first, 0) != thrd_success || thrd_join(second, 0) != thrd_success) return 1; printf("counter = %d\n", counter); return counter != 2000; }
CEOF
# 5) bare_tls - main thread _Thread_local initial value + child thread TLS independence + errno isolation.
#    aarch64 static binaries use GAP_ABOVE_TP = 16; with TPIDR_EL0 at the mmap
#    base the linker bakes a 16-byte TCB gap into tprel_lo12, so .tdata must
#    be copied to mmap_base + 16 or the main thread reads 0. The (main=5,
#    child=9) check isolates aarch64 TLS layout regressions.
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
# 6) malloc_threads - 4 threads each doing 2000 malloc/free, exercise thread-safe malloc
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

# # ===== Compile .c (mcc) + assemble .S (cross gcc) =====
# mcc does not support msr/mrs/svc/brk system instructions, so all aarch64 arch
# runtime must go through the cross assembler.
arch_runtime_objs() {
	for src in "$@"; do
		local base=$(basename "$src" .S)
		if [ "$base" = "crt1" ]; then
			"$ascc" -c -o "$work/crt1.o" "$src"
		else
			"$ascc" -c -o "$work/asm-$base.o" "$src"
		fi
	done
}
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/hello.o" "$work/hello.c"
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/atomic.o" "$work/atomic.c"
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/setjmp.o" "$work/setjmp.c"
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/phase2.o" "$work/phase2.c"
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/bare_tls.o" "$work/bare_tls.c"
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/malloc_threads.o" "$work/malloc_threads.c"
# exception runtime foundation (_meuos_exc_throw + setjmp chain): from the
# libc test suite, exercises register->throw->longjmp->catch-value.
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/exc.o" "$root/test/exc.c"
arch_runtime_objs \
	"$root/crt/aarch64/crt1.S" \
	"$root/src/internal/arch/aarch64/syscall.S" \
	"$root/src/arch/aarch64/atomic.S" \
	"$root/src/arch/aarch64/setjmp.S" \
	"$root/src/arch/aarch64/sigreturn.S" \
	"$root/src/arch/aarch64/thread_clone.S" \
	"$root/src/arch/aarch64/set_tls.S"

# # ===== Link =====
link_full() {
	local out=$1 main=$2; shift 2
	"$ascc" -nostdlib -static -o "$out" "$work/crt1.o" "$main" "$work/asm-syscall.o" "$work/asm-atomic.o" \
		"$work/asm-setjmp.o" "$work/asm-sigreturn.o" "$work/asm-thread_clone.o" "$work/asm-set_tls.o" \
		"$build/libc-meuos.a" "$build/libatomic-meuos.a" "$@"
}
link_full "$work/hello" "$work/hello.o"
# hello / atomic only need the minimal arch runtime (crt1 + syscall + atomic);
# phase2 / bare_tls / malloc_threads / setjmp pull in the full runtime
link_full "$work/atomic-test" "$work/atomic.o"
link_full "$work/setjmp-test" "$work/setjmp.o"
link_full "$work/phase2" "$work/phase2.o"
link_full "$work/bare-tls" "$work/bare_tls.o"
link_full "$work/malloc-threads" "$work/malloc_threads.o"
link_full "$work/exc-test" "$work/exc.o"

# # ===== ELF header check =====
for bin in "$work/hello" "$work/atomic-test" "$work/setjmp-test" "$work/phase2" "$work/bare-tls" "$work/malloc-threads" "$work/exc-test"; do
	LC_ALL=C readelf -h "$bin" | grep -Eq 'Class:[[:space:]]+ELF64' \
		|| { echo "FAIL: $bin is not ELF64" >&2; exit 1; }
	LC_ALL=C readelf -h "$bin" | grep -Eq 'Machine:[[:space:]]+AArch64' \
		|| { echo "FAIL: $bin is not AArch64" >&2; exit 1; }
done

# # ===== Optional runtime gate =====
if [ "${MEUOS_AARCH64_RUN:-0}" = 1 ]; then
	if [ -z "$qemu" ]; then
		for candidate in "${QEMU_ROOT}/qemu-aarch64-static" "$root/env/qemu/qemu-aarch64-static" \
		                  "$root/../env/qemu/qemu-aarch64-static" \
		                  qemu-aarch64-static qemu-aarch64; do
			if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then
				qemu="$candidate"; break
			fi
		done
	fi
	if [ -z "$qemu" ] || { [ ! -x "$qemu" ] && ! command -v "$qemu" >/dev/null 2>&1; }; then
		echo "MEUOS_AARCH64_RUN=1 but no qemu-aarch64 found" >&2
		exit 1
	fi
	# 1) hello - exercise the syscall gate + write wrapper end-to-end
	out=$("$qemu" "$work/hello" 2>&1) || { echo "hello failed: $out" >&2; exit 1; }
	[ "$out" = "aarch64 MeuOS libc" ] || { echo "hello wrong output: $out" >&2; exit 1; }
	# 2) atomic - exit 0 means pass (no stdout)
	"$qemu" "$work/atomic-test" || { echo "atomic-test failed" >&2; exit 1; }
	# 3) setjmp - "setjmp ok"
	out=$("$qemu" "$work/setjmp-test" 2>&1) || { echo "setjmp failed: $out" >&2; exit 1; }
	[ "$out" = "setjmp ok" ] || { echo "setjmp wrong output: $out" >&2; exit 1; }
	# 4) phase2 - "counter = 2000"
	out=$("$qemu" "$work/phase2" 2>&1) || { echo "phase2 failed: $out" >&2; exit 1; }
	[ "$out" = "counter = 2000" ] || { echo "phase2 wrong output: $out" >&2; exit 1; }
	# 5) bare_tls - "tls main=5 child=9 errno=31/47"
	out=$("$qemu" "$work/bare-tls" 2>&1) || { echo "bare-tls failed: $out" >&2; exit 1; }
	[ "$out" = "tls main=5 child=9 errno=31/47" ] || { echo "bare-tls wrong output: $out" >&2; exit 1; }
	# 6) malloc_threads - exit 0 means pass (no stdout)
	"$qemu" "$work/malloc-threads" || { echo "malloc-threads failed" >&2; exit 1; }
	# 7) exc (_meuos_exc_throw setjmp chain) - "PASS exc"
	out=$("$qemu" "$work/exc-test" 2>&1) || { echo "exc-test failed: $out" >&2; exit 1; }
	[ "$out" = "PASS exc" ] || { echo "exc wrong output: $out" >&2; exit 1; }
fi

printf '%s\n' 'aarch64 bootstrap ELF64 check passed'
