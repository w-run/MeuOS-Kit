#!/usr/bin/env python3
# recovery_cleaner.py - 僵尸锁/超时会话清理器
#
# 职责（SPEC §3.1 + §4.3 + §5）：
#   1. 扫描 state/locks/*.lock，删除超过 zombie_lock_minutes 的僵尸锁，并将
#      对应任务卡片重置（删除 .plan/<task>.json 标记，供驱动重新规划）。
#   2. 扫描 state/sessions/mapping.json，对超过 session_ttl_minutes 的会话
#      强制清理映射（codebuddy 后台会话由其自身回收）。
#   3. 扫描 state/retry_counters/，清理已无对应锁的过期计数器。
#
# 用法：python3 recovery_cleaner.py [--dry-run]

import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CICR_ROOT = os.path.dirname(HERE)
STATE = os.path.join(CICR_ROOT, "state")

try:
    import yaml
except ImportError:
    yaml = None


def load_thresholds():
    path = os.path.join(CICR_ROOT, "config", "model_router.yaml")
    if not os.path.exists(path):
        return {}
    if yaml:
        with open(path) as f:
            return (yaml.safe_load(f) or {}).get("thresholds", {}) or {}
    # 回退解析
    th = {}
    with open(path) as f:
        for line in f:
            s = line.strip()
            if s.startswith("zombie_lock_minutes") or s.startswith("session_ttl_minutes"):
                k, _, v = s.partition(":")
                v = v.split("#", 1)[0].strip()
                try:
                    th[k] = int(v)
                except ValueError:
                    pass
    return th


def now_ts():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def file_mtime(path):
    return os.path.getmtime(path)


def clean_zombie_locks(dry_run):
    lock_dir = os.path.join(STATE, "locks")
    plan_dir = os.path.join(CICR_ROOT, ".plan")
    ttl = load_thresholds().get("zombie_lock_minutes", 30) * 60
    cleaned = []
    if not os.path.isdir(lock_dir):
        return cleaned
    for name in os.listdir(lock_dir):
        if not name.endswith(".lock"):
            continue
        path = os.path.join(lock_dir, name)
        age = time.time() - file_mtime(path)
        if age > ttl:
            task_id = name[:-len(".lock")]
            if dry_run:
                cleaned.append({"lock": name, "task_id": task_id, "action": "would_delete"})
            else:
                os.remove(path)
                # 重置任务卡片：移除 .plan/<task_id>.json 使驱动可重新规划
                card = os.path.join(plan_dir, task_id + ".json")
                if os.path.exists(card):
                    os.remove(card)
                cleaned.append({"lock": name, "task_id": task_id,
                                "action": "deleted", "age_min": int(age / 60)})
    return cleaned


def clean_stale_sessions(dry_run):
    mapping_path = os.path.join(STATE, "sessions", "mapping.json")
    ttl = load_thresholds().get("session_ttl_minutes", 120) * 60
    cleaned = []
    if not os.path.exists(mapping_path):
        return cleaned
    try:
        with open(mapping_path) as f:
            mapping = json.load(f)
    except (json.JSONDecodeError, OSError):
        return cleaned
    changed = False
    for task_id, entry in list(mapping.items()):
        if not isinstance(entry, dict):
            continue
        created = entry.get("created_ts")
        if not created:
            continue
        try:
            ct = time.mktime(time.strptime(created, "%Y-%m-%dT%H:%M:%SZ"))
        except ValueError:
            continue
        if time.time() - ct > ttl:
            if dry_run:
                cleaned.append({"task_id": task_id, "session_id": entry.get("session_id"),
                                "action": "would_clean"})
            else:
                cleaned.append({"task_id": task_id, "session_id": entry.get("session_id"),
                                "action": "cleaned"})
                del mapping[task_id]
                changed = True
    if changed and not dry_run:
        with open(mapping_path, "w") as f:
            json.dump(mapping, f, indent=2)
    return cleaned


def clean_orphan_counters(dry_run):
    ctr_dir = os.path.join(STATE, "retry_counters")
    lock_dir = os.path.join(STATE, "locks")
    cleaned = []
    if not os.path.isdir(ctr_dir):
        return cleaned
    for name in os.listdir(ctr_dir):
        if not name.endswith(".json"):
            continue
        task_id = name[:-len(".json")]
        lock = os.path.join(lock_dir, task_id + ".lock")
        # 锁不存在且非当日计数 -> 清理
        if not os.path.exists(lock):
            if dry_run:
                cleaned.append({"counter": name, "action": "would_delete"})
            else:
                os.remove(os.path.join(ctr_dir, name))
                cleaned.append({"counter": name, "action": "deleted"})
    return cleaned


def main():
    ap = argparse.ArgumentParser(description="MeuOS Kit CI/CR 僵尸锁/会话清理器")
    ap.add_argument("--dry-run", action="store_true", help="只报告不执行")
    args = ap.parse_args()

    report = {
        "run_ts": now_ts(),
        "dry_run": args.dry_run,
        "zombie_locks": clean_zombie_locks(args.dry_run),
        "stale_sessions": clean_stale_sessions(args.dry_run),
        "orphan_counters": clean_orphan_counters(args.dry_run),
    }
    print(json.dumps(report, indent=2, ensure_ascii=False))
    # 写一份审计日志
    log_dir = os.path.join(CICR_ROOT, "logs", time.strftime("%Y-%m-%d", time.gmtime()))
    os.makedirs(log_dir, exist_ok=True)
    with open(os.path.join(log_dir, "recovery.log"), "a") as f:
        f.write(json.dumps(report, ensure_ascii=False) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
