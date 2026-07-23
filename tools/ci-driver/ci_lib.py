"""Shared helpers for ci_driver.

Exposes:
- Paths: DRIVER_DIR, PROMPTS_DIR, STATE_DIR, LOGS_DIR
- load_config / save_config
- parse_todos: scan <project>/projects/<sub>/.todo/*.md and extract metadata
- list_todos: filter by subproject, status, priority
- Todo dataclass
- call_codebuddy: invoke codebuddy CLI as subprocess, return parsed JSON
- log: append to log file with ts
"""

from __future__ import annotations

import dataclasses
import datetime as _dt
import json
import os
import re
import subprocess
import sys
import uuid
from pathlib import Path
from typing import Any, Iterable

DRIVER_DIR = Path(__file__).resolve().parent
PROMPTS_DIR = DRIVER_DIR / "prompts"
STATE_DIR = DRIVER_DIR / "state"
LOGS_DIR = DRIVER_DIR / "logs"
MCP_CONFIG_PATH = DRIVER_DIR / "mcp_config.json"

PRIORITY_RANK = {"P0": 0, "P1": 1, "P2": 2, "P3": 3, "P4": 4, "P5": 5}


@dataclasses.dataclass
class Todo:
    subproject: str           # e.g. "mcc"
    path: Path                # absolute path to .todo file
    rel_path: str             # relative to project root
    name: str                 # basename without .md
    priority: str             # P0..P5, "" if missing
    status: str               # "pending" | "in_progress" | "done" | ""
    title: str                # first H1/H2 line
    raw_head: str             # raw <!-- ... --> block (for context)
    priority_rank: int        # numeric rank for sorting (lowest first)

    def is_actionable(self) -> bool:
        # in_progress is also actionable: a previous round may have crashed
        # mid-batch and we want to resume. Only `done` is terminal.
        return self.status != "done" and self.priority in PRIORITY_RANK


_FRONT_RE = re.compile(r"<!--\s*(.*?)\s*-->", re.DOTALL)
_H1_RE = re.compile(r"^#\s+(.+?)\s*$", re.MULTILINE)
_H2_RE = re.compile(r"^##\s+(.+?)\s*$", re.MULTILINE)
_H3_RE = re.compile(r"^###\s+(.+?)\s*$", re.MULTILINE)
_KV_RE = re.compile(r"^\s*([A-Za-z_]+)\s*:\s*(.+?)\s*$")
# Match fenced code blocks ``` ... ``` (any language tag). We extract
# everything inside the fences as candidate shell commands.
_FENCE_RE = re.compile(r"```[a-zA-Z0-9_+\-]*\n(.*?)```", re.DOTALL)
# Match inline code `...` containing shell-like content.
_INLINE_CODE_RE = re.compile(r"`([^`\n]+)`")


def _parse_front(blob: str) -> tuple[dict, str]:
    m = _FRONT_RE.match(blob)
    if not m:
        return {}, ""
    block = m.group(1)
    kv: dict = {}
    for line in block.splitlines():
        mm = _KV_RE.match(line)
        if mm:
            kv[mm.group(1).strip().lower()] = mm.group(2).strip()
    return kv, block


def _first_title(text: str) -> str:
    m = _H1_RE.search(text)
    return m.group(1).strip() if m else ""


def parse_todo(path: Path, project_root: Path, subproject: str) -> Todo | None:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    kv, _ = _parse_front(text)
    priority = kv.get("priority", "").upper()
    status = kv.get("status", "").lower()
    if priority and priority not in PRIORITY_RANK:
        return None
    if status and status not in ("pending", "in_progress", "done"):
        status = ""
    return Todo(
        subproject=subproject,
        path=path,
        rel_path=str(path.relative_to(project_root)),
        name=path.stem,
        priority=priority,
        status=status,
        title=_first_title(text),
        raw_head=json.dumps(kv, ensure_ascii=False, sort_keys=True),
        priority_rank=PRIORITY_RANK.get(priority, 99),
    )


def list_todos(
    project_root: Path,
    subprojects: Iterable[str] | None = None,
    *,
    include_done: bool = False,
) -> list[Todo]:
    """Scan <project>/projects/<sub>/.todo/*.md and return todos.

    If `subprojects` is given, restrict to those subdirectories. Empty / None
    means "all subprojects that have a .todo directory".
    """
    out: list[Todo] = []
    projects_dir = project_root / "projects"
    if not projects_dir.is_dir():
        return out
    candidates: list[Path]
    if subprojects:
        candidates = [projects_dir / sp for sp in subprojects]
    else:
        candidates = [p for p in projects_dir.iterdir() if p.is_dir()]
    for sub in candidates:
        todo_dir = sub / ".todo"
        if not todo_dir.is_dir():
            continue
        for f in sorted(todo_dir.glob("*.md")):
            # Skip archived todos (under .todo/archive/). Active todo
            # files live at the .todo/ top level only.
            if "archive" in f.relative_to(todo_dir).parts:
                continue
            t = parse_todo(f, project_root, sub.name)
            if t is None:
                continue
            if not include_done and t.status == "done":
                continue
            out.append(t)
    out.sort(key=lambda t: (t.priority_rank, t.subproject, t.name))
    return out


def dump_todo_priority(
    project_root: Path,
    subprojects: Iterable[str] | None = None,
    out_path: Path | None = None,
    *,
    include_done: bool = False,
) -> Path:
    """Write a sorted, priority-grouped JSON manifest of actionable todos.

    The main session can `Read` or `Grep` this file to see the queue at
    a glance (without driver having to inline it into every prompt). The
    manifest is overwritten on every call, so a stale file is impossible.

    Returns the path written. Default location: <state>/todo_priority.json.
    """
    todos = list_todos(project_root, subprojects, include_done=include_done)
    groups: dict[str, list[dict]] = {}
    for t in todos:
        p = t.priority or "P?"
        groups.setdefault(p, []).append({
            "subproject": t.subproject,
            "name": t.name,
            "rel_path": t.rel_path,
            "status": t.status or "pending",
            "title": t.title,
            "actionable": t.is_actionable(),
            "priority_rank": t.priority_rank,
        })
    ordered = sorted(groups.items(), key=lambda kv: PRIORITY_RANK.get(kv[0], 99))
    payload = {
        "generated_at": now_ts(),
        "project_root": str(project_root),
        "subprojects": list(subprojects) if subprojects else "ALL",
        "include_done": include_done,
        "by_priority": [
            {"priority": p, "count": len(items), "todos": items}
            for p, items in ordered
        ],
        "flat_order": [
            f"{t.subproject}/{t.name} [{t.priority or 'P?'}]"
            for t in todos
        ],
    }
    if out_path is None:
        ensure_dirs()
        out_path = STATE_DIR / "todo_priority.json"
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False),
                        encoding="utf-8")
    return out_path


def now_ts() -> str:
    return _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def ensure_dirs() -> None:
    for d in (STATE_DIR, LOGS_DIR):
        d.mkdir(parents=True, exist_ok=True)


def log(msg: str, *, name: str = "ci_driver") -> None:
    ensure_dirs()
    line = f"[{now_ts()}] {msg}\n"
    fp = LOGS_DIR / f"{name}.log"
    with fp.open("a", encoding="utf-8") as f:
        f.write(line)


# ---- state file (single JSON, atomic write) ----

def _atomic_write(path: Path, obj: Any) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, indent=2, ensure_ascii=False), encoding="utf-8")
    os.replace(tmp, path)


def load_state(name: str, default: dict | None = None) -> dict:
    ensure_dirs()
    fp = STATE_DIR / f"{name}.json"
    if not fp.exists():
        return dict(default or {})
    try:
        return json.loads(fp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return dict(default or {})


def save_state(name: str, obj: dict) -> None:
    ensure_dirs()
    _atomic_write(STATE_DIR / f"{name}.json", obj)


# ---- codebuddy invocation ----

# Model names are *config*, not constants: a user can switch providers or
# models without touching this file. Resolution order for the main session:
#   1. explicit --main-model / --sub-model argv
#   2. env CI_DRIVER_MAIN_MODEL / CI_DRIVER_SUB_MODEL
#   3. codebuddy's own default (whatever `codebuddy` resolves when --model is
#      omitted, e.g. the user's `codebuddy config get model` setting)
#
# The fallback value of "" means "do not pass --model; let codebuddy pick".
# The prompt templates also avoid hard-coding model names; the driver injects
# the actual values into the system prompt per session.

DEFAULT_MODEL_MAIN = ""  # resolved at call time: env > argv > codebuddy default
DEFAULT_MODEL_SUB = ""


def resolve_main_model(argv_value: str | None = None) -> str:
    return (argv_value
            or os.environ.get("CI_DRIVER_MAIN_MODEL", "")
            or os.environ.get("CI_DRIVER_MODEL", ""))


def resolve_sub_model(argv_value: str | None = None) -> str:
    return (argv_value
            or os.environ.get("CI_DRIVER_SUB_MODEL", "")
            or os.environ.get("CI_DRIVER_MODEL", ""))


def call_codebuddy(
    prompt: str,
    *,
    model: str,
    workdir: Path,
    append_system_prompt: str = "",
    allowed_tools: list[str] | None = None,
    resume: str | None = None,
    continue_recent: bool = False,
    session_id: str | None = None,
    timeout_s: int = 1800,
    extra_args: list[str] | None = None,
    use_mcp: bool = False,
    mcp_config: Path | None = None,
) -> dict:
    """Invoke codebuddy in headless -p mode and return parsed JSON output.

    Returns dict with keys: ok, exit_code, result, session_id, cost, raw, stderr.
    """
    args: list[str] = ["codebuddy", "-p", prompt, "--output-format", "json"]
    # Only pass --model if a name was explicitly resolved; otherwise let
    # codebuddy use its configured default (user's `codebuddy config get model`).
    if model:
        args += ["--model", model]
    args += ["-y"]
    if append_system_prompt:
        args += ["--append-system-prompt", append_system_prompt]
    if allowed_tools:
        args += ["--allowedTools", ",".join(allowed_tools)]
    if resume:
        args += ["--resume", resume]
    if continue_recent:
        args += ["--continue"]
    if session_id:
        args += ["--session-id", session_id]
    if use_mcp and mcp_config:
        args += ["--mcp-config", str(mcp_config), "--strict-mcp-config"]
    if extra_args:
        args += list(extra_args)

    # --add-dir for the project root, so sub-agent can read everything
    args += ["--add-dir", str(workdir)]

    env = os.environ.copy()
    env.setdefault("PAGER", "cat")
    env["CI_DRIVER_RUN"] = "1"

    try:
        proc = subprocess.run(
            args,
            cwd=str(workdir),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout_s,
        )
    except subprocess.TimeoutExpired as e:
        return {"ok": False, "exit_code": -1, "result": "",
                "session_id": "", "raw": "", "stderr": f"timeout: {e}"}

    out = proc.stdout
    obj: dict = {}
    # codebuddy --output-format json emits a JSON array, one entry per turn;
    # the final element carries `result`, `session_id`, `is_error`, etc.
    try:
        parsed = json.loads(out)
        if isinstance(parsed, list) and parsed:
            last = parsed[-1]
            if isinstance(last, dict):
                obj = last
        elif isinstance(parsed, dict):
            obj = parsed
    except json.JSONDecodeError:
        # fall back: pick the last line that looks like a JSON object
        for line in reversed(out.splitlines()):
            line = line.strip().rstrip(",")
            if line.startswith("{") and line.endswith("}"):
                try:
                    cand = json.loads(line)
                    if isinstance(cand, dict):
                        obj = cand
                        break
                except json.JSONDecodeError:
                    continue

    return {
        "ok": proc.returncode == 0 and not obj.get("is_error", False),
        "exit_code": proc.returncode,
        "result": obj.get("result") or obj.get("text") or "",
        "session_id": obj.get("session_id") or obj.get("sessionId") or "",
        "is_error": bool(obj.get("is_error", False)),
        "cost": obj.get("total_cost_usd") or 0.0,
        "raw": out,
        "stderr": proc.stderr,
        "structured": obj,
    }


def gen_session_id() -> str:
    return str(uuid.uuid4())


# ---- todo state mutation ----

# Forbidden: words that should NOT appear in a `note:` field. They
# mislead the LLM into thinking the todo is partly-done or done.
# `note:` is meant to describe the *content* of the todo, not its
# completion state. Completion state belongs in `status` + `progress_note`.
_NOTE_FORBIDDEN_RE = re.compile(
    r"已完成|已验证|全绿|全过|全 PASS|done|verified|completed|passed"
    r"|partial|部分完成|暂不阻塞|可立即开始|已先前完成|全部就位|已修复"
    r"|核心编码完成|核心完成",
    re.IGNORECASE,
)


def scan_note_warnings(todo: Todo) -> list[str]:
    """Return list of warning strings if `note:` contains completion-style
    language. Empty list = clean. Called by the audit script; the main
    session can also call it via a tool if needed.
    """
    text = todo.path.read_text(encoding="utf-8", errors="replace")
    kv, _ = _parse_front(text)
    note = kv.get("note", "")
    if not note:
        return []
    warnings: list[str] = []
    for m in _NOTE_FORBIDDEN_RE.finditer(note):
        warnings.append(f"note contains '{m.group(0)}'")
    return warnings


def extract_acceptance_cmds(todo: Todo) -> list[str]:
    """Extract the list of acceptance commands from a todo's body.

    The todo file MUST contain a `## 验收标准` (or English equivalent)
    H2 section. Inside that section, every fenced code block's content
    is treated as one shell command (commands joined with newlines are
    split into separate commands). If no acceptance section is found,
    the function returns an empty list — the caller should treat that
    as "todo has no programmatic acceptance, refuse to mark done".

    Recognized H2 titles (case-insensitive, also matches English):
      - 验收标准
      - Acceptance Criteria
      - Acceptance
      - Verify

    Returns a list of individual shell commands (one per line of a
    fenced block, trimmed). Empty lines and `#` comments are dropped.
    """
    try:
        text = todo.path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    # Find the start of the acceptance section
    section_start: int | None = None
    section_title: str = ""
    for m in _H2_RE.finditer(text):
        title = m.group(1).strip().lower()
        if title in ("验收标准", "acceptance criteria", "acceptance",
                     "verify", "verification"):
            section_start = m.end()
            section_title = m.group(1).strip()
            break
    if section_start is None:
        return []
    # The section ends at the next H2 (or H1, or EOF)
    section_end = len(text)
    for m in _H2_RE.finditer(text, section_start):
        section_end = m.start()
        break
    section_text = text[section_start:section_end]

    cmds: list[str] = []
    for fb in _FENCE_RE.finditer(section_text):
        body = fb.group(1)
        for line in body.splitlines():
            line = line.rstrip()
            if not line.strip():
                continue
            if line.lstrip().startswith("#"):
                continue
            cmds.append(line)
    return cmds


def has_acceptance_section(todo: Todo) -> bool:
    """True if the todo has any 验收标准 / Acceptance section at all."""
    try:
        text = todo.path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    for m in _H2_RE.finditer(text):
        title = m.group(1).strip().lower()
        if title in ("验收标准", "acceptance criteria", "acceptance",
                     "verify", "verification"):
            return True
    return False


def mark_todo_done_by_driver(todo: Todo, *, note: str = "") -> None:
    """Set status: done — **driver only**, never the main session.

    This is the only sanctioned path to mark a todo done. It writes
    `done_by_driver_ts` alongside `done_ts` so the post-round check
    can tell whether a `status: done` was authorized by the driver
    (programmatic acceptance) or smuggled in by the LLM.

    Args:
        todo: the Todo to mutate (its file is rewritten in place)
        note: optional short note (≤120 chars) describing the
              verification (e.g. "make check: 18/18 OK, libmcc.a built").
    """
    text = todo.path.read_text(encoding="utf-8", errors="replace")
    m = _FRONT_RE.match(text)
    today = _dt.date.today().isoformat()
    now = now_ts()
    if not m:
        new_block = (
            f"<!--\n"
            f"priority: {todo.priority or 'P3'}\n"
            f"status: done\n"
            f"done_ts: {today}\n"
            f"done_by_driver_ts: {now}\n"
            + (f"note: {note[:120]}\n" if note else "")
            + "-->\n\n"
        )
        todo.path.write_text(new_block + text, encoding="utf-8")
        return
    block = m.group(1)
    extra_lines: list[str] = []
    kv_lines: dict[str, str] = {}
    for line in block.splitlines():
        mm = _KV_RE.match(line)
        if mm:
            kv_lines[mm.group(1).strip().lower()] = mm.group(2).strip()
        else:
            extra_lines.append(line)
    kv_lines["status"] = "done"
    kv_lines["done_ts"] = today
    kv_lines["done_by_driver_ts"] = now
    if "priority" not in kv_lines and todo.priority:
        kv_lines["priority"] = todo.priority
    if note:
        kv_lines["done_note"] = note[:120]
    # preserve original key order: existing keys first (in original order),
    # then any new keys (done_by_driver_ts / done_note appended).
    new_block_lines = [f"{k}: {v}" for k, v in kv_lines.items()] + extra_lines
    new_block = "\n".join(l for l in new_block_lines if l.strip())
    new_text = text[:m.start()] + f"<!--\n{new_block}\n-->" + text[m.end():]
    todo.path.write_text(new_text, encoding="utf-8")


def rollback_unauthorized_done(todo: Todo, *, reason: str) -> None:
    """Force-revert a `status: done` that wasn't written by the driver.

    Used by post-round check when it sees a done without
    `done_by_driver_ts` set: revert to `in_progress` and write a
    `progress_note` explaining why.
    """
    text = todo.path.read_text(encoding="utf-8", errors="replace")
    m = _FRONT_RE.match(text)
    if not m:
        return
    block = m.group(1)
    extra_lines: list[str] = []
    kv_lines: dict[str, str] = {}
    for line in block.splitlines():
        mm = _KV_RE.match(line)
        if mm:
            kv_lines[mm.group(1).strip().lower()] = mm.group(2).strip()
        else:
            extra_lines.append(line)
    if kv_lines.get("status") != "done":
        return  # nothing to do
    # Preserve original key order; mutate only the specific fields.
    for k in ("done_ts", "done_by_driver_ts", "done_note"):
        kv_lines.pop(k, None)
    kv_lines["status"] = "in_progress"
    if "progress_note" in kv_lines:
        kv_lines["progress_note"] = (
            f"{kv_lines['progress_note']} | driver rollback: {reason[:80]}"
        )
    else:
        kv_lines["progress_note"] = f"driver rollback: {reason[:200]}"
    new_block_lines = [f"{k}: {v}" for k, v in kv_lines.items()] + extra_lines
    new_block = "\n".join(l for l in new_block_lines if l.strip())
    new_text = text[:m.start()] + f"<!--\n{new_block}\n-->" + text[m.end():]
    todo.path.write_text(new_text, encoding="utf-8")


def mark_todo_in_progress(todo: Todo) -> None:
    text = todo.path.read_text(encoding="utf-8", errors="replace")
    m = _FRONT_RE.match(text)
    if not m:
        new_block = (
            f"<!--\n"
            f"priority: {todo.priority or 'P3'}\n"
            f"status: in_progress\n"
            f"start_ts: {_dt.date.today().isoformat()}\n"
            f"-->\n\n"
        )
        todo.path.write_text(new_block + text, encoding="utf-8")
        return
    block = m.group(1)
    lines: list[str] = []
    kv_lines: dict[str, str] = {}
    for line in block.splitlines():
        mm = _KV_RE.match(line)
        if mm:
            kv_lines[mm.group(1).strip().lower()] = mm.group(2).strip()
        else:
            lines.append(line)
    kv_lines["status"] = "in_progress"
    if "start_ts" not in kv_lines:
        kv_lines["start_ts"] = _dt.date.today().isoformat()
    if "priority" not in kv_lines and todo.priority:
        kv_lines["priority"] = todo.priority
    new_block_lines = [f"{k}: {v}" for k, v in kv_lines.items()] + lines
    new_block = "\n".join(l for l in new_block_lines if l.strip())
    new_text = text[:m.start()] + f"<!--\n{new_block}\n-->" + text[m.end():]
    todo.path.write_text(new_text, encoding="utf-8")


# ============================================================
# Task mode helpers
# ============================================================

import hashlib


def hash_task_desc(desc: str) -> str:
    """Generate a short 8-char task_id from a task description.

    Used as a prefix for todo filenames generated by --task mode, and as
    the `task_id` field in the todo front matter for traceability.
    """
    return hashlib.sha256(desc.encode("utf-8")).hexdigest()[:8]


def load_task_mode() -> dict:
    """Read task_mode from state/main_session.json.

    Returns an empty dict if no active task_mode.
    """
    state = load_state("main_session", {})
    return state.get("task_mode", {})


def save_task_mode(task_mode: dict) -> None:
    """Merge task_mode into state/main_session.json (preserves other fields)."""
    state = load_state("main_session", {})
    state["task_mode"] = task_mode
    save_state("main_session", state)


def init_task_mode(desc: str, task_id: str | None = None) -> dict:
    """Create a fresh task_mode dict for a new --task session.

    Args:
        desc: the task description text
        task_id: optional explicit task_id (e.g. for --task-resume); if
                None, derived from desc via hash_task_desc()
    """
    tid = task_id or hash_task_desc(desc)
    return {
        "active": True,
        "task_id": tid,
        "task_desc": desc[:500],  # cap to avoid state bloat
        "todo_names": [],          # filled after round 0 generation
        "started_ts": now_ts(),
        "finished_ts": None,
    }


def scan_task_todos(root: Path, subs: list[str], task_id: str) -> list[Todo]:
    """List all todo files that have `task_id: <tid>` in their front matter.

    Used by driver to discover todos generated by the current task session,
    including derivative ones created via todo_admin.py during execution.
    """
    all_todos = list_todos(root, subs, include_done=True)
    out: list[Todo] = []
    for t in all_todos:
        try:
            text = t.path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        m = _FRONT_RE.search(text)
        if not m:
            continue
        for line in m.group(1).splitlines():
            kv = _KV_RE.match(line)
            if kv and kv.group(1).strip().lower() == "task_id":
                if kv.group(2).strip() == task_id:
                    out.append(t)
                break
    return out


if __name__ == "__main__":
    # tiny CLI: ci_lib list-todos [project_root] [subproject,subproject,...]
    import argparse
    p = argparse.ArgumentParser(prog="ci_lib")
    sub = p.add_subparsers(dest="cmd", required=True)
    lt = sub.add_parser("list-todos")
    lt.add_argument("project_root")
    lt.add_argument("subprojects", nargs="*")
    lt.add_argument("--include-done", action="store_true")
    args = p.parse_args()
    root = Path(args.project_root).resolve()
    subs = args.subprojects or None
    todos = list_todos(root, subs, include_done=args.include_done)
    out = []
    for t in todos:
        out.append({
            "subproject": t.subproject,
            "rel_path": t.rel_path,
            "name": t.name,
            "priority": t.priority,
            "status": t.status,
            "title": t.title,
            "actionable": t.is_actionable(),
        })
    json.dump(out, sys.stdout, indent=2, ensure_ascii=False)
    print()
