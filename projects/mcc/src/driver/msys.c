/* msys.c — .msys single-file sysroot utilities for mcc.
 *
 * Provides: detect .msys suffix, extract .msys contents to a temp
 * directory so the rest of the driver can treat it as a regular path. */

#include "mt/msys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Check if a path ends with ".msys". */
int
msys_is_sysroot(const char *path)
{
	size_t len = path ? strlen(path) : 0;
	return len >= 5 && strcmp(path + len - 5, ".msys") == 0;
}

/* Extract all files from a .msys archive to a target directory. */
static int
msys_extract_to(const char *msys_path, const char *dest)
{
	struct msys *m = msys_open(msys_path);
	unsigned char *index_bytes;
	char path_buf[4096];
	size_t i;

	if (!m)
		return -1;

	index_bytes = (unsigned char *)m->index;

	for (i = 0; i < m->hdr->index_count; ) {
		struct msys_index_entry *entry = (struct msys_index_entry *)(index_bytes);
		uint16_t name_len = (uint16_t)entry->name_len[0] |
		                    ((uint16_t)entry->name_len[1] << 8);
		uint64_t d_off = (uint64_t)entry->data_offset[0] |
		                 ((uint64_t)entry->data_offset[1] << 8) |
		                 ((uint64_t)entry->data_offset[2] << 16) |
		                 ((uint64_t)entry->data_offset[3] << 24) |
		                 ((uint64_t)entry->data_offset[4] << 32) |
		                 ((uint64_t)entry->data_offset[5] << 40);
		uint32_t d_size = (uint32_t)entry->data_size[0] |
		                  ((uint32_t)entry->data_size[1] << 8) |
		                  ((uint32_t)entry->data_size[2] << 16) |
		                  ((uint32_t)entry->data_size[3] << 24);
		char *fname = (char *)(index_bytes + 16);
		const void *data = (const char *)m->base + d_off;
		size_t j;
		FILE *out;

		if (name_len >= sizeof(path_buf) - 1)
			name_len = sizeof(path_buf) - 1;
		memcpy(path_buf, dest, strlen(dest));
		path_buf[strlen(dest)] = '/';
		memcpy(path_buf + strlen(dest) + 1, fname, name_len);
		path_buf[strlen(dest) + 1 + name_len] = '\0';

		/* Create subdirectories */
		for (j = strlen(dest) + 1; path_buf[j]; ++j) {
			if (path_buf[j] == '/') {
				char saved = path_buf[j];
				path_buf[j] = '\0';
				mkdir(path_buf, 0755);
				path_buf[j] = saved;
			}
		}

		out = fopen(path_buf, "wb");
		if (!out) {
			perror(path_buf);
			msys_close(m);
			return -1;
		}
		if (fwrite(data, 1, d_size, out) != d_size) {
			fclose(out);
			msys_close(m);
			return -1;
		}
		fclose(out);

		i++;
		index_bytes += 16 + name_len;
	}

	msys_close(m);
	return 0;
}

/* Open a .msys file by extracting to a temp directory.
 * Returns the temp directory path (caller must free), or NULL on error. */
char *
msys_sysroot_open(const char *sysroot_path)
{
	char template[] = "/tmp/meuos-sysroot-XXXXXX";

	if (!msys_is_sysroot(sysroot_path))
		return NULL;
	if (mkdtemp(template) == NULL)
		return NULL;
	if (msys_extract_to(sysroot_path, template) != 0) {
		char cmd[4096];
		snprintf(cmd, sizeof(cmd), "rm -rf %s", template);
		system(cmd);
		return NULL;
	}
	return strdup(template);
}
