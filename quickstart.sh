#!/bin/bash
# quickstart.sh - 通用子项目快速启动入口
#
# 动态扫描 projects/*/ 下的子项目,任意一个都可以作为参数。**不**硬编码
# 子项目名字(除一个"绑定组别名"列表外),所以新增子项目无需改本脚本。
#
# 绑定组 (mcc + meuos-libc) 是基本绑定的,转发到 ./mcc-libc.sh 处理;
# 其他子项目独立处理。
#
# 用法:
#   ./quickstart.sh                       # 列出所有子项目
#   ./quickstart.sh <sub> <target>        # 跑 <sub> 的 make <target>
#   ./quickstart.sh <sub> -- <make-flags> # 透传 make 自己的 flag (e.g. -j4 -n)
#   ./quickstart.sh <sub> check           # 常用:跑 make check
#   ./quickstart.sh --all check           # 跑所有子项目 make check
#   ./quickstart.sh --list                # 只列名,不执行
#   ./quickstart.sh --help
#
# 参数解析规则:
#   - 第一个非 flag 参数是 <sub> 名字
#   - 第二个及之后的非 flag 参数,以及 `--` 之后的所有参数,都传给 make
#   - flag (以 - 或 -- 开头) 只接受: --help, --list, --all
#   - 透传给 make 用 `--` 分隔:`./quickstart.sh meow -- -j4 check`
#   - target 必须显式指定 (无默认值,避免误操作如跑 `make check clean`)
#
# 任意 make 错误会让脚本立即退出 (set -e); --all 模式下,个别子项目失败
# 不会中断,会收集失败列表在最后报告。
#
# 注:子项目根目录的判断只看 projects/<name>/Makefile 是否存在,所以可以
# 动态支持任何未来新增的子项目。

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECTS_DIR="${REPO_ROOT}/projects"

# ---- 绑定组别名 (除了这里,没有其他地方硬编码子项目名) ---------------
# 含义:这些"别名"参数会被转发到 ./mcc-libc.sh。这是脚本里唯一允许硬编码
# 子项目名的地方,理由是 mcc 和 meuos-libc 在自举链上紧耦合,统一入口。
declare -A BOUND_ALIAS=(
    ["mcc-libc"]="mcc-libc.sh"
    ["mcc+libc"]="mcc-libc.sh"
)

# ---- helpers ------------------------------------------------------------

log()  { printf '[quickstart] %s\n' "$*" >&2; }
fail() { printf '[quickstart][FATAL] %s\n' "$*" >&2; exit 1; }

usage() {
    sed -n '3,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

# 列出所有 projects/<name>/ 下含 Makefile 的子项目,按字典序输出到 stdout
list_subprojects() {
    local d
    for d in "${PROJECTS_DIR}"/*/; do
        [[ -d "$d" ]] || continue
        [[ -f "${d}/Makefile" ]] || continue
        basename "$d"
    done | sort
}

# 校验子项目存在 + 有 Makefile
validate_subproject() {
    local sub="$1"
    local d="${PROJECTS_DIR}/${sub}"
    [[ -d "$d" ]]            || fail "subproject not found: ${sub} (try --list)"
    [[ -f "${d}/Makefile" ]] || fail "subproject ${sub} has no Makefile"
    printf '%s' "$d"
}

# 跑一个子项目 (cd + make $@)
run_subproject() {
    local sub="$1"; shift
    local dir
    dir="$(validate_subproject "$sub")"
    log ">>> ${sub}: make $*"
    (cd "$dir" && make "$@")
}

# ---- arg parse ----------------------------------------------------------

if [[ $# -eq 0 ]]; then
    echo "Available subprojects (auto-discovered from projects/*/):"
    list_subprojects | sed 's/^/  - /'
    echo ""
    echo "Bound aliases:"
    for k in "${!BOUND_ALIAS[@]}"; do echo "  - $k (-> ${BOUND_ALIAS[$k]})"; done | sort
    echo ""
    echo "Run with no args for help; pass --help for full usage."
    exit 0
fi

ACTION="run"        # run | list | all
SUBS=()
MAKE_ARGS=()       # 默认空,要求显式 target

# Two-pass parsing: the first positional arg decides whether the rest
# should be passed to a bound-alias forwarder (which has its own flags)
# or to make (default). We detect a bound alias by peeking at $1.
FIRST_POSITIONAL=""
for arg in "$@"; do
    if [[ "$arg" == --* || "$arg" == -* ]]; then
        continue
    fi
    FIRST_POSITIONAL="$arg"
    break
done

if [[ -n "$FIRST_POSITIONAL" && -n "${BOUND_ALIAS[$FIRST_POSITIONAL]:-}" ]]; then
    # Bound-alias mode: forward all args as-is to the bound script.
    SUBS=("$FIRST_POSITIONAL")
    ACTION="run"
    MAKE_ARGS=("$@")
    # drop the alias itself
    MAKE_ARGS=("${MAKE_ARGS[@]:1}")
else
    # Standard mode. Args after the sub name are passed straight to make.
    # Use `--` to separate if you need to pass flags to make (e.g. -j4).
    # Examples:
    #   ./quickstart.sh meow check          → make check
    #   ./quickstart.sh meow -- -j4 check   → make -j4 check
    #   ./quickstart.sh meow clean          → make clean
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help|-h)   usage ;;
            --list)      ACTION="list"; shift ;;
            --all)       ACTION="all"; shift ;;
            --)          shift; MAKE_ARGS+=("$@"); break ;;
            -*)          fail "unknown flag: $1 (use '--' to pass flags to make, e.g. 'meow -- -j4 check')" ;;
            *)
                if [[ ${#SUBS[@]} -eq 0 && "$ACTION" == "run" ]]; then
                    SUBS=("$1")
                else
                    MAKE_ARGS+=("$1")
                fi
                shift
                ;;
        esac
    done
fi

# ---- dispatch -----------------------------------------------------------

case "$ACTION" in
    list)
        log "subprojects:"
        list_subprojects | sed 's/^/  /'
        log "bound aliases:"
        for k in "${!BOUND_ALIAS[@]}"; do echo "  $k -> ${BOUND_ALIAS[$k]}"; done | sort | sed 's/^/  /'
        ;;

    run)
        [[ ${#SUBS[@]} -eq 1 ]] || fail "run mode takes exactly one subproject (got ${#SUBS[@]}: ${SUBS[*]:-})"
        sub="${SUBS[0]}"
        # 绑定组别名转发 — **所有**剩余参数原样转发,quickstart.sh 不再解释
        # (避免 ./quickstart.sh mcc-libc --help 被 quickstart 误解析为自身 --help)
        if [[ -n "${BOUND_ALIAS[$sub]:-}" ]]; then
            log "bound alias '$sub' -> ${BOUND_ALIAS[$sub]} (forwarding all args)"
            exec "${REPO_ROOT}/${BOUND_ALIAS[$sub]}" "${MAKE_ARGS[@]}"
        fi
        [[ ${#MAKE_ARGS[@]} -ge 1 ]] || fail "no make target specified (e.g. 'meow check', or 'meow -- -j4 check')"
        run_subproject "$sub" "${MAKE_ARGS[@]}"
        log "PASS (${sub}, make ${MAKE_ARGS[*]})"
        ;;

    all)
        [[ ${#MAKE_ARGS[@]} -ge 1 ]] || fail "no make target specified for --all (e.g. '--all check')"
        log "running all subprojects (make ${MAKE_ARGS[*]})"
        # 收集失败但不中断,最后报告
        failed=()
        passed=()
        for sub in $(list_subprojects); do
            if run_subproject "$sub" "${MAKE_ARGS[@]}"; then
                passed+=("$sub")
            else
                failed+=("$sub")
            fi
        done
        log "all-mode summary: passed=${#passed[@]} failed=${#failed[@]}"
        if [[ ${#passed[@]} -gt 0 ]]; then
            log "  passed: ${passed[*]}"
        fi
        if [[ ${#failed[@]} -gt 0 ]]; then
            log "  failed: ${failed[*]}"
            exit 1
        fi
        ;;

    *)
        fail "internal: unknown action '$ACTION'"
        ;;
esac
