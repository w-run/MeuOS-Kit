# mt/readelf -d 对 PIE 文件误报 "no dynamic section"

> 状态：✅ 完成（2026-08-04 exec-toolchain-lite）
> 分支参考：tmp/exec-toolchain/mt-readelf-pie（HEAD=49349b2）
> 关联 commit：153be27 / 3f2c354 / 82a4b40 / f88ae83（与本任务无关，仅是 P0 闭环期间复现）

## 现象

`mt/readelf -d /tmp/.../hello.pie` 输出：
```
There is no dynamic section in this file.
```

但实际：
- `mt/readelf -S` 能看到 `.dynamic` 段
- `mt/readelf -x .dynamic` 能 hex dump 段内容（9 个 DT 条目完整）
- GNU readelf -d 能正确解析 9 个 DT 条目（SYMTAB/SYMENT/STRTAB/STRSZ/HASH/RELA/RELASZ/RELAENT/NULL）

## 影响

- 仅 mt/readelf 自身的限制，**不影响 ld.so 实际运行**（独立 e2e 实跑 hello 返回 42）。
- 不影响本次 P0 闭环：rtld P0 4 切片门禁（make check 含 check-rtld）全 PASS。
- 用户/脚本若依赖 `mt/readelf -d` 解析 PIE 输出，会被误判为非动态链接文件。

## 根因定位（待 exec-toolchain 实施时核实）

- `src/readelf/main.c` 的 `-d` 处理路径未读取 PT_DYNAMIC program header，而只查 .dynamic section 段头表；
- 或在解析段头时对 ET_DYN 类型应用了与 ET_EXEC 不同的过滤。

## 验收

- `mt/readelf -d /tmp/.../hello.pie` 输出 SYMTAB/SYMENT/STRTAB/STRSZ/HASH/RELA/RELASZ/RELAENT/NULL 9 条；
- `mt/readelf -d /tmp/.../hello.exec` 既有静态二进制仍正常；
- `make -C projects/meuos-toolchain check` 不引入新 warning；
- 静态/动态/PIE 三种 ET_* 都能被 -d 正确解析。

## 范围约束

- 仅 src/readelf/* 与 Makefile；
- 不动 src/ld/* / src/rtld/* / src/as/* / src/libelf/*；
- 不引入新 warning（-Werror）；
- 文件级 git commit `mt: readelf: fix -d parsing for PIE files`。

## 修复记录（2026-08-04 exec-toolchain-lite）

- **commit**：`49349b2`（分支 `tmp/exec-toolchain/mt-readelf-pie`）
- **关键 diff**：
  - `dump_dynamic` 改为**优先读取 `PT_DYNAMIC` program header**（而非只查 .dynamic section 段头表），适配 ET_DYN / PIE；
  - 对 `.dynamic` 节区 `sh_type=PROGBITS`（mt/ld 端生成）**回退兼容**：不因 sh_type≠SHT_DYNAMIC 而拒绝解析；
  - strtab **按名查 `.dynstr`**，而非依赖 `sh_link` 到 `.dynstr` 的索引（mt/ld 端 sh_link=0）。
- **3 类样本验证矩阵**：
  - 静态（ET_EXEC 静态）：`-d` 正常输出 / 无动态段继续提示；
  - 传统动态 ET_EXEC：9 条 DT 条目正确；
  - PIE（ET_DYN）：GNU readelf 对齐，9 条 DT 条目完整不再误报。
- **commit message**：`mt: readelf: fix -d parsing for PIE files`
