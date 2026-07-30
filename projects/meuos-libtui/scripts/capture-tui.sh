#!/bin/bash
# capture-tui.sh — 在伪终端运行 TUI 程序并捕获终端效果（HTML）
#
# 用法:
#   ./scripts/capture-tui.sh <demo程序名> [输出基础名]
#
# 参数:
#   <demo>      examples/ 下编译好的程序名（demo / demo_dialog / demo_list）
#   [输出基础名]  默认 <demo>-capture，生成 .raw + .html
#
# 依赖: python3（自带 ANSI->HTML 转换器，无需 aha/socat）
#
# 工作流程:
#   1. 通过伪终端运行 demo 程序，使用 TUI_DEMO_CAPTURE=1 触发一次性渲染模式
#   2. 重定向 stdin 为 /dev/null，stdout 到 raw 文件
#   3. 等待渲染完成（程序自动退出）
#   4. 用 Python 解析 ANSI + 渲染成 HTML
#
# 输出:
#   build/<name>-capture.raw   ANSI 原始字节
#   build/<name>-capture.html  HTML 渲染版（浏览器打开）
#   build/<name>-capture.txt   纯文本预览（供 Agent 分析布局）

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
# 截图输出目录（与 build 隔离）
SCREENSHOT_DIR="${SCREENSHOT_DIR:-$PROJECT_DIR/docs/screenshots}"
mkdir -p "$SCREENSHOT_DIR"

DEMO_NAME="${1:?用法: $0 <demo名称> [输出基础名] [主题名] [宽] [高]}"
OUTPUT_BASE="${2:-${DEMO_NAME}-capture}"
THEME_NAME="${3:-}"
WIDTH="${4:-80}"
HEIGHT="${5:-30}"

EXE="$BUILD_DIR/$DEMO_NAME"
if [ ! -x "$EXE" ]; then
    echo "❌ $EXE 不存在或不可执行"
    echo "   请先运行: cd $PROJECT_DIR && make examples"
    exit 1
fi

RAW="$BUILD_DIR/${OUTPUT_BASE}.raw"
HTML="$BUILD_DIR/${OUTPUT_BASE}.html"
TXT="$BUILD_DIR/${OUTPUT_BASE}.txt"
PNG="$SCREENSHOT_DIR/${OUTPUT_BASE}.png"

echo "🎬 TUI 捕获工具"
echo "==============================="
echo "程序:   $EXE"
echo "主题:   ${THEME_NAME:-默认}"
echo "输出:   $PNG"
echo ""

# ── 1. 在伪终端中运行 demo ──
# 使用 `script -q -f` 提供 PTY 环境，通过 TUI_DEMO_CAPTURE=1 触发一次性渲染
# 注意：不要把 stdin 重定向到 /dev/null，会让 PTY 立即 EOF，导致 demo
# 还没渲染完就关闭 PTY master
echo "🎥 启动 PTY 捕获..."

rm -f "$RAW"

# 强制使用 80x30 终端尺寸，便于稳定截图
# 注意：不要用 2>&1 把 stderr 推到文件，会污染 ANSI 输出
TERMINAL_COLS=$WIDTH \
TERMINAL_LINES=$HEIGHT \
LINES=$HEIGHT \
COLUMNS=$WIDTH \
TERM=xterm-256color \
TUI_DEMO_CAPTURE=1 \
TUI_DEMO_THEME="$THEME_NAME" \
script -q -f -c "$EXE" "$RAW" || true

# ── 2. 校验输出 ──
if [ ! -s "$RAW" ]; then
    echo "❌ 捕获失败，$RAW 为空"
    exit 1
fi

# 去掉 script(1) 头部/尾部标记（只删除完全匹配的行，保留 ANSI 内容）
python3 -c "
with open('$RAW', 'rb') as f:
    data = f.read().decode('utf-8', errors='replace')

lines = data.split('\n')
# 只删除完全是 'Script started on ...' 或 'Script done on ...' 的行
filtered = [line for line in lines
            if not line.startswith('Script started on ')
            and not line.startswith('Script done on ')]
out = '\n'.join(filtered)
with open('$RAW', 'w', encoding='utf-8') as f:
    f.write(out)
print(f'Cleaned: {len(data)} -> {len(out)} bytes')
"

RAW_SIZE=$(wc -c < "$RAW")
echo "✅ 原始捕获: $RAW ($RAW_SIZE bytes)"

# ── 3. ANSI -> HTML ──
echo "🔄 转换 ANSI → HTML..."

python3 "$SCRIPT_DIR/ansi2html.py" \
    --input "$RAW" \
    --output "$HTML" \
    --title "$DEMO_NAME TUI Capture" \
    --width $WIDTH \
    --height $HEIGHT

HTML_SIZE=$(wc -c < "$HTML")
echo "✅ HTML 输出: $HTML ($HTML_SIZE bytes)"

# ── 4. 纯文本预览（供 Agent 分析布局） ──
python3 "$SCRIPT_DIR/ansi2html.py" \
    --input "$RAW" \
    --output "$TXT" \
    --text-only \
    --width $WIDTH \
    --height $HEIGHT

echo "✅ 文本预览: $TXT"

# ── 5. PNG 截图（直接看效果，无需浏览器） ──
if python3 -c "from PIL import Image" 2>/dev/null; then
    python3 "$SCRIPT_DIR/ansi2png.py" \
        --input "$RAW" \
        --output "$PNG" \
        --width $WIDTH \
        --height $HEIGHT \
        --font-size 14 \
        --scale 1 || echo "⚠️  PNG 生成失败"
else
    echo "⚠️  Pillow 未安装，跳过 PNG 生成"
    echo "   安装: pip install Pillow"
fi

# ── 6. 总结 ──
echo ""
echo "🎉 捕获完成！"
echo "==================="
echo "  PNG  (直接看):   $PNG  ← 唯一持久化位置"
echo "  HTML (浏览器):   $HTML  (build/)"
echo "  Raw  (ANSI):     $RAW   (build/)"
echo "  Text (无ANSI):   $TXT   (build/)"
echo ""
echo "下一步:"
echo "  1. 浏览器打开 $HTML 查看彩色渲染"
echo "  2. Agent 读取 $TXT 分析布局/对齐问题"
