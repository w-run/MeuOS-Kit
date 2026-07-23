#!/bin/bash
# driver_daemon.sh - MeuOS Kit CI/CR 主驱动守护进程（入口）
#
# 由 Cron 或 Systemd Timer 每 5 分钟触发一次。每次运行处理一个"滴答"：
#   1. 启动自检：清理僵尸锁、探测余额、决定本轮大脑。
#   2. 状态机（SPEC §3.2）：
#      IDLE -> 扫描 .todo 待办 -> 大脑生成任务卡片 -> PLANNING
#      PLANNING -> task_executor.py 执行 -> EXECUTING
#      EXECUTING 成功 -> 验收通过 -> 更新 .todo + git commit -> IDLE
#      EXECUTING 失败 -> task_executor 内部 HOTFIX -> 成功/放弃
#      MILESTONE -> 子项目 .todo 全完成 -> milestone_reviewer.py -> REVIEWING
#
# 用法：./driver_daemon.sh [--once] [--dry-run]
#   --once    仅处理一个任务后退出（默认行为，适合 cron）
#   --dry-run 只扫描与规划，不执行

set -euo pipefail

# ============================================================
# 路径与配置
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CICR_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${CICR_ROOT}/.." && pwd)"
STATE="${CICR_ROOT}/state"
PLAN_DIR="${CICR_ROOT}/.plan"
CONFIG="${CICR_ROOT}/config"

PY="${PYTHON:-python3}"
TODAY="$(date -u +%Y-%m-%d)"
LOG_DIR="${CICR_ROOT}/logs/${TODAY}"
DRIVER_LOG="${LOG_DIR}/driver.log"

mkdir -p "${LOG_DIR}" "${PLAN_DIR}" "${STATE}/locks" "${STATE}/sessions"

ONCE=0
DRY_RUN=0
for arg in "$@"; do
    case "$arg" in
        --once)    ONCE=1 ;;
        --dry-run) DRY_RUN=1 ;;
    esac
done

# ============================================================
# 日志
# ============================================================

log() {
    local line="[driver $(date -u +%H:%M:%S)] $*"
    echo "$line" >&2
    echo "$line" >> "${DRIVER_LOG}"
}

die() { log "FATAL: $*"; exit 1; }

json_get() {  # json_get <file> <jq_path>  —— 用 python 提取，避免依赖 jq
    "${PY}" - "$1" "$2" <<'PYEOF'
import json, sys
try:
    with open(sys.argv[1]) as f:
        d = json.load(f)
    keys = sys.argv[2].split('.')
    for k in keys:
        d = d[k] if isinstance(d, dict) else d[int(k)]
    print(d if not isinstance(d, bool) else ('true' if d else 'false'))
except Exception:
    print('')
PYEOF
}

# ============================================================
# 工具函数
# ============================================================

# 在 worktree 中操作（SPEC 使用 Git Worktree 隔离）
current_branch() { git -C "${REPO_ROOT}" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "main"; }

has_pending_lock() { [[ -f "${STATE}/driver.lock" ]]; }

acquire_driver_lock() {
    if has_pending_lock; then
        local age
        age=$(( $(date +%s) - $(date -r "${STATE}/driver.lock" +%s 2>/dev/null || echo 0) ))
        if [[ ${age} -lt 1800 ]]; then
            log "driver.lock held (${age}s); skipping tick"
            return 1
        fi
        log "stale driver.lock (${age}s); reclaiming"
    fi
    echo $$ > "${STATE}/driver.lock"
    return 0
}

release_driver_lock() { rm -f "${STATE}/driver.lock"; }

# ============================================================
# Phase A：启动自检
# ============================================================

startup_selfcheck() {
    log "== 启动自检 =="

    # 1. 清理僵尸锁（>30min）
    "${PY}" "${SCRIPT_DIR}/recovery_cleaner.py" >> "${DRIVER_LOG}" 2>&1 || \
        log "recovery_cleaner returned $?"

    # 2. 探测余额
    "${PY}" "${SCRIPT_DIR}/balance_probe.py" \
        --out "${STATE}/balance_snapshots/latest.json" >> "${DRIVER_LOG}" 2>&1 || \
        log "balance_probe returned $? (brain may be unavailable)"

    # 3. 决定本轮大脑
    BAL_FILE="${STATE}/balance_snapshots/latest.json"
    if [[ ! -f "${BAL_FILE}" ]]; then
        BRAIN_AVAILABLE=false
        BRAIN_SELECTION=""
        DEGRADED=true
    else
        BRAIN_AVAILABLE=$("${PY}" - "${BAL_FILE}" <<'PYEOF'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
    print("true" if d.get("brain_available") else "false")
except Exception:
    print("true")  # 保守假设可用
PYEOF
        )
        BRAIN_SELECTION=$(json_get "${BAL_FILE}" "brain_selection")
        DEGRADED=$("${PY}" - "${BAL_FILE}" <<'PYEOF'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
    print("true" if d.get("degraded_single_task") else "false")
except Exception:
    print("false")
PYEOF
        )
    fi

    log "brain_available=${BRAIN_AVAILABLE} selection=${BRAIN_SELECTION} degraded=${DEGRADED}"
}

# ============================================================
# Phase B：规划（IDLE -> PLANNING）
# ============================================================

# 用大脑从 .todo 待办生成任务卡片
generate_task_card() {
    local task_id="$1" component="$2" name="$3" path="$4"
    local card_path="${PLAN_DIR}/$(echo "${task_id}" | tr ':' '_').json"

    # 已存在卡片则跳过
    if [[ -f "${card_path}" ]]; then
        log "card exists: ${card_path}"
        echo "${card_path}"
        return 0
    fi

    if [[ "${BRAIN_AVAILABLE}" != "true" ]]; then
        log "brain unavailable; skipping planning for ${task_id}"
        return 1
    fi

    # 降级时强制单任务：跳过生成新计划，仅执行已有卡片
    if [[ "${DEGRADED}" == "true" ]] && ls "${PLAN_DIR}"/*.json >/dev/null 2>&1; then
        log "degraded mode; not generating new plan"
        return 1
    fi

    local todo_content
    todo_content="$(cat "${REPO_ROOT}/${path}" 2>/dev/null | head -100)"

    local prompt
    prompt="$(cat <<EOF
你是 MeuOS Kit 的规划大脑。请为以下 .todo 任务生成一份 JSON 任务卡片，供轻量执行模型（Hy3）实现。

任务 ID: ${task_id}
组件: ${component}
分支: $(current_branch)
.todo 文件: ${path}

.todo 内容（前 100 行）:
${todo_content}

要求：
1. 严格遵守 AGENTS.md（零 GNU/glibc，直接 syscall()）。
2. 将任务拆解为 Hy3 可直接执行的最小变更：指定 files、task_type、spec。
3. task_type 取值：libc | compiler | toolchain | buildtools | generic。
4. 只输出 JSON，不要额外解释。schema:
{
  "task_id": "${task_id}",
  "title": "...",
  "task_type": "...",
  "component": "${component}",
  "branch": "$(current_branch)",
  "files": ["..."],
  "spec": "详细实现规格...",
  "acceptance_script": "libc_arch.sh | compiler_sanity.sh | common_checks.sh",
  "max_turns": 40
}
EOF
)"

    log "generating card for ${task_id} via brain (role=${BRAIN_SELECTION})"
    # 大脑规划：通过 providers.run_model() 按 BRAIN_SELECTION 角色调度
    # provider（codebuddy/codex/nvidia_nim），不再硬编码 codebuddy CLI。
    # 把 BRAIN_SELECTION 角色与 prompt 透传给 Python 子进程。
    local brain_rc=1
    CICR_TLS_INSECURE="${CICR_TLS_INSECURE:-}" \
    "${PY}" - "${card_path}" "${BRAIN_SELECTION}" "${prompt}" 2>>"${DRIVER_LOG}" <<'PYEOF'
import json, os, re, sys
sys.path.insert(0, '.ci-cr/scripts')
from providers import run_model

card_path, role_name, prompt = sys.argv[1], sys.argv[2], sys.argv[3]

if not role_name:
    sys.exit(1)
result = run_model(
    prompt, role_name=role_name, timeout=900,
    permission_mode="plan", max_turns=10,
    disallowed_tools=["Bash", "Write", "Edit"],
)
if result.rc == 127:
    sys.stderr.write(f"FATAL: provider not available for role={role_name}\n")
    sys.exit(1)
if result.rc != 0:
    sys.stderr.write(f"brain rc={result.rc}: {result.text[:200]}\n")
    sys.exit(1)
# 从 result.text 抽取首个 JSON 块
m = re.search(r'\{[\s\S]*\}', result.text)
if not m:
    sys.stderr.write(f"brain output has no JSON card: {result.text[:200]}\n")
    sys.exit(1)
try:
    card = json.loads(m.group(0))
except json.JSONDecodeError as e:
    sys.stderr.write(f"card JSON parse failed: {e}\n")
    sys.exit(1)
card.setdefault("task_id", card.get("task_id", "unknown"))
card.setdefault("task_type", "generic")
card.setdefault("max_turns", 40)
card.setdefault("component", card.get("component", "unknown"))
card.setdefault("branch", "main")
with open(card_path, "w") as f:
    json.dump(card, f, indent=2, ensure_ascii=False)
sys.exit(0)
PYEOF
    brain_rc=$?
    if [[ ${brain_rc} -eq 0 ]]; then
        log "card generated: ${card_path}"
        echo "${card_path}"
        return 0
    fi
    log "card generation failed for ${task_id}"
    return 1
}

# ============================================================
# Phase C：执行 + 验收（PLANNING -> EXECUTING）
# ============================================================

execute_task() {
    local card_path="$1"
    log "executing task card: ${card_path}"

    if [[ ${DRY_RUN} -eq 1 ]]; then
        log "DRY-RUN: skipping execution"
        return 1
    fi

    "${PY}" "${SCRIPT_DIR}/task_executor.py" --card "${card_path}" \
        >> "${DRIVER_LOG}" 2>&1
    local rc=$?
    log "task_executor rc=${rc}"

    # 0=成功 1=未完成/热修复未过 2=放弃 3=配置错误 127=无 codebuddy
    return ${rc}
}

# ============================================================
# Phase D：成功后处理（更新 .todo + git commit）
# ============================================================

on_task_success() {
    local task_id="$1" card_path="$2"
    log "task ${task_id} SUCCESS"

    # 更新 .todo 状态
    "${PY}" "${SCRIPT_DIR}/todo_scanner.py" done "${task_id}" >> "${DRIVER_LOG}" 2>&1 || true

    # git commit（仅提交源码，排除 .ci-cr 运行时状态）
    local branch
    branch="$(current_branch)"
    git -C "${REPO_ROOT}" add -A \
        ':!.ci-cr/state' ':!.ci-cr/.plan' ':!.ci-cr/logs' 2>/dev/null || true
    if ! git -C "${REPO_ROOT}" diff --cached --quiet 2>/dev/null; then
        git -C "${REPO_ROOT}" commit -m "ci-cr: ${task_id} (auto)" \
            >> "${DRIVER_LOG}" 2>&1 || log "git commit failed"
        log "committed on ${branch}"
    fi

    # 清理已完成卡片
    rm -f "${card_path}"
}

on_task_abandon() {
    local task_id="$1"
    log "task ${task_id} ABANDONED"
    "${PY}" "${SCRIPT_DIR}/todo_scanner.py" abandon "${task_id}" >> "${DRIVER_LOG}" 2>&1 || true
}

# ============================================================
# Phase E：里程碑检查（MILESTONE -> REVIEWING）
# ============================================================

check_milestones() {
    log "== 里程碑检查 =="
    local comp
    for comp_dir in "${REPO_ROOT}"/projects/*/; do
        comp="$(basename "${comp_dir}")"
        [[ -d "${comp_dir}.todo" ]] || continue
        local ready
        ready="$("${PY}" "${SCRIPT_DIR}/todo_scanner.py" milestone-ready "${comp}" 2>/dev/null \
                | "${PY}" -c "import json,sys; print(json.load(sys.stdin).get('ready',False))" 2>/dev/null || echo "False")"
        if [[ "${ready}" == "True" ]]; then
            log "milestone ready: ${comp}"
            if [[ ${DRY_RUN} -eq 0 ]]; then
                "${PY}" "${SCRIPT_DIR}/milestone_reviewer.py" \
                    --component "${comp}" --base main --apply \
                    >> "${DRIVER_LOG}" 2>&1 || \
                    log "milestone_reviewer returned $? for ${comp}"
            fi
        fi
    done
}

# ============================================================
# 主循环
# ============================================================

main() {
    log "==== driver tick start ===="
    if ! acquire_driver_lock; then
        exit 0
    fi

    trap release_driver_lock EXIT

    startup_selfcheck

    # 扫描待办任务
    local pending_json
    pending_json="$("${PY}" "${SCRIPT_DIR}/todo_scanner.py" pending 2>/dev/null || echo "[]")"
    local pending_count
    pending_count="$(echo "${pending_json}" | "${PY}" -c "import json,sys; print(len(json.load(sys.stdin)))" 2>/dev/null || echo 0)"
    log "pending tasks: ${pending_count}"

    # 优先执行已存在的规划卡片（可能来自上一轮中断）
    local card=""
    for f in "${PLAN_DIR}"/*.json; do
        [[ -f "$f" ]] || continue
        card="$f"
        break
    done

    # 若无现成卡片且有待办任务且大脑可用 -> 规划
    if [[ -z "${card}" && "${pending_count}" -gt 0 ]]; then
        local first
        first="$(echo "${pending_json}" | "${PY}" -c "
import json,sys
d=json.load(sys.stdin)
if d: print(d[0]['task_id'],d[0]['component'],d[0]['name'],d[0]['path'])
" 2>/dev/null)"
        if [[ -n "${first}" ]]; then
            # shellcheck disable=SC2086
            card="$(generate_task_card $first 2>/dev/null || true)"
        fi
    fi

    # 执行
    if [[ -n "${card}" ]]; then
        local task_id
        task_id="$("${PY}" -c "import json; print(json.load(open('${card}'))['task_id'])" 2>/dev/null || echo "")"
        execute_task "${card}"
        local rc=$?
        case ${rc} in
            0) on_task_success "${task_id}" "${card}" ;;
            2) on_task_abandon "${task_id}"; rm -f "${card}" ;;
            *) log "task ${task_id} incomplete (rc=${rc}); will retry next tick" ;;
        esac
    else
        log "no actionable task this tick"
    fi

    # 里程碑检查（每轮都查，成本低）
    check_milestones

    log "==== driver tick end ===="
}

main "$@"
