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

DEMO_NAME="${1:?用法: $0 <demo名称> [输出基础名]}"
OUTPUT_BASE="${2:-${DEMO_NAME}-capture}"

EXE="$BUILD_DIR/$DEMO_NAME"
if [ ! -x "$EXE" ]; then
    echo "❌ $EXE 不存在或不可执行"
    echo "   请先运行: cd $PROJECT_DIR && make examples"
    exit 1
fi

RAW="$BUILD_DIR/${OUTPUT_BASE}.raw"
HTML="$BUILD_DIR/${OUTPUT_BASE}.html"
TXT="$BUILD_DIR/${OUTPUT_BASE}.txt"
PNG="$BUILD_DIR/${OUTPUT_BASE}.png"

echo "🎬 TUI 捕获工具"
echo "==============================="
echo "程序: $EXE"
echo "输出: $HTML"
echo ""

# ── 1. 在伪终端中运行 demo ──
# 使用 `script -q -f` 提供 PTY 环境，让 TUI 程序认为自己运行在真实终端。
# TUI demo 在渲染后会阻塞等待按键，用 `timeout` 限制运行时间，
# 此时渲染已完成，输出已写入 RAW 文件。
echo "🎥 启动 PTY 捕获..."

rm -f "$RAW"

# 使用 Python PTY (capture-pty.py) 提供真实 PTY 环境，非交互环境也能可靠工作。
# TUI demo 渲染后阻塞等待按键，超时后自动退出。
python3 "$SCRIPT_DIR/capture-pty.py" \
    --program "$EXE" \
    --output "$RAW" \
    --width 80 \
    --height 30 \
    --timeout 10

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
    --width 80 \
    --height 30

HTML_SIZE=$(wc -c < "$HTML")
echo "✅ HTML 输出: $HTML ($HTML_SIZE bytes)"

# ── 4. 纯文本预览（供 Agent 分析布局） ──
python3 "$SCRIPT_DIR/ansi2html.py" \
    --input "$RAW" \
    --output "$TXT" \
    --text-only \
    --width 80 \
    --height 30

echo "✅ 文本预览: $TXT"

# ── 5. PNG 截图（直接看效果，无需浏览器） ──
if python3 -c "from PIL import Image" 2>/dev/null; then
    python3 "$SCRIPT_DIR/ansi2png.py" \
        --input "$RAW" \
        --output "$PNG" \
        --width 80 \
        --height 30 \
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
echo "  PNG  (直接看):   $PNG"
echo "  HTML (浏览器):   $HTML"
echo "  Raw  (ANSI):     $RAW"
echo "  Text (无ANSI):   $TXT"
echo ""
echo "下一步:"
echo "  1. 浏览器打开 $HTML 查看彩色渲染"
echo "  2. Agent 读取 $TXT 分析布局/对齐问题"
