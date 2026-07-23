# MeuOS Kit 自动化 CI/CR 循环工作流

零人工干预的迭代开发闭环：任务规划 → 代码编写 → 验收测试 → 代码审查。

高推理模型（大脑）负责规划与审查，轻量模型（Hy3）负责执行——成功路径上大脑零 Token 消耗。严格遵守 `AGENTS.md` 全部规范。

> 实现依据：`SPEC.md`

---

## 目录结构

```text
.ci-cr/
├── config/                 # 配置（模型路由、验收映射、CodeBuddy 基线）
├── scripts/                # 驱动、探针、执行器、审查器、清理器、扫描器
├── state/                  # 运行时状态（可丢失，用于恢复）
│   ├── locks/              # 原子任务锁
│   ├── sessions/           # task_id -> session_id 映射
│   ├── balance_snapshots/  # 余额快照 latest.json
│   ├── retry_counters/    # 热修复重试计数
│   └── abandoned/          # 放弃的任务摘要
├── .plan/                  # 大脑生成的任务卡片 JSON
├── acceptance/             # 标准化验收脚本
├── templates/              # 任务卡片与修正提示词模板
└── logs/YYYY-MM-DD/        # 运行日志
```

## 状态机

```
IDLE ──(扫描 .todo 待办)──▶ PLANNING ──(卡片生成)──▶ EXECUTING
                                                      │
                          ┌───────────────────────────┤
                       验收通过                        验收失败
                          │                           │
                       git commit                 HOTFIXING(≤3次)
                          │                           │
                         IDLE                     放弃/修复
                                                     │
                                                    IDLE

某子项目 .todo 全完成 ──▶ MILESTONE ──▶ REVIEWING ──▶ APPROVED: 合并入 main
                                              │
                                              └─ 驳回(带补丁): 新修正卡片 ▶ PLANNING
```

## 快速开始

### 1. 前置条件

- `codebuddy` CLI 已登录（`codebuddy` 交互式 `/login`）——腾讯云版
- `arkcli` 已初始化 profile（`arkcli profile init`）——字节跳动
- `codex` 已登录（`codex login`）——OpenAI codex CLI
- Python 3.10+ 与 PyYAML
- 宿主 `gcc`/`tcc`（Phase 0 自举）
- 可选：`NVIDIA_API_KEY` 或填写 `config/secret.yaml` 用 NIM free tier
- 可选：`ai-api-proxy.w-run.dev` 反向代理（部署在 JP 机房）

### 2. Provider 矩阵

| Provider | 类型 | Auth | 备注 |
|---------|------|------|------|
| codebuddy | 腾讯云 CLI | `/login` | 现成模型：deepseek-v3-2-volc 等 |
| codex | OpenAI CLI | `codex login` | 默认模型可用；指定 -m 解析失败会自动回退 |
| arkcli | 字节跳动套餐 | `arkcli profile init` | 仅作余额/路由查询 |
| **nvidia_nim** | OpenAI 兼容 REST | `NVIDIA_API_KEY` 或 `secret.yaml` | **本机推荐**：30 RPM free tier |

#### 2.1 ai-api-proxy.w-run.dev 反向代理

部署在 JP 机房，路径前缀路由：

```
https://ai-api-proxy.w-run.dev/<vendor>/v1/...
  /openai/...  -> OpenAI 兼容 REST
  /nvidia/...  -> NVIDIA NIM 兼容 REST
  /claude/...  -> Anthropic Claude
  /gemini/...  -> Google Gemini
```

默认 HTTPS 证书有效。`providers.py` 与 `balance_probe.py` 通过 `CICR_TLS_INSECURE=1`
跳过 TLS 校验（应急逃生口，用于自签/临时证书场景，**不要**在生产启用）：

```bash
export CICR_TLS_INSECURE=1   # 仅在反代证书失效时使用
```

#### 2.2 NVIDIA NIM（推荐免费默认）

实测可用 free tier 模型（按速度排序）：

| 速度 | 模型 ID |
|-----|--------|
| 快 | `mistralai/mixtral-8x7b-instruct-v0.1`、`meta/llama-3.1-8b-instruct` |
| 中 | `meta/llama-3.1-70b-instruct`、`meta/llama-3.3-70b-instruct`、`mistralai/ministral-14b-instruct-2512` |
| 慢（冷启动）| `z-ai/glm-5.2`、`deepseek-ai/deepseek-v4-pro` |

GLM/DS-v4-Pro 在冷启动时容易超时，已自动放至 `fallback_models` 末位。

#### 2.3 API key 配置（按优先级）

```bash
# 方式 1：环境变量（推荐 CI/容器）
export NVIDIA_API_KEY=nvapi-...

# 方式 2：secret.yaml（gitignored）
cp config/secret.yaml.example config/secret.yaml
vim config/secret.yaml
chmod 600 config/secret.yaml
```

### 3. 单次运行（cron 模式）

```bash
# 每轮处理一个任务（适合 cron 每 5 分钟触发）
./.ci-cr/scripts/driver_daemon.sh --once

# 启用反向代理
export CICR_TLS_INSECURE=1   # 仅证书失效时设置
./.ci-cr/scripts/driver_daemon.sh --dry-run   # 只扫描+规划
```

### 4. 配置定时触发

**cron：**
```cron
*/5 * * * * cd /workspace/MeuOS-Kit && ./.ci-cr/scripts/driver_daemon.sh --once >> /tmp/cicr-driver.log 2>&1
```

**systemd timer：**
```ini
# /etc/systemd/system/meuos-cicr.service
[Service]
Type=oneshot
WorkingDirectory=/workspace/MeuOS-Kit
ExecStart=/.ci-cr/scripts/driver_daemon.sh --once

# /etc/systemd/system/meuos-cicr.timer
[Timer]
OnCalendar=*:0/5
Persistent=true
```

## 核心脚本

| 脚本 | 职责 |
|------|------|
| `driver_daemon.sh` | 主入口：自检→规划→执行→验收→里程碑 |
| `balance_probe.py` | 探测各模型余额，决定大脑人选（§4.1 门控） |
| `task_executor.py` | 调用 CodeBuddy 执行单任务卡片，含会话恢复与热修复 |
| `milestone_reviewer.py` | 里程碑 Git Diff 审查（调用大脑） |
| `recovery_cleaner.py` | 清理僵尸锁/超时会话 |
| `todo_scanner.py` | 扫描 `.todo/*.md` 并跟踪任务状态 |

## 验收脚本

任务卡片通过 `task_type` 映射到验收脚本（见 `config/acceptance_manifest.json`）：

1. `common_checks.sh`（全局，必跑）：检测 glibc/GNU/LLVM 依赖、compat 符号泄漏。
2. `libc_arch.sh`：meuos-libc 架构移植（time64 不变量、裸链接回归、自重建闭环）。
3. `compiler_sanity.sh`：mcc 冒烟 + C11 特性门禁。

## 异常自愈

| 场景 | 处理 |
|------|------|
| API 闪断 | 重试 3 次（指数退避），保留 session_id |
| 配额耗尽(429) | 切换备用模型；全部耗尽则放弃本轮规划，已有任务继续 |
| 验收无法修复 | 热修复失败 3 次后放弃，`.todo` 标记 `[!]`，禁止回溯 |
| 驱动被 kill | 重启后 `recovery_cleaner.py` 清理超时锁并重置任务 |

## 手动调试

```bash
# 查看待办任务
python3 .ci-cr/scripts/todo_scanner.py pending

# 仅运行验收（跳过执行）
python3 .ci-cr/scripts/task_executor.py --task-id meuos-libc:32bit-time64 --acceptance-only

# 试跑余额探针
python3 .ci-cr/scripts/balance_probe.py

# 清理僵尸锁（只报告）
python3 .ci-cr/scripts/recovery_cleaner.py --dry-run

# 触发里程碑审查
python3 .ci-cr/scripts/milestone_reviewer.py --component meuos-libc --base main
```

## 设计约束

- 零 GNU/glibc 代码污染（`common_checks.sh` 强制）
- 成功路径大脑 Token 占比 < 5%
- 会话恢复成功率 ≥ 90%
- 构建可重现（无时间戳、无绝对路径硬编码）
