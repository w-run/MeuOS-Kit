---
name: m++ 多 worker 并发提交纪律与夹带事故
description: m++ 团队多 worker 并发同一 worktree 时，必须文件级 git add，已发生两次夹带事故（647a05b 夹带 F、93ab4b4 夹带 V），遇夹带优先维持现状不 force push
type: feedback
---

m++ 重构多 worker 并发改同一 worktree（分支 worktree-mxx-work）时，每个 worker 提交必须 `git add <具体文件路径>`，**严禁 `git add -A`、`git add <目录>`、`git add -u`**。提交前 `git pull` 同步，提交后 `git push origin worktree-mxx-work`。

**并发安全补充纪律（2026-08-03 team-lead 广播，因 worker-cpp23 的 6cc2d18 被他人 `git reset --hard` 冲掉 + fe1d55c 夹带 worker-lambda/worker-fold 文件）：**
- **严禁 `git reset --hard` / `git checkout .` / `git clean -f`**：会丢弃他人在途未提交改动；想放弃本地改动先通知 team-lead 协调。
- 改完立即 `git add <具体文件> && git commit && git push`，不在暂存区滞留多文件；commit 前先 `git pull`（非 ff 则 `git fetch && git rebase origin/worktree-mxx-work`）。
- commit 前 `git status` 自查暂存区（M/A 列），出现非自己文件先 `git restore --staged <文件>` 归还他人。
- 共享 git index 是夹带根因：`git add` 会连同他人在途已暂存/未暂存改动；改被多人编辑的文件（如 .issues/0802.md）时用「基于 HEAD 版本 + `diff -u --label` + `git apply --cached`」外科手术式只暂存自己的 hunk。

**Why:** 项目已发生两次夹带事故，根因都是 worker 用目录级/全局 add 把他人在途未提交改动一并推走，导致 commit 归属混杂：
- 647a05b（placement new）夹带了缺陷 F 的 fold 修复（msimp_block），37e7857 记录但未及时同步文档状态。
- 93ab4b4（concept 形参名修复）夹带了 worker-fold 的缺陷 V 修复（src/mir/passes.c、test/mir/pass_test.c、test/c99/signed_div_pow2.c、.issues/0802.md 共 4 文件）。

两次功能均正确（verify-all 仍 6/6），但污染提交历史、commit 信息未反映真实内容。

**How to apply:**
- 每次 spawn m++ worker 时，prompt 硬性强调「文件级 git add，严禁 git add -A/目录级 add」，并约束只改自己文件域。
- 遇已 push 代码的夹带事故：**优先维持现状、不 force push**。force push 重写祖先链会破坏所有已 `git pull` 的 worker 工作树，并发竞态风险远大于提交归属不纯净的收益。仅在文档/复盘记录夹带事实并后续加强纪律。仲裁夹带时直接选「维持现状」。
- 因一个 worktree 上多 lite worker 并行（项目目标：lite 10+ 并行），文件级 add 是硬性纪律，lead 在仲裁并发提交冲突时亲自执行 git add -p 拆分也要小心 hunk 非交互行为不可靠（用回退+patch 拆分更稳）。
- **已发生实例**：fe1d55c（worker-cpp23 的 C++23 路线图提交）因共享索引夹带 worker-lambda 的 lambda 测试迁移 + worker-fold 的 0802.md V 行；worker-cpp23 已按纪律在 0802.md 注明归属（456718f）。worker-cpp23 的首个提交 6cc2d18 曾被他 worker 的 `reset --hard FETCH_HEAD` 冲掉（文档文件仍在磁盘、内容未丢，重新提交即可）。
- **第三次实例 6ca4ba1（2026-08-03，worker-pp4 的 pp.c 被 worker-gate3 夹带）**：pp4 已文件级 `git add pp.c`，但 gate3 的 `git commit`（全量提交、未限定路径）把 pp4 已 staged 的 pp.c 一并吞入其 6ca4ba1（"test: chibicc run.sh..."）并推上 origin。教训：**即使自己做了文件级 add，只要 commit 不是 `git commit -- <path>`，另一个 worker 的全量 commit 仍可吞掉你已 staged 的文件**。故「add 后立即 commit」窗口要尽量短；被夹带方按纪律维持现状、不 force push，只回报 lead 并书面知会被夹带 commit 的提交者。pp4 已向 gate3 书面提醒其提交方式。
- **gate3 侧教训（2026-08-03）**：我的 run.sh 修复在同一次事故里反被吞（6ca4ba1 只含 pp.c，run.sh 未提交，最终由并发的 f05633f 提交）。**正确提交姿势：不用 `git add` + 无路径 `git commit`，直接用 `git commit --only <path>`（或 `git commit -- <path>`）**，它只提交指定路径的工作树内容、完全绕过共享暂存区，不受他人并发 add/commit 影响。commit 后立即 `git push` 并 `git show --name-status HEAD` 核对本提交只含预期文件。
- **gate3 确认采纳（2026-08-03）**：gate3 已核实 6ca4ba1 事故属实并道歉，其 run.sh 修复已独立落地为 f05633f（单文件），此后其所有提交一律 `git commit --only <path>`。团队约定升级：commit 前 `git diff --cached --name-only` 自查 + `--only` 限定路径双保险。pp4 的 #elifdef/#elifndef 修复最终在独立分支 worktree-tmp-pp4 干净落地（d2456fe + 1d45104，已 push 待 lead 合入）；后续类似个人任务优先用独立 worktree，避开共享 index 竞态。
