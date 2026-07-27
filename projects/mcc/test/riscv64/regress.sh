set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
asm=${TMPDIR:-/tmp}/mcc-rv64-regress.$$.s
varargs=${TMPDIR:-/tmp}/mcc-rv64-varargs.$$.s
vla=${TMPDIR:-/tmp}/mcc-rv64-vla.$$.s
abi=${TMPDIR:-/tmp}/mcc-rv64-abi.$$.s
tls=${TMPDIR:-/tmp}/mcc-rv64-tls.$$.s
trap 'rm -f "$asm" "$varargs" "$vla" "$abi" "$tls" "${asm}.large.c" "${asm}.large"' EXIT HUP INT TERM

"$mcc" --target=riscv64-linux -S -o "$asm" "$root/test/riscv64/regress.c"
"$mcc" --target=riscv64-linux -S -o "$varargs" "$root/test/riscv64/varargs.c"
"$mcc" --target=riscv64-linux -S -o "$vla" "$root/test/riscv64/vla.c"
"$mcc" --target=riscv64-linux -S -o "$abi" "$root/test/riscv64/abi.c"
"$mcc" --target=riscv64-linux -S -o "$tls" "$root/test/riscv64/tls.c"

cat >"${asm}.large.c" <<'EOF'
int f(void) { char pad[4096]; return pad[0]; }
EOF
"$mcc" --target=riscv64-linux -S -o "${asm}.large" "${asm}.large.c"
grep -Eq 'li t6, [4-9][0-9]{3}' "${asm}.large"
grep -Eq 'add fp, sp, -16' "${asm}.large"

grep -Fq '{ Ostoreb, Kw, "sb %0, %M1" }' "$root/src/target/riscv64/riscv64_emit.c"
grep -Fq '{ Ostoreh, Kw, "sh %0, %M1" }' "$root/src/target/riscv64/riscv64_emit.c"
grep -Fq '{ Ostorew, Kw, "sw %0, %M1" }' "$root/src/target/riscv64/riscv64_emit.c"
grep -Eq 'sd fp, -16\(sp\)' "$asm"
grep -Eq 'add fp, sp, -16' "$varargs"
grep -Eq 'sd a1, .*\(sp\)' "$varargs"
grep -Eq 'add sp, fp, [0-9]+' "$varargs"
grep -Eq 'ld ra, 8\(fp\)' "$varargs"
grep -Eq 'ld fp, 0\(fp\)' "$varargs"
[ "$(grep -Ec 'sd fp, -16\(sp\)' "$varargs")" -eq 2 ]
[ "$(grep -Ec 'ld fp, 0\(fp\)' "$varargs")" -eq 2 ]
! grep -Eq '(^|[^[:alnum:]_])s11([^[:alnum:]_]|$)' "$asm"
grep -Eq 'sub sp, fp, t6' "$vla"
! grep -Eq 'add.*sp, fp, -[2-9][0-9]{3}' "$vla"
grep -Eq '\bcall sum8' "$abi"
grep -Eq '\bcall pair_add' "$abi"
grep -Eq '\bcall big_id' "$abi"
grep -Eq '%tprel_hi\(tls_counter\)' "$tls"
grep -Eq '%tprel_lo\(tls_counter\)' "$tls"
grep -Eq '%tprel_add\(tls_counter\)' "$tls"
grep -Eq 'add.*tp, %tprel_add\(tls_counter\)' "$tls"

printf '%s\n' 'RISC-V 64 regression checks passed'
