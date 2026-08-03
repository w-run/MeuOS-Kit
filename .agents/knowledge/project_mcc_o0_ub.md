---
name: mcc -O0 构建触发预存在 UB
description: mcc/m++ C++ 前端存在未初始化内存 UB，-O0 构建会触发 deducing_this.cc tokenstr 断言崩溃，-O2 正常
type: project
---

mcc/m++ C++ 前端存在预存在的未初始化内存 UB：用 CFLAGS=-O0 构建时，编译 `test/cpp/deducing_this.cc`（C++23 显式对象参数 `this X& self`）会在 `declspecs`（specs.c）读到垃圾 token kind，触发 `tokenstr: Assertion 'kind < tokstr.len'` 崩溃（exit 134）。用默认 -O2 构建则完全正常，门禁全过。

**Why:** 2026-08-03 排查 requires 任务门禁失败时，一度误判为 requires 回归。通过 scratch worktree 在 eb8372d（requires 前）用 -O0 复现同样崩溃，确认是 -O0 暴露的 UB，与 requires 无关。alice 曾把 Makefile CFLAGS 临时改 -O0 调试导致此现象。

**How to apply:** 跑 check-cpp-func / check-cpp-lex / check-cpp-neg / check-c-mir 等门禁一律用默认 CFLAGS（-O2）。若看到 -O0 构建下 deducing_this.cc 的 tokenstr 断言，不要归因于最近改动，先检查构建优化级别。根治需排查 specs.c/declspecs 路径的未初始化内存，未列入待办。
