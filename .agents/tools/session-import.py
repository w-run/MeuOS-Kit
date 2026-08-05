#!/usr/bin/env python3
"""从我 ComuOS-Kit 会话 jsonl 提取上下文，生成可用于 Agent 启动的 teammate prompt。

用法:
  session-import.py list                       # 列出所有会话 (id, 标题, 时间, 消息数)
  session-import.py show <session-id>          # 打印该会话的上下文摘要并输出 teammate prompt

从会话 jsonl 提取: 标题(ai-title)、参与模型、用户/助手对话流、关键工具调用，
整理成一份可粘贴到 Agent(teammate) 的 prompt，实现『把已存在的会话当作团队成员导入』。

jsonl 查找位置 /root/.codebuddy/projects/workspace-MeuOS-Kit/ (memory 上级目录)。
"""
import json
import os
import sys
from datetime import datetime

BASE = "/root/.codebuddy/projects/workspace-MeuOS-Kit"


def _sessions_dir():
    """会话 jsonl 存放目录。优先 BASE，否则用户指定。"""
    return os.environ.get("MEUOS_SESSIONS_DIR", BASE)


def _load(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return rows


def _ts(ts):
    if not ts:
        return ""
    try:
        return datetime.fromtimestamp(ts / 1000).strftime("%Y-%m-%d %H:%M")
    except (ValueError, OSError):
        return ""


def _text_of(msg):
    """从 message.content 里取纯文本。"""
    out = []
    for c in msg.get("content", []):
        t = c.get("text") if isinstance(c, dict) else None
        if t:
            out.append(t.strip())
    return "\n".join(x for x in out if x)


OPENERS = ("<system-reminder", "<command-caveat", "You are CodeBuddy", "Can you directly")
def _is_noise(text):
    if not text:
        return True
    return text.startswith(OPENERS) or text.startswith("<") and len(text) < 2000


def _func_calls(rows):
    """提取 function_call 摘要，帮助理解会话做了些什么。"""
    calls = []
    for r in rows:
        if r.get("type") != "function_call":
            continue
        name = r.get("name", "")
        inp = r.get("input") or r.get("arguments") or r.get("params")
        arg = ""
        if isinstance(inp, dict):
            arg = ",".join(str(k) for k in list(inp)[:4])
        calls.append(f"{name}({arg})")
    return calls


def list_sessions():
    rows_out = []
    for fn in sorted(os.listdir(_sessions_dir())):
        if not fn.endswith(".jsonl"):
            continue
        path = os.path.join(_sessions_dir(), fn)
        rows = _load(path)
        sid = fn[:-6]
        title = "?"
        msgs = 0
        models = set()
        last_ts = None
        for r in rows:
            if r.get("type") == "ai-title":
                title = r.get("aiTitle") or title
            if r.get("type") == "message":
                msgs += 1
                if r.get("role") == "assistant":
                    m = (r.get("providerData") or {}).get("requestModelName")
                    if m:
                        models.add(m)
            if r.get("timestamp"):
                ts = int(r["timestamp"])
                last_ts = ts if last_ts is None else max(last_ts, ts)
        rows_out.append((sid, title, _ts(last_ts), msgs, ",".join(sorted(models))))
    rows_out.sort(key=lambda x: x[2], reverse=True)
    print(f"{'会话ID':<38}{'时间':<18}{'消息':<5}{'模型':<32}标题")
    print("-" * 120)
    for sid, title, ts, msgs, models in rows_out:
        print(f"{sid:<38}{ts:<18}{msgs:<5}{models:<32}{title}")


def show_session(sid):
    path = os.path.join(_sessions_dir(), sid + ".jsonl")
    if not os.path.exists(path):
        sys.exit(f"找不到会话 {sid}: {path}")
    rows = _load(path)

    title, models = "?", set()
    transcript = []          # [(role, text)]
    for r in rows:
        if r.get("type") == "ai-title":
            title = r.get("aiTitle") or title
        elif r.get("type") == "function_call":
            pass  # 不混入正文，稍后单独统计
        elif r.get("type") == "message":
            role = r.get("role", "?")
            if role not in ("user", "assistant"):
                continue
            text = _text_of(r)
            if _is_noise(text):
                continue
            if role == "assistant":
                model = (r.get("providerData") or {}).get("requestModelName")
                if model:
                    models.add(model)
            transcript.append((role, text))

    # 工具调用统计
    calls = _func_calls(rows)
    from collections import Counter
    call_counter = Counter(c for c in calls)
    top_calls = call_counter.most_common(15)

    # 裁剪过长的正文（压缩成摘要提示）
    max_chars = 6000
    compact = []
    used = 0
    for role, text in transcript:
        piece = text if len(text) <= 400 else text[:400] + " …(截断)"
        used += len(piece)
        if used > max_chars:
            compact.append((role, "(…更多内容已省略)"))
            break
        compact.append((role, piece))

    # 输出 prompt
    print(f"# 会话导入提示（从 {sid} 恢复为 teammate）\n")
    print(f"**原始标题**: {title}  \n**参与模型**: {', '.join(sorted(models)) or '未知'}  \n"
          f"**时间窗**: {_ts(rows[0].get('timestamp')) if rows else '?'} ~ {_ts(rows[-1].get('timestamp')) if rows else '?'}  \n"
          f"**文本消息**: {len(compact)} 条（含截断提示）\n")
    if top_calls:
        print("**主要工具调用**: " + ", ".join(f"{n}×{c}" for c, n in top_calls) + "\n")
    print("--- 对话脉络 ---")
    for role, text in compact:
        tag = "用户" if role == "user" else "助手"
        print(f"\n[{tag}]\n{text}")


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help", "help"):
        print(__doc__)
        return
    if args[0] == "list":
        list_sessions()
    elif args[0] == "show":
        if len(args) < 2:
            sys.exit("用法: session-import.py show <session-id>")
        show_session(args[1])
    else:
        sys.exit(f"未知命令: {args[0]} (用 help 查看用法)")


if __name__ == "__main__":
    main()
