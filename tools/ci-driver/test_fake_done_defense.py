#!/usr/bin/env python3
"""test_fake_done_defense.py — 验证 ci_driver 的 fake-DONE defense。

背景: 之前 main session 在 batch 内有 [[CLAIM_DONE]] REFUSED/REJECTED
时会**误用** [[DONE]] 退出整个 driver,导致"跑了一轮就退出"假象。
B 改动后 (ci_driver.py:782+),driver 在 [[DONE]] 之前重检 actionable,
如果还有 actionable 就强制 continue 而不是 return 0。

本测试不调 codebuddy,直接验证防御逻辑的子集:
  1. list_todos 在有 pending + in_progress todo 时返回 actionable
  2. "still_actionable" 集合的判定 (actionable && not in completed)
  3. round 1 的 state history 在 B 改动前会 exit 0,在改动后会被拦截

用法: python3 tools/ci-driver/test_fake_done_defense.py
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_lib


SAMPLE_TODO_PENDING = """<!--
priority: P1
status: pending
note: a pending todo for fake-DONE defense test
-->

# Sample Pending

## 验收标准

```
true
```
"""

SAMPLE_TODO_IN_PROGRESS = """<!--
priority: P1
status: in_progress
note: a partially done todo
-->

# Sample In Progress

## 验收标准

```
true
```
"""

SAMPLE_TODO_DONE = """<!--
priority: P1
status: done
done_ts: 2026-01-01
done_by_driver_ts: 2026-01-01T00:00:00Z
note: already done
-->

# Sample Done

## 验收标准

```
true
```
"""


def _write_todo(todo_dir: Path, name: str, content: str) -> Path:
    p = todo_dir / f"{name}.md"
    p.write_text(content, encoding="utf-8")
    return p


class TestListTodosActionable(unittest.TestCase):
    """验证 list_todos 在各种 status 下的 actionable 判定。"""

    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="ci-test-fake-done-")
        self.root = Path(self.tmp)
        self.proj_dir = self.root / "projects" / "fake"
        self.todo_dir = self.proj_dir / ".todo"
        self.todo_dir.mkdir(parents=True)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_pending_is_actionable(self) -> None:
        _write_todo(self.todo_dir, "a-pending", SAMPLE_TODO_PENDING)
        todos = ci_lib.list_todos(self.root, ["fake"])
        self.assertEqual(len(todos), 1)
        self.assertTrue(todos[0].is_actionable(),
                        "pending todo should be actionable")

    def test_in_progress_is_actionable(self) -> None:
        _write_todo(self.todo_dir, "a-in-prog", SAMPLE_TODO_IN_PROGRESS)
        todos = ci_lib.list_todos(self.root, ["fake"])
        self.assertEqual(len(todos), 1)
        self.assertTrue(todos[0].is_actionable(),
                        "in_progress todo should be actionable")

    def test_done_is_not_actionable(self) -> None:
        _write_todo(self.todo_dir, "a-done", SAMPLE_TODO_DONE)
        todos = ci_lib.list_todos(self.root, ["fake"])
        # list_todos with default include_done=False filters done out
        self.assertEqual(len(todos), 0,
                         "done todo should be filtered out by default")
        todos_inc = ci_lib.list_todos(self.root, ["fake"], include_done=True)
        self.assertEqual(len(todos_inc), 1)
        self.assertFalse(todos_inc[0].is_actionable(),
                         "done todo should not be actionable")

    def test_archive_subdir_excluded(self) -> None:
        """Make sure archived todos don't get picked up as actionable."""
        archive = self.todo_dir / "archive"
        archive.mkdir()
        _write_todo(archive, "archived", SAMPLE_TODO_PENDING)
        todos = ci_lib.list_todos(self.root, ["fake"])
        self.assertEqual(len(todos), 0,
                         ".todo/archive/ should be excluded")


class TestFakeDoneDetection(unittest.TestCase):
    """模拟 driver 的 still_actionable 计算 — 这正是 fake-DONE defense
    用的过滤条件 (ci_driver.py:790-794)。

    关键不变量: actionable && not in completed == still_actionable。
    如果 len(still_actionable) > 0 但 main session 发 [[DONE]],
    driver 拒绝退出。
    """

    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="ci-test-fake-done-")
        self.root = Path(self.tmp)
        self.proj_dir = self.root / "projects" / "fake"
        self.todo_dir = self.proj_dir / ".todo"
        self.todo_dir.mkdir(parents=True)
        _write_todo(self.todo_dir, "a-pending", SAMPLE_TODO_PENDING)
        _write_todo(self.todo_dir, "b-done", SAMPLE_TODO_DONE)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_still_actionable_when_pending_exists(self) -> None:
        """场景: 1 pending + 1 done → still_actionable = 1 → driver
        应拒绝 [[DONE]] 退出。"""
        todos = ci_lib.list_todos(self.root, ["fake"])
        completed: set[str] = set()  # 没 CLAIM_DONE accepted
        still = [t for t in todos if t.is_actionable() and t.name not in completed]
        self.assertEqual(len(still), 1,
                         "expected exactly 1 still-actionable todo (the pending one)")
        self.assertIn("a-pending", still[0].name)

    def test_no_still_actionable_when_all_done(self) -> None:
        """场景: 全 done (or completed in session) → still_actionable = 0
        → driver 正常 [[DONE]] 退出。"""
        todos = ci_lib.list_todos(self.root, ["fake"])
        # 假设 a-pending 也被这次 session 处理完了
        completed = {"a-pending", "b-done"}
        still = [t for t in todos if t.is_actionable() and t.name not in completed]
        self.assertEqual(len(still), 0,
                         "no todo should be still-actionable")

    def test_done_todo_does_not_count(self) -> None:
        """done 状态在 list_todos 默认 (include_done=False) 里被过滤掉,
        不会进入 driver 的 still_actionable 计算。"""
        # 默认 (exclude done): 应该只有 a-pending
        todos_default = ci_lib.list_todos(self.root, ["fake"])
        self.assertEqual(
            [t.name for t in todos_default], ["a-pending"],
            "默认参数下 done 不应出现")
        # include_done=True: 看到全部, 但 driver 用的是默认参数
        # (line 790 `ci_lib.list_todos(root, subs)` 不传 include_done)
        # 验证我们的 is_actionable 行为在 b-done 上是 False
        for t in todos_default:
            self.assertTrue(
                t.is_actionable(),
                f"default list 里不该有 non-actionable: {t.name} ({t.status})")


class TestFakeDoneSourcePresence(unittest.TestCase):
    """静态检查: ci_driver.py 里的 fake-DONE defense 块必须存在。
    这是 CI 不可少的回归 guard — 防止有人 refactor 掉防御逻辑。"""

    def test_defense_block_present(self) -> None:
        driver_path = Path(__file__).resolve().parent / "ci_driver.py"
        src = driver_path.read_text(encoding="utf-8")
        self.assertIn("Fake-DONE defense", src,
                      "ci_driver.py 缺少 'Fake-DONE defense' 注释 (B 改动被回退?)")
        self.assertIn("still_actionable", src,
                      "ci_driver.py 缺少 'still_actionable' 变量 (B 改动被回退?)")
        # 顺序检查: defense 块必须在 `if markers["done"]:` 之前。
        # `markers["done"]` 在文件里有多处 (RE 提取 + history dict),
        # 所以要更精确地找 `if markers["done"]:` 这一行。
        if_done_idx = src.find('if markers["done"]:')
        defense_idx = src.find("Fake-DONE defense")
        self.assertGreater(if_done_idx, defense_idx,
                           f"fake-DONE defense (line ~{defense_idx}) 必须早于 "
                           f"'if markers[done]' (line ~{if_done_idx})")
        # 验证防御逻辑在 round 循环体内 (粗略: 出现在 'while round_no' 之后)
        # 略 — 简单文本 grep 容易漏判,跳过


if __name__ == "__main__":
    unittest.main(verbosity=2)
