#!/usr/bin/env python3
"""ci_driver.py - multi-round codebuddy headless driver for MeuOS-Kit todos.

Work model:
  - The main session (model from CI_DRIVER_MAIN_MODEL / --main-model /
    codebuddy's own default) is a PLANNER + DISPATCHER + AUDITOR + COMMITTER.
    It picks the next todo, reads it, calls `subagent_run` (a sub-agent via
    MCP), runs its own independent acceptance check, then **git commits** the
    work. When the batch is finished, the main session emits `[[RESTART]]`
    in its output and codebuddy exits. This Python driver then re-launches
    the same main session via `--resume <id>` to continue with the next
    batch. Sessions persist across restarts; on crash / context overflow /
    abnormal exit, the driver starts a fresh session and reports the gap.

  - Sub-agents (model from CI_DRIVER_SUB_MODEL / --sub-model) are pure
    executors: receive one micro-task via the `subagent_run` MCP tool, do
    the work, run the acceptance test themselves, and return a structured
    report.

  - State: <state_dir>/main_session.json holds the current main session id,
    last batch, todo completion log. state/current_todo.json records the
    todo currently being processed. state/todo_priority.json is the
    priority-ordered manifest the main session reads on demand.

Usage:
  python3 tools/ci-driver/ci_driver.py <project_dir> [options]

Options:
  --projects mcc,meow,...   Restrict to these subprojects (default: all)
  --batch-size N            Todos per main-session round (default 2)
  --max-rounds N            Hard cap on rounds before giving up (default 999)
  --main-model M            Main session model (default: $CI_DRIVER_MAIN_MODEL,
                            else $CI_DRIVER_MODEL, else codebuddy's own default)
  --sub-model  M            Sub-agent model (default: $CI_DRIVER_SUB_MODEL, else
                            $CI_DRIVER_MODEL, else codebuddy's own default)
  --round-timeout S         Seconds per main-session round (default 3600)
  --once                    Process at most one batch and exit
  --reset                   Wipe main session state and start fresh
  --dry-run                 Show what would be done without invoking codebuddy
  --list-todos              Just list actionable todos and exit
  --no-echo                 Disable live stream-json echo (logs only)
  --include-done            When listing, include already-done todos
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

import ci_lib

# Markers the main session is expected to emit
RE_RESTART = re.compile(r"\[\[RESTART(:([^\]]*))?\]\]")
RE_DONE = re.compile(r"\[\[DONE(:([^\]]*))?\]\]")
RE_FAIL = re.compile(r"\[\[FAIL(:([^\]]*))?\]\]")
RE_SESSION = re.compile(r"\[\[SESSION_ID:([^\]]+)\]\]")
# Per-todo claim markers. Format: [[CLAIM_DONE: <relpath>]] or
# [[CLAIM_FAILED: <relpath>: <reason>]]
RE_CLAIM_DONE = re.compile(
    r"\[\[CLAIM_DONE:\s*([^\]\s]+)\s*(?::\s*([^\]]*))?\]\]"
)
RE_CLAIM_FAILED = re.compile(
    r"\[\[CLAIM_FAILED:\s*([^\]\s]+)\s*:\s*([^\]]+)\]\]"
)

MAIN_PROMPT_PATH = ci_lib.PROMPTS_DIR / "main_planner.md"


def _materialize_mcp_config(project_root: Path) -> Path:
    """Write an absolute-path MCP config to state/, so the ci_server is
    invoked with the right CI_DRIVER_PROJECT_ROOT and absolute paths for
    `command` / `args` (codebuddy does not resolve placeholders).
    """
    server_py = (ci_lib.DRIVER_DIR / "ci_server.py").resolve()
    cfg = {
        "mcpServers": {
            "ci_driver": {
                "type": "stdio",
                "command": sys.executable,
                "args": [str(server_py)],
                "env": {
                    "CI_DRIVER_PROJECT_ROOT": str(project_root.resolve()),
                },
            }
        }
    }
    out = ci_lib.STATE_DIR / "mcp_config.active.json"
    out.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")
    return out


def _load_main_prompt() -> str:
    if not MAIN_PROMPT_PATH.exists():
        sys.exit(f"FATAL: main prompt missing: {MAIN_PROMPT_PATH}")
    return MAIN_PROMPT_PATH.read_text(encoding="utf-8")


def _format_todo_block(todos: list[ci_lib.Todo]) -> str:
    if not todos:
        return "(no actionable todos remaining)"
    parts = []
    for i, t in enumerate(todos, 1):
        parts.append(
            f"### Todo {i}  [{t.priority or 'P?'}] {t.subproject}/{t.name}\n"
            f"- file: {t.rel_path}\n"
            f"- title: {t.title or '(no title)'}\n"
            f"- status: {t.status or 'pending'}\n"
        )
    return "\n".join(parts)


def _run_main_round(
    *,
    project_root: Path,
    main_model: str,
    session_id: str | None,
    system_prompt: str,
    user_prompt: str,
    round_timeout: int,
    use_mcp: bool,
    batch_no: int,
    live_echo: bool = True,
) -> dict:
    """Spawn one main-session codebuddy process, return parsed result.

    Uses --output-format stream-json for live incremental output (so the
    user sees text / tool calls as they happen, not after round ends). The
    stream-json format is one JSON object per line; the last object of type
    "result" / "assistant" carries the final `result` text + `session_id`.

    Marker detection MUST only see codebuddy's actual output, never the
    system_prompt or echoed args — those contain the marker syntax in
    example text and would produce false positives.
    """
    args: list[str] = ["codebuddy", "-p", user_prompt, "--output-format",
                       "stream-json", "--verbose"]
    if main_model:
        args += ["--model", main_model]
    args += ["-y", "--append-system-prompt", system_prompt]
    if session_id:
        args += ["--resume", session_id]
    if use_mcp:
        mcp_cfg = _materialize_mcp_config(project_root)
        args += ["--mcp-config", str(mcp_cfg), "--strict-mcp-config"]
    args += ["--add-dir", str(project_root)]

    env = os.environ.copy()
    env.setdefault("PAGER", "cat")
    env["CI_DRIVER_RUN"] = "1"
    env["CI_DRIVER_PROJECT_ROOT"] = str(project_root)

    args_log = ci_lib.LOGS_DIR / f"main_round_{batch_no:04d}.args.log"
    out_log = ci_lib.LOGS_DIR / f"main_round_{batch_no:04d}.out.log"
    args_log.write_text(
        f"# codebuddy argv (for audit, not used for marker detection)\n"
        f"cd {project_root}\n{' '.join(args)}\n"
        f"\n# system_prompt ({len(system_prompt)} chars, head omitted)\n"
        f"# user_prompt ({len(user_prompt)} chars, head omitted)\n",
        encoding="utf-8",
    )
    ci_lib.log(f"round {batch_no} start: session={session_id or '(new)'} "
               f"model={main_model or '(default)'}",
               name="ci_driver")

    # ---- live echo via stream-json ----
    final_result_text = ""
    final_session_id = session_id or ""
    final_is_error = False
    final_exit_code = -1
    timed_out = False
    last_assistant_text = ""
    saw_init = False

    try:
        proc = subprocess.Popen(
            args,
            cwd=str(project_root),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        with out_log.open("w", encoding="utf-8") as outf:
            assert proc.stdout is not None
            for line in proc.stdout:
                outf.write(line)
                outf.flush()
                line = line.strip()
                if not line:
                    continue
                try:
                    ev = json.loads(line)
                except json.JSONDecodeError:
                    if live_echo:
                        sys.stderr.write(f"[raw] {line}\n")
                        sys.stderr.flush()
                    continue
                ev_type = ev.get("type", "")
                if live_echo:
                    _echo_event(ev, ev_type, last_assistant_text)
                # capture state from relevant events
                if ev_type == "system" and ev.get("subtype") == "init":
                    saw_init = True
                    if ev.get("session_id"):
                        final_session_id = ev["session_id"]
                elif ev_type == "assistant":
                    msg = ev.get("message") or {}
                    content = msg.get("content")
                    if isinstance(content, list):
                        for blk in content:
                            if isinstance(blk, dict) and blk.get("type") == "text":
                                last_assistant_text = blk.get("text", "")
                    elif isinstance(content, str):
                        last_assistant_text = content
                elif ev_type == "result":
                    final_result_text = ev.get("result", "") or ""
                    if ev.get("session_id"):
                        final_session_id = ev["session_id"]
                    final_is_error = bool(ev.get("is_error", False))
                elif ev_type == "error":
                    final_is_error = True
            final_exit_code = proc.wait(timeout=round_timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        try:
            proc.kill()
            proc.wait(timeout=5)
        except Exception:
            pass
        ci_lib.log(f"round {batch_no} TIMEOUT after {round_timeout}s", name="ci_driver")
    except Exception as e:
        ci_lib.log(f"round {batch_no} EXCEPTION: {e}", name="ci_driver")
        timed_out = True

    if timed_out:
        return {"exit_code": -1, "stdout": "", "raw_stdout": "",
                "timed_out": True, "session_id": session_id, "log": str(out_log)}

    # Prefer the final `result` event; fall back to the last assistant text
    # (handles cases where the model streamed text but never produced a
    # result event due to crash).
    result_text = final_result_text or last_assistant_text
    ci_lib.log(f"round {batch_no} done: exit={final_exit_code} "
               f"session={(final_session_id or '(none)')[:8]} "
               f"restart={'yes' if RE_RESTART.search(result_text) else 'no'} "
               f"done={'yes' if RE_DONE.search(result_text) else 'no'}",
               name="ci_driver")
    return {"exit_code": final_exit_code, "stdout": result_text,
            "raw_stdout": "", "timed_out": False,
            "session_id": final_session_id, "log": str(out_log)}


def _echo_event(ev: dict, ev_type: str, last_text: str) -> None:
    """Print a compact human-readable line per stream-json event to stderr."""
    if ev_type == "system":
        sub = ev.get("subtype", "")
        if sub == "init":
            sys.stderr.write(f"\n[init model={ev.get('model','?')} "
                             f"session={(ev.get('session_id','') or '')[:8]}]\n")
        return
    if ev_type == "user":
        msg = ev.get("message") or {}
        content = msg.get("content")
        if isinstance(content, list):
            for blk in content:
                if isinstance(blk, dict) and blk.get("type") == "tool_result":
                    name = (ev.get("tool_use_result") or {}).get("name", "tool")
                    sys.stderr.write(f"  [tool result {name}]\n")
        return
    if ev_type == "assistant":
        msg = ev.get("message") or {}
        content = msg.get("content")
        if isinstance(content, list):
            for blk in content:
                if not isinstance(blk, dict):
                    continue
                if blk.get("type") == "text":
                    text = blk.get("text", "")
                    if text and text != last_text:
                        sys.stderr.write(f"  > {text}\n")
                elif blk.get("type") == "tool_use":
                    tn = blk.get("name", "?")
                    ti = blk.get("input", {})
                    if tn in ("Read", "Grep", "Glob"):
                        # show path / pattern
                        p = ti.get("file_path") or ti.get("pattern") or ti.get("path") or ""
                        sys.stderr.write(f"  · {tn} {p}\n")
                    elif tn == "Bash":
                        cmd = (ti.get("command") or "").splitlines()[0][:100]
                        sys.stderr.write(f"  $ {cmd}\n")
                    elif tn == "Edit" or tn == "Write":
                        p = ti.get("file_path") or ""
                        sys.stderr.write(f"  · {tn} {p}\n")
                    else:
                        sys.stderr.write(f"  · {tn}\n")
        elif isinstance(content, str) and content and content != last_text:
            sys.stderr.write(f"  > {content}\n")
        return
    if ev_type == "result":
        text = ev.get("result", "") or ""
        sys.stderr.write(f"[result] {text[:200]}{'...' if len(text) > 200 else ''}\n")
        return
    if ev_type == "error":
        sys.stderr.write(f"[error] {ev.get('message', ev)}\n")
        return


def _pick_batch(todos: list[ci_lib.Todo], start: int, size: int) -> list[ci_lib.Todo]:
    return todos[start:start + size]


def _read_todo_status(todo: ci_lib.Todo) -> str:
    """Re-read a todo file's `status` front matter from disk.

    The main session may have edited the file during the round; the Todo
    snapshot we hold is stale. The file on disk is the single source of
    truth for "is this actually done?".
    """
    try:
        text = todo.path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    m = re.search(r"<!--\s*(.*?)\s*-->", text, re.DOTALL)
    if not m:
        return ""
    for line in m.group(1).splitlines():
        kv = re.match(r"\s*status\s*:\s*(\S+)", line)
        if kv:
            return kv.group(1).strip().lower()
    return ""


def _read_done_meta(todo: ci_lib.Todo) -> dict[str, str]:
    """Re-read the todo's front matter and return a dict of keys, lowercased.

    Used by the post-round check to determine whether a `status: done`
    was authorized by the driver (presence of `done_by_driver_ts`).
    """
    try:
        text = todo.path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    m = re.search(r"<!--\s*(.*?)\s*-->", text, re.DOTALL)
    if not m:
        return {}
    out: dict[str, str] = {}
    for line in m.group(1).splitlines():
        kv = re.match(r"\s*([A-Za-z_]+)\s*:\s*(.+?)\s*$", line)
        if kv:
            out[kv.group(1).strip().lower()] = kv.group(2).strip()
    return out


def _run_acceptance_cmds(
    todo: ci_lib.Todo, *, cwd: Path, timeout_s: int = 1800
) -> tuple[bool, str, str]:
    """Run the todo's acceptance commands and return (ok, stdout, stderr).

    The commands are extracted from the `## 验收标准` section by
    `ci_lib.extract_acceptance_cmds()`. If the section is missing or
    empty, returns `(False, "", "no acceptance commands in todo body")`.

    Each command runs sequentially; we stop on the first failure.
    stdout+stderr are capped at 4000 chars each to avoid log bloat.
    """
    cmds = ci_lib.extract_acceptance_cmds(todo)
    if not cmds:
        return False, "", "no acceptance commands in todo body"
    combined_out: list[str] = []
    combined_err: list[str] = []
    for i, cmd in enumerate(cmds, 1):
        ci_lib.log(f"acceptance [{todo.name}] cmd {i}/{len(cmds)}: {cmd[:200]}",
                   name="ci_driver")
        try:
            proc = subprocess.run(
                cmd, shell=True, cwd=str(cwd),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=timeout_s,
            )
        except subprocess.TimeoutExpired:
            msg = f"TIMEOUT after {timeout_s}s"
            combined_out.append(f"$ {cmd}\n<{msg}>")
            return False, "\n".join(combined_out), msg
        ok = (proc.returncode == 0)
        # Cap output to last 1500 chars per command
        out_tail = (proc.stdout or "")[-1500:]
        err_tail = (proc.stderr or "")[-1500:]
        combined_out.append(f"$ {cmd}\n{out_tail}")
        combined_err.append(f"$ {cmd}\n{err_tail}")
        if not ok:
            ci_lib.log(
                f"acceptance [{todo.name}] FAILED at cmd {i} "
                f"rc={proc.returncode}",
                name="ci_driver",
            )
            return False, "\n".join(combined_out), "\n".join(combined_err)
    return True, "\n".join(combined_out), "\n".join(combined_err)


def _process_claim_markers(
    markers: dict, batch: list[ci_lib.Todo], project_root: Path,
    acceptance_log: Path, completed: set[str], completed_log: dict,
) -> dict:
    """Process [[CLAIM_DONE: ...]] markers from the main session.

    For each marker, find the matching todo in the batch, run its
    acceptance commands, and (on success) call
    `ci_lib.mark_todo_done_by_driver`. Returns a per-todo outcome dict
    for the post-round log.

    The `completed` set is mutated in place; the caller is responsible
    for saving state.
    """
    rel_to_todo = {str(t.rel_path): t for t in batch}
    outcomes: dict[str, dict] = {}
    for relpath, _ in markers.get("claim_dones", []):
        todo = rel_to_todo.get(relpath)
        if todo is None:
            ci_lib.log(
                f"CLAIM_DONE for '{relpath}' but todo is not in current "
                f"batch; ignoring",
                name="ci_driver",
            )
            outcomes[relpath] = {"status": "ignored", "reason": "not in batch"}
            continue
        if not ci_lib.has_acceptance_section(todo):
            msg = "todo has no 验收标准 section; main session must add one"
            ci_lib.log(f"CLAIM_DONE [{relpath}] REFUSED: {msg}", name="ci_driver")
            ci_lib.rollback_unauthorized_done(
                todo, reason=f"claim_refused:{msg}")
            outcomes[relpath] = {"status": "refused", "reason": msg}
            continue
        ok, out, err = _run_acceptance_cmds(todo, cwd=project_root)
        # Persist the run log
        try:
            with acceptance_log.open("a", encoding="utf-8") as f:
                f.write(f"\n===== {relpath} @ {ci_lib.now_ts()} =====\n")
                f.write(f"OK={ok}\n--- stdout ---\n{out}\n--- stderr ---\n{err}\n")
        except OSError:
            pass
        if ok:
            note = "driver accepted; all cmds passed"
            ci_lib.mark_todo_done_by_driver(todo, note=note)
            completed.add(todo.name)
            completed_log[todo.name] = "done_via_driver"
            ci_lib.log(f"CLAIM_DONE [{relpath}] ACCEPTED", name="ci_driver")
            outcomes[relpath] = {"status": "done", "ok": True}
        else:
            reason = (err.strip() or out.strip() or "acceptance failed")[-200:]
            ci_lib.rollback_unauthorized_done(
                todo, reason=f"acceptance_failed:{reason}")
            ci_lib.log(
                f"CLAIM_DONE [{relpath}] REJECTED; reason={reason}",
                name="ci_driver",
            )
            outcomes[relpath] = {
                "status": "rejected", "ok": False, "reason": reason,
            }
    for relpath, reason in markers.get("claim_faileds", []):
        todo = rel_to_todo.get(relpath)
        if todo is None:
            outcomes[relpath] = {"status": "ignored", "reason": "not in batch"}
            continue
        # Ensure todo stays in_progress (may already be), and record note
        completed.discard(todo.name)
        ci_lib.log(
            f"CLAIM_FAILED [{relpath}] recorded: {reason[:100]}",
            name="ci_driver",
        )
        outcomes[relpath] = {"status": "claimed_failed", "reason": reason}
    return outcomes


def _detect_markers(stdout: str) -> dict:
    claim_dones = [
        (m.group(1).strip(), (m.group(2) or "").strip())
        for m in RE_CLAIM_DONE.finditer(stdout)
    ]
    claim_faileds = [
        (m.group(1).strip(), m.group(2).strip())
        for m in RE_CLAIM_FAILED.finditer(stdout)
    ]
    return {
        "restart": bool(RE_RESTART.search(stdout)),
        "done": bool(RE_DONE.search(stdout)),
        "fail": bool(RE_FAIL.search(stdout)),
        "fail_msg": (RE_FAIL.search(stdout).group(2) if RE_FAIL.search(stdout) else ""),
        "claim_dones": claim_dones,
        "claim_faileds": claim_faileds,
    }


def cmd_list_todos(args: argparse.Namespace) -> int:
    root = Path(args.project_root).resolve()
    subs = [s for s in (args.projects or "").split(",") if s] or None
    todos = ci_lib.list_todos(root, subs, include_done=args.include_done)
    for t in todos:
        flag = "*" if t.is_actionable() else " "
        print(f"{flag} [{t.priority or 'P?':3}] {t.subproject}/{t.name}"
              f"  ({t.status or 'pending'})  - {t.title}")
    print(f"\n{len(todos)} todo(s); {sum(1 for t in todos if t.is_actionable())} actionable")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    root = Path(args.project_root).resolve()
    if not (root / "AGENTS.md").is_file():
        sys.exit(f"FATAL: {root}/AGENTS.md not found; not a MeuOS-Kit project?")
    if not (root / "projects").is_dir():
        sys.exit(f"FATAL: {root}/projects not found")

    ci_lib.ensure_dirs()
    ci_lib.log(f"ci_driver start: root={root} args={vars(args)}", name="ci_driver")

    # Resolve models at startup so the system prompt and logs are consistent.
    main_model = ci_lib.resolve_main_model(args.main_model)
    sub_model = ci_lib.resolve_sub_model(args.sub_model)
    ci_lib.log(f"resolved models: main={main_model or '(codebuddy default)'} "
               f"sub={sub_model or '(codebuddy default)'}", name="ci_driver")

    subs: list[str] | None = None
    if args.projects:
        subs = [s.strip() for s in args.projects.split(",") if s.strip()]

    if args.reset:
        for f in (ci_lib.STATE_DIR / "main_session.json",
                  ci_lib.STATE_DIR / "current_todo.json"):
            if f.exists():
                f.unlink()
        ci_lib.log("state reset", name="ci_driver")

    state = ci_lib.load_state("main_session", {
        "session_id": None,
        "last_batch": 0,
        "completed": [],
        "history": [],
    })
    # Per-round map: todo name -> "done_via_driver" | "unauthorized_done".
    # Reset each round; used for audit + the state history entry.
    completed_log: dict[str, str] = {}

    system_prompt = _load_main_prompt()
    if args.dry_run:
        print("=== DRY-RUN ===")
        print(f"project: {root}")
        print(f"subprojects: {subs or '(all)'}")
        print(f"main_model: {args.main_model}")
        print(f"sub_model: {args.sub_model}")
        print(f"batch_size: {args.batch_size}")
        print(f"existing main session: {state.get('session_id') or '(none)'}")
        todos = ci_lib.list_todos(root, subs)
        print(f"\nactionable todos: {sum(1 for t in todos if t.is_actionable())}")
        for t in todos:
            if t.is_actionable():
                print(f"  - [{t.priority}] {t.subproject}/{t.name}: {t.title}")
        return 0

    round_no = 0
    while round_no < args.max_rounds:
        round_no += 1

        # Refresh the priority manifest at the start of every round so the
        # main session always sees a current view of the queue.
        manifest_path = ci_lib.dump_todo_priority(root, subs)
        todos_all = ci_lib.list_todos(root, subs)
        actionable = [t for t in todos_all if t.is_actionable()]
        if not actionable:
            ci_lib.log("no actionable todos; exiting", name="ci_driver")
            print(f"[round {round_no}] no actionable todos; done.")
            return 0

        # Resume from where we left off: skip already-completed names
        completed = set(state.get("completed") or [])
        pending = [t for t in actionable if t.name not in completed]
        if not pending:
            ci_lib.log("all actionable todos marked completed in state; exiting",
                       name="ci_driver")
            return 0

        batch = _pick_batch(pending, 0, args.batch_size)
        if not batch:
            return 0
        for t in batch:
            ci_lib.mark_todo_in_progress(t)
        ci_lib.save_state("current_todo", {
            "batch": [t.rel_path for t in batch],
            "started_at": ci_lib.now_ts(),
        })

        todo_block = _format_todo_block(batch)
        n_pending_total = len(pending)
        n_pending_after = max(0, n_pending_total - len(batch))
        completed_block = "\n".join(f"- {n}" for n in sorted(completed)) or "(none)"

        # Compact ordered view of the full queue, for the main session to
        # cross-reference when it wants to peek beyond the current batch.
        all_actionable = ci_lib.list_todos(root, subs)
        flat_view = "\n".join(
            f"  {i:3d}. {t.subproject}/{t.name}  [{t.priority or 'P?'}]  "
            f"({t.status or 'pending'})  - {t.title}"
            for i, t in enumerate(all_actionable, 1)
        ) or "(empty)"

        # Commit-message template the main session should use.
        commit_template_lines = []
        for t in batch:
            commit_template_lines.append(
                f"  {t.subproject}: {t.name} ({t.priority or 'P?'}) - <one-line summary>"
            )
        commit_template = "\n".join(commit_template_lines)

        user_prompt = (
            "## Driver status\n"
            f"- project_root: {root}\n"
            f"- main session: {'resume ' + state['session_id'] if state.get('session_id') else 'NEW'}\n"
            f"- round: {round_no} / max {args.max_rounds}\n"
            f"- actionable todos remaining (after this batch): {n_pending_after}\n"
            f"- already completed in this session: {len(completed)}\n"
            f"- completed list:\n{completed_block}\n\n"
            f"## Full priority queue ({len(all_actionable)} actionable)\n"
            f"(read {manifest_path} for the structured JSON manifest, "
            f"or grep by priority with `Grep priority:P1 {manifest_path}`)\n"
            f"{flat_view}\n\n"
            f"## Batch ({len(batch)} todo{'s' if len(batch) != 1 else ''})\n"
            f"{todo_block}\n\n"
            "## Instructions for this round\n"
            "Process the batch above. For EACH todo:\n"
            "  1. Read the todo file in full (path is in 'file:').\n"
            "  2. Verify the todo has a `## 验收标准` (Acceptance Criteria) section\n"
            "     with one or more fenced code-block shell commands. If it does\n"
            "     NOT, you must add one (with reasonable commands) BEFORE doing\n"
            "     any other work on that todo — the driver uses that section to\n"
            "     decide whether to mark the todo done.\n"
            "  3. Form a concrete micro-task: which file(s) to change, what the\n"
            "     acceptance check is (test script / make target / build step).\n"
            "  4. Call `subagent_run(task=<microtask>, workdir=<this project root>,\n"
            "     extra_context=<brief todo context>)` to dispatch a sub-agent.\n"
            "  5. After sub-agent returns, run the acceptance commands YOURSELF\n"
            "     with Bash. Do NOT trust the sub-agent's report blindly.\n"
            "  6. **DONE-claim protocol** (CRITICAL — read carefully):\n"
            "     - You MUST NOT edit the todo's `<!-- ... -->` block to set\n"
            "       `status: done` or `done_ts:`. The driver is the only entity\n"
            "       that can set those. If you do, the driver will detect the\n"
            "       absence of `done_by_driver_ts` and roll the status back to\n"
            "       `in_progress` automatically.\n"
            "     - Instead, after you have BOTH committed the work AND verified\n"
            "       the acceptance commands pass, output this marker on its own\n"
            "       line in your final reply:\n"
            "         [[CLAIM_DONE: <todo_relpath>]]\n"
            "       The driver will then re-run the acceptance commands\n"
            "       independently and, on success, set `status: done` for you.\n"
            "     - If the acceptance commands fail, output:\n"
            "         [[CLAIM_FAILED: <todo_relpath>: <one-line reason>]]\n"
            "       The driver will record the failure and keep the todo in\n"
            "       `in_progress`.\n"
            "  7. **Commit BEFORE claiming done**:\n"
            "     a) `git add` the changed files (the todo file itself, plus\n"
            "        any source/test files the work touched);\n"
            "     b) `git commit -m \"<see templates below>\"` (one commit per todo);\n"
            "     c) verify with `git log -1 --stat` that the commit landed.\n"
            "     Conventional-commit style:\n"
            + commit_template + "\n"
            "  8. If the acceptance check fails, retry with a refined sub-task,\n"
            "     OR call `subagent_run` again with a fix prompt (max 3 retries).\n"
            "When the whole batch is processed, output a final summary AND one of:\n"
            "  [[RESTART]]                          -> more todos remain, ask driver to continue\n"
            "  [[DONE]]                             -> all actionable todos finished\n"
            "  [[FAIL: <one-line reason>]]          -> unrecoverable; driver will start a fresh session\n"
            "Per-todo claim markers (use as many as needed, one per todo):\n"
            "  [[CLAIM_DONE: <relpath>]]            -> asks driver to verify + set done\n"
            "  [[CLAIM_FAILED: <relpath>: <reason>]] -> records a failure, keeps in_progress\n"
            "If a session-id line is useful, emit `[[SESSION_ID:<id>]]` somewhere.\n\n"
            "Begin the round now.\n"
        )

        result = _run_main_round(
            project_root=root,
            main_model=main_model,
            session_id=state.get("session_id"),
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            round_timeout=args.round_timeout,
            use_mcp=True,
            batch_no=round_no,
            live_echo=not args.no_echo,
        )

        # update state
        markers = _detect_markers(result["stdout"])
        new_sid = result["session_id"] or state.get("session_id")
        if result["timed_out"]:
            # Abandon this session, start a new one next round
            new_sid = None
            ci_lib.log("session abandoned due to timeout", name="ci_driver")
        state["session_id"] = new_sid
        state["last_batch"] = round_no

        # ---- 1. Process [[CLAIM_DONE: ...]] markers ----
        # For each claim, the driver independently re-runs the todo's
        # `## 验收标准` commands. On success: mark_todo_done_by_driver().
        # On failure or missing acceptance section: rollback to in_progress.
        acceptance_log = ci_lib.LOGS_DIR / "acceptance_runs.log"
        claim_outcomes = _process_claim_markers(
            markers, batch, root, acceptance_log, completed, completed_log,
        )

        # ---- 2. Post-round truth check ----
        # Status file is the only source of truth. A `status: done` is
        # only valid if the file has a `done_by_driver_ts` field —
        # otherwise the LLM smuggled it in and we revert.
        round_status: dict[str, str] = {}
        rollback_reasons: dict[str, str] = {}
        for t in batch:
            actual = _read_todo_status(t)
            round_status[t.name] = actual
            if actual == "done":
                meta = _read_done_meta(t)
                if "done_by_driver_ts" not in meta:
                    # Unauthorized done (LLM wrote it directly). Revert.
                    reason = (
                        "status=done but no done_by_driver_ts; LLM wrote "
                        "directly without going through driver. Reverted."
                    )
                    ci_lib.log(
                        f"ROLLBACK: {t.rel_path} {reason}",
                        name="ci_driver",
                    )
                    ci_lib.rollback_unauthorized_done(t, reason=reason)
                    rollback_reasons[t.name] = reason
                    completed.discard(t.name)
                    round_status[t.name] = "in_progress"
                else:
                    completed.add(t.name)
            else:
                if t.name in completed and actual in ("in_progress", "pending", ""):
                    ci_lib.log(
                        f"WARNING: {t.rel_path} status regressed to '{actual}' "
                        f"after main session claimed done; removing from completed",
                        name="ci_driver",
                    )
                    completed.discard(t.name)
        state["completed"] = sorted(completed)
        state["history"].append({
            "round": round_no,
            "batch": [t.rel_path for t in batch],
            "batch_status_after_round": round_status,
            "claim_outcomes": claim_outcomes,
            "rollbacks": rollback_reasons,
            "exit_code": result["exit_code"],
            "timed_out": result["timed_out"],
            "restart": markers["restart"],
            "done": markers["done"],
            "fail": markers["fail"],
            "fail_msg": markers["fail_msg"],
            "session_id": new_sid,
            "ts": ci_lib.now_ts(),
        })
        # keep history bounded
        state["history"] = state["history"][-200:]
        ci_lib.save_state("main_session", state)

        n_claim_done = sum(1 for o in claim_outcomes.values()
                           if o.get("status") == "done")
        n_claim_rejected = sum(1 for o in claim_outcomes.values()
                               if o.get("status") == "rejected")
        n_rolled = len(rollback_reasons)
        print(f"[round {round_no}] exit={result['exit_code']} "
              f"restart={markers['restart']} done={markers['done']} "
              f"fail={markers['fail']} session={new_sid[:8] if new_sid else '(none)'} "
              f"claims(accepted={n_claim_done} rejected={n_claim_rejected}) "
              f"unauthorized_done_rollbacks={n_rolled} "
              f"-> log: {result['log']}")

        if markers["done"]:
            ci_lib.log("main session signalled DONE", name="ci_driver")
            return 0
        if markers["fail"]:
            # Abandon session; next round will be fresh
            state["session_id"] = None
            ci_lib.save_state("main_session", state)
            ci_lib.log(f"main session FAILED: {markers['fail_msg']}", name="ci_driver")
            if args.once:
                return 1
            time.sleep(5)
            continue
        if not markers["restart"]:
            # No explicit restart marker, but exit was clean: assume batch done
            # and continue (treat like implicit RESTART). This protects against
            # the main session forgetting the marker.
            ci_lib.log("no [[RESTART]] marker but exit was clean; continuing",
                       name="ci_driver")
        if args.once:
            return 0
        time.sleep(1)

    ci_lib.log(f"hit max_rounds={args.max_rounds}; exiting", name="ci_driver")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(prog="ci_driver",
                                description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("project_root", help="Path to MeuOS-Kit project root")
    p.add_argument("--projects", default="",
                   help="Comma-separated subprojects to scope to (default: all)")
    p.add_argument("--batch-size", type=int, default=2,
                   help="Todos per main-session round (default 2)")
    p.add_argument("--max-rounds", type=int, default=999,
                   help="Hard cap on rounds (default 999)")
    p.add_argument("--main-model", default=ci_lib.resolve_main_model(),
                   help="Main session model (default: $CI_DRIVER_MAIN_MODEL, "
                        "else $CI_DRIVER_MODEL, else codebuddy's own default)")
    p.add_argument("--sub-model", default=ci_lib.resolve_sub_model(),
                   help="Sub-agent model (default: $CI_DRIVER_SUB_MODEL, else "
                        "$CI_DRIVER_MODEL, else codebuddy's own default)")
    p.add_argument("--round-timeout", type=int, default=3600,
                   help="Per-round timeout seconds (default 3600)")
    p.add_argument("--once", action="store_true",
                   help="Process at most one batch and exit")
    p.add_argument("--reset", action="store_true",
                   help="Wipe main-session state and start fresh")
    p.add_argument("--dry-run", action="store_true",
                   help="Show what would be done without invoking codebuddy")
    p.add_argument("--list-todos", action="store_true",
                   help="Just list actionable todos and exit")
    p.add_argument("--no-echo", action="store_true",
                   help="Disable live stream-json echo; only log to files")
    p.add_argument("--include-done", action="store_true",
                   help="When listing, include already-done todos")
    args = p.parse_args()

    if args.list_todos:
        return cmd_list_todos(args)
    return cmd_run(args)


if __name__ == "__main__":
    sys.exit(main())
