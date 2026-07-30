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
    _is_wide,
)


# ══════════════════════════════════════════════════════
#  字体加载
#
#  三种角色（与 ansi2html.py 的 CSS 字体栈保持一致）：
#    - mono（MonaSpace）    等宽，用于 TUI 网格（ASCII / box-drawing）
#    - cjk  （NotoSansCJK） 等宽 CJK，覆盖 CJK / 全角字符
#    - proportional（MonaSans） 比例字体，可用于 HTML 标题、PNG 页眉
# ══════════════════════════════════════════════════════

# MonaSpace 五种 voice；优先 Neon（默认、最接近经典编程字体）
_MONOSPACE_CANDIDATES = [
    # MonaSpace v1.400（GitHub Next 等宽字体超级家族）
    "/usr/share/fonts/monaspace/Monaspace Neon Var.ttf",
    "/usr/share/fonts/monaspace/Monaspace Argon Var.ttf",
    "/usr/share/fonts/monaspace/Monaspace Xenon Var.ttf",
    "/usr/share/fonts/monaspace/Monaspace Radon Var.ttf",
    "/usr/share/fonts/monaspace/Monaspace Krypton Var.ttf",
    # 备用：Mona Sans 的 mono 可变字体
    "/usr/share/fonts/mona-sans/MonaSansMonoVF[wdth,wght].ttf",
    "/usr/share/fonts/mona-sans/MonaSansMonoVF[wght].ttf",
    # 通用回退
    "/usr/share/fonts/google-noto-sans-mono-cjk-vf-fonts/NotoSansMonoCJK-VF.ttc",
    "/usr/share/fonts/redhat-vf/RedHatMono[wght].ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
    "/usr/share/fonts/gnu-free/FreeMono.otf",
    "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf",
]

_CJK_CANDIDATES = [
    # Noto Sans Mono CJK（等宽，渲染 CJK + 符号都对齐到 1 个 ASCII 列宽）
    "/usr/share/fonts/google-noto-sans-mono-cjk-vf-fonts/NotoSansMonoCJK-VF.ttc",
    # Noto Sans CJK（非等宽，回退）
    "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc",
    "/usr/share/fonts/chinese/NotoSansCJK-VF.ttc",
    "/usr/share/fonts/wqy-zenhei-fonts/wqy-zenhei.ttc",
    "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
]

# Mona Sans v2.0.27（GitHub 比例字体）
_PROPORTIONAL_CANDIDATES = [
    "/usr/share/fonts/mona-sans/MonaSansVF[wdth,wght,opsz,ital].ttf",
    "/usr/share/fonts/mona-sans/MonaSansVF[opsz,wght].ttf",
    "/usr/share/fonts/google-noto-sans-vf/NotoSansVF[wdth,wght].ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
]


def _first_existing(candidates):
    """返回候选列表中第一个实际存在的路径；都不存在则返回 None。"""
    for p in candidates:
        if os.path.exists(p):
            return p
    return None


def find_mono():
    """找一个等宽字体（用于 TUI 网格 ASCII/box-drawing 字符）。"""
    return _first_existing(_MONOSPACE_CANDIDATES)


def find_cjk():
    """找一个 CJK 字体（用于中日韩/全角字符）。"""
    return _first_existing(_CJK_CANDIDATES)


def find_proportional():
    """找一个比例字体（用于标题/页眉等非网格文本）。"""
    return _first_existing(_PROPORTIONAL_CANDIDATES)


def _load(path, size, prefer_index_zero=False, weight="Regular"):
    """统一加载入口：.ttc 自动指定 index=0，其它直接加载。失败则降级。

    weight: 变体字重名（'Regular' / 'Medium' / 'Bold' / ...）
            仅对可变字体（VF）生效。设为 None 保持默认。
    """
    if not path:
        return ImageFont.load_default()
    try:
        if path.endswith(".ttc") or (prefer_index_zero and "VF" in path and path.endswith(".ttf")):
            # PIL 对带 OpenType Variation 的 .ttf 也能用 index=0
            font = ImageFont.truetype(path, size)
        else:
            font = ImageFont.truetype(path, size)
        # 设置字重（仅可变字体支持）
        if weight is not None and ("Var" in path or "VF" in path):
            try:
                font.set_variation_by_name(weight)
            except Exception:
                pass
        return font
    except Exception as e:
        print(f"⚠️  加载字体失败 {path}: {e}", file=sys.stderr)
        return ImageFont.load_default()


def load_mono(size=14, weight="Regular"):
    """加载等宽字体（MonaSpace）。

    weight: Regular / Medium / Bold / ... 对可变字体生效。
    """
    return _load(find_mono(), size, weight=weight)


def load_cjk(size=14):
    """加载 CJK 字体。.ttc 用 index=0 拿第一个子字体（SC）。"""
    path = find_cjk()
    if not path:
        return ImageFont.load_default()
    try:
        if path.endswith(".ttc"):
            return ImageFont.truetype(path, size, index=0)
        return ImageFont.truetype(path, size)
    except Exception as e:
        print(f"⚠️  加载 CJK 字体失败 {path}: {e}", file=sys.stderr)
        return ImageFont.load_default()


def load_proportional(size=14):
    """加载比例字体（MonaSans），使用 Regular 字重。"""
    return _load(find_proportional(), size, weight="Regular")


# 保留旧 API 兼容（find_font / load_font）
def find_font(cjk=False):
    """兼容旧 API：cjk=True → CJK 字体，否则 → 等宽字体。"""
    return find_cjk() if cjk else find_mono()


def load_font(size=14, cjk=False):
    """兼容旧 API。"""
    return load_cjk(size) if cjk else load_mono(size)


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
    """把 Screen 状态光栅化为 PNG 图像。

    字体分工：
      - font       MonaSpace  （等宽）用于 ASCII / box-drawing
      - cjk_font   NotoSansCJK 覆盖 CJK / 全角字符
      - prop_font  MonaSans  （比例）备用，可用于页眉等
    """

    def __init__(self, screen, theme_bg="#0d0d1a",
                 font_size=14, char_w=None, char_h=None):
        self.screen = screen
        self.theme_bg = parse_color(theme_bg) or (13, 13, 26)
        self.font_size = font_size
        self.font = load_mono(font_size, weight="Regular")
        self.font_bold = load_mono(font_size, weight="Bold")
        self.cjk_font = load_cjk(font_size)
        self.prop_font = load_proportional(font_size)

        # 主题色
        self.default_fg = (228, 228, 231)   # #e4e4e7
        self.default_bg = self.theme_bg

        # 自动测量字符尺寸：等宽字体里 M 永远是 1 个 cell 宽
        tmp = Image.new("RGB", (100, 100), self.theme_bg)
        drw = ImageDraw.Draw(tmp)
        bbox = drw.textbbox((0, 0), "M", font=self.font)
        self.char_w = char_w or (bbox[2] - bbox[0])
        self.char_h = char_h or int(font_size * 1.4)

    def _is_bold(self, style):
        """判断 style 是否包含 ANSI bold 属性。"""
        if not style or not style.css:
            return False
        return any("font-weight:bold" in s for s in style.css)

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

                fg, bg = self.cell_color(style, with_bg=True)

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

                # 块元素字符用 ImageDraw.rectangle 填满整个 cell
                # 避免字体字形宽度 < cell 宽度导致的"碎片化"
                cp = ord(ch[0])
                is_block = (
                    0x2580 <= cp <= 0x259F or  # Block Elements
                    0x25A0 <= cp <= 0x25FF or  # Geometric Shapes (■▣▤▥▦▧▨▩▪▫◆◼◾▮▰)
                    cp == 0x2588  # Full block █
                )
                if is_block:
                    # 估算字形在 cell 中的对齐偏移
                    font_for_measure = self.font_bold if self._is_bold(style) else self.font
                    try:
                        bbox = draw.textbbox((0, 0), ch, font=font_for_measure)
                        gw = bbox[2] - bbox[0]
                        gh = bbox[3] - bbox[1]
                        # 让块填满整个 cell（不留间隙）
                        if ch in ("█", "▮", "▰", "■", "▣", "◼", "◾"):
                            draw.rectangle(
                                [x, y, x + self.char_w - 1, y + self.char_h - 1],
                                fill=fg,
                            )
                        elif ch in ("▀",):
                            # Upper half block
                            draw.rectangle(
                                [x, y, x + self.char_w - 1, y + self.char_h // 2],
                                fill=fg,
                            )
                        elif ch in ("▄",):
                            # Lower half block
                            draw.rectangle(
                                [x, y + self.char_h // 2, x + self.char_w - 1, y + self.char_h - 1],
                                fill=fg,
                            )
                        elif ch in ("▌",):
                            # Left half block
                            draw.rectangle(
                                [x, y, x + self.char_w // 2, y + self.char_h - 1],
                                fill=fg,
                            )
                        elif ch in ("▐",):
                            # Right half block
                            draw.rectangle(
                                [x + self.char_w // 2, y, x + self.char_w - 1, y + self.char_h - 1],
                                fill=fg,
                            )
                        else:
                            # 其它块字符：用 text 渲染
                            draw.text((x, y), ch, fill=fg, font=font_for_measure)
                    except Exception:
                        # 退化：填充整个 cell
                        draw.rectangle(
                            [x, y, x + self.char_w - 1, y + self.char_h - 1],
                            fill=fg,
                        )
                    continue

                # CJK/全角用 cjk_font；其它等宽字符用 mono（MonaSpace）
                # bold 文本使用 Bold 字重，确保标题/强调文字清晰可见
                is_cjk = _is_wide(ch)
                if is_cjk:
                    font = self.cjk_font
                elif self._is_bold(style):
                    font = self.font_bold
                else:
                    font = self.font

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
