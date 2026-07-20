# QEMU 自举交接文档（供负责移植的 Agent 阅读）

> 目标：让 `qemu-system-{x86_64,i386,aarch64}`（TCG-only、headless、+9p）
> 能用 **MeuOS Kit 自身**（`mcc` + `libc-meuos.a` + `meow`）从源码构建出来，
> 使测试环境本身进入 MeuOS 自举链（Phase 6）。
>
> 本文档面向"对方 Agent"：即接手 QEMU 移植任务的会话。先读
> [`../STATE.md`](../STATE.md) 与 [`../AGENTS.md`](../AGENTS.md)（§4 禁止事项、
> §7 参考策略），再读本文件。

---

## 1. 当前状态（过渡期）

`env/build/qemu-install/bin/qemu-system-*` 目前用 **宿主 gcc + 宿主 glib2/pixman**
构建（见 `env/build/build-qemu.sh`），仅作测试过渡。它能跑（三架构 boot + 9p
+ mcc 二进制运行均验证通过），但**不是自建的**，不满足 AGENTS.md §1.3
"Kit 自身可在 MeuOS 环境中自我重建"。

需要你做的：把这条构建链从"宿主 gcc"切换到"mcc + libc-meuos"。

---

## 2. 提供给你的资源

### 2.1 QEMU 源码
- 本地：`env/build/qemu-10.1.0.tar.xz`（142MB，已验证完整）。
- 上游：`https://download.qemu.org/qemu-10.1.0.tar.xz`。
- 已抽取的参考构建配置：`env/build/build-qemu.sh`（精确的 `--disable` 标志集合）。

### 2.2 env 工具（可直接复用，见 §3 可移植性）
| 文件 | 作用 | 语言 |
|------|------|------|
| `env/bin/qvm` | VM 启动/连接/停止/运行（串口 socket + socat + 9p） | bash |
| `env/bin/build-initramfs.sh` | 用 Alpine minirootfs + 9p 模块组装 initramfs | bash |
| `env/README.md` | env 设计与使用说明 | 文档 |

### 2.3 测试夹具（外部下载，**非** MeuOS 构建）
这些是"被测对象"的运行环境，本身不需要用 MeuOS 重建（如同测试一个编译器不需要用该编译器重建内核）：
- **内核**：Alpine `linux-virt-6.6.142`（LTS，VM 优化）--`env/kernels/<arch>/vmlinuz` + `config`。来源 `dl-cdn.alpinelinux.org/alpine/v3.20/main/<arch>/linux-virt-6.6.142-r0.apk`。
- **rootfs**：Alpine `minirootfs-3.20.9`（busybox+musl+apk 基础）--`env/rootfs/minirootfs-<arch>.tar.gz`。

> 长远看，内核与 busybox 也可纳入 MeuOS 自建（内核用 mcc 交叉编译、busybox 用 mcc 编译），
> 但那是独立的大任务，不阻塞 QEMU 自举。

### 2.4 验证基线
当前过渡版 qemu 已通过的验收（你的自建版须同样通过）：
```
env/bin/qvm boot x86_64 && env/bin/qvm run x86_64 'uname -r'   # -> 6.6.142-0-virt
env/bin/qvm run x86_64 'cat /mnt/host/<file>'                  # 9p 共享读
# mcc+libc-meuos 静态二进制在 VM 内运行（见 STATE.md §5 集成测试）
```
三架构（x86_64 / i386 / aarch64）均需通过。

---

## 3. 附带工具是否可提供？--可以

| 工具 | 可否提供 | 说明 |
|------|---------|------|
| `qvm` | ✅ 原样可用 | 纯 bash，依赖 `socat` + qemu 二进制在 PATH；不绑定具体 qemu 构建方式 |
| `build-initramfs.sh` | ✅ 原样可用 | 纯 bash + `cpio`/`gzip`/`tar`；initramfs 内容是 Alpine busybox（夹具） |
| 自建 qemu 二进制 | ❗需你产出 | 当前是宿主 gcc 产物，需替换为 mcc 产物 |
| 内核 / minirootfs | ✅ 作为夹具提供 | 外部 Alpine 资源，不要求 MeuOS 重建 |

**结论**：env 的"管理/连接"层（qvm、build-initramfs.sh）完全可移植、与 qemu 的构建方式解耦，可直接交给你的环境使用。你只需替换 `env/build/qemu-install/bin/qemu-system-*` 这三个二进制为自建版，qvm 即自动改用它们（qvm 按 `QEMU_PREFIX/bin/qemu-system-<arch>` 解析）。

---

## 4. 核心挑战：依赖

QEMU 10.1.0 的 meson.build 声明的**硬依赖**（`required: true`）：

| 依赖 | 状态 | 移植难度 |
|------|------|---------|
| **glib-2.0** + gmodule | 必需，不可禁用 | **高** --glib2 庞大且重度依赖 glibc 扩展 |
| **zlib** | 必需 | **低** --小、干净的 C，mcc 可直接编译（参考 musl 模式） |
| pixman | 可选（`--disable-pixman`，headless 不需要） | 跳过 |
| meson ≥1.5 / ninja | **构建期**编排工具（类比 make） | 宿主提供即可（见 §5） |

其余（gnutls/slirp/spice/gtk/sdl/vnc/...）全部可 `--disable`。

### 4.1 glib2 是关键工作量
glib2 约 50 万行，重度使用 glibc 专有特性。三条路线（按推荐度）：

1. **写最小 `meuos-glib` shim**（推荐起步）：审计 QEMU 实际用到的 glib API 面
   （`GMainLoop`/`GHashTable`/`GArray`/`GString`/`GList`/`GSList`/`g_malloc`/
   `GQuark`/`GError`/`GOptionContext` 等），用 musl 风格的干净 C 重实现这一子集。
   - 用 `nm -u qemu-system-x86_64 | grep g_` 或 grep qemu 源里的 `g_*` 调用审计范围。
   - 参考 `reference/musl/` 的哈希/分配器算法（AGENTS.md §7）。
2. **移植真 glib2 子集**：取 glib2 源，裁剪到 qemu 需要的子集，逐文件修掉 glibc-isms 让 mcc 能编译。工作量大但路径清晰。
3. **既有最小 glib 替代**：参考嵌入式项目的 glib shim（许可需 MIT/ISC 兼容），但多数不完整。

**建议**：先做路线 1 的审计，量化 API 面后再决定 1 vs 2。

### 4.2 zlib
直接 `mcc -c` zlib 源（`adler32.c`/`crc32.c`/`inflate.c`/...）产出 `libz-meuos.a`。
zlib 已在 LFS Phase 5 验证过类似流程（见 STATE.md），低风险。

---

## 5. 构建策略

### 最小配置（参考 `env/build/build-qemu.sh`）
```
--target-list=x86_64-softmmu,i386-softmmu,aarch64-softmmu
--enable-tcg --disable-kvm           # TCG 纯软件仿真（跨架构必需）
--enable-virtfs                       # 9p（qvm 的 /mnt/host 依赖）
--disable-pixman --disable-vnc --disable-gtk --disable-sdl --disable-cocoa
--disable-spice --disable-opengl --disable-virglrenderer --disable-bochs
--disable-docs --disable-tools --disable-guest-agent --disable-werror
```

### meson/ninja 的定位
meson + ninja 是 qemu 的**构建编排器**（生成构建图、解析依赖），类比 `make`。
按 AGENTS.md §4，Kit 自身用 Makefile/shell，但**包装**外部构建系统可以。
所以：meson/ninja 可由宿主提供（作为构建工具，不进入运行时），产物用 mcc 编译链接。
最终理想是 meow 直接驱动 qemu 的编译单元（绕过 meson），但这可分阶段：
- **阶段 A**：meson 生成构建描述，但 `CC=mcc`、`CFLAGS=...--specs=meuos`、链接 `libc-meuos.a + libglib-meuos.a + libz-meuos.a`。
- **阶段 B**：meow 原生驱动（写 `pkgs/qemu/meow.yaml`，逐步替代 meson）。

### 链接形态
```
mcc --specs=meuos --sysroot=<sysroot> --nostdlib --static \
  -o qemu-system-x86_64 <qemu objs> \
  -l:libc-meuos.a -l:libglib-meuos.a -l:libz-meuos.a
```
目标：静态链接、纯 libc-meuos，可在 `env/` QEMU 内运行（自举闭环）。

---

## 6. 里程碑建议

1. **M1**：zlib 用 mcc 构建出 `libz-meuos.a` 并通过 zlib 自测。
2. **M2**：审计 qemu 的 glib API 面（`nm -u` + 源码 grep），输出清单。
3. **M3**：`meuos-glib` shim 实现审计到的 API 子集，单元测试。
4. **M4**：单 target（x86_64-softmmu）用 mcc 构建出能 boot 内核 + 串口的 qemu。
5. **M5**：三 target + 9p，过 §2.4 全部验收。
6. **M6**：meow 原生驱动（`pkgs/qemu/meow.yaml`），纳入 bootstrap.sh。

---

## 7. 参考资源（AGENTS.md §7）

- `reference/musl/`：哈希表/分配器/字符串算法（glib shim 可参考其干净实现风格）。
- `reference/qbe/`、`reference/cproc/`：mcc 自身如何源码级整合参考树。
- glib2 源（`https://gitlab.gnome.org/GNOME/glib`）：路线 2 的裁剪输入。
- qemu 文档：`docs/devel/`（内部架构）、`docs/about/build-platforms.rst`。
- SysV psABI / AAPCS / RISC-V calling convention：各 arch 代码生成正确性参考。

---

## 8. 验收清单（自建 qemu 须通过）

- [ ] `env/build/qemu-install/bin/qemu-system-{x86_64,i386,aarch64}` 存在且 `--version` 报 10.1.0。
- [ ] `nm -u` 不含宿主 glibc 符号（仅 libc-meuos/libglib-meuos/libz-meuos）。
- [ ] `env/bin/qvm boot <arch>` + `run '<cmd>'` 三架构均通过（见 §2.4）。
- [ ] mcc+libc-meuos 静态二进制能在自建 qemu 的 x86_64 VM 内运行。
- [ ] 9p 共享（`/mnt/host`）可读写。
- [ ] 更新 `../STATE.md` §1（Phase 6 PASS）与 §6 最近变更。
