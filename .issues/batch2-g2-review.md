# if consteval（P1938）实施预审与已实现审查（batch2-g2-review.md）

> 角色：worker-judge（reasoning 裁判）。批次 2，2026-08-03。
> 依据：`a29ad3f`（batch2-cpp23-plan.md 三缺口方案）§2。G2 方案在审稿期间已由实施 worker 落地提交（见 §0），本文档对**已实现代码**做独立审查 + 给出剩余边界与风险，供 worker-doc 收口与 G2 后续迭代参考。

---

## 0. 状态快照（审稿期间发生，超出原「预审」预期）

| 提交 | 内容 | 状态 |
|:-----|:-----|:-----|
| `dca1620` | G1 constexpr 语句解释器（多语句体/局部变量/if/循环/break/continue/多 return，步数上限 10 万） | ✅ 已合入 |
| `55499d6` | G2 if consteval（普通函数：stmt.c 识别 + cpp_if_consteval + G1 联动） | ✅ 已合入 |
| `b4ecfdc` | 引用返回成员方法修复（`int &at()`/`int &r;`，cfeadf9 回归） | ✅ 已合入 |

原预审任务（方案设计）已被实现取代；本报告转为**实现审查 + 残余风险**。以下对 a29ad3f §2 方案的技术正确性先给结论，再对落地实现逐项核验。

---

## 1. 方案评审（a29ad3f §2 技术合理性）

**结论：方案技术正确，关键机制判断全部成立，推荐顺序（G2 期1→G1→G2 期2）合理。**

1. **g_cpp_cexpr_depth 作为常量/运行期上下文判别量**——成立。
   - cpp_parse.c:5501 `static int g_cpp_cexpr_depth`（求值递归深度，上限 64，6358/6375/6391 增自减）。常量求值回放期间 >0，普通运行期解析 ==0。
   - 与标准 P1938「语句处于常量求值上下文」语义精确对应。方案建议复用此量而非新增判别，**实现也确按此做了**（cpp_if_consteval:6419 `bool constant_ctx = g_cpp_cexpr_depth > 0`；G1 解释器 6173 同）。
2. **cpp_skip_branch 复用**——成立。token 级跳过 + 括号配平，被弃分支不做语法良构检查（方案 §2.2 记为已知降级），与 if constexpr 一贯宽松哲学一致。
3. **`if consteval` 无条件表达式**——方案 §2.3 步骤 2 写 `expect(TLPAREN)、expr(s)`，但**标准 P1938 语法是 `if consteval { }`（无圆括号、无条件）**，方案该处描述有偏差（实际实现正确跳过了括号，见 §2）。此为方案文档小瑕疵，实现未受影响。
4. **`if ! consteval` 的 `!` 前瞻**——方案 §2.3 与风险 §2.5 都要求先探测 TLNOT 再探 consteval，避免与按位非混淆。实现 stmt.c:547-562 正确处理（非 consteval 时 tokpush 回退让普通路径报错）。
5. **G2 期2 依赖 G1**——成立。constexpr 函数体内 if consteval 需语句解释器支持；G1 合入后 6151-6210 的 TIF 分支已内置 consteval 处理。

---

## 2. 已实现代码审查（worker-judge 独立实测）

### 2.1 stmt.c TIF 分支（识别层）
- `constexpr`（TCONSTEXPR，C23 关键字）与 `consteval`（C 词法标识符，按名匹配 `tokenstr=="consteval"`）**token 类型不同，按 kind 分路**——方案 §2.5 风险「别混」已正确处理。
- `if ! consteval`：TLNOT 前瞻；非 consteval 时 `tokpush(&tok,1); tok=nottok;` 回退——注意此处 tokpush 的用法与 b4ecfdc 修复引用成员是同一模式（正确恢复 token 流）。

### 2.2 cpp_if_consteval（运行时解析层）
```c
bool constant_ctx = g_cpp_cexpr_depth > 0;
bool take_then = constant_ctx != negate;
if (take_then) { stmt(f, s); if (tok.kind==TELSE) { next(); cpp_skip_branch(); } }
else { cpp_skip_branch(); if (tok.kind==TELSE) { next(); stmt(f, s); } }
```
- 判别式 `constant_ctx != negate`：consteval 且非 negate → 常量上下文取 then；运行期 → else；`!consteval` 取反。**逻辑正确**。
- 运行时（depth==0）：`if consteval` 恒取 else，`if !consteval` 恒取 then。标准行为正确。

### 2.3 G1 解释器内 consteval（6173-6194）
- 常量求值回放时恒取 consteval 分支（`consteval_if` 直接不 eval 条件）。`if !consteval` 取反。与 cpp_if_consteval 判别一致，天然成立。

### 2.4 实测矩阵（worker-judge canary，全部 rc=0）

| 用例 | 期望 | 结果 |
|:-----|:-----|:-----|
| `constexpr int csum(int n){ if consteval{return n*2;} else {return n+100;} }` 常量实参 csum(10) | 20（consteval 分支） | ✅ |
| 同一函数运行期实参 csum(v) | 105（else 分支） | ✅ |
| 普通函数 `int ord(int n){ if consteval{return 1;} else {return 2;} }` | 恒 2 | ✅ |
| `if !consteval` 取反 | consteval 调用取 else、运行期取 then | ✅ |
| 无 else 的 `if consteval {…}` | 常量取 then、运行期穿透 | ✅ |
| 无花括号单语句分支 | 正确 | ✅ |
| 嵌套 `if constexpr` 内 `if consteval` | 常量 10 / 运行期 20 | ✅ |
| **G1+G2 组合**：constexpr 循环体内 `if consteval` | 常量 12 / 运行期 6 | ✅ |
| while 循环常量求值 sum(5) | 10 | ✅ |
| `if !consteval` 于 constexpr 函数 | 常量 30 / 运行期 104 | ✅ |
| 模板 constexpr 函数 + if consteval | 常量 1 / 运行期 2 | ✅ |
| static_assert 经 consteval 分支 | 通过 | ✅ |
| `if constexpr (sizeof(int)==4)` + 内嵌 if consteval | 正确分派 | ✅ |

官方测试 `test/cpp/if_consteval.cc`（12 断言 + static_assert）与 `test/cpp/constexpr_body.cc`（15 断言）均 PASS。

---

## 3. 残留风险与边界（供后续迭代）

1. **被弃分支不检查良构性**（方案 §2.2 已知降级）：`if consteval { 语法错误代码 } else { … }` 若运行期解析，跳过 then 分支不报错——与标准「需诊断」有差距。**有意为之**（与 if constexpr 一致），记录即可。
2. **`if !consteval` 的 `!` 与表达式冲突**：`if !x`（非 consteval）回退后由普通路径报「expected '(' after 'if'」——报错信息误导（实际语法错误是 `if !expr` 非法），可接受。
3. **g_cpp_cexpr_depth 递归上限 64**：深递归 constexpr 求值仍受限；若 consteval 分支含递归调用，64 层内折叠、超限降级运行期——降级后语义仍正确（宽松哲学），但需知悉。
4. **步数上限 100000（CEXP_MAX_STEPS）**：compile-time hang 防护到位；超限降级为运行期调用，无编译期挂死。
5. **G1 解释器覆盖面**：`break`/`continue`/多 return/局部变量/循环已支持；`goto`/标签/局部类/static 局部变量在常量求值路径**不解释**（降级运行期）——与 P2242 标准承诺有差距，记为已知降级（方案 §1.3 同样标注）。
6. **`consteval` 函数**：specs.c:75-81 已把 `consteval` 映射为 QUALCONST|QUALCONSTEXPR，复用 constexpr 机制；若 consteval 函数体含 if consteval，走同一解释器路径——需确认无特殊交互（当前测试未覆盖 consteval 函数内 if consteval，建议补）。

---

## 4. 测试补充建议

- [ ] `consteval` 声明函数体内 `if consteval` 交互（§3.6）
- [ ] 被弃分支含未定义标识符/语法错误（确认跳过不报错，负向记录）
- [ ] 深度递归 constexpr（逼近 64 上限）行为
- [ ] if consteval 与 if constexpr 混合嵌套 >=3 层
- [ ] `if consteval` 于模板实例化分支中的表现（方案声称不实例化）

---

## 5. 结论

G2（if consteval P1938）**方案正确、实现完整、实测通过**。方案文档 §2.3「expect(TLPAREN)」表述与标准不符（实际无括号），实现未受此影响。与 G1 的判别量复用（g_cpp_cexpr_depth）是本次实现的点睛——单判别量贯穿运行时解析与常量求值两条路径，维护成本低。无阻塞问题，可进入收口。

（worker-judge 预审/审查产出；不修改 src/，仅文档。）
