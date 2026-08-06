# stability-worker — 稳定性回归守门员

## 职责

常态化回归 mcc + meuos-toolchain + meuos-libc，确保后续改动不引入 regression：
- 每 4 小时：跑 `projects/mcc/test/verify-all.sh`（24/25-test gate）
- 每 12 小时：跑 `make -C projects/mcc check-sysroot-static`（mcc 自举）
- DWARF 缺口调研（mt/as + mt/ld）
- CI 集成（GitHub Actions）

## 调度

| 周期 | 任务 | 命令 | 来源 |
|------|------|------|------|
| 4h | verify-all | `sh projects/mcc/test/verify-all.sh` | `.github/workflows/stability-regression.yml` |
| 12h | self-host | `make -C projects/mcc check-sysroot-static` | 同上 |
| 4h | verify-all + self-host | `bash .agents/workers/stability-worker/regression-runner.sh all` | 本地 cron / 手动 |

## 工作流程

1. 拉分支 `tmp/regression-worker/p2-stability`（基于 mcc-dev）
2. 跑 baseline：mt check + mcc check + verify-all + check-sysroot-static
3. 把结果记入 `regression-logs/`
4. 维护 `.todo/meuos-toolchain/mt-dwarf-eh-frame-gaps.md`（调研文档）
5. 维护 `.github/workflows/stability-regression.yml`（CI 调度）
6. 调研发现 P0/P1 缺口，单独 commit + push
7. 周期完成汇报 team-lead

## 验证标准

每次改动必须满足：
- `make -C projects/meuos-toolchain check` PASS
- `make -C projects/mcc check` exit=0
- `projects/mcc/test/verify-all.sh` 25/25 PASS（无新增 FAIL/SKIP）
- `make -C projects/mcc check-sysroot-static` exit=0

## 文件清单

```
.agents/workers/stability-worker/
├── README.md                       本文件
├── regression-runner.sh            本地回归运行脚本（离线备份 GitHub Actions）
└── regression-logs/                历史日志
    ├── BASELINE.md                 基线状态
    ├── verify-all-baseline-*.log   首轮 verify-all 全过日志
    └── self-host-baseline-*.log    首轮自举日志

.github/workflows/stability-regression.yml  GitHub Actions 调度（4h/12h）

.todo/meuos-toolchain/mt-dwarf-eh-frame-gaps.md  mt DWARF 缺口调研
```
