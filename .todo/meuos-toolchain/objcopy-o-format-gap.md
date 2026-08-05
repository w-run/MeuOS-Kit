# mt/objcopy 缺 `-O <format>` 输出格式转换

> 状态：✅ 已闭环（2026-08-05）
> 关联：commit 5b621b3（-O binary/ihex/srec，合入 main a28187b），ihex 校验和修，与 GNU 逐字节对齐
> 跟进验证见 `.agents/knowledge/project_mcc_toolchain.md`（-O 折叠 + GNU 对齐经验）

## 现象

- `mt/objcopy -O ihex add.o add.ihex` → `objcopy: unknown option: -O`，exit 2；
- `mt/objcopy --help` 只暴露 ELF 节区操作集：`-S/-g/-R/--keep-section/-j/--only-section/--add-section/--rename-section/--set-section-flags/--dump-section/-o/-v/-V/-h`；
- **不含** GNU binutils objcopy 的 `-O <bfdname>`（输出到 ihex/srec/binary/elf32 等非默认格式）能力。

## 判定

- **功能空白，非回归/非缺陷**：现有 ELF 节区操作（-j/--dump-section/--add-section/-S）经真实验证全部工作正常；
- 缺的是 **bfd 输出格式转换**层（ihex/srec/plain binary），这与 mt 自研 libelf（仅 ELF 格式，非 bfd）的架构定位一致；
- 若某待办/文档标注了 objcopy 支持 `-O binary`/ihex 烧录格式，则与实现不符（此空白）。

## 影响

- 低优先级：不阻断汇编→链接→解析→归档核心链；影响的是需要把 ELF 转成烧录/内存镜像格式的场景（嵌入式/内核 image）。

## 范围

- mt/objcopy 增加 `-O <format>`，支持 ihex / srec / plain binary 输出（若非 libelf 架构决定走 `--dump-section` 拼接）；或明确定位为"不支持，用 --dump-section 代替"。

## 验收

- 依决策：要么 `-O ihex`/`-O binary` 可用（输出与 GNU objcopy 对齐）；要么文档明示不支持并提供替代路径。

## 范围约束

- 由 exec-toolchain（mt/objcopy）决策并实施；doc-pm 只登记追踪。
