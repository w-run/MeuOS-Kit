#!/bin/sh
set -eu

as=${1:?as path required}
ar=${2:?ar path required}
ld=${3:?ld path required}
work=$(mktemp -d /tmp/meuos-toolchain-ld-smoke.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat >"$work/start.s" <<'ASM'
.text
.globl _start
_start:
	callq main
	movl %eax, %edi
	movl $60, %eax
	syscall
ASM
cat >"$work/main.s" <<'ASM'
.text
.globl main
main:
	movq global@gotpcrel(%rip), %rax
	movl (%rax), %edi
	callq helper@plt
	cmpl $42, %eax
	setnz %al
	movzbl %al, %eax
	ret
.data
.balign 4
.globl global
global:
	.int 41
ASM
cat >"$work/helper.s" <<'ASM'
.text
.globl helper
helper:
	movl %edi, %eax
	addl $1, %eax
	ret
ASM

"$as" -o "$work/start.o" "$work/start.s"
"$as" -o "$work/main.o" "$work/main.s"
"$as" -o "$work/helper.o" "$work/helper.s"
"$ar" rcs "$work/libhelper.a" "$work/helper.o"
"$ld" -o "$work/app" "$work/start.o" "$work/main.o" "$work/libhelper.a"
"$work/app"
printf '%s\n' 'mt ld x86_64 smoke: PASS'
