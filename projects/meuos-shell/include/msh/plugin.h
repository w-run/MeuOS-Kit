#ifndef MSH_PLUGIN_H
#define MSH_PLUGIN_H

/* zsh 风格插件/主题 API */
int msh_plugin_scan(void);
int msh_plugin_load(const char *name);
int msh_plugin_list(void);
int msh_plugin_enable(const char *name);
int msh_plugin_disable(const char *name);
int msh_theme_list(void);
int msh_theme_apply(const char *name);
int msh_theme_current(void);
int msh_theme_builtin(const char *name);
int msh_plugin_builtin(int argc, char **argv);

#endif
