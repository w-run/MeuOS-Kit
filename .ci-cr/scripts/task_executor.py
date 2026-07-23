#!/usr/bin/env python3
# task_executor.py - 单任务卡片执行器（CodeBuddy CLI 集成 + 会话恢复）
#
# 职责（SPEC §3.2 + §4.2 + §4.3 + §5）：
#   1. 读取任务卡片 JSON（.plan/<task_id>.json 或 --card）。
#   2. 会话恢复：检查 state/sessions/mapping.json，有历史 session_id 则
#      codebuddy --resume；否则新建会话并捕获 session_id。
#   3. 按 settings.base.json 构建 codebuddy 命令（工具白/黑名单、权限、系统提示）。
#   4. 网络闪断：重试 3 次（指数退避），保留 session_id 等待下一轮驱动。
#   5. 验收：先 common_checks.sh（全局禁止检查），再任务卡片指定的验收脚本；
#      两步均 exit 0 才成功。
#   6. 失败 -> HOTFIX：截取验收 stderr 最后 3 行，调用紧急大脑生成 sed/patch，
#      应用后重试验收；重试 > max_hotfix_retries 则放弃（SPEC §7.3 禁止回溯）。
#
# 退出码：
#   0  任务成功（验收通过）
#   1  验收失败，已进入热修复但未通过
#   2  重试耗尽，任务已放弃（.todo 标记 [!]）
#   3  配置/参数错误

import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CICR_ROOT = os.path.dirname(HERE)
STATE = os.path.join(CICR_ROOT, "state")
ACCEPT = os.path.join(CICR_ROOT, "acceptance")
PLAN_DIR = os.path.join(CICR_ROOT, ".plan")

try:
    import yaml
except ImportError:
    yaml = None

# 统一 provider 调度（codebuddy / codex）
sys.path.insert(0, HERE)
from providers import run_model, get_role, load_model_router  # noqa: E402


def now_ts():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def today_log_dir():
    d = os.path.join(CICR_ROOT, "logs", time.strftime("%Y-%m-%d", time.gmtime()))
    os.makedirs(d, exist_ok=True)
    return d


def log(msg):
    line = f"[exec] {msg}"
    print(line, file=sys.stderr)
    with open(os.path.join(today_log_dir(), "exec_hy3.log"), "a") as f:
        f.write(line + "\n")


def load_json(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path) as f:
        return json.load(f)


def save_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


def load_settings():
    return load_json(os.path.join(CICR_ROOT, "config", "settings.base.json"), {})


def load_thresholds():
    path = os.path.join(CICR_ROOT, "config", "model_router.yaml")
    if yaml and os.path.exists(path):
        with open(path) as f:
            return (yaml.safe_load(f) or {}).get("thresholds", {}) or {}
    return {}


def load_manifest():
    return load_json(os.path.join(CICR_ROOT, "config", "acceptance_manifest.json"), {})


# --------------------------------------------------------------------------- #
# 会话管理
# --------------------------------------------------------------------------- #

SESSIONS_PATH = os.path.join(STATE, "sessions", "mapping.json")


def get_session(task_id):
    mapping = load_json(SESSIONS_PATH, {})
    entry = mapping.get(task_id)
    if isinstance(entry, dict):
        return entry.get("session_id")
    return None


def put_session(task_id, session_id):
    mapping = load_json(SESSIONS_PATH, {})
    mapping[task_id] = {
        "session_id": session_id,
        "created_ts": now_ts(),
        "updated_ts": now_ts(),
    }
    save_json(SESSIONS_PATH, mapping)


def update_session_ts(task_id):
    mapping = load_json(SESSIONS_PATH, {})
    if task_id in mapping and isinstance(mapping[task_id], dict):
        mapping[task_id]["updated_ts"] = now_ts()
        save_json(SESSIONS_PATH, mapping)


# --------------------------------------------------------------------------- #
# CodeBuddy 调用
# --------------------------------------------------------------------------- #

def build_cb_prompt(card):
    """将任务卡片转为给执行层（Hy3）的提示词。"""
    files = card.get("files", [])
    spec = card.get("spec", "")
    lines = [
        f"# 任务：{card.get('title', card.get('task_id', ''))}",
        "",
        f"组件：{card.get('component', 'unknown')}",
        f"分支：{card.get('branch', 'main')}",
        "",
        "## 必须遵守",
        "严格遵守 MeuOS Kit AGENTS.md（零 GNU/glibc，系统调用直接 syscall()，不引入 LLVM/Clang/GCC 代码）。",
        "只修改下方指定文件，禁止新增 glibc 专有符号。",
        "",
        "## 待修改文件",
    ]
    if files:
        lines += [f"- {f}" for f in files]
    else:
        lines.append("- （由任务卡片指定）")
    lines += ["", "## 实现规格", spec or "（见任务卡片 spec 字段）", ""]
    return "\n".join(lines)


def build_cb_cmd(card, settings, resume_sid=None):
    """构建 codebuddy 命令行。"""
    model = card.get("executor_model") or settings.get("model", "default-model")
    cmd = ["codebuddy", "-p", build_cb_prompt(card), "--output-format", "json",
           "--model", str(model)]
    # 工具限制
    allowed = card.get("allowedTools") or settings.get("allowedTools")
    if allowed:
        cmd += ["--allowedTools"] + list(allowed)
    disallowed = card.get("disallowedTools") or settings.get("disallowedTools")
    if disallowed:
        cmd += ["--disallowedTools"] + list(disallowed)
    # 权限模式
    pm = settings.get("permissionMode", "bypassPermissions")
    cmd += ["--permission-mode", pm]
    # 系统提示
    sp = card.get("systemPrompt") or settings.get("systemPrompt")
    if sp:
        cmd += ["--system-prompt", sp]
    # 轮次限制
    turns = str(card.get("max_turns", 40))
    cmd += ["--max-turns", turns]
    # 会话恢复
    if resume_sid:
        cmd += ["-r", resume_sid]
    return cmd


def run_codebuddy(card, settings, resume_sid=None, timeout=900):
    """执行 codebuddy，返回 (rc, session_id, raw_output)。
    解析 JSON 输出提取 session_id 与结果文本。"""
    cmd = build_cb_cmd(card, settings, resume_sid=resume_sid)
    log("codebuddy cmd: " + " ".join(cmd[:6]) + " ...")
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, resume_sid, ""
    out = p.stdout
    sid = resume_sid
    # 尝试从 JSON 提取 session_id
    try:
        data = json.loads(out)
        if isinstance(data, dict):
            sid = data.get("session_id") or sid
            result_text = data.get("result", "")
            subtype = data.get("subtype", "")
            # codebuddy 在 subtype=error 时仍可能返回 session_id
            if subtype == "error" and not result_text:
                log(f"codebuddy error subtype: {data.get('error', '')[:200]}")
    except json.JSONDecodeError:
        # 非严格 JSON（如鉴权失败提示），保留原文用于诊断
        pass
    return p.returncode, sid, out


def execute_with_retries(card, settings, thresholds):
    """SPEC §5：网络闪断重试 3 次（指数退避）。
    通过 providers.run_model() 按 executor 角色调度 codebuddy/codex。"""
    retries = int(thresholds.get("network_retry_count", 3))
    base = int(thresholds.get("network_backoff_base", 2))
    resume_sid = get_session(card["task_id"])
    prompt = build_cb_prompt(card)
    # 从 settings 提取工具限制与权限，透传给 provider
    extra = {}
    allowed = card.get("allowedTools") or settings.get("allowedTools")
    if allowed:
        extra["allowed_tools"] = list(allowed)
    disallowed = card.get("disallowedTools") or settings.get("disallowedTools")
    if disallowed:
        extra["disallowed_tools"] = list(disallowed)
    sp = card.get("systemPrompt") or settings.get("systemPrompt")
    if sp:
        extra["system_prompt"] = sp
    extra["permission_mode"] = settings.get("permissionMode", "bypassPermissions")
    extra["max_turns"] = int(card.get("max_turns", 40))

    last_rc = 1
    last_out = ""
    for attempt in range(1, retries + 1):
        result = run_model(prompt, role_name="executor", resume_sid=resume_sid,
                           timeout=900, **extra)
        sid = result.session_id
        if sid and sid != resume_sid:
            put_session(card["task_id"], sid)
            resume_sid = sid
        else:
            update_session_ts(card["task_id"])
        # 0 = 成功；124 = 超时（保留会话等下轮）；127 = 命令缺失（致命）
        if result.rc == 0:
            return 0, result.text
        if result.rc == 127:
            log(f"FATAL: provider command not found (role=executor)")
            return 127, result.text
        if attempt < retries:
            wait = base ** attempt
            log(f"attempt {attempt} rc={result.rc}; retrying in {wait}s")
            time.sleep(wait)
        last_rc = result.rc
        last_out = result.text
    return last_rc, last_out


# --------------------------------------------------------------------------- #
# 验收
# --------------------------------------------------------------------------- #

def run_acceptance(card, manifest):
    """SPEC §4.2：先 common_checks.sh，再任务指定脚本。两步均 exit 0 才成功。
    返回 (success, combined_stderr)。"""
    task_type = card.get("task_type", "generic")
    types = manifest.get("task_types", {})
    entry = types.get(task_type, manifest.get("default", {}))
    script_name = card.get("acceptance_script") or entry.get("script", "common_checks.sh")

    env = os.environ.copy()
    env["MEUOS_CICR_ROOT"] = CICR_ROOT
    env["MEUOS_TASK_ID"] = card.get("task_id", "")
    env["MEUOS_COMPONENT"] = card.get("component", "")
    env["MEUOS_BRANCH"] = card.get("branch", "main")

    stderr_parts = []

    for sname in ("common_checks.sh", script_name):
        spath = os.path.join(ACCEPT, sname)
        if not os.path.exists(spath):
            stderr_parts.append(f"[acceptance] missing script: {sname}")
            return False, "\n".join(stderr_parts)
        if not os.access(spath, os.X_OK):
            os.chmod(spath, 0o755)
        log(f"running acceptance: {sname}")
        p = subprocess.run([spath], capture_output=True, text=True, env=env, timeout=600)
        if p.returncode != 0:
            stderr_parts.append(f"--- {sname} FAILED (rc={p.returncode}) ---")
            stderr_parts.append(p.stderr[-2000:])
            return False, "\n".join(stderr_parts)
        # stdout 丢弃，stderr 截断保存
        stderr_parts.append(p.stderr[-500:] if p.stderr else "")
    return True, "\n".join(stderr_parts)


# --------------------------------------------------------------------------- #
# 热修复
# --------------------------------------------------------------------------- #

def hotfix(card, settings, thresholds, acceptance_err):
    """SPEC §3.2 HOTFIXING：截取验收 stderr 最后 3 行，调用紧急大脑生成补丁。
    返回 (success, patch_applied)。"""
    max_retries = int(thresholds.get("max_hotfix_retries", 3))
    err_tail = "\n".join((acceptance_err or "").splitlines()[-3:])
    template_path = os.path.join(CICR_ROOT, "templates", "fix_prompt.txt")
    tpl = ""
    if os.path.exists(template_path):
        with open(template_path) as f:
            tpl = f.read()

    counter_path = os.path.join(STATE, "retry_counters", card["task_id"] + ".json")
    counter = load_json(counter_path, {"count": 0, "history": []})

    for attempt in range(1, max_retries + 1):
        counter["count"] = attempt
        counter["history"].append({"attempt": attempt, "ts": now_ts(),
                                   "err_tail": err_tail[:300]})
        save_json(counter_path, counter)

        prompt = tpl.replace("{{TASK_ID}}", card.get("task_id", "")) \
                    .replace("{{COMPONENT}}", card.get("component", "")) \
                    .replace("{{ERR_TAIL}}", err_tail) \
                    .replace("{{FILES}}", ", ".join(card.get("files", [])))
        log(f"hotfix attempt {attempt}: calling emergency brain (brain_emergency)")
        # 按 brain_emergency 角色调度 provider（codebuddy 或 codex）
        result = run_model(prompt, role_name="brain_emergency",
                           timeout=600, permission_mode="acceptEdits",
                           max_turns=20)
        if result.rc == 127:
            log("FATAL: brain_emergency provider command not found")
            continue
        # 紧急大脑可能直接应用了编辑，重新验收
        ok, new_err = run_acceptance(card, load_manifest())
        if ok:
            return True, True
        err_tail = "\n".join((new_err or "").splitlines()[-3:])

    return False, False


# --------------------------------------------------------------------------- #
# 主流程
# --------------------------------------------------------------------------- #

def acquire_lock(task_id):
    lock_path = os.path.join(STATE, "locks", task_id + ".lock")
    if os.path.exists(lock_path):
        age = time.time() - os.path.getmtime(lock_path)
        ttl = load_thresholds().get("zombie_lock_minutes", 30) * 60
        if age < ttl:
            log(f"lock held (age {int(age)}s < {ttl}s); another executor running")
            return False
        log(f"stale lock (age {int(age)}s); reclaiming")
    save_json(lock_path, {"task_id": task_id, "ts": now_ts()})
    return True


def release_lock(task_id):
    lock_path = os.path.join(STATE, "locks", task_id + ".lock")
    if os.path.exists(lock_path):
        os.remove(lock_path)


def main():
    ap = argparse.ArgumentParser(description="MeuOS Kit CI/CR 单任务执行器")
    ap.add_argument("--card", help="任务卡片 JSON 路径（默认 .plan/<task_id>.json）")
    ap.add_argument("--task-id", help="任务 ID（自动定位 .plan/<id>.json）")
    ap.add_argument("--acceptance-only", action="store_true",
                    help="跳过执行，仅运行验收（用于调试）")
    args = ap.parse_args()

    card_path = args.card
    if not card_path:
        if not args.task_id:
            print("error: need --card or --task-id", file=sys.stderr)
            return 3
        card_path = os.path.join(PLAN_DIR, args.task_id + ".json")
    if not os.path.exists(card_path):
        print(f"error: task card not found: {card_path}", file=sys.stderr)
        return 3
    card = load_json(card_path)
    if not card or "task_id" not in card:
        print("error: invalid task card (missing task_id)", file=sys.stderr)
        return 3

    settings = load_settings()
    thresholds = load_thresholds()
    manifest = load_manifest()

    if not acquire_lock(card["task_id"]):
        return 1

    try:
        if not args.acceptance_only:
            rc, _ = execute_with_retries(card, settings, thresholds)
            if rc == 127:
                return 127
            if rc != 0:
                log(f"execution rc={rc}; session preserved for next round")
                # 网络问题导致执行未完成：保留会话，本轮不验收
                return 1

        # 验收
        ok, err = run_acceptance(card, manifest)
        if ok:
            log(f"task {card['task_id']} ACCEPTANCE PASS")
            return 0

        # 热修复
        log(f"task {card['task_id']} acceptance FAILED; entering HOTFIX")
        fixed, _ = hotfix(card, settings, thresholds, err)
        if fixed:
            log(f"task {card['task_id']} HOTFIX PASS")
            return 0

        # 耗尽重试 -> 放弃（SPEC §7.3 禁止回溯）
        log(f"task {card['task_id']} ABANDONED (retries exhausted)")
        abandon_path = os.path.join(STATE, "abandoned", card["task_id"] + ".json")
        save_json(abandon_path, {
            "task_id": card["task_id"], "ts": now_ts(),
            "reason": "hotfix retries exhausted",
            "err_tail": "\n".join((err or "").splitlines()[-3:]),
        })
        return 2
    finally:
        release_lock(card["task_id"])


if __name__ == "__main__":
    sys.exit(main())
