#!/bin/sh
set -eu
meow=${1:?meow path required}
fail=0

# Clean first
cd .. && "$meow" clean meow-macros >/dev/null 2>&1 || true

# Build - should succeed despite `run(?): false`
cd .. && "$meow" build meow-macros 2>&1 || {
    echo "FAIL: meow-macros build should succeed"
    fail=1
}

# Verify continue-after-false file exists
test -f /tmp/meow-macro-test.txt || {
    echo "FAIL: run(?) did not continue after false"
    fail=1
}

if [ "$fail" -eq 0 ]; then
    echo "meow macros: all checks PASS"
else
    echo "meow macros: FAILED"
    exit 1
fi
