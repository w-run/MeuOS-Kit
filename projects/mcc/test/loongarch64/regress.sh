set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
asm=${TMPDIR:-/tmp}/mcc-la64-regress.$$.s
varargs=${TMPDIR:-/tmp}/mcc-la64-varargs.$$.s
vla=${TMPDIR:-/tmp}/mcc-la64-vla.$$.s
abi=${TMPDIR:-/tmp}/mcc-la64-abi.$$.s
tls=${TMPDIR:-/tmp}/mcc-la64-tls.$$.s
loc=${TMPDIR:-/tmp}/mcc-la64-localvar.$$.s
glob=${TMPDIR:-/tmp}/mcc-la64-globaladdr.$$.s
fpc=${TMPDIR:-/tmp}/mcc-la64-fpconst.$$.s
trap 'rm -f "$asm" "$varargs" "$vla" "$abi" "$tls" "$loc" "$glob" "$fpc" "${asm}.large.c" "${asm}.large"' EXIT HUP INT TERM

"$mcc" --target=loongarch64-linux -S -o "$asm" "$root/test/loongarch64/regress.c"
"$mcc" --target=loongarch64-linux -S -o "$varargs" "$root/test/loongarch64/varargs.c"
"$mcc" --target=loongarch64-linux -S -o "$vla" "$root/test/loongarch64/vla.c"
"$mcc" --target=loongarch64-linux -S -o "$abi" "$root/test/loongarch64/abi.c"
"$mcc" --target=loongarch64-linux -S -o "$tls" "$root/test/loongarch64/tls.c"

cat >"${asm}.large.c" <<'EOF'
int f(void) { char pad[4096]; return pad[0]; }
EOF
"$mcc" --target=loongarch64-linux -S -o "${asm}.large" "${asm}.large.c"
# MIR-native backend (full coverage since #141): operands are GNU as
# tab-separated, and the frame layout saves ra/fp at framesize-dependent
# offsets.  Accept both separator styles; match semantics not layout.
grep -Eq 'li[.]d[[:space:]].*-[2-9][0-9]{3}|li[.]d[[:space:]].*-[1-9][0-9]{4}' "${asm}.large"
grep -Eq 'add[.]d[[:space:]].*\$fp' "${asm}.large"
grep -Fq '{ Ostoreb, Ki, "st.b %0, %M1" }' "$root/src/target/loongarch64/loongarch64_emit.c"
grep -Fq '{ Ostoreh, Ki, "st.h %0, %M1" }' "$root/src/target/loongarch64/loongarch64_emit.c"
grep -Fq '{ Ostorew, Ki, "st.w %0, %M1" }' "$root/src/target/loongarch64/loongarch64_emit.c"
# regress.c: scalar, frame-pointer prologue + no s9 usage
grep -Eq 'st[.]d[[:space:]]\$fp, \$sp' "$asm"
! grep -Eq '(^|[^[:alnum:]_])s9([^[:alnum:]_]|$)' "$asm"
# varargs.c (MIR-native): two functions each save/restore fp; va_list is a
# single pointer advanced by 8 (va_save area + stack continuation)
grep -Eq 'addi[.]d[[:space:]]\$fp, \$sp' "$varargs"
grep -Eq '\$a1' "$varargs"
grep -Eq 'ld[.]d[[:space:]]\$ra' "$varargs"
[ "$(grep -Ec 'st[.]d[[:space:]]\$fp, \$sp' "$varargs")" -eq 2 ]
[ "$(grep -Ec 'ld[.]d[[:space:]]\$fp, \$sp' "$varargs")" -eq 2 ]
# vla.c: dynamic alloca adjusts sp at runtime
grep -Eq 'sub[.]d[[:space:]]\$sp' "$vla"
! grep -Eq 'addi[.]d[[:space:]]\$sp, \$fp, -[2-9][0-9]{3}' "$vla"
# abi.c: aggregate ABI — calls materialized via pcaddu12i + jirl
grep -Eq 'pc_hi20\(sum8\)|bl[[:space:]]sum8' "$abi"
grep -Eq 'pc_hi20\(pair_add\)|bl[[:space:]]pair_add' "$abi"
grep -Eq 'pc_hi20\(big_id\)|bl[[:space:]]big_id' "$abi"
# tls.c: local-exec TLS address sequence
grep -Eq '%le_hi20\(tls_counter\)' "$tls"
grep -Eq '%le_lo12\(tls_counter\)' "$tls"
grep -Eq '%le64_lo20\(tls_counter\)' "$tls"
grep -Eq '%le64_hi12\(tls_counter\)' "$tls"
grep -Eq 'add[.]d[[:space:]].*\$tp' "$tls"

# Function-entry CFG gate: each function must branch from entry to its MIR
# start block (`.L<fn>.bb0`) rather than fall through to the first emitted
# body block.  A function with locals reaches its stack-base setup only via
# that start block; without the entry branch it dereferences garbage `a0`
# (rr_struct local-access segfault).  use_locals materializes the base with
# an `addi.d`/`or $a0` on `$fp`, so assert the entry `b .Lsuffix.bb0` for a
# locals-bearing function and that the start block computes a fp-relative
# local address before branching to the body.
"$mcc" --target=loongarch64-linux -S -o "$loc" "$root/test/loongarch64/localvar.c"
grep -Eq 'b[[:space:]]+\.Luse_locals\.bb0' "$loc"
grep -Eq '^\.Luse_locals\.bb0:' "$loc"
grep -Eq '\$fp' "$loc"
# the locals-bearing body must access locals through the stack pointer
# (a0 = fp-relative address), not a reg that the prologue never set
grep -Eq 'addi[.]d[[:space:]]+\$fp' "$loc"
grep -Eq 'or[[:space:]]+\$a0' "$loc"

# Global/function-address gate: the address sequence must use `pcalau12i`
# (page-masked PC base) paired with `%pc_hi20`, NOT `pcaddu12i` (full PC).
# mt/ld's PCALA_HI20(71) is page-relative and PCALA_LO12(72) adds the
# absolute low 12; pcalau12i's page masking makes the pair reconstruct the
# true address, while pcaddu12i produced wrong addresses (rr_global return 0,
# rr_call deadlock).  Assert the mnemonic on both a global var and a
# function call target.
"$mcc" --target=loongarch64-linux -S -o "$glob" "$root/test/loongarch64/global_addr.c"
grep -Eq 'pcalau12i[[:space:]].*pc_hi20\(g\)' "$glob"
grep -Eq 'pcalau12i[[:space:]].*pc_hi20\(add2\)' "$glob"
! grep -Eq 'pcaddu12i' "$glob"

# FP-constant gate: floating-point constants must be stashed in .rodata and
# loaded with fld.s/fld.d.  They cannot be materialized as integer
# immediates — a double bit pattern is 64 bits and li.d only encodes 32 —
# and reading just the low half turned 1.5/28.0 into integer 0 (rr_fp
# returned 0 instead of 42).  Assert the pool and the loads, per function.
"$mcc" --target=loongarch64-linux -S -o "$fpc" "$root/test/loongarch64/fpconst.c"
grep -Eq '^\.section[[:space:]]+\.rodata' "$fpc"
# double 1.5 and 28.0 keep their full IEEE patterns as distinct pool entries
grep -Eq '^\.Ldmul\.lc0:' "$fpc"
grep -Eq '^\.Ldmul\.lc1:' "$fpc"
grep -Eq '[[:space:]]\.quad[[:space:]]+4609434218613702656' "$fpc"
grep -Eq '[[:space:]]\.quad[[:space:]]+4628574517030027264' "$fpc"
grep -Eq 'fld\.d[[:space:]]+\$f[0-9]+, \$t0, 0' "$fpc"
# float 2.5f/4.0f use the 4-byte pool form and fld.s
grep -Eq '^\.Lfmul\.lc0:' "$fpc"
grep -Eq '[[:space:]]\.long[[:space:]]+1075838976' "$fpc"
grep -Eq '[[:space:]]\.long[[:space:]]+1082130432' "$fpc"
grep -Eq 'fld\.s[[:space:]]+\$f[0-9]+, \$t0, 0' "$fpc"
# the pool address is PC-relative (pcalau12i/%pc_lo12), and no FP constant
# is routed through the integer scratch (the old movgr2fr materialization)
grep -Eq 'pcalau12i[[:space:]].*%pc_hi20\(\.Ldmul\.lc0\)' "$fpc"
! grep -Eq 'movgr2fr' "$fpc"

printf '%s\n' 'LoongArch64 regression checks passed'
