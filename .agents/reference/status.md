# 项目状态速查（.agents/reference/status.md）

> 从 AGENTS.md §10 下放（2026-08-04）。已完成里程碑、待启动工作、架构支持矩阵、文档索引、CI 管道。
> ⚠️ 本文件易过时，权威状态以 .todo/、.agents/knowledge/ 与各组件 ARCHITECTURE.md 为准。

## 10. 项目状态速查

### 10.1 已完成里程碑

- **mcc C11 完整实现** — `_Atomic`/`_Generic`/`_Thread_local`/`_Alignas`/`_Alignof`/`_Noreturn`/`_Static_assert`/匿名结构体/复合字面量/变长数组
- **mcc C23 特性** — `constexpr`/`typeof`/`typeof_unqual`/`nullptr_t`/`#embed`/`__has_include`/`[[]]` 属性/`#elifdef`/`#elifndef`/`#warning`/二进制字面量/数字分隔符/空初始化器/`auto` 类型推导/Labeled break/continue/`bool`/`true`/`false`/`_BitInt(N)`/`_Decimal32`/`64`/`128`/`static_assert` 无消息
- **6 个后端全部内置** — x86_64 / aarch64 / riscv64 / i386 / loongarch64 / arm（新增）
- **arm 完整移植（2026-07-27）** — mcc 后端 + libc 运行时（9 文件）+ mt as+ld，qemu-arm 验证通过
- **meuos-libc x86_64 完整运行验证** — stdio/stdlib/string/thread/signal/syscall/compat 全覆盖
- **meuos-libc aarch64 qemu 端到端验证通过**
- **meow 构建系统** — YAML 配方 / Makefile 兼容 / 并行构建（`-jN`）/ DAG 增量构建
- **meuos-toolchain 9 工具** — as/ld/ar/ranlib/nm/readelf/strip/objcopy/objdump
- **Phase 4 自举验证通过** — sysroot 内 mcc + meow 自重建全套工具
- **Phase 5 零宿主依赖验证通过** — mcc driver 集成 `MT_AS`/`MT_LD`，`check-mt-integration` 通过
- **.msys v2 完整实现** — v2 格式（64B header + 32B index + dir block）、SHA-256 去重/校验、ed25519 签名、Overlay 分层、流式消费、扩展块机制、xattr 扩展属性、msysctl 统一 CLI（22+ 命令）、Python ctypes 绑定。`msysctl fzf` 交互式浏览器支持。
- **稳定增强 worktree 完成（2026-07-29）** — 64 次提交全面完善全套工具链：
  - **meow `.meow` 自定义格式** — 替代 YAML，`run:` shell 脚本块 + `%VAR%` 插值 + 三元组推断 + `meow init/show/lint` 子命令
  - **ld TLS 动态模型完整实现** — link.c TLSGD/TLSLD/DTPMOD/DTPOFF 重定位 + ld.so PT_TLS/模块ID/连续布局/`__tls_get_addr` + bug-mt-so-undef 修复（shared UNDEF → JUMP_SLOT）
  - **mt-info 统一 ELF 分析工具** — 7 子命令（info/headers/deps/strings/which/diff/inspect）+ `--json` 跨工具输出
  - **mcc `--warn=` 语义警告体系** + 彩色诊断 + `--error-json` + triple 统一解析
  - **march-generic / cpu_detect** — `-march=native`/`x86-64-vN` + `/proc/cpuinfo` 跨架构回退
  - **as-isa-gating** — 两层指令门控（VEX 前缀快速检查 + required_features 精确门控）+ `--march=x86-64-vN`
  - **riscv-extensions / arm-multiver / i386-variants / aarch64-ext** — 全架构 `-march` 解析 + 特性位映射
  - **ci-pipeline / community-tests** — GitHub Actions 工作流 + chibicc 测试套件修复
- **`.meow` 宏系统完整实现（2026-07-29）** — 25+ 语义宏：
  - **run 修饰符** — `run(!)` 遇错中断 / `run(?)` 遇错继续 / `run(q)` 安静执行
  - **语义节** — `env:` 环境变量 / `download:` 下载 / `has:` 工具检测 / `lib:` 库检测 / `log:` 构建日志
  - **构建参数** — `toolchain:` 交叉前缀 / `cflags:` / `ldflags:` / `srcdir:` / `builddir:` / `parallel:` 并行度
  - **构建流水线** — `sha256:` 校验 / `unpack:` 解包 / `patch:` 补丁 / `test:` 测试 / `copy:` 复制 / `strip:` 去调试符号
  - **流程控制** — `only:` / `except:` 架构过滤 / `workdir:` 工作目录 / `pre:` / `post:` 钩子 / `error:` 回调 / `meta:` 元数据
- **pkgs/* 全面迁移至 `project.meow`** — 18 个构建包全部使用 `.meow` 配方格式，兼容保留 `meow.yaml`
- **pkg-config 内置替代** — `meow pkg-config` 子命令 + 27 库内置参数表 + recipe `uses:` 字段集成 + 构建时 `%LIBS%`/`%CFLAGS%` 自动展开
- **netdb.h 完整 POSIX 实现** — host/serv/proto/net 解析 + getaddrinfo/getnameinfo + reentrant `_r` 变体 + 7 项回归测试

### 10.2 待启动/进行中工作

| 工作项                                 | 状态     | 备注                                                             |
| -------------------------------------- | -------- | ---------------------------------------------------------------- |
| m++ C++ 前端（阶段 B/C/D）              | ⏳ 待启动 | 阶段 A（libmcc 分离）已完成；子阶段待 m++ 启动时实施               |
| meuos-buildtools（m4/bison/flex/gperf） | ⏳ 待启动 | Phase 6；替换 GNU 构建工具依赖                                    |
| meuos-utils（coreutils 等）             | 🟡 骨架  | Phase 7；worktree-shell-utils 启动，libutils.a + 5 工具烟雾通过；P1-P5 见 `projects/meuos-utils/.todo/` |
| meuos-shell (msh)                       | 🟡 骨架  | Phase 7；worktree-shell-utils 启动，三模式烟雾通过；P6-P8 POSIX sh + 交互层 + bash 兼容陆续推进 |
| meow `meowctl` 配置界面                | ⏳ 待设计 | `.meow` 格式已可用，配置/查询 CLI 待设计                           |
| meow 原生 shell 替代                   | 🔄 进行中 | 用 msh 替代 /bin/sh（阻塞于 msh 可用性）                          |
| mt DWARF 调试信息（P8）                 | ⏳ 待启动 | 调试信息生成                                                       |
| arm-multiver emit 多版本分支            | 🟡 待补 | ARM emit 层 `g_arm_arch_ver` 已就绪，v6/v7+/v8 指令分支待落地        |
| mcc atomic 窄类型符号扩展缺陷           | 🔴 回归 | 2026-08-04 审计：`atomic_fetch_add` 对 `atomic_short` 返回值零扩展(65534)而非符号扩展(-2)，致 libc `test/atomic.c` FAIL。verify-all 19/19 未覆盖。详见 `.todo/mcc/defect-atomic-narrow-signext.md` |
| mcc TLS 局部静态符号不一致缺陷          | 🔴 回归 | 2026-08-04 审计：`static _Thread_local` 变量 `.tbss` 段定义为 `.L<name>.N`，函数体引用 `<name>`，符号不一致致链接 `undefined reference`。meow `make check` 失败。详见 `.todo/mcc/defect-tls-static-local-symbol.md` |
| check-pic-verify aarch64/riscv64 GOT    | 🔴 缺口 | 2026-08-04 审计：x86_64/i386 PIC GOT ✅，aarch64/riscv64 仍失败。diana 6db1691 仅修 i386+riscv64（riscv64 实测仍失败），aarch64 从未覆盖。`worker-deployment.md` §3/§4 "四架构全过"为错误声明 |

### 10.3 各架构支持矩阵

| 架构         | mcc 后端 | libc 核心 | mt/as     | mt/ld     | qemu 运行时验证       | 系统依赖                   |
| ------------- | -------- | --------- | --------- | --------- | --------------------- | -------------------------- |
| x86_64        | ✅       | ✅        | ✅ P0-P9  | ✅ P0-P9  | ✅ 完整验证           | 无                         |
| aarch64       | ✅       | ✅        | ✅        | ✅        | ✅ qemu 端到端        | `aarch64-linux-gnu-gcc`    |
| riscv64       | ✅       | ✅        | ✅ P11    | ✅ P11    | 🟡 exit=42 通过       | `riscv64-linux-gnu-gcc`    |
| i386          | ✅       | ✅        | ✅        | ✅        | 🟡 qemu 系统仿真      | `gcc -m32` + 32-bit libc   |
| loongarch64   | ✅       | ✅        | ✅        | ✅        | 🟡 exit=42 通过       | `loongarch64-linux-gnu-gcc` |
| arm           | ✅       | ✅        | ✅        | ✅        | ✅ qemu-arm 运行时     | `arm-linux-gnueabihf-gcc`  |

### 10.4 相关文档索引

| 文档路径                                           | 内容                                    |
| -------------------------------------------------- | --------------------------------------- |
| `AGENTS.md`（本文件）                                | 项目规约、命令参考、知识库管理           |
| `README.md`                                        | 快速开始与项目简介                      |
| `projects/mcc/ARCHITECTURE.md`                     | 编译器架构、模块职责、阶段状态           |
| `projects/meuos-libc/ARCHITECTURE.md`              | C 库目录结构与模块职责                  |
| `projects/meuos-libc/PORTING.md`                   | 多架构移植说明、ABI 契约、time64 策略   |
| `projects/meow/ARCHITECTURE.md`                    | 构建系统模块职责与数据流                |
| `projects/meuos-toolchain/ARCHITECTURE.md`         | 工具链架构、P0-P11 分阶段任务           |
| `projects/meuos-sysroot/ARCHITECTURE.md`           | .msys 格式设计与依赖关系               |
| `projects/meuos-utils/ARCHITECTURE.md`             | 核心工具集架构 + libutils.a + 工具路线图（Phase 7 骨架已建） |
| `projects/meuos-shell/ARCHITECTURE.md`             | msh 架构 + 三模式 + P6-P8 路线图（Phase 7 骨架已建） |
| `env/README.md`                                    | QEMU 测试环境使用说明                  |
| `.todo/README.md`                                 | 项目待办索引（唯一待办来源，按项目子目录） |
| `.agents/knowledge/README.md`                     | 全局记忆索引（缺陷闭环/纪律/经验）     |
| IMA 知识库（通过 `ima-skill` 访问）                 | 设计笔记、移植记录、调试踩坑           |
| `.github/workflows/ci.yml`                            | CI 管道定义                            |

### 10.5 CI 管道

`.github/workflows/ci.yml` 定义 GitHub Actions 工作流，push/PR 到 `main` 和 `worktree-*` 分支时触发：

| 步骤 | 说明 |
|------|------|
| 安装依赖 | build-essential + qemu-user |
| 构建 meuos-sysroot | libmsys.a + mkmsys + msysctl |
| 构建 meuos-toolchain | 9 个工具的完整构建 |
| 工具链回归测试 | `make -C projects/meuos-toolchain check` |
| 构建 mcc | C99+C11+C23 编译器 |
| mcc 回归测试 | check + check-c99 + check-c11 + check-c23 |
| chibicc 社区测试 | check-chibicc（社区功能测试套件） |
| 构建 meow | 使用宿主 cc（CI 环境初始阶段） |
| 跨架构运行时 | riscv64 / aarch64 / i386 qemu-user（条件性） |
| 多目标汇编测试 | `meuos-toolchain` 多架构汇编验证 |
| 失败处理 | 自动上传测试日志到 artifacts |

---

