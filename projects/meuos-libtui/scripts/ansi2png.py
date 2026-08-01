#!/usr/bin/env python3
"""
ansi2png.py — ANSI/VT100 转义序列 → PNG 图像

复用 ansi2html.py 的 Screen 模型，把屏幕状态光栅化为 PNG。
支持 16 色 ANSI + 24-bit truecolor + 粗体/下划线 + CJK 字符。
默认输出 meuos-dark 主题（黑底 + 绿色调）。

用法:
  ansi2png.py --input RAW --output PNG [--theme meuos-dark] [--font-size N]
"""

import argparse
import os
import sys
import unicodedata

from PIL import Image, ImageDraw, ImageFont

# 复用 ansi2html.py 的 ANSI 解析
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ansi2html import (
    parse_ansi, Screen,
    FG_COLORS, BG_COLORS, ATTRS,
)


# ══════════════════════════════════════════════════════
#  字体加载
# ══════════════════════════════════════════════════════

def find_font(cjk=False):
    """找一个等宽字体（优先 CJK）。"""
    candidates = [
        # CJK 优先
        "/usr/share/fonts/google-noto-sans-mono-cjk-vf-fonts/NotoSansMonoCJK-VF.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
        # 通用 mono
        "/usr/share/fonts/redhat-vf/RedHatMono[wght].ttf",
        "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
        "/usr/share/fonts/gnu-free/FreeMono.otf",
        # 系统默认
        "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf",
    ]

    for path in candidates:
        if os.path.exists(path):
            return path

    # 最后手段：让 PIL 找默认字体
    return None


def load_font(size=14, cjk=False):
    """加载字体，必要时用 CJK 字体覆盖。"""
    font_path = find_font(cjk=cjk)
    if font_path:
        try:
            # CJK 字体的 .ttc 需要指定 index
            if font_path.endswith(".ttc") and "CJK" in font_path:
                return ImageFont.truetype(font_path, size, index=0)
            return ImageFont.truetype(font_path, size)
        except Exception as e:
            print(f"⚠️  加载字体失败 {font_path}: {e}", file=sys.stderr)

    # fallback
    return ImageFont.load_default()


# ══════════════════════════════════════════════════════
#  颜色处理
# ══════════════════════════════════════════════════════

def parse_color(s):
    """CSS 颜色字符串 → (r,g,b)。支持 hex (#rrggbb)。"""
    if s is None:
        return None
    s = s.strip()
    if s.startswith("#") and len(s) == 7:
        try:
            return (int(s[1:3], 16), int(s[3:5], 16), int(s[5:7], 16))
        except ValueError:
            return None
    if s.startswith("rgb(") and s.endswith(")"):
        try:
            parts = s[4:-1].split(",")
            return (int(parts[0]), int(parts[1]), int(parts[2]))
        except (ValueError, IndexError):
            return None
    return None


# ══════════════════════════════════════════════════════
#  渲染
# ══════════════════════════════════════════════════════

class PNGRenderer:
    """把 Screen 状态光栅化为 PNG 图像。"""

    def __init__(self, screen, theme_bg="#0d0d1a",
                 font_size=14, char_w=None, char_h=None):
        self.screen = screen
        self.theme_bg = parse_color(theme_bg) or (13, 13, 26)
        self.font_size = font_size
        self.font = load_font(font_size, cjk=False)
        self.cjk_font = load_font(font_size, cjk=True)

        # 自动测量字符尺寸
        tmp = Image.new("RGB", (100, 100), self.theme_bg)
        drw = ImageDraw.Draw(tmp)
        bbox = drw.textbbox((0, 0), "M", font=self.font)
        self.char_w = char_w or (bbox[2] - bbox[0])
        self.char_h = char_h or int(font_size * 1.4)

        # 主题色
        self.default_fg = (228, 228, 231)   # #e4e4e7
        self.default_bg = self.theme_bg

    def cell_color(self, style, with_bg=True):
        """根据 style 解析出 (fg, bg)。"""
        fg = parse_color(style.fg) if (style and style.fg) else self.default_fg
        bg = parse_color(style.bg) if (style and style.bg) else None

        if with_bg and bg is None:
            bg = self.default_bg

        # 处理属性
        attrs = list(style.css) if style else []
        dim = False
        for a in attrs:
            if "opacity:0.6" in a:
                dim = True
            if "filter:invert(100%)" in a:
                fg, bg = bg, fg
                if bg is None:
                    bg = self.default_fg
                if fg is None:
                    fg = self.default_fg

        if dim and fg:
            fg = (int(fg[0] * 0.6), int(fg[1] * 0.6), int(fg[2] * 0.6))

        return fg, bg

    def char_width_in_cells(self, ch):
        """返回字符占几个单元格（CJK = 2）。"""
        if not ch or ch == " ":
            return 1
        cp = ord(ch[0])
        # 检查 CJK 范围（粗略）
        if (0x1100 <= cp <= 0x115F or
            0x2E80 <= cp <= 0x303E or
            0x3040 <= cp <= 0x309F or
            0x30A0 <= cp <= 0x30FF or
            0x3100 <= cp <= 0x31FF or
            0x3200 <= cp <= 0x33FF or
            0x3400 <= cp <= 0x4DBF or
            0x4E00 <= cp <= 0x9FFF or
            0xAC00 <= cp <= 0xD7AF or
            0xF900 <= cp <= 0xFAFF or
            0xFE30 <= cp <= 0xFE6F or
            0xFF01 <= cp <= 0xFF60 or
            0xFFE0 <= cp <= 0xFFE6 or
            0x20000 <= cp <= 0x2FFFF):
            return 2
        return 1

    def render(self):
        """生成 PNG 图像。"""
        w = self.screen.w * self.char_w
        h = self.screen.h * self.char_h

        img = Image.new("RGB", (w + 8, h + 8), self.theme_bg)
        draw = ImageDraw.Draw(img)

        for r in range(self.screen.h):
            for c in range(self.screen.w):
                idx = r * self.screen.w + c
                ch_tuple = self.screen.cells[idx]
                ch = ch_tuple[0] if ch_tuple else " "
                style = self.screen.styles[idx]

                fg, bg = self.cell_color(style, with_bg=False)

                x = c * self.char_w + 4
                y = r * self.char_h + 4

                # 背景填充（覆盖前一个字符的残留）
                if bg and bg != self.theme_bg:
                    draw.rectangle(
                        [x, y, x + self.char_w, y + self.char_h],
                        fill=bg
                    )

                # 空格 + CJK 双宽字符：可能占用 2 个单元
                # 这里不画空格（背景已覆盖），只画非空字符
                if not ch or ch == " ":
                    continue
                # 跳过 NUL / 控制字符
                if ord(ch[0]) < 0x20 and ch != " ":
                    continue
                # 检查 CJK 是否需要 2 格
                is_cjk = self.char_width_in_cells(ch) == 2

                font = self.cjk_font if is_cjk else self.font

                try:
                    draw.text((x, y), ch, fill=fg, font=font)
                except Exception:
                    # 字体不支持的字符，用方块代替
                    draw.rectangle(
                        [x + 1, y + 1, x + self.char_w - 2, y + self.char_h - 2],
                        outline=fg, width=1
                    )

        return img


# ══════════════════════════════════════════════════════
#  CLI
# ══════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="ANSI escape → PNG")
    parser.add_argument("--input", required=True, help="ANSI 输入文件")
    parser.add_argument("--output", required=True, help="PNG 输出文件")
    parser.add_argument("--width", type=int, default=80)
    parser.add_argument("--height", type=int, default=30)
    parser.add_argument("--font-size", type=int, default=14)
    parser.add_argument("--scale", type=int, default=1, help="放大倍数")
    parser.add_argument("--theme-bg", default="#0d0d1a")
    args = parser.parse_args()

    with open(args.input, "rb") as f:
        raw = f.read().decode("utf-8", errors="replace")

    screen = Screen(width=args.width, height=args.height)
    parse_ansi(raw, screen)

    renderer = PNGRenderer(
        screen,
        theme_bg=args.theme_bg,
        font_size=args.font_size,
    )
    img = renderer.render()

    if args.scale > 1:
        new_w = img.width * args.scale
        new_h = img.height * args.scale
        img = img.resize((new_w, new_h), Image.NEAREST)

    if args.output == "-":
        img.save("/dev/stdout", "PNG")
    else:
        img.save(args.output, "PNG")
        print(f"✅ PNG 保存: {args.output} ({img.width}x{img.height})", file=sys.stderr)


if __name__ == "__main__":
    main()
