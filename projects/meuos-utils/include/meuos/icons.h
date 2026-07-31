/* meuos/icons.h — 文件类型图标
 *
 * 设计原则：
 *   - 文件类型 → 图标映射
 *   - 优先 Nerd Font 字符（美观），回退 ASCII（兼容老终端）
 *   - 通过 icon_set()/icon_get() 切换字体集
 */
#ifndef MEUOS_ICONS_H
#define MEUOS_ICONS_H

#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ICON_SET_ASCII,      /* 回退到 ASCII:  /dir *exe >link  */
    ICON_SET_NERD,       /* Nerd Font:    󰉋 󰈔 󰈭                */
} icon_set_t;

/* 设置全局字体集。返回之前的状态。 */
icon_set_t icon_set(icon_set_t s);

/* 检测是否使用 Nerd Font（通过 LANG/TERM 启发式）。
 * 用户可在配置中强制覆盖。 */
int icon_detect_nerd(void);

/* 文件类型 → 图标（4 字节字符串）。
 * 必须用 4 字节数组接受返回值（如 char icon[5]）。
 * mode 是 stat.st_mode；is_link 表示是符号链接。 */
const char *icon_for_mode(mode_t mode, int is_link);

/* 文件类型标签（用于 ls -l 详细样式） */
const char *icon_type_label(mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_ICONS_H */
