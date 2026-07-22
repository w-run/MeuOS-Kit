#!/bin/sh
# qemu-runtime.sh - i386 mcc+libc runtime regression inside the MeuOS Kit
# qemu-system-i386 VM (env/).
#
# Compiles the i386 runtime tests with mcc's i386 backend, drops them into
# env/share/i386 (the 9p host share), boots the i386 VM if needed, runs them
# on a REAL 32-bit kernel (Alpine linux-virt 6.6.x under QEMU TCG), and checks
# the PASS/FAIL report written back to the 9p share.
#
# This complements runtime.sh (which executes the same binaries directly on the
# host kernel via CONFIG_IA32_EMULATION, no qemu needed).  qemu-runtime.sh is
# the stronger gate: it validates the full system-level 32-bit execution path
# (libc init, syscalls, exception handling) under a genuine 32-bit kernel.
#
# Requires:
#   - env/build/qemu-install/bin/qemu-system-i386  (env/build/build-qemu.sh)
#   - sysroot-i386 at MeuOS-Kit top
#       make -C projects/meuos-libc ARCH=i386 install DESTDIR=$PWD/sysroot-i386 PREFIX=/usr
#   - socat (qvm dependency) for console interaction
#
# Usage: qemu-runtime.sh [mcc-binary] [sysroot]

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
kitroot=$(CDPATH= cd -- "$(dirname -- "$root")/.." && pwd)
mcc=${1:-"$root/mcc"}
sysroot=${2:-"$kitroot/sysroot-i386"}
envdir="$kitroot/env"
qvm="$envdir/bin/qvm"
share="$envdir/share/i386"

# --- preflight (skip gracefully if env unavailable) ---
if [ ! -x "$envdir/build/qemu-install/bin/qemu-system-i386" ]; then
	printf 'qemu-runtime: qemu-system-i386 not built; skipping.\n' >&2
	printf '               build it: env/build/build-qemu.sh\n' >&2
	exit 0
fi
if [ ! -f "$sysroot/usr/lib/libc-meuos.a" ]; then
	printf 'qemu-runtime: i386 sysroot missing at %s; skipping.\n' "$sysroot" >&2
	printf '               build it: make -C projects/meuos-libc ARCH=i386 install DESTDIR=%s PREFIX=/usr\n' "$sysroot" >&2
	exit 0
fi
command -v socat >/dev/null 2>&1 || {
	printf 'qemu-runtime: socat required (qvm dependency); skipping.\n' >&2
	exit 0
}

# --- compile tests into the 9p share ---
mkdir -p "$share"
tests="runtime_kl runtime_fp runtime_time64 runtime_va fp_unsigned fp_arith"
for t in $tests; do
	printf '  qemu-runtime: build %s\n' "$t"
	"$mcc" --target=i386 --specs=meuos --sysroot="$sysroot" \
		--nostdlib --static -o "$share/$t" "$root/test/i386/$t.c" -l:libc-meuos.a
done

# --- install the guest-side runner (writes report back to 9p) ---
cat > "$share/run-all.sh" <<'EOF'
#!/bin/sh
cd /mnt/host/i386
: > RESULTS.txt
for t in runtime_kl runtime_fp runtime_time64 runtime_va fp_unsigned fp_arith; do
	if ./$t >/tmp/$t.out 2>&1; then
		echo "PASS $t (rc=0)" >> RESULTS.txt
	else
		rc=$?
		echo "FAIL $t (rc=$rc)" >> RESULTS.txt
	fi
	echo "---- $t stdout ----" >> RESULTS.txt
	tail -8 /tmp/$t.out >> RESULTS.txt
done
echo "ALLDONE" >> RESULTS.txt
EOF

# stale report from a previous run would make the poll below exit early
rm -f "$share/RESULTS.txt"

# --- boot the i386 VM if needed ---
"$qvm" boot i386 >/dev/null 2>&1 || true
# wait until the guest has the 9p share mounted and can see our runner
for _ in $(seq 1 30); do
	if "$qvm" run i386 'test -f /mnt/host/i386/run-all.sh && echo READY' 2>/dev/null | grep -q READY; then
		break
	fi
	sleep 1
done

# --- run inside the guest (background so we can poll the report) ---
"$qvm" run i386 '/bin/sh /mnt/host/i386/run-all.sh & echo STARTED' >/dev/null 2>&1 || true
for _ in $(seq 1 30); do
	if [ -f "$share/RESULTS.txt" ] && grep -q ALLDONE "$share/RESULTS.txt" 2>/dev/null; then
		break
	fi
	sleep 1
done

if [ ! -f "$share/RESULTS.txt" ]; then
	printf 'qemu-runtime: no RESULTS from guest (VM boot/9p failure?)\n' >&2
	exit 1
fi

# --- report ---
cat "$share/RESULTS.txt"
if grep -q '^FAIL' "$share/RESULTS.txt"; then
	printf 'qemu-runtime: i386 runtime FAILED\n' >&2
	exit 1
fi
printf 'qemu-runtime: i386 runtime regression passed (qemu-system-i386, real 32-bit kernel)\n'
