<!--
priority: P3
status: pending
note: accept-fail 场景 — 验收命令故意 false, 测 driver 拒绝 CLAIM_DONE
-->

# 测试: 创建 fail.c (验收故意失败)

## 任务

在 `projects/ci-test/src/fail.c` 创建一个 C 文件,内容任意(能编译即可)。

这是 ci-driver 的 accept-fail 场景:验收命令**故意**用 `false` 结尾,
即使文件存在也会失败。预期 main session 发 `[[CLAIM_DONE]]` 后,driver
跑验收命令 → 失败 → 回滚 todo 为 in_progress,输出 REJECTED。

## 验收标准

```
test -f projects/ci-test/src/fail.c && false
```
