---
name: notify正文禁止裸斜杠且宜精简
description: Bark推送(push.py)的body含裸 "/" 会被当URL路径分隔符致404；长正文也可能超URL长度限制，应精简或改用HTML报告+url
type: feedback
---

用 `notify` skill（`/root/.codebuddy/skills/notify/push.py`，Bark 协议 `https://msg.w-run.net/...`）推送时，正文（第2参数）**不要包含裸 `/`**，且宜精简。

**Why:** `push.py` 用 `urllib.parse.quote(body)`，但默认 `safe='/'` 不编码斜杠；body 里的 `C15/C16` 等 `/` 被 Bark 服务端当作 URL path 分隔符，导致路径多段、返回 `Error: 404`（短正文无斜杠则成功）。此外超长正文经 URL 编码后也可能触发 404。

**How to apply:**
- 通知文案里的层级列举用 `、` 或空格代替 `/`（如写 `C15、C16、V1`，不要 `C15/C16`）。
- 需要传递大段详情时，按 SKILL 建议生成 HTML 报告到 `/workspace/static/<group>/<file>.html`，notify 仅发短摘要 + `url=https://box.w-run.net/<group>/<file>.html` 跳转，不要把全文塞进 body。
- 已验证可用组合：`icon=https://box.w-run.net/assets/meuos_icon.png`（PNG，Bark 不支持 SVG）+ `group=meuos-kernel`，正文无裸 `/` 时返回 `code:200`。
