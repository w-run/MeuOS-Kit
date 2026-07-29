#!/bin/sh
set -eu

meow=${1:?meow path required}
fail=0

# Test basic --libs query
result=$("$meow" pkg-config --libs zlib 2>/dev/null)
if [ "$result" != "-lz" ]; then
    echo "FAIL: zlib --libs expected '-lz', got '$result'"
    fail=1
fi
echo "PASS: zlib --libs = $result"

# Test multi-flag result
result=$("$meow" pkg-config --libs openssl 2>/dev/null)
if [ "$result" != "-lssl -lcrypto" ]; then
    echo "FAIL: openssl --libs expected '-lssl -lcrypto', got '$result'"
    fail=1
fi
echo "PASS: openssl --libs = $result"

# Test --cflags
result=$("$meow" pkg-config --cflags ncurses 2>/dev/null)
if [ "$result" != "-D_GNU_SOURCE" ]; then
    echo "FAIL: ncurses --cflags expected '-D_GNU_SOURCE', got '$result'"
    fail=1
fi
echo "PASS: ncurses --cflags = $result"

# Test cflags for a library with no cflags
result=$("$meow" pkg-config --cflags zlib 2>/dev/null)
if [ "$result" != "" ]; then
    echo "FAIL: zlib --cflags expected '', got '$result'"
    fail=1
fi
echo "PASS: zlib --cflags = (empty)"

# Test unknown package
result=$("$meow" pkg-config --libs nonexistent 2>&1) && {
    echo "FAIL: nonexistent should have failed"
    fail=1
} || true
echo "PASS: nonexistent correctly rejected"

# Test multiple packages
result=$("$meow" pkg-config --libs zlib bz2 2>/dev/null)
if [ "$result" != "-lz -lbz2" ]; then
    echo "FAIL: multiple --libs expected '-lz -lbz2', got '$result'"
    fail=1
fi
echo "PASS: multiple packages = $result"

# Test mpfr (depends on gmp)
result=$("$meow" pkg-config --libs mpfr 2>/dev/null)
if [ "$result" != "-lmpfr -lgmp" ]; then
    echo "FAIL: mpfr --libs expected '-lmpfr -lgmp', got '$result'"
    fail=1
fi
echo "PASS: mpfr --libs = $result"

if [ "$fail" -ne 0 ]; then
    echo "meow pkg-config: FAILED"
    exit 1
fi
echo "meow pkg-config: all checks PASS"
