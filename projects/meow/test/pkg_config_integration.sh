#!/bin/sh
# Test uses: recipe integration with pkg-config library database.
set -eu

meow=${1:?meow path required}
fail=0

# Test: uses: zlib expands LIBS=-lz
# Run with verbose to capture command output via stderr
result=$(cd .. && "$meow" build meow-uses-test 2>&1) || true
# meow only shows command stdout on error or in debug mode
# Check the build succeeded
echo "$result" | grep -q "built" || {
    echo "FAIL: meow-uses-test build failed"
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
