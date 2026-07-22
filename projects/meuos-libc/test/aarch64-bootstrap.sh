#!/bin/sh
# aarch64-bootstrap.sh -- aarch64 跨编译自检 + 可选 qemu-aarch64 运行时 gate。
#
# 模仿 test/i386-bootstrap.sh 的结构，目标是：
#   1) 验证 mcc + meuos-libc 能为 aarch64 输出有效 ELF64/AArch64 二进制；
#   2) 默认交叉编译 hello / atomic / phase2-counter；ELF 头验证 (Class=ELF64,
#      Machine=AArch64) 通过即视为 Phase-2 跨编译端到端可用；
#   3) 设 MEUOS_AARCH64_RUN=1 且 qemu-aarch64-static 可执行时跑全部二进制，
#      期望 phase2-counter 输出 "counter = 2000"。
#
# 默认 qemu 二进制查找顺序：$MEUOS_AARCH64_QEMU > env/qemu/qemu-aarch64-static
# > PATH 中的 qemu-aarch64 / qemu-aarch64-static。找不到则只跑交叉编译与
# readelf 自检，运行时 gate 静默跳过。
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mcc=${MCC:-"$root/../mcc/mcc"}
ascc=${ASCC:-aarch64-linux-gnu-gcc}
build=${BUILD:-"$root/build/aarch64"}
sysroot=${SYSROOT:-"$root/../sysroot-aarch64"}
qemu=${MEUOS_AARCH64_QEMU:-}
work=${TMPDIR:-/tmp}/meuos-aarch64-bootstrap.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

if [ ! -f "$build/libc-meuos.a" ] || [ ! -f "$build/libatomic-meuos.a" ] || [ ! -f "$build/crt1.o" ]; then
	printf 'aarch64 build artefacts missing under %s; run "make ARCH=aarch64" first\n' "$build" >&2
	exit 1
fi

# 测试 1: hello world via write(2)
cat > "$work/hello.c" <<'CEOF'
#include <unistd.h>
int main(void) { if (getpid() <= 0) return 2; return write(1, "aarch64 MeuOS libc\n", 19) == 19 ? 0 : 1; }
CEOF
# 测试 2: 三种宽度的 C11 atomic (int / unsigned char / unsigned short)
cat > "$work/atomic.c" <<'CEOF'
#include <stdatomic.h>
_Atomic int value = 3;
_Atomic unsigned char byte_value = 1;
_Atomic unsigned short short_value = 2;
int main(void) { int expected = 5; unsigned char b = 1; unsigned short s = 2; if (atomic_fetch_add(&value, 2) != 3) return 1; if (!atomic_compare_exchange_strong(&value, &expected, 9)) return 2; if (!atomic_compare_exchange_strong(&byte_value, &b, 3)) return 3; if (!atomic_compare_exchange_strong(&short_value, &s, 4)) return 4; return atomic_load(&value) == 9 && atomic_load(&byte_value) == 3 && atomic_load(&short_value) == 4 ? 0 : 5; }
CEOF
# 测试 3: phase2_counter -- 两条 C11 线程各 +1000，最后期望 counter == 2000
cat > "$work/phase2.c" <<'CEOF'
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>
static _Atomic int counter;
static int worker(void *argument) { (void)argument; for (int i = 0; i < 1000; ++i) counter++; return 0; }
int main(void) { thrd_t first, second; if (thrd_create(&first, worker, 0) != thrd_success || thrd_create(&second, worker, 0) != thrd_success) return 1; if (thrd_join(first, 0) != thrd_success || thrd_join(second, 0) != thrd_success) return 1; printf("counter = %d\n", counter); return counter != 2000; }
CEOF
# 测试 4: bare_tls -- 主线程 _Thread_local 初值 + 子线程独立 TLS + errno 隔离。
# aarch64 在静态 binary 里 GAP_ABOVE_TP = 16，主线程 TPIDR_EL0 = mmap 起点
# 时，子线程访问的 tprel_lo12 偏移是 image_offset + 16，因此 .tdata 必须
# 拷贝到 mmap_base + 16，否则主线程 .tdata 初值读到 0。验证两组初值
# (main=5, child=9) 即可隔离 aarch64 TLS 布局的回归。
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

# Compile .c with mcc --target=aarch64; .S with cross gcc (mcc 不支持
# msr/mrs/svc/brk 等系统指令，所有 aarch64 arch runtime 必须走交叉汇编器)。
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/hello.o" "$work/hello.c"
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/atomic.o" "$work/atomic.c"
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/phase2.o" "$work/phase2.c"
"$mcc" --target=aarch64 -I"$root/include" -c -o "$work/bare_tls.o" "$work/bare_tls.c"
"$ascc" -c -o "$work/crt1.o" "$root/crt/aarch64/crt1.S"
"$ascc" -c -o "$work/syscall.o" "$root/src/internal/arch/aarch64/syscall.S"
"$ascc" -c -o "$work/atomic-asm.o" "$root/src/arch/aarch64/atomic.S"
"$ascc" -c -o "$work/setjmp.o" "$root/src/arch/aarch64/setjmp.S"
"$ascc" -c -o "$work/sigreturn.o" "$root/src/arch/aarch64/sigreturn.S"
"$ascc" -c -o "$work/thread_clone.o" "$root/src/arch/aarch64/thread_clone.S"
"$ascc" -c -o "$work/set_tls.o" "$root/src/arch/aarch64/set_tls.S"

"$ascc" -nostdlib -static -o "$work/hello" \
	"$work/crt1.o" "$work/hello.o" "$work/syscall.o" "$work/atomic-asm.o" \
	"$build/libc-meuos.a" "$build/libatomic-meuos.a"
"$ascc" -nostdlib -static -o "$work/atomic-test" \
	"$work/crt1.o" "$work/atomic.o" "$work/syscall.o" "$work/atomic-asm.o" \
	"$build/libc-meuos.a" "$build/libatomic-meuos.a"
"$ascc" -nostdlib -static -o "$work/phase2" \
	"$work/crt1.o" "$work/phase2.o" "$work/syscall.o" "$work/atomic-asm.o" \
	"$work/setjmp.o" "$work/sigreturn.o" "$work/thread_clone.o" "$work/set_tls.o" \
	"$build/libc-meuos.a" "$build/libatomic-meuos.a"
"$ascc" -nostdlib -static -o "$work/bare-tls" \
	"$work/crt1.o" "$work/bare_tls.o" "$work/syscall.o" "$work/atomic-asm.o" \
	"$work/setjmp.o" "$work/sigreturn.o" "$work/thread_clone.o" "$work/set_tls.o" \
	"$build/libc-meuos.a" "$build/libatomic-meuos.a"

# ELF 头验证：必须 Class=ELF64、Machine=AArch64。
for bin in "$work/hello" "$work/atomic-test" "$work/phase2" "$work/bare-tls"; do
	LC_ALL=C readelf -h "$bin" | grep -Eq 'Class:[[:space:]]+ELF64' \
		|| { echo "FAIL: $bin is not ELF64" >&2; exit 1; }
	LC_ALL=C readelf -h "$bin" | grep -Eq 'Machine:[[:space:]]+AArch64' \
		|| { echo "FAIL: $bin is not AArch64" >&2; exit 1; }
done

# 可选运行时 gate：MEUOS_AARCH64_RUN=1 时尝试 qemu-aarch64-static。
if [ "${MEUOS_AARCH64_RUN:-0}" = 1 ]; then
	if [ -z "$qemu" ]; then
		for candidate in "$root/env/qemu/qemu-aarch64-static" \
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
	"$qemu" "$work/hello"
	"$qemu" "$work/atomic-test"
out=$("$qemu" "$work/phase2" 2>&1) || { echo "phase2 failed: $out" >&2; exit 1; }
echo "$out" | grep -Eq '^counter = 2000$' \
		|| { echo "phase2 unexpected output: $out" >&2; exit 1; }
	btls=$("$qemu" "$work/bare-tls" 2>&1) || { echo "bare-tls failed: $btls" >&2; exit 1; }
	echo "$btls" | grep -Eq '^tls main=5 child=9 errno=31/47$' \
		|| { echo "bare-tls unexpected output: $btls" >&2; exit 1; }
fi

printf '%s\n' 'aarch64 bootstrap ELF64 check passed'
