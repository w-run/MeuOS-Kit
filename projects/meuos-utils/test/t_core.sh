#!/bin/sh
# t_core.sh — 新增 P1/P2 工具的回归测试
. ./common.sh

# --- mkdir ---
rm -rf "/tmp/meuos_test_$$/mk_test"
U mkdir -p "/tmp/meuos_test_$$/mk_test/a/b/c" || fail "mkdir -p 失败"
[ -d "/tmp/meuos_test_$$/mk_test/a/b/c" ] && pass "mkdir -p 递归创建" || fail "mkdir -p 递归创建" "目录不存在"
U mkdir -m 700 "/tmp/meuos_test_$$/mk_test/perm" 2>/dev/null
[ "$(stat -c '%a' "/tmp/meuos_test_$$/mk_test/perm" 2>/dev/null)" = "700" ] && pass "mkdir -m 设置权限" || fail "mkdir -m 设置权限" "$(stat -c '%a' "/tmp/meuos_test_$$/mk_test/perm" 2>/dev/null)"
U mkdir "/tmp/meuos_test_$$/mk_test/exist" && U mkdir "/tmp/meuos_test_$$/mk_test/exist" 2>/dev/null
[ $? -ne 0 ] && pass "mkdir 已存在报错" || fail "mkdir 已存在报错" "rc=$?"
U mkdir -p "/tmp/meuos_test_$$/mk_test/exist" && pass "mkdir -p 已存在不报错" || fail "mkdir -p 已存在不报错"

# --- touch ---
rm -rf "/tmp/meuos_test_$$/tc_test"; mkdir -p "/tmp/meuos_test_$$/tc_test"
U touch "/tmp/meuos_test_$$/tc_test/new" && [ -f "/tmp/meuos_test_$$/tc_test/new" ] && pass "touch 创建空文件" || fail "touch 创建空文件"
U touch -c "/tmp/meuos_test_$$/tc_test/never" 2>/dev/null
[ ! -f "/tmp/meuos_test_$$/tc_test/never" ] && pass "touch -c 不创建" || fail "touch -c 不创建"

# --- ln ---
rm -rf "/tmp/meuos_test_$$/ln_test"; mkdir -p "/tmp/meuos_test_$$/ln_test"
echo hi > "/tmp/meuos_test_$$/ln_test/src"
U ln "/tmp/meuos_test_$$/ln_test/src" "/tmp/meuos_test_$$/ln_test/hard" && [ -f "/tmp/meuos_test_$$/ln_test/hard" ] && pass "ln 硬链接" || fail "ln 硬链接"
U ln -s src "/tmp/meuos_test_$$/ln_test/soft" && [ -L "/tmp/meuos_test_$$/ln_test/soft" ] && pass "ln -s 软链接" || fail "ln -s 软链接"
U ln -f "/tmp/meuos_test_$$/ln_test/src" "/tmp/meuos_test_$$/ln_test/hard" 2>/dev/null && pass "ln -f 强制" || fail "ln -f 强制"

# --- cmp ---
rm -rf "/tmp/meuos_test_$$/cmp_test"; mkdir -p "/tmp/meuos_test_$$/cmp_test"
echo hello > "/tmp/meuos_test_$$/cmp_test/f1"; cp "/tmp/meuos_test_$$/cmp_test/f1" "/tmp/meuos_test_$$/cmp_test/f2"
U cmp "/tmp/meuos_test_$$/cmp_test/f1" "/tmp/meuos_test_$$/cmp_test/f2"; [ $? -eq 0 ] && pass "cmp 相同 rc=0" || fail "cmp 相同 rc=0" "rc=$?"
echo world > "/tmp/meuos_test_$$/cmp_test/f3"
U cmp "/tmp/meuos_test_$$/cmp_test/f1" "/tmp/meuos_test_$$/cmp_test/f3"; [ $? -eq 1 ] && pass "cmp 不同 rc=1" || fail "cmp 不同 rc=1" "rc=$?"
U cmp -s "/tmp/meuos_test_$$/cmp_test/f1" "/tmp/meuos_test_$$/cmp_test/f3"; [ $? -eq 1 ] && pass "cmp -s 静默 rc=1" || fail "cmp -s 静默 rc=1"

# --- xargs ---
out=$(printf 'a\nb\nc\n' | U xargs -n 2 echo)
[ "$out" = "a b
c" ] && pass "xargs -n 2" || fail "xargs -n 2" "got=[$out]"
out=$(printf 'foo\nbar\n' | U xargs -I {} echo "found:{}")
[ "$out" = "found:foo
found:bar" ] && pass "xargs -I {}" || fail "xargs -I {}" "got=[$out]"

# --- rmdir ---
rm -rf "/tmp/meuos_test_$$/rm_test"; mkdir -p "/tmp/meuos_test_$$/rm_test/a/b/c"
U rmdir -p "/tmp/meuos_test_$$/rm_test/a/b/c" && [ ! -e "/tmp/meuos_test_$$/rm_test" ] && pass "rmdir -p 递归删除" || fail "rmdir -p 递归删除"

# --- chmod ---
rm -rf "/tmp/meuos_test_$$/ch_test"; mkdir -p "/tmp/meuos_test_$$/ch_test"
echo x > "/tmp/meuos_test_$$/ch_test/m1"
U chmod 755 "/tmp/meuos_test_$$/ch_test/m1" && [ "$(stat -c '%a' "/tmp/meuos_test_$$/ch_test/m1")" = "755" ] && pass "chmod 八进制" || fail "chmod 八进制" "$(stat -c '%a' "/tmp/meuos_test_$$/ch_test/m1")"
U chmod u+x "/tmp/meuos_test_$$/ch_test/m1" && [ "$(stat -c '%a' "/tmp/meuos_test_$$/ch_test/m1")" = "755" ] && pass "chmod u+x" || fail "chmod u+x" "$(stat -c '%a' "/tmp/meuos_test_$$/ch_test/m1")"
U chmod go-w "/tmp/meuos_test_$$/ch_test/m1" && [ "$(stat -c '%a' "/tmp/meuos_test_$$/ch_test/m1")" = "755" ] && pass "chmod go-w" || fail "chmod go-w" "$(stat -c '%a' "/tmp/meuos_test_$$/ch_test/m1")"

# --- stat ---
out=$(U stat --classic "/tmp/meuos_test_$$/ch_test/m1" 2>/dev/null)
[ -n "$out" ] && pass "stat --classic 输出" || fail "stat --classic 输出"
out=$(U stat --json "/tmp/meuos_test_$$/ch_test/m1" 2>/dev/null)
echo "$out" | grep -q '"size"' && pass "stat --json" || fail "stat --json"

t_summary "core utils"
