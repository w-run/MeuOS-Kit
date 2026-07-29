#!/bin/sh
# Test uses: recipe integration with pkg-config library database.
set -eu

meow=${1:?meow path required}
fail=0

# Test: uses: zlib expands LIBS=-lz
result=$(cd .. && "$meow" --verbose build meow-uses-test 2>&1) || true
echo "$result" | grep -q "LIBS=-lz" || {
    echo "FAIL: uses: zlib did not expand LIBS=-lz"
    echo "got: $result"
    fail=1
}
echo "$result" | grep -q "PKG_ZLIB_LIBS=-lz" || {
    echo "FAIL: uses: zlib did not export PKG_ZLIB_LIBS=-lz"
    echo "got: $result"
    fail=1
}

cd .. && "$meow" clean meow-uses-test >/dev/null 2>&1 || true

if [ "$fail" -eq 0 ]; then
    echo "meow pkg-config integration: all checks PASS"
else
    echo "meow pkg-config integration: FAILED"
    exit 1
fi
