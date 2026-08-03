---
name: notify skill push.py icon 编码 bug
description: notify skill 的 push.py 对 icon/url 参数不编码，含 :// 时 Bark 返回 404；需完整编码或用 curl --data-urlencode
type: feedback
---

notify skill（`/root/.codebuddy/skills/notify/push.py`）对 `icon=`/`url=` 参数故意不编码（脚本注释声称"编码后服务器无法识别"），但实测 **含 `://` 的完整 URL 原样进 query string 会导致 Bark 返回 `Error: 404`**。

**Why:** 2026-08-03 推送 `icon=https://box.w-run.net/assets/meuos_icon.png` 时 push.py 返回 404，而同一 URL 用 `urllib.parse.quote(..., safe="")` 完整编码（`%3A%2F%2F`）后返回 `{"code":200}`。curl `--get --data-urlencode` 也成功（200）。证明脚本的"不编码"假设是错的。

**How to apply:** 需要带 icon/url 推送时，不要直接用 push.py 传 `icon=<完整URL>`。改用内联 python：`icon = urllib.parse.quote("<url>", safe="")`，再拼进 `BASE/title/body?icon=...&group=...`。或 curl `--get --data-urlencode "icon=..."`。仅标题+内容（无 icon/url）时 push.py 正常工作。

Bark 服务端 BASE：`https://msg.w-run.net/2EkBhWJoeQ9D43dpiZUC76`；webroot `https://box.w-run.net/` 的 assets 图标均 200 可达（meuos_icon.png / meuos_logo.png 可用）。
