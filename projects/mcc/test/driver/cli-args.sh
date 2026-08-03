#!/bin/sh
#   -pg             gprof profiling（接受，no-op，不再 unknown option）
#   --verbose       打印驱动执行的每个阶段命令
#   --color[=auto|always|never]  错误输出颜色控制
#   -x <lang>       强制按语言解析（-x c / -x c++）
#   -std=<standard> 定义 __STDC_VERSION__/__cplusplus（语言模式语义化）
#   -Wa,/-Wl,       汇编/链接选项透传
#   -fno-omit-frame-pointer  保留帧指针（关闭 omit-frame-pointer）
#   -Wno-error      取消 -Werror
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mpp=${1:-"$root/m++"}
mcc=${2:-"$root/mcc"}
work=${TMPDIR:-/tmp}/mcc-cli-args.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

# 1. -pg: accepted, no-op (no "unknown option")
printf '%s\n' 'int main(void) { return 0; }' > "$work/t.c"
"$mpp" --specs=host -pg -o "$work/t-pg" "$work/t.c"
"$work/t-pg"

# 2. --verbose prints the executed driver command
"$mpp" --specs=host --verbose -o "$work/t-verbose" "$work/t.c" \
	2> "$work/verbose.log"
grep -Eq '^cc ' "$work/verbose.log"

# 3. --color=always forces ANSI color on stderr diagnostics
printf '%s\n' 'int main(void) { return undeclared; }' > "$work/err.c"
set +e
"$mpp" --specs=host --color=always -o "$work/err" "$work/err.c" \
	2> "$work/color.log"
set -e
# Detect an ANSI ESC sequence portably (dash's /bin/sh does not expand
# bash's $'\x1b' form; awk's \033 escape is POSIX).
if ! LC_ALL=C awk '/\033\[[0-9;]*m/' "$work/color.log" | grep -q .; then
	echo "FAIL: --color=always did not emit ANSI color" >&2
	exit 1
fi

# 4. -x <lang>: force language parsing regardless of suffix
printf '%s\n' 'struct S { int v; S(int x) : v(x) {} }; int main(void) { S s(42); return s.v == 42 ? 0 : 1; }' > "$work/x.cpp"
"$mpp" --specs=host -x c++ -o "$work/x-cpp" "$work/x.cpp"
"$work/x-cpp"
printf '%s\n' 'int main(void) { return 0; }' > "$work/x.cc"
"$mcc" --specs=host -x c -o "$work/x-c" "$work/x.cc"
"$work/x-c"

# 5. -std=: defines the standard version macros (language-mode semantics)
printf '%s\n' '#if defined(__STDC_VERSION__)' \
	'return __STDC_VERSION__ == 199901L ? 0 : 10;' \
	'#else' \
	'return 20;' \
	'#endif' > "$work/std.c"
printf '%s\n' 'int main(void) {' > "$work/std-main.c"
cat "$work/std.c" >> "$work/std-main.c"
printf '%s\n' '}' >> "$work/std-main.c"
"$mpp" --specs=host -std=c99 -o "$work/std-c99" "$work/std-main.c"
"$work/std-c99"
printf '%s\n' '#if defined(__cplusplus)' \
	'return __cplusplus == 201703L ? 0 : 10;' \
	'#else' \
	'return 20;' \
	'#endif' > "$work/stdcpp.c"
printf '%s\n' 'int main(void) {' > "$work/stdcpp-main.c"
cat "$work/stdcpp.c" >> "$work/stdcpp-main.c"
printf '%s\n' '}' >> "$work/stdcpp-main.c"
"$mpp" --specs=host -std=c++17 -o "$work/std-cpp17" "$work/stdcpp-main.c"
"$work/std-cpp17"

# 6. -Wa,/-Wl, passthrough: accepted and forwarded to host toolchain
"$mpp" --specs=host -Wa,--noexecstack -Wl,--build-id -o "$work/t-w" "$work/t.c"
"$work/t-w"

# 7. -fno-omit-frame-pointer: keeps the frame pointer (pushq %rbp)
"$mpp" --specs=host -fno-omit-frame-pointer -S -o "$work/fp.s" "$work/t.c"
grep -Eq 'push(q)?[[:space:]]+%?rbp' "$work/fp.s"

# 8. -Wno-error undoes -Werror
"$mpp" --specs=host -Werror -Wno-error -o "$work/t-we" "$work/t.c"
"$work/t-we"

echo "cli-args: all checks passed"
