#!/usr/bin/env python3
"""test_task_mode.py — 验证 ci_lib 的 task mode 辅助函数。

测试:
- hash_task_desc 确定性 + 不同描述产生不同 hash
- init_task_mode 生成正确结构
- scan_task_todos 按 task_id 过滤 (含衍生 todo)
- save/load_task_mode round-trip
"""
from __future__ import annotations

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_lib


class TestHashTaskDesc(unittest.TestCase):
    def test_deterministic(self) -> None:
        h1 = ci_lib.hash_task_desc("fix i386 float ABI")
        h2 = ci_lib.hash_task_desc("fix i386 float ABI")
        self.assertEqual(h1, h2)
        self.assertEqual(len(h1), 8)

    def test_different_descs(self) -> None:
        h1 = ci_lib.hash_task_desc("fix i386 float ABI")
        h2 = ci_lib.hash_task_desc("add riscv64 support")
        self.assertNotEqual(h1, h2)

    def test_empty_desc(self) -> None:
        h = ci_lib.hash_task_desc("")
        self.assertEqual(len(h), 8)


class TestInitTaskMode(unittest.TestCase):
    def test_structure(self) -> None:
        tm = ci_lib.init_task_mode("test task")
        self.assertTrue(tm["active"])
        self.assertEqual(len(tm["task_id"]), 8)
        self.assertEqual(tm["task_desc"], "test task")
        self.assertEqual(tm["todo_names"], [])
        self.assertIsNone(tm["finished_ts"])
        self.assertIsNotNone(tm["started_ts"])

    def test_explicit_task_id(self) -> None:
        tm = ci_lib.init_task_mode("test", task_id="custom123")
        self.assertEqual(tm["task_id"], "custom123")

    def test_desc_capped(self) -> None:
        long_desc = "x" * 1000
        tm = ci_lib.init_task_mode(long_desc)
        self.assertEqual(len(tm["task_desc"]), 500)


class TestScanTaskTodos(unittest.TestCase):
    """验证 scan_task_todos 按 task_id 过滤。"""

    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="ci-test-task-")
        self.root = Path(self.tmp)
        self.todo_dir = self.root / "projects" / "fake" / ".todo"
        self.todo_dir.mkdir(parents=True)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _write_todo(self, name: str, task_id: str | None) -> Path:
        p = self.todo_dir / f"{name}.md"
        front = "<!--\npriority: P1\nstatus: pending\n"
        if task_id:
            front += f"task_id: {task_id}\n"
        front += "-->\n\n# Todo\n\n## 验收标准\n\n```\ntrue\n```\n"
        p.write_text(front, encoding="utf-8")
        return p

    def test_scan_finds_matching_task_id(self) -> None:
        self._write_todo("task-abc123-01", "abc123")
        self._write_todo("task-abc123-02", "abc123")
        self._write_todo("other-todo", None)
        results = ci_lib.scan_task_todos(self.root, ["fake"], "abc123")
        self.assertEqual(len(results), 2)
        names = {t.name for t in results}
        self.assertEqual(names, {"task-abc123-01", "task-abc123-02"})

    def test_scan_excludes_other_task_id(self) -> None:
        self._write_todo("task-aaa-01", "aaa")
        self._write_todo("task-bbb-01", "bbb")
        results = ci_lib.scan_task_todos(self.root, ["fake"], "aaa")
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].name, "task-aaa-01")

    def test_scan_includes_derivative(self) -> None:
        """Derivative todos (created by main session during execution)
        should be found if they have the same task_id."""
        self._write_todo("task-abc-01", "abc")
        self._write_todo("task-abc-derivative", "abc")  # derivative
        results = ci_lib.scan_task_todos(self.root, ["fake"], "abc")
        self.assertEqual(len(results), 2)


class TestSaveLoadTaskMode(unittest.TestCase):
    """验证 save/load round-trip (uses real state dir)."""

    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="ci-test-task-")
        self.orig_state_dir = ci_lib.STATE_DIR
        ci_lib.STATE_DIR = Path(self.tmp) / "state"
        ci_lib.STATE_DIR.mkdir()

    def tearDown(self) -> None:
        ci_lib.STATE_DIR = self.orig_state_dir
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_round_trip(self) -> None:
        tm = ci_lib.init_task_mode("round trip test")
        tm["todo_names"] = ["task-01", "task-02"]
        ci_lib.save_task_mode(tm)
        loaded = ci_lib.load_task_mode()
        self.assertEqual(loaded["task_id"], tm["task_id"])
        self.assertEqual(loaded["todo_names"], ["task-01", "task-02"])
        self.assertTrue(loaded["active"])

    def test_empty_when_no_state(self) -> None:
        loaded = ci_lib.load_task_mode()
        self.assertEqual(loaded, {})


if __name__ == "__main__":
    unittest.main(verbosity=2)
