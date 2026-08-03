# 知识库管理参考（.agents/reference/knowledge-mgmt.md）

> 从 AGENTS.md §9 下放（2026-08-04）。IMA 知识库 + 本地知识沉淀 + Agent 启动读取流程。

## 9. 知识库管理（IMA 集成）

> 本项目集成了 IMA OpenAPI 技能（`ima-skill`），作为 MeuOS Kit 文档的统一外部知识库。
> 知识库中存储设计文档、会议记录、架构决策、移植笔记等不适合纳入代码仓库的内容。

### 9.0 本地知识沉淀（`.agents/knowledge/`）

> **项目经验库**：`.agents/knowledge/` 随 git 版本管理，沉淀自 AI 会话的工作纪律与项目技术经验
> （git 并发纪律、缺陷闭环、修复方案、架构决策）。新 agent 启动时**应读取**该目录了解既有经验，避免重复踩坑。

- **feedback_\*.md** — 工作纪律与操作教训（git 安全、并发、构建、通知）
- **project_\*.md** — 项目技术经验（缺陷闭环、修复方案）
- **README.md** — 分类索引（按主题列出全部文件与要点）

**使用约定**：
1. 新 agent 启动读 `README.md` 索引，按需打开对应文件。
2. 会话中产生新的可复用经验（踩坑教训、缺陷根因、修复方案），完成后沉淀为 `.agents/knowledge/` 下 markdown 并更新 README 索引。
3. 本地 AI 记忆（`~/.codebuddy/projects/*/memory/`）与 `.agents/knowledge/` 分工：前者是会话级自动记忆，后者是项目级持久知识（进 git）。

### 9.1 分工边界

| 内容类型                              | 存放位置                 | 说明                                                                 |
| ------------------------------------- | ------------------------ | -------------------------------------------------------------------- |
| 功能需求、功能规格说明                 | IMA 知识库                | 功能需求的完整描述，包括使用场景、行为约束、验收条件                   |
| 设计方案、架构决策、技术选型           | IMA 知识库                | 设计讨论记录、备选方案对比、最终决策及其理由                           |
| 项目规划、路线图、阶段计划             | IMA 知识库                | Phase 级/里程碑级的规划文档，进度追踪和调整记录                        |
| 任务计划、实施步骤、分工安排           | IMA 知识库                | 具体功能的实现计划，包括任务拆解、依赖关系、验收标准                   |
| 会议记录、讨论结论                     | IMA 知识库                | 同步/异步讨论的完整记录和结论                                          |
| 组件规格、API 接口定义                 | 代码仓库 `ARCHITECTURE.md` | 随代码版本控制，代码变更时同步更新；IMA 中的设计决策落地后应更新此处    |
| 项目规约、任务编排策略、禁止事项       | `AGENTS.md`（本文件）     | Agent 初始化的核心参考                                                |
| 移植笔记、架构差异对照表               | IMA 知识库                | 多架构移植的实测经验记录                                               |
| 社区参考资源笔记                       | IMA 知识库                | 对 `reference/` 目录中社区源码的阅读笔记和分析                         |
| 构建/调试踩坑记录                      | IMA 知识库                | 现场调试的完整过程和解决方案                                           |
| 工具链就绪情况、编译器缺口的追踪       | IMA 知识库                | 跨架构测试的现场状态记录                                               |

**核心判据**：凡是**未来才实现**的需求/设计/规划 → IMA 知识库；凡是**已经实现**的规格和约束 → 代码仓库。IMA 中的设计决策落地后，应将最终确定的规格同步到 `ARCHITECTURE.md` 并标记知识库对应文档为「已实现」。

### 9.2 在 MeuOS Kit 中使用 ima-skill

`ima-skill` 提供两类操作：**笔记管理（notes）** 和 **知识库操作（knowledge-base）**。

**常用场景与对应操作：**

| 场景                               | skill 模块         | 关键步骤                                                                 |
| ---------------------------------- | ------------------ | ------------------------------------------------------------------------ |
| 搜索知识库中 MeuOS 相关文档           | knowledge-base     | `search_knowledge` → 指定 `query` 关键词                                 |
| 查看某篇知识的原始内容               | knowledge-base     | `get_media_info` → 获取 `media_id` → 下载原文                             |
| 浏览知识库内容列表                   | knowledge-base     | 先 `search_knowledge_base` 获取知识库 ID → 再 `get_knowledge_list`       |
| 新建一篇设计笔记                     | notes              | `import_doc` → 指定 `content`（Markdown 格式）和 `title`                  |
| 追加调试记录到已有笔记               | notes              | `search_note` 找到笔记 → `append_doc`                                     |
| 上传架构差异对照表文件到知识库       | knowledge-base     | `preflight-check` → `create_media` → COS Upload → `add_knowledge`        |

### 9.3 配置要求

使用 `ima-skill` 需要配置 IMA OpenAPI 凭证：

```bash
# 方式 A：配置文件（推荐）
mkdir -p ~/.config/ima
echo "your_client_id" > ~/.config/ima/client_id
echo "your_api_key" > ~/.config/ima/api_key

# 方式 B：环境变量
export IMA_OPENAPI_CLIENTID="your_client_id"
export IMA_OPENAPI_APIKEY="your_api_key"
```

凭证优先级：环境变量 → 配置文件。缺少凭证时 API 调用以 code `-100` 退出。

### 9.4 Agent 启动时主动读取规划文档

**这是项目的第一规约：任何 agent 会话启动后，必须主动读取 IMA 知识库中的规划文档。**

规划文档是"接下来做什么"的权威来源，优先级高于代码仓库中的任何待办文件（`.todo/`）。
规划设计可能先于代码存在，只有主动读取才能理解当前阶段的目标。

#### 执行步骤（在 §0 会话恢复流程的第 1 步执行）

```sh
# 1. 找到 MeuOS 知识库
#    使用 ima-skill 的 knowledge-base 模块：
#    search_knowledge_base(query: "MeuOS")

# 2. 浏览知识库内容，查找规划类文档
#    get_knowledge_list(knowledge_base_id="<上一步返回的 kb_id>")
#    重点查找标题包含以下关键词的文档：
#    - "规划" / "计划" / "路线图" / "路线"
#    - "需求" / "功能规格" / "设计"
#    - "阶段" / "Phase" / "P0" / "P1" / ...
#    - "TODO" / "待办" / "任务"
#    - "v4.0" / "v4"（最新的版本号）

# 3. 阅读每个规划文档的原始内容
#    get_media_info(media_id="<文档的 media_id>")
#    下载并阅读全文，理解：
#    - 当前阶段的目标是什么
#    - 有哪些待实现的功能/架构
#    - 设计方案和验收条件
#    - 与其他组件的依赖关系

# 4. 将规划内容与 AGENTS.md §10（项目状态速查）交叉对比
#    - 规划中提到的待办项是否已在仓库 .todo/ 中记录
#    - 规划中的设计决策是否需要更新 ARCHITECTURE.md
```

#### 查询模板（可直接执行）

```bash
source ~/.bashrc
cd /workspace/MeuOS-Kit/.codebuddy/skills/ima-skill
SKILL_DIR="$(pwd)"
OPTS=$(printf '{"clientId":"%s","apiKey":"%s"}' "$IMA_OPENAPI_CLIENTID" "$IMA_OPENAPI_APIKEY")

# 搜索 MeuOS 知识库
resp=$(node "$SKILL_DIR/ima_api.cjs" "openapi/wiki/v1/search_knowledge_base" \
  '{"query":"MeuOS","cursor":"","limit":5}' "$OPTS" 2>/dev/null)
KB_ID=$(echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['data']['info_list'][0]['kb_id'])" 2>/dev/null)

# 浏览知识库内容
node "$SKILL_DIR/ima_api.cjs" "openapi/wiki/v1/get_knowledge_list" \
  "{\"knowledge_base_id\":\"$KB_ID\",\"cursor\":\"\",\"limit\":50}" "$OPTS" 2>/dev/null | \
  python3 -c "
import sys, json
data = json.load(sys.stdin)['data']['info_list']
for item in data:
    title = item['title']
    tags = ' '.join(['📋' if kw in title else '' for kw in ['规划','计划','路线','需求','设计','TODO','v4']])
    print(f\"  {tags} {title}\")
"
```

#### 阅读后的行动

读取规划文档后，agent 应：

1. **更新对当前阶段的理解**：规划文档中描述的目标是什么，当前进展到哪里
2. **确认 `.todo/` 的同步状态**：规划中提到的待办是否已在 `.todo/<project>/` 中有对应条目
3. **确定本次会话的工作范围**：从规划中选取一个具体的、可独立完成的任务
4. **如有模糊之处**：在 IMA 知识库搜索相关设计笔记补充上下文，或向用户确认

### 9.5 文档贡献指南

向知识库贡献 MeuOS 文档时遵循以下原则：

1. **标题格式**：`MeuOS/<主题>` — 例如 `MeuOS/mcc-i386-缺口分析`
2. **内容格式**：Markdown，保持简洁的技术笔记风格
3. **分类**：按阶段/组件组织，便于搜索
4. **关联代码**：提及代码文件时注明相对路径（如 `projects/mcc/src/driver/main.c`）
5. **定期清理**：过时文档标记为「已归档」或在笔记标题中添加 `[存档]` 前缀
6. **与仓库同步**：当某个设计决策最终被编码实现后，在知识库中标记对应记录为「已实现」

### 9.6 Codebuddy 技能清单

本项目配置了以下 Codebuddy 技能（统一存放于 `.agents/skills/`，`.codebuddy` 与 `.trae` 均为指向 `.agents` 的软链接别名）：

| 技能 | 用途 |
|------|------|
| `cross-test` | 跨架构测试编排 |
| `ima-skill` | IMA 知识库/笔记管理（§9.1-9.5） |
| `mkit-bootstrap` | Phase 0-5 自举流程编排（调用 bootstrap.sh） |
| `mkit-c11-check` | mcc C11 符合性检查（`_Atomic`/`_Generic`/`_Thread_local` 等） |
| `mkit-doc-sync` | 代码变更后文档同步收尾，确保文档不落后于代码 |
| `mkit-syscall-gen` | 生成单文件 syscall 封装（meuos-libc syscall 目录） |

技能通过 `Skill` 工具或对应的 slash command 调用。

---

