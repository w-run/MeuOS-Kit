#!/bin/sh
# check-i18n — mcc/m++ 双语消息目录回归（--lang=en/zh）。
#
# 验证（对照 src/util/i18n.c 的目录 + src/driver/main.c 的 --lang）：
#   1) --lang=en 诊断为英文、--lang=zh 为中文（同一错误文件）
#   2) --error-json 的 message 字段随所选语言
#   3) --explain 修复建议语言一致（m++ nodiscard 警告）
#   4) --help 按语言输出（en/zh 标记）
#   5) LANG 环境推断：zh* -> zh、en* -> en
#   6) 未收录消息降级为 en（不崩溃）
#
# 用法：sh test/i18n/run.sh [mcc 二进制] [m++ 二进制]
set -e
BIN=${1:-./mcc}
MPP=${2:-./m++}
DIR=$(dirname "$0")

fail() { echo "FAIL: $*" >&2; exit 1; }

# --- 1) 错误文本按语言 ---
$BIN --lang=en --specs=host -c -o /tmp/i18n-e.o "$DIR/err.c" 2>/tmp/i18n-en.err || true
$BIN --lang=zh --specs=host -c -o /tmp/i18n-z.o "$DIR/err.c" 2>/tmp/i18n-zh.err || true
grep -q "undeclared identifier: nope" /tmp/i18n-en.err || fail "--lang=en should show English message"
if grep -q "未声明的标识符" /tmp/i18n-en.err; then fail "--lang=en must not contain Chinese"; fi
grep -q "未声明的标识符：nope" /tmp/i18n-zh.err || fail "--lang=zh should show Chinese message"
if grep -q "undeclared identifier: nope" /tmp/i18n-zh.err; then fail "--lang=zh must not contain English body"; fi
grep -q "错误:" /tmp/i18n-zh.err || fail "--lang=zh should use Chinese '错误:' word"

# --- 2) --error-json message 随语言 ---
$BIN --lang=en --error-json --specs=host -c -o /tmp/i18n-e.o "$DIR/err.c" 2>/tmp/i18n-je.json || true
$BIN --lang=zh --error-json --specs=host -c -o /tmp/i18n-z.o "$DIR/err.c" 2>/tmp/i18n-jz.json || true
grep -q '"message":"undeclared identifier: nope"' /tmp/i18n-je.json || fail "JSON (en) message wrong"
grep -q '"message":"未声明的标识符：nope"' /tmp/i18n-jz.json || fail "JSON (zh) message wrong"

# --- 3) --explain 修复建议语言一致（m++ nodiscard 警告） ---
$MPP --lang=en --explain --specs=host -o /tmp/i18n-nd-e "$DIR/nodiscard.cc" 2>/tmp/i18n-ne.err || true
$MPP --lang=zh --explain --specs=host -o /tmp/i18n-nd-z "$DIR/nodiscard.cc" 2>/tmp/i18n-nz.err || true
grep -q "hint: use the return value" /tmp/i18n-ne.err || fail "--explain (en) hint should be English"
grep -q "建议: 使用返回值" /tmp/i18n-nz.err || fail "--explain (zh) hint should be Chinese"

# --- 4) --help 按语言 ---
$BIN --lang=en --help >/tmp/i18n-he.txt 2>&1 || true
$BIN --lang=zh --help >/tmp/i18n-hz.txt 2>&1 || true
grep -q "Output control" /tmp/i18n-he.txt || fail "--help (en) missing English marker"
grep -q "输出控制" /tmp/i18n-hz.txt || fail "--help (zh) missing Chinese marker"

# --- 5) LANG 环境推断 ---
env LANG=en_US.UTF-8 $BIN --specs=host -c -o /tmp/i18n-e.o "$DIR/err.c" 2>/tmp/i18n-le.err || true
env LANG=zh_CN.UTF-8 $BIN --specs=host -c -o /tmp/i18n-z.o "$DIR/err.c" 2>/tmp/i18n-lz.err || true
grep -q "undeclared identifier" /tmp/i18n-le.err || fail "LANG=en_US should infer English"
grep -q "未声明的标识符" /tmp/i18n-lz.err || fail "LANG=zh_CN should infer Chinese"

# --- 6) 未收录消息优雅降级为 en（不崩溃）：合法程序 --lang=zh 正常编译运行 ---
printf 'int main(void){ return 0; }\n' > /tmp/i18n-ok.c
$BIN --lang=zh --specs=host -o /tmp/i18n-ok /tmp/i18n-ok.c 2>/tmp/i18n-ok.err || \
  fail "valid program at --lang=zh should compile"
/tmp/i18n-ok || fail "valid program should run"

echo "PASS: check-i18n (--lang=en/zh 错误/JSON/--explain/--help/LANG 推断/降级)"
