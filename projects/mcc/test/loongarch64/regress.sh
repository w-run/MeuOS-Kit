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
pie=${TMPDIR:-/tmp}/mcc-la64-pie.$$.s
trap 'rm -f "$asm" "$varargs" "$vla" "$abi" "$tls" "$loc" "$glob" "$pie" "${asm}.large.c" "${asm}.large"' EXIT HUP INT TERM

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

# PIE (position-independent executable) gate: compiling with -fPIE must
# produce PC-relative addressing for global variables and function calls.
# Assert pcalau12i + %pc_hi20 / %pc_lo12 sequences for both data and
# function symbols.
"$mcc" --target=loongarch64-linux -fPIE -S -o "$pie" "$root/test/loongarch64/pie.c"
grep -Eq 'pcalau12i[[:space:]].*pc_hi20\(global_var\)' "$pie"
grep -Eq 'pcalau12i[[:space:]].*pc_hi20\(get_global\)' "$pie"
! grep -Eq 'pcaddu12i' "$pie"

# FP-constant .rodata gate (rr_fp): LoongArch has no 64-bit FP immediate, so
# every float/double constant must be stashed in .rodata and loaded with
# fld.s/fld.d via its pcalau12i-computed address.  The pre-fix emitter
# materialised it with `li.d $t0, 0x0; movgr2fr.w` from a truncated bit
# pattern, so `double d=1.5; (int)(d*28)` returned 0 instead of 42.  Assert
# the .rodata stash (.quad/.long), the fld load, and the pcalau12i address
# sequence; and forbid the old broken movgr2fr-from-constant materialisation.
fpc=${TMPDIR:-/tmp}/mcc-la64-fpconst.$$.s
trap 'rm -f "$asm" "$varargs" "$vla" "$abi" "$tls" "$loc" "$glob" "$pie" "$fpc" "${asm}.large.c" "${asm}.large"' EXIT HUP INT TERM
"$mcc" --target=loongarch64-linux -S -o "$fpc" "$root/test/loongarch64/fp_const.c"
grep -Eq '^[.]section[[:space:]]+[.]rodata' "$fpc"
# 1.5 (double) = 4609434218613702656 ; 2.25f (float) = 1073741824
grep -Eq '\.quad[[:space:]]+4609434218613702656' "$fpc"
grep -Eq '\.long[[:space:]]+1073741824' "$fpc"
grep -Eq 'fld[.][ds][[:space:]]+\$f[0-9]+, \$t0, 0' "$fpc"
grep -Eq 'pcalau12i[[:space:]]+\$t0, %pc_hi20\([.]L' "$fpc"
# the old broken immediate + movgr2fr-from-gpr materialisation must not occur
! grep -Eq 'movgr2fr[.][wd][[:space:]]' "$fpc"
printf '%s\n' 'LoongArch64 FP-constant .rodata gate passed'

printf '%s\n' 'LoongArch64 regression checks passed'
