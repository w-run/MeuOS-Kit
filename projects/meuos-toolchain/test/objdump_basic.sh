#!/bin/sh
# objdump_basic.sh - 验证 objdump 工具基本功能
# Usage: objdump_basic.sh <objdump> <test-exec>
set -eu
OBJDUMP="${1:?usage: objdump_basic.sh <objdump> <exec>}"
EXEC="${2:?usage: objdump_basic.sh <objdump> <exec>}"

export LC_ALL=C

# 1. --version
"$OBJDUMP" --version | grep -q '^meuos-toolchain objdump 0\.2\.0'

# 2. --help
"$OBJDUMP" --help >/dev/null

# 3. -d 反汇编可执行文件, 输出含 Disassembly of section .text
"$OBJDUMP" -d "$EXEC" | grep -q 'Disassembly of section .text'

# 4. 反汇编输出含常见指令 (endbr64 或 push 或 mov 或 ret)
out=$("$OBJDUMP" -d "$EXEC")
echo "$out" | grep -Eq '(endbr64|push|mov|ret|call)'

# 5. -h 节区头
"$OBJDUMP" -h "$EXEC" | grep -q '\.text'

# 6. -D 反汇编所有节区 (不崩)
"$OBJDUMP" -D "$EXEC" >/dev/null

# 7. -s hex dump (不崩)
"$OBJDUMP" -s "$EXEC" >/dev/null

# 8. -x all headers (不崩)
"$OBJDUMP" -x "$EXEC" >/dev/null

# 9. 无参数显示文件头摘要 (不崩)
"$OBJDUMP" "$EXEC" >/dev/null

# 10. 反汇编输出的指令行格式: addr: bytes mnemonic
echo "$out" | grep -Eq '^\s+[0-9a-f]+:\s+[0-9a-f ]+\s+\w+'

echo "mt objdump basic: PASS"
