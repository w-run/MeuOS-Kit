/* mt-info 共享 ELF 加载：读文件 + libelf 解析 */
#include "mt-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int
mt_load_elf(const char *path, void **buf, size_t *size,
            struct mt_elf64_view *view)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		mt_msg(0, "无法打开文件: %s", path);
		return -1;
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		mt_msg(0, "无法 stat: %s", path);
		close(fd);
		return -1;
	}
	*size = (size_t)st.st_size;
	*buf = malloc(*size ? *size : 1);
	if (!*buf) {
		mt_msg(0, "内存分配失败");
		close(fd);
		return -1;
	}
	ssize_t got = 0;
	while (got < (ssize_t)*size) {
		ssize_t n = read(fd, (char *)*buf + got, *size - (size_t)got);
		if (n <= 0)
			break;
		got += n;
	}
	close(fd);
	if ((size_t)got != *size) {
		mt_msg(0, "读取不完整: %s", path);
		free(*buf);
		return -1;
	}
	if (mt_elf64_parse(*buf, *size, view) != MT_ELF_OK) {
		mt_msg(0, "不是有效的 ELF 文件: %s", path);
		free(*buf);
		return -1;
	}
	return 0;
}
