#!/bin/bash
# ============================================
# ai-usage-monitor 查询脚本 v2
# 读取 settings.json variantModels + models.json 的真实模型路由，
# 结合三个渠道用量（MiniMax plan / DeepSeek 按量 / Ark plan）输出决策建议。
#
# 关键：模型 ID 前带 "custom-local:" 前缀时，表示它是本地注册模型，
#       由 models.json 的 url/apiKey 映射到真实厂商端点：
#         custom-local:deepseek-v4-flash -> DeepSeek 官方按量 (api.deepseek.com)
#         custom-local:glm-5.2           -> Ark Coding Plan  (volces coding api)
#         custom-local:MiniMax-M3        -> MiniMax Token Plan (api.minimaxi.com)
#         custom-local:catpaw/glm-5.2    -> 本地 CatPaw (127.0.0.1) 免费
# 依赖: jq, awk, minimax-usage.sh, deepseek-balance.sh, arkcli(已配置)
# ============================================

set -u

SETTINGS=/root/.codebuddy/settings.json
MODELS=/root/.codebuddy/models.json

# --- 1. 读取 settings.json 的 variantModels 与 models.json 路由 ---
lite_m=$(jq -r '.variantModels.lite // "hy3"' "$SETTINGS" 2>/dev/null)
def_m=$(jq -r '.variantModels.default // "hy3"' "$SETTINGS" 2>/dev/null)
rsn_m=$(jq -r '.variantModels.reasoning // "custom-local:glm-5.2"' "$SETTINGS" 2>/dev/null)
main_m=$(jq -r '.model // "hy3"' "$SETTINGS" 2>/dev/null)

# 去掉 custom-local: 前缀，得到 models.json 里的真实模型 id
base_id() { echo "${1#custom-local:}"; }

# 由模型 id 查 channel（厂商/端点），用于判断走哪个渠道计费
channel_of() {
    local id="$1" c
    c=$(jq -r --arg id "$id" '.models[] | select(.id==$id) | .vendor' "$MODELS" 2>/dev/null)
    case "$c" in
        DeepSeek) echo "deepseek";;
        MiniMax)  echo "minimax";;
        ArkCode)  echo "ark";;
        CatPaw)   echo "local-free";;
        *)        echo "unknown";;
    esac
}

LITE_CH=$(channel_of "$(base_id "$lite_m")")
RSN_CH=$(channel_of "$(base_id "$rsn_m")")
DEF_CH=$(channel_of "$(base_id "$def_m")")
MAIN_CH=$(channel_of "$(base_id "$main_m")")

# --- 2. 采集三个渠道原始数据 ---
mm_raw=$(MINIMAX_API_KEY="${MINIMAX_API_KEY:-}" /root/.codebuddy/minimax-usage.sh 2>/dev/null)
mm_i=$(echo "$mm_raw" | cut -d: -f1)
mm_w=$(echo "$mm_raw" | cut -d: -f2)
mm_ei=$(echo "$mm_raw" | cut -d: -f3)
mm_ew=$(echo "$mm_raw" | cut -d: -f4)

ds=$(/root/.codebuddy/deepseek-balance.sh 2>/dev/null)
[ -z "$ds" ] && ds="N/A"

ark_json=$(arkcli usage plan --format json 2>/dev/null)
ark_s_used=$(echo "$ark_json" | jq -r '.items[0].periods[]? | select(.label=="session") | .percent' 2>/dev/null)
ark_w_used=$(echo "$ark_json" | jq -r '.items[0].periods[]? | select(.label=="weekly") | .percent' 2>/dev/null)
ark_m_used=$(echo "$ark_json" | jq -r '.items[0].periods[]? | select(.label=="monthly") | .percent' 2>/dev/null)
ark_rs=$(echo "$ark_json" | jq -r '.items[0].periods[]? | select(.label=="session") | .reset_at' 2>/dev/null)

to_rem() { [ -z "$1" ] || [ "$1" = "null" ] && echo "N/A" || awk "BEGIN{printf \"%.0f\", 100 - $1}" 2>/dev/null; }
s_rem=$(to_rem "$ark_s_used")
w_rem=$(to_rem "$ark_w_used")
m_rem=$(to_rem "$ark_m_used")

now=$(date +%s)
fmt_reset() {
    local raw=$1
    [ -z "$raw" ] || [ "$raw" = "null" ] && { echo "N/A"; return; }
    local ep
    if [ "$raw" = "$(echo "$raw" | sed 's/[0-9]//g')" ]; then
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

ds_pct="N/A"
if [ "$ds" != "N/A" ] && awk "BEGIN{exit !($ds+0 >= 0)}" 2>/dev/null; then
    ds_pct=$(awk "BEGIN{p=($ds/50)*100; if(p>100)p=100; printf \"%.0f\", p}" 2>/dev/null)
fi

# --- 3. 决策：按 variant 各自渠道余量评估可用性 ---
# 各 variant 状态：ok / low / no
mm_ok="ok"; [ "$mm_i" != "N/A" ] && awk "BEGIN{exit !($mm_i < 20)}" 2>/dev/null && mm_ok="low"
ds_ok="ok"; [ "$ds" != "N/A" ] && awk "BEGIN{exit !($ds < 10)}" 2>/dev/null && ds_ok="low"
ark_ok="ok"; [ "$s_rem" != "N/A" ] && awk "BEGIN{exit !($s_rem < 20)}" 2>/dev/null && ark_ok="low"

# 推荐 variant：按渠道余量 + 免费优先
# 规则：优先 lite（DeepSeek 按量），reasoning 仅 Ark 充足且需要复杂分析，M3 仅在 MiniMax 充足
sugg="lite"
sugg_reason="lite($lite_m, ${LITE_CH}) 默认（DeepSeek 按量 ¥${ds}）"
if [ "$LITE_CH" = "deepseek" ] && [ "$ds_ok" = "low" ]; then
    sugg="hy3"
    sugg_reason="DeepSeek 按量余额低(¥${ds})，转 hy3 免费"
fi
if [ "$RSN_CH" = "ark" ] && [ "$ark_ok" = "no" ]; then
    sugg="$sugg"
fi

reasoning_ok="no"
if [ "$RSN_CH" = "ark" ] && [ "$ark_ok" = "ok" ]; then
    reasoning_ok="yes"
elif [ "$RSN_CH" = "local-free" ]; then
    reasoning_ok="yes"
fi

cat <<EOF
{
  "routes": {
    "lite":       { "variant": "$lite_m", "id": "$(base_id "$lite_m")", "channel": "$LITE_CH" },
    "default":    { "variant": "$def_m", "id": "$(base_id "$def_m")", "channel": "$DEF_CH" },
    "reasoning":  { "variant": "$rsn_m", "id": "$(base_id "$rsn_m")", "channel": "$RSN_CH" },
    "main":       { "id": "$main_m", "channel": "$MAIN_CH" }
  },
  "usage": {
    "minimax_plan":  { "interval_pct_rem": "$mm_i", "weekly_pct_rem": "$mm_w", "interval_reset": "$mm_i_cnt", "weekly_reset": "$mm_w_cnt", "ok": "$mm_ok" },
    "deepseek":      { "balance_yuan": "$ds", "safe_pct": "$ds_pct", "ok": "$ds_ok" },
    "ark_plan":      { "session_pct_rem": "$s_rem", "weekly_pct_rem": "$w_rem", "monthly_pct_rem": "$m_rem", "session_reset": "$s_cnt", "ok": "$ark_ok" }
  },
  "decision": { "suggested": "$sugg", "reasoning_available": "$reasoning_ok", "reason": "$sugg_reason" }
}
EOF
