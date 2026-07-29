# meuos-buildtools — MeuOS Build Tool Replacements

## Phase 6a: m4 / gperf / flex

### Design principles
- Each tool = single `.c` file → standalone binary
- Zero external dependencies: `mcc + meuos-libc` only
- Output must be compilable by mcc (no GNU extensions)
- Implement minimal subset sufficient for self-hosting more packages

### Tool specs

#### m4 — Macro processor
- File: `src/m4.c`
- Replaces: GNU m4
- Required builtins: `define`/`undefine`/`include`/`sinclude`/`ifdef`/`ifelse`/`substr`/`translit`/`patsubst`/`regexp`/`eval`/`incr`/`decr`/`len`/`index`/`dnl`/`changequote`/`changecom`/`format`/`m4exit`/`m4wrap`/`syscmd`/`esyscmd`
- Must handle: recursive macro expansion, argument quoting, nested parentheses

#### gperf — Perfect hash generator
- File: `src/gperf.c`
- Replaces: GNU gperf
- Required: `%define` / `%struct-type` / keyword list → `hash()` + `in_word_set()` output
- Simplified: one output format, no switch tables

#### flex — Lexer generator
- File: `src/flex.c`
- Replaces: flex / lex
- Required: `%%` rules section, regex patterns, C actions, `ECHO`, `yyless`, `unput`
- Generates: DFA-driven `yylex()` with jump-table dispatch

### Bootstrap order
1. m4 (most foundational — autoconf depends on it)
2. gperf (independent, simpler)
3. flex (regex engine dependency)
4. bison (LALR(1) table generation — Phase 6d, later)
