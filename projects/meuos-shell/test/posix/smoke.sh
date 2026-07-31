#!/bin/sh
# test/posix/smoke.sh — msh POSIX sh 烟雾测试
#
# 每个 case 用 `msh -c '...'` 执行并对比期望输出。
# 与 bash 对照验证行为等价（bash 作为参考实现）。
#
# 用法：sh test/posix/smoke.sh [msh 二进制路径]
# 默认 MSH=./build/msh

MSH="${1:-./build/msh}"
if [ ! -x "$MSH" ]; then
    echo "FATAL: $MSH not found. Run make first." >&2
    exit 1
fi

pass=0
fail=0

# assert_output <desc> <expected> <script>
assert_output() {
    desc="$1"; want="$2"; script="$3"
    got=$("$MSH" -c "$script" 2>&1)
    if [ "$got" = "$want" ]; then
        pass=$((pass+1))
        # echo "PASS: $desc"
    else
        fail=$((fail+1))
        echo "FAIL: $desc"
        echo "  script : $script"
        echo "  want   : [$want]"
        echo "  got    : [$got]"
    fi
}

# assert_rc <desc> <want_rc> <script>
assert_rc() {
    desc="$1"; want="$2"; script="$3"
    "$MSH" -c "$script" >/dev/null 2>&1
    got=$?
    if [ "$got" = "$want" ]; then
        pass=$((pass+1))
    else
        fail=$((fail+1))
        echo "FAIL: $desc (rc want=$want got=$got)"
    fi
}

### 基础命令 ###
assert_output "echo hello" "hello" "echo hello"
assert_output "echo multiple args" "a b c" "echo a b c"
assert_output "echo -n" "no newline" "echo -n no newline"

### 变量展开 ###
assert_output "simple var" "world" "x=world; echo \$x"
assert_output "env var" "/root" "echo \$HOME"
assert_output "positional in func" "hi there" "f() { echo \$1 \$2; }; f hi there"
assert_output "arith" "14" "echo \$((2+3*4))"
assert_output "arith paren" "7" "echo \$(( (2+3)*4 - 13 ))"
assert_output "arith var" "4" "i=4; echo \$((i))"
assert_output "cmdsub" "nested" "echo \$(echo nested)"
assert_output "param default" "dflt" "echo \${UNSET:-dflt}"
assert_output "param assign" "hello" "echo \${X:=hello}"
assert_output "param alt" "alt" "X=set; echo \${X:+alt}"
assert_output "param len" "5" "X=hello; echo \${#X}"
assert_output "param strip prefix" "llo" "X=hello; echo \${X#he}"
assert_output "param strip suffix" "hell" "X=hello; echo \${X%o}"
assert_output "dollar-question" "0" "true; echo \$?"
assert_output "dollar-hash in func" "3" "f() { echo \$#; }; f a b c"
assert_output "dollar-at in func" "a b c" "f() { echo \$@; }; f a b c"

### 控制流 ###
assert_output "if true" "y" "if true; then echo y; fi"
assert_output "if false else" "n" "if false; then echo y; else echo n; fi"
assert_output "if elif" "elif" "if false; then echo a; elif true; then echo elif; fi"
assert_output "if elif chain" "c" "if false; then echo a; elif false; then echo b; elif true; then echo c; fi"
assert_output "nested if" "in" "if true; then if true; then echo in; fi; fi"
assert_output "for loop" "a
b
c" "for i in a b c; do echo \$i; done"
assert_output "while loop" "0
1
2" "i=0; while [ \$i -lt 3 ]; do echo \$i; i=\$((i+1)); done"
assert_output "until loop" "0
1
2" "i=0; until [ \$i -ge 3 ]; do echo \$i; i=\$((i+1)); done"
assert_output "case match" "hit" "case foo in foo) echo hit;; *) echo no;; esac"
assert_output "case wildcard" "wild" "case abc in a*) echo wild;; esac"
assert_output "case default" "other" "case zz in a*) echo wild;; *) echo other;; esac"
assert_output "func call" "hi" "f() { echo hi; }; f"
assert_output "func args" "1 2" "f() { echo \$1 \$2; }; f 1 2"
assert_output "func recursion fib" "5" "fib() { if [ \$1 -lt 2 ]; then echo \$1; else a=\$(fib \$((\$1-1))); b=\$(fib \$((\$1-2))); echo \$((a+b)); fi; }; fib 5"
assert_output "and list" "both" "true && echo both"
assert_output "or list" "or" "false || echo or"
assert_output "semicolon list" "a
b" "echo a; echo b"

### 重定向 ###
assert_output "redirect out" "file content" "echo file content > /tmp/msh_smoke_$$.txt; cat /tmp/msh_smoke_$$.txt; rm -f /tmp/msh_smoke_$$.txt"
assert_output "redirect append" "a
b" "echo a > /tmp/msh_smoke_$$.txt; echo b >> /tmp/msh_smoke_$$.txt; cat /tmp/msh_smoke_$$.txt; rm -f /tmp/msh_smoke_$$.txt"
assert_output "redirect stderr to file" "out" "echo err > /tmp/msh_err_$$.txt; echo out; cat /tmp/msh_err_$$.txt >/dev/null 2>&1; rm -f /tmp/msh_err_$$.txt"
assert_output "redirect in" "read it" "echo read it > /tmp/msh_smoke_in_$$.txt; cat < /tmp/msh_smoke_in_$$.txt; rm -f /tmp/msh_smoke_in_$$.txt"

### 管道 ###
assert_output "pipe" "hello" "echo hello | cat"
assert_output "pipe chain" "3" "echo 1 2 3 | wc -w"
assert_output "pipe grep" "world" "printf 'hello\nworld\n' | grep world"

### 退出码 ###
assert_rc "true rc=0" 0 "true"
assert_rc "false rc=1" 1 "false"
assert_rc "exit rc" 7 "exit 7"
assert_rc "cmd not found" 127 "nonexistent_cmd_xyz"

### 子 shell / 括号 ###
assert_output "subshell" "in" "(echo in)"
assert_output "subshell var isolation" "1" "x=1; (x=2); echo \$x"
assert_output "brace group" "both" "{ echo both; }"

### 注释与多行 ###
assert_output "comment" "" "# just a comment"
assert_output "inline comment" "a b" "echo a b # trailing"
assert_output "multiline if" "y" "if true
then
echo y
fi"

echo ""
echo "=== POSIX smoke: $pass PASS / $fail FAIL ==="
[ "$fail" -eq 0 ]
