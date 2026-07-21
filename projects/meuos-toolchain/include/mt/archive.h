#ifndef MT_ARCHIVE_H
#define MT_ARCHIVE_H

/*
 * SysV/BSD ar archive 的最小公共接口。
 * 首期故意限制成员名为 15 个字符以内，不实现 GNU long-name table；
 * 该限制会在完整 ar 阶段通过 // 成员扩展解除。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MT_AR_MAGIC "!<arch>\n"
#define MT_AR_MAGIC_SIZE 8
#define MT_AR_MEMBER_NAME_SIZE 16
#define MT_AR_MEMBER_NAME_MAX 15
#define MT_AR_HEADER_SIZE 60

enum mt_ar_status {
	MT_AR_OK = 0,
	MT_AR_E_ARGUMENT = 1,
	MT_AR_E_IO,
	MT_AR_E_FORMAT,
	MT_AR_E_NAME,
	MT_AR_E_NOT_FOUND
};

struct mt_ar_member {
	char name[MT_AR_MEMBER_NAME_SIZE + 1];
	uint64_t size;
};

typedef int (*mt_ar_member_callback)(const struct mt_ar_member *member,
                                     void *context);

/* 以可复现元数据重写一个 archive；首期 r/q 都使用此路径。 */
int mt_ar_create(const char *archive, const char *const *members,
                 size_t member_count);

/* 遍历 archive 中的所有成员；回调返回非零时停止。 */
int mt_ar_list(const char *archive, mt_ar_member_callback callback,
               void *context);

/* 输出成员内容；names 为空时处理全部成员。 */
int mt_ar_print(const char *archive, const char *const *names,
                size_t name_count, FILE *output);

/* 解出成员；names 为空时解出全部成员。 */
int mt_ar_extract(const char *archive, const char *const *names,
                  size_t name_count);

const char *mt_ar_status_string(enum mt_ar_status status);

#endif
