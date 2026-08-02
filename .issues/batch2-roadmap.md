# 第二批路线图状态表（batch2-roadmap.md）

> 角色：worker-judge（reasoning 裁判）。批次 2，2026-08-03。
> 分支：worktree-mxx-work。基线：c54b363（第二批缺陷全闭环）。
> 依据：a29ad3f（batch2-cpp23-plan.md）、docs/cpp23-gaps.md、0802.md。
> 用途：第二批（C++23 缺口 + 缺陷收尾）状态总表与后续优先级。

---

## 0. 总览

第二批分两条线：**C++23 缺口攻坚**（G1~G3）与**缺陷收尾**（c-00/c-01/cpp-0f 等）。截至本文档，缺陷线已全闭环；C++23 线 G1/G2 已合入，G3 待实施。

---

## 1. 缺陷收尾（c54b363 已全闭环）

| 缺陷 | 提交 | 内容 | 状态 |
|:-----|:-----|:-----|:-----|
| c-00（u8 字面量元素类型） | `604be9e` | expr_literal.c `case '8'` → typechar | ✅ closed |
| c-01（inline+extern 外部定义） | `e9fae35` | 延迟发射 inline 定义（inlinedefers 链） | ✅ closed |
| cpp-0f（delete cast 操作数） | `9e43494` | unaryexpr→castexpr（delete/delete[]） | ✅ closed |

抽查（worker-judge）：三提交 diff 干净、回归全 PASS（u8_string/inline/new_delete_cast）。提交纪律：单文件域、文件级 add、无夹带。

---

## 2. C++23 缺口（G1~G3）

| 缺口 | 提交 | 内容 | 状态 | 备注 |
|:-----|:-----|:-----|:-----|:-----|
| G1 constexpr 语句解释器 | `dca1620` | cpp_cexpr_stmt 一族：局部变量/if/while/do/for/多 return/break/continue，步数上限 10 万；不可折叠降级运行期 | ✅ closed | constexpr_body.cc 15 断言 |
| G2 if consteval（P1938） | `55499d6` | stmt.c 识别 + cpp_if_consteval + G1 联动（g_cpp_cexpr_depth 判别）；if !consteval | ✅ closed | if_consteval.cc 12 断言；审查见 batch2-g2-review.md |
| G3 deducing this（P0847） | — | 基础引用形态（X&/const X&/X&&），按值延后 | 🔄 **待实施** | 方案 a29ad3f §3；风险：类按值传参基线（见 cpp23-gaps.md） |
| （前置）operator[] P2128 | `cfeadf9` | 多维 operator[] + 引用返回写穿 | ✅ closed | multidim_index.cc 10 断言；**附带回回归**（见 §3） |

### G1/G2 验收证据（worker-judge 实测）
- `make check-cpp-func`（含 constexpr_body/if_consteval/multidim_index/ref_member_return）**全绿**。
- `make check-cpp-neg` 通过。
- G1+G2 组合边界 canary（循环内 if consteval、while 常量求值、if !consteval 于 constexpr 函数、嵌套 if constexpr）全部 rc=0。

---

## 3. cfeadf9 引入的回归（已闭环）

`cfeadf9`（operator[] P2128）合入时引入了**引用成员解析回归**：普通引用声明（`int &r;` 引用数据成员、`int &at()` 引用返回方法）因 struct_decl.c 折叠块 `tok = save` 只恢复 token 副本、不恢复 lexer 流位置而解析失败（父提交可解析）。

- **修复**：`b4ecfdc`（tokpush 重排队被消费 token + 恢复 `&`）。
- **测试**：test/cpp/ref_member_return.cc（写穿/读/&&/const 方法/与 operator[] 共存，5 断言）。
- 教训：`next()` 后 `tok = save` 不能回退流；恢复 token 必须 `tokpush`（b4ecfdc 与 if !consteval 回退同模式）。

### ⚠️ 未闭环缺陷（worker-judge 审核发现）
**operator[] 的 const 重载决议错误**：类同时定义 `int &operator[](int)`（非 const）与 `int operator[](int) const` 时，**const 对象调用错误绑定非 const 版本**。

- 根因：`cpp_subscript_call`（cpp_parse.c:1322-1330）先查非 const mangle 名，**命中即用**，不检查对象 const 性；K 后缀只在非 const 名不存在时回退。
- 对照：普通成员方法调用用 `g_cpp_method.this_decl->type->qual & QUALCONST` 决定加 K（cpp_parse.c:356-384），决议正确。
- 复现：`const D cd={1,2,3}; cd[0]` 在 const 版本返回 `d[0]*10` 时应得 10，实测得 1（选中非 const 版本）。
- 影响面：仅 operator[]（multidim_index.cc 无 const+非 const 同签名重载，故测试未捕获）；范围已界定，普通方法不受影响。
- 建议：登记缺陷，修复方向为 cpp_subscript_call 按对象 qual 决定先查哪个 mangle 名（仿 382-384）。

---

## 4. 后续优先级建议

| 优先级 | 项 | 理由 |
|:-----|:-----|:-----|
| P0 | 修复 operator[] const 重载决议（§3） | 语义错误，影响 const 对象的多维下标 |
| P1 | G3 deducing this 引用形态 | 三缺口最后一块；方案已齐，按值形态延后 |
| P2 | verify-all.sh 纳入 `check-c-mir` | 门禁缺口（第一批遗留）；legacy 双路径显式化 |
| P2 | consteval 函数体内 if consteval 测试补充 | g2-review §3.6 |
| P3 | P2242/P2647 求值面扩展（goto/static 局部/非字面量） | 已降级运行期，记录即可 |
| P3 | G3 deducing 形态（`this auto&`） | 依赖模板推导，单独迭代 |

---

## 5. 风险登记

- **cpp_parse.c 共享热点**：G1（5483 求值区）/G2（5579 cpp_if_constexpr 旁）/G3（1429 cpp_define_method）同文件不同段，并行需文件级 add + commit 前 status 自查（团队 0802 纪律）。
- **G1 解释器降级面**：goto/标签/局部类/static 局部在常量求值路径不解释（降级运行期），与 P2242 标准承诺有差距（已记录）。
- **引用语义基线**：G3 按值对象参数依赖类按值传参正确性；cpp23-gaps.md 记录的类按值返回基线缺陷若影响传参，需先修基线。

---

## 6. 提交清单（本批次含审稿期间的推进）

```
a29ad3f  docs: batch2-cpp23-plan（三缺口方案）
cfeadf9  m++: operator[] P2128（+引用返回写穿）
dca1620  m++: G1 constexpr 语句解释器
55499d6  m++: G2 if consteval
b4ecfdc  m++: 引用返回成员方法修复（cfeadf9 回归）
```

（worker-judge 维护；下一版本更新：operator[] const 决议修复、G3 合入后。）
