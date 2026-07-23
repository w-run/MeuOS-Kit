"""ci_server.py - MCP stdio server exposing `subagent_run` tool.

Speaks JSON-RPC 2.0 over stdin/stdout (one JSON object per line).

Exposes one tool:

  subagent_run(task, workdir?, extra_context?, model?, timeout_s?)
    - Spawns a fresh `codebuddy -p <task> --model <model> --output-format json`
      process as a hy3 sub-agent.
    - Returns the agent's `result` text plus its session_id and a brief
      "how to verify" hint so the main session can run acceptance checks.

This file is intentionally dependency-free (no `mcp` SDK) so it runs on a
plain Python 3.8+ interpreter in any environment that already has codebuddy
installed.
"""

from __future__ import annotations

import json
import os
import sys
import traceback
from pathlib import Path
from typing import Any

# Allow `python ci_server.py` from anywhere
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_lib  # noqa: E402

SERVER_INFO = {"name": "ci-driver", "version": "0.1.0"}
PROTOCOL_VERSION = "2024-11-05"


# ----- JSON-RPC plumbing -----

def _reply(id_: Any, result: Any) -> dict:
    return {"jsonrpc": "2.0", "id": id_, "result": result}


def _err(id_: Any, code: int, msg: str, data: Any = None) -> dict:
    e: dict = {"code": code, "message": msg}
    if data is not None:
        e["data"] = data
    return {"jsonrpc": "2.0", "id": id_, "error": e}


def _read_message(stream) -> dict | None:
    """Read one JSON-RPC message (newline-delimited). Returns None on EOF."""
    line = stream.readline()
    if not line:
        return None
    line = line.strip()
    if not line:
        return None
    try:
        return json.loads(line)
    except json.JSONDecodeError:
        return None


def _write_message(msg: dict) -> None:
    sys.stdout.write(json.dumps(msg, ensure_ascii=False) + "\n")
    sys.stdout.flush()


# ----- Tool definition -----

SUBAGENT_RUN_SCHEMA = {
    "type": "object",
    "properties": {
        "task": {
            "type": "string",
            "description": (
                "Self-contained micro-task description for the hy3 sub-agent. "
                "Should include: file paths to touch, what to do, what the "
                "acceptance check is, and which docs to read first."
            ),
        },
        "workdir": {
            "type": "string",
            "description": (
                "Absolute path to the project root the sub-agent should cd "
                "into. Defaults to the env var CI_DRIVER_PROJECT_ROOT."
            ),
        },
        "extra_context": {
            "type": "string",
            "description": (
                "Optional extra context the main session wants the sub-agent "
                "to know (e.g. the current todo file path, related work in "
                "progress, constraints discovered earlier)."
            ),
        },
        "model": {
            "type": "string",
            "description": (
                "Override the sub-agent model. Defaults to "
                "ci_lib.DEFAULT_MODEL_SUB (custom-local:hy3)."
            ),
        },
        "timeout_s": {
            "type": "integer",
            "description": "Hard timeout in seconds (default 1800 = 30min).",
        },
        "session_id": {
            "type": "string",
            "description": (
                "Optional: a codebuddy session id to continue a previous "
                "sub-agent run. Leave empty to start a fresh sub-agent."
            ),
        },
        "allowed_tools": {
            "type": "string",
            "description": (
                "Comma-separated tool allowlist forwarded to codebuddy. "
                "Default is the sub-agent prompt's recommended set."
            ),
        },
    },
    "required": ["task"],
}


def _load_sub_prompt() -> str:
    p = ci_lib.PROMPTS_DIR / "sub_executor.md"
    if not p.exists():
        sys.exit(f"FATAL: sub-agent prompt missing: {p}")
    return p.read_text(encoding="utf-8")


SUBAGENT_PROMPT_BOILERPLATE = _load_sub_prompt()


def _handle_subagent_run(arguments: dict) -> dict:
    task = arguments.get("task", "").strip()
    if not task:
        return {
            "content": [{"type": "text", "text": "ERROR: empty 'task' argument"}],
            "isError": True,
        }

    workdir_s = arguments.get("workdir") or os.environ.get("CI_DRIVER_PROJECT_ROOT", "")
    if not workdir_s:
        return {
            "content": [{"type": "text", "text": "ERROR: no workdir given and CI_DRIVER_PROJECT_ROOT is unset"}],
            "isError": True,
        }
    workdir = Path(workdir_s).resolve()
    if not workdir.is_dir():
        return {
            "content": [{"type": "text", "text": f"ERROR: workdir not found: {workdir}"}],
            "isError": True,
        }

    model = arguments.get("model") or ci_lib.resolve_sub_model()
    timeout_s = int(arguments.get("timeout_s") or 1800)
    session_id = arguments.get("session_id") or None
    extra = arguments.get("extra_context") or ""
    allowed = arguments.get("allowed_tools") or "Read,Edit,Write,Grep,Glob,Bash"

    # Compose the full system prompt
    system_prompt = SUBAGENT_PROMPT_BOILERPLATE
    if extra:
        system_prompt += "\n\n--- extra_context from main session ---\n" + extra

    # Forward the project root as the workdir for codebuddy itself
    full_prompt = task  # the task itself becomes the user prompt

    ci_lib.log(f"subagent_run start: model={model} workdir={workdir} timeout={timeout_s}s "
               f"task_len={len(task)}", name="ci_server")
    res = ci_lib.call_codebuddy(
        full_prompt,
        model=model,
        workdir=workdir,
        append_system_prompt=system_prompt,
        allowed_tools=allowed.split(","),
        session_id=session_id,
        timeout_s=timeout_s,
    )
    ci_lib.log(f"subagent_run done: ok={res['ok']} exit={res['exit_code']} "
               f"session={res['session_id'][:8] if res['session_id'] else '(none)'}",
               name="ci_server")

    # Format result for the LLM
    head = f"[subagent_run ok={res['ok']} exit={res['exit_code']} session={res['session_id']}]\n"
    if res["ok"]:
        body = res["result"] or "(empty result text)"
    else:
        body = (res["result"] or "") + "\n--- stderr ---\n" + (res["stderr"] or "")
    text = head + body
    if len(text) > 200_000:
        text = text[:200_000] + "\n...[truncated]..."
    return {
        "content": [{"type": "text", "text": text}],
        "isError": not res["ok"],
    }


# ----- Dispatch -----

def _dispatch(msg: dict) -> dict | None:
    """Return a reply dict, or None if msg is a notification (no reply)."""
    method = msg.get("method")
    params = msg.get("params") or {}
    id_ = msg.get("id")

    if method == "initialize":
        return _reply(id_, {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": SERVER_INFO,
        })

    if method == "ping":
        return _reply(id_, {})

    if method == "tools/list":
        return _reply(id_, {"tools": [{
            "name": "subagent_run",
            "description": (
                "Spawn a hy3 sub-agent to execute a self-contained micro-task. "
                "Returns the agent's structured report (STATUS / FILES_CHANGED / "
                "COMMANDS_RUN / ACCEPTANCE / NOTES) so the main session can run "
                "its own independent acceptance verification before marking the "
                "todo done."
            ),
            "inputSchema": SUBAGENT_RUN_SCHEMA,
        }]})

    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        try:
            if name == "subagent_run":
                return _reply(id_, _handle_subagent_run(args))
            return _err(id_, -32601, f"unknown tool: {name}")
        except Exception as e:  # noqa: BLE001
            tb = traceback.format_exc()
            return _reply(id_, {
                "content": [{"type": "text", "text": f"INTERNAL ERROR: {e}\n{tb}"}],
                "isError": True,
            })

    # Notifications (no id) - ignore everything else silently
    if id_ is None:
        return None

    return _err(id_, -32601, f"method not implemented: {method}")


def main() -> None:
    # The MCP client (codebuddy) is the stdin/stdout peer
    stdin = sys.stdin
    while True:
        msg = _read_message(stdin)
        if msg is None:
            return  # EOF
        reply = _dispatch(msg)
        if reply is not None:
            _write_message(reply)


if __name__ == "__main__":
    main()
