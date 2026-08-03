# msh-parse — 语法分析器

> **任务 ID**: `msh-parse`
> **范围**: `projects/meuos-shell/src/parse/parse.c` + `include/msh/parse.h`
> **依赖**: `msh-lex` 完成
> **状态**: ⏳ 待启动（P6 第二步）

## 1. 目标

把词法 token 流组装为 AST（抽象语法树），供执行器遍历。

## 2. POSIX sh AST 节点类型

参考 dash AST（不复制源码）：

```c
enum node_type {
    N_CMD,         /* 简单命令：argv + 重定向 */
    N_PIPE,        /* pipeline：A | B | C */
    N_LIST,        /* 列表：A ; B ; C 或 A && B || C */
    N_AND, N_OR,   /* 短路 */
    N_BG,          /* 后台：A & */
    N_SUBSHELL,    /* ( A ) */
    N_BRACE_GROUP, /* { A; } */
    N_IF,          /* if ... then ... else ... fi */
    N_FOR,         /* for x in ... ; do ... ; done */
    N_WHILE,       /* while ... do ... done */
    N_UNTIL,       /* until ... do ... done */
    N_CASE,        /* case x in pat) ... ;; esac */
    N_FUNC,        /* fname() { ... } 或 function fname { ... } */
    N_ASSIGN,      /* VAR=val（作为命令前缀） */
    N_REDIR,       /* < > >> << 等 */
    N_WORD,        /* 字符串字面值或变量 */
    N_VAR,         /* $VAR ${VAR} $1 $@ */
    N_GLOB,        /* *.c 包含 glob 字符的 word */
    N_QUOTED,      /* 单/双引号 word */
};
```

## 3. AST 节点结构

```c
struct node {
    enum node_type type;
    struct node *next;       /* 同级节点链 */
    struct node *first_child;
    struct node *last_child;
    union {
        /* N_CMD */
        struct {
            char **argv;     /* NULL-terminated */
            struct node *redir;
        } cmd;
        /* N_IF */
        struct {
            struct node *cond;
            struct node *then_part;
            struct node *else_part;
        } if_stmt;
        /* N_FOR */
        struct {
            char *varname;
            struct node *wordlist;
            struct node *body;
        } for_stmt;
        /* ... */
    } u;
};
```

## 4. 解析优先级（自底向上）

```
atom       : word | redirection | subshell | compound
pipeline   : atom ('|' atom)*
list       : pipeline ((';' | '\n' | '&' | '&&' | '||') pipeline)*
complete_command : list ['&'] [';'] '\n'
```

## 5. 递归下降 parser

简单手动递归下降：

```c
parse_complete_command(lexer) → list_node | NULL
parse_list(lexer, acc)
  next = parse_pipeline(lexer)
  if next == NULL return acc
  if op == ';'  or '\n': acc = cons(acc, next); return parse_list
  if op == '&&' or '||': acc = cons(acc, make_logical(op, next)); ...
parse_pipeline(lexer)
  acc = parse_atom(lexer)
  if peek == '|': acc = cons(acc, parse_pipeline_inner); return acc
parse_atom(lexer)
  if peek == '(': return parse_subshell
  if peek == word: return parse_simple_command
  ...
parse_simple_command(lexer)
  argv = []
  while peek is assignment_or_word:
    argv.push(parse_word)
    redir = parse_redir  // 边收集
  return {type=N_CMD, argv, redir}
```

## 6. 测试用例（POSIX sh 子集）

```sh
# 1. 简单命令
echo hello world
→ N_CMD argv=["echo","hello","world"]

# 2. 管道
ls | grep foo | wc -l
→ N_PIPE [N_CMD(ls), N_CMD(grep,foo), N_CMD(wc,-l)]

# 3. 后台
sleep 5 &
→ N_BG(N_CMD(sleep,5))

# 4. 重定向
echo hello > /tmp/out
→ N_CMD(echo,hello) redir={N_REDIR(>,/tmp/out)}

# 5. 复合命令
{ cmd1; cmd2; }
→ N_BRACE_GROUP(N_LIST(N_CMD(cmd1), N_CMD(cmd2)))

# 6. if
if [ -f foo ]; then echo yes; fi
→ N_IF(cond=N_CMD([,-f,foo), then=N_CMD(echo,yes))
```

## 7. 验收

- [ ] `test/parse/test_ast.c` 6 个用例全过
- [ ] `make -C projects/meuos-shell check` 全过
- [ ] 与 dash 0.5.12 在相同输入下，AST 树形状对比一致（节点类型相同，结构相同）

## 8. 估算

- AST 结构定义：2 天
- 简单命令 + pipeline：1 周
- 控制流（if/for/while/case）：1-2 周
- 函数：2 天
- 测试：1 周
- **总计：约 4-5 周**

注意：词法完成后此任务即可启动，但语法 + 后续执行（exec/builtin/var）一并工作
才能跑出"bash -c 'foo'" 完整行为。建议 lex/parse/exec 三模块一并推进。
