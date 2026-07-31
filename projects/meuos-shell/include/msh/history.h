/* msh/history.h — 历史记录公共 API
 *
 * 行编辑读入的行会追加到历史；上下箭头翻历史。
 * 持久化到 ~/.msh_history（最多 1000 行）。
 */
#ifndef MEUOS_MSH_HISTORY_H
#define MEUOS_MSH_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

#define MSH_HIST_MAX 1000

/* 从 ~/.msh_history 加载历史。 */
void msh_history_load(void);

/* 把一行追加到历史并持久化。 */
void msh_history_add(const char *line);

/* 取历史条目（0-based 最新在 n-1）。返回内部指针，不要 free。 */
const char *msh_history_get(int idx);

/* 历史条目数。 */
int msh_history_count(void);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_HISTORY_H */
