/* libutils/icons.c — 文件类型图标
 *
 * ASCII 回退定义；Nerd Font 字符通过 icon_set(ICON_SET_NERD) 启用。
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <sys/stat.h>

#include "meuos/icons.h"

static icon_set_t current_set = ICON_SET_ASCII;

icon_set_t icon_set(icon_set_t s) {
    icon_set_t prev = current_set;
    current_set = s;
    return prev;
}

int icon_detect_nerd(void) {
    /* 通过 LANG/TERM 启发式；用户可通过 icon_set(ICON_SET_NERD) 强制 */
    const char *term = getenv("TERM");
    if (term && (strstr(term, "kitty") || strstr(term, "wezterm"))) return 1;
    const char *font = getenv("FONT");
    if (font && strstr(font, "Nerd")) return 1;
    return 0;
}

/* 文件名 → 短名 emoji/icon (UTF-8 Nerd Font) */
static const char *nerd_icon_for(const char *name) {
    /* 按扩展名 / 文件名（不区分大小写）匹配 */
    static const struct {
        const char *suffix;
        const char *icon;
    } table[] = {
        { ".c",      "" },        /*  */
        { ".h",      "" },
        { ".py",     "" },
        { ".rs",     "" },
        { ".go",     "" },
        { ".js",     "" },
        { ".ts",     "" },
        { ".sh",     "" },
        { ".md",     "" },
        { ".yaml",   "" },
        { ".yml",    "" },
        { ".json",   "" },
        { ".toml",   "" },
        { ".txt",    "" },
        { ".png",    "" },
        { ".jpg",    "" },
        { ".jpeg",   "" },
        { ".gif",    "" },
        { ".svg",    "" },
        { ".mp3",    "" },
        { ".mp4",    "" },
        { ".pdf",    "" },
        { ".zip",    "" },
        { ".tar",    "" },
        { ".gz",     "" },
        { ".tgz",    "" },
        { ".tar.gz", "" },
        { ".xz",     "" },
        { ".pdf",    "" },
        { ".tar.xz", "" },
        { NULL, NULL },
    };
    for (int i = 0; table[i].suffix; i++) {
        size_t sl = strlen(table[i].suffix);
        size_t nl = strlen(name);
        if (nl >= sl && strcasecmp(name + nl - sl, table[i].suffix) == 0) {
            return table[i].icon;
        }
    }
    return "";  /* 默认 Nerd 通用文件图标 */
}

static const char *nerd_for_mode(mode_t m, int is_link) {
    if (is_link) return "";
    if (S_ISDIR(m))  return "";
    if (S_ISLNK(m))  return "";
    if (S_ISFIFO(m)) return "";
    if (S_ISSOCK(m)) return "";
    if (S_ISBLK(m) || S_ISCHR(m)) return "";
    if (m & S_IXUSR)  return "";
    return "";
}

static const char *ascii_for_mode(mode_t m, int is_link) {
    if (is_link) return "@";    /* 符号链接 */
    if (S_ISDIR(m))  return "/";
    if (S_ISFIFO(m)) return "|";
    if (S_ISSOCK(m)) return "=";
    if (S_ISBLK(m))  return "b";
    if (S_ISCHR(m))  return "c";
    if (m & S_IXUSR)  return "*";
    return "";     /* 普通文件无图标 */
}

const char *icon_for_mode(mode_t mode, int is_link) {
    /* 文件名匹配需要 path，但不总是可用。我们先按 mode 给出基础图标，
     * 具体文件名增强由调用方通过 icon_for_filename() 实现。 */
    if (current_set == ICON_SET_NERD) {
        return nerd_for_mode(mode, is_link);
    }
    return ascii_for_mode(mode, is_link);
}

const char *icon_type_label(mode_t mode) {
    /* ls -l 首字符 */
    if (S_ISDIR(mode)) return "d";
    if (S_ISLNK(mode)) return "l";
    if (S_ISFIFO(mode)) return "p";
    if (S_ISSOCK(mode)) return "s";
    if (S_ISBLK(mode)) return "b";
    if (S_ISCHR(mode)) return "c";
    if ((mode & S_IXUSR) || (mode & S_IXGRP) || (mode & S_IXOTH)) return "*";
    return "-";
}
