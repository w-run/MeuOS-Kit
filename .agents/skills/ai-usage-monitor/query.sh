#!/bin/bash
# ============================================
# ai-usage-monitor 查询脚本（单次快照，非 TUI）
# 输出 JSON：各 AI 渠道用量 + 决策建议
# 依赖: jq, minimax-usage.sh, deepseek-balance.sh, arkcli(已配置)
# ============================================

set -u

# --- 1. 采集三个渠道原始数据 ---
mm_raw=$(MINIMAX_API_KEY="${MINIMAX_API_KEY:-}" /root/.codebuddy/minimax-usage.sh 2>/dev/null)
mm_i=$(echo "$mm_raw" | cut -d: -f1)   # 间隔用量(剩余%)
mm_w=$(echo "$mm_raw" | cut -d: -f2)   # 周用量(剩余%)
mm_ei=$(echo "$mm_raw" | cut -d: -f3)  # 间隔重置 epoch
mm_ew=$(echo "$mm_raw" | cut -d: -f4)  # 周重置 epoch

ds=$(/root/.codebuddy/deepseek-balance.sh 2>/dev/null)   # 按量余额 ¥
[ -z "$ds" ] && ds="N/A"

ark_json=$(arkcli usage plan --format json 2>/dev/null)
ark_s_used=$(echo "$ark_json" | jq -r '.items[0].periods[]? | select(.label=="session") | .percent' 2>/dev/null)
ark_w_used=$(echo "$ark_json" | jq -r '.items[0].periods[]? | select(.label=="weekly") | .percent' 2>/dev/null)
ark_m_used=$(echo "$ark_json" | jq -r '.items[0].periods[]? | select(.label=="monthly") | .percent' 2>/dev/null)
ark_rs=$(echo "$ark_json" | jq -r '.items[0].periods[]? | select(.label=="session") | .reset_at' 2>/dev/null)

# Ark percent 为"已用%"，换算为"剩余%"
to_rem() { [ -z "$1" ] || [ "$1" = "null" ] && echo "N/A" || awk "BEGIN{printf \"%.0f\", 100 - $1}" 2>/dev/null; }
s_rem=$(to_rem "$ark_s_used")
w_rem=$(to_rem "$ark_w_used")
m_rem=$(to_rem "$ark_m_used")

now=$(date +%s)

# 剩余秒数 -> 可读（支持 epoch 秒 或 ISO8601 两种格式）
# Ark reset_at 是 ISO8601（如 "2026-08-04T12:00:00Z"），需先转 epoch
fmt_reset() {
    local raw=$1
    [ -z "$raw" ] || [ "$raw" = "null" ] && { echo "N/A"; return; }
    local ep
    # ISO8601（含 'T' 或 '-'）转 epoch；纯数字视为 epoch 秒
    if [ "$raw" = "$(echo "$raw" | sed 's/[0-9]//g')" ]; then
        # 全非数字（异常值）→ N/A
        echo "N/A"; return
    elif [ -n "$(echo "$raw" | grep -E '[T :]')" ]; then
        ep=$(date -d "$raw" +%s 2>/dev/null)
    else
        ep=$raw
    fi
    [ -z "$ep" ] && { echo "N/A"; return; }
    local left=$(( ep - now ))
    [ "$left" -lt 0 ] && { echo "已到期/未知"; return; }
    local h=$(( left / 3600 )); local m=$(( (left % 3600) / 60 ))
    printf "%dh%02dm" "$h" "$m"
}
mm_i_cnt=$(fmt_reset "$mm_ei")
mm_w_cnt=$(fmt_reset "$mm_ew")
s_cnt=$(fmt_reset "$ark_rs")

# 按量余额 -> 安全百分比（50 元 = 100%，封顶 100）
ds_pct="N/A"
if [ "$ds" != "N/A" ] && awk "BEGIN{exit !($ds+0 >= 0)}" 2>/dev/null; then
    ds_pct=$(awk "BEGIN{p=($ds/50)*100; if(p>100)p=100; printf \"%.0f\", p}" 2>/dev/null)
fi

# --- 2. 决策建议（策略层）---
# 模型映射（由 settings.json variantModels 决定，此处只做建议）：
#   reasoning = GLM-5.2 (Ark Coding Plan)，稀缺点
#   lite      = DeepSeek-V4-Flash（项目/按量），免费可并行
#   default   = 当前会话主模型（可用时）
# 简化规则：全部按 lite/reasoning 决策，优先 lite；reasoning 仅当 Ark 足够。
sugg="lite"
sugg_reason="默认使用 lite（免费、可并行、命中率高）"

if [ "$s_rem" != "N/A" ] && awk "BEGIN{exit !($s_rem >= 20)}" 2>/dev/null && \
   [ "$w_rem" != "N/A" ] && awk "BEGIN{exit !($w_rem >= 20)}" 2>/dev/null; then
    sugg="lite"
    sugg_reason="Ark session(${s_rem}%)/weekly(${w_rem}%) 充足，但默认仍用 lite 保命；reasoning 仅在需要时由指挥官按需启用"
fi

if [ "$mm_i" != "N/A" ] && awk "BEGIN{exit !($mm_i < 20)}" 2>/dev/null; then
    sugg="lite"
    sugg_reason="MiniMax Token Plan 间隔用量仅剩 ${mm_i}%，禁用 default/高耗模型，全部走 lite/按量"
fi

# reasoning 仅在显式复杂任务时由指挥官按 Ark 余量临时启用；此处只给提示
reasoning_ok="no"
if [ "$s_rem" != "N/A" ] && awk "BEGIN{exit !($s_rem >= 30)}" 2>/dev/null; then
    reasoning_ok="yes"
fi

# --- 3. 输出 JSON ---
cat <<EOF
{
  "minimax": { "interval_pct_rem": "$mm_i", "weekly_pct_rem": "$mm_w", "interval_reset": "$mm_i_cnt", "weekly_reset": "$mm_w_cnt" },
  "deepseek": { "balance_yuan": "$ds", "safe_pct": "$ds_pct" },
  "ark": { "session_pct_rem": "$s_rem", "weekly_pct_rem": "$w_rem", "monthly_pct_rem": "$m_rem", "session_reset": "$s_cnt" },
  "decision": { "suggested": "$sugg", "reasoning_available": "$reasoning_ok", "reason": "$sugg_reason" }
}
EOF
