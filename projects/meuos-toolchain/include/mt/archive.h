#ifndef MT_ARCHIVE_H
#define MT_ARCHIVE_H

/*
 * SysV/GNU ar archive 的核心接口。
 *
 * ar 的成员名可以是任意非空 basename；短名直接放入 16 字节 name 字段，
 * 长名通过 GNU // long-name table 表示。回调中的 name 指针只在回调期间有效。
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
	MT_AR_E_NOT_FOUND,
	MT_AR_E_OVERFLOW
};

struct mt_ar_member {
	const char *name;
	uint64_t size;
};

typedef int (*mt_ar_member_callback)(const struct mt_ar_member *member,
                                     void *context);
typedef int (*mt_ar_data_callback)(const struct mt_ar_member *member,
                                   const unsigned char *data, void *context);

#define MT_AR_UPDATE_REPLACE 0x01u
#define MT_AR_UPDATE_APPEND  0x02u

/* 创建或更新 archive；REPLACE 保留已有成员并替换同名成员，APPEND 只追加。 */
int mt_ar_update(const char *archive, const char *const *members,
                 size_t member_count, unsigned flags);

/* 兼容旧的 P0a API：以 REPLACE 语义更新并生成索引。 */
int mt_ar_create(const char *archive, const char *const *members,
                 size_t member_count);

/* 遍历 archive 中的普通成员；索引和 long-name table 不会回调。 */
int mt_ar_list(const char *archive, mt_ar_member_callback callback,
               void *context);

/* 访问成员内容；data 只在回调期间有效，archive 不会在回调期间修改。 */
int mt_ar_foreach(const char *archive, mt_ar_data_callback callback,
                  void *context);

/* 输出成员内容；names 为空时处理全部成员。 */
int mt_ar_print(const char *archive, const char *const *names,
                size_t name_count, FILE *output);

/* 解出成员；names 为空时解出全部成员。 */
int mt_ar_extract(const char *archive, const char *const *names,
                  size_t name_count);

const char *mt_ar_status_string(enum mt_ar_status status);

#endif
