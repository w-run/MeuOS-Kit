#!/bin/sh
# x86_64 TLS relocation parsing: mt/as must turn @tlsgd / @tlsld / @dtpoff /
# @gottpoff modifiers into the correct ELF relocation types, and mt/ld must
# allocate the GD/LD DTPMOD|DTPOFF GOT pair (shared/PIE) while relaxing GD/LD
# to Local-Exec (no GOT) in a static executable.
set -eu

as=${1:?as path required}
ld=${2:?ld path required}
readelf=${3:?readelf path required}
work=$(mktemp -d /tmp/mt-as-tls.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail=0

# --- mt/as relocation parsing -------------------------------------------
cat >"$work/tls.s" <<'ASM'
.text
.globl _start
_start:
    leaq tvar@tlsgd(%rip), %rdi
    call __tls_get_addr@PLT
    leaq tvar@tlsld(%rip), %rdi
    movq tvar@gottpoff(%rip), %rax
    movq tvar@dtpoff(%rax), %rbx
    ret
.section .data
tvar: .byte 0
ASM
"$as" -o "$work/tls.o" "$work/tls.s" 2>/dev/null || {
    echo "FAIL: mt/as could not assemble TLS modifiers"; fail=1
}

# TLSGD = 19
"$readelf" -r "$work/tls.o" 2>/dev/null | grep -q "R_X86_64_TLSGD" || {
    echo "FAIL: @tlsgd did not produce R_X86_64_TLSGD"; fail=1
}
# TLSLD = 20
"$readelf" -r "$work/tls.o" 2>/dev/null | grep -q "R_X86_64_TLSLD" || {
    echo "FAIL: @tlsld did not produce R_X86_64_TLSLD"; fail=1
}
# DTPOFF32 = 21
"$readelf" -r "$work/tls.o" 2>/dev/null | grep -q "R_X86_64_DTPOFF32" || {
    echo "FAIL: @dtpoff did not produce R_X86_64_DTPOFF32"; fail=1
}
# GOTTPOFF = 22
"$readelf" -r "$work/tls.o" 2>/dev/null | grep -q "R_X86_64_GOTTPOFF" || {
    echo "FAIL: @gottpoff did not produce R_X86_64_GOTTPOFF"; fail=1
}

# mcc GD emits call __tls_get_addr@PLT (uppercase @PLT); mt/as must treat
# it as PLT32 (not PC32) like the lowercase @plt form.
cat >"$work/pltcase.s" <<'ASM'
.text
.globl _start
_start:
    call __tls_get_addr@PLT
    call foo@plt
    ret
ASM
"$as" -o "$work/pltcase.o" "$work/pltcase.s" 2>/dev/null || {
    echo "FAIL: mt/as could not assemble @PLT calls"; fail=1
}
"$readelf" -r "$work/pltcase.o" 2>/dev/null | grep "__tls_get_addr" | grep -q "R_X86_64_PLT32" || {
    echo "FAIL: @PLT (uppercase) did not produce R_X86_64_PLT32"; fail=1
}
"$readelf" -r "$work/pltcase.o" 2>/dev/null | grep "foo" | grep -q "R_X86_64_PLT32" || {
    echo "FAIL: @plt (lowercase) did not produce R_X86_64_PLT32"; fail=1
}

# --- mt/ld shared-library TLS GD: DTPMOD|DTPOFF GOT pair in .rela.dyn ---
cat >"$work/tlslib.s" <<'ASM'
.text
.globl get_tvar
get_tvar:
    leaq tvar@tlsgd(%rip), %rdi
    call __tls_get_addr@PLT
    movq %rax, %rax
    ret
.section .tdata
.globl tvar
tvar: .byte 42
ASM
"$as" -o "$work/tlslib.o" "$work/tlslib.s" 2>/dev/null
"$ld" -shared -o "$work/tlslib.so" "$work/tlslib.o" 2>/dev/null || {
    echo "FAIL: mt/ld -shared with TLS GD failed"; fail=1
}
"$readelf" -r "$work/tlslib.so" 2>/dev/null | grep -q "R_X86_64_DTPMOD64" || {
    echo "FAIL: shared TLS GD missing DTPMOD64 .rela.dyn entry"; fail=1
}
"$readelf" -r "$work/tlslib.so" 2>/dev/null | grep -q "R_X86_64_DTPOFF64" || {
    echo "FAIL: shared TLS GD missing DTPOFF64 .rela.dyn entry"; fail=1
}

# --- mt/ld static executable TLS GD: relaxed, no GOT / no .rela.dyn ----
cat >"$work/static.s" <<'ASM'
.text
.globl _start
.globl __tls_get_addr
_start:
    leaq tvar@tlsgd(%rip), %rdi
    call __tls_get_addr@PLT
    ret
__tls_get_addr:
    movq $0, %rax
    ret
.section .tdata
.globl tvar
tvar: .byte 42
ASM
"$as" -o "$work/static.o" "$work/static.s" 2>/dev/null
"$ld" -o "$work/static" "$work/static.o" 2>/dev/null || {
    echo "FAIL: mt/ld static executable with TLS GD failed"; fail=1
}
"$readelf" -S "$work/static" 2>/dev/null | grep -q '\.got' && {
    echo "FAIL: static TLS GD should have no .got section"; fail=1
}

if [ "$fail" -ne 0 ]; then
    echo "mt/as x86_64 TLS relocation: FAILED"
    exit 1
fi
echo "mt/as x86_64 TLS relocation (GD/LD/IE/DTPOFF): all checks PASS"
