#!/bin/sh
# i386_e2e.sh  — 端到端验证 mt/as + mt/ld (i386)
#
# 从汇编到链接：
#   编写 .S → mt/as 汇编 → mt/ld 链接 → file 检查
#
# i386 编码器目前受限（无 mem 寻址、% 注册前缀未处理），
# 测试仅验证 as+ld 对 ELF32 i386 的正确性。

AS=${1:-build/bin/as}
LD=${2:-build/bin/ld}
QEMU=${3:-qemu-i386}

echo "=== i386 e2e: as + ld ==="
cat > /tmp/meuos-i386-e2e.S << 'EOF'
.text
.globl _start
_start:
	ret
EOF

$AS --target=i386 /tmp/meuos-i386-e2e.S -o /tmp/meuos-i386-e2e.o 2>&1 || {
	echo "FAIL: mt/as assembly failed"; exit 1; }
echo "  as: PASS (i386 ELF32 object)"

$LD --target=i386 -o /tmp/meuos-i386-e2e /tmp/meuos-i386-e2e.o 2>&1 || {
	echo "FAIL: mt/ld link failed"; exit 1; }

file /tmp/meuos-i386-e2e | grep -q "ELF.*32-bit.*80386" || {
	echo "FAIL: output is not i386 ELF"
	exit 1
}
echo "  as+ld: PASS (i386 ELF)"

echo "i386 e2e: PASS"
exit 0
