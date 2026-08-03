# utils-impl-template — 每工具实现的通用模板

> **任务 ID 范围**: `utils-{toolname}`
> **状态**: 📌 流程文档，每次新增工具按此模式

## 1. 标准实现流程

每个 meuos-utils 工具都按此模板实现：

### 第 1 步：创建 .c 文件

```sh
cat > projects/meuos-utils/src/utils/<tool>.c <<'EOF'
/* <tool> — <one-line description> + POSIX/GNU 选项说明 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "meuos/utils.h"

static void usage(void) {
    fprintf(stdout, "Usage: %s [OPTION]... [ARG]...\n", program_name);
    /* ... POSIX + GNU 主要选项说明 ... */
    exit(0);
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    /* 长选项数组 */
    /* opts 解析 while 循环 */
    /* 主体实现 */
    return 0;
}
EOF
```

### 第 2 步：添加 check 用例到 Makefile

在 `Makefile check` 末尾追加：

```makefile
	@out=$$(./$(BUILD)/<tool> <args>); \
	 [ "$$out" = "<expected>" ] && echo "<tool>: PASS" || (echo "<tool>: FAIL got=[$$out]"; exit 1)
```

### 第 3 步：更新 ARCHITECTURE.md 状态表

`meuos-utils/ARCHITECTURE.md §4 当前能力` 表更新。

### 第 4 步：单独 .todo 文件

每个工具的具体细节（POSIX 边界 / GNU 扩展 / 实现笔记）写到
`meuos-utils/.todo/utils-<toolname>.md`。骨架工作量大时，先合并多工具写一个
但仍按工具分节（不允许超大 todo 文件）。

## 2. 优先级选择准则

P1 必做：POSIX 必要工具。MeuOS 系统运行最少所需的：
- 路径遍历：ls find
- 文件 IO：cat echo
- 文件操作：cp mv rm mkdir rmdir ln touch
- 文本基础：head tail wc
- 脚本需要：true false test [

P2 常用：日常使用频次高
- chmod chown df du
- sort uniq cut tr tee dd
- grep sed

P3 不必需但不实现有缺憾
- awk（可用 meow build 装 gawk 兜底）
- patch tar（机制复杂，可 P5 提前）

## 3. 公共工具要求

每个工具必须：

1. **接受 `--help`** — 输出用法到 stdout
2. **接受 `--version`** — 输出 version 信息
3. **接受 `-h` `-V` 短选项**（GNU 风格）
4. **0 长度参数数组 OK**（如 `true`/`false` 不依赖 argc）
5. **失败 exit 非 0**（POSIX 要求）
6. **错误消息包含 program_name**（如 `cat: foo.txt: No such file or directory`）
7. **链接 libutils.a**（`die()`/`xmalloc()`/set_program_name()）

无 libutils.a 的常见替换：

| 工具 | 需要的替代品 |
|------|---------------|
| 不需 EXIT_HELP 的工具 | `die()` 错误退出 |
| 不需 OOM 检查的工具 | `malloc()` + 检查 NULL |
| 不需 GNU 长选项的工具 | 用 `getopt()` 短选项即可 |

## 4. 完成标志

每工具完成需满足：

- [ ] 实现源文件齐全
- [ ] `make check` 新增至少 1 项 PASS
- [ ] 与 GNU 对应物对比 PASS
- [ ] ARCHITECTURE.md 状态更新
- [ ] INDEX.md 任务勾选
- [ ] git commit 提交到 worktree-shell-utils
