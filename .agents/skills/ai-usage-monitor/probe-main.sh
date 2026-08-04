#!/bin/bash
# ============================================
# probe-main.sh — 主模型选择 + fallback 链
# 优先级: MiniMax M3 > codebuddy 自带 deepseek-v4-flash > DeepSeek platform v4flash
#   - M3 可用性 = MiniMax 周配额充足（读取 query.sh 的 minimax 周配额）
#   - codebuddy 自带 dsv4flash = `codebuddy -p --model=deepseek-v4-flash` 有回复
#   - DeepSeek platform v4flash = 按量余额充足
# 输出 JSON: { main_model, channel, fallback_order }
# ============================================

set -u

QUERY=/workspace/MeuOS-Kit/.codebuddy/skills/ai-usage-monitor/query.sh
SETTINGS=/root/.codebuddy/settings.json

# --- 1. 取 query.sh 的用量与决策 ---
q=$(bash "$QUERY" 2>/dev/null)
mm_w=$(echo "$q" | jq -r '.usage.minimax_plan.weekly_pct_rem' 2>/dev/null)
ds=$(echo "$q" | jq -r '.usage.deepseek.balance_yuan' 2>/dev/null)

# 当前主模型（settings.json "model"）
cur_main=$(jq -r '.model // "hy3"' "$SETTINGS" 2>/dev/null)

# --- 2. 可用性判定 ---
# MiniMax 周配额前提 + 实探测（探测能真实反映 429/plan 不可用，而不只信 minimax-usage.sh 的周配额缓存）
# 注意：实探测 codebuddy -p --model=custom-local:MiniMax-M3 会消耗 MiniMax token；周配额<20 直接判 no 免探测
m3_ok="no"
if [ "$mm_w" != "N/A" ] && awk "BEGIN{exit !($mm_w >= 20)}" 2>/dev/null; then
    # 周配额充足，再实探测 M3 是否真可用（429/plan 上限时探测失败）
    mm_probe=$(timeout 60 codebuddy -p --model=custom-local:MiniMax-M3 "reply with just: OK" 2>/dev/null)
    [ -n "$mm_probe" ] && echo "$mm_probe" | grep -q "OK" && m3_ok="yes"
fi

ds_ok="no";   [ "$ds" != "N/A" ] && awk "BEGIN{exit !($ds >= 10)}" 2>/dev/null && ds_ok="yes"

# 探测 codebuddy 自带 deepseek-v4-flash（有回复即可用）
cb_ok="no"
probe_out=$(timeout 60 codebuddy -p --model=deepseek-v4-flash "reply with just: OK" 2>/dev/null)
[ -n "$probe_out" ] && echo "$probe_out" | grep -q "OK" && cb_ok="yes"

# --- 3. 决策主模型 ---
if [ "$m3_ok" = "yes" ]; then
    main="custom-local:MiniMax-M3"; channel="minimax"; reason="MiniMax 周配额 ${mm_w}% 充足且实探测可用，M3 主模型"
elif [ "$cb_ok" = "yes" ]; then
    main="custom-local:deepseek-v4-flash"; channel="codebuddy-builtin"; reason="M3 不可用（周配额不足或 429），fallback codebuddy 自带 dsv4flash"
elif [ "$ds_ok" = "yes" ]; then
    main="custom-local:deepseek-v4-flash"; channel="deepseek-platform"; reason="双 fallback 后 DeepSeek platform 按量"
else
    main="hy3"; channel="free"; reason="所有付费渠道不足，hy3 兜底"
fi

# fallback 顺序（供指挥官参考）
fb="[MiniMax-M3, codebuddy-builtin-deepseek-v4-flash, deepseek-platform-v4flash]"

# needs_switch：剥掉 custom-local: 前缀比较，避免同一模型字面前缀差异误判
norm() { echo "${1#custom-local:}"; }
need=$([ "$(norm "$main")" != "$(norm "$cur_main")" ] && echo yes || echo no)

cat <<EOF
{
  "current_main": "$cur_main",
  "decided_main": "$main",
  "channel": "$channel",
  "reason": "$reason",
  "availability": { "minimax_m3": "$m3_ok", "codebuddy_builtin_dsv4flash": "$cb_ok", "deepseek_platform": "$ds_ok" },
  "fallback_order": "$fb",
  "needs_switch": "$need"
}
EOF
