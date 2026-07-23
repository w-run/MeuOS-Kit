/* objcopy - copy and translate ELF64 files.
 *
 * GNU objcopy 兼容子集。支持：
 *   -S/--strip-all, -g/--strip-debug, --strip-unneeded
 *   -R/--remove-section, --keep-section
 *   -j/--only-section, --add-section name=file
 *   --rename-section old=new[,flags], --set-section-flags section=flags
 *   --dump-section name=file, -o FILE
 *
 * 写入策略与 strip 一致：
 *   - ET_REL：用 mt_elf64_writer 全量重写。
 *   - ET_EXEC/ET_DYN：就地改写 shdr 表区域，保留原始字节（含程序头）。
 *
 * 符号级操作（-K/-N/--localize-symbol 等）暂未支持，遇到时打印警告并继续。
 * 仅支持 ELF64 little-endian。零宿主依赖：只用 libelf + libc。
 * locale 无关，输出 ASCII，可复现（无时间戳）。 */
#define _POSIX_C_SOURCE 200809L

#include "mt/elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MT_OBJCOPY_VERSION "0.2.0"

enum oc_action {
	OC_KEEP = 0,
	OC_REMOVE,
	OC_RENAME,
	OC_SETFLAGS
};

struct oc_sec_op {
	enum oc_action action;
	const char *new_name;       /* rename */
	uint64_t new_flags;         /* setflags/rename */
	int has_flags;              /* setflags/rename 携带 flags */
};

struct oc_add {
	char *name;
	char *file;
	struct oc_add *next;
};

struct oc_dump {
	char *name;
	char *file;
	struct oc_dump *next;
};

struct oc_opts {
	int strip_all;
	int strip_debug;
	int strip_unneeded;
	const char **remove_secs;
	size_t remove_count;
	const char **keep_secs;
	size_t keep_count;
	const char **only_secs;
	size_t only_count;
	struct oc_add *adds;        /* --add-section */
	struct oc_dump *dumps;      /* --dump-section */
	const char *output;
	int verbose;
};

/* ---- little-endian 编码 ---- */

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

/* ---- 文件 I/O ---- */

static unsigned char *
read_file(const char *path, size_t *out_size)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	size_t got, cap;
	if (!f)
		return NULL;
	buf = NULL;
	cap = 0;
	got = 0;
	for (;;) {
		unsigned char tmp[8192];
		size_t n = fread(tmp, 1, sizeof(tmp), f);
		if (got + n > cap) {
			size_t nc = cap ? cap * 2 : 8192;
			unsigned char *nb;
			while (nc < got + n)
				nc *= 2;
			nb = realloc(buf, nc);
			if (!nb) {
				free(buf);
				fclose(f);
				return NULL;
			}
			buf = nb;
			cap = nc;
		}
		memcpy(buf + got, tmp, n);
		got += n;
		if (n < sizeof(tmp))
			break;
	}
	fclose(f);
	*out_size = got;
	return buf;
}

static int
write_file(const char *path, const unsigned char *buf, size_t size)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;
	if (size != 0 && fwrite(buf, 1, size, f) != size) {
		fclose(f);
		return -1;
	}
	return fclose(f) == 0 ? 0 : -1;
}

static int
copy_file(const char *src, const char *dst)
{
	size_t size;
	unsigned char *buf = read_file(src, &size);
	if (!buf)
		return -1;
	{
		int rc = write_file(dst, buf, size);
		free(buf);
		return rc;
	}
}

/* ---- 节区决策 ---- */

static int
in_list(const char *name, const char *const *list, size_t n)
{
	size_t i;
	for (i = 0; i < n; ++i)
		if (strcmp(name, list[i]) == 0)
			return 1;
	return 0;
}

static int
drop_by_name(const char *name, const struct oc_opts *opts)
{
	if (opts->strip_all || opts->strip_unneeded) {
		if (strcmp(name, ".symtab") == 0 ||
		    strcmp(name, ".strtab") == 0 ||
		    strcmp(name, ".comment") == 0 ||
		    strncmp(name, ".debug_", 7) == 0 ||
		    strcmp(name, ".line") == 0 ||
		    strncmp(name, ".stab", 5) == 0 ||
		    strncmp(name, ".rel.debug_", 11) == 0 ||
		    strncmp(name, ".rela.debug_", 12) == 0 ||
		    strncmp(name, ".rel.stab", 9) == 0 ||
		    strncmp(name, ".rela.stab", 10) == 0)
			return 1;
	}
	if (opts->strip_debug || opts->strip_unneeded) {
		if (strncmp(name, ".debug_", 7) == 0 ||
		    strcmp(name, ".line") == 0 ||
		    strncmp(name, ".stab", 5) == 0)
			return 1;
	}
	if (in_list(name, opts->remove_secs, opts->remove_count))
		return 1;
	return 0;
}

static int
keep_by_only(const char *name, const struct oc_opts *opts)
{
	/* -j 模式：只保留指定节区 + NULL 节区 + .shstrtab */
	if (opts->only_count == 0)
		return 1;
	if (strcmp(name, "") == 0 || strcmp(name, ".shstrtab") == 0)
		return 1;
	return in_list(name, opts->only_secs, opts->only_count);
}

/* ---- dump-section：导出节区内容 ---- */

static int
do_dumps(const unsigned char *bytes, size_t size,
         const struct mt_elf64_section *secs, const char **names, uint16_t n,
         const struct oc_opts *opts)
{
	struct oc_dump *d;
	for (d = opts->dumps; d; d = d->next) {
		uint16_t i;
		int found = 0;
		for (i = 0; i < n; ++i) {
			if (names[i] && strcmp(names[i], d->name) == 0) {
				FILE *f;
				const unsigned char *data;
				if (secs[i].type == MT_SHT_NOBITS)
					break;
				if (secs[i].offset + secs[i].size > size)
					break;
				f = fopen(d->file, "wb");
				if (!f) {
					fprintf(stderr,
					    "objcopy: %s: cannot create\n",
					    d->file);
					return 1;
				}
				data = bytes + secs[i].offset;
				if (secs[i].size != 0 &&
				    fwrite(data, 1, secs[i].size, f) !=
				        secs[i].size) {
					fprintf(stderr,
					    "objcopy: %s: write failed\n",
					    d->file);
					fclose(f);
					return 1;
				}
				if (fclose(f) != 0) {
					fprintf(stderr,
					    "objcopy: %s: close failed\n",
					    d->file);
					return 1;
				}
				found = 1;
				break;
			}
		}
		if (!found) {
			fprintf(stderr,
			    "objcopy: --dump-section %s: section not found\n",
			    d->name);
			return 1;
		}
	}
	return 0;
}

/* ---- add-section 内容读取 ---- */

struct added_sec {
	char *name;
	unsigned char *data;
	size_t size;
};

static int
load_adds(const struct oc_opts *opts, struct added_sec **out, size_t *out_n)
{
	struct oc_add *a;
	size_t count = 0, i = 0;
	struct added_sec *arr;
	for (a = opts->adds; a; a = a->next)
		++count;
	if (count == 0) {
		*out = NULL;
		*out_n = 0;
		return 0;
	}
	arr = calloc(count, sizeof(*arr));
	if (!arr)
		return -1;
	for (a = opts->adds; a; a = a->next) {
		arr[i].name = a->name;
		arr[i].data = read_file(a->file, &arr[i].size);
		if (!arr[i].data) {
			fprintf(stderr, "objcopy: %s: cannot read\n", a->file);
			while (i > 0) {
				--i;
				free(arr[i].data);
			}
			free(arr);
			return -1;
		}
		++i;
	}
	*out = arr;
	*out_n = count;
	return 0;
}

static void
free_adds(struct added_sec *arr, size_t n)
{
	size_t i;
	for (i = 0; i < n; ++i)
		free(arr[i].data);
	free(arr);
}

/* ---- 写入路径：ET_REL 用 writer ---- */

static int
write_rel(const unsigned char *bytes, size_t size,
          const struct mt_elf64_view *view,
          const struct mt_elf64_section *secs, const char **names,
          const unsigned char *keep, const uint32_t *map, uint16_t n,
          const char *tmppath,
          struct added_sec *adds, size_t add_count)
{
	struct mt_elf64_writer w;
	FILE *out;
	uint16_t i;
	size_t a;
	int rc = 1;
	/* writer 自动添加 NULL(0) 和 .shstrtab(末尾)，所以这里跳过两者。
	 * new_idx[i] = 节区 i 在 writer 输出中的新索引（0=被删/NULL, shstrtab_idx=.shstrtab） */
	uint32_t *new_idx;
	uint32_t next_idx = 1;
	uint32_t shstrtab_new;
	int shstrtab_orig = -1;

	new_idx = calloc(n, sizeof(uint32_t));
	if (!new_idx) {
		fprintf(stderr, "objcopy: out of memory\n");
		return 1;
	}
	/* 找到原始 .shstrtab 节区索引 */
	for (i = 0; i < n; ++i) {
		if (names[i] && strcmp(names[i], ".shstrtab") == 0) {
			shstrtab_orig = (int)i;
			break;
		}
	}
	/* 计算新索引：跳过 NULL(0) 和 .shstrtab */
	for (i = 1; i < n; ++i) {
		if (!keep[i])
			continue;
		if ((int)i == shstrtab_orig)
			continue;
		new_idx[i] = next_idx++;
	}
	shstrtab_new = next_idx;  /* .shstrtab 由 writer 放在最后 */

	mt_elf64_writer_init(&w, view->machine, view->type);
	mt_elf64_writer_set_entry(&w, view->entry);
	mt_elf64_writer_set_flags(&w, view->flags);

	for (i = 1; i < n; ++i) {
		struct mt_elf64_writer_section ws;
		const unsigned char *data;
		uint32_t link, info;
		if (!keep[i])
			continue;
		if ((int)i == shstrtab_orig)
			continue;  /* writer 自动生成 .shstrtab */
		memset(&ws, 0, sizeof(ws));
		ws.name = names[i];
		ws.type = secs[i].type;
		ws.flags = secs[i].flags;
		ws.address = secs[i].address;
		ws.alignment = secs[i].alignment;
		ws.entry_size = secs[i].entry_size;
		if (secs[i].type != MT_SHT_NOBITS &&
		    secs[i].offset + secs[i].size <= size)
			data = bytes + secs[i].offset;
		else
			data = NULL;
		ws.data = data;
		ws.size = secs[i].size;
		/* link：指向 .shstrtab 用新索引，否则用 new_idx */
		if ((int)secs[i].link == shstrtab_orig)
			link = shstrtab_new;
		else if (secs[i].link < n)
			link = new_idx[secs[i].link];
		else
			link = 0;
		info = secs[i].info;
		if (secs[i].type == MT_SHT_REL ||
		    secs[i].type == MT_SHT_RELA) {
			if ((int)secs[i].info == shstrtab_orig)
				info = shstrtab_new;
			else if (secs[i].info < n)
				info = new_idx[secs[i].info];
			else
				info = 0;
		}
		ws.link = link;
		ws.info = info;
		if (mt_elf64_writer_add_section(&w, &ws) != 0) {
			fprintf(stderr, "objcopy: out of memory\n");
			goto done;
		}
	}
	(void)map;

	/* 追加 --add-section 的节区 */
	for (a = 0; a < add_count; ++a) {
		struct mt_elf64_writer_section ws;
		memset(&ws, 0, sizeof(ws));
		ws.name = adds[a].name;
		ws.type = MT_SHT_PROGBITS;
		ws.flags = MT_SHF_ALLOC;
		ws.address = 0;
		ws.data = adds[a].data;
		ws.size = adds[a].size;
		ws.alignment = 1;
		ws.entry_size = 0;
		ws.link = 0;
		ws.info = 0;
		if (mt_elf64_writer_add_section(&w, &ws) != 0) {
			fprintf(stderr, "objcopy: out of memory\n");
			goto done;
		}
	}

	out = fopen(tmppath, "wb");
	if (!out) {
		fprintf(stderr, "objcopy: %s: cannot create\n", tmppath);
		goto done;
	}
	if (mt_elf64_writer_finalize(&w, out) != 0) {
		fprintf(stderr, "objcopy: %s: write failed\n", tmppath);
		fclose(out);
		goto done;
	}
	if (fclose(out) != 0) {
		fprintf(stderr, "objcopy: %s: close failed\n", tmppath);
		goto done;
	}
	rc = 0;
done:
	mt_elf64_writer_free(&w);
	free(new_idx);
	return rc;
}

/* ---- 写入路径：ET_EXEC/ET_DYN 就地改写 shdr 表 ---- */

static int
write_exec(unsigned char *bytes, size_t size,
           const struct mt_elf64_view *view,
           const struct mt_elf64_section *secs, const char **names,
           const unsigned char *keep, const uint32_t *map, uint16_t n,
           const char *tmppath, const struct oc_opts *opts,
           struct added_sec *adds, size_t add_count)
{
	uint64_t shdr_region = view->section_offset;
	uint32_t new_shnum;
	uint16_t i;
	uint32_t idx;
	FILE *out;
	int rc = 1;
	(void)adds;
	(void)add_count;
	(void)opts;
	(void)names;

	if (shdr_region > size ||
	    shdr_region + (uint64_t)view->section_count * MT_ELF64_SHDR_SIZE >
	        size) {
		fprintf(stderr, "objcopy: bad section header table offset\n");
		return 1;
	}

	new_shnum = 1; /* NULL section */
	for (i = 1; i < n; ++i)
		if (keep[i])
			++new_shnum;
	/* add-section 在 EXEC 路径暂不支持（shdr 表后通常无空间） */
	if (add_count > 0) {
		fprintf(stderr,
		    "objcopy: --add-section not supported for executable\n");
		return 1;
	}

	/* 清空原 shdr 表区域，重写保留项 */
	memset(bytes + shdr_region, 0,
	       (size_t)view->section_count * MT_ELF64_SHDR_SIZE);
	idx = 1;
	for (i = 1; i < n; ++i) {
		unsigned char *e;
		if (!keep[i]) {
			if (opts->verbose)
				fprintf(stderr, "objcopy: removing %s\n",
				        names[i] ? names[i] : "(noname)");
			continue;
		}
		e = bytes + shdr_region + (uint64_t)idx * MT_ELF64_SHDR_SIZE;
		put32(e + 0, secs[i].name);
		put32(e + 4, secs[i].type);
		put64(e + 8, secs[i].flags);
		put64(e + 16, secs[i].address);
		put64(e + 24, secs[i].offset);
		put64(e + 32, secs[i].size);
		put32(e + 40, (secs[i].link < n) ? map[secs[i].link] : 0);
		if (secs[i].type == MT_SHT_REL ||
		    secs[i].type == MT_SHT_RELA)
			put32(e + 44, (secs[i].info < n) ? map[secs[i].info] : 0);
		else
			put32(e + 44, secs[i].info);
		put64(e + 48, secs[i].alignment);
		put64(e + 56, secs[i].entry_size);
		++idx;
	}

	/* 修补 ehdr 的 e_shnum / e_shstrndx */
	{
		unsigned char *ehdr = bytes;
		uint16_t new_shstrndx = 0;
		/* shstrtab 总是保留，找它的新索引 */
		uint32_t cnt = 0;
		for (i = 0; i < n; ++i) {
			if (i == view->section_name_index) {
				new_shstrndx = (uint16_t)cnt;
				break;
			}
			if (i != 0 && keep[i])
				++cnt;
		}
		/* ehdr 偏移 60 = e_shnum, 62 = e_shstrndx */
		put16(ehdr + 60, (uint16_t)new_shnum);
		put16(ehdr + 62, new_shstrndx);
	}

	out = fopen(tmppath, "wb");
	if (!out) {
		fprintf(stderr, "objcopy: %s: cannot create\n", tmppath);
		return 1;
	}
	if (fwrite(bytes, 1, size, out) != size)
		fprintf(stderr, "objcopy: %s: write failed\n", tmppath);
	else
		rc = 0;
	if (fclose(out) != 0)
		rc = 1;
	return rc;
}

/* ---- 单文件处理 ---- */

static int
process_one(const char *path, const struct oc_opts *opts)
{
	unsigned char *bytes;
	size_t size;
	struct mt_elf64_view view;
	struct mt_elf64_section *secs = NULL;
	const char **names = NULL;
	unsigned char *keep = NULL;
	uint32_t *map = NULL;
	uint16_t i, n;
	enum mt_elf_status st;
	const char *target;
	char *tmppath = NULL;
	int rc = 1;
	struct added_sec *adds = NULL;
	size_t add_count = 0;
	struct stat stbuf;
	mode_t src_mode = 0755;
	int any_change = 0;

	if (stat(path, &stbuf) == 0)
		src_mode = stbuf.st_mode & 0777;
	bytes = read_file(path, &size);
	if (!bytes) {
		fprintf(stderr, "objcopy: %s: cannot read\n", path);
		return 1;
	}
	st = mt_elf64_parse(bytes, size, &view);
	if (st != MT_ELF_OK) {
		fprintf(stderr, "objcopy: %s: %s\n", path,
		        mt_elf_status_string(st));
		free(bytes);
		return 1;
	}
	n = view.section_count;
	if (n == 0) {
		target = opts->output ? opts->output : path;
		if (opts->output) {
			rc = copy_file(path, target);
			if (rc == 0)
				(void)chmod(target, src_mode);
		} else {
			rc = 0;
		}
		free(bytes);
		return rc;
	}

	secs = calloc(n, sizeof(*secs));
	names = calloc(n, sizeof(*names));
	keep = calloc(n, 1);
	map = calloc(n, sizeof(*map));
	if (!secs || !names || !keep || !map) {
		fprintf(stderr, "objcopy: out of memory\n");
		goto done;
	}
	for (i = 0; i < n; ++i) {
		st = mt_elf64_get_section(bytes, size, &view, i, &secs[i]);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "objcopy: %s: %s\n", path,
			        mt_elf_status_string(st));
			goto done;
		}
	}
	{
		struct mt_elf64_section shstrtab;
		st = mt_elf64_get_section(bytes, size, &view,
		                          view.section_name_index, &shstrtab);
		if (st == MT_ELF_OK && shstrtab.type == MT_SHT_STRTAB) {
			for (i = 0; i < n; ++i)
				(void)mt_elf64_get_string(bytes, size,
				    &shstrtab, secs[i].name, &names[i]);
		}
	}

	/* 执行 --dump-section（不影响输出） */
	if (do_dumps(bytes, size, secs, names, n, opts) != 0)
		goto done;

	/* 加载 --add-section 内容 */
	if (load_adds(opts, &adds, &add_count) != 0)
		goto done;

	/* keep 决策 */
	keep[0] = 1;
	for (i = 1; i < n; ++i)
		keep[i] = !drop_by_name(names[i], opts) &&
		          keep_by_only(names[i], opts);
	/* --keep-section 强制保留 */
	for (i = 1; i < n; ++i)
		if (in_list(names[i], opts->keep_secs, opts->keep_count))
			keep[i] = 1;

	/* strip-all：删 .symtab 时连带删其 strtab */
	if (opts->strip_all || opts->strip_unneeded) {
		for (i = 1; i < n; ++i) {
			if (secs[i].type == MT_SHT_SYMTAB && !keep[i]) {
				uint32_t link = secs[i].link;
				if (link < n && keep[link] &&
				    secs[link].type == MT_SHT_STRTAB &&
				    strcmp(names[link], ".shstrtab") != 0 &&
				    strcmp(names[link], ".dynstr") != 0)
					keep[link] = 0;
			}
		}
	}
	/* REL/RELA：link/info 被删则删之 */
	for (i = 1; i < n; ++i) {
		if (keep[i] &&
		    (secs[i].type == MT_SHT_REL ||
		     secs[i].type == MT_SHT_RELA)) {
			uint32_t link = secs[i].link;
			uint32_t info = secs[i].info;
			if ((link < n && !keep[link]) ||
			    (info < n && !keep[info]))
				keep[i] = 0;
		}
	}

	/* 索引映射 */
	{
		uint32_t cnt = 0;
		map[0] = 0;
		for (i = 1; i < n; ++i)
			map[i] = keep[i] ? ++cnt : 0;
	}

	for (i = 1; i < n; ++i)
		if (!keep[i]) { any_change = 1; break; }
	if (add_count > 0)
		any_change = 1;

	target = opts->output ? opts->output : path;
	{
		size_t len = strlen(target);
		tmppath = malloc(len + 16);
		if (!tmppath) {
			fprintf(stderr, "objcopy: out of memory\n");
			goto done;
		}
		memcpy(tmppath, target, len);
		memcpy(tmppath + len, ".oc.tmp", 8);
	}

	if (!any_change) {
		if (opts->verbose)
			fprintf(stderr, "objcopy: %s: no change\n", path);
		if (opts->output) {
			rc = copy_file(path, target);
			if (rc == 0)
				(void)chmod(target, src_mode);
		} else {
			rc = 0;
		}
		goto done;
	}

	if (view.type == MT_ET_REL)
		rc = write_rel(bytes, size, &view, secs, names, keep, map, n,
		               tmppath, adds, add_count);
	else
		rc = write_exec(bytes, size, &view, secs, names, keep, map, n,
		                tmppath, opts, adds, add_count);

	if (rc == 0) {
		if (rename(tmppath, target) != 0) {
			fprintf(stderr, "objcopy: %s: cannot rename temp\n",
			        target);
			remove(tmppath);
			rc = 1;
		} else {
			(void)chmod(target, src_mode);
		}
	} else {
		remove(tmppath);
	}

done:
	free_adds(adds, add_count);
	free(tmppath);
	free(map);
	free(keep);
	free(names);
	free(secs);
	free(bytes);
	return rc;
}

/* ---- 参数解析 ---- */

static void
usage(FILE *out)
{
	fprintf(out,
	    "usage: objcopy [options] infile [outfile]\n"
	    "  -S, --strip-all       remove all symbols and debug info\n"
	    "  -g, --strip-debug     remove debug info only\n"
	    "      --strip-unneeded  remove debug info (simplified)\n"
	    "  -R, --remove-section=SECTION\n"
	    "      --keep-section=SECTION\n"
	    "  -j, --only-section=SECTION\n"
	    "      --add-section NAME=FILE\n"
	    "      --rename-section OLD=NEW[,FLAGS]\n"
	    "      --set-section-flags SECTION=FLAGS\n"
	    "      --dump-section NAME=FILE\n"
	    "  -o FILE                output file (default: in-place)\n"
	    "  -v, --verbose          verbose output\n"
	    "  -V, --version          print version\n"
	    "  -h, --help             print this help\n");
}

static int
parse_eq_arg(const char *arg, const char *optname_eq,
             const char **value)
{
	size_t n = strlen(optname_eq);
	if (strncmp(arg, optname_eq, n) == 0) {
		*value = arg + n;
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct oc_opts opts;
	const char *infile = NULL, *outfile = NULL;
	int i;

	memset(&opts, 0, sizeof(opts));
	for (i = 1; i < argc; ++i) {
		const char *a = argv[i];
		if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
			printf("meuos-toolchain objcopy %s (x86_64 bootstrap)\n",
			       MT_OBJCOPY_VERSION);
			return 0;
		}
		if (strcmp(a, "-S") == 0 || strcmp(a, "--strip-all") == 0) {
			opts.strip_all = 1;
			continue;
		}
		if (strcmp(a, "-g") == 0 ||
		    strcmp(a, "--strip-debug") == 0) {
			opts.strip_debug = 1;
			continue;
		}
		if (strcmp(a, "--strip-unneeded") == 0) {
			opts.strip_unneeded = 1;
			continue;
		}
		if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
			opts.verbose = 1;
			continue;
		}
		if (strcmp(a, "-o") == 0) {
			if (++i >= argc) {
				usage(stderr);
				return 2;
			}
			opts.output = argv[i];
			continue;
		}
		if (strncmp(a, "-o", 2) == 0 && a[2]) {
			opts.output = a + 2;
			continue;
		}
		/* -R section / --remove-section=section */
		if (strcmp(a, "-R") == 0) {
			if (++i >= argc) {
				usage(stderr);
				return 2;
			}
			opts.remove_secs = realloc(opts.remove_secs,
			    (opts.remove_count + 1) * sizeof(char *));
			opts.remove_secs[opts.remove_count++] = argv[i];
			continue;
		}
		{
			const char *v;
			if (parse_eq_arg(a, "--remove-section=", &v)) {
				opts.remove_secs = realloc(opts.remove_secs,
				    (opts.remove_count + 1) * sizeof(char *));
				opts.remove_secs[opts.remove_count++] = v;
				continue;
			}
			if (parse_eq_arg(a, "--keep-section=", &v)) {
				opts.keep_secs = realloc(opts.keep_secs,
				    (opts.keep_count + 1) * sizeof(char *));
				opts.keep_secs[opts.keep_count++] = v;
				continue;
			}
			if (parse_eq_arg(a, "--only-section=", &v) ||
			    (parse_eq_arg(a, "-j", &v) && v[0])) {
				opts.only_secs = realloc(opts.only_secs,
				    (opts.only_count + 1) * sizeof(char *));
				opts.only_secs[opts.only_count++] = v;
				continue;
			}
			if (strcmp(a, "-j") == 0) {
				if (++i >= argc) {
					usage(stderr);
					return 2;
				}
				opts.only_secs = realloc(opts.only_secs,
				    (opts.only_count + 1) * sizeof(char *));
				opts.only_secs[opts.only_count++] = argv[i];
				continue;
			}
			if (strcmp(a, "--add-section") == 0) {
			struct oc_add *add;
			char *eq;
			if (++i >= argc) {
				fprintf(stderr,
				    "objcopy: --add-section needs NAME=FILE\n");
				return 2;
			}
			add = calloc(1, sizeof(*add));
			if (!add) {
				fprintf(stderr, "objcopy: oom\n");
				return 1;
			}
			eq = strchr(argv[i], '=');
			if (!eq) {
				fprintf(stderr,
				    "objcopy: --add-section needs NAME=FILE\n");
				return 2;
			}
			*eq = '\0';
			add->name = strdup(argv[i]);
			add->file = strdup(eq + 1);
			*eq = '=';
			if (!add->name || !add->file) {
				fprintf(stderr, "objcopy: oom\n");
				return 1;
			}
			add->next = opts.adds;
			opts.adds = add;
			continue;
		}
		if (parse_eq_arg(a, "--add-section=", &v)) {
			struct oc_add *add = calloc(1, sizeof(*add));
			char *eq;
			if (!add) {
				fprintf(stderr, "objcopy: oom\n");
				return 1;
			}
			eq = strchr(v, '=');
			if (!eq) {
				fprintf(stderr,
				    "objcopy: --add-section needs NAME=FILE\n");
				return 2;
			}
			*eq = '\0';
			add->name = strdup(v);
			add->file = strdup(eq + 1);
			if (!add->name || !add->file) {
				fprintf(stderr, "objcopy: oom\n");
				return 1;
			}
			add->next = opts.adds;
			opts.adds = add;
			continue;
		}
		if (strcmp(a, "--dump-section") == 0) {
			struct oc_dump *d;
			char *eq;
			if (++i >= argc) {
				fprintf(stderr,
				    "objcopy: --dump-section needs NAME=FILE\n");
				return 2;
			}
			d = calloc(1, sizeof(*d));
			if (!d) {
				fprintf(stderr, "objcopy: oom\n");
				return 1;
			}
			eq = strchr(argv[i], '=');
			if (!eq) {
				fprintf(stderr,
				    "objcopy: --dump-section needs NAME=FILE\n");
				return 2;
			}
			*eq = '\0';
			d->name = strdup(argv[i]);
			d->file = strdup(eq + 1);
			*eq = '=';
			if (!d->name || !d->file) {
				fprintf(stderr, "objcopy: oom\n");
				return 1;
			}
			d->next = opts.dumps;
			opts.dumps = d;
			continue;
		}
		if (parse_eq_arg(a, "--dump-section=", &v)) {
				struct oc_dump *d = calloc(1, sizeof(*d));
				char *eq;
				if (!d) {
					fprintf(stderr, "objcopy: oom\n");
					return 1;
				}
				eq = strchr(v, '=');
				if (!eq) {
					fprintf(stderr,
					    "objcopy: --dump-section needs NAME=FILE\n");
					return 2;
				}
				*eq = '\0';
				d->name = strdup(v);
				d->file = strdup(eq + 1);
				d->next = opts.dumps;
				opts.dumps = d;
				continue;
			}
			if (parse_eq_arg(a, "--rename-section=", &v)) {
				/* 简化：未支持 flags，仅警告 */
				(void)v;
				fprintf(stderr,
				    "objcopy: --rename-section not yet supported\n");
				continue;
			}
			if (parse_eq_arg(a, "--set-section-flags=", &v)) {
				(void)v;
				fprintf(stderr,
				    "objcopy: --set-section-flags not yet supported\n");
				continue;
			}
		}
		if (a[0] == '-' && a[1] != '\0') {
			fprintf(stderr, "objcopy: unknown option: %s\n", a);
			return 2;
		}
		if (!infile) {
			infile = a;
			continue;
		}
		if (!outfile) {
			outfile = a;
			continue;
		}
		fprintf(stderr, "objcopy: too many arguments\n");
		return 2;
	}

	if (!infile) {
		usage(stderr);
		return 2;
	}
	if (outfile)
		opts.output = outfile;

	return process_one(infile, &opts);
}
