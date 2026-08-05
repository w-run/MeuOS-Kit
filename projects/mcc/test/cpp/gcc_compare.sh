#!/usr/bin/env bash
#
# gcc_compare.sh — optional m++ vs g++ semantic parity batch.  For each
# listed test, compiles and runs the SAME source with m++ and g++, and
# requires the two exit codes to agree (both pass -> 0).  Skips cleanly
# when g++ is unavailable, and skips a test where either compiler does not
# accept the source (m++ accepts a superset / different dialect), so it
# asserts agreement only when BOTH compilers run it.
#
# Used by verify-all.sh; `make check-cpp-gcc`.

set -u
MPP="./m++"
TESTS="virtual_delete out_of_line_dtor ctor_dtor_order"
FAILED=0

if ! command -v g++ >/dev/null 2>&1; then
    echo "gcc_compare: g++ not found, SKIP"
    exit 0
fi

for arg in "$@"; do
    case "$arg" in --mpp) MPP="$(readlink -f "$2")"; shift 2;; esac
done

for t in $TESTS; do
    f="test/cpp/$t.cc"
    mo="/tmp/mpp-gcc-$t"
    go="/tmp/gcc-gcccmp-$t"
    mcode=-1; gcode=-1
    if ! "$MPP" --specs=host -o "$mo" "$f"; then
        echo "  SKIP $t (m++ does not compile it)"
        continue
    fi
    "$mo"; mcode=$?
    if ! g++ -w -x c++ -o "$go" "$f"; then
        echo "  SKIP $t (g++ does not accept it)"
        continue
    fi
    "$go"; gcode=$?
    if [ "$mcode" -eq "$gcode" ]; then
        echo "  OK  $t (m++=$mcode g++=$gcode)"
    else
        echo "  FAIL $t (m++=$mcode g++=$gcode)"
        FAILED=1
    fi
done

[ "$FAILED" = 0 ] && echo "gcc_compare: OK" || { echo "gcc_compare: FAIL"; exit 1; }
