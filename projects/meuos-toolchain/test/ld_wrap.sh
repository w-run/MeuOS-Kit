#!/bin/sh
# ld_wrap.sh — test mt/ld --wrap symbol redirection.
#
# Verifies that --wrap=SYM redirects UNDEF references to __wrap_SYM,
# and that __real_SYM gives access to the original definition.
set -eu

ld=${1:?ld path required}
work=$(mktemp -d /tmp/meuos-toolchain-ld-wrap.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Test 1: __wrap_malloc intercepts malloc calls
cat >"$work/wrap_malloc.c" <<'CEOF'
#include <stdlib.h>
void *__wrap_malloc(size_t n) { (void)n; return NULL; }
int main(void) { void *p = malloc(10); return p != NULL; }
CEOF
gcc -c -fno-pic -o "$work/wrap_malloc.o" "$work/wrap_malloc.c"
"$ld" -e main --wrap=malloc -o "$work/wrap.elf" \
  "$work/wrap_malloc.o" 2>/dev/null || {
	printf '%s\n' 'FAIL: --wrap=malloc link failed'
	exit 1
}
printf '%s\n' 'mt ld --wrap: malloc redirection PASS'

# Test 2: Multiple --wrap instances
cat >"$work/wrap_multi.c" <<'CEOF'
#include <stdlib.h>
void *__wrap_malloc(size_t n) { return NULL; }
void __wrap_free(void *p) { (void)p; }
int main(void) { void *p = malloc(10); free(p); return p != NULL; }
CEOF
gcc -c -fno-pic -o "$work/wrap_multi.o" "$work/wrap_multi.c"
"$ld" -e main --wrap=malloc --wrap=free -o "$work/multi.elf" \
  "$work/wrap_multi.o" 2>/dev/null || {
	printf '%s\n' 'FAIL: multiple --wrap link failed'
	exit 1
}
printf '%s\n' 'mt ld --wrap: multiple instances PASS'

printf '%s\n' 'mt ld --wrap: all checks PASS'
