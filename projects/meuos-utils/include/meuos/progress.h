/* meuos/progress.h — 进度条 + ETA
 *
 * 设计原则：
 *   - 仅在 tty 中渲染（自动检测）
 *   - 非 tty：只输出数字进度
 *   - ETA：根据已用时长 + 完成度估算
 *   - 线程不安全（单线程 cp/mv 足够）
 *   - 最低粒度 100ms 更新（不刷屏）
 */
#ifndef MEUOS_PROGRESS_H
#define MEUOS_PROGRESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct progress;

typedef struct progress progress_t;

/* 创建进度条。label 简述（如 "Copying"）。total=0 表示不确定（转圈圈）。*/
progress_t *progress_new(const char *label, uint64_t total);

/* 推进 current 字节/单位 */
void progress_update(progress_t *p, uint64_t current);

/* 强制刷新一次（用于关键点确认） */
void progress_force_flush(progress_t *p);

/* 完成并清除进度条。结束前必须调用。 */
void progress_finish(progress_t *p);

/* 释放资源 */
void progress_free(progress_t *p);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_PROGRESS_H */
