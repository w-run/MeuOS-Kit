#!/usr/bin/env python3
# todo_scanner.py - .todo 任务扫描与状态跟踪
#
# 桥接 SPEC 的 [ ]/[x]/[!] 模型与项目实际的 .todo/*.md 状态文档：
# 每个 projects/<component>/.todo/<name>.md 视为一个任务单元，ID 为
# "<component>:<name>"。完成状态记录在 state/task_status.json 侧车文件中。
#
# 子命令：
#   pending   - 列出未完成任务（JSON 数组）
#   status    - 查询某任务状态
#   done      - 标记完成
#   abandon   - 标记放弃（[!]）
#   milestone-ready <component> - 该组件是否所有 .todo 已完成
#
# 用法：python3 todo_scanner.py pending

import argparse
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CICR_ROOT = os.path.dirname(HERE)
REPO_ROOT = os.path.dirname(CICR_ROOT)
STATE_PATH = os.path.join(CICR_ROOT, "state", "task_status.json")
PROJECTS = os.path.join(REPO_ROOT, "projects")


def load_status():
    if not os.path.exists(STATE_PATH):
        return {}
    with open(STATE_PATH) as f:
        try:
            return json.load(f)
        except json.JSONDecodeError:
            return {}


def save_status(data):
    os.makedirs(os.path.dirname(STATE_PATH), exist_ok=True)
    with open(STATE_PATH, "w") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


def scan_tasks():
    """扫描 projects/*/.todo/*.md，返回任务列表。"""
    tasks = []
    for comp_dir in sorted(glob.glob(os.path.join(PROJECTS, "*"))):
        comp = os.path.basename(comp_dir)
        todo_dir = os.path.join(comp_dir, ".todo")
        if not os.path.isdir(todo_dir):
            continue
        for md in sorted(glob.glob(os.path.join(todo_dir, "*.md"))):
            name = os.path.splitext(os.path.basename(md))[0]
            tasks.append({
                "task_id": f"{comp}:{name}",
                "component": comp,
                "name": name,
                "path": os.path.relpath(md, REPO_ROOT),
            })
    return tasks


def pending():
    status = load_status()
    out = []
    for t in scan_tasks():
        st = status.get(t["task_id"], {}).get("status", "pending")
        if st == "pending":
            out.append(t)
    return out


def set_status(task_id, new_status, extra=None):
    status = load_status()
    entry = status.get(task_id, {})
    entry["status"] = new_status
    entry["updated_ts"] = __import__("time").strftime("%Y-%m-%dT%H:%M:%SZ", __import__("time").gmtime())
    if extra:
        entry.update(extra)
    status[task_id] = entry
    save_status(status)


def milestone_ready(component):
    """组件的所有 .todo 是否均已完成/放弃。"""
    status = load_status()
    for t in scan_tasks():
        if t["component"] != component:
            continue
        st = status.get(t["task_id"], {}).get("status", "pending")
        if st == "pending":
            return False
    return True


def main():
    ap = argparse.ArgumentParser(description="MeuOS Kit .todo 扫描器")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("pending")
    s = sub.add_parser("status"); s.add_argument("task_id")
    s = sub.add_parser("done"); s.add_argument("task_id")
    s = sub.add_parser("abandon"); s.add_argument("task_id")
    s = sub.add_parser("milestone-ready"); s.add_argument("component")
    args = ap.parse_args()

    if args.cmd == "pending":
        print(json.dumps(pending(), indent=2, ensure_ascii=False))
    elif args.cmd == "status":
        status = load_status()
        print(json.dumps(status.get(args.task_id, {"status": "pending"}), ensure_ascii=False))
    elif args.cmd == "done":
        set_status(args.task_id, "done")
        print(json.dumps({"task_id": args.task_id, "status": "done"}))
    elif args.cmd == "abandon":
        set_status(args.task_id, "abandoned")
        print(json.dumps({"task_id": args.task_id, "status": "abandoned"}))
    elif args.cmd == "milestone-ready":
        ready = milestone_ready(args.component)
        print(json.dumps({"component": args.component, "ready": ready}))


if __name__ == "__main__":
    main()
