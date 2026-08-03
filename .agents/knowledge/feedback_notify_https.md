---
name: notify 推送必须用 https 绝对路径
description: notify skill 推送时 icon/url 参数必须用 https://box.w-run.net 绝对路径，http 会导致 Bark 端 404
type: feedback
---

使用 notify 技能推送通知时，`icon=` 与 `url=` 参数**必须**使用 https 绝对路径（如 `https://box.w-run.net/assets/meuos_icon.svg`），不能使用 `http://`。

**Why:** 2026-08-03 实测 `http://box.w-run.net/...` 图标导致 Bark 服务端返回 404 推送失败；改为 `https://` 后同样内容立即 200 success。Bark 端对 http 图标/URL 解析失败。

**How to apply:** 任何 notify 推送，静态资源一律用 `https://box.w-run.net/<path>`（本地 /workspace/static 经 box-webroot 服务映射到该域名）。**icon 一律用 PNG**（如 `https://box.w-run.net/assets/meuos_icon.png`，assets 下已有各图标 PNG），不用 SVG（部分客户端拒绝）；url 指向 /workspace/static 下的 .html。标题/内容含 UTF-8 中文正常，但避免 `/`、`×` 等特殊符号（曾致 404）。

**报告目录规则**（2026-08-03 修正）：详细报告放 `/workspace/static/<groupname>/` 下，目录名与推送的 `group=` 参数一致（建议英文/拼音），URL 为 `https://box.w-run.net/<groupname>/<文件名>`。例如 group=meuos-kit → 报告放 `/workspace/static/meuos-kit/aug03.html`。icon/url 参数**不编码**直接传完整 URL；只做简报时把关键信息塞进 content 即可，详细内容才生成 HTML 用 url 跳转。
