#!/bin/sh
# riscv64_e2e.sh  — 端到端验证 mt/as + mt/ld + qemu-riscv64
#
# 从汇编到运行的完整链路：
#   编写 .S → mt/as 汇编 → mt/ld 链接 → qemu-riscv64 运行
#
# 本测试只验证 as+ld 本身对 riscv64 的正确性。

AS=${1:-build/bin/as}
LD=${2:-build/bin/ld}
QEMU=${3:-../../env/qemu/qemu-riscv64}

fail=0

echo "=== riscv64 e2e: return 42 ==="
cat > /tmp/meuos-riscv64-e2e.S << 'EOF'
.text
.globl _start
_start:
	li	a0, 42
	li	a7, 93		/* __NR_exit */
	ecall
EOF

$AS --target=riscv64 /tmp/meuos-riscv64-e2e.S -o /tmp/meuos-riscv64-e2e.o 2>&1 || {
	echo "FAIL: mt/as assembly failed"; exit 1; }

$LD --target=riscv64 -o /tmp/meuos-riscv64-e2e /tmp/meuos-riscv64-e2e.o 2>&1 || {
	echo "FAIL: mt/ld link failed"; exit 1; }

file /tmp/meuos-riscv64-e2e | grep -q "RISC-V" || {
	echo "FAIL: output is not RISC-V ELF"; exit 1; }

$QEMU /tmp/meuos-riscv64-e2e; rc=$?
if [ "$rc" != "42" ]; then
	echo "FAIL: qemu exit=$rc (expected 42)"
	exit 1
fi

echo "riscv64 e2e: PASS"
exit 0
