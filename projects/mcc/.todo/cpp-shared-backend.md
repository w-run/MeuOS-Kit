<!--
priority: P2
status: in_progress
progress_note: Phase A 已验证 (libmcc.a build OK, make check 全绿); Phase B/C/D 待 m++ 启动时实施
note: mcc/m++ 共享后端架构(libmcc 化)的分阶段计划;阶段 A 已落地,阶段 B/C/D 待 m++ 启动时实施
start_ts: 2026-07-24
-->

# 待规划：mcc/m++ 共享后端架构调整（libmcc 化）

## 背景

当前 `mcc` 是单体可执行文件：C 前端（lex/parse/sema）+ AST→IR 桥
（irgen）+ IR/优化/ABI/emit + 后端 target 全部链接进一个二进制。

如果未来要支持 C++（`m++` 二进制），C 与 C++ 的
lex/parse/sema/irgen **完全不同**（C++ 的 template、overload resolution、
name lookup、class、virtual、exception handling 都是 C 没有的），
但 **IR 优化、ABI 分类基础、后端代码生成** 是语言无关、可复用的。

理想结构：提供 `mcc` / `m++` 两个二进制，共用后端 `libmcc` 库
（主静态、辅动态）。

## 当前结构（2026-07-22）

```
projects/mcc/src/
├── driver/      # main、argv 解析、target 选择、host 工具链交接
├── lex/         # C 词法 + C 预处理器（C 特有）
├── parse/       # C 语法分析（C 特有）
├── sema/        # C 语义分析（type/scope/init/eval/targ，C 特有）
├── irgen/       # C AST → IR 构造（桥接层，C 特有）
├── ir/          # IR 定义、optab、printfn（语言无关）
├── opt/         # IR 优化 passes（ssa/gvn/gcm/spill/rega/...，语言无关）
├── abi/         # ABI 分类（C ABI；C++ 需扩展）
├── emit/        # 数据/函数 emit（语言无关）
└── target/<arch>/  # 后端 isel+emit+targ+abi（语言无关）
```

## 目标结构

```
projects/
├── libmcc/                    # 共享后端库（主静态 libmcc.a + 可选 libmcc.so）
│   ├── include/               # 公共库 API（清晰的对外接口）
│   │   ├── ir.h
│   │   ├── ir_ops.h
│   │   ├── target.h           # Target 抽象（已存在）
│   │   ├── abi.h              # ABI 公共分类接口
│   │   └── libmcc.h           # 库总入口（编译一个 Fn / 一段 Dat）
│   ├── ir/                    # IR 定义（语言无关）
│   ├── opt/                   # IR 优化 passes（语言无关）
│   ├── abi/                   # ABI 基础（C/C++ 共享部分）
│   ├── emit/                  # 数据/函数 emit（语言无关）
│   └── target/<arch>/         # 后端（语言无关）
├── mcc/                       # C 编译器二进制
│   ├── driver/                # C driver（含 host_toolchain/target_select）
│   ├── lex/                   # C lexer + C 预处理器
│   ├── parse/                 # C parser
│   ├── sema/                  # C sema
│   ├── irgen/                 # C AST → IR
│   └── main.c
└── m++/                       # C++ 编译器二进制（未来）
    ├── driver/                # C++ driver（复用 mcc 的 host_toolchain/target_select）
    ├── lex/                   # C++ lexer + 兼容 C 预处理器
    ├── parse/                 # C++ parser
    ├── sema/                  # C++ sema（class/template/overload/name lookup）
    ├── irgen/                 # C++ AST → IR（含异常、vtable、RTTI、mangling）
    └── main.c
```

## 关键耦合点分析

### 1. ABI 层（最复杂）

- 当前 `src/abi/abi.c` 只处理 C ABI（SysV psABI 的整数/浮点/聚合分类）
- C++ ABI 额外需要：
    - **Name mangling**（Itanium C++ ABI / GCW ABI）
    - **Vtable layout**（虚函数表布局、虚基类偏移）
    - **Class member layout**（含空基类优化、位域、对齐）
    - **RTTI**（typeinfo、dynamic_cast）
    - **Exception handling tables**（.gcc_except_table、LSDA）
    - **Pointer-to-member** 布局
- **分层方案**：
    - `libmcc/abi/abi_base.c` — 整数/浮点/聚合 ABI 分类（C/C++ 共享）
    - `m++/sema/abi_cxx.c` — C++ 特有（mangling、vtable、RTTI、EH）

### 2. IR 类型系统（Typ 表）

- 当前 `Typ` 表只表达 C 类型（function、aggregate、padding）
- C++ 需要：
    - **Class metadata**（基类链、虚函数表索引、成员偏移）
    - **Template instantiation cache**
- **方案**：扩展 `Typ` 加 `class_meta` 字段，由 `m++/irgen` 填充，
  libmcc 后端只关心 size/align/class，不解析 metadata。

### 3. 异常处理（IR 扩展）

- C++ 的 try/catch/throw 需要：
    - IR 支持 landing pad、invoke 指令（call 的 EH 版本）
    - eh frame、unwind table 生成
    - `__cxa_throw` / `__cxa_begin_catch` 运行时调用
- **方案**：libmcc 的 IR 加 `Oinvoke`（带 eh_labels 的 call）、
  新增 `emit_eh_table()` 函数；后端无需改动（landing pad 是普通块）。
- 这是 IR 层扩展，不是后端层；mcc（C 前端）不使用，不增加成本。

### 4. driver 共享

- `host_toolchain.c` / `target_select.c` 与语言无关，应抽到 libmcc
  或共享 driver 工具库
- C/C++ 的命令行参数差异：
    - C：`-std=c11`、`--specs=meuos`
    - C++：`-std=c++20`、`--specs=meuos-cxx`、`-fno-exceptions`、`-frtti`
- **方案**：driver 拆为 `driver_common.c`（host toolchain、target select）
    - `driver_c.c` / `driver_cxx.c`（语言特定选项）

## 实施阶段（渐进、可回滚）

### 阶段 A（最小破坏性改动，验证可行性）✅ 已完成（2026-07-22）

- **不改目录结构**，只在 `projects/mcc/Makefile` 把后端 .o 打成
  `libmcc.a`（包含 `ir/opt/abi/emit/target/util` 的所有 .o，共 41 个）
- mcc 二进制 = C 前端 .o（39 个：driver/lex/parse/sema/irgen）+ libmcc.a
- **验收**：全绿（check / check-c11 / check-driver / check-targets /
  check-i386 / check-i386-runtime / check-loongarch64 / check-abi /
  check-sysroot-static mcc 自重编译通过）
- **构建产物**：`build/libmcc.a`（2.35MB）、`mcc`（1.76MB）
- **行为零变化**：链接命令从"全 .o 直接链接"改为"FE .o + libmcc.a"，
  ld 从 .a 中按需拉取被引用的 .o，最终二进制功能等价

### 阶段 B（提取公共 API，准备 m++ 接入）

- 创建 `projects/libmcc/` 目录（仅 include 部分）
- 把 `include/{ir.h, ir_ops.h, x86_64.h, aarch64.h, ...}` 移到
  `projects/libmcc/include/`
- mcc 引用改为 `#include <libmcc/ir.h>`
- 抽取 driver 的 `host_toolchain.c` / `target_select.c` 到
  `projects/libmcc/driver/`（共享）
- **验收**：mcc check 全绿；新 include 路径稳定

### 阶段 C（后端代码物理迁移）

- 把 `projects/mcc/src/{ir,opt,abi,emit,target}` 迁到
  `projects/libmcc/src/`
- 更新 Makefile 生成 `libmcc.a` / `libmcc.so`
- mcc 二进制 = `src/{driver,lex,parse,sema,irgen}` + 链接 libmcc
- **验收**：mcc check 全绿；`nm mcc` 确认 C 前端符号 + libmcc 符号

### 阶段 D（m++ 接入，可选/未来）

- 新建 `projects/m++/` 目录
- C++ 前端独立实现，链接 libmcc
- 扩展 IR：`Oinvoke`、eh_table 生成
- 扩展 ABI：mangling、vtable、RTTI
- **验收**：m++ 能编译 `int main() { return 0; }` 并运行

## 优先级评估

- **不阻塞当前 aarch64 移植**：可先完成 P2 aarch64，再做架构调整
- **建议时机**：在 m++ 真正开始之前完成阶段 A+B（不破坏现有功能，
  为 m++ 铺路）
- **风险**：阶段 C 涉及大范围文件移动，需严格回归测试

## 影响范围

- `projects/mcc/Makefile`（构建产物从单一二进制 → libmcc.a + mcc 二进制）
- `projects/mcc/src/{ir,opt,abi,emit,target}/` → 迁移到 libmcc
- `projects/mcc/include/` → 部分迁到 libmcc/include
- `projects/mcc/ARCHITECTURE.md` → 重写目录结构说明
- `AGENTS.md` §2.3 → 更新组件列表（增加 libmcc 与 m++）
- `STATE.md` §1 → 增加 libmcc / m++ 阶段状态

## 前置依赖

- aarch64 移植完成（P2），避免重构期叠加架构移植风险
- 完整的 `make check` 回归基线（已有）

## 验收

- 阶段 A 完成：`libmcc.a` 产出，mcc 行为零变化（全绿 check）
- 阶段 B 完成：`projects/libmcc/include/` 就绪，可被外部链接
- 阶段 C 完成：mcc = 前端 + libmcc.a，二进制大小相近（<5% 增长）
- 阶段 D 完成（未来）：m++ 能编译并运行最简 C++ 程序

## 备注

- **不急于现在做**：先用 .todo 记录架构方向，待 aarch64 完成、
  确实要启动 m++ 时再分阶段实施
- **保持 MIT 许可**：libmcc 仍为 MIT，m++ 也为 MIT
- **参考实现**：
    - LLVM 的 clang/clang++ 共用 libLLVM（动态库模式）
    - GCC 的 cc1/cc1plus 共用 libbackend + libcommon
    - cxx-frontend（robertoraggi/cplusplus）：独立 C++23 前端，词法/语法/语义完整流程，AST 节点设计参考
    - aburi（serjective/aburi）：C/C++ 前端全流程（lex→preprocess→parse→ast→constexpr→ast2llvm），类/继承/虚派发/模板/异常/Itanium C++ ABI 参考
    - 本项目走 libmcc 静态库为主、动态为辅（更易自举、零运行时依赖）

## 验收标准

```bash
# Phase A: verify libmcc.a exists
test -f projects/mcc/build/libmcc.a
# Phase A: verify existing tests still pass
cd projects/mcc && make check
# Phase A: verify libmcc.a contains backend symbols (not frontend-only)
nm build/libmcc.a | grep -q 'T.*isel' && echo "backend symbols in libmcc: PASS"
```

