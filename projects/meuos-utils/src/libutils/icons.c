/* libutils/icons.c — 文件类型图标
 *
 * 三级图标体系：ASCII → Unicode → Nerd Font
 * 根据终端能力自动选择最佳级别。
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <sys/stat.h>
#include <unistd.h>

#include "meuos/icons.h"

static icon_set_t current_set = ICON_SET_ASCII;

icon_set_t icon_set(icon_set_t s) {
    icon_set_t prev = current_set;
    current_set = s;
    return prev;
}

int icon_detect_nerd(void) {
    /* Nerd Font 环境变量检测 */
    const char *nf = getenv("MSH_NERD_FONT");
    if (nf && (strcmp(nf, "1") == 0 || strcmp(nf, "true") == 0)) return 1;
    const char *term = getenv("TERM");
    if (term && (strstr(term, "kitty") || strstr(term, "wezterm"))) return 1;
    const char *font = getenv("FONT");
    if (font && strstr(font, "Nerd")) return 1;
    return 0;
}

int icon_detect_unicode(void) {
    /* 检测终端是否支持 UTF-8 */
    const char *lang = getenv("LANG");
    if (lang && (strstr(lang, "UTF-8") || strstr(lang, "utf8"))) return 1;
    const char *lc = getenv("LC_ALL");
    if (lc && (strstr(lc, "UTF-8") || strstr(lc, "utf8"))) return 1;
    const char *term = getenv("TERM");
    if (term && strstr(term, "256color")) return 1;
    return 0;
}

icon_set_t icon_auto_detect(void) {
    if (icon_detect_nerd()) return ICON_SET_NERD;
    if (icon_detect_unicode()) return ICON_SET_UNICODE;
    return ICON_SET_ASCII;
}

/* === Nerd Font 图标 === */
static const char *nerd_icon_for(const char *name) {
    static const struct {
        const char *suffix;
        const char *icon;
    } table[] = {
        { ".c",      "\xef\x9b\x99" },   /*  */
        { ".h",      "\xef\x9b\x9c" },   /*  */
        { ".py",     "\xee\x94\xba" },   /*  */
        { ".rs",     "\xee\x9a\xbb" },   /*  */
        { ".go",     "\xee\x98\x91" },   /*  */
        { ".js",     "\xee\x8c\x8c" },   /*  */
        { ".ts",     "\xee\x8c\x8c" },   /*  */
        { ".sh",     "\xef\x91\x93" },   /*  */
        { ".md",     "\xef\x92\x8a" },   /*  */
        { ".yaml",   "\xee\x9a\x94" },   /*  */
        { ".yml",    "\xee\x9a\x94" },   /*  */
        { ".json",   "\xee\x8a\x93" },   /*  */
        { ".toml",   "\xee\x9a\x94" },   /*  */
        { ".txt",    "\xef\x85\x9b" },   /*  */
        { ".png",    "\xee\x97\x8d" },   /*  */
        { ".jpg",    "\xee\x97\x8d" },   /*  */
        { ".jpeg",   "\xee\x97\x8d" },   /*  */
        { ".gif",    "\xee\x97\x8d" },   /*  */
        { ".svg",    "\xee\x97\x8d" },   /*  */
        { ".mp3",    "\xef\x80\x81" },   /*  */
        { ".mp4",    "\xee\x84\x9b" },   /*  */
        { ".pdf",    "\xee\x84\xab" },   /*  */
        { ".zip",    "\xef\x87\x86" },   /*  */
        { ".tar",    "\xef\x87\x86" },   /*  */
        { ".gz",     "\xef\x87\x86" },   /*  */
        { ".tgz",    "\xef\x87\x86" },   /*  */
        { ".xz",     "\xef\x87\x86" },   /*  */
        { NULL, NULL },
    };
    for (int i = 0; table[i].suffix; i++) {
        size_t sl = strlen(table[i].suffix);
        size_t nl = strlen(name);
        if (nl >= sl && strcasecmp(name + nl - sl, table[i].suffix) == 0) {
            return table[i].icon;
        }
    }
    return "\xef\x85\x9b";  /*  默认通用文件 */
}

static const char *nerd_for_mode(mode_t m, int is_link) {
    if (is_link) return "\xef\x91\xa0";  /*  */
    if (S_ISDIR(m))  return "\xef\x84\x94";  /*  */
    if (S_ISLNK(m))  return "\xef\x91\xa0";  /*  */
    if (S_ISFIFO(m)) return "\xef\x90\xa6";  /*  */
    if (S_ISSOCK(m)) return "\xef\x91\xa2";  /*  */
    if (S_ISBLK(m) || S_ISCHR(m)) return "\xef\x84\x80";  /*  */
    if (m & S_IXUSR)  return "\xef\x92\x9b";  /*  */
    return "\xef\x85\x9b";  /*  */
}

/* === Unicode 图标（基础 emoji，大多数终端支持） === */
static const char *unicode_for_mode(mode_t m, int is_link) {
    if (is_link) return "\xf0\x9f\x94\x97";  /* 🔗 */
    if (S_ISDIR(m))  return "\xf0\x9f\x93\x81";  /* 📁 */
    if (S_ISLNK(m))  return "\xf0\x9f\x94\x97";  /* 🔗 */
    if (S_ISFIFO(m)) return "\xf0\x9f\x93\xa6";  /* 📦 */
    if (S_ISSOCK(m)) return "\xf0\x9f\x94\x8c";  /* 🔌 */
    if (S_ISBLK(m))  return "\xf0\x9f\x92\xbe";  /* 💾 */
    if (S_ISCHR(m))  return "\xf0\x9f\x96\xb2";  /* 🖲 */
    if (m & S_IXUSR)  return "\xe2\x9a\x99";  /* ⚙ */
    return "\xf0\x9f\x93\x84";  /* 📄 */
}

/* === ASCII 图标（ls -F 风格后缀） === */
static const char *ascii_for_mode(mode_t m, int is_link) {
    if (is_link) return "@";
    if (S_ISDIR(m))  return "/";
    if (S_ISFIFO(m)) return "|";
    if (S_ISSOCK(m)) return "=";
    if (S_ISBLK(m))  return "b";
    if (S_ISCHR(m))  return "c";
    if (m & S_IXUSR)  return "*";
    return "";
}

const char *icon_for_mode(mode_t mode, int is_link) {
    if (current_set == ICON_SET_NERD) {
        return nerd_for_mode(mode, is_link);
    }
    if (current_set == ICON_SET_UNICODE) {
        return unicode_for_mode(mode, is_link);
    }
    return ascii_for_mode(mode, is_link);
}

const char *icon_type_label(mode_t mode) {
    if (S_ISDIR(mode)) return "d";
    if (S_ISLNK(mode)) return "l";
    if (S_ISFIFO(mode)) return "p";
    if (S_ISSOCK(mode)) return "s";
    if (S_ISBLK(mode)) return "b";
    if (S_ISCHR(mode)) return "c";
    if ((mode & S_IXUSR) || (mode & S_IXGRP) || (mode & S_IXOTH)) return "*";
    return "-";
}
