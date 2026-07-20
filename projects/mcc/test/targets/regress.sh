set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
aarch=${TMPDIR:-/tmp}/mcc-aarch64-regress.$$.s
riscv=${TMPDIR:-/tmp}/mcc-riscv64-regress.$$.s
trap 'rm -f "$aarch" "$riscv"' EXIT HUP INT TERM

"$mcc" --target=aarch64-linux -S -o "$aarch" "$root/test/targets/int_abi.c"
grep -Eq 'stp[[:space:]]+x29, x30' "$aarch"
grep -Eq 'bl[[:space:]]+pair' "$aarch"
grep -Eq 'add[[:space:]]+x0' "$aarch"

"$mcc" --target=riscv64-linux -S -o "$riscv" "$root/test/targets/int_abi.c"
grep -Eq 'sd[[:space:]]+ra' "$riscv"
grep -Eq 'call[[:space:]]+pair' "$riscv"
grep -Eq 'add[[:space:]]+a0|add[[:space:]]+t0' "$riscv"
printf '%s\n' 'aarch64/riscv64 integer ABI regression checks passed'
