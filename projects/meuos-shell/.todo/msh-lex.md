# msh-lex — 词法分析器

> **任务 ID**: `msh-lex`
> **范围**: `projects/meuos-shell/src/lex/lex.c` + `include/msh/lex.h`
> **状态**: ⏳ 待启动（P6 第一步）

## 1. 目标

实现 POSIX.1-2008 Shell 输入语言的词法分词器。

## 2. POSIX sh Token 类型

参考 dash 1.0 词法（不复制源码）：

| Token 类型 | 说明 |
|------------|------|
| `TOK_WORD` | 普通 word（未被引号包裹，或仅被双引号包裹） |
| `TOK_SQUOTED_WORD` | 单引号 word（保留字面值） |
| `TOK_EOF` | 输入结尾 |
| `TOK_NEWLINE` | 未在引号内的换行 |
| `TOK_BAR` | `\|` |
| `TOK_AMP` | `&` |
| `TOK_SEMI` | `;` |
| `TOK_AND` | `&&` |
| `TOK_OR` | `\|\|` |
| `TOK_LPAREN` / `TOK_RPAREN` | `(` `)`（子 shell） |
| `TOK_LBRACE` / `TOK_RBRACE` | `{` `}`（命令分组） |
| `TOK_REDIR_*` | `<` `>` `<>` `>>` `<&` `>&` `<<` `<<-` `<<<` `&>` `2>` 等 |
| `TOK_DOLLAR_*` | `$VAR` `${VAR}` `$1` `$@` `$?` `$(cmd)` `$((expr))` |
| `TOK_BANG` | `!`（管道非，POSIX 可选） |

## 3. 关键状态机

```
NORMAL state:
  见字符 c:
    普通字符      → 累积到 word buf
    ' ' 或 '\t'   → 终结当前 word（若非空），输出 TOK_WORD
    '\n'          → 终结当前 word，输出 TOK_NEWLINE
    '#'           → 跳到行尾
    '"'           → 入 DQUOTE state
    '\''          → 入 SQUOTE state
    '$'           → 切换至 expand 检测（剩余字符读为变量名）
    '\\'          → 入 ESC state（保留下一个字符）
    操作符        → 终结当前 word，输出操作符 token

DQUOTE state:
  '"'           → 出 DQUOTE
  '\\'          → 入 ESC state（仅 $ ` " \ \n 等字符保留）
  '$'           → 切换至 expand（参数展开/命令替换）
  其他          → 累积到 word buf
  注：DQUOTE 中保留变量展开和命令替换

SQUOTE state:
  '\''          → 出 SQUOTE（保留所有字符字面值）
  其他          → 累积到 word buf

ESC state:
  任意字符       → 字面值入 buf

COMMENT state:
  '\n'         → 出 COMMENT → NORMAL
```

## 4. 接口设计

```c
/* 一次 lexer_next() 调用返回一个 token。*/
struct msh_token {
    int type;             /* 见上 */
    char *text;           /* word 文本（TOK_WORD/TOK_*_WORD 等） */
    size_t len;
    int lineno;           /* token 起始行号（用于错误信息） */
};

struct msh_lexer {
    const char *input;    /* 完整输入 buffer */
    size_t input_len;
    size_t pos;           /* 当前指针 */
    int lineno;
    /* 状态栈（NORMAL/DQUOTE/SQUOTE/ESC/COMMENT） */
};

void msh_lexer_init(struct msh_lexer *lx, const char *input, size_t len);
struct msh_token msh_lexer_next(struct msh_lexer *lx);   /* 返回 cloned token */
void msh_token_free(struct msh_token *tok);
```

## 5. 测试用例

```sh
# 测试 1: 简单命令
input:  echo hello
tokens: TOK_WORD(echo) TOK_WORD(hello) TOK_NEWLINE TOK_EOF

# 测试 2: 单引号保留字面值
input:  echo '$HOME $(cmd)'
tokens: TOK_WORD(echo) TOK_WORD($HOME $(cmd)) TOK_NEWLINE TOK_EOF

# 测试 3: 双引号保留变量
input:  echo "$HOME"
tokens: TOK_WORD(echo) TOK_WORD($HOME variable expansion at parse time) ...

# 测试 4: 注释
input:  # this is a comment\necho ok
tokens: TOK_EOF (newline at end-of-comment is consumed)
        TOK_WORD(echo) TOK_WORD(ok) TOK_NEWLINE TOK_EOF

# 测试 5: 续行（backslash + newline）
input:  echo hello \\
        world
tokens: TOK_WORD(echo) TOK_WORD(hello world) TOK_NEWLINE TOK_EOF
```

## 6. 验收

- [ ] `test/lex/test_basic.c` 通过：覆盖上 5 例
- [ ] `make -C projects/meuos-shell check` 全过（不破坏现有骨架测试）
- [ ] 与 dash 0.5.12 在同等输入下，token 序列对比一致

## 7. 估算

- 状态机实现：1 周
- 边界情况处理（EOF 时的 in-progress 引号）：2 天
- 测试：2 天
- **总计：约 1.5-2 周**
