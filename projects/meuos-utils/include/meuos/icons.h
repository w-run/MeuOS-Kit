/* meuos/icons.h — 文件类型图标
 *
 * 三级图标体系：
 *   ASCII   — 最简回退：/  @  *  (ls -F 风格)
 *   Unicode — 基础 emoji，大多数终端支持：📁 📄 🔗 ⚙️
 *   Nerd    — Nerd Font 字形，最美观
 *
 * 默认根据终端能力自动选择。
 */
#ifndef MEUOS_ICONS_H
#define MEUOS_ICONS_H

#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ICON_SET_ASCII = 0,   /* 回退到 ASCII:  / @ * |  */
    ICON_SET_UNICODE,     /* Unicode emoji: 📁 📄 🔗  */
    ICON_SET_NERD,        /* Nerd Font:    󰉋 󰈔 󰈭     */
} icon_set_t;

/* 设置全局字体集。返回之前的状态。 */
icon_set_t icon_set(icon_set_t s);

/* 自动检测最佳图标集（Nerd > Unicode > ASCII）。 */
int icon_detect_nerd(void);
int icon_detect_unicode(void);

/* 自动选择最佳图标集。 */
icon_set_t icon_auto_detect(void);

/* 文件类型 → 图标字符串。
 * mode 是 stat.st_mode；is_link 表示是符号链接。 */
const char *icon_for_mode(mode_t mode, int is_link);

/* 文件类型标签（用于 ls -l 详细样式） */
const char *icon_type_label(mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_ICONS_H */
