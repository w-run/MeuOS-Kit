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
| alice | reasoning | worktree-tmp-alice-cpp + worktree-tmp-alice-cpp20 + worktree-tmp-alice-cli + worktree-tmp-alice-mtld (自 worktree-mxx-work) | /tmp/mxx-wt-alice | cpp_parse D1/D4/D2/E2/E3 + C++20 NTTP/consteval/<=>/聚合初始化/constexpr 成员 + CLI 参数 + **mt/as imm64 截断修复（check-mt-integration 闭环）** | **completed**（D1/D4 等已合入；cpp20 68d1222；cli ac57402；mtld 本次归并合入主线 **ad52f9b**，verify-all 恢复 19/19） | push worktree-tmp-alice-cpp20 |
| bella | lite | worktree-tmp-bella-la64 (自 worktree-mxx-work@fc1f279) | /tmp/mxx-wt-bella | x86_64 MIR-native（fallback/≤16B/cmov）+ Phase 3a riscv64 + Phase 3b loongarch64 MIR-native 试点（#117） | **completed**（#94/#97/#111 已合入；#117：e73469b target 注册 + cca9c13 loongarch64 标量后端 + e888880 gate/大帧/TLS 修复，已 push worktree-tmp-bella-la64；check-loongarch64 门禁转绿，verify-all 19/19） | b68447c |
| chloe | lite | worktree-tmp-chloe-mirp2 + worktree-tmp-chloe-memconst (自 worktree-mxx-work@9742e2f) | /tmp/mxx-wt-chloe | MIR Phase 2：强制 MIR-native + TLS PIC（g_pic 正版）+ verify-all 19 步 + **MIR-native -O1 内存常量传播（check-olevel 差距③）** | **completed**（6cafb11/0702745/318e184 已合入；memconst 本次归并合入主线 **8d0aace**，-O0/-O1 指令数断言转绿） | 81186b3 |
| diana | lite | worktree-tmp-diana-dwarfloc (自 worktree-mxx-work) | /tmp/mxx-wt-diana | C23/C11 边界测试 + check-pic-verify + 错误码体系/多错收集 + DWARF 调试信息 + **DWARF 变量位置** | **completed**（db1451b C23、6db1691 PIC、errcode da5a646/a561b36/c204bbe、dwarf dfdb0db/381bfdd/ad4d69a 均已合入主线；dwarfloc d032082 已 push） | 11 test/c23 + F1-F3 + GOT + E#### + DWARF4 + DW_AT_location |
| eve | lite | worktree-tmp-eve + worktree-tmp-eve-olevel + worktree-tmp-eve-i18n (自 worktree-mxx-work) | /tmp/mxx-wt-eve | m++ 负向测试矩阵 + -O 级别语义分级 + **i18n 消息目录与双语（--lang=en/zh、LANG 推断、--explain/usage/--error-json 双语、check-i18n 目标）** | **completed**（测试矩阵已合入 b4cad7e；eve-olevel 已合入 a1bbb85；eve-i18n 本次归并合入主线 **1ce1b33**，check-i18n 通过） | worktree-tmp-eve-i18n |
| grace | lite | worktree-tmp-grace-cpp23 (自 worktree-mxx-work@1ef0a9a) | /tmp/mxx-wt-grace | sema/decl E1/E4/E5/E6（已合入）+ C++23 缺口：P0849/P1774/P1401/P2360/nodiscard | **completed**（sema 已合入；cpp23 f7e313a+b54c8b9+16c2ca5+2a4d655+da7a107 **已合入主线** ba6d9f8） | push worktree-tmp-grace-cpp23 |
| hazel | lite | worktree-tmp-hazel-aarch64 (自 worktree-mxx-work@fc1f279) | /tmp/mxx-wt-hazel | MIR Phase 3b aarch64 移植（标量整数 MIR-native） | **completed**（5 commit 已 push origin/worktree-tmp-hazel-aarch64，52/53 样例 as 通过，verify-all 全 PASS） | 15d4261 |

### 会话中断恢复速查（当前团队）
1. `git fetch origin`（在 /workspace/MeuOS-Kit）
2. alice 的分支：`worktree-tmp-alice`（含 requires 半成品 c9ca880 续作）；bella：`worktree-tmp-bella`
3. 需要续接时 spawn 同名 worker，prompt 指向对应 worktree 路径即可
4. 半成品保护范例：`worktree-requires-wip` = requires 半成品恢复点（网络故障时保护成功）

### 门禁已知差距（2026-08-03 归并后）
- **check-olevel：三项已知差距已全部清零**（本批 6 分支归并）：① MIR-native if-conversion（cmov）→ **bella**（机器层 ifconv 通道 + MMOP_CMOV）；② -O2 省略叶函数帧指针 → **hazel**（a988893，worktree-tmp-hazel-fp）；③ -O1 内存局部常量传播 → **chloe**（worktree-tmp-chloe-memconst）。**check-olevel 实测 PASS（RC=0）**，含 -O0/-O1 指令数断言。
- **check-pic-verify：已修复**（97d5467，x86_64 MIR-native -fPIC GOT 回归——MIR Phase 2 强制 MIR-native 后丢失外部符号 GOT，emit_global_addr + @gotpcrel + @plt 修复，四架构全过）。

### 门禁状态（2026-08-03 alice mt/ld 修复归并后）
- **check-mt-integration：已闭环**（alice 3d3f91f，归并 ad52f9b）：根因是 mt/as x86_64 `movq $imm, %r64` 的 imm64 截断编码（encode.c 对 width==8 用 0xb8 movabs 形式但 imm 只写 4 字节，后续指令被吞进立即数 → 解码垃圾 → crt1 入口段错误 0x40101c）。**门禁已知失败清零：verify-all 恢复 19/19 全绿**。

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
- 状态：**已修复**（alice 608f31c，测试 operator_ref_const.cc），check-cpp-func 绿

### D4 缺陷：非空类按值返回错乱（实为 ctor 初始化列表标量成员落地缺失）
- 根因：`cpp_parse.c emit_base_ctors_for`（基线 2052-2165），行 2068-2069 只对 struct/union 成员发 ctor 调用，标量成员 init-list 项被 `continue` 丢弃
- 修复方案：对命中初始化项的非 struct/union 成员发射 `*(this+offset)=args[0]`
- 状态：**已修复**（alice 608f31c，测试 ctor_scalar_initlist.cc），check-cpp-func 绿

### D2 缺陷：急切实例化未使用成员函数（非惰性）
- 根因：`cpp_parse.c flush_pending_methods`（1176-1199），行 1197 无条件 `cpp_parse_method_body`
- 修复方案：模板实例化期间延迟模式 + 保留 pending_method 进 per-class 延迟表 + 调用点按需重放（**不能简单 continue 丢弃**，probe 已证明会破坏已使用方法）
- 状态：**已修复**（alice d28c744，测试 tmpl_lazy_methods.cc；funcexpr EXPRCALL 按需重放 + 尾节点重锚 g_cpp_deferred_end），check-cpp-func 绿
- 消息瑕疵已修复：按需解析报错成员名曾显示 `(null)`（expr_postfix.c 错误消息误用 tok.lit，标识符文本在 tokenstr 表）。修复改用 tokenstr(tok.kind)，测试 tmpl_member_undefined.neg.cc

### #elifdef/#elifndef 修复
- 状态：**已合入主线**（pp.c→6ca4ba1，测试→e472811），已闭环

### chibicc run.sh sysroot 修复
- 状态：**已合入主线**（f05633f），PASS=9/RUNFAIL=6/COMPILEFAIL=26，已闭环

### chibicc conformance 缺陷组（hazel，worktree-tmp-hazel-conform）
- 状态：**completed**（5 commit 已 push origin/worktree-tmp-hazel-conform，chibicc PASS 11→16、RUNFAIL 5→0、COMPILEFAIL 25 不变）
- cd7e5d5 bitfield：全局位域静态初始化打包（emitdata 单元重叠非位域成员时右移保留）
- 2d179b1 浮点转换：MOP_F2I 折叠按目标宽度（64 位）；新增 MOP_UI2F 无符号 int→float（折叠 + x86_64 技巧发射 + bridge）
- 0ab75ff 字面量：十进制超范围回绕 signed long long（18446744073709551615>>63 == -1，对齐 chibicc）
- 0feff81 宽字符：越界回绕（C11 6.4.4.4 实现定义）+ 有符号宽 escape 符号扩展
- 24596ee commonsym 测试适配：commonsym_ext.c 提供 common_ext2=3 强定义（跨 TU common 合并）
- 剩余 25 COMPILEFAIL 均为 GNU 扩展（({})/alloca/asm/attribute）、chibicc 宽松指针（char16_t*→char* 等，非标准）、测试自身偏差（extern/function/tls 重声明）与环境（pragma-once 路径），不属 mcc conformance 缺陷

### chibicc B 类真 bug（常量折叠窄化转换 + va_end 类型检查）
- 状态：**已合入主线**（hazel a932c60 胜出，覆盖窄化 IAND 掩码+符号扩展 + TYPEATOMIC 解包 + va_end no-op，已 merge 69a6f2c）

### C++20 标准缺口补齐（alice，分支 worktree-tmp-alice-cpp20）
- **NTTP**（6fc4d57）：`template<int N>`/`template<auto N>` 支持（函数/类模板、显式实参、constexpr 折叠）；`template<T, T N>` 依赖类型 NTTP 仍缺。测试 test/cpp/nttp.cc；移除过时负向测试 nttp.neg.cc
- **consteval 即时调用强制**：非常量实参编译报错；consteval 函数体为常量上下文跳过检查。测试 consteval_immediate.cc / consteval_nonconst.neg.cc；更新 consteval_boundary.cc
- **类类型三向比较**：cpp_op_mangle 补 TSPACESHIP→'ss'，成员 operator<=> 重载与 a<=>b 调用。测试 spaceship_member.cc
- **聚合初始化**：括号直接构造（聚合按成员序）+ 直接列表初始化（声明符后 '{'）。测试 aggregate_init.cc
- **constexpr 聚合对象成员访问**（mini 内存模型）：成员值表 obj+offset->value，static_assert 内 p.a/p.b 可求值。测试 constexpr_obj_member.cc
- 门禁 check-cpp-func/neg/c-mir 全绿

### CLI 参数与产物控制补充（alice，分支 worktree-tmp-alice-cli，commit b480024）
- **-pg**：接受（gprof no-op），不再 unknown option
- **--verbose**：-v 长形式别名，打印驱动各阶段命令
- **--color[=auto|always|never]**：诊断颜色参数可控（token.c g_diag_color，取代 isatty 独占）
- **-x c/c++**：强制语言解析，覆盖默认与后缀推断
- **-std= 语义化**：c89..c23 与 c++98..c++23 映射 g_std_mode，ppinit 定义 __STDC_VERSION__/__cplusplus
- **-fno-omit-frame-pointer/-fomit-frame-pointer**：映射 g_force_fp；其它 -f/-fno- 接受 no-op
- **-W 细粒度**：-Wno-error/-Wno-all 取消对应组
- **-Wa,/-Wl, 透传**：汇编/链接选项转发 host 工具链（run_host_cc/run_host_link 加参）
- 测试：test/driver/cli-args.sh（check-driver 接入）；usage.c 帮助同步
- 门禁：check-driver 全绿；verify-all 见状态行

### 文档同步（cpp20/cpp23-gaps.md、c23-review.md）
- 状态：**已合入主线**（edb854b + eb8372d），已闭环；cpp20-gaps.md 已由 alice 更新 5 项状态

### check-pic-verify 修复（i386/riscv64 GOT，-fPIC）
- 状态：**已修复**（diana 6db1691，分支 worktree-tmp-diana-pic，已 push）
- 根因：`i386_emit.c` SExt Oaddr 恒发绝对地址 `movl $sym`（不查 T.pic）；`riscv64_emit.c` loadaddr SExt 恒发 `la`（非 GOT）
- 修复：
  - i386：`i386_targ.c` 无条件保留 %ebx（SysV PIC 基址寄存器）；prologue 在 T.pic 下 push %ebx + `call __x86.get_pc_thunk.bx` + `addl $_GLOBAL_OFFSET_TABLE_, %ebx`（R_386_GOTPC）；SExt 地址加载在 T.pic 下改 `sym@GOT(%ebx)`（R_386_GOT32X）
  - riscv64：SExt 地址加载在 T.pic 下改 `auipc %got_pcrel_hi + ld %pcrel_lo(label)`（label 配对 R_RISCV_GOT_HI20 + R_RISCV_PCREL_LO12_I，gas 不接受 %got_pcrel_lo 修饰符）；extern 调用加 `@plt`
  - 非 PIC 路径不变（i386 绝对地址 / riscv64 plain `la`）
- 验证：check-pic-verify 全绿（x86_64/aarch64/riscv64/i386）；check-c99 / check-c-mir / check-i386 无回归；i386 非 PIC 运行时（IA32 模拟）通过

### 错误码体系与多错收集（diana errcode 分支）
- 状态：**已修复**（da5a646 错误码+caret 全跨 / a561b36 JSON 多错收集 / c204bbe fix-it，分支 worktree-tmp-diana-errcode，已 push）
- 内容：
  - `error: E%04d: <msg>`（E0001 语法/E0002 未声明/E0003 类型/E0004 重定义），`--error-json` 增 `code`/`end_col`
  - caret 单 ^ → 覆盖 token 全跨（error_tok_code 按 token 文本宽度）
  - `--error-json` 多错收集（上限 10，顶层循环 setjmp/longjmp 恢复 + err_sync 跳到下一 ';'/'}'），结束后出错仍 exit(1)
  - fix-it：缺分号 `note: add ';' here` + 未声明标识符编辑距离拼写建议 `note: did you mean 'X'?`
- 验证：verify-all.sh 17 PASS / 0 FAIL / 0 SKIP

### DWARF 调试信息支持（diana dwarf 分支）
- 状态：**已修复**（dfdb0db -g 分级 / 381bfdd 最小 DWARF4 / ad4d69a bootstrap 修复，分支 worktree-tmp-diana-dwarf，已 push）
- 内容：
  - `-g0`（完全关闭，无 .file/.loc/.debug_*）/ `-g`（默认 1）/ `-gN`(1,2,4,5) / `-gdwarf[-N]`；MIR/LIR 文本 dump 移至 `-dX`
  - 最小可用 DWARF 4：`.debug_line`（显式产出，emitdbgloc 在 -g 下抑制）/`.debug_abbrev`/`.debug_info`
  - compile_unit DIE（name/producer/C99/low_pc/stmt_list/comp_dir）、subprogram DIE（名/low_pc/high_pc=size/decl_line/file/frame_base=rbp）、variable DIE（名/类型/decl_line；位置暂缺）、base_type DIE（int/char/uint/pointer）
  - 局部变量经 funcalloc 记录（func.dvars）；`readelf --debug-dump=info` 可解析 CU/function/variable
  - 已知限制：变量无 DW_AT_location（后端未回传最终栈偏移），gdb 可见变量但不可取地址
- 验证：verify-all.sh **19 PASS / 0 FAIL / 0 SKIP**（含 check-sysroot-static 自举）

### DWARF 变量位置 DW_AT_location（diana dwarfloc 分支）
- 状态：**已修复**（d032082，分支 worktree-tmp-diana-dwarfloc，已 push）
- 机制：memit 静态 alloca 处记录最终帧偏移（g_alloca_cur）到 dwarf_loc 表（key=MVal id，rsp/rbp 帧基按 g_omit_fp）；func_to_mir 保留 value id→MVal vmap；emit.c 收集时反查
- 产出：栈变量 `DW_OP_fbreg(offset)`，寄存器变量 `DW_OP_regN`（预留）；帧基在 dwarf_end_func 捕获
- 验证：readelf 显示 a=fbreg -24/b=-40/s=-56 与 asm 帧偏移一致；gdb 单步后 `print a`=42 / `print b`=8 读取正确
- 已知限制：legacy bridge 路径（MCC_USE_MIR=0）无 memit 记录，变量保持无 location（统一处理）

---

### 缺陷队列（待修复）
- **E1** 命名空间作用域变量重定义 `int a; int a;` 漏检（eve 发现，pending/redef_global_var.neg.cc）→ **done**（grace，decl.c 重定义诊断 + 测试转正 test/cpp/redef_global_var.neg.cc）
- **E2** 类内重复成员 `int x; int x;` 漏检（**已修复** alice，struct_decl.c addmember 检查；测试移至 test/cpp/dup_member.neg.cc）
- **E3** 重复枚举符 `enum E{a,a};` 漏检（**已修复** alice，specs.c tagspec 检查；测试移至 test/cpp/dup_enum.neg.cc）
- **E4** 非 void 函数缺 return 漏检（pending/missing_return.neg.cc）→ **done**（grace，func_falls_off_end CFG 可达性 + decl.c 检查 + 测试转正 test/cpp/missing_return.neg.cc；**GNU noreturn 误报已修复**——attr.c PREFIXGNU 补 noreturn + qualtype.kind 传播 GNU `__attribute__` 到 decl，test/cpp/noreturn_attr.cc 覆盖四风格；**行号定位已修复**——struct func.bodyend 记录函数体 } 位置，报错不再落到下一 token）
- **E5** 引用未初始化 `int&r;` 漏检（pending/uninit_ref.neg.cc）→ **done**（grace，decl.c 声明处检查 + 测试转正 test/cpp/uninit_ref.neg.cc）
- **E6** C++ 模式禁用 VLA 未生效（pending/vla.neg.cc）→ **done**（grace，decl.c PROPVM 检查 + 测试转正 test/cpp/vla.neg.cc）
- **D1** const T& operator 整体失败（**已修复** alice 608f31c，cpp_try_operator_call 4-way 级联）
- **D4** ctor 标量成员 init-list 落地缺失（**已修复** alice 608f31c，emit_base_ctors_for）
- **D2** 急切实例化未使用成员（**已修复** alice d28c744，flush_pending_methods 延迟表）
- **F1** constexpr 函数体纯度不诊断（constexpr 函数调用 printf 不报错，diana 发现，test/c23/neg/constexpr.neg.c）— **done**（hazel 8e5aae3）
- **F2** `nullptr_t` 变量进真值条件 ICE（内部错误 unsupported conversion，diana 发现）— **done**（hazel 8e5aae3）
- **F3** 具名 constexpr 变量不折叠进整数常量表达式（_Static_assert(K==9) 报非常量，diana 发现）— **done**（hazel 8e5aae3）
- **C++23 纯缺口 4 项** → **全部 done**（grace worktree-tmp-grace-cpp23）：P0849 `auto(x)`（f7e313a，expr_primary）、P1774 `[[assume]]`（b54c8b9，attr.c 要求括号参数形式 no-op）、P1401 if constexpr 窄化转 bool（16c2ca5，PROPINT→PROPSCALAR）、P2360 init 语句 alias + C++11 using 别名（2a4d655，cpp_using_decl 支持 `using Name=Type;` + if 头部 init-statement 扫描）。属性语义：alignas 本已生效、nodiscard 丢弃返回值警告（da7a107）；deprecated/fallthrough/maybe_unused 使用点警告留待后续。
- **chibicc B 类**：常量折叠窄化 + va_end 类型检查（verify2/gate3/chi4 定位，chloe 精确行号，grace/hazel 竞争中）

### x86_64 MIR-native fallback 闭环（bella，worktree-tmp-bella-mirp1）
- 分支：`worktree-tmp-bella-mirp1`（自 worktree-mxx-work），已 push origin
- 目标：MCC_MIR_BACKEND=1 从"标量为主、聚合/TLS/VLA fallback 到 bridge"变成 x86_64 完整路径
- 三个提交：
  - `4c908df` 聚合实参 + SALLOC：mbe_supported() 放开 MOP_ARG/MOP_CALL(MV_TYPE) 与 MOP_SALLOC（SysV 分类/BLIT/栈传参在 mabi 已就绪）；新增 test/c11/aggregate_arg.c
  - `1a1d599` TLS：emit 增加 emit_tls_addr()（movq %fs:0 + leaq sym@tpoff，local-exec），覆盖地址取址/寻址基址/MC_ADDR；新增 test/c11/tls_basic.c
  - `7e78598` 动态 alloca/VLA：MFnM.dynalloc 标志 + emit 动态序列（subq rsp/16 对齐/leaq 0(rsp)）+ 前置扫描（块逆序输出问题）+ epilogue 从 rbp 恢复 rsp；新增 test/c11/vla_boundary.c
- 验证：check-c-mir 默认与 MCC_MIR_BACKEND=1 双路径 fail=0；check-mir 全绿（mabi 45/regalloc 45）；check-sysroot-static 自举默认路径 exit 0（MIR_BACKEND=1 自举另验）
- 说明：check-c99/c11/c23 默认 specs 需要 meuos-sysroot libc（-lc-meuos），本 worktree 未装 sysroot 属环境问题，与 MIR 改动无关（mir_matrix 显式 --specs=host 全绿）

### ≤16B 聚合按值返回修复（#94，bella+chloe 竞争融合，worktree-tmp-bella-mirp1）
- 现象：MCC_MIR_BACKEND=1 下 8/12/16B struct 按值返回 SIGSEGV（24B+ sret 正常）；cpp 侧 ref_ctor/ctor_scalar_initlist/structured_binding 崩
- 根因（互补三处）：① mabi_selcall ≤16B else 分支为空（call->dst 未指向真实 pad，RAX 数据当地址解引用）；② 混合类返回寄存器映射位置式错误（SysV 按类计数 INTEGER:rax,rdx / SSE:xmm0,xmm1）；③ regalloc 区间 start 被多 def 覆盖（call dst 先赋 pad 地址、call 再 re-def → [mov,call) 区间失去保护 → 线性扫描复用寄存器，链式 `a+b+c` 的 this 被覆盖）
- 提交：chloe `4aaa11f`（mabi pad+per-class retreg+aggregate_ret_small 测试）、`fafa676`（memit 直接 movsd + regalloc 多 def 区间 + aggregate_return 测试）；bella `72a04bd`（MCC_MIR_BACKEND 目标门控限 x86_64）、`16273af`（MIR-native TLS 适配 PIC @gottpoff）
- 验证：`MCC_MIR_BACKEND=1 sh test/verify-all.sh` **17/17**（含 check-driver shared/TLS、check-i386/loongarch64/targets 交叉目标）；默认模式亦 17/17
- 备注：与 chloe 撞车为竞争融合正面案例，chloe 4aaa11f/fafa676 与其 Phase 2 分支（worktree-tmp-chloe-mirp2）无文件重叠

### Phase 2：x86_64 默认强制 MIR-native（#80，chloe，worktree-tmp-chloe-mirp2）
- 目标：MCC_USE_MIR=0 不再生效（g_use_mir 恒 1）；x86_64 默认走 MIR-native（g_use_mir_backend 默认 1）；删 emit.c 死 fallback 路径。
- 提交：`e6f8c56`（main.c g_use_mir 恒 1 + g_use_mir_backend 默认 1；emit.c 删 NULL fall-through + `strcmp(T.name,"x86_64")` 目标门控）→ merge bella #94 收口 → `20e6988`（memit TLS PIC：新增 g_pic 全局镜像 T.pic，emit_tls_addr/mov_to_rax/emit_const 三处 @gottpoff 分叉，守 MIR 纯纪律优于 bella 16273af 的 T.pic 方案，已获 bella review 采纳）→ `0702745`（merge bella 72a04bd/16273af/e4b420b，memit 冲突以 g_pic 版为准）。
- 验证：MCC_MIR_BACKEND=1 与默认模式双路径 verify-all 均 **19/19**（含 check-cpp-func/neg × MIR-native/bridge 双后端变体、check-driver TLS、自举）；此前 3 例 cpp 聚合返回段错误全修复。
- verify-all.sh 改造：新增 `check-cpp-func/neg × MCC_MIR_BACKEND=1/0` 双后端显式复验步骤（补"cpp 套件只覆盖单一后端"缺口，17→19 步）。
- 非 x86_64 target（aarch64/arm/riscv64/loongarch64/i386）仍走 MIR→bridge→LIR。

### MIR-native -O1 内存常量传播（#99，chloe，worktree-tmp-chloe-memconst）
- 目标：check-olevel 的 `-O0 (243) > -O1 (243)` 断言转绿。根因：MIR -O1 FOLD 不做内存局部常量传播（`int k=7; x+(k+1)` 的 k 走栈槽，读取 load 未被折叠为常量）。
- 实现：`src/mir/passes.c` msimp_block 新增**块内 store→load 常量转发**（MMemC 表）——常量 store 到 MOP_ALLOCA 栈槽后，同块同址 load 折叠为常量（掩码到 load 宽度）；保守规则：未知/非 alloca 地址的 store 与 call 全失效、不同 alloca 互不别名、load 不失效。alloca 集合在 FOLD 入口预收集（避免 deref 被压实失效的 `MVal.def`）。
- volatile 不折叠：新增 `INST_VOLATILE`（struct inst.flags）→ func_to_mir 传播到 MIR `MIns.extra` bit1 → memconst 跳过。funcload 加 `enum typequal tq` 参数（caller 传 `e->qual`），`(t->qual|tq)&QUALVOLATILE` 标记 volatile load。前端 funcstore 本就拒绝 volatile store。
- 踩坑修复：①`memc_const` 宽度映射漏 MT_I16（513&0xFF=1，narrow_cast 回归）；②load 折叠未查 `used_outside`（switch 值跨块用被删定义，attr_basic 回归）；③`addr->def->op` 在块压实后失效导致自举崩溃（改 alloca 集合）。
- 测试：新增 `test/olevel/memconst.c`（const_local/const_twice/overwrite）+ run.sh 专项断言（memconst -O0=194 > -O1=177，运行时 O0/O1 均过）。
- 验证：check-c-mir fail=0、c99/c11/c23/cpp-func/cpp-neg 全绿、check-sysroot-static 自举 exit 0、**verify-all 19/19**。

## 5. 纪律速查

- 提交：文件级 `git add <文件>`，**禁止 `git add -A`**；建议 `git commit --only <path>` 规避共享 index 竞态
- 分支：核心交付前只推 worktree 分支远程，**禁止合并 main**
- 半成品：立即提交 + push 独立 wip 分支，禁止留工作树
- 模型：reasoning 限 2 个（贵）；lite(hy3) 免费可 10+ 并行；禁止 default
- 夹带：遇夹带**维持现状不 force push**（已发生 3 次：647a05b/93ab4b4/6ca4ba1）
- 门禁：每提交跑对应 `make check` / verify-all.sh，通过才 push
- 竞争实现：双 worker 竞争同一任务（快速拿正确实现）。**竞争落败者的思路若优秀，也要参考融合进最终方案**（team-lead 审阅双方报告后合并优者），不浪费洞察。竞争派发时要求双方各自输出"实现 + 理由"，由 team-lead 统一裁决融合。
