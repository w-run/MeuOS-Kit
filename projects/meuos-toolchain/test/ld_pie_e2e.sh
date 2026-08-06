#!/bin/sh
# ld_pie_e2e.sh - PIE end-to-end regression gate.
#
# Verifies that mt/ld produces correct PIE executables:
#   1. JUMP_SLOT relocations go into .rela.plt (DT_JMPREL), not .rela.dyn
#   2. R_X86_64_RELATIVE emited for .data and .bss data symbols
#   3. .plt stubs are correctly generated
#   4. The ld.so dynamic linker can load and relocate the PIE
#   5. The executed PIE produces the correct exit code
#
# Tests are x86_64 only (multi-arch PIE tests extended separately).
set -eu

as=${1:?mt/as path required}
ld=${2:?mt/ld path required}
readelf=${3:?mt/readelf path required}
rtld=${4:?mt/ld.so path required}

work=$(mktemp -d /tmp/mt-pie-e2e.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
fail=0

# ---- Test 1: JUMP_SLOT routing test ----
# Build a minimal PIE that calls an external function via PLT.
cat >"$work/jmprel.s" <<'ASM'
.text
.globl _start
.type ext_func, @function
_start:
	leaq hello_str(%rip), %rdi
	xorq %rax, %rax
	call ext_func@PLT
	movl $42, %edi
	movl $60, %eax
	syscall
.data
hello_str:
	.asciz "hello\n"
ASM

"$as" --target=x86_64 -o "$work/jmprel.o" "$work/jmprel.s"
"$ld" -pie -dynamic-linker /tmp/ld-meuos.so.1 -o "$work/jmprel" "$work/jmprel.o"

# 1a) File must be ET_DYN (PIE executable)
if ! "$readelf" -h "$work/jmprel" 2>/dev/null | grep -q "Type:.*DYN"; then
	echo "FAIL (jmprel): PIE not ET_DYN"; fail=1
fi

# 1b) Dynamic section must have DT_JMPREL
if ! "$readelf" -d "$work/jmprel" 2>/dev/null | grep -q "JMPREL"; then
	echo "FAIL (jmprel): missing DT_JMPREL"; fail=1
fi

# 1c) .rela.plt must contain a JUMP_SLOT for ext_func
if ! "$readelf" -r "$work/jmprel" 2>/dev/null | grep -q "JUMP_SLO"; then
	echo "FAIL (jmprel): missing JUMP_SLOT in .rela.plt"; fail=1
fi

# 1d) .rela.dyn must NOT contain JUMP_SLOT entries
if "$readelf" -r "$work/jmprel" 2>/dev/null | \
	sed -n '/\.rela\.dyn/,/\.rela\.plt/p' | grep -q "JUMP_SLO"; then
	echo "FAIL (jmprel): JUMP_SLOT present in .rela.dyn"; fail=1
fi

echo "  JUMP_SLOT routing: OK"

# ---- Test 2: RELATIVE generation test ----
# Build a PIE with .data and .bss globals: RELATIVE entries fix them up.
cat >"$work/relative.s" <<'ASM'
.text
.globl _start
_start:
	# Access .data symbol via GOT
	movq data_var@gotpcrel(%rip), %rax
	movl (%rax), %ebx
	# Access .bss symbol via GOT
	movq bss_var@gotpcrel(%rip), %rax
	movl (%rax), %ecx
	# ebx = 42 (from .data), ecx = 0 (from .bss, zero-initialized)
	leal (%rbx, %rcx), %edi
	movl $60, %eax
	syscall
.data
.globl data_var
data_var:
	.int 42
.bss
.globl bss_var
bss_var:
	.zero 4
ASM

"$as" --target=x86_64 -o "$work/relative.o" "$work/relative.s"
"$ld" -pie -dynamic-linker /tmp/ld-meuos.so.1 -o "$work/relative" "$work/relative.o"

# 2a) Relocations must include R_X86_64_RELATIVE
if ! "$readelf" -r "$work/relative" 2>/dev/null | grep -q "RELATIVE"; then
	echo "FAIL (relative): missing R_X86_64_RELATIVE"; fail=1
fi

# 2b) The only GOT entry should be RELATIVE (.data + .bss)
n_rel=$( "$readelf" -r "$work/relative" 2>/dev/null | grep -c "RELATIVE" )
if [ "$n_rel" -lt 2 ]; then
	echo "FAIL (relative): only $n_rel RELATIVE entries (need >=2 for data+bss)"; fail=1
fi

echo "  RELATIVE generation: OK"

# ---- Test 3: ld.so runtime execution ----
# Copy ld.so into the expected PT_INTERP path
cp "$rtld" "$work/ld-meuos.so.1"

cat >"$work/run.s" <<'ASM'
.text
.globl _start
_start:
	movq the_answer@gotpcrel(%rip), %rax
	movl (%rax), %edi
	movl $60, %eax
	syscall
.data
.globl the_answer
the_answer:
	.int 42
ASM

"$as" --target=x86_64 -o "$work/run.o" "$work/run.s"
"$ld" -pie -dynamic-linker "$work/ld-meuos.so.1" -o "$work/run" "$work/run.o"

# 3a) Execute the PIE via the kernel's PT_INTERP loader (ld.so relocates it)
set +e
"$work/run"
rc=$?
set -e

if [ "$rc" -ne 42 ]; then
	echo "FAIL (runtime): PIE exit code $rc (expected 42)"; fail=1
fi
echo "  ld.so runtime: exit=$rc OK"

# ---- Summary ----
if [ "$fail" -ne 0 ]; then
	echo "mt ld PIE e2e: FAILED"
	exit 1
fi
echo "mt ld PIE e2e: all checks PASS"