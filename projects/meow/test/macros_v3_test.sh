#!/bin/sh
set -eu

meow=${1:?meow path required}
fail=0

rm -f /tmp/meow-v3-pre.txt /tmp/meow-v3-main.txt /tmp/meow-v3-post.txt

# Run from repo root so pkgs/ is found
cd /workspace/MeuOS-Kit
"$meow" build meow-macros-v3 2>&1 || true

test -f /tmp/meow-v3-pre.txt  || { echo "FAIL: pre not created"; fail=1; }
test -f /tmp/meow-v3-main.txt || { echo "FAIL: main not created"; fail=1; }
test -f /tmp/meow-v3-post.txt || { echo "FAIL: post not created"; fail=1; }

"$meow" clean meow-macros-v3 >/dev/null 2>&1 || true

if [ "$fail" -eq 0 ]; then
    echo "meow macros v3: all checks PASS"
else
    exit 1
fi
