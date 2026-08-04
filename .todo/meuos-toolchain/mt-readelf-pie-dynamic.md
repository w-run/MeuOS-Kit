# mt/readelf -d 对 PIE 文件误报 "no dynamic section"

> 状态：🔄 开放（pre-existing，2026-08-04 由 reviewer-auditor 在 rtld P0 验收中发现）
> 分支参考：tmp/rtld-p0（HEAD=f88ae83）
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
