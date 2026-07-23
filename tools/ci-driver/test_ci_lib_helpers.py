#!/usr/bin/env python3
"""test_ci_lib_helpers.py — minimal smoke tests for the new
mark_todo_done_by_driver / rollback_unauthorized_done /
extract_acceptance_cmds / scan_note_warnings helpers.

Run with: python3 tools/ci-driver/test_ci_lib_helpers.py
"""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_lib


SAMPLE_TODO = """<!--
priority: P1
status: pending
note: simple todo for test
-->

# Test Todo

## Background

Just a test.

## 验收标准

```
echo "hello"
true
```

## Notes

This is the body.
"""


def _setup(text: str = SAMPLE_TODO) -> tuple[Path, Path, ci_lib.Todo]:
    """Write a fake todo file under a temp project; return (project_root, todo_path, todo)."""
    tmp = tempfile.mkdtemp(prefix="ci_test_")
    proj = Path(tmp) / "proj"
    proj.mkdir()
    todo_dir = proj / "projects" / "fake" / ".todo"
    todo_dir.mkdir(parents=True)
    p = todo_dir / "demo.md"
    p.write_text(text, encoding="utf-8")
    return proj, p, ci_lib.parse_todo(p, proj, "fake")


class TestExtractAcceptanceCmds(unittest.TestCase):

    def test_basic(self):
        _, p, t = _setup()
        self.assertTrue(ci_lib.has_acceptance_section(t))
        cmds = ci_lib.extract_acceptance_cmds(t)
        self.assertEqual(cmds, ['echo "hello"', "true"])

    def test_no_acceptance(self):
        # Remove the entire 验收标准 H2 section, including the body inside it
        import re
        text = re.sub(
            r"\n## 验收标准.*?(?=\n## |\Z)",
            "",
            SAMPLE_TODO,
            count=1,
            flags=re.DOTALL,
        )
        _, p, t = _setup(text)
        self.assertFalse(ci_lib.has_acceptance_section(t))
        self.assertEqual(ci_lib.extract_acceptance_cmds(t), [])

    def test_english_title(self):
        text = SAMPLE_TODO.replace("## 验收标准", "## Acceptance Criteria")
        _, p, t = _setup(text)
        self.assertTrue(ci_lib.has_acceptance_section(t))
        self.assertEqual(len(ci_lib.extract_acceptance_cmds(t)), 2)

    def test_section_ends_at_next_h2(self):
        text = SAMPLE_TODO + "\n## Other\n\n```\necho should_not_appear\n```\n"
        _, p, t = _setup(text)
        cmds = ci_lib.extract_acceptance_cmds(t)
        self.assertEqual(cmds, ['echo "hello"', "true"])


class TestMarkDoneByDriver(unittest.TestCase):

    def test_driver_done_sets_done_by_driver_ts(self):
        _, p, t = _setup()
        ci_lib.mark_todo_done_by_driver(t, note="make check: 5/5 OK")
        meta = ci_lib._parse_front(p.read_text(encoding="utf-8"))[0]
        self.assertEqual(meta.get("status"), "done")
        self.assertIn("done_ts", meta)
        self.assertIn("done_by_driver_ts", meta)
        self.assertEqual(meta.get("done_note"), "make check: 5/5 OK")
        # Priority must be preserved
        self.assertEqual(meta.get("priority"), "P1")

    def test_rollback_unauthorized(self):
        _, p, t = _setup()
        # Simulate an LLM writing done directly
        text = p.read_text(encoding="utf-8")
        text = text.replace("status: pending", "status: done")
        p.write_text(text, encoding="utf-8")
        t2 = ci_lib.parse_todo(p, t.path.parents[3], "fake")
        ci_lib.rollback_unauthorized_done(t2, reason="no driver verify")
        meta = ci_lib._parse_front(p.read_text(encoding="utf-8"))[0]
        self.assertEqual(meta.get("status"), "in_progress")
        self.assertIn("driver rollback: no driver verify",
                      meta.get("progress_note", ""))
        self.assertNotIn("done_ts", meta)
        self.assertNotIn("done_by_driver_ts", meta)


class TestScanNoteWarnings(unittest.TestCase):

    def test_clean_note(self):
        text = SAMPLE_TODO.replace("note: simple todo for test",
                                   "note: foo bar baz")
        _, p, t = _setup(text)
        self.assertEqual(ci_lib.scan_note_warnings(t), [])

    def test_warning(self):
        text = SAMPLE_TODO.replace("note: simple todo for test",
                                   "note: 阶段 A 已完成")
        _, p, t = _setup(text)
        ws = ci_lib.scan_note_warnings(t)
        self.assertTrue(any("已完成" in w for w in ws))


class TestRunAcceptanceCmds(unittest.TestCase):
    """Smoke test the subprocess wrapper via a tiny driver helper."""

    def test_run_passing(self):
        from ci_driver import _run_acceptance_cmds
        _, p, t = _setup()
        with tempfile.TemporaryDirectory() as cwd:
            ok, out, err = _run_acceptance_cmds(t, cwd=Path(cwd), timeout_s=10)
        self.assertTrue(ok, f"acceptance should pass; got: {out!r} {err!r}")

    def test_run_failing(self):
        text = SAMPLE_TODO.replace("true", "false")
        _, p, t = _setup(text)
        from ci_driver import _run_acceptance_cmds
        with tempfile.TemporaryDirectory() as cwd:
            ok, out, err = _run_acceptance_cmds(t, cwd=Path(cwd), timeout_s=10)
        self.assertFalse(ok)
        self.assertIn("false", err)

    def test_run_no_acceptance(self):
        text = SAMPLE_TODO.replace('## 验收标准\n\n```\necho "hello"\ntrue\n```\n', "")
        _, p, t = _setup(text)
        from ci_driver import _run_acceptance_cmds
        with tempfile.TemporaryDirectory() as cwd:
            ok, out, err = _run_acceptance_cmds(t, cwd=Path(cwd), timeout_s=10)
        self.assertFalse(ok)
        self.assertIn("no acceptance", err)


class TestDetectMarkers(unittest.TestCase):
    def test_claim_dones(self):
        from ci_driver import _detect_markers
        out = (
            "...\n"
            "[[CLAIM_DONE: projects/mcc/.todo/foo.md]]\n"
            "[[CLAIM_DONE: projects/mcc/.todo/bar.md: extra note]]\n"
        )
        m = _detect_markers(out)
        self.assertEqual(len(m["claim_dones"]), 2)
        self.assertEqual(m["claim_dones"][0][0], "projects/mcc/.todo/foo.md")
        self.assertEqual(m["claim_dones"][1][0], "projects/mcc/.todo/bar.md")
        self.assertEqual(m["claim_dones"][1][1], "extra note")

    def test_claim_faileds(self):
        from ci_driver import _detect_markers
        out = "[[CLAIM_FAILED: projects/x/.todo/y.md: build failed]]"
        m = _detect_markers(out)
        self.assertEqual(m["claim_faileds"],
                         [("projects/x/.todo/y.md", "build failed")])


if __name__ == "__main__":
    unittest.main(verbosity=2)
