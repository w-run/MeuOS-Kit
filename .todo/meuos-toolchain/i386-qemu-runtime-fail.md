# i386 QEMU runtime FAIL：产物返回 0 非 42

> 状态：🔄 开放（2026-08-05 qemu_c_hello.sh set -e 修复后首次真实暴露）
> 关联：commit f5af14b（修复 set -e 使脚本能正确报错后才暴露，非该修复引入）

## 现象

- `make check-qemu-i386`：mcc→as→ld 三个阶段 **PASS**，但 **QEMU runtime 返回 0（期望 42）** → 脚本报 `FAIL (exit=0, expected 42)`，Makefile 用 `⚠️ SKIP (partial failure)` 包为非阻塞；
- 被测程序 `test/hello.c`（`main` 直接 `mov $42,%rax; ret`，无 libc 符号依赖），同文件在 x86_64/aarch64/riscv64/loongarch64 均返回 42 正常，**仅 i386 异常**。

## 判定

- **真实现象，非脚本误报**（set -e 修复后脚本已能正确走到 PASS/FAIL 判断）；
- i386 mcc 交叉编译、mt/as 组装、mt/ld 链接**均成功**（产物是正确 arch 的 ELF），问题在**运行期**：要么 i386 的 crt1.o/libc-meuos 组合下入口/返回约定不对，要么 mt/ld 对 i386 生成的返回路径有偏差；
- 疑似根因方向：i386 ABI（cdecl return / 栈对齐 / crt1 入口交递）与 x86_64 后端差异，或 sysroot/i386 的 crt1.o 对 `main` 返回值传递有缺。

## 影响

- i386 跨架构 e2e（C 程序全管线验证）不能作为门禁 PASS，仅能 SKIP 非阻塞；
- 不阻断 i386 其它已验证能力（as/ld 门禁、静态链接等），仅 QEMU 运行期 hello C 链。

## 范围

- 优先 **mt/ld i386 端**：核对 ELF32 EXEC 的入口/返回路径、crt1.o 与 main 返回值传递；
- 否则 **sysroot/i386** crt1.o / libc-meuos 组合；
- 可先在 i386 qemu 下单独跑 `crt1.o` 纯返回用例定位。

## 验收

- `make check-qemu-i386` 的 QEMU runtime 变为 `PASS (exit=42)` / `C hello: PASS`（不再 FAIL/SKIP）；
- 不引入 x86_64 及其它架构回归。

## 范围约束

- 由 exec-toolchain（mt/ld i386 端）或 exec-libc/sysroot（i386 crt1）修复；doc-pm 只登记追踪；
- 修复后经验沉淀到 `.agents/knowledge/`，本待办删除。
