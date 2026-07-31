#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

char *
getenv(const char *name)
{
	size_t length;
	char **entry;

	if (!name || !*name || !environ)
		return 0;
	length = strlen(name);
	for (entry = environ; *entry; ++entry)
		if (strncmp(*entry, name, length) == 0 && (*entry)[length] == '=')
			return *entry + length + 1;
	return 0;
}

/*
 * 环境数组的管理：
 *  - environ 初始指向内核压栈的 envp 数组（crt1.S 中设置），不能对其实行
 *    realloc/free；因此 setenv() 追加新变量时总是另起一块堆数组，拷贝旧项。
 *  - env_array 记录当前由本实现持有的堆数组，下次扩容前释放旧堆数组。
 *  - env_owned 记录 setenv() 分配的 "name=value" 字符串；unsetenv()/覆盖时
 *    只能释放这些指针，避免 free 内核栈上的初始 envp 字符串（free() 会把它
 *    前面的字节当成堆元数据写坏）。
 */
static char **env_array;
static struct env_owned {
	char *ptr;
	struct env_owned *next;
} *env_owned_list;

static void
env_owned_add(char *ptr)
{
	struct env_owned *node = malloc(sizeof(*node));

	if (node) {
		node->ptr = ptr;
		node->next = env_owned_list;
		env_owned_list = node;
	}
}

static int
env_owned_contains(char *ptr)
{
	struct env_owned *node;

	for (node = env_owned_list; node; node = node->next)
		if (node->ptr == ptr)
			return 1;
	return 0;
}

static int
env_valid_name(const char *name)
{
	if (!name || !*name)
		return 0;
	for (; *name; ++name)
		if (*name == '=')
			return 0;
	return 1;
}

static char *
env_make(const char *name, size_t name_len, const char *value, size_t value_len)
{
	char *entry;

	entry = malloc(name_len + value_len + 2);
	if (!entry)
		return 0;
	memcpy(entry, name, name_len);
	entry[name_len] = '=';
	memcpy(entry + name_len + 1, value, value_len + 1);
	return entry;
}

int
setenv(const char *name, const char *value, int overwrite)
{
	size_t name_len, value_len, count;
	char *entry, **envp, **new_array;

	if (!env_valid_name(name)) {
		errno = EINVAL;
		return -1;
	}
	if (!value)
		value = "";
	name_len = strlen(name);
	value_len = strlen(value);

	/* 已存在的变量：直接替换字符串（不调整数组）。 */
	if (environ) {
		for (envp = environ; *envp; ++envp) {
			if (strncmp(*envp, name, name_len) == 0 &&
			    (*envp)[name_len] == '=') {
				if (!overwrite)
					return 0;
				entry = env_make(name, name_len, value, value_len);
				if (!entry)
					return -1;
				if (env_owned_contains(*envp))
					free(*envp);
				*envp = entry;
				env_owned_add(entry);
				return 0;
			}
		}
	}

	/* 新变量：复制数组后追加（不能 realloc 内核栈上的初始 envp）。 */
	entry = env_make(name, name_len, value, value_len);
	if (!entry)
		return -1;
	count = 0;
	for (envp = environ; envp && *envp; ++envp)
		++count;
	new_array = malloc((count + 2) * sizeof(char *));
	if (!new_array) {
		free(entry);
		return -1;
	}
	for (envp = environ, count = 0; envp && *envp; ++envp)
		new_array[count++] = *envp;
	new_array[count] = entry;
	new_array[count + 1] = NULL;
	if (env_array)
		free(env_array);
	env_array = new_array;
	environ = new_array;
	env_owned_add(entry);
	return 0;
}

int
unsetenv(const char *name)
{
	size_t name_len;
	char **src, **dst;

	if (!env_valid_name(name)) {
		errno = EINVAL;
		return -1;
	}
	if (!environ)
		return 0;
	name_len = strlen(name);
	for (src = environ, dst = environ; *src; ++src) {
		if (strncmp(*src, name, name_len) == 0 && (*src)[name_len] == '=') {
			if (env_owned_contains(*src))
				free(*src);
			continue;
		}
		*dst++ = *src;
	}
	*dst = NULL;
	return 0;
}
