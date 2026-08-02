/* msh/cmd.h — :cmd 指令系统
 *
 * msh 使用 : 前缀触发内置功能（受 vim 启发）。
 * :foo 被解析为单个 word，不会与 POSIX 的 : (no-op) 冲突。
 *
 * 内置 :cmd 指令：
 *   :theme <name>      应用主题
 *   :themes             列出主题
 *   :plugin <name>      加载插件
 *   :plugins            列出插件
 *   :lang <lang>        切换语言
 *   :langs              列出语言
 *   :compat <target>    导入 bash/zsh 配置
 *   :config             显示配置
 *   :reload             重新加载配置
 *   :help [cmd]         帮助
 *   :version            版本信息
 *   :init-plugin <name> 创建插件模板
 *   :init-theme <name>  创建主题模板
 *
 * 插件扩展：
 *   插件中定义函数 _cmd_<name> 即可注册 :<name> 指令。
 *   例如插件中定义 _cmd_status() 函数，则 :status 会调用它。
 *
 *   也可以通过 alias 注册：
 *   alias :status='echo system status'
 */
#ifndef MSH_CMD_H
#define MSH_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/* 检查 name 是否以 : 开头且是 :cmd 格式（非单独 :） */
int msh_is_colon_cmd(const char *name);

/* 分发 :cmd 指令。
 * argc/argv 同 main 参数，argv[0] 是 ":cmdname"。
 * 返回退出码。 */
int msh_cmd_dispatch(int argc, char **argv);

/* :cmd 帮助 */
int msh_cmd_help(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* MSH_CMD_H */
