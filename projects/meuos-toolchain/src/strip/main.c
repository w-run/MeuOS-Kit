/* strip - remove symbols and debug info from ELF64 files.
 *
 * 写入策略（混合）：
 *   - ET_REL（.o）：用 mt_elfNN_writer 全量重写，输出紧凑、无 dead bytes。
 *   - ET_EXEC/ET_DYN（可执行文件/共享库）：就地改写节区头表，完整保留
 *     原始文件字节（含程序头与所有 PT_LOAD 数据），仅修补 ehdr 的
 *     e_shnum/e_shstrndx 并重写 shdr 表区域。writer 不输出程序头，故
 *     可执行文件必须走就地路径以保证剥离后仍可运行。
 *
 * 默认行为等同 --strip-all。就地修改时先写临时文件再 rename 原子替换。
 * 输出可复现：无时间戳、固定节区顺序、locale 无关。支持 ELF64 LE 和 ELF32 LE。 */
#define _POSIX_C_SOURCE 200809L

#include "mt/elf.h"
#include "mt/elf32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     /* rename(): this toolchain's glibc declares it in
                         * <unistd.h>, not <stdio.h> (gcc14 -std=c11) */
#include <sys/stat.h>

#define MT_STRIP_VERSION "0.2.0"

enum strip_mode {
	STRIP_ALL = 0,
	STRIP_DEBUG
};

struct strip_opts {
	enum strip_mode mode;
	const char *output;          /* -o；NULL 表示就地修改 */
	const char **remove_secs;    /* -R/--remove-section */
	size_t remove_count;
	const char **keep_secs;      /* --keep-section */
	size_t keep_count;
	int verbose;
};

/* ---- little-endian 编码（就地路径修补 shdr 用） ---- */

static void
put16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static void
put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static void
put64(unsigned char *p, uint64_t v)
{
	put32(p, (uint32_t)v);
	put32(p + 4, (uint32_t)(v >> 32));
}

static int
has_prefix(const char *s, const char *p)
{
	return strncmp(s, p, strlen(p)) == 0;
}

static void
usage(FILE *out)
{
	fprintf(out,
	    "usage: strip [options] file...\n"
	    "  -s, --strip-all       remove all symbols and debug info (default)\n"
	    "  -g, -S, --strip-debug remove debug info only\n"
	    "      --strip-unneeded  remove debug info (simplified)\n"
	    "  -K SYMBOL             keep SYMBOL (not yet supported)\n"
	    "  -N SYMBOL             strip SYMBOL (not yet supported)\n"
	    "  -R SECTION            remove SECTION\n"
	    "      --remove-section=SECTION\n"
	    "      --keep-section=SECTION\n"
	    "  -o FILE               write output to FILE (default: in-place)\n"
	    "  -p, --preserve-dates  (accepted, ignored)\n"
	    "  -v, --verbose         list removed sections\n"
	    "  -V, --version         print version\n"
	    "  -h, --help            print this help\n");
}

static int
name_in_list(const char *name, const char *const *list, size_t n)
{
	size_t i;
	for (i = 0; i < n; ++i)
		if (strcmp(name, list[i]) == 0)
			return 1;
	return 0;
}

/* 按节区名+模式判定是否删除。返回 1=删除。 */
static int
drop_by_name(const char *name, const struct strip_opts *opts)
{
	if (name_in_list(name, opts->keep_secs, opts->keep_count))
		return 0;
	if (name_in_list(name, opts->remove_secs, opts->remove_count))
		return 1;
	if (opts->mode == STRIP_ALL) {
		if (strcmp(name, ".symtab") == 0) return 1;
		if (strcmp(name, ".strtab") == 0) return 1;
		if (strcmp(name, ".comment") == 0) return 1;
		if (strcmp(name, ".line") == 0) return 1;
		if (has_prefix(name, ".debug_") || has_prefix(name, ".zdebug_"))
			return 1;
		if (has_prefix(name, ".stab")) return 1;
	} else { /* STRIP_DEBUG */
		if (strcmp(name, ".line") == 0) return 1;
		if (has_prefix(name, ".debug_") || has_prefix(name, ".zdebug_"))
			return 1;
		if (has_prefix(name, ".stab")) return 1;
	}
	return 0;
}

/* 读取整个文件到 malloc 缓冲区。返回 0 成功。 */
static int
read_file(const char *path, unsigned char **out, size_t *out_size)
{
	FILE *f;
	unsigned char *buf;
	long sz;
	size_t got;

	f = fopen(path, "rb");
	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
	sz = ftell(f);
	if (sz < 0) { fclose(f); return -1; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
	buf = malloc((size_t)sz ? (size_t)sz : 1);
	if (!buf) { fclose(f); return -1; }
	got = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	if (got != (size_t)sz) { free(buf); return -1; }
	*out = buf;
	*out_size = (size_t)sz;
	return 0;
}

static int
copy_bytes_to(const unsigned char *bytes, size_t size, const char *dst)
{
	FILE *out = fopen(dst, "wb");
	if (!out) {
		fprintf(stderr, "strip: %s: cannot create\n", dst);
		return 1;
	}
	if (fwrite(bytes, 1, size, out) != size) {
		fprintf(stderr, "strip: %s: write failed\n", dst);
		fclose(out);
		return 1;
	}
	if (fclose(out) != 0)
		return 1;
	return 0;
}

static int
copy_file(const char *src, const char *dst)
{
	unsigned char *buf = NULL;
	size_t sz = 0;
	int rc;

	if (read_file(src, &buf, &sz) != 0) {
		fprintf(stderr, "strip: %s: cannot read\n", src);
		return 1;
	}
	rc = copy_bytes_to(buf, sz, dst);
	free(buf);
	return rc;
}

/* ET_REL 路径：用 writer 全量重写。保留节区直接复制原始字节。
 * writer 自动添加 NULL(0) 和 .shstrtab(末尾)，所以这里跳过两者并
 * 重新计算 link/info 索引映射。 */
static int
write_via_writer(const unsigned char *bytes, size_t size,
                 const struct mt_elf64_view *view,
                 const struct mt_elf64_section *secs,
                 const char *const *names,
                 const unsigned char *keep, const uint32_t *map,
                 uint16_t n, const char *tmppath,
                 const struct strip_opts *opts)
{
	FILE *out;
	uint16_t i;
	int rc = 1;
	uint32_t *new_idx;
	uint32_t next_idx = 1;
	uint32_t shstrtab_new;
	int shstrtab_orig = -1;

	int elf32 = (bytes[4] == 1);  /* ELFCLASS32 */

	(void)size;
	new_idx = calloc(n, sizeof(uint32_t));
	if (!new_idx) {
		fprintf(stderr, "strip: out of memory\n");
		return 1;
	}
	for (i = 0; i < n; ++i) {
		if (names[i] && strcmp(names[i], ".shstrtab") == 0) {
			shstrtab_orig = (int)i;
			break;
		}
	}
	for (i = 1; i < n; ++i) {
		if (!keep[i])
			continue;
		if ((int)i == shstrtab_orig)
			continue;
		new_idx[i] = next_idx++;
	}
	shstrtab_new = next_idx;

	if (elf32) {
		struct mt_elf32_writer w32;
		mt_elf32_writer_init(&w32, view->machine, view->type);
		mt_elf32_writer_set_entry(&w32, (uint32_t)view->entry);
		mt_elf32_writer_set_flags(&w32, (uint32_t)view->flags);

		for (i = 1; i < n; ++i) {
			struct mt_elf32_writer_section ws;
			if (!keep[i]) {
				if (opts->verbose)
					fprintf(stderr, "strip: removing %s\n",
					        names[i][0] ? names[i] : "(noname)");
				continue;
			}
			if ((int)i == shstrtab_orig)
				continue;
			ws.name = names[i];
			ws.type = secs[i].type;
			ws.flags = (uint32_t)secs[i].flags;
			ws.address = (uint32_t)secs[i].address;
			ws.size = (uint32_t)secs[i].size;
			ws.alignment = (uint32_t)secs[i].alignment;
			ws.entry_size = (uint32_t)secs[i].entry_size;
			if ((int)secs[i].link == shstrtab_orig)
				ws.link = shstrtab_new;
			else if (secs[i].link < n)
				ws.link = new_idx[secs[i].link];
			else
				ws.link = 0;
			if (secs[i].type == MT_SHT_REL || secs[i].type == MT_SHT_RELA) {
				if ((int)secs[i].info == shstrtab_orig)
					ws.info = shstrtab_new;
				else if (secs[i].info < n)
					ws.info = new_idx[secs[i].info];
				else
					ws.info = 0;
			} else {
				ws.info = secs[i].info;
			}
			if (secs[i].type == MT_SHT_NOBITS || secs[i].size == 0)
				ws.data = NULL;
			else
				ws.data = bytes + secs[i].offset;
			if (mt_elf32_writer_add_section(&w32, &ws) != 0) {
				fprintf(stderr, "strip: out of memory\n");
				mt_elf32_writer_free(&w32);
				free(new_idx);
				return 1;
			}
		}
		(void)map;

		out = fopen(tmppath, "wb");
		if (!out) {
			fprintf(stderr, "strip: %s: cannot create\n", tmppath);
			mt_elf32_writer_free(&w32);
			free(new_idx);
			return 1;
		}
		if (mt_elf32_writer_finalize(&w32, out) != 0)
			fprintf(stderr, "strip: %s: write failed\n", tmppath);
		else
			rc = 0;
		if (fclose(out) != 0)
			rc = 1;
		mt_elf32_writer_free(&w32);
	} else {
		struct mt_elf64_writer w;
		mt_elf64_writer_init(&w, view->machine, view->type);
		mt_elf64_writer_set_entry(&w, view->entry);
		mt_elf64_writer_set_flags(&w, view->flags);

		for (i = 1; i < n; ++i) {
			struct mt_elf64_writer_section ws;
			if (!keep[i]) {
				if (opts->verbose)
					fprintf(stderr, "strip: removing %s\n",
					        names[i][0] ? names[i] : "(noname)");
				continue;
			}
			if ((int)i == shstrtab_orig)
				continue;
			ws.name = names[i];
			ws.type = secs[i].type;
			ws.flags = secs[i].flags;
			ws.address = secs[i].address;
			ws.size = secs[i].size;
			ws.alignment = secs[i].alignment;
			ws.entry_size = secs[i].entry_size;
			if ((int)secs[i].link == shstrtab_orig)
				ws.link = shstrtab_new;
			else if (secs[i].link < n)
				ws.link = new_idx[secs[i].link];
			else
				ws.link = 0;
			if (secs[i].type == MT_SHT_REL || secs[i].type == MT_SHT_RELA) {
				if ((int)secs[i].info == shstrtab_orig)
					ws.info = shstrtab_new;
				else if (secs[i].info < n)
					ws.info = new_idx[secs[i].info];
				else
					ws.info = 0;
			} else {
				ws.info = secs[i].info;
			}
			if (secs[i].type == MT_SHT_NOBITS || secs[i].size == 0)
				ws.data = NULL;
			else
				ws.data = bytes + secs[i].offset;
			if (mt_elf64_writer_add_section(&w, &ws) != 0) {
				fprintf(stderr, "strip: out of memory\n");
				mt_elf64_writer_free(&w);
				free(new_idx);
				return 1;
			}
		}
		(void)map;

		out = fopen(tmppath, "wb");
		if (!out) {
			fprintf(stderr, "strip: %s: cannot create\n", tmppath);
			mt_elf64_writer_free(&w);
			free(new_idx);
			return 1;
		}
		if (mt_elf64_writer_finalize(&w, out) != 0)
			fprintf(stderr, "strip: %s: write failed\n", tmppath);
		else
			rc = 0;
		if (fclose(out) != 0)
			rc = 1;
		mt_elf64_writer_free(&w);
	}
	free(new_idx);
	return rc;
}

/* ET_EXEC/ET_DYN 路径：就地改写。保留原始文件全部字节，仅修补 ehdr
 * 的 e_shnum/e_shstrndx 并重写 shdr 表区域（清零旧条目后填入保留项）。
 * 程序头与所有 PT_LOAD 数据原样保留，确保剥离后仍可运行。 */
static int
write_inplace(const unsigned char *bytes, size_t size,
              const struct mt_elf64_view *view,
              const struct mt_elf64_section *secs,
              const char *const *names,
              const unsigned char *keep, const uint32_t *map,
              uint16_t n, const char *tmppath,
              const struct strip_opts *opts)
{
	FILE *out;
	unsigned char *buf = NULL;
	uint64_t e_shoff = view->section_offset;
	uint32_t new_shnum, old_shnum;
	uint64_t shdr_region;
	uint16_t i;
	uint32_t idx;
	int rc = 1;
	int elf32 = (bytes[4] == 1);  /* ELFCLASS32 */
	uint16_t shdr_size = elf32 ? MT_ELF32_SHDR_SIZE : MT_ELF64_SHDR_SIZE;

	(void)opts;
	if (e_shoff == 0 || n == 0)
		return copy_bytes_to(bytes, size, tmppath);

	old_shnum = n;
	new_shnum = 1; /* NULL 节区 */
	for (i = 1; i < n; ++i)
		if (keep[i])
			++new_shnum;

	buf = malloc(size);
	if (!buf) {
		fprintf(stderr, "strip: out of memory\n");
		return 1;
	}
	memcpy(buf, bytes, size);

	/* 修补 ehdr: e_shnum, e_shstrndx (ELF32 offset 48/50, ELF64 60/62) */
	if (elf32) {
		put16(buf + 48, (uint16_t)new_shnum);
		put16(buf + 50, (uint16_t)map[view->section_name_index]);
	} else {
		put16(buf + 60, (uint16_t)new_shnum);
		put16(buf + 62, (uint16_t)map[view->section_name_index]);
	}

	/* 清零旧 shdr 表区域后重写保留项 */
	shdr_region = e_shoff;
	if (shdr_region + (uint64_t)old_shnum * shdr_size > size) {
		fprintf(stderr, "strip: corrupt section header table\n");
		free(buf);
		return 1;
	}
	memset(buf + shdr_region, 0, (size_t)old_shnum * shdr_size);
	idx = 1;
	for (i = 1; i < n; ++i) {
		unsigned char *e;
		if (!keep[i]) {
			if (opts->verbose)
				fprintf(stderr, "strip: removing %s\n",
				        names[i][0] ? names[i] : "(noname)");
			continue;
		}
		e = buf + shdr_region + (uint64_t)idx * shdr_size;
		if (elf32) {
			put32(e + 0, secs[i].name);
			put32(e + 4, secs[i].type);
			put32(e + 8, (uint32_t)secs[i].flags);
			put32(e + 12, (uint32_t)secs[i].address);
			put32(e + 16, (uint32_t)secs[i].offset);
			put32(e + 20, (uint32_t)secs[i].size);
			put32(e + 24, (secs[i].link < n) ? map[secs[i].link] : 0);
			if (secs[i].type == MT_SHT_REL || secs[i].type == MT_SHT_RELA)
				put32(e + 28, (secs[i].info < n) ? map[secs[i].info] : 0);
			else
				put32(e + 28, secs[i].info);
			put32(e + 32, (uint32_t)secs[i].alignment);
			put32(e + 36, (uint32_t)secs[i].entry_size);
		} else {
			put32(e + 0, secs[i].name);
			put32(e + 4, secs[i].type);
			put64(e + 8, secs[i].flags);
			put64(e + 16, secs[i].address);
			put64(e + 24, secs[i].offset);
			put64(e + 32, secs[i].size);
			put32(e + 40, (secs[i].link < n) ? map[secs[i].link] : 0);
			if (secs[i].type == MT_SHT_REL || secs[i].type == MT_SHT_RELA)
				put32(e + 44, (secs[i].info < n) ? map[secs[i].info] : 0);
			else
				put32(e + 44, secs[i].info);
			put64(e + 48, secs[i].alignment);
			put64(e + 56, secs[i].entry_size);
		}
		++idx;
	}

	out = fopen(tmppath, "wb");
	if (!out) {
		fprintf(stderr, "strip: %s: cannot create\n", tmppath);
		free(buf);
		return 1;
	}
	if (fwrite(buf, 1, size, out) != size)
		fprintf(stderr, "strip: %s: write failed\n", tmppath);
	else
		rc = 0;
	if (fclose(out) != 0)
		rc = 1;
	free(buf);
	return rc;
}

static int
strip_one(const char *path, const struct strip_opts *opts)
{
	unsigned char *bytes = NULL;
	size_t size = 0;
	struct mt_elf64_view view;
	struct mt_elf64_section shstrtab;
	struct mt_elf64_section *secs = NULL;
	const char **names = NULL;
	unsigned char *keep = NULL;
	uint32_t *map = NULL;
	enum mt_elf_status st;
	uint16_t i, n;
	const char *target;
	char *tmppath = NULL;
	int rc = 1;
	int any_drop = 0;
	struct stat stbuf;
	mode_t src_mode = 0755;

	if (stat(path, &stbuf) == 0)
		src_mode = stbuf.st_mode & 0777;
	if (read_file(path, &bytes, &size) != 0) {
		fprintf(stderr, "strip: %s: cannot read file\n", path);
		return 1;
	}
	st = mt_elf64_parse(bytes, size, &view);
	if (st != MT_ELF_OK) {
		fprintf(stderr, "strip: %s: %s\n", path, mt_elf_status_string(st));
		free(bytes);
		return 1;
	}
	n = view.section_count;
	if (n == 0) {
		int r;
		free(bytes);
		r = opts->output ? copy_file(path, opts->output) : 0;
		if (r == 0 && opts->output)
			(void)chmod(opts->output, src_mode);
		return r;
	}

	secs = calloc(n, sizeof(*secs));
	names = calloc(n, sizeof(*names));
	keep = calloc(n, 1);
	map = calloc(n, sizeof(*map));
	if (!secs || !names || !keep || !map) {
		fprintf(stderr, "strip: out of memory\n");
		goto done;
	}
	for (i = 0; i < n; ++i) {
		st = mt_elf64_get_section(bytes, size, &view, i, &secs[i]);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "strip: %s: %s\n", path,
			        mt_elf_status_string(st));
			goto done;
		}
	}
	st = mt_elf64_get_section(bytes, size, &view,
	                          view.section_name_index, &shstrtab);
	if (st != MT_ELF_OK || shstrtab.type != MT_SHT_STRTAB) {
		fprintf(stderr, "strip: %s: cannot read section name table\n", path);
		goto done;
	}
	for (i = 0; i < n; ++i) {
		st = mt_elf64_get_string(bytes, size, &shstrtab, secs[i].name,
		                         &names[i]);
		if (st != MT_ELF_OK)
			names[i] = "";
	}

	/* 初始 keep 决策（按名） */
	keep[0] = 1;
	for (i = 1; i < n; ++i)
		keep[i] = !drop_by_name(names[i], opts);

	/* strip-all：删 .symtab 时连带删其 sh_link 指向的 .strtab */
	if (opts->mode == STRIP_ALL) {
		for (i = 1; i < n; ++i) {
			uint32_t link;
			if (secs[i].type == MT_SHT_SYMTAB && !keep[i]) {
				link = secs[i].link;
				if (link < n && keep[link] &&
				    secs[link].type == MT_SHT_STRTAB &&
				    strcmp(names[link], ".shstrtab") != 0 &&
				    strcmp(names[link], ".dynstr") != 0)
					keep[link] = 0;
			}
		}
	}

	/* REL/RELA：若 sh_link(symtab) 或 sh_info(目标节区) 被删，则删之 */
	for (i = 1; i < n; ++i) {
		if (keep[i] &&
		    (secs[i].type == MT_SHT_REL || secs[i].type == MT_SHT_RELA)) {
			uint32_t link = secs[i].link;
			uint32_t info = secs[i].info;
			if ((link < n && !keep[link]) ||
			    (info < n && !keep[info]))
				keep[i] = 0;
		}
	}

	/* old->new 索引映射：保留项按序获得新索引 1,2,...（0 为 NULL 节区） */
	{
		uint32_t cnt = 0;
		map[0] = 0;
		for (i = 1; i < n; ++i)
			map[i] = keep[i] ? ++cnt : 0;
	}

	for (i = 1; i < n; ++i)
		if (!keep[i]) { any_drop = 1; break; }
	if (!any_drop) {
		if (opts->verbose)
			fprintf(stderr, "strip: %s: nothing to strip\n", path);
		rc = opts->output ? copy_file(path, opts->output) : 0;
		if (rc == 0 && opts->output)
			(void)chmod(opts->output, src_mode);
		goto done;
	}

	target = opts->output ? opts->output : path;
	{
		size_t len = strlen(target);
		tmppath = malloc(len + 16);
		if (!tmppath) {
			fprintf(stderr, "strip: out of memory\n");
			goto done;
		}
		memcpy(tmppath, target, len);
		memcpy(tmppath + len, ".strip.tmp", 11);
	}

	if (view.type == MT_ET_REL)
		rc = write_via_writer(bytes, size, &view, secs, names,
		                      keep, map, n, tmppath, opts);
	else
		rc = write_inplace(bytes, size, &view, secs, names, keep, map,
		                   n, tmppath, opts);

	if (rc == 0) {
		if (rename(tmppath, target) != 0) {
			fprintf(stderr, "strip: %s: cannot rename temp file\n",
			        target);
			remove(tmppath);
			rc = 1;
		} else {
			/* 保留原文件权限（fopen 新建临时文件会丢失 +x） */
			(void)chmod(target, src_mode);
		}
	} else {
		remove(tmppath);
	}

done:
	free(bytes);
	free(secs);
	free(names);
	free(keep);
	free(map);
	free(tmppath);
	return rc;
}

int
main(int argc, char **argv)
{
	struct strip_opts opts;
	const char **files = NULL;
	int file_count = 0;
	int i;
	int rc = 0;

	memset(&opts, 0, sizeof(opts));
	opts.mode = STRIP_ALL;
	files = calloc((size_t)argc, sizeof(*files));
	opts.remove_secs = calloc((size_t)argc, sizeof(const char *));
	opts.keep_secs = calloc((size_t)argc, sizeof(const char *));
	if (!files || !opts.remove_secs || !opts.keep_secs) {
		fprintf(stderr, "strip: out of memory\n");
		return 1;
	}

	for (i = 1; i < argc; ++i) {
		const char *a = argv[i];
		if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
			printf("meuos-toolchain strip %s (x86_64 bootstrap)\n",
			       MT_STRIP_VERSION);
			return 0;
		}
		if (strcmp(a, "-s") == 0 || strcmp(a, "--strip-all") == 0) {
			opts.mode = STRIP_ALL;
			continue;
		}
		if (strcmp(a, "-g") == 0 || strcmp(a, "-S") == 0 ||
		    strcmp(a, "--strip-debug") == 0) {
			opts.mode = STRIP_DEBUG;
			continue;
		}
		if (strcmp(a, "--strip-unneeded") == 0) {
			opts.mode = STRIP_DEBUG; /* 简化：仅去调试信息 */
			continue;
		}
		if (strcmp(a, "-o") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			opts.output = argv[i];
			continue;
		}
		if (strcmp(a, "-K") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			fprintf(stderr,
			        "strip: -K: symbol-level keep not yet supported\n");
			continue;
		}
		if (strcmp(a, "-N") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			fprintf(stderr,
			        "strip: -N: symbol-level strip not yet supported\n");
			continue;
		}
		if (strcmp(a, "-R") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			opts.remove_secs[opts.remove_count++] = argv[i];
			continue;
		}
		if (strcmp(a, "-F") == 0) {
			if (++i >= argc) { usage(stderr); return 2; }
			continue; /* 接受并忽略 bfdname */
		}
		if (strncmp(a, "--remove-section=", 17) == 0) {
			opts.remove_secs[opts.remove_count++] = a + 17;
			continue;
		}
		if (strncmp(a, "--keep-section=", 15) == 0) {
			opts.keep_secs[opts.keep_count++] = a + 15;
			continue;
		}
		if (strncmp(a, "--keep-symbol=", 14) == 0 ||
		    strncmp(a, "--strip-symbol=", 15) == 0) {
			fprintf(stderr,
			        "strip: symbol-level filtering not yet supported\n");
			continue;
		}
		if (strcmp(a, "-p") == 0 || strcmp(a, "--preserve-dates") == 0)
			continue;
		if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
			opts.verbose = 1;
			continue;
		}
		if (a[0] == '-') {
			if (a[1] == 'o' && a[2] != '\0') {
				opts.output = a + 2;
				continue;
			}
			if (a[1] == 'R' && a[2] != '\0') {
				opts.remove_secs[opts.remove_count++] = a + 2;
				continue;
			}
			fprintf(stderr, "strip: unsupported option: %s\n", a);
			return 2;
		}
		files[file_count++] = a;
	}

	if (file_count == 0) {
		usage(stderr);
		return 2;
	}
	if (opts.output && file_count > 1) {
		fprintf(stderr,
		        "strip: -o cannot be used with multiple input files\n");
		return 2;
	}
	for (i = 0; i < file_count; ++i)
		if (strip_one(files[i], &opts) != 0)
			rc = 1;

	free(files);
	free(opts.remove_secs);
	free(opts.keep_secs);
	return rc;
}
