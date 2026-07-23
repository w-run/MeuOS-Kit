#!/bin/bash
# mcc-libc.sh - mcc + meuos-libc 绑定组快速启动
#
# 这两个组件在自举链上是紧耦合的:mcc 编译 meuos-libc,meuos-libc 提供
# mcc 自重编译 (check-sysroot-static) 的运行时和链接库。绝大多数时间
# 改一个必然涉及另一个,所以统一入口。
#
# 用法:
#   ./mcc-libc.sh                       # 跑 mcc check + meuos-libc check
#   ./mcc-libc.sh --all                 # 跑 check + check-sysroot-static (慢)
#   ./mcc-libc.sh --target <make-tgt>   # 跑 mcc 的指定 target,libc 跟
#   ./mcc-libc.sh --mcc-only            # 只跑 mcc
#   ./mcc-libc.sh --libc-only           # 只跑 meuos-libc
#   ./mcc-libc.sh --clean               # 清理 build/ (两边都清)
#   ./mcc-libc.sh --help
#
# Exit code: 任意一边失败即非 0。

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 绑定组 - 这是脚本里**唯一**硬编码名字的地方
MCC_DIR="${REPO_ROOT}/projects/mcc"
LIBC_DIR="${REPO_ROOT}/projects/meuos-libc"

# ---- helpers ------------------------------------------------------------

log()  { printf '[mcc-libc] %s\n' "$*" >&2; }
fail() { printf '[mcc-libc][FATAL] %s\n' "$*" >&2; exit 1; }

usage() {
    sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

have_makefile() {
    local d="$1"
    [[ -f "${d}/Makefile" ]]
}

# ---- preflight ----------------------------------------------------------

[[ -d "${MCC_DIR}"  ]] || fail "mcc dir not found: ${MCC_DIR}"
[[ -d "${LIBC_DIR}" ]] || fail "meuos-libc dir not found: ${LIBC_DIR}"
have_makefile "${MCC_DIR}"  || fail "mcc/Makefile missing"
have_makefile "${LIBC_DIR}" || fail "meuos-libc/Makefile missing"

# ---- arg parse ----------------------------------------------------------

TARGET="check"
RUN_MCC=1
RUN_LIBC=1
RUN_ALL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)     usage ;;
        --all)         RUN_ALL=1 ;;
        --mcc-only)    RUN_LIBC=0 ;;
        --libc-only)   RUN_MCC=0 ;;
        --target)      TARGET="${2:-}"; shift ;;
        --target=*)    TARGET="${1#*=}" ;;
        --clean)
            log "clean: mcc"
            (cd "${MCC_DIR}"  && make clean) || true
            log "clean: meuos-libc"
            (cd "${LIBC_DIR}" && make clean) || true
            exit 0
            ;;
        -*)            fail "unknown flag: $1 (try --help)" ;;
        *)             fail "unexpected arg: $1 (this script takes no positional args; use quickstart.sh for arbitrary subprojects)" ;;
    esac
    shift
done

if [[ "${RUN_MCC}" -eq 0 && "${RUN_LIBC}" -eq 0 ]]; then
    fail "--mcc-only and --libc-only are mutually exclusive in spirit; pass neither for both"
fi

# ---- run ----------------------------------------------------------------

run_mcc() {
    log ">>> mcc: make ${TARGET}"
    (cd "${MCC_DIR}" && make "${TARGET}")
}

run_libc() {
    log ">>> meuos-libc: make ${TARGET}"
    (cd "${LIBC_DIR}" && make "${TARGET}")
}

if [[ "${RUN_ALL}" -eq 1 ]]; then
    # Sysroot-static mode: mcc + libc 都做 check,然后跑 mcc 自重编译。
    # 这个慢路径要 libelf / as / ld 等都就位,只在前置依赖齐了才能用。
    log "RUN_ALL mode: check + check-sysroot-static (slow, requires mt/as+ld built)"
    [[ "${RUN_MCC}"  -eq 1 ]] && run_mcc
    [[ "${RUN_LIBC}" -eq 1 ]] && run_libc
    [[ "${RUN_MCC}"  -eq 1 ]] && {
        log ">>> mcc: make check-sysroot-static"
        (cd "${MCC_DIR}" && make check-sysroot-static)
    }
    log "all PASS"
    exit 0
fi

# Default flow: mcc first (libc depends on it), libc second
if [[ "${RUN_MCC}" -eq 1 ]]; then
    run_mcc
fi
if [[ "${RUN_LIBC}" -eq 1 ]]; then
    run_libc
fi

log "PASS (mcc-libc bound group, target=${TARGET})"
