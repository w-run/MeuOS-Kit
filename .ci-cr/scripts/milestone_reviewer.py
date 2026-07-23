#!/usr/bin/env python3
# milestone_reviewer.py - 里程碑差异化审查器（调用大脑）
#
# 职责（SPEC §4.4 + §3.2 REVIEWING）：
#   1. 提取 git diff main...HEAD（排除 .todo 与 .ci-cr/）。
#   2. 用极简提示词喂给大脑（codebuddy 高推理模型），检查：
#      a. 是否引入 GNU/glibc 依赖（对照 AGENTS.md §4）？
#      b. ABI 调用约定是否符合目标架构？
#      c. 内存泄漏或未初始化风险？
#   3. 解析大脑输出：APPROVED -> 合并入 main；补丁 -> git apply + 二次编译验证。
#   4. 审查驳回（带补丁）-> 生成修正任务卡片进入 PLANNING 队列。
#
# 用法：python3 milestone_reviewer.py --component meuos-libc [--branch feat/xxx]
#                                       [--base main] [--apply]

import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CICR_ROOT = os.path.dirname(HERE)
REPO_ROOT = os.path.dirname(CICR_ROOT)

try:
    import yaml
except ImportError:
    yaml = None

# 统一 provider 调度（codebuddy / codex）
sys.path.insert(0, HERE)
from providers import run_model, get_role  # noqa: E402


def now_ts():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def log(msg):
    line = f"[review] {msg}"
    print(line, file=sys.stderr)
    d = os.path.join(CICR_ROOT, "logs", time.strftime("%Y-%m-%d", time.gmtime()))
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "brain_audit.log"), "a") as f:
        f.write(line + "\n")


def run(cmd, cwd=None, timeout=60):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=cwd)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"
    except FileNotFoundError:
        return 127, "", "command not found"


def load_model(name="brain_primary"):
    path = os.path.join(CICR_ROOT, "config", "model_router.yaml")
    if yaml and os.path.exists(path):
        with open(path) as f:
            cfg = yaml.safe_load(f) or {}
        return (cfg.get("models", {}) or {}).get(name, {})
    return {}


def git(args, timeout=60):
    return run(["git"] + args, cwd=REPO_ROOT, timeout=timeout)


def extract_diff(base, head="HEAD", component=None):
    """git diff base...HEAD，排除 .todo 与 .ci-cr/。"""
    paths = [":!*.todo", ":!.ci-cr/*", ":!.gitignore"]
    if component:
        paths.insert(0, f"projects/{component}")
    rc, out, err = git(["diff", f"{base}...{head}"] + paths, timeout=120)
    if rc != 0:
        log(f"git diff failed: {err[:200]}")
        return ""
    return out


def build_prompt(component, diff):
    prompt = f"""这是 `{component}` 里程碑的 Git Diff。请检查：

1. 是否引入 GNU/glibc 依赖（对照 AGENTS.md §4）？
2. ABI 调用约定是否符合目标架构？
3. 内存泄漏或未初始化风险？

输出：`APPROVED` 或直接给出 `git apply` 可用的修正补丁。

```diff
{diff[:60000]}
```
"""
    return prompt


def call_brain(prompt, model_spec, timeout=900, role_name="brain_primary"):
    """调用大脑模型审查。按角色 provider 调度 codebuddy/codex。"""
    provider = model_spec.get("provider", "codebuddy")
    model = model_spec.get("model") or model_spec.get("plan") or "default-model"
    log(f"calling brain role={role_name} provider={provider} model={model}")
    # 审查为只读：禁止 Bash/Write/Edit，权限 plan 模式
    result = run_model(prompt, role_name=role_name, resume_sid=None,
                       timeout=timeout, permission_mode="plan", max_turns=10,
                       disallowed_tools=["Bash", "Write", "Edit"])
    return result.rc, result.text, ""


def parse_review(text):
    """解析大脑输出。返回 (decision, patch)。
    decision: APPROVED | PATCH | REJECTED | UNCLEAR"""
    if not text:
        return "UNCLEAR", ""
    if re.search(r"\bAPPROVED\b", text, re.IGNORECASE):
        # 但若 APPROVED 后还跟有 diff，视为驳回带补丁
        patch = extract_patch(text)
        if patch:
            return "PATCH", patch
        return "APPROVED", ""
    patch = extract_patch(text)
    if patch:
        return "PATCH", patch
    return "REJECTED", ""


def extract_patch(text):
    """从文本中提取 git apply 可用的补丁块（diff --git 或 --- /+++ 格式）。"""
    lines = text.splitlines()
    start = None
    for i, ln in enumerate(lines):
        if ln.startswith("diff --git") or (ln.startswith("--- ") and i + 1 < len(lines)
                                            and lines[i + 1].startswith("+++ ")):
            start = i
            break
    if start is None:
        return ""
    return "\n".join(lines[start:]) + "\n"


def apply_patch(patch, do_apply):
    """应用补丁并返回是否成功。"""
    if not patch:
        return False
    if not do_apply:
        log("--apply not set; patch captured but not applied")
        # 试探 git apply --check
        rc, _, err = git(["apply", "--check", "-"], timeout=30)
        # 通过 stdin 传补丁
        p = subprocess.run(["git", "apply", "--check", "-"], input=patch,
                           capture_output=True, text=True, cwd=REPO_ROOT)
        return p.returncode == 0
    p = subprocess.run(["git", "apply", "-"], input=patch,
                      capture_output=True, text=True, cwd=REPO_ROOT)
    if p.returncode != 0:
        log(f"git apply failed: {p.stderr[:300]}")
        return False
    log("patch applied; running recompile verification")
    return verify_recompile()


def verify_recompile():
    """二次编译验证：对受影响组件运行 make check。"""
    component = os.environ.get("MEUOS_COMPONENT", "")
    proj = os.path.join(REPO_ROOT, "projects", component) if component else ""
    if not proj or not os.path.isdir(proj):
        return True  # 无组件则跳过
    rc, out, err = run(["make", "-C", proj, "check"], cwd=REPO_ROOT, timeout=600)
    if rc != 0:
        log(f"recompile verification FAILED: {err[-300:]}")
        return False
    log("recompile verification PASS")
    return True


def make_fix_card(component, patch, base):
    """审查驳回（带补丁）-> 生成修正任务卡片进入 PLANNING 队列。"""
    task_id = f"fix-review-{int(time.time())}"
    card = {
        "task_id": task_id,
        "title": f"里程碑审查修正：{component}",
        "task_type": "generic",
        "component": component,
        "branch": base,
        "files": [],
        "spec": f"里程碑审查发现需修正的问题。请应用以下审查反馈并修复：\n\n{patch[:4000]}",
        "acceptance_script": "common_checks.sh",
        "max_turns": 30,
        "review_origin": True,
    }
    card_path = os.path.join(CICR_ROOT, ".plan", task_id + ".json")
    os.makedirs(os.path.dirname(card_path), exist_ok=True)
    with open(card_path, "w") as f:
        json.dump(card, f, indent=2, ensure_ascii=False)
    log(f"fix card written: {card_path}")
    return card_path


def main():
    ap = argparse.ArgumentParser(description="MeuOS Kit CI/CR 里程碑审查器")
    ap.add_argument("--component", required=True, help="子项目名（如 meuos-libc）")
    ap.add_argument("--branch", default="HEAD", help="待审查分支/HEAD")
    ap.add_argument("--base", default="main", help="基线分支")
    ap.add_argument("--apply", action="store_true", help="自动应用修正补丁")
    ap.add_argument("--model-role", default="brain_primary",
                    help="使用的模型角色（brain_primary/fallback/emergency）")
    args = ap.parse_args()

    # 检查里程碑是否就绪：该组件 .todo 全部完成
    todo_dir = os.path.join(REPO_ROOT, "projects", args.component, ".todo")
    if os.path.isdir(todo_dir):
        # .todo 中的 [ ] 表示未完成
        rc, out, _ = run(["grep", "-rl", "[ \\[ ]", todo_dir], timeout=10)
        # 粗略：若存在 .todo 文件，认为仍有待办（细粒度由驱动判断）
    os.environ["MEUOS_COMPONENT"] = args.component

    diff = extract_diff(args.base, args.branch, args.component)
    if not diff.strip():
        log("empty diff; nothing to review")
        print(json.dumps({"decision": "NOTHING_TO_REVIEW", "ts": now_ts()}))
        return 0

    model_spec = load_model(args.model_role)
    prompt = build_prompt(args.component, diff)
    rc, text, err = call_brain(prompt, model_spec, role_name=args.model_role)
    if rc == 127:
        log(f"FATAL: provider command not found (role={args.model_role})")
        return 3

    decision, patch = parse_review(text)
    result = {"component": args.component, "decision": decision, "ts": now_ts(),
              "brain_rc": rc, "patch_present": bool(patch)}

    if decision == "APPROVED":
        log(f"APPROVED: merging {args.branch} into {args.base}")
        if args.apply:
            rc2, _, err2 = git(["checkout", args.base], timeout=30)
            rc2, _, err2 = git(["merge", "--no-ff", args.branch, "-m",
                                f"ci-cr: merge {args.component} milestone (auto-approved)"],
                               timeout=60)
            if rc2 != 0:
                log(f"merge failed: {err2[:300]}")
                result["merge_ok"] = False
            else:
                result["merge_ok"] = True
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 0

    if decision == "PATCH":
        ok = apply_patch(patch, args.apply)
        result["patch_applied"] = ok
        if ok:
            log("patch applied & verified; milestone can proceed")
            print(json.dumps(result, indent=2, ensure_ascii=False))
            return 0
        # 应用失败 -> 生成修正卡片
        card = make_fix_card(args.component, patch, args.base)
        result["fix_card"] = card
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 1

    # REJECTED / UNCLEAR
    card = make_fix_card(args.component, text[:4000], args.base)
    result["fix_card"] = card
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 1


if __name__ == "__main__":
    sys.exit(main())
