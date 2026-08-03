/* msh/complete.h — Tab 补全引擎公共 API */
#ifndef MEUOS_MSH_COMPLETE_H
#define MEUOS_MSH_COMPLETE_H

#include <stddef.h>

/* 补全当前行。
 * buf: 编辑缓冲区（会被修改），cur: 光标位置（会被更新），
 * len: 当前长度（会被更新），cap: 缓冲区容量。
 * is_continuous_tab: 上一次按键也是 Tab（触发列表显示）。
 * 返回: 0=无补全, 1=已补全（需刷新行）, 2=已显示候选列表。 */
int msh_complete(char *buf, size_t *cur, size_t *len, size_t cap, int is_continuous_tab);

#endif /* MEUOS_MSH_COMPLETE_H */
