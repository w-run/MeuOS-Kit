#!/bin/sh
# mt_integration.sh - verify MT_AS/MT_LD/MT_AR redirect mcc to the MeuOS
# toolchain (mt/as, mt/ld) instead of the host cc.
#
# Usage: mt_integration.sh <mcc> <mt-build-dir> [sysroot]
#
# The sysroot is resolved in this order: the third argument, the
# MEUOS_SYSROOT environment variable, or a ../sysroot sibling of the mcc
# binary.  The script SKIPs (exit 0) when no sysroot with crt1.o is
# available, because the --specs=meuos link cases require it.
set -eu

MCC="${1:?usage: mt_integration.sh <mcc> <mt-build-dir> [sysroot]}"
MT_DIR="${2:?usage: mt_integration.sh <mcc> <mt-build-dir> [sysroot]}"
SYSROOT="${3:-${MEUOS_SYSROOT:-}}"

export MT_AS="$MT_DIR/bin/as"
export MT_LD="$MT_DIR/bin/ld"
export MT_AR="$MT_DIR/bin/ar"
export LC_ALL=C

# Verify the mt tools exist and are executable.
for t in "$MT_AS" "$MT_LD" "$MT_AR"; do
	if [ ! -x "$t" ]; then
		echo "SKIP: $t not found or not executable"
		exit 0
	fi
done

# Resolve sysroot.
if [ -z "$SYSROOT" ]; then
	MCC_DIR=$(cd "$(dirname "$MCC")" && pwd)
	SYSROOT="$MCC_DIR/../sysroot"
fi
if [ ! -f "$SYSROOT/usr/lib/crt1.o" ]; then
	echo "SKIP: no sysroot with crt1.o at $SYSROOT (build meuos-libc first)"
	exit 0
fi
export MEUOS_SYSROOT="$SYSROOT"

tmp=$(mktemp -d /tmp/mt-integ.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# Test 1: compile-only (-c).  mt/as is used regardless of --specs=meuos.
printf 'int main(void) { return 0; }\n' > "$tmp/hello.c"
"$MCC" --specs=meuos -c -o "$tmp/hello.o" "$tmp/hello.c"
test -f "$tmp/hello.o" || { echo "FAIL: -c did not produce .o"; exit 1; }
file "$tmp/hello.o" | grep -q 'ELF' || { echo "FAIL: .o not ELF"; exit 1; }

# Test 2: full compile+link under --specs=meuos.  mt/as assembles, mt/ld
# links against crt1.o + libc-meuos.a from the sysroot.
"$MCC" --specs=meuos -o "$tmp/hello" "$tmp/hello.c"
"$tmp/hello" || { echo "FAIL: hello exited non-zero"; exit 1; }

# Test 3: link-only path (run_host_link) via .o inputs.
"$MCC" --specs=meuos -c -o "$tmp/part.o" "$tmp/hello.c"
"$MCC" --specs=meuos -o "$tmp/hello_link" "$tmp/part.o"
"$tmp/hello_link" || { echo "FAIL: hello_link exited non-zero"; exit 1; }

# Test 4: verify no host cc is invoked for the --specs=meuos link path.
# strace is optional; the test still PASSes without it.
if command -v strace >/dev/null 2>&1; then
	strace -f -e trace=execve "$MCC" --specs=meuos -o "$tmp/hello2" \
		"$tmp/hello.c" 2>"$tmp/strace.log" >/dev/null || true
	# Any execve of a bare "cc" / "gcc" / "clang" (not mcc, not mt) is a
	# regression: the --specs=meuos link must go through mt/as + mt/ld.
	if grep 'execve' "$tmp/strace.log" 2>/dev/null \
	    | grep -v '/meuos-toolchain/' \
	    | grep -E '"[^"]*/?(cc|gcc|clang)( |")' \
	    | grep -qvE '"/[^"]*mcc"|"/[^"]*mt' ; then
		echo "FAIL: host cc was invoked for --specs=meuos link"
		grep 'execve' "$tmp/strace.log" | grep -E '(cc|gcc|clang)' | head -5
		exit 1
	fi
fi

echo "mt integration: PASS"
