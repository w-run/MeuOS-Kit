#!/usr/bin/env python3
"""
ansi2html.py — ANSI/VT100 转义序列 → HTML/纯文本

支持范围（meuos-libtui 用到的子集）：
  - SGR: \\033[<n>[;...m（颜色、属性、重置）
  - 光标: \\033[H、\\033[<r>;<c>H、\\033[K（清行尾）
  - 清除: \\033[2J（全屏）、\\033[H（光标回家）

主题: meuos-dark（黑底 + 绿色调）

用法:
  ansi2html.py --input RAW --output HTML [--title T] [--text-only] [--theme meuos-dark]
"""

import argparse
import re
import sys
from html import escape as html_escape


# ══════════════════════════════════════════════════════
#  ANSI 颜色映射（meuos-libtui 主题：暗色背景 + 绿色主调）
# ══════════════════════════════════════════════════════

# 前景色（30-37, 90-97）→ CSS 颜色
FG_COLORS = {
    30: "#4d4d4d",   # 黑 → 暗灰（暗背景上可见）
    31: "#ff6b6b",   # 红
    32: "#4ade80",   # 绿（meuos 主色）
    33: "#fbbf24",   # 黄
    34: "#60a5fa",   # 蓝
    35: "#c084fc",   # 紫
    36: "#22d3ee",   # 青
    37: "#e4e4e7",   # 白
    # 亮色
    90: "#71717a",
    91: "#fca5a5",
    92: "#86efac",
    93: "#fde68a",
    94: "#93c5fd",
    95: "#d8b4fe",
    96: "#67e8f9",
    97: "#fafafa",
}

# 背景色（40-47, 100-107）→ CSS 颜色
BG_COLORS = {
    40: "#1a1a2e",   # 黑 → 深背景
    41: "#7f1d1d",
    42: "#166534",
    43: "#854d0e",
    44: "#1e3a8a",
    45: "#6b21a8",
    46: "#155e75",
    47: "#e4e4e7",
    100: "#52525b",
    101: "#dc2626",
    102: "#16a34a",
    103: "#eab308",
    104: "#2563eb",
    105: "#9333ea",
    106: "#0891b2",
    107: "#fafafa",
}

# 属性 → CSS
ATTRS = {
    1:  "font-weight:bold",
    2:  "opacity:0.6",       # dim
    3:  "font-style:italic",
    4:  "text-decoration:underline",
    5:  "text-decoration:blink",
    7:  "filter:invert(100%)",  # reverse — 简化处理
    8:  "visibility:hidden",
    9:  "text-decoration:line-through",
}


# ══════════════════════════════════════════════════════
#  屏幕状态（用于定位）
# ══════════════════════════════════════════════════════

class Screen:
    """简单的屏幕模型：二维字符数组 + 样式。"""

    def __init__(self, width=120, height=40):
        self.w = width
        self.h = height
        self.col = 1   # 1-based
        self.row = 1
        self.cells = [[""] for _ in range(height * width)]
        self.styles = [None] * (height * width)
        # 初始化为空字符串（便于 join）
        for i in range(len(self.cells)):
            self.cells[i] = [""]  # [[char, style_key], ...] 简化用 tuple 列表

    def idx(self, r, c):
        """r, c 是 1-based。"""
        if r < 1 or c < 1 or r > self.h or c > self.w:
            return -1
        return (r - 1) * self.w + (c - 1)

    def put(self, ch, style):
        i = self.idx(self.row, self.col)
        if i < 0:
            return
        # 跳过控制字符（除了空格和 TAB）
        if ch and ord(ch[0]) < 0x20 and ch != " ":
            return
        # 跳过 NUL
        if ch == "\x00":
            return
        # 注意：Python str 已是 Unicode，无需 UTF-8 截断检查。
        # 所有非 ASCII 字符（框线、块元素、CJK）都应正常显示。
        self.cells[i] = [ch]
        self.styles[i] = style
        self.col += 1
        if self.col > self.w:
            self.col = 1
            self.row += 1

    def clear_screen(self):
        for i in range(len(self.cells)):
            self.cells[i] = [" "]
            self.styles[i] = None

    def clear_line_from(self):
        for c in range(self.col, self.w + 1):
            i = self.idx(self.row, c)
            if i >= 0:
                self.cells[i] = [" "]
                self.styles[i] = None


# ══════════════════════════════════════════════════════
#  ANSI 解析器
# ══════════════════════════════════════════════════════

# ESC[ ... 终止符
ANSI_RE = re.compile(r"\x1b\[([\x30-\x3f]*)([\x20-\x2f]*)([\x40-\x7e])")
# \x1b]... BEL (OSC)
OSC_RE = re.compile(r"\x1b\][^\x07]*\x07")
# 单字符 ESC 序列（\x1b=、\x1b> 等）
SIMPLE_ESC_RE = re.compile(r"\x1b[=>NOMc]")


def parse_ansi(text, screen):
    """把含 ANSI 转义序列的字符串应用到 Screen 上。"""

    style = Style()
    pos = 0

    while pos < len(text):
        # 找下一个 ESC
        esc = text.find("\x1b", pos)
        if esc < 0:
            # 剩下的全是纯文本
            for ch in text[pos:]:
                screen.put(ch, style.copy())
            break

        # 输出 ESC 前的纯文本
        for ch in text[pos:esc]:
            screen.put(ch, style.copy())

        # 尝试匹配 CSI 序列
        m = ANSI_RE.match(text, esc)
        if m:
            params = m.group(1)
            intermediate = m.group(2)
            final = m.group(3)

            if final == "m":
                # SGR
                apply_sgr(style, params)
            elif final == "H" or final == "f":
                # 光标定位 \033[<r>;<c>H 或 \033[H
                if not params:
                    screen.row, screen.col = 1, 1
                else:
                    parts = params.split(";")
                    r = int(parts[0]) if parts[0] else 1
                    c = int(parts[1]) if len(parts) > 1 and parts[1] else 1
                    screen.row, screen.col = r, c
            elif final == "K":
                # 清行尾
                screen.clear_line_from()
            elif final == "J":
                # \033[2J 清屏
                if params == "2":
                    screen.clear_screen()
            elif final == "J" and params == "":
                screen.clear_screen()

            pos = m.end()
        else:
            # OSC 或简单 ESC
            m_osc = OSC_RE.match(text, esc)
            if m_osc:
                pos = m_osc.end()
                continue
            m_simple = SIMPLE_ESC_RE.match(text, esc)
            if m_simple:
                pos = m_simple.end()
                continue
            # 未知序列，跳过 ESC 字符
            pos = esc + 1

    return screen


def apply_sgr(style, params):
    """应用 SGR 参数到 Style。"""
    if not params:
        params = "0"

    codes = [int(c) if c else 0 for c in params.split(";")]

    i = 0
    while i < len(codes):
        c = codes[i]

        if c == 0:
            style.reset()
        elif c in FG_COLORS:
            style.fg = FG_COLORS[c]
        elif c in BG_COLORS:
            style.bg = BG_COLORS[c]
        elif c in ATTRS:
            style.css.append(ATTRS[c])
        elif c == 38 and i + 4 < len(codes):
            # 24-bit FG: 38;2;R;G;B
            r, g, b = codes[i+2], codes[i+3], codes[i+4]
            style.fg = f"rgb({r},{g},{b})"
            i += 4
        elif c == 48 and i + 4 < len(codes):
            r, g, b = codes[i+2], codes[i+3], codes[i+4]
            style.bg = f"rgb({r},{g},{b})"
            i += 4
        elif c == 39:
            style.fg = None
        elif c == 49:
            style.bg = None
        # 其他不识别的 SGR 忽略

        i += 1


class Style:
    """当前文本样式状态。"""

    __slots__ = ("fg", "bg", "css")

    def __init__(self):
        self.fg = None
        self.bg = None
        self.css = []

    def reset(self):
        self.fg = None
        self.bg = None
        self.css = []

    def copy(self):
        s = Style()
        s.fg = self.fg
        s.bg = self.bg
        s.css = list(self.css)
        return s

    def css_inline(self):
        """生成 CSS inline 字符串。"""
        parts = []
        if self.bg:
            parts.append(f"background-color:{self.bg}")
        if self.fg:
            parts.append(f"color:{self.fg}")
        parts.extend(self.css)
        return ";".join(parts) if parts else ""


# ══════════════════════════════════════════════════════
#  Screen → HTML
# ══════════════════════════════════════════════════════

def screen_to_html(screen, title="TUI Capture"):
    """把 Screen 渲染为 HTML。"""

    lines = []
    for r in range(screen.h):
        line_cells = []
        for c in range(screen.w):
            i = screen.idx(r + 1, c + 1)
            ch_tuple = screen.cells[i]
            ch = ch_tuple[0] if ch_tuple else " "
            style = screen.styles[i]

            if style and (style.css_inline() or style.bg or style.fg):
                css = style.css_inline()
                line_cells.append(f'<span style="{css}">{html_escape(ch) if ch != " " else "&nbsp;"}</span>')
            else:
                line_cells.append(html_escape(ch) if ch != " " else "&nbsp;")

        lines.append("".join(line_cells).rstrip())

    body = "<br>\n".join(lines)
    # 去掉尾部空行
    while body.endswith("<br>"):
        body = body[:-4]

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>{html_escape(title)}</title>
<style>
body {{
    background: #0d0d1a;
    color: #e4e4e7;
    font-family: 'JetBrains Mono', 'Fira Code', 'SF Mono', Consolas, monospace;
    font-size: 14px;
    line-height: 1.2;
    padding: 20px;
    margin: 0;
}}
.title {{
    color: #4ade80;
    font-size: 18px;
    margin-bottom: 12px;
    font-weight: bold;
}}
.terminal {{
    background: #000;
    border: 2px solid #4ade80;
    border-radius: 8px;
    padding: 16px;
    display: inline-block;
    min-width: 80ch;
    box-shadow: 0 0 24px rgba(74, 222, 128, 0.15);
}}
</style>
</head>
<body>
<div class="title">🖥️ {html_escape(title)}</div>
<div class="terminal">
{body}
</div>
</body>
</html>
"""


def screen_to_text(screen):
    """把 Screen 渲染为纯文本（去掉 ANSI + 样式），供 Agent 分析布局。"""

    lines = []
    for r in range(screen.h):
        line_cells = []
        for c in range(screen.w):
            i = screen.idx(r + 1, c + 1)
            ch_tuple = screen.cells[i]
            ch = ch_tuple[0] if ch_tuple else " "
            line_cells.append(ch if ch else " ")
        lines.append("".join(line_cells).rstrip())

    # 去掉尾部空行
    while lines and not lines[-1].strip():
        lines.pop()

    return "\n".join(lines)


# ══════════════════════════════════════════════════════
#  CLI 入口
# ══════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="ANSI escape → HTML/text")
    parser.add_argument("--input", required=True, help="含 ANSI 的输入文件")
    parser.add_argument("--output", required=True, help="输出文件（或 stdout）")
    parser.add_argument("--title", default="TUI Capture", help="HTML 标题")
    parser.add_argument("--width", type=int, default=120)
    parser.add_argument("--height", type=int, default=40)
    parser.add_argument("--text-only", action="store_true", help="只输出纯文本（不含 ANSI/HTML）")
    args = parser.parse_args()

    with open(args.input, "rb") as f:
        raw = f.read().decode("utf-8", errors="replace")

    screen = Screen(width=args.width, height=args.height)
    parse_ansi(raw, screen)

    if args.text_only:
        out = screen_to_text(screen)
    else:
        out = screen_to_html(screen, title=args.title)

    if args.output == "/dev/stdout" or args.output == "-":
        sys.stdout.write(out)
        if not out.endswith("\n"):
            sys.stdout.write("\n")
    else:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(out)


if __name__ == "__main__":
    main()
