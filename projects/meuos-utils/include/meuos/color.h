/* meuos/color.h — 24-bit ANSI 颜色与样式
 *
 * 设计原则：
 *   - 默认开颜色（检测 tty + 无 NO_COLOR 环境变量）
 *   - 24-bit 真彩色（capability 检测）
 *   - 256 色降级回退（终端不支持真彩时）
 *   - 模块化样式：bold/dim/italic/underline/invert
 */
#ifndef MEUOS_COLOR_H
#define MEUOS_COLOR_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 全局开关：通过 color_enable()/color_disable() 控制 */
extern int color_enabled;

/* 检测当前 stdout 是否支持 24-bit 真彩（"truecolor"）。
 * 根据 COLORTERM / TERM 环境变量判断，保守输出。 */
int color_detect_truecolor(void);

/* 检测 NO_COLOR 约定（https://no-color.org） */
int color_env_disabled(void);

/* 启用/禁用颜色输出 */
void color_enable(void);
void color_disable(void);

/* 应用颜色 + 样式到文件输出。
 * 用法：fputs(color_fg(COLOR_CYAN), fp); fputs("text", fp);
 *       fputs(color_reset(), fp); */
const char *color_fg(uint8_t r, uint8_t g, uint8_t b);   /* 24-bit fg */
const char *color_bg(uint8_t r, uint8_t g, uint8_t b);   /* 24-bit bg */
const char *color_named(int c);                            /* 0-15 named ANSI */
const char *color_256(uint8_t n);                          /* 256-color palette */
const char *color_reset(void);

/* 便捷宏与常用色 */
#define COLOR_RED_FG       color_named(1)
#define COLOR_GREEN_FG     color_named(2)
#define COLOR_YELLOW_FG    color_named(3)
#define COLOR_BLUE_FG      color_named(4)
#define COLOR_MAGENTA_FG   color_named(5)
#define COLOR_CYAN_FG      color_named(6)
#define COLOR_WHITE_FG     color_named(7)
#define COLOR_GRAY_FG      color_named(8)
#define COLOR_BRIGHT_RED   color_named(9)

/* 样式 */
#define STYLE_BOLD         "\033[1m"
#define STYLE_DIM          "\033[2m"
#define STYLE_ITALIC       "\033[3m"
#define STYLE_UNDERLINE    "\033[4m"
#define STYLE_INVERT       "\033[7m"

#include <sys/types.h>  /* mode_t */

/* 按 st_mode 选色：文件/目录/链接/可执行等 */
const char *color_for_mode(mode_t m);

/* MeuOS 默认主题色板 */
#define MEUOS_THEME_FG          color_named(7)       /* 主文本 */
#define MEUOS_THEME_ACCENT      color_named(2)       /* 绿色：MeuOS 主调 */
#define MEUOS_THEME_DIR         color_named(4)       /* 蓝色：目录 */
#define MEUOS_THEME_LINK        color_named(6)       /* 青色：符号链接 */
#define MEUOS_THEME_EXEC        color_named(2)       /* 绿色：可执行 */
#define MEUOS_THEME_ARCHIVE     color_named(3)       /* 黄色：归档 */
#define MEUOS_THEME_MEDIA       color_named(5)       /* 品红：媒体 */
#define MEUOS_THEME_TEXT        color_named(7)       /* 文本 */
#define MEUOS_THEME_CODE        color_named(6)       /* 代码 */
#define MEUOS_THEME_SOCKET      color_named(5)
#define MEUOS_THEME_PIPE        color_named(3)
#define MEUOS_THEME_DEVICE      color_named(3)
#define MEUOS_THEME_ERROR       color_named(9)       /* 红：错误 */

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_COLOR_H */
