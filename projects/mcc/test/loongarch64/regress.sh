set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
asm=${TMPDIR:-/tmp}/mcc-la64-regress.$$.s
varargs=${TMPDIR:-/tmp}/mcc-la64-varargs.$$.s
vla=${TMPDIR:-/tmp}/mcc-la64-vla.$$.s
abi=${TMPDIR:-/tmp}/mcc-la64-abi.$$.s
tls=${TMPDIR:-/tmp}/mcc-la64-tls.$$.s
trap 'rm -f "$asm" "$varargs" "$vla" "$abi" "$tls" "${asm}.large.c" "${asm}.large"' EXIT HUP INT TERM

"$mcc" --target=loongarch64-linux -S -o "$asm" "$root/test/loongarch64/regress.c"
"$mcc" --target=loongarch64-linux -S -o "$varargs" "$root/test/loongarch64/varargs.c"
"$mcc" --target=loongarch64-linux -S -o "$vla" "$root/test/loongarch64/vla.c"
"$mcc" --target=loongarch64-linux -S -o "$abi" "$root/test/loongarch64/abi.c"
"$mcc" --target=loongarch64-linux -S -o "$tls" "$root/test/loongarch64/tls.c"

cat >"${asm}.large.c" <<'EOF'
int f(void) { char pad[4096]; return pad[0]; }
EOF
"$mcc" --target=loongarch64-linux -S -o "${asm}.large" "${asm}.large.c"
grep -Eq 'li\.d .*-[2-9][0-9]{3}|li\.d .*-[1-9][0-9]{4}' "${asm}.large"
grep -Eq 'add\.d .*\$fp' "${asm}.large"

grep -Fq '{ Ostoreb, Ki, "st.b %0, %M1" }' "$root/src/target/loongarch64/loongarch64_emit.c"
grep -Fq '{ Ostoreh, Ki, "st.h %0, %M1" }' "$root/src/target/loongarch64/loongarch64_emit.c"
grep -Fq '{ Ostorew, Ki, "st.w %0, %M1" }' "$root/src/target/loongarch64/loongarch64_emit.c"
grep -Eq 'st\.d \$fp, \$sp, 0' "$asm"
grep -Eq 'addi\.d \$fp, \$sp, 0' "$varargs"
grep -Eq 'st\.d \$a1, \$sp, 8' "$varargs"
grep -Eq 'addi\.d [^,]+, \$fp, 24' "$varargs"
grep -Eq 'addi\.d [^,]+, \$fp, 80' "$varargs"
grep -Eq 'ld\.d \$ra, \$sp, 8' "$varargs"
grep -Eq 'ld\.d \$fp, \$sp, 0' "$varargs"
[ "$(grep -Ec 'st\.d \$fp, \$sp, 0' "$varargs")" -eq 2 ]
[ "$(grep -Ec 'ld\.d \$fp, \$sp, 0' "$varargs")" -eq 2 ]
! grep -Eq '(^|[^[:alnum:]_])s9([^[:alnum:]_]|$)' "$asm"
grep -Eq 'sub\.d \$sp, \$fp, \$t8' "$vla"
! grep -Eq 'addi\.d \$sp, \$fp, -[2-9][0-9]{3}' "$vla"
grep -Eq '\bbl sum8' "$abi"
grep -Eq '\bbl pair_add' "$abi"
grep -Eq '\bbl big_id' "$abi"
grep -Eq '%le_hi20\(tls_counter\)' "$tls"
grep -Eq '%le_lo12\(tls_counter\)' "$tls"
grep -Eq '%le64_lo20\(tls_counter\)' "$tls"
grep -Eq '%le64_hi12\(tls_counter\)' "$tls"
grep -Eq 'add\.d .*\$tp' "$tls"

printf '%s\n' 'LoongArch64 regression checks passed'
