#!/bin/sh
# rtld_e2e.sh - Dynamic-linker end-to-end gate.
#
# Builds a minimal position-independent executable (PIE) with mt/as + mt/ld,
# wires PT_INTERP to the freshly built ld.so (mt/ld -dynamic-linker), then
# executes it on the real kernel.  The kernel loads ld.so via PT_INTERP,
# ld.so relocates the PIE (applying .rela.dyn R_X86_64_RELATIVE against the
# actual load base) and jumps to the entry point, which reads a global
# variable and exits with its value (42).  A failure at any step aborts with
# the exact exit code so the caller can diagnose instead of masking a break.
set -eu

as=${1:?mt/as path required}
ld=${2:?mt/ld path required}
rtld=${3:?mt/ld.so path required}

# mt/readelf lives beside the dynamic linker in build/bin/.
rtld_dir=$(dirname "$rtld")
readelf="$rtld_dir/readelf"
if [ ! -x "$readelf" ]; then
	echo "mt rtld e2e: readelf not found at $readelf" >&2
	exit 1
fi

work=$(mktemp -d /tmp/meuos-rtld-e2e.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Copy the dynamic linker to the PT_INTERP path so the kernel can load it.
cp "$rtld" "$work/ld-meuos.so.1"

cat >"$work/main.s" <<'ASM'
.text
.globl _start
_start:
	# global is accessed via the GOT (R_X86_64_RELATIVE dynamic reloc),
	# so ld.so must apply .rela.dyn before _start runs.
	movq global@gotpcrel(%rip), %rax
	movl (%rax), %edi
	movl $60, %eax
	syscall
.data
.balign 4
.globl global
global:
	.int 42
ASM

"$as" -o "$work/main.o" "$work/main.s"
"$ld" -pie -dynamic-linker "$work/ld-meuos.so.1" \
	-o "$work/hello" "$work/main.o"

# Shape checks: readable ET_DYN + PT_INTERP, and a .rela.dyn whose
# R_X86_64_RELATIVE entry lets ld.so fix up the GOT slot at load time.
"$readelf" -h "$work/hello" | grep -q "Type:.*DYN" || {
	echo "mt rtld e2e: expected ET_DYN" >&2; exit 1; }
"$readelf" -l "$work/hello" | grep -q "INTERP" || {
	echo "mt rtld e2e: missing PT_INTERP" >&2; exit 1; }
"$readelf" -r "$work/hello" | grep -q "R_X86_64_RELATIVE" || {
	echo "mt rtld e2e: missing .rela.dyn R_X86_64_RELATIVE" >&2; exit 1; }

# Execute through the kernel's PT_INTERP loader.  ld.so relocates the PIE
# and the program exits with the value of the relocated global (42).
set +e
"$work/hello"
rc=$?
set -e
if [ "$rc" -ne 42 ]; then
	echo "mt rtld e2e: FAIL (hello exited $rc, expected 42)" >&2
	exit 1
fi

echo "mt rtld e2e: PASS"
