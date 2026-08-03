# worker-deployment.md — 多 Worker 会话恢复档案

> 目的：解决 **agent 任务会话中断导致恢复复杂** 的问题。
> 本文件是所有 worker 状态、分支、在途进度的**唯一权威来源**。
> 任何会话中断后，新会话只需读此文件 + `git fetch origin` 即可完整恢复。
> 更新时机：**每次 worker 状态变化 / 每笔提交 / 每笔 push 后立即更新**（文档纪律，禁止攒到结束）。

---

## 0. 恢复协议（中断后第一步读这里）

若本会话或 worker 会话中断，新会话按以下步骤恢复：

```bash
# 1. 同步远端所有分支（wip 分支是恢复点）
cd /workspace/MeuOS-Kit && git fetch origin

# 2. 检查在途分支（每个 <name> 对应一个 worker）
git branch -r | grep -E "worktree-(wip|tmp)"

# 3. 读本文件 §3 表格，确认每个 worker 的任务与分支

# 4. 需要继续某个 worker 的工作时：
git checkout -b worktree-resume-<name> origin/worktree-<name>
# 然后 spawn 新 worker 从该分支继续（或手动处理）
```

**铁律**：
- 所有未完成的半成品**必须立即提交并 push 到独立 `worktree-<wip>-<name>` 分支**，禁止留在工作树（工作树不跨会话）。
- 已完成的工作 push 到各自 `worktree-<name>` 分支，由 team-lead 合入 `worktree-mxx-work` 主线。
- 每个 worker **只在自己的 worktree/分支内工作**，禁止跨 worker 改共享文件。

---

## 1. 主线与分支约定

| 分支 | 用途 |
|---|---|
| `main` | 最终合流，核心交付完成前**禁止合并**（AGENTS.md 约束） |
| `worktree-mxx-work` | mcc/m++ 开发主线（共享工作树 `.agents/worktrees/mxx-work/`） |
| `worktree-<wip>-<name>` | 各 worker 的在途半成品恢复点（每个都 push 远端） |
| `worktree-tmp-<name>` | 各 worker 独立临时 worktree 分支（完成后合入主线） |

工作树布局：
- 主仓库：`/workspace/MeuOS-Kit`（main）
- mcc/m++ 共享工作树：`.agents/worktrees/mxx-work/`
- worker 临时 worktree：`/tmp/mxx-wt-<name>`（可随 worker 生命周期建删）

---

## 2. 会话中断恢复清单（每会话开始时核对）

- [ ] `git fetch origin` 同步全部远端分支
- [ ] 检查 `git worktree list` 确认各 worktree 存在
- [ ] 读本文件 §3 确认各 worker 在途状态
- [ ] 若任务队列在 `.issues/<date>.md`，读对应日期文件
- [ ] 若需重建团队，`TeamCreate` + 按 §3 重新 spawn 对应 worker

---

## 3. Worker 状态表（当前团队）

团队：`mcc-team-r5`（2026-08-03 网络故障后重建）
命名规范：**常见女性英文名**（不用数字尾缀，避免重名）。

| Worker | 模型 | 分支 | Worktree | 任务 | 状态 | 上次 push |
|---|---|---|---|---|---|---|
| alice | reasoning | worktree-tmp-alice (自 worktree-requires-wip) | /tmp/mxx-wt-alice | 续作 requires 表达式四类需求 | **in_progress** | c9ca880 起点 |
| bella | lite | worktree-tmp-bella (自 worktree-mxx-work) | /tmp/mxx-wt-bella | m++ 测试矩阵扩充 + 门禁验证 | **completed**（1ed8c53 已合入主线 dd78366） | 16 个 test/cpp 文件 |
| chloe | lite | 只读隔离副本 | /tmp/mcciso-chloe | 定位 chibicc B 类真 bug（常量折叠窄化 + va_end 类型检查） | **completed**（报告已收） | 只读不 push |
| diana | lite | worktree-tmp-diana (自 worktree-mxx-work) | /tmp/mxx-wt-diana | C23/C11 边界测试扩充 | **completed**（db1451b 已合入主线 294e5c2） | 11 个 test/c23 文件 + 发现 F1-F3 |
| eve | lite | worktree-tmp-eve (自 worktree-mxx-work) | /tmp/mxx-wt-eve | m++ 负向测试矩阵扩充 | **completed**（02d6684 已合入主线 b4cad7e） | 33 个 test/cpp 文件 |
| grace | lite | worktree-tmp-grace-sema (自 worktree-mxx-work) | /tmp/mxx-wt-grace | sema/decl 组 E1/E4/E5/E6 修复 | **completed**（fd222ad 后转 sema，B 类竞争由 hazel 胜出） | push worktree-tmp-grace-sema |
| hazel | lite | worktree-tmp-hazel (自 worktree-mxx-work) | /tmp/mxx-wt-hazel | chibicc B 类两 bug 实施（**竞争**） | **completed**（a932c60 胜出，已合入主线 69a6f2c） | a932c60 |

### 会话中断恢复速查（当前团队）
1. `git fetch origin`（在 /workspace/MeuOS-Kit）
2. alice 的分支：`worktree-tmp-alice`（含 requires 半成品 c9ca880 续作）；bella：`worktree-tmp-bella`
3. 需要续接时 spawn 同名 worker，prompt 指向对应 worktree 路径即可
4. 半成品保护范例：`worktree-requires-wip` = requires 半成品恢复点（网络故障时保护成功）

---

## 4. 任务队列（当前在途）

### requires 表达式（C++20）
- 分支：`worktree-requires-wip`（commit `c9ca880`，2026-08-03 网络故障保护）
- 内容：cpp_parse.c requires 表达式 trial-parse 半成品（+145/-95）+ 3 个测试文件
- 状态：**半成品，未稳定验证**，需续作 worker 在隔离副本验证后提交
- 任务：修复 requires 表达式四类需求（简单/类型/复合/嵌套），验收 check-cpp-func/neg/lex + check-c-mir

### D1 缺陷：const T& 形参 operator==/operator< 整体失败
- 根因：`cpp_parse.c cpp_try_operator_call`（基线 1274-1339）三处——成员路径缺 const-K/引用-R 编码回退、成员/自由函数路径实参未处理引用形参
- 修复方案：4-way 级联查找 + 引用实参 `&` 绑定；对照 expr_postfix.c:352-354 惯例
- 状态：def4 已定位，**待实施**（等 requires 合入主线后，同文件冲突）

### D4 缺陷：非空类按值返回错乱（实为 ctor 初始化列表标量成员落地缺失）
- 根因：`cpp_parse.c emit_base_ctors_for`（基线 2052-2165），行 2068-2069 只对 struct/union 成员发 ctor 调用，标量成员 init-list 项被 `continue` 丢弃
- 修复方案：对命中初始化项的非 struct/union 成员发射 `*(this+offset)=args[0]`
- 状态：def4 已定位，**待实施**

### D2 缺陷：急切实例化未使用成员函数（非惰性）
- 根因：`cpp_parse.c flush_pending_methods`（1176-1199），行 1197 无条件 `cpp_parse_method_body`
- 修复方案：模板实例化期间延迟模式 + 保留 pending_method 进 per-class 延迟表 + 调用点按需重放（**不能简单 continue 丢弃**，probe 已证明会破坏已使用方法）
- 状态：def4 已定位，**待实施**（工作量中偏大，专项派发）

### #elifdef/#elifndef 修复
- 状态：**已合入主线**（pp.c→6ca4ba1，测试→e472811），已闭环

### chibicc run.sh sysroot 修复
- 状态：**已合入主线**（f05633f），PASS=9/RUNFAIL=6/COMPILEFAIL=26，已闭环

### chibicc B 类真 bug（常量折叠窄化转换 + va_end 类型检查）
- 状态：**已合入主线**（hazel a932c60 胜出，覆盖窄化 IAND 掩码+符号扩展 + TYPEATOMIC 解包 + va_end no-op，已 merge 69a6f2c）

### 文档同步（cpp20/cpp23-gaps.md、c23-review.md）
- 状态：**已合入主线**（edb854b + eb8372d），已闭环

---

### 缺陷队列（待修复）
- **E1** 命名空间作用域变量重定义 `int a; int a;` 漏检（eve 发现，pending/redef_global_var.neg.cc）→ **done**（grace，decl.c 重定义诊断 + 测试转正 test/cpp/redef_global_var.neg.cc）
- **E2** 类内重复成员 `int x; int x;` 漏检（pending/dup_member.neg.cc）
- **E3** 重复枚举符 `enum E{a,a};` 漏检（pending/dup_enum.neg.cc）
- **E4** 非 void 函数缺 return 漏检（pending/missing_return.neg.cc）→ **done**（grace，func_falls_off_end CFG 可达性 + decl.c 检查 + 测试转正 test/cpp/missing_return.neg.cc；**GNU noreturn 误报已修复**——attr.c PREFIXGNU 补 noreturn + qualtype.kind 传播 GNU `__attribute__` 到 decl，test/cpp/noreturn_attr.cc 覆盖四风格）
- **E5** 引用未初始化 `int&r;` 漏检（pending/uninit_ref.neg.cc）→ **done**（grace，decl.c 声明处检查 + 测试转正 test/cpp/uninit_ref.neg.cc）
- **E6** C++ 模式禁用 VLA 未生效（pending/vla.neg.cc）→ **done**（grace，decl.c PROPVM 检查 + 测试转正 test/cpp/vla.neg.cc）
- **D1** const T& operator 整体失败（def4 定位，cpp_try_operator_call）
- **D4** ctor 标量成员 init-list 落地缺失（def4 定位，emit_base_ctors_for）
- **D2** 急切实例化未使用成员（def4 定位，flush_pending_methods）
- **F1** constexpr 函数体纯度不诊断（constexpr 函数调用 printf 不报错，diana 发现，test/c23/neg/constexpr.neg.c）
- **F2** `nullptr_t` 变量进真值条件 ICE（内部错误 unsupported conversion，diana 发现）
- **F3** 具名 constexpr 变量不折叠进整数常量表达式（_Static_assert(K==9) 报非常量，diana 发现）
- **chibicc B 类**：常量折叠窄化 + va_end 类型检查（verify2/gate3/chi4 定位，chloe 精确行号，grace/hazel 竞争中）

## 5. 纪律速查

- 提交：文件级 `git add <文件>`，**禁止 `git add -A`**；建议 `git commit --only <path>` 规避共享 index 竞态
- 分支：核心交付前只推 worktree 分支远程，**禁止合并 main**
- 半成品：立即提交 + push 独立 wip 分支，禁止留工作树
- 模型：reasoning 限 2 个（贵）；lite(hy3) 免费可 10+ 并行；禁止 default
- 夹带：遇夹带**维持现状不 force push**（已发生 3 次：647a05b/93ab4b4/6ca4ba1）
- 门禁：每提交跑对应 `make check` / verify-all.sh，通过才 push
- 竞争实现：双 worker 竞争同一任务（快速拿正确实现）。**竞争落败者的思路若优秀，也要参考融合进最终方案**（team-lead 审阅双方报告后合并优者），不浪费洞察。竞争派发时要求双方各自输出"实现 + 理由"，由 team-lead 统一裁决融合。
