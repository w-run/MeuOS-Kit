#!/bin/bash
# Build dash 0.5.12 entirely with the MeuOS toolchain (mcc + mt/ld + meuos-libc).
# No host gcc/cc is used to compile any dash source file.
# Host tools are only used for unpacking and applying the patch (git/patch).
#
# Usage: build-mcc.sh [SRC_TAR] [OUT_BIN]
set -e

SRC_TAR="${1:-/tmp/dash-build/dash-0.5.12.tar.gz}"
OUT_BIN="${2:-/tmp/dash-pure}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PATCH="$SCRIPT_DIR/dash-0.5.12-mcc.patch"

MCC="${MCC:-/workspace/MeuOS-Kit/.agents/worktrees/mcc-toolchain/projects/mcc/mcc}"

# Resolve the MeuOS sysroot. MEUOS_SYSROOT may point at the arch-farm root
# (e.g. .../sysroot) instead of an arch dir (e.g. .../sysroot/x86_64).
if [ -n "${MEUOS_SYSROOT:-}" ] && [ -d "$MEUOS_SYSROOT/usr/include" ]; then
    SYSROOT="$MEUOS_SYSROOT"
elif [ -n "${MEUOS_SYSROOT:-}" ] && [ -d "$MEUOS_SYSROOT/x86_64/usr/include" ]; then
    SYSROOT="$MEUOS_SYSROOT/x86_64"
else
    SYSROOT="/workspace/MeuOS-Kit/sysroot/x86_64"
fi
echo "== sysroot: $SYSROOT =="

WORK="/tmp/dash-mcc-fresh"
SRC="$WORK/dash-0.5.12"
OBJ="$WORK/obj"

rm -rf "$WORK"
mkdir -p "$OBJ"

echo "== unpack =="
tar xf "$SRC_TAR" -C "$WORK"

echo "== apply patch =="
cd "$SRC"
git apply --whitespace=nowarn "$PATCH"

echo "== generate wrappers =="
cd "$SRC/src"
FILES="alias.c arith_yacc.c arith_yylex.c cd.c error.c eval.c exec.c expand.c
histedit.c input.c jobs.c mail.c main.c memalloc.c miscbltin.c
mystring.c options.c parser.c redir.c show.c trap.c output.c
bltin/printf.c system.c bltin/test.c bltin/times.c var.c
builtins.c init.c nodes.c signames.c syntax.c supplement.c"
for f in $FILES; do
  base=$(basename "$f" .c)
  {
    echo "#include \"../config.h\""
    echo "#include \"$f\""
  } > "_wrap_${base}.c"
done

echo "== compile (mcc) =="
for w in _wrap_*.c; do
  base=${w#_wrap_}
  base=${base%.c}
  echo "  compiling $base"
  "$MCC" --sysroot="$SYSROOT" -I"$SRC/src" -I"$SRC" -I"$SRC/include" \
      -DBSD=1 -DSHELL -c -o "$OBJ/$base.o" "$w"
done

echo "== link (mcc -> mt/ld) =="
"$MCC" --sysroot="$SYSROOT" -o "$OUT_BIN" "$OBJ"/*.o

echo "== done: $OUT_BIN =="
