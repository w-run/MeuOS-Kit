---
name: 阶段进展用notify通知
description: MeuOS内核规划等长期多轮任务，阶段性进展里程碑用notify推送到手机，图标meuos_icon、分组meuos-kernel
type: feedback
---

每当内核规划（kernel-plan）等多轮长任务的**阶段性进展**达成时，使用 notify skill 向用户手机推送通知。

**Why:** 大喵明确要求"当阶段性任务有进展时，使用 /notify 通知用户"，并补充"图标使用 meuos_icon""通知分组使用 meuos-kernel"，以便无需盯终端也能获知长任务里程碑。

**How to apply:**
- 调用：`python3 ${CODEBUDDY_SKILL_DIR}/push.py "标题" "内容" icon=https://box.w-run.net/assets/meuos_icon.png group=meuos-kernel`（Bark/iOS 不支持 SVG，图标必须用 PNG 格式；http 致 Bark 404，见 feedback_notify_https.md）
- 图标固定 `meuos_icon.png`（PNG 格式；Bark/iOS 不支持 SVG，必须用 PNG；位于 `/workspace/static/assets/`，公网 URL 如上）；分组固定 `meuos-kernel`。
- 触发节点（按轮次里程碑，不要对每个小 agent 逐条通知以免刷屏）：
  1. 某轮全部子 agent 回收；
  2. 该轮 doc-sync 收口完成（写 `XX-收敛摘要.md` + 更新 `00`/`README`）；
  3. 该轮 `git commit` + `push origin worktree-kernel-plan` 完成。
- 推送内容不含敏感信息；标题清晰点明轮次与里程碑（如"第四轮完成并已推送"）。
