#!/bin/bash
# Run the chibicc community functional test suite against mcc.
#
# chibicc (https://github.com/rui314/chibicc, MIT) is a mature, widely-used
# C compiler test suite. Its functional tests are written in a freestanding
# style: test.h forward-declares assert/printf/etc. and links a helper
# (chibicc's own testutil.c); here we supply assert_adapt.c instead.
#
# Each test is compiled with mcc against the MeuOS sysroot (meuos-libc) and
# executed. Exit code 0 == PASS (assert() helpers abort on mismatch).
#
# Result classes:
#   PASS        compiled + ran, exit 0
#   RUNFAIL     compiled but exited non-zero
#   COMPILEFAIL compile error
set -u
# Derive ROOT from this script's location so it works in any worktree.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
MCC="$ROOT/projects/mcc/mcc"
# Sysroot layout is multiarch (sysroot/<arch>); prefer the explicit env var
# (MEUOS_SYSROOT=/path/to/sysroot/<arch>) and fall back to the repo default.
if [ -n "${MEUOS_SYSROOT:-}" ] && [ -d "$MEUOS_SYSROOT/usr/include" ]; then
  SYS="$MEUOS_SYSROOT"
elif [ -d "$ROOT/sysroot/x86_64" ]; then
  SYS="$ROOT/sysroot/x86_64"
else
  SYS="$ROOT/sysroot"
fi
DIR="$SCRIPT_DIR"
ADAPT="$DIR/assert_adapt.c"
LOG="$DIR/results.log"

: > "$LOG"
PASS=0; RF=0; CF=0
for t in "$DIR"/*.c; do
  [ "$(basename "$t")" = "assert_adapt.c" ] && continue
  name=$(basename "$t" .c)
  out=$(mktemp /tmp/chibicc.XXXXXX)
  cerr=$(mktemp /tmp/chibicc.XXXXXX)
  if "$MCC" --specs=meuos --sysroot="$SYS" -I"$DIR" -o "$out" "$t" "$ADAPT" >"$cerr" 2>&1; then
    if "$out" >/dev/null 2>&1; then
      echo "PASS        $name" | tee -a "$LOG"
      PASS=$((PASS+1))
    else
      rc=$?; echo "RUNFAIL     $name (rc=$rc)" | tee -a "$LOG"; RF=$((RF+1))
    fi
  else
    echo "COMPILEFAIL $name" | tee -a "$LOG"
    sed 's/^/    /' "$cerr" >>"$LOG"
    CF=$((CF+1))
  fi
  rm -f "$out" "$cerr"
done
echo "==== SUMMARY: PASS=$PASS RUNFAIL=$RF COMPILEFAIL=$CF (total $((PASS+RF+CF))) ====" | tee -a "$LOG"
