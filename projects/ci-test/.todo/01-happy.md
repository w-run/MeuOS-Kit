<!--
priority: P1
status: pending
note: happy-path 场景 — 创建 calc.c 实现 add(), 验收编译通过
-->

# 测试: 创建 calc.c 实现 add()

## 任务

在 `projects/ci-test/src/calc.c` 创建一个 C 文件,实现:

```c
int add(int a, int b) { return a + b; }
```

这是 ci-driver 自动化测试的 happy-path:todo 足够简单,main session
应该能直接创建文件、commit、发 `[[CLAIM_DONE]]`,driver 验收通过。

## 验收标准

```
cc -Wall -Werror -std=c11 -c projects/ci-test/src/calc.c -o /tmp/ci-test-calc.o
```
