/* meuos/table.h — 自适应列宽表格
 *
 * 设计原则：
 *   - 自动检测终端宽度
 *   - 列自适应：内容超长时截断或换行（策略可选）
 *   - 数字右对齐，文本左对齐
 *   - 支持分隔符行（如 "──────────────"）
 */
#ifndef MEUOS_TABLE_H
#define MEUOS_TABLE_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TABLE_ALIGN_LEFT,      /* 左对齐 */
    TABLE_ALIGN_RIGHT,     /* 右对齐（数字） */
    TABLE_ALIGN_CENTER,
} table_align_t;

struct table;
typedef struct table table_t;

/* 创建表。column_widths 是建议列宽（NULL=自适应）。strict=1 时列宽固定不截断。*/
table_t *table_new(size_t ncols, const int *column_widths, table_align_t default_align);

/* 添加行。cells 是 ncols 个字符串，NULL 表示空。 */
void table_add_row(table_t *t, const char **cells);

/* 添加分隔行（"----+----+----"）。 */
void table_add_separator(table_t *t);

/* 渲染表。fp 是输出流，term_width=0 自动检测。 */
void table_render(table_t *t, FILE *fp, int term_width);

/* 释放 */
void table_free(table_t *t);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_TABLE_H */
