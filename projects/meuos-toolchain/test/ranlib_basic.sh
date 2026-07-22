#!/bin/sh
# test/ranlib_basic.sh - ranlib generates symbol index usable by host ld.
set -eu
ar=${1:?ar path required}
ranlib=${2:?ranlib path required}
work=$(mktemp -d /tmp/mt-ranlib-test.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

echo 'int ranlib_test_func(void) { return 42; }' > "$work/func.c"
cc -c "$work/func.c" -o "$work/func.o"

# Create archive WITHOUT symbol index (ar r, no s)
"$ar" r "$work/libtest.a" "$work/func.o" 2>/dev/null

# Run ranlib to add symbol index
"$ranlib" "$work/libtest.a" || { echo "mt ranlib: FAIL (ranlib)"; exit 1; }

# Verify host ld can link using the ranlib'd archive
echo 'extern int ranlib_test_func(void); int main(void){return ranlib_test_func()==42?0:1;}' > "$work/main.c"
cc -c "$work/main.c" -o "$work/main.o"
cc "$work/main.o" "$work/libtest.a" -o "$work/main" 2>&1 || {
    echo "mt ranlib: FAIL (host ld link)"; exit 1; }
"$work/main" || { echo "mt ranlib: FAIL (run)"; exit 1; }

echo "mt ranlib: PASS"
