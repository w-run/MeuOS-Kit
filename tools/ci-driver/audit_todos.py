#!/usr/bin/env python3
"""audit_todos.py — scan every projects/<sub>/.todo/*.md and report:
  1. which todos are missing a `## 验收标准` / Acceptance section
  2. which todos have a misleading `note:` field (contains completion language)
  3. optionally apply fixes:
     - insert a placeholder `## 验收标准` section if missing
     - rewrite a misleading `note:` to a content-only description

Usage:
  python3 tools/ci-driver/audit_todos.py <project_root>           # report only
  python3 tools/ci-driver/audit_todos.py <project_root> --apply   # apply fixes
  python3 tools/ci-driver/audit_todos.py <project_root> --subprojects mcc,meow
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# Allow running from anywhere
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_lib


# Default acceptance placeholders per subproject. Keep them minimal —
# the main session is supposed to refine or replace them.
_DEFAULT_ACCEPTANCE: dict[str, str] = {
    "mcc": (
        "## 验收标准\n\n"
        "<!-- TODO(main session): replace these placeholders with concrete "
        "shell commands the driver should run to verify this todo. The "
        "commands must exit 0 on success; any non-zero exit means the todo "
        "is NOT done. Keep the fenced block format below. -->\n\n"
        "```\n"
        "make -C projects/mcc check\n"
        "```\n"
    ),
    "meow": (
        "## 验收标准\n\n"
        "<!-- TODO(main session): fill in concrete commands. -->\n\n"
        "```\n"
        "make -C projects/meow check\n"
        "```\n"
    ),
    "meuos-libc": (
        "## 验收标准\n\n"
        "<!-- TODO(main session): fill in concrete commands. -->\n\n"
        "```\n"
        "make -C projects/meuos-libc check\n"
        "```\n"
    ),
    "meuos-toolchain": (
        "## 验收标准\n\n"
        "<!-- TODO(main session): fill in concrete commands. -->\n\n"
        "```\n"
        "make -C projects/meuos-toolchain check\n"
        "```\n"
    ),
}

# Replacement note strings, keyed by todo rel_path. Falls back to a
# generic "todo content" descriptor if the relpath is not listed.
_REWRITE_NOTES: dict[str, str] = {
    "projects/mcc/.todo/cpp-shared-backend.md":
        "mcc/m++ 共享后端架构(libmcc 化)的分阶段计划;阶段 A 已落地,阶段 B/C/D 待 m++ 启动时实施",
    "projects/mcc/.todo/gd-tls.md":
        "5 个跨架构 GD-TLS 缺口的分阶段实施计划;Phase A 是 Phase B/C 的前置;Gap 5 受 P6 动态链接阻塞",
    "projects/meuos-toolchain/.todo/p0-foundation-ar.md":
        "meuos-toolchain P0 阶段: libelf / ar / ranlib 等归档基础设施",
    "projects/meuos-toolchain/.todo/p1-x86_64-as.md":
        "meuos-toolchain P1 阶段: x86_64 汇编器实现",
    "projects/meuos-toolchain/.todo/p2-x86_64-ld.md":
        "meuos-toolchain P2 阶段: x86_64 静态链接器实现",
    "projects/meuos-libc/.todo/native-linker.md":
        "为 mt/ld 原生链接器添加 check-native-linker 验证目标",
    "projects/meow/.todo/native-shell.md":
        "切到 ${MEUOS_SYSROOT}/bin/sh;前置是 MeuOS userspace 自身的 shell 落地",
    "projects/meow/.todo/dag-dedup.md":
        "-jN 并行执行的间接依赖重复问题;非阻塞,按需优化",
    "projects/meow/.todo/native-kit-build.md":
        "用 meow 原生构建 mcc / meuos-libc / meow;Phase 4 自举链前置",
    "projects/meuos-libc/.todo/non-x86_64-runtime.md":
        "x86_64 / aarch64 / i386 / riscv64 / loongarch64 5 架构门禁",
    "projects/mcc/.todo/aarch64-store-fix.md":
        "aarch64 store 相关:omap Ki 化 + isel store 特殊处理;作为永久参考保留",
    "projects/meuos-libc/.todo/32bit-time64.md":
        "32 位 time64 支持的类型 / syscall / qemu runtime gate",
    "projects/mcc/.todo/opt-warn-levels.md":
        "mcc 优化级别与警告级别参数化;9e1811b 已实现 -O0/1/2/3/s 与 -w/-Wall/-Werror 联动",
    "projects/mcc/.todo/i386-kl-arith.md":
        "i386_sysv_abi 预扫描重写为 libc 软算术调用",
    "projects/mcc/.todo/general-dynamic-tls.md":
        "已并入 gd-tls.md(更详细的 5 缺口分解),本文件留档",
    "projects/meuos-libc/.todo/i386-printf-va.md":
        "targ.c typevalist 改 struct,跨函数 va_list 永久参考",
}


def _suggested_note(todo: ci_lib.Todo) -> str:
    rel = str(todo.rel_path)
    if rel in _REWRITE_NOTES:
        return _REWRITE_NOTES[rel]
    # Fallback: use the title (first H1) as the descriptor
    if todo.title:
        return todo.title
    return "(todo content)"


def _apply_placeholder(todo: ci_lib.Todo) -> bool:
    """Insert a default 验收标准 section at end of file. Returns True if changed."""
    text = todo.path.read_text(encoding="utf-8", errors="replace")
    if ci_lib.has_acceptance_section(todo):
        return False
    placeholder = _DEFAULT_ACCEPTANCE.get(
        todo.subproject, _DEFAULT_ACCEPTANCE["mcc"],
    )
    if not text.endswith("\n"):
        text += "\n"
    text += "\n" + placeholder + "\n"
    todo.path.write_text(text, encoding="utf-8")
    return True


def _apply_note_rewrite(todo: ci_lib.Todo, new_note: str) -> bool:
    """Replace the `note:` value in the todo's front matter. Returns True if changed."""
    text = todo.path.read_text(encoding="utf-8", errors="replace")
    import re
    m = re.search(r"<!--\s*(.*?)\s*-->", text, re.DOTALL)
    if not m:
        return False
    block = m.group(1)
    new_lines: list[str] = []
    replaced = False
    for line in block.splitlines():
        kv = re.match(r"^\s*note\s*:\s*.*$", line)
        if kv and not replaced:
            new_lines.append(f"note: {new_note}")
            replaced = True
        else:
            new_lines.append(line)
    if not replaced:
        # No `note:` key; append one
        new_lines.append(f"note: {new_note}")
        replaced = True
    new_block = "\n".join(new_lines)
    new_text = text[:m.start()] + f"<!--\n{new_block}\n-->" + text[m.end():]
    if new_text == text:
        return False
    todo.path.write_text(new_text, encoding="utf-8")
    return True


def audit_todos(project_root: Path, subprojects: list[str] | None) -> list[dict]:
    todos = ci_lib.list_todos(project_root, subprojects, include_done=True)
    report: list[dict] = []
    for t in todos:
        entry = {
            "rel_path": t.rel_path,
            "subproject": t.subproject,
            "name": t.name,
            "priority": t.priority,
            "status": t.status,
            "title": t.title,
            "has_acceptance": ci_lib.has_acceptance_section(t),
            "acceptance_cmds": ci_lib.extract_acceptance_cmds(t),
            "note_warnings": ci_lib.scan_note_warnings(t),
        }
        report.append(entry)
    return report


def print_report(report: list[dict]) -> None:
    n_total = len(report)
    n_no_acc = sum(1 for r in report if not r["has_acceptance"])
    n_warn = sum(1 for r in report if r["note_warnings"])
    print(f"\n=== Todo audit summary ===")
    print(f"total:           {n_total}")
    print(f"missing 验收标准: {n_no_acc}")
    print(f"note warnings:   {n_warn}\n")
    for r in report:
        flags = []
        if not r["has_acceptance"]:
            flags.append("NO-ACCEPTANCE")
        if r["note_warnings"]:
            flags.append(f"NOTE-WARN({len(r['note_warnings'])})")
        flag_str = " ".join(flags) if flags else "ok"
        print(f"  [{r['priority'] or 'P?':3}] {r['status']:11} "
              f"{r['rel_path']:60}  {flag_str}")
        for w in r["note_warnings"]:
            print(f"         note warn: {w}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("project_root")
    ap.add_argument("--subprojects", default="",
                    help="comma-separated list, default: all")
    ap.add_argument("--apply", action="store_true",
                    help="apply fixes (insert missing acceptance, rewrite notes)")
    ap.add_argument("--json", action="store_true", help="JSON output")
    args = ap.parse_args()
    root = Path(args.project_root).resolve()
    subs = [s.strip() for s in args.subprojects.split(",") if s.strip()] or None
    report = audit_todos(root, subs)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print_report(report)
    if args.apply:
        print("\n=== Applying fixes ===")
        n_placeholder = 0
        n_note_rewrite = 0
        todos = ci_lib.list_todos(root, subs, include_done=True)
        todo_by_rel = {str(t.rel_path): t for t in todos}
        for r in report:
            t = todo_by_rel.get(r["rel_path"])
            if t is None:
                continue
            if not r["has_acceptance"]:
                if _apply_placeholder(t):
                    n_placeholder += 1
                    print(f"  + placeholder inserted: {r['rel_path']}")
            if r["note_warnings"]:
                new_note = _suggested_note(t)
                if _apply_note_rewrite(t, new_note):
                    n_note_rewrite += 1
                    print(f"  ~ note rewritten:       {r['rel_path']}")
                    print(f"      -> {new_note[:100]}")
        print(f"\nDone. placeholders added: {n_placeholder}, "
              f"notes rewritten: {n_note_rewrite}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
