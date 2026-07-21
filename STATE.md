# MeuOS Kit — 当前状态与会话恢复

> **这是动态单一事实源。新会话恢复时，先读本文件，再按需读 AGENTS.md（项目规约）与各组件 `ARCHITECTURE.md`。每次工作结束应更新本文件的「最近变更」与「下一步」。**

> 更新时间：2026-07-22（i386 收口完成）

---

## 0. 会话快速恢复

1. **项目规约**：`AGENTS.md`（harness 自动加载，定义组件规范/自举流程/禁止事项）。
2. **当前状态**：本文件（你正在读）。
3. **组件设计**：`projects/<组件>/ARCHITECTURE.md`（mcc / meow / meuos-libc）。
4. **待实现项**：`projects/<组件>/.todo/*.md` 与 `env/.todo/*.md`（每个含背景/目标/影响范围/验收）。
5. **测试环境**：`env/`（QEMU 多架构 VM，见 `env/README.md` 与 `env/bin/qvm`）。
6. **先跑自检确认基线绿**（见第 5 节），再开始改动。

工作约定：改动后必须让对应 `make check` 通过；新增待实现功能先建目录 + `.todo`（markdown 描述）再实现；中文优先。

---

## 1. 总体阶段状态

| Phase             | 状态                  | 说明                                                                                                                                                    |
| ----------------- | --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0 准备            | PASS                  | 宿主 gcc，sysroot 就绪                                                                                                                                  |
| 1 诞生 mcc        | PASS                  | 宿主编译 mcc，C11 矩阵 + 各 target 汇编回归通过；mcc 可用 libc-meuos 自重编译                                                                           |
| 2 诞生 meuos-libc | PASS                  | 41+ 直接 syscall、C11 原子/线程/TLS、stdio/signal/setjmp/pthread；x86_64 完整，i386 bootstrap；多架构战略与 ABI 契约见 `projects/meuos-libc/PORTING.md` |
| 3 诞生 meow       | PASS                  | 原生 YAML 构建系统（依赖/变量/模式规则/-jN/自动变量）+ Makefile 兼容                                                                                    |
| 4 自举验证        | PASS（via env/ QEMU） | 原 Alpine 容器门禁已废弃，改由 `env/` QEMU 6.6.142 内核验证 mcc+libc-meuos 二进制可运行                                                                 |
| 5 LFS 包验证      | PASS                  | bzip2 1.0.8、binutils 2.42 libiberty 端到端通过 meow 构建                                                                                               |

自举链零 GNU/LLVM/glibc 代码（AGENTS.md §4 强约束）。

---

## 2. 各组件当前状态

### mcc（编译器，`projects/mcc/`）

- 源码级整合 cproc 前端 + QBE 后端，单体 `mcc`；AST→IR 直接构造（无文本 IR 中间步）。
- 支持架构：x86_64（主开发/运行验证）、aarch64、riscv64、loongarch64（汇编回归基线）、i386（**完整可用**：含 x87 浮点、Kl 分解、time64、va_list）。
- 结构（已优化）：`src/{driver,lex,parse,sema,irgen,ir,opt,abi,emit,util,target/<arch>}`；`src/lex/pp_expr.c` 与 `src/lex/pp_internal.h` 从 pp.c 拆出；driver 拆为 `main/target_select/host_toolchain/usage + driver_internal.h`；`include/util.h` 已加 include guard。
- **已知限制**（详见 `.todo`）：general-dynamic TLS、`-O` 级别控制、`-W` 诊断系统。

### meuos-libc（C 库，`projects/meuos-libc/`）

- 直接 Linux 内核 ABI 封装（不经宿主 libc）；x86_64/i386 runtime 完整（含 64 位 time_t/time64、跨函数 va_list、x87 浮点）。
- 41+ 独立 syscall 源（一文件一调用）；C11 `<stdatomic.h>` + `libatomic-meuos.a`；C11 线程（clone/futex）+ pthread 适配；signal/sigaction/sigsetjmp；最小 stdio（vfprintf/snprintf 共享格式化内核）；first-fit malloc。
- `meuos-libc-compat`（独立归档）：argp/error/obstack/getline/asprintf/funopen 等 GNU 扩展。
- **架构路线**：x86_64、aarch64、loongarch64、i386 为已确认基石；riscv64、armhf 为强烈建议新增；ppc64le/s390x 按需；armel/mips\* 明确跳过。此处“基石”是战略选型，不代表 runtime 已完成。
- **未完成**（见 `projects/meuos-libc/PORTING.md` 与 `.todo`）：aarch64/riscv64/loongarch64 完整 runtime、armhf runtime、纯原生链接器。

### meow（构建系统，`projects/meow/`）

- 原生 YAML 目标图构建器（取代 Make）；已从单文件 `meow.c` 拆为 `src/{state,exec,recipe,parse,graph,main}.c + meow.h`。
- 支持：variables/targets、deps/commands、inputs/outputs 增量、phony、`%` 模式规则、include、`-jN` 并行、自动变量 `$@/$</$^/$*`、Makefile 兼容模式。
- **未完成**（见 `.todo`）：用 meow 原生构建 Kit 自身、完整 DAG 去重、MeuOS 原生 shell。

### env（QEMU 测试环境，`env/`）

- 自建 QEMU 10.1.0（x86_64/i386/aarch64，**KVM+9p+TCG**），内核 Alpine `linux-virt-6.6.142`，Alpine minirootfs initramfs（3.4–4.0MB）。
- `env/bin/qvm` 管理器：`boot/console/run/stop/status <arch>`，9p 共享宿主 `env/share/` ↔ guest `/mnt/host`。
- 已验证：mcc+libc-meuos 静态二进制在 x86_64 VM 内运行通过（`counter = 2000` 等价闭环）。
- **未覆盖**：loongarch64 / riscv64（见 `env/.todo/`）。
- **下一步（Phase 6）**：qemu 改由 mcc+libc-meuos 自建（当前用宿主 gcc 过渡）；交接规格见 `env/QEMU_BOOTSTRAP.md`。核心挑战=glib2 硬依赖（zlib 易）。

---

## 3. 已知阻塞 / 跨组件限制

- git 仓库已就绪（`main` 分支，391 文件跟踪；大文件如 `reference/`、`env/build/`、`sysroot/` 均可重建不提交）。改动建议：建分支 -> 改 -> `make check` -> **同步更新本文件 §6** -> 提交。
- mcc 链接仍依赖宿主 `cc`/`ld`（`src/driver/host_toolchain.c`）；纯原生链接器待实现。
- 完整独立 MeuOS userspace 尚未完成（非 x86_64 runtime、原生 shell）。

---

## 4. 下一步优先级

1. **P1** ✅ 收口 i386（已完成）：x87 浮点、Kl 分解 push/pop 修复、64 位 time_t/time64、跨函数 va_list；`make -C projects/mcc check-i386-runtime` 全绿（counter=2000、浮点、time64、stat、va_list）。
2. **P2** 补齐 aarch64 meuos-libc runtime + QEMU Phase-2 运行回归（`projects/meuos-libc/src/arch/aarch64/.todo`）。
3. **P3** 补齐 loongarch64 runtime，按最新 ABI/UAPI 建立独立 syscall、TLS、原子和信号门禁。
4. **P4** 补齐 riscv64 runtime，用于验证架构抽象并建立第三条 64 位完整链。
5. **P5** 建立 armhf TODO、交叉汇编与 QEMU 门禁；与 32 位 time64 方案一起落地。
6. **P6** 用 meow 原生构建 Kit 自身（`projects/meow/.todo/native-kit-build.md`）。
7. **P7** 实现 `-O` 级别控制与 `-W` 诊断系统（`projects/mcc/.todo/`）。
8. **P8** QEMU 自举：让 `qemu-system-*` 由 mcc+libc-meuos+meow 构建（见 `env/QEMU_BOOTSTRAP.md`；前置=glib2 移植、zlib 移植）。
9. **P9**（架构储备，进行中）mcc/m++ 共享后端架构调整：把 mcc 的 `ir/opt/abi/emit/target` 抽出为 `libmcc` 库，使未来 `m++`（C++ 前端）可复用后端（见 `projects/mcc/.todo/cpp-shared-backend.md`）。**阶段 A 已完成**（refactor/libmcc-split 分支）：Makefile 拆分 FE/BE 源，BE 打成 `build/libmcc.a`，mcc = FE .o + libmcc.a，全绿 check + 自举链未破。下一步阶段 B：抽 `projects/libmcc/include/` 公共 API。
10. **P10**（工具链自研，规划中）**meuos-toolchain**（简称 mt）：单项目整套提供汇编器 `as`、链接器 `ld`、归档器 `ar`、二进制工具 `nm`/`objdump`/`readelf`/`strip`/`objcopy`（无 m- 前缀，MeuOS 里是唯一工具）。解除 mcc 对宿主 `cc`/`as`/`ld`/`ar` 的最后依赖，完成 Kit 自举链。组织：单项目 `projects/meuos-toolchain/` + 内部 `src/libelf/` 共享库（不对外暴露，区别于跨二进制共享的 libmcc）。优先级：libelf+ar (P0) → as x86_64 (P1) → ld x86_64 静态 (P2) → 多架构扩展 (P3-6) → 动态链接 (P7) → 辅助工具 (P8)。详见 `projects/.todo/meuos-toolchain.md`。

---

## 5. 验证命令（快速自检）

```sh
# 三个核心组件（路径在 projects/ 下）
make -C projects/mcc check
make -C projects/mcc check-c11 check-driver check-targets check-i386 check-i386-runtime check-loongarch64
make -C projects/meuos-libc check
make -C projects/meow check

# 跨组件自举门禁（mcc 用 libc-meuos 自重编译）
make -C projects/mcc check-sysroot-static
make -C projects/meow check-sysroot-static

# QEMU 测试环境
env/bin/qvm status
env/bin/qvm boot x86_64 && env/bin/qvm run x86_64 'uname -r' && env/bin/qvm stop x86_64

# 全流程自举
./bootstrap.sh
```

---

## 6. 最近变更

> **每次变更（含 git 操作）后必须更新本节，并据实修订 §1–§5。**

- **2026-07-22**：i386 收口完成——`projects/mcc/src/target/i386/i386_emit.c` 所有 Kl 操作（Ocopy/Oload/Ostorel/Oshl/Oshr/Osar/Oadd/Osub/Oneg/Oand/Oor/Oxor/Oxcmp/Oxtest/Oextsw/Oextuw/Oxsel）统一加 push/pop EAX（shifts 还加 EDX）保护，消除 rega 盲区导致的 EAX clobber（修复 `s.mode` 被覆盖、`mode=0` bug）；Oload 的 ECX stash 与 Ostorel 的 vreg=ECX 也加 push/pop ECX 保护。新增 i386 运行时回归测试套件（`test/i386/runtime_{kl,fp,time64,va}.c` + `runtime.sh` + Makefile `check-i386-runtime` target）。所有 i386 综合测试通过（counter=2000、浮点、time64、stat、va_list），i386 ELF32 静态二进制在 x86_64 内核原生运行验证通过。STATE.md §2/§3/§4/§5 同步更新。
- **2026-07-22**：aarch64 移植进度保存——7 个运行时文件已就位（`crt/aarch64/crt1.S`、`src/internal/arch/aarch64/syscall.S`、`src/arch/aarch64/{atomic,setjmp,sigreturn,thread_clone,set_tls}.S` + `tls.c`，均可用 `aarch64-linux-gnu-gcc -c` 汇编验证），但 `tls.c` 被 mcc aarch64 后端 bug 阻塞（存储 64 位值到全局变量触发 `dying: invalid class`）。诊断与修复方案已记录到 `projects/mcc/.todo/aarch64-store-fix.md`（omap 存储条目 cls=Kw 应改 Ki/Ks/Kd + isel 缺少 store 特殊处理）；任务 8-11 待办清单已更新到 `projects/meuos-libc/src/arch/aarch64/.todo`。新增架构储备项 P9：mcc/m++ 共享后端 `libmcc` 化（见 `projects/mcc/.todo/cpp-shared-backend.md`），为未来 C++ 前端铺路。后续会话从 .todo 恢复即可继续。
- **2026-07-22**：libmcc 化阶段 A 完成（分支 `refactor/libmcc-split`）——`projects/mcc/Makefile` 拆分 FE/BE 源：FE_DIRS=`src/{driver,lex,parse,sema,irgen}`（39 .o），BE_DIRS=`src/{ir,opt,abi,emit,target,util}`（41 .o）。BE .o 打成 `build/libmcc.a`（2.35MB），mcc 二进制 = FE .o + libmcc.a（1.76MB）。全绿验证：`make check` / `check-c11` / `check-driver` / `check-targets` / `check-i386` / `check-i386-runtime` / `check-loongarch64` / `check-abi` / `check-sysroot-static`（mcc 自重编译 mcc 通过）。行为零变化，为未来 m++ 共享后端铺路。
- **2026-07-22**：工具链自研规划落地（`projects/.todo/meuos-toolchain.md`）——确认 mcc 通过 `host_toolchain.c` 把汇编/链接外包给宿主 `cc`，Makefile 用宿主 `ar`，这是 Kit 自举链的最后外部依赖。决策：**单项目整套提供**，项目名 `meuos-toolchain`（简称 mt），二进制无 m- 前缀（`as`/`ld`/`ar`/`nm`/`objdump`/`readelf`/`strip`/`objcopy`，MeuOS 环境里是唯一工具）。组织：单项目 `projects/meuos-toolchain/` + 内部 `src/libelf/` 共享库（不对外暴露）。优先级：libelf+ar (P0) → as x86_64 (P1) → ld x86_64 静态 (P2) → 多架构 (P3-6) → 动态链接 (P7) → 辅助 (P8)。STATE.md §4 增 P10。
- **2026-07-21**：新增 `projects/meuos-libc/PORTING.md`，记录 x86_64/aarch64/loongarch64/i386 基石状态、riscv64/armhf 推荐新增、ppc64le/s390x 按需以及 armel/mips\* 排除策略；补充 syscall/TLS/原子/启动 ABI、32 位统一 time64 和跨架构验收清单。新增 `32bit-time64.md` 与 armhf runtime TODO。
- **2026-07-21**：env/ qemu 重建为 **KVM+9p**（`--enable-kvm`）；新增 `env/MEUOS2026.md`（给 MeuOS 2026 构建 VM 交接文档）+ `env/bin/qemu-path`。修复 MeuOS 2026 `run-vm.sh` 的 9p 模式（RHEL qemu-kvm 缺 9p）；现可用 `QEMU_BIN=$(env/bin/qemu-path) run-vm.sh` 跑 KVM+9p 构建 VM。KVM+9p 协同已验证。
- **2026-07-21**：新增 `env/QEMU_BOOTSTRAP.md`--QEMU 自举交接文档（供移植 Agent 阅读）：列明 qemu 源/配置/夹具提供方式、env 工具（qvm/build-initramfs.sh）可移植性、glib2 硬依赖挑战与策略、6 个里程碑与验收清单。STATE.md §4 增 P6。
- **2026-07-21**：初始化 git 仓库（`main` 分支，初始提交 81d4532 + 391 文件）；完善 `.gitignore`（忽略 mcc 二进制 / env 大文件 / share 工作目录）与 `.gitattributes`（二进制标记）；AGENTS.md 新增 §7「实现策略与参考资源」--指引 agent 优先参考 musl/cproc/QBE/tinycc 等社区实现以节省算力。
- **2026-07-21**：清理冗余文档（删除 `-MP` 垃圾文件 + 4 个 `PROGRESS.md`，合并为 `STATE.md`）；修正 `bootstrap.sh` Phase 4 对已删除 `experiments/` 的引用；精简 `README.md`。
- **2026-07-21**：搭建 `env/` QEMU 测试环境（自建 qemu 10.1.0 三 target + 9p，内核 6.6.142，三架构 boot/run/9p 验证通过）。
- **2026-07-20**：三项目代码结构优化——meow 单文件拆分、mcc pp.c/main.c 拆分、各组件 ARCHITECTURE.md + .todo 体系、修复 4 个阻断自举的预存缺陷。
