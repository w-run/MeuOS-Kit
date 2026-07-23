<!--
priority: P2
status: done
note: partial 场景 — 加 mul() + div(), 验收只测 mul
start_ts: 2026-07-24
done_ts: 2026-07-24
done_by_driver_ts: 2026-07-23T18:24:36Z
done_note: driver accepted; all cmds passed
-->

# 测试: 在 calc.c 加 mul() 和 div()

## 任务

在 `projects/ci-test/src/calc.c` 的 `add()` 之后,追加两个函数:

```c
int mul(int a, int b) { return a * b; }
int div(int a, int b) { return a / b; }
```

这是 ci-driver 的 partial 场景:验收命令**只测 mul**,不测 div。
main session 如果两个都实现了,CLAIM_DONE 会通过;如果只实现 add
没动这个 todo,应该保持 pending/in_progress。

## 验收标准

```
printf 'int main(void){return mul(2,3)==6?0:1;}' > /tmp/ci-test-mul-main.c
cc -Wno-implicit-function-declaration -Iprojects/ci-test/src /tmp/ci-test-mul-main.c projects/ci-test/src/calc.c -o /tmp/ci-test-mul
/tmp/ci-test-mul
```
