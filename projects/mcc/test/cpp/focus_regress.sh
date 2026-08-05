#!/usr/bin/env bash
#
# focus_regress.sh — m++ focused regression batch for the recent closed
# clusters (new/delete, braced-init, destructor chains, virtual / pure
# virtual dispatch).  Each semantic domain is a self-contained PASS/FAIL
# group so a regression pinpoints the area; used by verify-all.sh and
# `make check-cpp-focus`.
#
# Usage: sh test/cpp/focus_regress.sh [--mpp PATH]
#
# Exits 0 iff every domain passes.  Each test/*.cc is a freestanding user
# program compiled with m++ --specs=host (no meuos-libc dependency).

set -u
MPP="./m++"
DOMAIN_OK=1
for arg in "$@"; do
    case "$arg" in
        --mpp) MPP="$(readlink -f "$2")"; shift 2;;
    esac
done

# domain <name> <file...>  — compile+run every listed test, one failure ends it
domain() {
    local name="$1"; shift
    local ok=1 f out
    for f in "$@"; do
        out="/tmp/mpp-focus-$(basename "$f" .cc)"
        if ! "$MPP" --specs=host -o "$out" "$f"; then
            echo "  FAIL(compile) $f"; ok=0; break
        fi
        if ! "$out"; then
            echo "  FAIL(run) $f"; ok=0; break
        fi
    done
    if [ "$ok" = 1 ]; then
        echo "  OK  $name"
    else
        DOMAIN_OK=0
    fi
}

echo "== m++ focus regress (new/delete, braced, dtor, virtual) =="

# new / delete cluster
domain "new/delete" \
    test/cpp/new_delete.cc \
    test/cpp/new_delete_array.cc \
    test/cpp/new_delete_boundary.cc \
    test/cpp/placement_new.cc \
    test/cpp/local_class_new.cc

# braced-init cluster (scalar + class arrays)
domain "braced-init" \
    test/cpp/new_braced.cc \
    test/cpp/braced_init_array.cc

# destructor chain cluster (ctor/dtor order, out-of-line, global)
domain "dtor" \
    test/cpp/ctor_dtor_order.cc \
    test/cpp/ctor_base_dtor.cc \
    test/cpp/out_of_line_dtor.cc \
    test/cpp/global_dtor.cc

# virtual / pure-virtual cluster (multi-arch safe: no sysroot needed)
domain "virtual" \
    test/cpp/pure_virtual.cc \
    test/cpp/virtual_delete.cc

echo
[ "$DOMAIN_OK" = 1 ] && echo "focus regress: ALL OK" || { echo "focus regress: FAIL"; exit 1; }
