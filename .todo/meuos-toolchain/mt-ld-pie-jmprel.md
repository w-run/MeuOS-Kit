# mt/ld 构建的 PIE 主程序 JUMP_SLOT 未入 .rela.plt（DT_JMPREL）致 rtld_dlopen 崩

> 状态：🔄 开放（2026-08-04/05 exec-toolchain-gp 方案 a 交付后验证遗留）
> 关联 commit：`c52d807`（方案 a，rtld notify libc on TLS module）、`7ecb1ae`（mt/ld 已为 PC32 UND 函数收集 JUMP_SLOT）

## 现象

- mt/ld 全链 `main_dl`（PIE 主程序调用 `rtld_dlopen`）运行仍崩：**dlopen 调用跳 base 0x555555154000、GOT 槽=0**；
- 根因：mt/ld 生成的 `main_dl` **JUMP_SLOT 在 `.rela.dyn`（DT_RELA，无 DT_JMPREL）**，rtld 应用 main_dl 的 `dlopen`/`dlsym` JUMP_SLOT 时 **GOT 槽填 0**；
- `7ecb1ae` 已让 mt/ld 为 mcc PC32 收集 JUMP_SLOT，但 **.plt stub 跳转仍错**。

## 判定

- **独立 mt/ld 缺陷**（JUMP_SLOT 段归属 / stub 填址）；
- **host-ld 链可绕过**（link 产物正确，仅 mt/ld 自身链接路径崩）；
- **不影响 P0.3 方案 a 主线程闭环**：exec-integration / exec-libc 用 **host-ld 链**验收，故不阻塞方案 a。

## 范围

- **mt/ld**：生成 PIE 主程序 `.dynamic` 的 **JMPREL 段**——`JUMP_SLOT` 应入 `.rela.plt`（`DT_JMPREL`）而非 `.rela.dyn`（`DT_RELA`）；并正确填写 .plt stub 跳转目标；
- **rtld**：对 `DT_RELA`（.rela.dyn）里的 `JUMP_SLOT` 的应用/解析。

## 验收

- mt/ld 构建的 PIE 主程序调 `rtld_dlopen` 运行**正常**（GOT 槽正确填写、不再跳 base）；
- `readelf -d` 含 **`DT_JMPREL` / `.rela.plt`**；
- rtld 解析 `main_dl` 的 JUMP_SLOT 并正确填 GOT；
- 不引入其它门禁/架构回归。

## 范围约束

- 由 exec-toolchain（mt/ld JMPREL 段 + .plt stub + rtld 解析）修复；doc-pm 只登记与追踪；
- 修复后经验沉淀到 `.agents/knowledge/`。
