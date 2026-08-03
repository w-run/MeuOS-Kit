---
name: notify 推送经验
description: 里程碑推送、icon 必须 https PNG、push.py 对 icon 不编码致 404、正文禁裸斜杠且宜精简
type: feedback
---

notify skill（`/root/.codebuddy/skills/notify/push.py`，Bark 协议 `https://msg.w-run.net/...`）推送经验汇总：

**1. 阶段进展用 notify（长期任务里程碑）**
- kernel-plan 等多轮长任务的**阶段性进展**（某轮子 agent 回收完成 / doc-sync 收口 / commit+push 完成）用 notify 推送，勿对每个小 agent 逐条通知刷屏。
- 图标固定 `https://box.w-run.net/assets/meuos_icon.png`（PNG；Bark 不支持 SVG），分组 `meuos-kernel`。

**2. icon/url 必须 https 绝对路径 + PNG**
- 必须用 `https://box.w-run.net/<path>`（webroot 映射 `/workspace/static`），http 致 Bark 404。
- icon 一律 PNG（`meuos_icon.png`/`meuos_logo.png` 可用），不用 SVG（部分客户端拒绝）。

**3. push.py icon/url 编码 bug**
- push.py 对 `icon=`/`url=` 故意不编码，含 `://` 完整 URL 原样进 query 会致 Bark `Error: 404`。
- **规避**：带 icon/url 时不用 push.py 裸传，改用内联 python `urllib.parse.quote(url, safe="")` 完整编码后拼 URL，或 curl `--get --data-urlencode "icon=..."`。仅标题+内容（无 icon/url）时 push.py 正常。

**4. 正文禁裸斜杠且宜精简**
- `urllib.parse.quote(body)` 默认 `safe='/'` 不编码斜杠，body 里 `C15/C16` 等 `/` 被 Bark 当 URL path 分隔符 → 404。
- 文案层级用 `、`/空格代替 `/`（写 `C15、C16、V1`，不写 `C15/C16`）。
- 大段详情生成 HTML 报告到 `/workspace/static/<group>/<file>.html`，notify 只发短摘要 + `url=https://box.w-run.net/<group>/<file>.html` 跳转。

**已验证可用组合**：`icon=https://box.w-run.net/assets/meuos_icon.png` + `group=meuos-kernel`，正文无裸 `/` 返回 `code:200`。标题/内容 UTF-8 中文正常。
