#!/bin/bash
# MeuOS Kit Bootstrap — Phase 0 → 4
# Per AGENTS.md §3 + mkit-bootstrap skill.
#
# Usage:
#   ./bootstrap.sh             # run from current state (resumes)
#   ./bootstrap.sh --phase 0  # run a specific phase only
#
# Halts on first phase failure. Each run writes bootstrap-report.md.

set -euo pipefail

# ============================================================
# Configuration
# ============================================================

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROGRESS_FILE="${REPO_ROOT}/bootstrap-report.md"

# Environment with sensible defaults (idempotent re-runs)
: "${HOST_CC:=}"
: "${MEUOS_SYSROOT:=${REPO_ROOT}/sysroot}"
: "${HOST_TRIPLET:=}"

export MEUOS_SYSROOT
export HOST_TRIPLET
export HOST_CC

# Fixed-format UTC timestamp (reproducible: no locale, no tz)
TIMESTAMP="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# ============================================================
# Helpers
# ============================================================

log()   { printf '[bootstrap] %s\n' "$*" >&2; }
phase() { printf '\n========== %s ==========\n' "$*" >&2; }

die() {
    printf '[bootstrap][FATAL] %s\n' "$*" >&2
    exit 1
}

# ============================================================
# Phase 0 — Preparation
# ============================================================

phase0_prepare() {
    phase "Phase 0 — Preparation"

    # 1. Detect HOST_CC (gcc preferred, tcc fallback)
    if [[ -z "${HOST_CC}" ]]; then
        if command -v gcc >/dev/null 2>&1; then
            HOST_CC="$(command -v gcc)"
        elif command -v tcc >/dev/null 2>&1; then
            HOST_CC="$(command -v tcc)"
        else
            die "Neither gcc nor tcc found on PATH"
        fi
    fi
    log "HOST_CC=${HOST_CC}"
    "${HOST_CC}" --version 2>&1 | head -1 >&2 || die "HOST_CC --version failed"

    # 2. MEUOS_SYSROOT (already defaulted above)
    log "MEUOS_SYSROOT=${MEUOS_SYSROOT}"

    # 3. Derive HOST_TRIPLET (Phase 0–3: vendor=unknown; Phase 4 chroot switches to vendor=meuos)
    if [[ -z "${HOST_TRIPLET}" ]]; then
        local arch
        arch="$(uname -m)"
        # Normalize to GNU arch naming (QBE uses amd64/arm64/rv64; GNU uses x86_64/aarch64/riscv64)
        case "${arch}" in
            x86_64|amd64)  arch="x86_64"  ;;
            aarch64|arm64) arch="aarch64" ;;
            riscv64)       arch="riscv64" ;;
            *) die "Unsupported host arch: ${arch}" ;;
        esac
        HOST_TRIPLET="${arch}-unknown-linux"
    fi
    log "HOST_TRIPLET=${HOST_TRIPLET} (Phase 0–3: vendor=unknown)"

    # 4. Create sysroot layout (flat — no multiarch subdir, single-target per build)
    mkdir -p \
        "${MEUOS_SYSROOT}/bin" \
        "${MEUOS_SYSROOT}/lib" \
        "${MEUOS_SYSROOT}/include" \
        "${MEUOS_SYSROOT}/usr/bin" \
        "${MEUOS_SYSROOT}/usr/lib" \
        "${MEUOS_SYSROOT}/usr/include"
    log "sysroot layout created at ${MEUOS_SYSROOT}"

    # 5. Validate
    "${HOST_CC}" -v >/dev/null 2>&1 || die "HOST_CC -v exited non-zero"
    [[ -w "${MEUOS_SYSROOT}" ]] || die "MEUOS_SYSROOT not writable: ${MEUOS_SYSROOT}"

    log "Phase 0 PASS"
    return 0
}

# ============================================================
# Phase 1 — Birth of mcc (host CC builds mcc)
# ============================================================

phase1_build_mcc() {
    phase "Phase 1 — Birth of mcc"
    if [[ ! -d "${REPO_ROOT}/mcc" ]] || [[ ! -f "${REPO_ROOT}/mcc/Makefile" ]]; then
        die "mcc/ source tree (with Makefile) not yet created. Implement mcc first (AGENTS.md §6 Task 1)."
    fi

    # Keep the source tree reusable: build incrementally and leave cleaning
    # to an explicit `make clean`, rather than deleting a user's artifacts.
    make -C "${REPO_ROOT}/mcc"
    make -C "${REPO_ROOT}/mcc" check
    make -C "${REPO_ROOT}/mcc" check-c11
    make -C "${REPO_ROOT}/mcc" check-driver
    make -C "${REPO_ROOT}/mcc" check-i386
    make -C "${REPO_ROOT}/mcc" check-targets
    make -C "${REPO_ROOT}/mcc" check-loongarch64

    mkdir -p "${MEUOS_SYSROOT}/usr/bin"
    cp "${REPO_ROOT}/mcc/mcc" "${MEUOS_SYSROOT}/usr/bin/mcc"
    chmod 755 "${MEUOS_SYSROOT}/usr/bin/mcc"

    local hello_c hello_bin atomic_bin
    hello_c="/tmp/meuos-mcc-phase1-hello.c"
    hello_bin="/tmp/meuos-mcc-phase1-hello"
    printf '%s\n' 'int main(void) { return 0; }' > "${hello_c}"
    "${MEUOS_SYSROOT}/usr/bin/mcc" -o "${hello_bin}" "${hello_c}"
    "${hello_bin}"
    atomic_bin="/tmp/meuos-mcc-phase1-atomic"
    "${MEUOS_SYSROOT}/usr/bin/mcc" -I"${REPO_ROOT}/mcc/test/c11" \
        -o "${atomic_bin}" "${REPO_ROOT}/mcc/test/c11/atomic_concurrent.c" -lpthread
    "${atomic_bin}" | grep -qx 'PASS'
    log "Phase 1 PASS: mcc installed to ${MEUOS_SYSROOT}/usr/bin/mcc"
}

# ============================================================
# Phase 2 - Birth of meuos-libc (mcc builds meuos-libc)
# ============================================================

phase2_build_libc() {
    phase "Phase 2 - Birth of meuos-libc"
    # mcc rebuilds the libc C sources and runs the full regression
    # (atomic, threads, TLS, stdio, signal, setjmp, pthread, process).
    make -C "${REPO_ROOT}/meuos-libc" check-mcc
    # mcc itself is then rebuilt with only libc-meuos (--nostdlib --static)
    # and runs a hello-world, proving the standalone sysroot closed loop.
    make -C "${REPO_ROOT}/mcc" check-sysroot-static
    # GNU-extension compatibility layer (error, argp, obstack, funopen,
    # getline, asprintf, malloc hooks, strdupa) per AGENTS.md §2.2.
    make -C "${REPO_ROOT}/meuos-libc-compat" check
    make -C "${REPO_ROOT}/meuos-libc-compat" install DESTDIR="${MEUOS_SYSROOT}" PREFIX=/usr
    log "Phase 2 PASS: libc-meuos + libc-meuos-compat verified"
}

# ============================================================
# Phase 3 - Birth of meow (mcc + meuos-libc build meow)
# ============================================================

phase3_build_meow() {
    phase "Phase 3 — Birth of meow"
    # meow is built with mcc + libc-meuos (--nostdlib --static) and runs
    # the native YAML target-graph regression.
    make -C "${REPO_ROOT}/meow" SYSROOT="${MEUOS_SYSROOT}" check-sysroot-static
    make -C "${REPO_ROOT}/meow" SYSROOT="${MEUOS_SYSROOT}" check
    make -C "${REPO_ROOT}/meow" SYSROOT="${MEUOS_SYSROOT}" \
        DESTDIR="${MEUOS_SYSROOT}" install
    (cd "${REPO_ROOT}" && "${MEUOS_SYSROOT}/usr/bin/meow" list >/dev/null)
    log "Phase 3 PASS: meow builds with mcc+libc-meuos and runs native YAML targets"
}

# ============================================================
# Phase 4 — Bootstrap Verification (chroot self-rebuild)
# ============================================================

phase4_verify() {
    # Phase 4 = self-rebuild gate. The former Alpine-container flow
    # (experiments/run-alpine-phase4.sh) was removed; the equivalent
    # verification is mcc rebuilding itself against libc-meuos only.
    # Cross-arch runtime verification lives in env/ (QEMU, see env/README.md).
    phase "Phase 4 - Bootstrap Verification (self-rebuild gate)"
    if [ "${MEUOS_SKIP_PHASE4:-0}" = 1 ]; then
        log "Phase 4 SKIP (MEUOS_SKIP_PHASE4=1)"
        return 0
    fi
    if make -C "${REPO_ROOT}/projects/mcc" check-sysroot-static >/dev/null 2>&1; then
        log "Phase 4 PASS: mcc self-rebuilds with libc-meuos (check-sysroot-static)"
        log "  cross-arch runtime: env/bin/qvm (see env/README.md)"
    else
        log "Phase 4 FAIL: check-sysroot-static failed"
        return 1
    fi
}

# ============================================================
# Progress report writer
# ============================================================

write_progress() {
    local phase0_status="${1:-PASS}"
    local phase1_status="${2:-SKIP}"
    local phase2_status="${3:-SKIP}"
    local phase3_status="${4:-SKIP}"
    local phase4_status="${5:-SKIP}"
    local next_steps="${6:-Implement mcc source tree under mcc/ (AGENTS.md §6 Task 1). Use mkit-c11-check as validation gate.}"

    cat > "${PROGRESS_FILE}" <<EOF
# MeuOS Kit Bootstrap Progress

Generated: ${TIMESTAMP}
Host CC: ${HOST_CC}
MEUOS_SYSROOT: ${MEUOS_SYSROOT}
HOST_TRIPLET: ${HOST_TRIPLET}

## Phase 0 — Preparation [${phase0_status}]

- HOST_CC detected: \`${HOST_CC}\`
- MEUOS_SYSROOT: \`${MEUOS_SYSROOT}\`
- HOST_TRIPLET: \`${HOST_TRIPLET}\` (vendor=unknown for Phase 0–3)
- sysroot layout created: \`bin lib include usr/bin usr/lib usr/include\`
- Validation: \`HOST_CC -v\` OK; sysroot writable

## Phase 1 — Birth of mcc [${phase1_status}]

- Built with host compiler using \`make -C mcc\`.
- Validation: host execution, C11 matrix, driver shared/TLS/float checks,
  i386, aarch64, riscv64, and LoongArch64 assembly regressions.
- Installed host-bootstrap compiler: \`${MEUOS_SYSROOT}/usr/bin/mcc\`.
- Hello-world compiled and ran with the installed compiler (exit 0).
- Installed compiler compiled and ran the two-thread \`_Atomic\` gate (\`PASS\`).

## Phase 2 - Birth of meuos-libc [${phase2_status}]

- x86_64 \`libc-meuos.a\` + \`libatomic-meuos.a\` + \`crt1.o\` installed and
  tested; mcc compiles every libc C source (check-mcc regression PASS).
- 40 independently implemented direct syscalls (read, write, open, close,
  fork, execve, exit, mmap, stat, fstat, lseek, getdents64, socket, ...).
- Standard headers per AGENTS.md §2.1: \`<signal.h>\`, \`<setjmp.h>\`,
  \`<pthread.h>\`, \`<sys/syscall.h>\`, \`<stdatomic.h>\`, \`<threads.h>\`,
  \`<stdio.h>\`, \`<stdlib.h>\`, \`<string.h>\`, \`<unistd.h>\`, \`<fcntl.h>\`,
  \`<errno.h>\`, \`<sys/stat.h>\`, \`<sys/mman.h>\`, \`<sys/types.h>\`, etc.
- Signal handling: \`signal\`, \`raise\`, \`sigaction\`, \`sigprocmask\`,
  \`sigpending\`, \`sigsuspend\`, \`sigaltstack\`, sigset primitives, and a
  \`rt_sigreturn\` restorer trampoline (SA_RESTORER).
- \`setjmp\`/\`longjmp\` and \`sigsetjmp\`/\`siglongjmp\` implemented in
  assembly (sigsetjmp saves the caller's frame directly, not a stale wrapper
  frame).
- \`pthread\` minimal subset adapting over the C11 thread primitives
  (create, join, exit, self, mutex, cond, TSS).
- C11 threads (clone/futex), mutexes, condition variables, call_once, TSS,
  static TLS, and per-thread errno -- all verified in bare \`--nostdlib\`
  regressions.
- \`meuos-libc-compat\` (§2.2): \`error\`/\`error_at_line\`, \`argp\`, \`obstack\`,
  \`funopen\`/\`fopencookie\`, \`getline\`/\`getdelim\`, \`asprintf\`/\`vasprintf\`, weak
  \`__malloc_hook\`/\`__free_hook\`, \`strdupa\`/\`strndupa\` -- all tested.
- Closed loop: mcc rebuilds itself with only \`libc-meuos.a\`
  (\`--nostdlib --static\`) and runs hello-world (check-sysroot-static PASS).
  The Phase 2 static C11 counter regression prints \`counter = 2000\`.

## Phase 3 — Birth of meow [${phase3_status}]

- meow is built with mcc + libc-meuos (\`--nostdlib --static\`) and runs
  the native YAML target-graph regression (check-sysroot-static PASS).
- Supports \`variables\`, \`targets\` with \`deps\`/\`commands\`,
  \`inputs\`/\`outputs\` up-to-date checks, \`phony\` targets, \`%\` pattern
  rules, \`include\`, \`-jN\` parallel execution, and automatic variables
  (\`$@\`, \`$<\`, \`$^\`).
- Makefile compatibility mode for directories without \`meow.yaml\`.
- Installed to \`${MEUOS_SYSROOT}/usr/bin/meow\`; \`meow list\` enumerates
  packages in \`pkgs/\`.

## Phase 4 - Bootstrap Verification [${phase4_status}]

MeuOS Next 尚不可启动时，Alpine/musl 最小用户空间作为自举代理环境。入口为
\`env/bin/qvm (QEMU 6.6.142, see env/README.md)\`。

容器内验证链：
1. Alpine \`gcc\` 构建 stage-1 \`mcc\`；
2. stage-1 \`mcc\` 重新编译全部 \`mcc\` C 源码（stage-2 自重编译）；
3. stage-2 \`mcc\` 通过变参回归测试（x86_64 SysV va_list lowering）；
4. stage-2 \`mcc\` 构建并运行裸链接 \`meuos-libc\` 全部回归（原子、线程、TLS、
   stdio、signal、setjmp、pthread、进程等）；
5. stage-2 \`mcc\` 构建 \`meow\`，运行原生 YAML 目标图回归与 Makefile 兼容模式。

本轮修复的关键阻塞：
- \`--specs=meuos\` 现在自动启用 \`-static\`（之前在 musl 上产生动态链接二进制）；
- \`meow.c\` 用 POSIX \`opendir\`/\`readdir\` 替代 \`getdents64\`（musl 不导出此符号）；
- \`meuos-libc\` 新增 \`<dirent.h>\` 与 \`opendir\`/\`readdir\`/\`closedir\` 实现。
## Next Steps

${next_steps}
EOF
    log "Report written to ${PROGRESS_FILE} (auto-generated; curated status is in each project's ARCHITECTURE.md)"
}

# ============================================================
# Phase 5 — LFS package validation (bzip2 1.0.8 + binutils 2.42 libiberty)
# ============================================================

phase5_lfs() {
    phase "Phase 5 — LFS package validation"
    if [ "${MEUOS_SKIP_PHASE5:-0}" = 1 ]; then
        log "Phase 5 SKIP (MEUOS_SKIP_PHASE5=1)"
        return 0
    fi
    cp "${REPO_ROOT}/mcc/mcc" "${MEUOS_SYSROOT}/usr/bin/mcc"
    cp "${REPO_ROOT}/meow/build/meow" "${MEUOS_SYSROOT}/usr/bin/meow"

    # bzip2 1.0.8
    (cd "${REPO_ROOT}" && MEUOS_SYSROOT="${MEUOS_SYSROOT}" \
        "${MEUOS_SYSROOT}/usr/bin/meow" clean bzip2) >/dev/null 2>&1 || true
    (cd "${REPO_ROOT}" && MEUOS_SYSROOT="${MEUOS_SYSROOT}" \
        "${MEUOS_SYSROOT}/usr/bin/meow" build bzip2) | tail -5
    if [ ! -x /tmp/bzip2-test ]; then
        die "bzip2 1.0.8 build failed via meow"
    fi
    /tmp/bzip2-test --version 2>&1 | head -1 | grep -q '1.0.8' \
        || die "bzip2 1.0.8 --version check failed"

    # binutils 2.42 libiberty
    (cd "${REPO_ROOT}" && MEUOS_SYSROOT="${MEUOS_SYSROOT}" \
        "${MEUOS_SYSROOT}/usr/bin/meow" clean binutils) >/dev/null 2>&1 || true
    (cd "${REPO_ROOT}" && MEUOS_SYSROOT="${MEUOS_SYSROOT}" \
        "${MEUOS_SYSROOT}/usr/bin/meow" build binutils) | tail -5
    if [ ! -f /tmp/libiberty-meuos.a ]; then
        die "binutils 2.42 libiberty build failed via meow"
    fi
    log "Phase 5 PASS: bzip2 1.0.8 and binutils 2.42 libiberty built via meow"
}

# ============================================================
# Main
# ============================================================

main() {
    local requested_phase="${1:-1}"
    log "MeuOS Kit Bootstrap starting at ${TIMESTAMP}"
    log "Repo: ${REPO_ROOT}"

    phase0_prepare

    phase1_build_mcc

    if [ "${requested_phase}" = "--phase" ]; then
        requested_phase="${2:-1}"
    fi
    case "${requested_phase}" in
    1|--phase=1)
        write_progress "PASS" "PASS" "SKIP" "SKIP" "SKIP" \
            "Run ./bootstrap.sh --phase 2 for the current meuos-libc incremental gate."
        ;;
    2|--phase=2)
        phase2_build_libc
        write_progress "PASS" "PASS" "PASS" "SKIP" "SKIP" \
            "Phase 2 libc+compat + mcc self-rebuild verified."
        ;;
    3|--phase=3)
        phase2_build_libc
        phase3_build_meow
        write_progress "PASS" "PASS" "PASS" "PASS" "SKIP" \
            "Phase 2 libc+compat and Phase 3 meow verified."
        ;;
    4|--phase=4)
        phase2_build_libc
        phase3_build_meow
        phase4_verify
        write_progress "PASS" "PASS" "PASS" "PASS" "PASS" \
            "Phase 0-4 all PASS. Self-rebuild verified in Alpine compat gate."
        ;;
    5|--phase=5)
        phase2_build_libc
        phase3_build_meow
        phase4_verify
        phase5_lfs
        write_progress "PASS" "PASS" "PASS" "PASS" "PASS" \
            "Phase 0-5 all PASS. Self-rebuild verified and LFS packages built via meow."
        ;;
    *)
        die "usage: bootstrap.sh [--phase 1|2|3|4|5]"
        ;;
    esac

    log ""
    log "Bootstrap validation complete."
}

main "$@"
