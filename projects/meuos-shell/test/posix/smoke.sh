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

### 循环控制流: break/continue/return ###
assert_output "break in while" "0
1" "i=0; while [ \$i -lt 10 ]; do echo \$i; i=\$((i+1)); if [ \$i -ge 2 ]; then break; fi; done"
assert_output "break in for" "a
b" "for x in a b c d e; do if [ \"\$x\" = c ]; then break; fi; echo \$x; done"
assert_output "continue in for" "a
b
d
e" "for x in a b c d e; do if [ \"\$x\" = c ]; then continue; fi; echo \$x; done"
assert_output "continue in while" "1
3" "i=0; while [ \$i -lt 5 ]; do i=\$((i+1)); if [ \$((i%2)) -eq 1 ]; then continue; fi; echo \$((i-1)); done"
assert_output "nested break" "a0
a1
b0
b1" "for x in a b; do for y in 0 1 2; do if [ \"\$y\" = 2 ]; then break; fi; echo \$x\$y; done; done"
assert_output "nested break N" "outer-done" "for x in a b; do for y in 0 1; do break 2; done; done; echo outer-done"
assert_output "return in func" "before
ret" "f() { echo before; return 0; echo after; }; f; echo ret"
assert_output "return value" "42" "f() { return 42; }; f; echo \$?"

### ANSI-C quoting (\$'...') ###
assert_output "ansic basic" "hello" "echo \$'hello'"
assert_output "ansic newline" "a
b" "echo \$'a\\nb'"
assert_output "ansic tab" "a	b" "echo \$'a\\tb'"
assert_output "ansic null in string" "" "echo \$'\\x00x'"
assert_output "ansic hex escape" "A" "echo \$'\\x41'"

### let 内建命令 ###
assert_output "let basic" "15" "let x=15; echo \$x"
assert_output "let arithmetic" "14" "let 'x=2+3*4'; echo \$x"
assert_output "let power" "1024" "let 'x=2**10'; echo \$x"
assert_output "let hex" "255" "let 'x=0xFF'; echo \$x"
assert_output "let ternary" "100" "let 'x=5>3?100:200'; echo \$x"
assert_output "let compound assign" "6" "let 'a=1'; let 'a+=5'; echo \$a"
assert_output "let bitwise" "255" "let 'x=0xF0|0x0F'; echo \$x"
assert_output "let modulo" "1" "let 'x=10%3'; echo \$x"
assert_output "let shift" "16" "let 'x=1<<4'; echo \$x"
assert_output "let multi expr" "13" "let x=5 y=x+3 z=x+y; echo \$z"
assert_rc "let returns 0 if nonzero" 0 "let '1+1'"
assert_rc "let returns 1 if zero" 1 "let '1-1'"

### getopts 内建 ###
assert_rc "getopts no error" 0 "OPTIND=1; getopts ab: opt -a"

### shift 内建 (in function context where positional params work) ###
assert_output "shift basic" "2
3" "f() { shift; echo \$1; echo \$2; }; f 1 2 3"
assert_output "shift by N" "c" "f() { shift 2; echo \$1; }; f a b c"

### alias/unalias ###
assert_output "alias expansion" "aliased" "alias ll='echo aliased'; ll"
assert_output "alias list" "alias ll='echo test'" "alias ll='echo test'; alias ll"

### umask ###
assert_output "umask set" "022" "umask 022; umask"

### declare/typeset/local ###
assert_output "declare basic" "hello" "declare x=hello; echo \$x"
assert_output "typeset basic" "world" "typeset x=world; echo \$x"
assert_output "local basic" "localval" "f() { local x=localval; echo \$x; }; f"

### Shell-Utils 联动内置：printf / test / sleep / seq ###
assert_output "printf %s" "hello" "printf '%s' hello"
assert_output "printf %d" "42" "printf '%d' 42"
assert_output "printf mixed" "Name:A Value:42" "printf '%s:%s %s:%d' Name A Value 42"
assert_rc "test -eq true" 0 "test 1 -eq 1"
assert_rc "test -eq false" 1 "test 1 -eq 2"
assert_rc "test string =" 0 "test 'abc' = 'abc'"
assert_rc "test string !=" 1 "test 'abc' != 'abc'"
assert_rc "test -d /tmp" 0 "test -d /tmp"
assert_rc "test -f exists" 0 "test -f /etc/hostname"
assert_rc "test -e missing" 1 "test -e /nonexistent12345"
assert_rc "[ -eq true ]" 0 "[ 1 -eq 1 ]"
assert_rc "[ -eq false ]" 1 "[ 1 -eq 2 ]"
assert_rc "[ string = ]" 0 "[ 'a' = 'a' ]"
assert_output "seq 3" "1
2
3" "seq 3"
assert_output "seq range" "2
3
4" "seq 2 4"
assert_output "seq step" "2
4
6" "seq 2 2 6"
assert_rc "sleep 0.01" 0 "sleep 0.01"
assert_output "seq countdown" "3
2
1" "seq 3 -1 1"

echo ""
echo "=== POSIX smoke: $pass PASS / $fail FAIL ==="
[ "$fail" -eq 0 ]
