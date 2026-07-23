/* nm - list symbols from ELF64 files.
 *
 * GNU nm 兼容子集：默认输出 "<value> <type> <name>"，每行一个符号。
 * 选项：-a/-g/-u/--defined-only/-n/-p/-r/--size-sort/-S/-D/-f posix/-h/-V。
 *
 * 仅支持 ELF64 little-endian（与 mt 一致）。零宿主依赖：只用 libelf + libc。
 * locale 无关，输出 ASCII。 */
#include "mt/elf.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MT_NM_VERSION "0.2.0"

enum nm_format {
	FMT_BSD = 0,
	FMT_POSIX,
	FMT_SYSV
};

struct nm_opts {
	int debug_syms;        /* -a */
	int extern_only;       /* -g */
	int undefined_only;    /* -u */
	int defined_only;
	int numeric_sort;     /* -n */
	int no_sort;           /* -p */
	int reverse_sort;      /* -r */
	int size_sort;         /* --size-sort */
	int print_size;        /* -S */
	int dynamic;           /* -D */
	enum nm_format format;
};

struct nm_entry {
	uint64_t value;
	uint64_t size;
	char type_char;
	const char *name;
	const char *secname;
	int defined;
	unsigned char bind;
	unsigned char stype;
};

/* ---- 文件读取 ---- */

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

/* ---- 符号类型字符分类（GNU nm 兼容） ---- */

static char
classify_symbol(const struct mt_elf64_symbol *sym,
                const char *secname)
{
	unsigned bind = MT_ELF64_ST_BIND(sym->info);
	unsigned type = MT_ELF64_ST_TYPE(sym->info);
	int is_global = (bind == MT_STB_GLOBAL || bind == MT_STB_GNU_UNIQUE);
	int is_weak = (bind == MT_STB_WEAK);
	char c;

	if (sym->section == MT_SHN_UNDEF) {
		/* undefined: U / w (weak undef) / v (weak object undef) */
		if (is_weak)
			return (type == MT_STT_OBJECT) ? 'v' : 'w';
		return 'U';
	}
	if (sym->section == MT_SHN_ABS) {
		c = 'A';
	} else if (sym->section == MT_SHN_COMMON ||
	           type == MT_STT_COMMON) {
		c = 'C';
	} else if (type == MT_STT_GNU_IFUNC) {
		c = 'i';
	} else if (type == MT_STT_TLS) {
		c = 'D';
	} else if (type == MT_STT_OBJECT) {
		if (secname) {
			if (strcmp(secname, ".bss") == 0 ||
			    strcmp(secname, ".tbss") == 0)
				c = 'B';
			else if (strncmp(secname, ".rodata", 7) == 0 ||
			         strcmp(secname, ".eh_frame") == 0 ||
			         strcmp(secname, ".eh_frame_hdr") == 0 ||
			         strncmp(secname, ".rodata", 7) == 0)
				c = 'R';
			else
				c = 'D';
		} else {
			c = 'D';
		}
	} else if (type == MT_STT_FUNC) {
		c = 'T';
	} else if (type == MT_STT_NOTYPE) {
		/* NOTYPE: 按节区属性分类 */
		if (secname) {
			if (strcmp(secname, ".text") == 0 ||
			    strncmp(secname, ".text.", 6) == 0)
				c = 'T';
			else if (strcmp(secname, ".data") == 0 ||
			         strncmp(secname, ".data.", 6) == 0 ||
			         strncmp(secname, ".init_array", 11) == 0 ||
			         strncmp(secname, ".fini_array", 11) == 0)
				c = 'D';
			else if (strcmp(secname, ".bss") == 0)
				c = 'B';
			else if (strncmp(secname, ".rodata", 7) == 0)
				c = 'R';
			else
				c = 'N';
		} else {
			c = 'N';
		}
	} else {
		c = '?';
	}

	/* 大小写：global -> 大写, local -> 小写 */
	if (is_global)
		return (char)toupper((unsigned char)c);
	if (is_weak) {
		/* weak 已定义：大写（V/W 等），但 B/R 等保持原样 */
		if (c == 'D' || c == 'T' || c == 'B' || c == 'R' || c == 'V')
			return (char)toupper((unsigned char)c);
		return c;
	}
	return (char)tolower((unsigned char)c);
}

/* ---- 过滤 ---- */

static int
filter_symbol(const struct nm_entry *e, const struct nm_opts *opts)
{
	unsigned bind = e->bind;
	if (opts->extern_only &&
	    bind != MT_STB_GLOBAL && bind != MT_STB_WEAK &&
	    bind != MT_STB_GNU_UNIQUE)
		return 0;
	if (opts->undefined_only && e->defined)
		return 0;
	if (opts->defined_only && !e->defined)
		return 0;
	if (!opts->debug_syms) {
		/* 默认隐藏 STT_FILE 和 STT_SECTION */
		if (e->stype == MT_STT_FILE || e->stype == MT_STT_SECTION)
			return 0;
	}
	return 1;
}

/* ---- 排序 ---- */

static int
cmp_default(const void *a, const void *b)
{
	/* 默认按 symtab 顺序，这里用稳定方式：保持原序（qsort 不稳定，
	 * 但我们用辅助索引）。为简化，默认不排序直接输出。 */
	(void)a;
	(void)b;
	return 0;
}

static int
cmp_numeric(const void *a, const void *b)
{
	const struct nm_entry *ea = a;
	const struct nm_entry *eb = b;
	if (ea->value < eb->value)
		return -1;
	if (ea->value > eb->value)
		return 1;
	return 0;
}

static int
cmp_size(const void *a, const void *b)
{
	const struct nm_entry *ea = a;
	const struct nm_entry *eb = b;
	if (ea->size < eb->size)
		return 1;
	if (ea->size > eb->size)
		return -1;
	return 0;
}

static int
cmp_reverse(const void *a, const void *b)
{
	return -cmp_default(a, b);
}

static int
cmp_reverse_numeric(const void *a, const void *b)
{
	return -cmp_numeric(a, b);
}

static int
cmp_reverse_size(const void *a, const void *b)
{
	return -cmp_size(a, b);
}

/* ---- 输出 ---- */

static void
print_entry_bsd(const struct nm_entry *e, const struct nm_opts *opts)
{
	if (e->defined) {
		if (opts->print_size && e->size != 0)
			printf("%016" PRIx64 " %016" PRIx64 " %c %s\n",
			       e->value, e->size, e->type_char,
			       e->name ? e->name : "");
		else
			printf("%016" PRIx64 " %c %s\n",
			       e->value, e->type_char,
			       e->name ? e->name : "");
	} else {
		if (opts->print_size && e->size != 0)
			printf("                %016" PRIx64 " %c %s\n",
			       e->size, e->type_char,
			       e->name ? e->name : "");
		else
			printf("                %c %s\n",
			       e->type_char, e->name ? e->name : "");
	}
}

static void
print_entry_posix(const struct nm_entry *e, const struct nm_opts *opts)
{
	(void)opts;
	if (e->defined)
		printf("%s %c %016" PRIx64 " %s\n",
		       e->name ? e->name : "", e->type_char, e->value,
		       e->secname ? e->secname : "");
	else
		printf("%s %c\n", e->name ? e->name : "", e->type_char);
}

static void
print_entry(const struct nm_entry *e, const struct nm_opts *opts)
{
	if (opts->format == FMT_POSIX)
		print_entry_posix(e, opts);
	else
		print_entry_bsd(e, opts);
}

/* ---- 符号表查找 ---- */

static int
find_symtab(const struct mt_elf64_section *secs, uint16_t n,
            int want_dynamic,
            struct mt_elf64_section *out_symtab,
            struct mt_elf64_section *out_strtab)
{
	uint16_t i;
	uint32_t target_type = want_dynamic ? MT_SHT_DYNSYM : MT_SHT_SYMTAB;
	int found = 0;

	for (i = 0; i < n; ++i) {
		if (secs[i].type == target_type) {
			*out_symtab = secs[i];
			found = 1;
			break;
		}
	}
	if (!found)
		return -1;
	if (out_symtab->link >= n)
		return -1;
	if (secs[out_symtab->link].type != MT_SHT_STRTAB)
		return -1;
	*out_strtab = secs[out_symtab->link];
	return 0;
}

/* ---- 单文件处理 ---- */

static int
process_file(const char *path, const struct nm_opts *opts)
{
	unsigned char *bytes;
	size_t size;
	struct mt_elf64_view view;
	struct mt_elf64_section *secs = NULL;
	const char **secnames = NULL;
	struct mt_elf64_section symtab, strtab;
	struct nm_entry *entries = NULL;
	uint64_t sym_count, i;
	enum mt_elf_status st;
	uint16_t n, j;
	int rc = 1;
	int multi;

	multi = 0; /* 单文件时不打印头 */

	bytes = read_file(path, &size);
	if (!bytes) {
		fprintf(stderr, "nm: %s: cannot read\n", path);
		return 1;
	}
	st = mt_elf64_parse(bytes, size, &view);
	if (st != MT_ELF_OK) {
		fprintf(stderr, "nm: %s: %s\n", path, mt_elf_status_string(st));
		free(bytes);
		return 1;
	}
	n = view.section_count;
	if (n == 0) {
		fprintf(stderr, "nm: %s: no sections\n", path);
		free(bytes);
		return 1;
	}
	secs = calloc(n, sizeof(*secs));
	secnames = calloc(n, sizeof(*secnames));
	if (!secs || !secnames) {
		fprintf(stderr, "nm: out of memory\n");
		goto done;
	}
	for (j = 0; j < n; ++j) {
		st = mt_elf64_get_section(bytes, size, &view, j, &secs[j]);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "nm: %s: %s\n", path,
			        mt_elf_status_string(st));
			goto done;
		}
	}
	/* 读取节区名 */
	{
		struct mt_elf64_section shstrtab;
		st = mt_elf64_get_section(bytes, size, &view,
		                          view.section_name_index, &shstrtab);
		if (st == MT_ELF_OK && shstrtab.type == MT_SHT_STRTAB) {
			for (j = 0; j < n; ++j) {
				st = mt_elf64_get_string(bytes, size, &shstrtab,
				                         secs[j].name, &secnames[j]);
				if (st != MT_ELF_OK)
					secnames[j] = "";
			}
		}
	}

	if (find_symtab(secs, n, opts->dynamic, &symtab, &strtab) != 0) {
		if (multi)
			printf("\n%s:\n", path);
		if (opts->dynamic)
			fprintf(stderr, "nm: %s: no dynamic symbol table\n",
			        path);
		else
			fprintf(stderr, "nm: %s: no symbols\n", path);
		rc = 0;
		goto done;
	}

	if (symtab.entry_size == 0 ||
	    symtab.size / symtab.entry_size > 100000000ULL) {
		fprintf(stderr, "nm: %s: bad symtab entry size\n", path);
		goto done;
	}
	sym_count = symtab.size / symtab.entry_size;
	entries = calloc(sym_count, sizeof(*entries));
	if (!entries) {
		fprintf(stderr, "nm: out of memory\n");
		goto done;
	}
	{
		uint64_t k = 0;
		for (i = 0; i < sym_count; ++i) {
			struct mt_elf64_symbol sym;
			st = mt_elf64_get_symbol(bytes, size, &symtab, i, &sym);
			if (st != MT_ELF_OK) {
				fprintf(stderr, "nm: %s: %s\n", path,
				        mt_elf_status_string(st));
				goto done;
			}
			/* 跳过 symtab[0] 的 NULL 符号（STN_UNDEF） */
			if (i == 0 && sym.name == 0 && sym.info == 0 &&
			    sym.section == MT_SHN_UNDEF && sym.value == 0)
				continue;
			entries[k].value = sym.value;
			entries[k].size = sym.size;
			entries[k].bind = MT_ELF64_ST_BIND(sym.info);
			entries[k].stype = MT_ELF64_ST_TYPE(sym.info);
			entries[k].defined = (sym.section != MT_SHN_UNDEF);
			entries[k].type_char = classify_symbol(&sym,
			    (sym.section < n) ? secnames[sym.section] : NULL);
			(void)mt_elf64_get_string(bytes, size, &strtab,
			                          sym.name, &entries[k].name);
			if (sym.section < n)
				entries[k].secname = secnames[sym.section];
			++k;
		}
		sym_count = k;
	}

	/* 排序 */
	if (!opts->no_sort) {
		int (*cmp)(const void *, const void *) = cmp_default;
		if (opts->numeric_sort)
			cmp = opts->reverse_sort ? cmp_reverse_numeric
			                        : cmp_numeric;
		else if (opts->size_sort)
			cmp = opts->reverse_sort ? cmp_reverse_size : cmp_size;
		else if (opts->reverse_sort)
			cmp = cmp_reverse;
		qsort(entries, sym_count, sizeof(*entries), cmp);
	}

	if (multi)
		printf("\n%s:\n", path);

	for (i = 0; i < sym_count; ++i) {
		if (filter_symbol(&entries[i], opts))
			print_entry(&entries[i], opts);
	}
	rc = 0;

done:
	free(entries);
	free(secs);
	free(secnames);
	free(bytes);
	return rc;
}

/* ---- 参数解析 ---- */

static void
usage(FILE *out)
{
	fprintf(out,
	    "usage: nm [options] file...\n"
	    "  -a, --debug-syms       show all symbols (debug-only too)\n"
	    "  -g, --extern-only      show only external symbols\n"
	    "  -u, --undefined-only   show only undefined symbols\n"
	    "      --defined-only     show only defined symbols\n"
	    "  -n, --numeric-sort     sort by address\n"
	    "  -p, --no-sort          do not sort\n"
	    "  -r, --reverse-sort     reverse sort\n"
	    "      --size-sort        sort by size\n"
	    "  -S, --print-size       show symbol size\n"
	    "  -D, --dynamic          use dynamic symbol table\n"
	    "  -f {bsd|posix|sysv}    output format\n"
	    "  -h, --help             print this help\n"
	    "  -V, --version          print version\n");
}

int
main(int argc, char **argv)
{
	struct nm_opts opts;
	int i;
	int first_file = 1;

	memset(&opts, 0, sizeof(opts));
	opts.format = FMT_BSD;

	for (i = 1; i < argc; ++i) {
		const char *a = argv[i];
		if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
			printf("meuos-toolchain nm %s (x86_64 bootstrap)\n",
			       MT_NM_VERSION);
			return 0;
		}
		if (strcmp(a, "-a") == 0 || strcmp(a, "--debug-syms") == 0) {
			opts.debug_syms = 1;
			continue;
		}
		if (strcmp(a, "-g") == 0 || strcmp(a, "--extern-only") == 0) {
			opts.extern_only = 1;
			continue;
		}
		if (strcmp(a, "-u") == 0 ||
		    strcmp(a, "--undefined-only") == 0) {
			opts.undefined_only = 1;
			continue;
		}
		if (strcmp(a, "--defined-only") == 0) {
			opts.defined_only = 1;
			continue;
		}
		if (strcmp(a, "-n") == 0 || strcmp(a, "--numeric-sort") == 0) {
			opts.numeric_sort = 1;
			continue;
		}
		if (strcmp(a, "-p") == 0 || strcmp(a, "--no-sort") == 0) {
			opts.no_sort = 1;
			continue;
		}
		if (strcmp(a, "-r") == 0 || strcmp(a, "--reverse-sort") == 0) {
			opts.reverse_sort = 1;
			continue;
		}
		if (strcmp(a, "--size-sort") == 0) {
			opts.size_sort = 1;
			continue;
		}
		if (strcmp(a, "-S") == 0 || strcmp(a, "--print-size") == 0) {
			opts.print_size = 1;
			continue;
		}
		if (strcmp(a, "-D") == 0 || strcmp(a, "--dynamic") == 0) {
			opts.dynamic = 1;
			continue;
		}
		if (strcmp(a, "-f") == 0 || strncmp(a, "-f", 2) == 0) {
			const char *fmt = (a[2] == 0) ? argv[++i] : a + 2;
			if (i >= argc && a[2] == 0) {
				usage(stderr);
				return 2;
			}
			if (strcmp(fmt, "bsd") == 0)
				opts.format = FMT_BSD;
			else if (strcmp(fmt, "posix") == 0)
				opts.format = FMT_POSIX;
			else if (strcmp(fmt, "sysv") == 0)
				opts.format = FMT_SYSV;
			else {
				fprintf(stderr, "nm: unknown format %s\n", fmt);
				return 2;
			}
			continue;
		}
		if (a[0] == '-' && a[1] != '\0') {
			fprintf(stderr, "nm: unknown option: %s\n", a);
			return 2;
		}
		/* 文件参数 */
		if (argc > 3) {
			/* 多文件模式：打印文件头 */
			if (!first_file)
				printf("\n");
			printf("%s:\n", a);
		}
		if (process_file(a, &opts) != 0)
			return 1;
		first_file = 0;
	}

	if (first_file) {
		/* 无文件参数 */
		usage(stderr);
		return 2;
	}
	return 0;
}
