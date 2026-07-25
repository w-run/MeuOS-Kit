#!/bin/sh
# aarch64_e2e.sh  — 端到端验证 mt/as + mt/ld + qemu-aarch64
#
# 从汇编到运行的完整链路：
#   编写 .S → mt/as 汇编 → mt/ld 链接 → qemu-aarch64 运行
#
# 注意：完整的 C 程序链路 (crt1 + libc) 仍受 TLS 重定位影响，
# 参见 P1.3（loongarch64 TLS）和 mt/ld TLS 支持计划。
# 本测试只验证 as+ld 本身对 aarch64 的正确性。

AS=${1:-build/bin/as}
LD=${2:-build/bin/ld}
QEMU=${3:-../../env/qemu/qemu-aarch64}

fail=0

echo "=== aarch64 e2e: return 42 ==="
cat > /tmp/meuos-aarch64-e2e.S << 'EOF'
.text
.globl _start
_start:
	mov	x0, #42
	mov	x8, #93		/* __NR_exit */
	svc	#0
EOF

$AS --target=aarch64 /tmp/meuos-aarch64-e2e.S -o /tmp/meuos-aarch64-e2e.o 2>&1 || {
	echo "FAIL: mt/as assembly failed"; exit 1; }

$LD --target=aarch64 -o /tmp/meuos-aarch64-e2e /tmp/meuos-aarch64-e2e.o 2>&1 || {
	echo "FAIL: mt/ld link failed"; exit 1; }

file /tmp/meuos-aarch64-e2e | grep -q "ARM aarch64" || {
	echo "FAIL: output is not aarch64 ELF"; exit 1; }

$QEMU /tmp/meuos-aarch64-e2e; rc=$?
if [ "$rc" != "42" ]; then
	echo "FAIL: qemu exit=$rc (expected 42)"
	exit 1
fi

echo "aarch64 e2e: PASS"
exit 0
