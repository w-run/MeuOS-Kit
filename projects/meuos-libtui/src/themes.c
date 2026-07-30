/* themes.c — 主题系统
 *
 * 每个主题定义完整的视觉风格：16 色调色板 + 24-bit 装饰色 + 渐变。
 * 默认主题是 MeuOS 绿调；可用 tui_set_theme() 切换。
 */

#include "meuos/libtui.h"

/* ══════════════════════════════════════════════════════
 *  主题 1: MeuOS（默认，柔和绿调）
 * ══════════════════════════════════════════════════════ */

const tui_theme_t tui_theme_meuos = {
    .name = "MeuOS",
    .palette = {
        .accent    = TUI_COLOR_GREEN,
        .bg        = TUI_COLOR_DEFAULT,
        .fg        = TUI_COLOR_DEFAULT,
        .border    = TUI_COLOR_GREEN,
        .highlight = TUI_COLOR_GREEN,
        .dim       = TUI_COLOR_BLACK,
        .success   = TUI_COLOR_GREEN,
        .warning   = TUI_COLOR_YELLOW,
        .error     = TUI_COLOR_RED,
        .info      = TUI_COLOR_CYAN,
    },
    .surface_bg     = { 13,  17,  23},   /* #0d1117 */
    .surface_fg     = {230, 237, 243},
    .header_bg      = { 46, 160,  67},   /* #2ea043 MeuOS 绿 */
    .header_fg      = {255, 255, 255},
    .statusbar_bg   = { 33,  38,  45},
    .statusbar_fg   = {240, 246, 252},
    .gradient = {
        {139,  92, 246},   /* 紫 */
        { 56, 189, 248},   /* 天蓝 */
        { 34, 211, 238},   /* 青 */
        { 74, 222, 128},   /* 亮绿 */
        { 46, 160,  67},   /* MeuOS 绿 */
        { 22, 101,  52},   /* 深绿 */
    },
    .selection_bg   = { 46,  92,  50},
    .selection_fg   = {240, 253, 244},
    .zebra_bg       = { 22,  27,  34},
    .use_24bit      = 1,
};

/* ══════════════════════════════════════════════════════
 *  主题 2: Solarized Dark
 *  经典暖色低对比度配色
 * ══════════════════════════════════════════════════════ */

const tui_theme_t tui_theme_solarized = {
    .name = "Solarized",
    .palette = {
        .accent    = TUI_COLOR_CYAN,
        .bg        = TUI_COLOR_DEFAULT,
        .fg        = TUI_COLOR_DEFAULT,
        .border    = TUI_COLOR_CYAN,
        .highlight = TUI_COLOR_YELLOW,
        .dim       = TUI_COLOR_BLACK,
        .success   = TUI_COLOR_GREEN,
        .warning   = TUI_COLOR_YELLOW,
        .error     = TUI_COLOR_RED,
        .info      = TUI_COLOR_CYAN,
    },
    .surface_bg     = {  0,  43,  54},   /* #002b36 base03 */
    .surface_fg     = {147, 161, 161},   /* #93a1a1 base1 */
    .header_bg      = {  0,  60,  72},
    .header_fg      = {253, 246, 227},   /* base3 近白，确保可读 */
    .statusbar_bg   = {  7,  54,  66},
    .statusbar_fg   = {147, 161, 161},
    .gradient = {
        {220,  50,  47},   /* 红色 red */
        {203,  75,  22},   /* 橙 */
        {181, 137,   0},   /* 黄 */
        {133, 153,   0},   /* 绿 */
        { 38, 139, 210},   /* 蓝 */
        {108, 113, 196},   /* 紫 */
    },
    .selection_bg   = {  7,  54,  66},
    .selection_fg   = {253, 246, 227},
    .zebra_bg       = {  0,  50,  62},
    .use_24bit      = 1,
};

/* ══════════════════════════════════════════════════════
 *  主题 3: Nord
 *  极地蓝灰调，冷色低饱和度
 * ══════════════════════════════════════════════════════ */

const tui_theme_t tui_theme_nord = {
    .name = "Nord",
    .palette = {
        .accent    = TUI_COLOR_CYAN,
        .bg        = TUI_COLOR_DEFAULT,
        .fg        = TUI_COLOR_DEFAULT,
        .border    = TUI_COLOR_CYAN,
        .highlight = TUI_COLOR_BLUE,
        .dim       = TUI_COLOR_BLACK,
        .success   = TUI_COLOR_GREEN,
        .warning   = TUI_COLOR_YELLOW,
        .error     = TUI_COLOR_RED,
        .info      = TUI_COLOR_CYAN,
    },
    .surface_bg     = { 46,  52,  64},   /* #2e3440 nord0 */
    .surface_fg     = {216, 222, 233},   /* #d8dee9 nord6 */
    .header_bg      = { 59,  66,  82},   /* #3b4252 nord1 */
    .header_fg      = {255, 255, 255},   /* 纯白，确保可读 */
    .statusbar_bg   = { 59,  66,  82},
    .statusbar_fg   = {229, 233, 240},
    .gradient = {
        {191,  97, 106},  /* nord11 红 */
        {208, 135, 112},  /* nord12 橙 */
        {235, 203, 139},  /* nord13 黄 */
        {163, 190, 140},  /* nord14 绿 */
        {129, 161, 193},  /* nord10 冰蓝 */
        {180, 142, 173},  /* nord15 紫 */
    },
    .selection_bg   = { 67,  76,  94},   /* nord3 */
    .selection_fg   = {236, 239, 244},
    .zebra_bg       = { 52,  59,  72},
    .use_24bit      = 1,
};

/* ══════════════════════════════════════════════════════
 *  主题 4: Catppuccin Mocha
 *  暖色高饱和度，柔和对比
 * ══════════════════════════════════════════════════════ */

const tui_theme_t tui_theme_catppuccin = {
    .name = "Catppuccin",
    .palette = {
        .accent    = TUI_COLOR_MAGENTA,
        .bg        = TUI_COLOR_DEFAULT,
        .fg        = TUI_COLOR_DEFAULT,
        .border    = TUI_COLOR_MAGENTA,
        .highlight = TUI_COLOR_MAGENTA,
        .dim       = TUI_COLOR_BLACK,
        .success   = TUI_COLOR_GREEN,
        .warning   = TUI_COLOR_YELLOW,
        .error     = TUI_COLOR_RED,
        .info      = TUI_COLOR_CYAN,
    },
    .surface_bg     = { 30,  30,  46},   /* #1e1e2e base */
    .surface_fg     = {205, 214, 244},   /* #cdd6f4 text */
    .header_bg      = { 49,  50,  68},   /* #313244 surface1 */
    .header_fg      = {255, 255, 255},   /* 白色，确保在深色 header 上可读 */
    .statusbar_bg   = { 49,  50,  68},
    .statusbar_fg   = {186, 194, 222},   /* subtext1 */
    .gradient = {
        {243, 139, 168},  /* #f38ba8 red */
        {250, 179, 135},  /* #fab387 peach */
        {249, 226, 175},  /* #f9e2af yellow */
        {166, 227, 161},  /* #a6e3a1 green */
        {137, 220, 235},  /* #89dceb sky */
        {203, 166, 247},  /* #cba6f7 mauve */
    },
    .selection_bg   = { 69,  71,  90},   /* overlay0 */
    .selection_fg   = {205, 214, 244},
    .zebra_bg       = { 36,  36,  52},
    .use_24bit      = 1,
};

/* ══════════════════════════════════════════════════════
 *  主题 5: Cyberpunk
 *  霓虹粉/青/黄，强对比
 * ══════════════════════════════════════════════════════ */

const tui_theme_t tui_theme_cyberpunk = {
    .name = "Cyberpunk",
    .palette = {
        .accent    = TUI_COLOR_MAGENTA,
        .bg        = TUI_COLOR_DEFAULT,
        .fg        = TUI_COLOR_DEFAULT,
        .border    = TUI_COLOR_MAGENTA,
        .highlight = TUI_COLOR_CYAN,
        .dim       = TUI_COLOR_BLACK,
        .success   = TUI_COLOR_GREEN,
        .warning   = TUI_COLOR_YELLOW,
        .error     = TUI_COLOR_RED,
        .info      = TUI_COLOR_CYAN,
    },
    .surface_bg     = { 12,  12,  28},   /* 深蓝黑 */
    .surface_fg     = {255, 230, 240},
    .header_bg      = {255,   0, 110},   /* 霓虹粉 */
    .header_fg      = {  0,   0,   0},
    .statusbar_bg   = {  0, 240, 255},   /* 霓虹青 */
    .statusbar_fg   = { 12,  12,  28},
    .gradient = {
        {255,   0, 110},  /* 霓虹粉 */
        {255, 100, 200},
        {255, 200, 100},  /* 霓虹黄 */
        {100, 255, 200},  /* 霓虹绿 */
        {  0, 240, 255},  /* 霓虹青 */
        {180,   0, 255},  /* 霓虹紫 */
    },
    .selection_bg   = {255,   0, 110},
    .selection_fg   = {  0,   0,   0},
    .zebra_bg       = { 22,  18,  40},
    .use_24bit      = 1,
};

/* ══════════════════════════════════════════════════════
 *  主题 6: Mono (极简)
 *  无颜色，仅靠粗体/反白区分，兼容最差终端
 * ══════════════════════════════════════════════════════ */

const tui_theme_t tui_theme_mono = {
    .name = "Mono",
    .palette = {
        .accent    = TUI_COLOR_WHITE,
        .bg        = TUI_COLOR_DEFAULT,
        .fg        = TUI_COLOR_WHITE,
        .border    = TUI_COLOR_WHITE,
        .highlight = TUI_COLOR_WHITE,
        .dim       = TUI_COLOR_BLACK,
        .success   = TUI_COLOR_WHITE,
        .warning   = TUI_COLOR_WHITE,
        .error     = TUI_COLOR_WHITE,
        .info      = TUI_COLOR_WHITE,
    },
    .surface_bg     = {  0,   0,   0},
    .surface_fg     = {229, 229, 229},
    .header_bg      = {229, 229, 229},
    .header_fg      = {  0,   0,   0},
    .statusbar_bg   = { 40,  40,  40},
    .statusbar_fg   = {229, 229, 229},
    .gradient = {
        {200, 200, 200},
        {180, 180, 180},
        {160, 160, 160},
        {140, 140, 140},
        {120, 120, 120},
        {100, 100, 100},
    },
    .selection_bg   = {229, 229, 229},
    .selection_fg   = {  0,   0,   0},
    .zebra_bg       = { 20,  20,  20},
    .use_24bit      = 0,
};

/* ══════════════════════════════════════════════════════
 *  主题列表 / 切换
 * ══════════════════════════════════════════════════════ */

const tui_theme_t * const tui_themes[] = {
    &tui_theme_meuos,
    &tui_theme_solarized,
    &tui_theme_nord,
    &tui_theme_catppuccin,
    &tui_theme_cyberpunk,
    &tui_theme_mono,
};
const int tui_themes_count = (int)(sizeof(tui_themes) / sizeof(tui_themes[0]));

static const tui_theme_t *current_theme = &tui_theme_meuos;

const tui_theme_t *tui_set_theme(const tui_theme_t *t)
{
    if (!t) return current_theme;
    current_theme = t;
    return current_theme;
}

const tui_theme_t *tui_theme_current(void)
{
    return current_theme;
}

const tui_theme_t *tui_theme_by_name(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < tui_themes_count; i++) {
        const tui_theme_t *t = tui_themes[i];
        if (t && t->name) {
            /* 大小写不敏感匹配 */
            const char *a = name, *b = t->name;
            while (*a && *b) {
                char ca = *a, cb = *b;
                if (ca >= 'A' && ca <= 'Z') ca += 32;
                if (cb >= 'A' && cb <= 'Z') cb += 32;
                if (ca != cb) break;
                a++; b++;
            }
            if (!*a && !*b) return t;
        }
    }
    return NULL;
}
