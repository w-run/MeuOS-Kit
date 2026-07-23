#!/bin/sh
# test/readelf_basic.sh - readelf basic acceptance tests (REL + EXEC).
#
# 验证 readelf 的 --version/--help/-h/-l/-S/-s/-r/-d/-x/-a/-W 选项在
# REL（可重定位）和 EXEC（可执行）文件上的基本行为。REL 文件的输出与
# GNU readelf（LC_ALL=C）逐字节一致，这里做 grep 级别的关键内容检查。
set -eu

readelf=${1:?readelf path required}

work=$(mktemp -d /tmp/mt-readelf-test.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# ---- 测试夹具 ----
# REL：简单 .o 文件，含 .text/.data/.bss/.rodata/.symtab/.strtab/.rela.text
cat > "$work/test.c" <<'EOF'
int shared_var = 42;
int uninit_var;
extern int printf(const char *, ...);
static int helper(int x) { return x * 2; }
int main(int argc, char **argv) {
    (void)argv;
    printf("hello %d\n", helper(argc));
    return shared_var - 42;
}
EOF
cc -c -o "$work/test.o" "$work/test.c"
cc -o "$work/test_exec" "$work/test.c"

REL="$work/test.o"
EXEC="$work/test_exec"
export LC_ALL=C

fail=0
check() {
	desc=$1; shift
	if "$@" >/dev/null 2>&1; then
		echo "  ok: $desc"
	else
		echo "  FAIL: $desc"
		fail=1
	fi
}

echo "=== readelf basic acceptance ==="

# ---- --version ----
check "--version prints version string" \
	sh -c '"$0" --version | grep -q "^meuos-toolchain readelf 0\.2\.0 (x86_64 bootstrap)$"' \
	"$readelf"

# ---- --help / -H ----
check "--help prints usage" \
	sh -c '"$0" --help | grep -q "^Usage: readelf"' \
	"$readelf"
check "-H prints usage" \
	sh -c '"$0" -H | grep -q "Display information about the contents of ELF format files"' \
	"$readelf"

# ---- -h (ELF Header) ----
check "REL -h: Type is REL" \
	sh -c '"$0" -h "$1" | grep -Eq "Type:[[:space:]]+REL "' \
	"$readelf" "$REL"
check "REL -h: Machine is X86-64" \
	sh -c '"$0" -h "$1" | grep -q "Advanced Micro Devices X86-64"' \
	"$readelf" "$REL"
check "REL -h: Class is ELF64" \
	sh -c '"$0" -h "$1" | grep -q "ELF64"' \
	"$readelf" "$REL"
check "EXEC -h: Type is EXEC" \
	sh -c '"$0" -h "$1" | grep -Eq "Type:[[:space:]]+EXEC "' \
	"$readelf" "$EXEC"
check "EXEC -h: Entry point nonzero" \
	sh -c '"$0" -h "$1" | grep -Eq "Entry point address:[[:space:]]+0x[1-9]"' \
	"$readelf" "$EXEC"

# ---- -l (Program Headers) ----
check "REL -l: no program headers message" \
	sh -c '"$0" -l "$1" | grep -q "There are no program headers in this file."' \
	"$readelf" "$REL"
check "EXEC -l: has LOAD segment" \
	sh -c '"$0" -l "$1" | grep -q "LOAD"' \
	"$readelf" "$EXEC"
check "EXEC -l: Entry point line" \
	sh -c '"$0" -l "$1" | grep -q "Entry point 0x"' \
	"$readelf" "$EXEC"

# ---- -S (Section Headers) ----
check "REL -S: has .text section" \
	sh -c '"$0" -S "$1" | grep -q "\.text"' \
	"$readelf" "$REL"
check "REL -S: has .symtab section" \
	sh -c '"$0" -S "$1" | grep -q "\.symtab"' \
	"$readelf" "$REL"
check "REL -S: has .strtab section" \
	sh -c '"$0" -S "$1" | grep -q "\.strtab"' \
	"$readelf" "$REL"
check "REL -S: has Key to Flags legend" \
	sh -c '"$0" -S "$1" | grep -q "Key to Flags"' \
	"$readelf" "$REL"

# ---- -s (Symbols) ----
check "REL -s: has main symbol" \
	sh -c '"$0" -s "$1" | grep -Eq "[[:space:]]main$"' \
	"$readelf" "$REL"
check "REL -s: has FUNC type" \
	sh -c '"$0" -s "$1" | grep -q "FUNC"' \
	"$readelf" "$REL"
check "REL -s: has GLOBAL bind" \
	sh -c '"$0" -s "$1" | grep -q "GLOBAL"' \
	"$readelf" "$REL"

# ---- -r (Relocations) ----
check "REL -r: has relocation section header" \
	sh -c '"$0" -r "$1" | grep -q "Relocation section"' \
	"$readelf" "$REL"
check "REL -r: has R_X86_64 reloc type" \
	sh -c '"$0" -r "$1" | grep -q "R_X86_64_"' \
	"$readelf" "$REL"

# ---- -d (Dynamic) ----
check "EXEC -d: has Dynamic section" \
	sh -c '"$0" -d "$1" | grep -q "Dynamic section"' \
	"$readelf" "$EXEC"
check "EXEC -d: has NEEDED entry" \
	sh -c '"$0" -d "$1" | grep -q "NEEDED"' \
	"$readelf" "$EXEC"

# ---- -x (Hex Dump) ----
check "REL -x .text: hex dump header" \
	sh -c '"$0" -x.text "$1" | grep -q "Hex dump of section"' \
	"$readelf" "$REL"
check "REL -x .text: has hex bytes" \
	sh -c '"$0" -x.text "$1" | grep -Eq "0x[0-9a-f]{8} [0-9a-f]{8}"' \
	"$readelf" "$REL"
check "REL -x.text: NOTE about relocations" \
	sh -c '"$0" -x.text "$1" | grep -q "NOTE: This section has relocations"' \
	"$readelf" "$REL"

# ---- -a (All) ----
check "REL -a: no crash, exit 0" \
	sh -c '"$0" -a "$1" >/dev/null' \
	"$readelf" "$REL"
check "EXEC -a: no crash, exit 0" \
	sh -c '"$0" -a "$1" >/dev/null' \
	"$readelf" "$EXEC"

# ---- -W (Wide) ----
check "REL -W -s: no crash" \
	sh -c '"$0" -W -s "$1" >/dev/null' \
	"$readelf" "$REL"

# ---- REL byte-identical to GNU readelf (if available) ----
if command -v readelf >/dev/null 2>&1; then
	echo "=== REL diff against GNU readelf ==="
	for opt in -h -l -S -s -r "-x.text"; do
		"$readelf" $opt "$REL" > "$work/my_out" 2>&1
		readelf $opt "$REL" > "$work/gnu_out" 2>&1
		if diff "$work/my_out" "$work/gnu_out" >/dev/null 2>&1; then
			echo "  ok: REL $opt identical to GNU readelf"
		else
			echo "  FAIL: REL $opt differs from GNU readelf"
			fail=1
		fi
	done
else
	echo "=== GNU readelf not found, skipping diff checks ==="
fi

# ---- Error handling ----
check "nonexistent file: exit nonzero" \
	sh -c '! "$0" -h "$1/nonexistent" >/dev/null 2>&1' \
	"$readelf" "$work"
check "no arguments: exit nonzero" \
	sh -c '! "$0" >/dev/null 2>&1' \
	"$readelf"

if [ "$fail" -eq 0 ]; then
	echo "mt readelf: PASS"
else
	echo "mt readelf: FAIL"
	exit 1
fi
