#!/bin/bash
# ============================================
# switch-main.sh — 主模型自动切换（fallback 链）
# 复用 probe-main.sh 的决策；若 needs_switch=yes，
# 通过 tmux send-keys 向指定 session 发送 /model 触发切换。
#
# 用法：
#   bash switch-main.sh [tmux_session]   # 默认 meuos-kit
#
# 决策链路（同 probe-main.sh）：
#   MiniMax M3 > codebuddy 自带 deepseek-v4-flash > DeepSeek platform v4flash > hy3
# ============================================

set -u

SKILL_DIR=/workspace/MeuOS-Kit/.codebuddy/skills/ai-usage-monitor
TMUX_SESSION="${1:-meuos-kit}"
PROBE_OUT=$(bash "$SKILL_DIR/probe-main.sh" 2>/dev/null)

decided=$(echo "$PROBE_OUT" | jq -r '.decided_main' 2>/dev/null)
cur=$(echo "$PROBE_OUT" | jq -r '.current_main' 2>/dev/null)
need=$(echo "$PROBE_OUT" | jq -r '.needs_switch' 2>/dev/null)
reason=$(echo "$PROBE_OUT" | jq -r '.reason' 2>/dev/null)

echo "decided=$decided  current=$cur  needs_switch=$need"
echo "reason=$reason"

if [ "$need" != "yes" ]; then
    echo "no switch needed"
    exit 0
fi

# 检查 tmux session
if ! tmux has-session -t "$TMUX_SESSION" 2>/dev/null; then
    echo "ERROR: tmux session '$TMUX_SESSION' not found" >&2
    exit 2
fi

# 发送 /model <id> 到 tmux session
# 注意：codebuddy 交互式会话接受 "/model <id>" 命令后会立即切换。
# -l 防止 send-keys 把空格/特殊字符当字面键
echo "switching to $decided via tmux session '$TMUX_SESSION'..."
tmux send-keys -t "$TMUX_SESSION" -l "/model ${decided}"
sleep 0.3
tmux send-keys -t "$TMUX_SESSION" Enter
sleep 1
echo "switch command sent"
