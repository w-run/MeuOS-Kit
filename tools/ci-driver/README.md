# ci_driver — codebuddy headless 多轮 todo 驱动器

> 状态: **Phase 0 (driver + MCP + prompt + 流式回显 + 强制 commit)** — 2026-07-23
>
> 把 `projects/<sub>/.todo/*.md` 里堆积的待办项,逐个拆给 sub-agent 跑,
> 主会话只做规划和验收审计,长 session 自动分轮续接,跨 session 状态由
> Python 驱动持久化。**主会话在验收通过后强制 git commit**,driver 端
> 实时打印 stream-json 事件回显。

## 1. 架构

```
┌──────────────────────────┐    ┌──────────────────────────┐
│ ci_driver.py (Python)    │    │ MCP server (stdio)       │
│  - 选 batch / 维护 state │───▶│  ci_server.py            │
│  - spawn codebuddy       │    │   tool: subagent_run     │
│  - 实时回显 stream-json  │    │   ↳ spawn sub codebuddy  │
│  - 检测 marker / 续接    │    └──────────────────────────┘
│  - 处理超时 / 失败       │                ▲
└──────────────┬───────────┘                │ MCP over stdio
               │                            │
               ▼                            │
       ┌────────────────────────────────────────────────┐
       │ codebuddy main session (model from env/argv)   │
       │  - 读 todo / 拆微任务                          │
       │  - 调 subagent_run ─────────────────────────────┘
       │  - 独立跑验收 (make check / test.sh)
       │  - 改 todo front matter
       │  - **git add + git commit** (强制,一个 todo 一个 commit)
       │  - 输出 [[RESTART]] / [[DONE]] / [[FAIL:...]]
       └────────────────────────────────────────────────┘
                       │  --resume <session_id>
                       ▼  (driver 续接)
              (driver 启动下一轮)
```

每轮:

1. driver 刷新 `state/todo_priority.json` (优先级排序的清单,主会话可 grep)
2. driver 选下 N 个 actionable todo,标记 `in_progress`
3. driver spawn `codebuddy -p <user_prompt> --resume <id> --mcp-config ...`
4. **driver 实时打印 stream-json 事件** (Read/Bash/Grep/Edit/Result 边跑边出)
5. 主会话在轮内处理 batch,逐个派发 sub-agent,跑验收,改 todo,**commit**
6. 主会话在轮末输出 marker,codebuddy 退出
7. driver 解析 marker,持久化 state,启动下一轮

## 2. 文件

```
tools/ci-driver/
├── ci_driver.py         # Python 主驱动 (CLI 入口)
├── ci_server.py         # MCP stdio server,实现 subagent_run
├── ci_lib.py            # 共享:state / todo 解析 / codebuddy 包装
├── mcp_config.json      # codebuddy 用的 MCP 配置
├── prompts/
│   ├── main_planner.md  # 主会话 SystemPrompt (规划+派发+审计)
│   └── sub_executor.md  # sub-agent SystemPrompt (执行+自验收)
├── state/               # 运行时 state (gitignored)
├── logs/                # 每轮主会话 stdout/stderr (gitignored)
├── .gitignore
└── README.md
```

## 3. 安装

依赖:

- Python 3.8+ (仅用 stdlib,无第三方包)
- `codebuddy` (或 `cbc`) 在 PATH 上,版本 ≥ 2.125

直接使用,无需安装:

```sh
cd /workspace/MeuOS-Kit
python3 tools/ci-driver/ci_driver.py --help
```

## 4. 使用

### 4.1 看 todo 列表

```sh
# 人可读
python3 tools/ci-driver/ci_driver.py /workspace/MeuOS-Kit --list-todos

# JSON (主会话查阅用)
python3 -c "import sys;sys.path.insert(0,'tools/ci-driver');import ci_lib;from pathlib import Path;print(ci_lib.dump_todo_priority(Path('/workspace/MeuOS-Kit')))"
# → state/todo_priority.json  (按优先级分组,含 flat_order 列表)
```

> **优先级规则**:`P0` 最高,`P5` 最低。driver 总是先做 P0。`in_progress`
> 视为 actionable (续做),只有 `done` 是终态。

### 4.2 dry-run

```sh
python3 tools/ci-driver/ci_driver.py /workspace/MeuOS-Kit --dry-run
# 打印当前 main session id / actionable todo / 计划派发顺序,不真调 codebuddy
```

### 4.3 启动完整流程

```sh
# 限定到 mcc 和 meuos-libc,每轮 1 个 todo,跑一轮即停
python3 tools/ci-driver/ci_driver.py /workspace/MeuOS-Kit \
    --projects mcc,meuos-libc \
    --batch-size 1 \
    --once
```

去掉 `--once` 即可持续跑(默认 `--max-rounds 999`)。

### 4.4 续接 / 重置

```sh
# 中断后重跑会自动用 state/main_session.json 里的 session_id 续接
python3 tools/ci-driver/ci_driver.py /workspace/MeuOS-Kit --projects mcc

# 强制重置 (丢弃旧 session,从新 session 重新规划)
python3 tools/ci-driver/ci_driver.py /workspace/MeuOS-Kit --projects mcc --reset
```

### 4.5 模型选择 (无硬编码)

driver 不写死任何模型名。解析顺序:

```
--main-model / --sub-model (CLI)
  ↓
$CI_DRIVER_MAIN_MODEL / $CI_DRIVER_SUB_MODEL (env)
  ↓
$CI_DRIVER_MODEL (env, 兜底两边)
  ↓
不传 --model, 让 codebuddy 用 `codebuddy config get model` 的默认
```

```sh
# 临时换主会话模型
python3 tools/ci-driver/ci_driver.py /workspace/MeuOS-Kit \
    --main-model deepseek-v4-flash --sub-model hy3

# 走 env,持久化
export CI_DRIVER_MAIN_MODEL="deepseek-v4-flash"
export CI_DRIVER_SUB_MODEL="hy3"
python3 tools/ci-driver/ci_driver.py /workspace/MeuOS-Kit
```

> **模型名格式**: codebuddy 自带的 (hy3 / dsv4pro / dsv4flash / minimax-m3 ...)
> 用裸名。Remote provider (ark / ark-coding-plan / minimax-plan / nim) 才需要
> `<provider>:<model>` 前缀。driver 不替你做这个区分 — 你给的字符串原样转发。

### 4.6 流式回显

默认开。driver 用 `codebuddy --output-format stream-json --verbose`,每行
一个 JSON 事件,driver 解析后实时打印到 stderr:

```
[init model=deepseek-v4-flash session=7782fe55]
  · Read /workspace/MeuOS-Kit/projects/meuos-libc/.todo/native-linker.md
  · Glob projects/meuos-libc/ARCHITECTURE.md
  · Bash sh test/ld_smoke.sh 2>&1; echo EXIT: $?
  · Edit /workspace/MeuOS-Kit/projects/meuos-libc/Makefile
  > Both files have been created/modified...
[result] Commit landed. All tasks for this batch are complete.
```

关掉:`--no-echo`(只写日志到 `logs/main_round_NNNN.out.log`)。

### 4.7 强制 git commit

主会话 prompt 明确要求:**验收通过 → 改 todo front matter → git add + commit**。
一个 todo 一个 commit,message 形如:

```
<sub>: <name> (P?) - <one-line summary>
```

示例:

```
meuos-libc: native-linker (P0) - add check-native-linker target for mt/ld integration
```

如不想让 driver 自动 commit,可临时在 prompt 里改(改 `prompts/main_planner.md`
的 §2 Step 5.b) — 默认是强制的。

## 5. 主会话与 driver 的协议

主会话**每轮结束必输出**一行 marker,driver 解析:

| Marker               | driver 行为                                          |
| -------------------- | ---------------------------------------------------- |
| `[[RESTART]]`        | 保存 session,启动下一轮 (复用 `--resume <id>`)       |
| `[[DONE]]`           | 退出整个 driver (无 todo 可做)                       |
| `[[FAIL: <reason>]]` | 丢弃当前 session,下一轮开新 session (空 `<id>` 启动) |

可选输出 `[[SESSION_ID:<id>]]` 打印主 session id(用于人工续接)。

> **不输出 marker** 时,如果退出码 0,driver 视作隐式 RESTART;
> 如果超时被 kill,driver 视作不可恢复,丢弃 session。

## 6. todo front matter 约定

每个 todo `.md` 文件的顶部 `<!-- ... -->` 块携带元数据,driver 据此过滤和排序:

```yaml
<!--
priority: P0           # P0=最高,P5=最低,空=不参与自动派发
status: pending        # pending | in_progress | done
note: <可选,简述>
done_ts: 2026-07-23    # 完成后由主会话写
-->
```

主会话在验收通过后改 `status: done` 并写 `done_ts: <today>`。

## 7. 安全 / 沙箱

driver 启动 `codebuddy` 时自动加 `-y` (skip permissions) 和
`--add-dir <project_root>`,让 sub-agent 可读写项目但不能越界到外部。
MCP server 自身只暴露 `subagent_run` 一个工具,且只接受显式参数。

如需更严的沙箱,可在 `mcp_config.json` 给 server 加 wrapper 脚本,
用 `bwrap` / `firejail` 包一层。

## 8. 已知限制

- **JSON-RPC over stdio**: `ci_server.py` 是手写 minimal MCP,只实现
  `initialize` / `tools/list` / `tools/call` / `ping`。codebuddy 当前
  不会调其他方法,够用。
- **session 续接**: codebuddy 的 `--resume` 在上下文耗尽 / 进程被 OOM
  kill 后**会丢**;driver 会在 `[[FAIL]]`/超时时主动重置 session,
  必要时人工 `codebuddy --resume <id>` 排查。
- **state 不并发**: driver 是单进程串行,不要在 driver 运行时手动
  改 `state/main_session.json`。
- **sub-agent timeout**: 默认 30 分钟。微任务超过这个时间必须拆。

## 9. 调试

```sh
# 单独跑一次主会话,不看 todo (人工 debug)
codebuddy -p "explain this" \
  --model custom-local:ark/deepseek-v4-flash \
  --append-system-prompt "$(cat tools/ci-driver/prompts/main_planner.md)" \
  -y \
  --mcp-config tools/ci-driver/mcp_config.json \
  --strict-mcp-config \
  --add-dir /workspace/MeuOS-Kit

# 单独测试 MCP server
echo '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | \
  python3 tools/ci-driver/ci_server.py | jq .

# 看主会话的 stdout
tail -f tools/ci-driver/logs/main_round_0001.log
```

## 10. 路线图

- [x] **Phase 0** — driver + MCP + prompt + todo front-matter 解析
- [ ] **Phase 1** — 端到端跑通 1 个真 todo,补自动化测试
- [ ] **Phase 2** — multi-round 长 session 稳定性 (token 溢出、--resume 失效的兜底)
- [ ] **Phase 3** — `subagent_run` 增强:支持 sub-agent 内部 `--resume` 续接 (长 task)
- [ ] **Phase 4** — 多 driver 并行(每个 subproject 一个 driver,互不抢 session)
- [ ] **Phase 5** — GitHub Action / GitLab CI 集成,自动 PR review 派发
