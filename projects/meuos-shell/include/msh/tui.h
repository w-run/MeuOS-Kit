/* msh/tui.h — TUI 模式接口
 *
 * msh --tui 启动 AI Agent 风格的 TUI 终端。
 * 使用 libtui 渲染，调用 msh_run_string() 执行命令。
 */
#ifndef MSH_TUI_H
#define MSH_TUI_H

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 TUI Shell 模式。返回退出码。 */
int msh_tui_main(void);

#ifdef __cplusplus
}
#endif

#endif /* MSH_TUI_H */
