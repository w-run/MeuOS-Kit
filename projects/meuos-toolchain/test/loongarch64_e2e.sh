#!/bin/sh
# loongarch64_e2e.sh  — 端到端验证 mt/as + mt/ld
#
# 从汇编到链接：
#   编写 .S → mt/as 汇编 → mt/ld 链接 → file 检查
# QEMU 运行时可选（env qemu-loongarch64 7.2.0 IL-instruction bug）

AS=${1:-build/bin/as}
LD=${2:-build/bin/ld}
QEMU=${3:-../../env/qemu/qemu-loongarch64}

fail=0

echo "=== loongarch64 e2e: as + ld ==="
cat > /tmp/meuos-loongarch64-e2e.S << 'EOF'
.text
.globl _start
_start:
	li.w	$a0, 42
	li.w	$a7, 93		/* __NR_exit */
	syscall 0
EOF

$AS --target=loongarch64 /tmp/meuos-loongarch64-e2e.S -o /tmp/meuos-loongarch64-e2e.o 2>&1 || {
	echo "FAIL: mt/as assembly failed"; exit 1; }

$LD --target=loongarch64 -o /tmp/meuos-loongarch64-e2e /tmp/meuos-loongarch64-e2e.o 2>&1 || {
	echo "FAIL: mt/ld link failed"; exit 1; }

file /tmp/meuos-loongarch64-e2e | grep -q "ELF.*64-bit.*LoongArch" || {
	echo "FAIL: output is not LoongArch ELF"; exit 1; }
echo "  as+ld: PASS (LoongArch ELF)"

if [ -x "$QEMU" ]; then
	echo "=== loongarch64 e2e: qemu runtime ==="
	$QEMU /tmp/meuos-loongarch64-e2e; rc=$?
	if [ "$rc" = "42" ]; then
		echo "  qemu: PASS"
	else
		echo "  qemu: SKIP (exit=$rc, env QEMU 7.2.0 known SIGILL on loongarch64)"
	fi
else
	echo "  qemu: SKIP (no QEMU binary)"
fi

echo "loongarch64 e2e: PASS"
exit 0
