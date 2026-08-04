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

# --- 3. 决策：按渠道策略（大喵 2026-08-04 定位）---
# 渠道优先级：
#   MiniMax M3  —— 只有周/5h 限制，间隔重置快，周配额充足可"猛用"（default=M3）
#   lite(DS flash) —— 经济实惠好吃不贵，性价比首选（常规任务主力）
#   reasoning(GLM-5.2/Ark) —— 月总量告急(monthly 15%)，必须省着用，仅必要复杂分析
#   codebuddy 自带 —— 查不到积分余额，只能应急
# 各渠道 ok/low 判定：
#   minimax: 用"周"配额判断（猛用依据），间隔只影响短期（重置快）；周<20% 才 low
#   deepseek: 余额 <¥10 low
#   ark: 用"月"总量判断（告急依据），monthly<20% 则 reasoning 禁用
mm_ok="ok"; [ "$mm_w" != "N/A" ] && awk "BEGIN{exit !($mm_w < 20)}" 2>/dev/null && mm_ok="low"
ds_ok="ok"; [ "$ds" != "N/A" ] && awk "BEGIN{exit !($ds < 10)}" 2>/dev/null && ds_ok="low"
ark_ok="ok"; [ "$m_rem" != "N/A" ] && awk "BEGIN{exit !($m_rem < 20)}" 2>/dev/null && ark_ok="low"

# 推荐 variant：
#   **禁用 default**（settings.json 中 default 为随便配置，不可靠）。
#   只用 lite / reasoning / hy3。
#   - MiniMax 周配额充足 → 提示可把某 variant 临时切到 M3 猛用（需改 variantModels），
#     但不建议 default；常规仍走 lite（DS flash 性价比）
#   - lite（DS flash）常规主力
#   - DeepSeek 余额低 → 转 hy3 免费
sugg="lite"
sugg_reason="lite($lite_m, ${LITE_CH}) 常规主力（DS flash 性价比；default 禁用）"
if [ "$ds_ok" = "low" ]; then
    sugg="hy3"
    sugg_reason="DeepSeek 按量余额低(¥${ds})，转 hy3 免费"
fi

# reasoning 可用性：按 reasoning variant 实际 channel 判断
#   - ark (glm-5.2)     → Ark monthly≥20% 才可用（月总量告急）
#   - minimax (M3)      → MiniMax 周配额 ok 即可（可猛用）
#   - local-free        → 恒可用
reasoning_ok="no"
case "$RSN_CH" in
    ark)     [ "$ark_ok" = "ok" ] && reasoning_ok="yes";;
    minimax) [ "$mm_ok" = "ok" ] && reasoning_ok="yes";;
    local-free) reasoning_ok="yes";;
esac

# MiniMax 备注：周配额充足时可用于临时切某 variant 到 M3 猛用（需改 variantModels），但不禁用 lite
mm_note="MiniMax 周配额 ${mm_w}% (${mm_ok})，间隔 ${mm_i}% 约 ${mm_i_cnt} 后重置"
if [ "$mm_ok" = "ok" ]; then
    mm_note="MiniMax 周配额 ${mm_w}% 充足 — 如需猛用 M3，可将某 variant(lite/reasoning) 临时改 variantModels 指向 custom-local:MiniMax-M3；决策仍禁用 default"
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
  "decision": { "suggested": "$sugg", "reasoning_available": "$reasoning_ok", "minimax_note": "$mm_note", "reason": "$sugg_reason" }
}
EOF
