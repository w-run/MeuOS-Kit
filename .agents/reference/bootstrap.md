# 自举流程参考（.agents/reference/bootstrap.md）

> 从 AGENTS.md §3 下放（2026-08-04）。自举阶段与依赖顺序，按需读取。

## 3. 自举流程

### 3.1 组件间构建依赖

组件之间的构建依赖关系（从下往上依赖，下层必须先构建）：

```
meuos-buildtools (m4/bison/flex/gperf)
  ↑ 依赖 mcc + meuos-libc
meuos-utils / meuos-shell
  ↑ 依赖 mcc + meuos-libc + meow
meow（构建系统）
  ↑ 依赖 mcc + meuos-libc + meuos-toolchain（mt/as, mt/ld）
meuos-toolchain (as/ld/ar/ranlib/nm/objdump/readelf/strip/objcopy)
  ↑ 依赖 mcc + meuos-libc + meuos-sysroot（libmsys）
meuos-sysroot（.msys 格式：libmsys + mkmsys + msysctl）
  ↑ 依赖宿主 cc（Phase 0-1）或 mcc（Phase 4+）
meuos-libc（标准 C 库）
  ↑ 依赖 mcc 编译（Phase 2+）
mcc（编译器）
  ↑ 依赖宿主 cc（Phase 1）或自身（Phase 4 自举）
```

### 3.2 自举阶段

Agent 必须严格遵循以下阶段，每步都要验证：

**Phase 0 - 准备**

- 宿主编译器可用（gcc 或 tcc）。
- 设定 `MEUOS_SYSROOT` 环境变量指向目标根文件系统路径。

**Phase 1 - 诞生 mcc**

- 用宿主编译器编译 mcc 源码，产出第一代 `mcc` 二进制。
- 验证：`mcc` 能编译 `int main(){return 0;}` 并输出可执行文件。

**Phase 2 - 诞生 meuos-libc**

- 用 Phase 1 的 `mcc` 编译 meuos-libc（含 compat 层）。
- 安装到 `${MEUOS_SYSROOT}/lib` 和 `${MEUOS_SYSROOT}/include`。

**Phase 3 - 诞生 meow**

- 用 `mcc` + `meuos-libc` 编译 meow。
- 验证：`meow build` 能读取 YAML 配方并执行。

**Phase 4 - 自举验证**

- 用 sysroot 内的 `mcc` + `meow` 重新编译 mcc、meuos-libc、meow。
- 比较两次产物的行为一致性（功能等价即可，不要求 bit 级相同）。

**Phase 5 - 工具链完善**

- 用 `mcc` + `meuos-libc` 构建 `meuos-toolchain`（as/ld/ar/ranlib）。
- mcc driver 集成 mt 工具，消除对宿主 `cc` 的最后依赖。
- 验证：Kit 全程零宿主依赖。

**Phase 6 - 构建工具**

- 构建 `meuos-buildtools`（m4/bison/flex/gperf）。
- 验证：能用 meow + buildtools 构建需要 bison/flex 的软件包。

**Phase 7 - 用户空间**

- 构建 `meuos-utils`、`meuos-shell`。
- 验证：MeuOS Next 最小 sysroot 可运行、可交互。

---

