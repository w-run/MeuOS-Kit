#!/bin/sh
# test/as_sse_x86_64.sh - SSE/SSE2 scalar instruction encoding golden bytes test.
#
# Verifies that mt/as produces byte-identical output to the host assembler
# for all SSE scalar instructions that mcc emits.
set -eu

as=${1:?as path required}
work=$(mktemp -d /tmp/mt-sse-check.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/sse.s" << 'ASM'
.text
movss %xmm0, (%rsp)
movss (%rsp), %xmm0
movss %xmm1, %xmm0
movsd %xmm0, (%rsp)
movsd (%rsp), %xmm0
movsd %xmm1, %xmm0
addsd %xmm1, %xmm0
subsd %xmm1, %xmm0
mulsd %xmm1, %xmm0
divsd %xmm1, %xmm0
sqrtsd %xmm0, %xmm1
addss %xmm1, %xmm0
subss %xmm1, %xmm0
mulss %xmm1, %xmm0
divss %xmm1, %xmm0
sqrtss %xmm0, %xmm1
cvtss2sd %xmm0, %xmm1
cvtsd2ss %xmm0, %xmm1
cvttss2sil %xmm0, %eax
cvttss2siq %xmm0, %rax
cvttsd2sil %xmm0, %eax
cvttsd2siq %xmm0, %rax
cvtsi2ssl %edi, %xmm0
cvtsi2ssq %rdi, %xmm0
cvtsi2sdl %edi, %xmm0
cvtsi2sdq %rdi, %xmm0
ucomiss %xmm0, %xmm1
ucomisd %xmm0, %xmm1
movq %xmm0, %rax
movq %rax, %xmm0
movd %xmm0, %eax
movd %eax, %xmm0
xorps %xmm0, %xmm0
pxor %xmm0, %xmm0
ASM

"$as" -o "$work/mt.o" "$work/sse.s"

if ! as --64 -o "$work/host.o" "$work/sse.s" 2>"$work/host_err.txt"; then
    echo "mt as SSE x86_64 check: SKIP (host as failed)"
    cat "$work/host_err.txt" >&2
    exit 0
fi

# Compare instruction bytes (skip addresses and symbol names)
"$as" -o "$work/mt2.o" "$work/sse.s" 2>/dev/null  # use mt/as path object
objdump -d "$work/mt.o"   | sed -n 's/^\s*[0-9a-f]*:\s//p' | awk '{print $1, $2, $3, $4, $5, $6, $7, $8}' > "$work/mt.txt"
objdump -d "$work/host.o" | sed -n 's/^\s*[0-9a-f]*:\s//p' | awk '{print $1, $2, $3, $4, $5, $6, $7, $8}' > "$work/host.txt"

if diff -q "$work/mt.txt" "$work/host.txt" >/dev/null 2>&1; then
    echo "mt as SSE x86_64 check: PASS"
    exit 0
else
    echo "mt as SSE x86_64 check: FAIL"
    echo "--- diff (mt vs host) ---"
    diff "$work/mt.txt" "$work/host.txt" | head -20
    exit 1
fi
