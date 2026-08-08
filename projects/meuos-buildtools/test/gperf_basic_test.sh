#!/bin/sh
# gperf_basic_test.sh — Test gperf perfect hash generator
set -eu

b="${1:?buildtools path required}"

pass=0; total=0

total=$((total+1)); "$b" gperf --help >/dev/null 2>&1 && echo "PASS: --help" && pass=$((pass+1)) || echo "FAIL: --help"
total=$((total+1)); "$b" gperf --version >/dev/null 2>&1 && echo "PASS: --version" && pass=$((pass+1)) || echo "FAIL: --version"

# Generate perfect hash; verify it has expected output
total=$((total+1))
printf '%%\nalpha\nbeta\ngamma\ndelta\n' > /tmp/gperf_in.txt
"$b" gperf < /tmp/gperf_in.txt 2>/dev/null > /tmp/gperf_out.c
if grep -q 'TOTAL_KEYWORDS 4' /tmp/gperf_out.c; then echo "PASS: 4 keywords" && pass=$((pass+1)); else echo "FAIL: 4 keywords"; fi

total=$((total+1))
cc -O2 -std=c11 -Wall -Wextra -Werror -x c -c -o /tmp/gperf_out.o /tmp/gperf_out.c 2>&1 && echo "PASS: compiles" && pass=$((pass+1)) || echo "FAIL: compiles"

total=$((total+1))
cat > /tmp/gperf_run.c << 'HEREDOC'
#include <stdio.h>
HEREDOC
"$b" gperf < /tmp/gperf_in.txt 2>/dev/null >> /tmp/gperf_run.c
printf 'int main(void) {\n  if(!in_word_set("alpha",5)){printf("FAIL\\n");return 1;}\n  if(!in_word_set("beta",4)){printf("FAIL\\n");return 1;}\n  if(in_word_set("unknown",7)){printf("FAIL\\n");return 1;}\n  printf("PASS\\n"); return 0; }\n' >> /tmp/gperf_run.c
cc -O2 -std=c11 -Wall -Wextra -Werror -o /tmp/gperf_run /tmp/gperf_run.c 2>&1
result=$(/tmp/gperf_run) && [ "$result" = "PASS" ] && echo "PASS: lookup works" && pass=$((pass+1)) || echo "FAIL: lookup works"

rm -f /tmp/gperf_in.txt /tmp/gperf_out.c /tmp/gperf_out.o /tmp/gperf_run.c /tmp/gperf_run
echo "[ $pass/$total ]"
[ "$pass" -eq "$total" ] || exit 1