/* msh/plugin.h — msh 原生插件/主题 API
 *
 * msh 拥有自己的插件和主题系统。
 * bash/zsh 兼容通过 compat.c 转换层实现。
 *
 * 插件开发指南：
 *   插件就是 .msh 脚本，使用注释声明元数据：
 *     # @plugin my-plugin
 *     # @desc 我的插件描述
 *     # @version 1.0
 *     # @author my-name
 *
 *   然后写 alias/export/complete/函数定义等。
 *   放到 ~/.msh/plugins/my-plugin.msh 即可。
 *
 * 主题开发指南：
 *   主题就是设置 PS1/PS2 的 .msh 脚本：
 *     # @theme my-theme
 *     # @desc 我的主题描述
 *     # @colorscheme dark
 *
 *     export MSH_PS1='\[\e[32m\]\u@\h\[\e[0m\]:\w\$ '
 *     export MSH_PS2='> '
 *
 *   放到 ~/.msh/themes/my-theme.msh 即可。
 *
 * PS1 转义序列见 config.c msh_prompt_expand() 文档。
 */
#ifndef MSH_PLUGIN_H
#define MSH_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* === 插件 API === */
int msh_plugin_scan(void);
int msh_plugin_load(const char *name);
int msh_plugin_list(void);
int msh_plugin_enable(const char *name);
int msh_plugin_disable(const char *name);

/* === 主题 API === */
int msh_theme_list(void);
int msh_theme_apply(const char *name);
int msh_theme_current(void);
int msh_theme_builtin(const char *name);  /* 兼容旧接口 */

/* === YAML 主题 API === */
int msh_theme_try_yaml_builtin(const char *name);
int msh_theme_apply_yaml_file(const char *path, const char *name);

/* === 兼容转换层 API === */
/* 从 bash/zsh 配置文件导入别名、环境变量、主题。
 * target: "bash" 或 "zsh"
 * 返回 0 成功，非 0 失败。 */
int msh_compat_import(const char *target);

/* === msh plugin 内建命令入口 === */
int msh_plugin_builtin(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* MSH_PLUGIN_H */
