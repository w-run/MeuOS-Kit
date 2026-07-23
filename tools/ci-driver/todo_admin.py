#!/usr/bin/env python3
"""todo_admin.py — add / list / delete / sync operations on .todo/*.md.

Per project workflow rules:
  - During implementation, if a new sub-problem is discovered, **add** a new
    todo via `todo_admin.py add` (do not bury it in a comment).
  - Once a todo is fully completed (status: done, done_by_driver_ts set),
    **delete** the file via `todo_admin.py delete` (the git history is the
    archive — no separate archive/ dir needed).

Subcommands:
  add     Create a new todo file with front matter + a placeholder
          `## 验收标准` section. Driver will pick it up next round.
  list    Print a compact table of todos (similar to ci_driver's manifest).
  delete  Remove a todo file (and its git entry if tracked). Records the
          removal in `tools/ci-driver/state/todo_deletions.log`.
  sync    Re-read all todo files; report inconsistencies (e.g. status=done
          but no done_by_driver_ts; missing 验收标准; misleading note).

Examples:
  python3 tools/ci-driver/todo_admin.py . add \
      --subproject mcc --name gd-tls-gap6 \
      --title "GD-TLS Gap 6: i386 __tls_get_addr" \
      --priority P2 --note "i386 TLS 端到端验证"

  python3 tools/ci-driver/todo_admin.py . list --include-done

  python3 tools/ci-driver/todo_admin.py . delete --relpath projects/mcc/.todo/foo.md

  python3 tools/ci-driver/todo_admin.py . sync
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import subprocess
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_lib


# ---- Defaults for the acceptance section per subproject ----
_DEFAULT_ACCEPTANCE: dict[str, str] = {
    "mcc": "make -C projects/mcc check",
    "meow": "make -C projects/meow check",
    "meuos-libc": "make -C projects/meuos-libc check",
    "meuos-toolchain": "make -C projects/meuos-toolchain check",
}

_VALID_PRIORITIES = {"P0", "P1", "P2", "P3", "P4"}
_KNOWN_SUBPROJECTS = ("mcc", "meow", "meuos-libc", "meuos-toolchain")


# --------------------------------------------------------------------------
# Subcommand: add
# --------------------------------------------------------------------------

def cmd_add(args: argparse.Namespace) -> int:
    root = Path(args.project_root).resolve()
    if args.subproject not in _KNOWN_SUBPROJECTS:
        print(f"WARN: subproject '{args.subproject}' is not in known list "
              f"{_KNOWN_SUBPROJECTS}. Proceeding anyway.", file=sys.stderr)
    if args.priority not in _VALID_PRIORITIES:
        print(f"ERROR: priority must be one of {sorted(_VALID_PRIORITIES)}, "
              f"got {args.priority!r}", file=sys.stderr)
        return 2
    name = args.name
    if not re.match(r"^[A-Za-z0-9._-]+$", name):
        print(f"ERROR: name must match [A-Za-z0-9._-]+, got {name!r}",
              file=sys.stderr)
        return 2
    if not name.endswith(".md"):
        name = name + ".md"
    todo_dir = root / "projects" / args.subproject / ".todo"
    todo_path = todo_dir / name
    if todo_path.exists():
        print(f"ERROR: todo already exists: {todo_path}", file=sys.stderr)
        return 2

    rel = f"projects/{args.subproject}/.todo/{name}"
    today = date.today().isoformat()
    priority = args.priority
    title = args.title.strip()
    note = (args.note or "").strip()
    body = (args.body or "").strip()
    acceptance = (args.acceptance or _DEFAULT_ACCEPTANCE.get(
        args.subproject, "make -C projects/" + args.subproject + " check")).strip()

    note_line = f"note: {note}\n" if note else ""
    body_section = (body + "\n\n") if body else ""
    content = (
        "<!--\n"
        f"priority: {priority}\n"
        "status: pending\n"
        f"start_ts: {today}\n"
        f"{note_line}"
        "-->\n"
        "\n"
        f"# {title}\n"
        "\n"
        f"{body_section}"
        "## 验收标准\n"
        "\n"
        f"```\n{acceptance}\n```\n"
    )
    if args.dry_run:
        print(f"[dry-run] would create: {todo_path}")
        print("---- file content ----")
        print(content)
        return 0
    todo_dir.mkdir(parents=True, exist_ok=True)
    todo_path.write_text(content, encoding="utf-8")
    print(f"created: {todo_path}")
    print(f"  priority:   {priority}")
    print(f"  title:      {title}")
    print(f"  relpath:    {rel}")
    print(f"  acceptance: {acceptance}")
    return 0


# --------------------------------------------------------------------------
# Subcommand: list
# --------------------------------------------------------------------------

def cmd_list(args: argparse.Namespace) -> int:
    root = Path(args.project_root).resolve()
    todos = ci_lib.list_todos(root, subprojects=None,
                              include_done=args.include_done)
    # filter by priority / subproject if requested
    if args.priority:
        todos = [t for t in todos
                 if (t.priority or "").upper() == args.priority.upper()]
    if args.subproject:
        todos = [t for t in todos if t.subproject == args.subproject]
    if args.json:
        print(json.dumps([
            {
                "rel_path": str(t.rel_path),
                "subproject": t.subproject,
                "name": t.name,
                "priority": t.priority,
                "status": t.status,
                "title": t.title,
            } for t in todos
        ], ensure_ascii=False, indent=2))
        return 0
    if not todos:
        print("(no todos)")
        return 0
    # Compact table
    print(f"{'pri':4} {'status':12} {'subproject':16} {'rel_path':50} title")
    print("-" * 100)
    for t in todos:
        rel = str(t.rel_path)
        print(f"{(t.priority or 'P?'):4} "
              f"{(t.status or 'pending'):12} "
              f"{t.subproject:16} "
              f"{rel:50} "
              f"{t.title or '(no title)'}")
    print(f"\ntotal: {len(todos)}")
    return 0


# --------------------------------------------------------------------------
# Subcommand: delete
# --------------------------------------------------------------------------

def _log_deletion(root: Path, rel: str, reason: str) -> None:
    log = ci_lib.STATE_DIR / "todo_deletions.log"
    ci_lib.STATE_DIR.mkdir(parents=True, exist_ok=True)
    with log.open("a", encoding="utf-8") as f:
        from datetime import datetime
        f.write(f"{datetime.now().isoformat()} {rel}  reason={reason}\n")


def cmd_delete(args: argparse.Namespace) -> int:
    root = Path(args.project_root).resolve()
    rel = args.relpath
    p = (root / rel).resolve()
    if not p.exists():
        print(f"ERROR: file not found: {p}", file=sys.stderr)
        return 2
    # Safety: only allow files under projects/*/.todo/
    try:
        rel_to_root = p.relative_to(root)
    except ValueError:
        print(f"ERROR: file is not under project root: {p}", file=sys.stderr)
        return 2
    parts = rel_to_root.parts
    if len(parts) < 4 or parts[0] != "projects" or parts[2] != ".todo" \
            or not parts[3].endswith(".md"):
        print(f"ERROR: refusing to delete non-todo file: {rel_to_root}",
              file=sys.stderr)
        return 2
    # Reject if not status:done (unless --force)
    todo = ci_lib.parse_todo(p, root, parts[1])
    if todo.status != "done" and not args.force:
        print(f"ERROR: todo is status='{todo.status}', not 'done'. "
              f"Use --force to override.", file=sys.stderr)
        return 2
    if args.dry_run:
        print(f"[dry-run] would delete: {p}")
        return 0
    # If tracked by git, use `git rm`; otherwise just rm.
    rc = subprocess.run(
        ["git", "ls-files", "--error-unmatch", str(p.relative_to(root))],
        cwd=str(root), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode
    if rc == 0:
        subprocess.run(["git", "rm", "-f", str(p.relative_to(root))],
                       cwd=str(root), check=True)
    else:
        p.unlink()
    reason = args.reason or ("status=done confirmed" if todo.status == "done"
                             else "forced delete")
    _log_deletion(root, str(rel_to_root), reason)
    print(f"deleted: {p}")
    return 0


# --------------------------------------------------------------------------
# Subcommand: sync
# --------------------------------------------------------------------------

def cmd_sync(args: argparse.Namespace) -> int:
    """Re-read every todo; report inconsistencies without modifying files."""
    root = Path(args.project_root).resolve()
    todos = ci_lib.list_todos(root, subprojects=None, include_done=True)
    issues: list[str] = []
    n_total = len(todos)
    n_done_unauth = 0
    n_no_acc = 0
    n_note_warn = 0
    for t in todos:
        actual = ci_lib._parse_front(t.path.read_text(
            encoding="utf-8", errors="replace"))[0]
        if actual.get("status") == "done" and "done_by_driver_ts" not in actual:
            n_done_unauth += 1
            issues.append(f"  ! {t.rel_path}: status=done but no "
                          f"done_by_driver_ts (manual / pre-fix mark)")
        if not ci_lib.has_acceptance_section(t):
            n_no_acc += 1
            issues.append(f"  ! {t.rel_path}: missing 验收标准 section")
        ws = ci_lib.scan_note_warnings(t)
        if ws:
            n_note_warn += 1
            issues.append(f"  ! {t.rel_path}: note warnings: {'; '.join(ws)}")
    print(f"sync: {n_total} todo files scanned")
    print(f"  status=done without done_by_driver_ts: {n_done_unauth}")
    print(f"  missing 验收标准 section:               {n_no_acc}")
    print(f"  note warnings:                          {n_note_warn}")
    if issues:
        print("\nDetails:")
        for i in issues:
            print(i)
    return 0 if not issues else 1


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="todo_admin.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("project_root", help="path to MeuOS-Kit repo root")
    sub = p.add_subparsers(dest="subcommand", required=True)

    p_add = sub.add_parser("add", help="create a new todo file")
    p_add.add_argument("--subproject", required=True)
    p_add.add_argument("--name", required=True,
                       help="todo filename without .md (or with .md)")
    p_add.add_argument("--title", required=True)
    p_add.add_argument("--priority", required=True,
                       help="one of P0..P4")
    p_add.add_argument("--note", default="")
    p_add.add_argument("--body", default="",
                       help="optional markdown body (without front matter)")
    p_add.add_argument("--acceptance", default="",
                       help="shell command for 验收标准 (default per sub)")
    p_add.add_argument("--dry-run", action="store_true")
    p_add.set_defaults(func=cmd_add)

    p_list = sub.add_parser("list", help="list todos")
    p_list.add_argument("--include-done", action="store_true")
    p_list.add_argument("--priority", default="")
    p_list.add_argument("--subproject", default="")
    p_list.add_argument("--json", action="store_true")
    p_list.set_defaults(func=cmd_list)

    p_del = sub.add_parser("delete", help="remove a todo file")
    p_del.add_argument("--relpath", required=True,
                       help="path relative to project_root, e.g. "
                            "projects/mcc/.todo/foo.md")
    p_del.add_argument("--reason", default="")
    p_del.add_argument("--force", action="store_true",
                       help="delete even if status is not 'done'")
    p_del.add_argument("--dry-run", action="store_true")
    p_del.set_defaults(func=cmd_delete)

    p_sync = sub.add_parser("sync", help="scan for inconsistencies")
    p_sync.set_defaults(func=cmd_sync)

    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
