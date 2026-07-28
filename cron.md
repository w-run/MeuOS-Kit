# worktree-stable-enhance 循环任务

- **作业 ID**: `f12af141`
- **间隔**: 每 10 分钟（`:07`、`:17`、`:27`、`:37`、`:47`、`:57`）
- **Cron**: `7-59/10 * * * *`
- **创建时间**: 2026-07-28
- **过期**: 3 天后自动删除（session 退出时也删除）
- **指令**:
  ```
  按照/workspace/MeuOS-Kit/.codebuddy/worktrees/stable-enhance/.issues/AGENT.md执行, 
  实现/workspace/MeuOS-Kit/.codebuddy/worktrees/stable-enhance/.issues/INDEX.md的任务;
  未完成前禁止中断;如遇新的bug则更新此文档.
  ```

## 注意

- 每个循环点启动独立 agent，不会中断前序任务
- 前序任务未完成时新 agent 会并行启动，可能有编辑冲突风险
- 需取消时执行：`CronDelete("f12af141")`
