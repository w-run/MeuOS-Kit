#!/bin/sh
# flex_basic_test.sh — Test flex lexer generator
set -eu

buildtools="${1:?buildtools path required}"
inputs="$(dirname "$0")/inputs"

pass=0
total=0

# 1: --help
total=$((total+1)) && "$buildtools" flex --help >/dev/null 2>&1 \
    && echo "PASS: --help" && pass=$((pass+1)) \
    || echo "FAIL: --help"

# 2: --version (check help text mentions flex)
total=$((total+1)) && "$buildtools" flex --help 2>&1 | grep -q 'flex' \
    && echo "PASS: help has flex" && pass=$((pass+1)) \
    || echo "FAIL: help has flex"

# 3: Generate lexer from numwords.l
total=$((total+1)) && \
    "$buildtools" flex "$inputs/numwords.l" 2>/dev/null > /tmp/flex_test.c \
    && echo "PASS: numwords.l generates lexer" && pass=$((pass+1)) \
    || echo "FAIL: numwords.l generates lexer"

# 4: Generated lexer compiles
total=$((total+1)) && \
    cc -O2 -std=c11 -Wall -Wextra -Werror -o /tmp/flex_test_bin /tmp/flex_test.c 2>&1 \
    && echo "PASS: generated lexer compiles" && pass=$((pass+1)) \
    || echo "FAIL: generated lexer compiles"

# 5: Generated lexer works on numbers
total=$((total+1)) && \
    result=$(printf "42 99" | /tmp/flex_test_bin 2>&1) \
    && echo "$result" | grep -q 'NUM:42.*NUM:99' \
    && echo "PASS: lexer matches numbers" && pass=$((pass+1)) \
    || echo "FAIL: lexer matches numbers (got: $result)"

# 6: Generated lexer works on words
total=$((total+1)) && \
    result=$(printf "hello world" | /tmp/flex_test_bin 2>&1) \
    && echo "$result" | grep -q 'WORD:hello.*WORD:world' \
    && echo "PASS: lexer matches words" && pass=$((pass+1)) \
    || echo "FAIL: lexer matches words (got: $result)"

# 7: Generated lexer defaults to char echo
total=$((total+1)) && \
    result=$(printf "!" | /tmp/flex_test_bin 2>&1) \
    && echo "$result" | grep -q 'CH:!' \
    && echo "PASS: lexer fallback to char" && pass=$((pass+1)) \
    || echo "FAIL: lexer fallback to char (got: $result)"

rm -f /tmp/flex_test.c /tmp/flex_test_bin

echo
if [ "$pass" -eq "$total" ]; then
    echo "=== ALL $total FLEX TESTS PASSED ==="
else
    echo "=== $pass/$total TESTS PASSED ==="
    exit 1
fi