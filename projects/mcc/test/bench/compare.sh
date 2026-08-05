#!/bin/bash
# compare.sh — mcc (MIR-native) vs gcc benchmark comparison.
# Usage: compare.sh [mcc-binary] [runs]   (default runs=3, median runtime)
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
MCC=${1:-"$root/mcc"}
RUNS=${2:-3}
BENCH="intloop fp_mat structs strings sortbench recur"

rtime() {  # rtime <cmd>: wall seconds
  local s e
  s=$(date +%s.%N)
  "$@" >/dev/null 2>&1
  e=$(date +%s.%N)
  awk "BEGIN{printf \"%.3f\", $e-$s}"
}

printf "%-10s %-4s %-9s %-9s %-6s %-9s %-9s %-6s\n" \
  bench opt mcc_instr gcc_instr instr_ratio mcc_time gcc_time time_ratio

for f in $BENCH; do
  for opt in O2 O3; do
    $MCC --specs=host -$opt -o mcc_${f}_$opt $f.c 2>/dev/null
    gcc -$opt -o gcc_${f}_$opt $f.c 2>/dev/null
    mi=$(objdump -d mcc_${f}_$opt 2>/dev/null | awk '/^[[:space:]]+[0-9a-f]+:/{c++} END{print c+0}')
    gi=$(objdump -d gcc_${f}_$opt 2>/dev/null | awk '/^[[:space:]]+[0-9a-f]+:/{c++} END{print c+0}')
    iratio=$(awk "BEGIN{printf \"%.1f\", $mi/$gi}")
    mts=(); gts=()
    for r in $(seq 1 $RUNS); do
      mts+=("$(rtime ./mcc_${f}_$opt)")
      gts+=("$(rtime ./gcc_${f}_$opt)")
    done
    mt=$(printf '%s\n' "${mts[@]}" | sort -n | sed -n "$(( (RUNS+1)/2 ))p")
    gt=$(printf '%s\n' "${gts[@]}" | sort -n | sed -n "$(( (RUNS+1)/2 ))p")
    tratio=$(awk "BEGIN{printf \"%.1f\", $mt/$gt}")
    printf "%-10s %-4s %-9s %-9s %-6s %-9s %-9s %-6s\n" \
      $f $opt $mi $gi ${iratio}x $mt $gt ${tratio}x
  done
done
