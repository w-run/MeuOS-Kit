# utils-cat-rendering — 完善 cat 的 -E/-T/-v/-A 渲染

> **任务 ID**: `utils-cat-rendering`
> **范围**: `projects/meuos-utils/src/utils/cat.c`
> **状态**: ⏳ 待启动（骨架后第一阶段补全）

## 1. 现状

骨架 cat 接收了所有选项标志（`-A` `-b` `-e` `-E` `-n` `-s` `-t` `-T` `-v`），设置了对应的 static int 标志位，但仅 `show_ends/show_tabs/show_nonprint` 在 main() 末尾被 `(void)X` 抑制，实际输出时**未应用**。

## 2. 任务

实现按 GNU cat 行为的字符级渲染，逐字符处理：

```c
for each byte b in buf:
    if (show_nonprint || show_tabs):
        if b == '\t' && show_tabs:          → 输出 "^I"
        elif b == '\n':                      → 输出 "\n"（show_ends 时后接 "$"）
        elif b < 0x20 || b == 0x7f:          → 控制字符 ^X（X = b + 64）
        elif b >= 0x80:                      → M-X 标记 (UTF-8 单字节近似处理)
        else:                                → 原样输出
    else:
        原样输出
```

详细规则见 GNU coreutils `cat.c` 的 `cat()` 主循环（简化即可，不复制源码）。

## 3. 验收

```sh
printf 'a\tb\nc\x01d' > /tmp/cat-input
./build/cat -A /tmp/cat-input
# 期望输出：a^Ib\nc^A$d （v 选项渲染控制字符，E 渲染行尾 $）
# 与 GNU cat -A 输出对照

./build/cat -T /tmp/cat-input
# 期望：a^Ib\nc\x01d （仅 Tab 渲染为 ^I）

./build/cat -E /tmp/cat-input
# 期望：a\tb$\nc\x01d$ （每行尾加 $）
```

阶段档案：

1. `make -C projects/meuos-utils check` 通过
2. 与 GNU cat 在 `/usr/bin/cat` 行为对比：相同输入产生相同输出
3. 测试加进 `Makefile check`：输出检查项

## 4. 依赖

- 当前 cat 已能基本 IO（依赖本骨架阶段）
- `libutils.a` 已能用（依赖本骨架阶段）

## 5. 时间估算

- 实现阶段：2-3 小时
- 对照 GNU 测试 + 集成：1 小时
- 总计：约 4 小时
