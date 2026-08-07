# 组件架构决策——buildtools 独立，不合并

**事实**：buildtools（m4/bison/flex/gperf）保持独立组件，不与 toolchain 合并，也不与 meow 合并。

**Why:** 权衡利弊后结论：
- **buildtools vs toolchain**：定位不同——toolchain 处理编译产物（as/ld/ar/...），buildtools 是代码生成器（m4/bison/...）。代码零共享。合并后 toolchain 的 37/37 门禁会被 buildtools 拖成 FAIL，混淆状态。
- **buildtools vs meow**：meow 是**构建调度器**（代替 make），不依赖 buildtools 自构建；buildtools 是**被构建的工具**。meow 成熟度不应被 buildtools 拖累。
- **归属**：buildtools 与 meuos-utils/msh 同级（Phase 6/7），非 mcc/toolchain 同级核心组件。

**How to apply:**
- data.json 中 buildtools 保持独立组件区块
- 门禁各算各的账，不合并 check 计数

**追加确认（2026-08-08）**：所有 m4/libm4 全在 buildtools 中统一维护。
- **meow** 不再内嵌 m4，需要配方宏展开时调 buildtools 的 m4 CLI 或链接 buildtools 的 libm4
- **buildtools** 统一管理所有 m4 代码：独立 CLI + 可链接的 libm4 库
- 这样 m4 实现只维护一份，不分散到两个组件