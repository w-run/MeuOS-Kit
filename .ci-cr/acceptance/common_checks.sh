#!/bin/bash
# common_checks.sh - 全局禁止检查（SPEC §4.2 第 1 步）
#
# 检查内容：
#   1. 源码树中无新增 glibc/GNU 依赖符号（对照 AGENTS.md §4）。
#   2. 禁止引入 LLVM/Clang/GCC 代码。
#   3. meuos-libc 核心无 glibc 专有符号泄漏到标准符号。
#
# 环境变量（由 task_executor 注入）：
#   MEUOS_CICR_ROOT, MEUOS_TASK_ID, MEUOS_COMPONENT, MEUOS_BRANCH
#
# 退出码：0=通过，1=发现违规

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPONENT="${MEUOS_COMPONENT:-}"

log() { printf '[common_checks] %s\n' "$*" >&2; }
fail() { printf '[common_checks][FAIL] %s\n' "$*" >&2; exit 1; }

# 扫描范围：projects/*/src，排除 reference/（只读参考树）
SCAN_DIRS=()
for d in "${REPO_ROOT}"/projects/*/src; do
    [[ -d "$d" ]] && SCAN_DIRS+=("$d")
done
[[ ${#SCAN_DIRS[@]} -eq 0 ]] && { log "no src dirs to scan; pass"; exit 0; }

# 排除构建产物/暂存目录与 compat 独立归档层（SPEC §2.1：compat 独立归档）
EXCLUDES=(--exclude-dir=compat --exclude-dir=sysroot --exclude-dir=build \
          --exclude-dir=out --exclude-dir=.git --exclude-dir=reference)

# 1. 禁止 glibc/GNU 依赖（排除注释中的合理引用与 reference/）
log "scanning for glibc/GNU dependencies in ${SCAN_DIRS[*]}"

# 允许出现 GNU 的合法场景：版权声明、兼容性说明、AGENTS 引用。
# 违规场景：#include <glibc/...>、直接调用 glibc 内部符号、链接 -lglibc 等。
violations=$(grep -rnE "${EXCLUDES[@]}" \
    -e '#include\s*<glibc' \
    -e '-lglibc\b' \
    -e '-lgnu\b' \
    -e '__glibc_' \
    -e '__GNU_' \
    "${SCAN_DIRS[@]}" 2>/dev/null || true)

if [[ -n "${violations}" ]]; then
    fail "glibc/GNU dependency violations found:
${violations}"
fi

# 2. 禁止引入 LLVM/Clang/GCC 代码
log "scanning for LLVM/Clang/GCC code"
clang_violations=$(grep -rnE "${EXCLUDES[@]}" \
    -e '#include\s*<clang/' \
    -e '#include\s*"clang/' \
    -e '#include\s*<llvm/' \
    -e '#include\s*"llvm/' \
    -e '#include\s*<gcc/' \
    "${SCAN_DIRS[@]}" 2>/dev/null || true)

if [[ -n "${clang_violations}" ]]; then
    fail "LLVM/Clang/GCC code violations found:
${clang_violations}"
fi

# 3. meuos-libc 核心不含 compat 层符号（compat 应独立归档）
LIBC_SRC="${REPO_ROOT}/projects/meuos-libc/src"
if [[ -d "${LIBC_SRC}" ]]; then
    log "checking meuos-libc core for compat-layer symbol leakage"
    # compat 符号应在 src/compat/ 内，不应出现在核心源码
    # getline 是 POSIX 标准函数，允许在核心；只拦截 error_at_line/obstack/argp
    core_compat=$(grep -rnE "${EXCLUDES[@]}" \
        -e 'error_at_line|obstack|argp_parse|__argp' \
        "${LIBC_SRC}" 2>/dev/null || true)
    if [[ -n "${core_compat}" ]]; then
        fail "glibc-compat symbols leaked into libc core:
${core_compat}"
    fi
fi

# 4. 构建可重现检查：无绝对路径硬编码（排除 __FILE__ 调试与 reference）
log "checking for hardcoded absolute paths"
abs_paths=$(grep -rnE "${EXCLUDES[@]}" -e '"/(usr|home|tmp|opt|var)/' \
    "${SCAN_DIRS[@]}" 2>/dev/null \
    | grep -vE 'reference|/dev/null|/proc/|/sys/|__FILE__' || true)
if [[ -n "${abs_paths}" ]]; then
    # 警告但不直接失败（部分测试 fixture 可能需要绝对路径）
    log "WARNING: hardcoded absolute paths detected (review needed):
${abs_paths}" >&2
fi

log "all common checks PASS"
exit 0
