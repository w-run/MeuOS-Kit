#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mcc=${1:-"$root/mcc"}
work=${TMPDIR:-/tmp}/mcc-driver-features.$$
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work"

# The host linker must produce a real shared object, including a local TLS
# definition.  On x86_64 mcc selects the linkable initial-exec GOT model for
# that DSO; the remaining targets are checked at assembly-generation level.
# These are host-runtime checks: --specs=host forces the host specs so the
# script behaves the same whether or not MEUOS_SYSROOT implies meuos specs
# (which would try to link the static libc-meuos.a into a DSO).
"$mcc" --specs=host --shared -o "$work/libfeatures.so" "$root/test/driver/shared.c"
LC_ALL=C readelf -h "$work/libfeatures.so" | grep -Eq 'Type:[[:space:]]+DYN'
"$mcc" --specs=host --shared -o "$work/libfeatures-tls.so" "$root/test/driver/shared_tls.c"
LC_ALL=C readelf -r "$work/libfeatures-tls.so" | grep -Eq 'TPOFF64'
"$mcc" --specs=host -o "$work/shared-tls-consumer" \
	"$root/test/driver/shared_tls_consumer.c" -L"$work" -lfeatures-tls
LD_LIBRARY_PATH="$work" "$work/shared-tls-consumer"

# A Make-style final link gives the driver only object files.  They must be
# passed to the host linker, never fed back into the C lexer as source text.
printf '%s\n' 'int main(void) { return 0; }' > "$work/object-link.c"
"$mcc" --specs=host -c -o "$work/object-link.o" "$work/object-link.c"
"$mcc" --specs=host -o "$work/object-link" "$work/object-link.o"
"$work/object-link"

for target in x86_64-linux aarch64-linux riscv64-linux loongarch64-linux; do
	"$mcc" --target="$target" -S -o "$work/$target.s" \
		"$root/test/driver/shared_tls.c"
	done

grep -Eq '%fs:|@GOTTPOFF' "$work/x86_64-linux.s"
grep -Eq 'fcmp|ucomi|comisd|comiss' "$work/x86_64-linux.s"
grep -Eq 'fcmp' "$work/aarch64-linux.s"
grep -Eq 'f(l|g|e)\.(s|d)|f(eq|lt|le|gt|ge)\.' "$work/riscv64-linux.s"
grep -Eq 'fcmp\.' "$work/loongarch64-linux.s"

printf '%s\n' 'shared/TLS/floating-comparison regression checks passed'
