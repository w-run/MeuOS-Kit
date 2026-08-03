# .agents/reference/ — 详细参考文档

> 从 AGENTS.md 下放的详细参考（2026-08-04）。AGENTS.md 是精简核心规约，各章节在此有完整版。
> **按需读取**：新 agent 启动只读 AGENTS.md，涉及具体领域时再打开对应文件。

## 索引

| 文件 | 内容 | 对应 AGENTS.md 章节 |
|------|------|---------------------|
| [components.md](components.md) | 各组件规范（libc/mcc/meow/toolchain/utils/shell/buildtools/compress/libtui/kernel）、软件包策略、mz 压缩库 | §2 |
| [bootstrap.md](bootstrap.md) | 自举流程：组件构建依赖链 + Phase 0-7 阶段 | §3 |
| [organization.md](organization.md) | 项目组织：目录结构、构建约定、QEMU 环境、Issue/TODO 导航系统 | §5 + §11 |
| [strategy.md](strategy.md) | 实现策略（三阶段路径/参考资源）+ 任务编排（颗粒度/卡片五要素/阶段归档/循环任务） | §6 + §7 |
| [build-reference.md](build-reference.md) | 构建与测试命令速查（构建/门禁/自举/跨架构/调试） | §8 |
| [knowledge-mgmt.md](knowledge-mgmt.md) | 知识库管理：IMA 集成、本地知识沉淀、Agent 启动读取流程 | §9 |
| [status.md](status.md) | 项目状态速查：里程碑、待启动工作、架构支持矩阵、文档索引、CI | §10 |

## 更新约定

- 修改 AGENTS.md 中对应章节内容时，同步更新 reference 文件。
- 状态类文件（status.md）易过时，权威状态以 `.issues/` 与各组件 ARCHITECTURE.md 为准。
