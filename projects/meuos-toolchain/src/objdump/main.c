/* objdump - display information from ELF64 object files.
 *
 * GNU objdump 兼容子集。重点支持 -d（反汇编），复用 libdisasm。
 * 其他选项：-h/-x/-s/-t/-r/-D/-a/-i/-H/-V。
 *
 * 反汇编输出格式与 GNU objdump 兼容：
 *   addr:  bytes          mnemonic  operands
 *   addr:  bytes          <symbol>:
 *
 * 仅支持 ELF64 little-endian。零宿主依赖：libelf + libdisasm + libc。 */
#include "mt/disasm.h"
#include "mt/elf.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MT_OBJDUMP_VERSION "0.2.0"

enum od_format {
	FMT_DEFAULT = 0,
	FMT_BFD
};

struct od_opts {
	int disasm;            /* -d */
	int disasm_all;        /* -D */
	int section_headers;   /* -h */
	int all_headers;       /* -x */
	int full_contents;     /* -s */
	int syms;              /* -t */
	int relocs;            /* -r */
	int archive;           /* -a */
	int info;              /* -i */
	const char *target_section; /* -j section */
	int private_headers;   /* -p */
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

/* ---- 符号表辅助（反汇编标注用） ---- */

struct od_sym {
	uint64_t value;
	uint64_t size;
	const char *name;
	int is_func;
};

struct od_symtab {
	struct od_sym *syms;
	size_t count;
};

static void
load_symtab(const unsigned char *bytes, size_t size,
            const struct mt_elf64_section *secs,
            uint16_t n, struct od_symtab *out)
{
	uint16_t i;
	struct mt_elf64_section symtab, strtab;
	uint64_t count, k;

	out->syms = NULL;
	out->count = 0;
	for (i = 0; i < n; ++i) {
		if (secs[i].type == MT_SHT_SYMTAB) {
			symtab = secs[i];
			break;
		}
	}
	if (i >= n)
		return;
	if (symtab.link >= n)
		return;
	strtab = secs[symtab.link];
	if (symtab.entry_size == 0)
		return;
	count = symtab.size / symtab.entry_size;
	out->syms = calloc(count, sizeof(struct od_sym));
	if (!out->syms)
		return;
	k = 0;
	for (i = 0; i < count; ++i) {
		struct mt_elf64_symbol sym;
		if (mt_elf64_get_symbol(bytes, size, &symtab, i, &sym) !=
		    MT_ELF_OK)
			continue;
		if (sym.name == 0 && sym.value == 0 && sym.info == 0)
			continue; /* NULL */
		out->syms[k].value = sym.value;
		out->syms[k].size = sym.size;
		out->syms[k].is_func =
		    (MT_ELF64_ST_TYPE(sym.info) == MT_STT_FUNC);
		(void)mt_elf64_get_string(bytes, size, &strtab, sym.name,
		                          &out->syms[k].name);
		if (out->syms[k].name && out->syms[k].name[0])
			++k;
	}
	out->count = k;
}

static void
free_symtab(struct od_symtab *st)
{
	free(st->syms);
	st->syms = NULL;
	st->count = 0;
}

/* 查找 addr 处的符号（值 == addr） */
static const char *
find_sym_at(const struct od_symtab *st, uint64_t addr)
{
	size_t i;
	for (i = 0; i < st->count; ++i)
		if (st->syms[i].value == addr)
			return st->syms[i].name;
	return NULL;
}

/* ---- 节区头显示 (-h) ---- */

static void
show_section_headers(const unsigned char *bytes, size_t size,
                     const struct mt_elf64_section *secs,
                     const char **names, uint16_t n)
{
	uint16_t i;
	(void)bytes;
	(void)size;
	printf("\n");
	printf("Sections:\n");
	printf("Idx Name          Size      VMA               "
	       "LMA               File off  Algn\n");
	for (i = 0; i < n; ++i) {
		const char *nm = names[i] ? names[i] : "";
		uint64_t flags = secs[i].flags;
		char flagnames[16];
		size_t fi = 0;
		if (flags & MT_SHF_WRITE) flagnames[fi++] = 'W';
		if (flags & MT_SHF_ALLOC) flagnames[fi++] = 'A';
		if (flags & MT_SHF_EXECINSTR) flagnames[fi++] = 'X';
		if (flags & MT_SHF_MERGE) flagnames[fi++] = 'M';
		if (flags & MT_SHF_STRINGS) flagnames[fi++] = 'S';
		if (flags & MT_SHF_TLS) flagnames[fi++] = 'T';
		flagnames[fi] = '\0';
		printf("%3u %-13s %08" PRIx64 "  %016" PRIx64 "  "
		       "%016" PRIx64 "  %08" PRIx64 "  2**%u  [%s]\n",
		       i, nm, secs[i].size, secs[i].address, secs[i].address,
		       secs[i].offset, /* log2(alignment) */
		       secs[i].alignment ? __builtin_ctzll(secs[i].alignment)
		                        : 0,
		       flagnames);
	}
	printf("Key to Flags: W (write), A (alloc), X (execute), "
	       "M (merge), S (strings), T (TLS)\n");
}

/* ---- 符号表显示 (-t) ---- */

static void
show_symbols(const unsigned char *bytes, size_t size,
             const struct mt_elf64_section *secs, const char **names,
             uint16_t n)
{
	uint16_t i;
	for (i = 0; i < n; ++i) {
		if (secs[i].type == MT_SHT_SYMTAB) {
			struct mt_elf64_section strtab;
			uint64_t count, j;
			if (secs[i].link >= n)
				continue;
			strtab = secs[secs[i].link];
			printf("\nSYMBOL TABLE:\n");
			if (secs[i].entry_size == 0)
				break;
			count = secs[i].size / secs[i].entry_size;
			for (j = 0; j < count; ++j) {
				struct mt_elf64_symbol sym;
				const char *name = "";
				const char *secname = "";
				if (mt_elf64_get_symbol(bytes, size, &secs[i],
				    j, &sym) != MT_ELF_OK)
					continue;
				(void)mt_elf64_get_string(bytes, size, &strtab,
				    sym.name, &name);
				if (sym.section < n)
					secname = names[sym.section] ?
				              names[sym.section] : "";
				printf("%016" PRIx64 " %c    %s\t%u\t%s\n",
				       sym.value,
				       (MT_ELF64_ST_BIND(sym.info) ==
				        MT_STB_LOCAL) ? 'l' : 'g',
				       mt_elf_section_type_name(
				           MT_ELF64_ST_TYPE(sym.info) ==
				           MT_STT_FUNC ? MT_STT_FUNC :
				           MT_STT_NOTYPE),
				       sym.section, name);
				(void)secname;
			}
			break;
		}
	}
	(void)names;
}

/* ---- hex dump (-s) ---- */

static void
hex_dump(const unsigned char *bytes, size_t size,
         const struct mt_elf64_section *sec, const char *name)
{
	uint64_t off;
	if (sec->type == MT_SHT_NOBITS) {
		printf("Contents of section %s:\n", name);
		return;
	}
	printf("Contents of section %s:\n", name);
	for (off = 0; off < sec->size; off += 16) {
		uint64_t addr = sec->address + off;
		char line[80];
		int pos = 0;
		uint64_t k;
		pos += snprintf(line + pos, sizeof(line) - pos,
		                 " %04" PRIx64 " ", addr);
		for (k = 0; k < 16; ++k) {
			if (off + k < sec->size) {
				pos += snprintf(line + pos,
				    sizeof(line) - pos, "%02x",
				    bytes[sec->offset + off + k]);
			} else {
				pos += snprintf(line + pos,
				    sizeof(line) - pos, "  ");
			}
			if (k == 7)
				pos += snprintf(line + pos,
				    sizeof(line) - pos, " ");
		}
		pos += snprintf(line + pos, sizeof(line) - pos, "  ");
		for (k = 0; k < 16 && off + k < sec->size; ++k) {
			unsigned char c = bytes[sec->offset + off + k];
			pos += snprintf(line + pos, sizeof(line) - pos,
			    "%c", isprint(c) ? c : '.');
		}
		printf("%s\n", line);
		(void)size;
	}
}

/* ---- 反汇编 (-d) ---- */

static int
is_disasm_section(const struct mt_elf64_section *sec, const char *name,
                  const struct od_opts *opts)
{
	if (opts->target_section)
		return strcmp(name, opts->target_section) == 0;
	if (opts->disasm_all)
		return sec->size > 0 && sec->type != MT_SHT_NOBITS;
	/* -d: 默认 .text 或可执行节区 */
	return (sec->flags & MT_SHF_EXECINSTR) != 0 ||
	       strcmp(name, ".text") == 0 ||
	       strncmp(name, ".text.", 6) == 0;
}

static void
disasm_section(const unsigned char *bytes, size_t size,
               const struct mt_elf64_section *sec, const char *name,
               uint64_t base_addr, const struct od_symtab *st)
{
	uint64_t off;
	const unsigned char *data;
	uint64_t sec_addr = sec->address ? sec->address : base_addr;

	if (sec->type == MT_SHT_NOBITS) {
		printf("Disassembly of section %s:\n", name);
		return;
	}
	if (sec->offset + sec->size > size) {
		fprintf(stderr, "objdump: %s: section out of range\n", name);
		return;
	}
	data = bytes + sec->offset;
	printf("\nDisassembly of section %s:\n", name);

	off = 0;
	while (off < sec->size) {
		struct mt_disasm_insn insn;
		const char *sym;
		int rc;
		uint64_t addr = sec_addr + off;
		/* 检查是否有符号在此地址 */
		sym = find_sym_at(st, addr);
		if (sym) {
			printf("\n%016" PRIx64 " <%s>:\n", addr, sym);
		}
		rc = mt_disasm_x86_64_one(data, sec->size, off, addr, &insn);
		if (rc != 0 && insn.length == 0)
			insn.length = 1;
		/* GNU objdump 格式: "  addr:  bytes  mnemonic  operands" */
		printf("  %" PRIx64 ":\t%-20s\t%s", addr, insn.bytes_hex,
		       insn.mnemonic);
		if (insn.operands[0])
			printf("\t%s", insn.operands);
		printf("\n");
		off += insn.length;
	}
}

/* ---- 重定位显示 (-r) ---- */

static void
show_relocs(const unsigned char *bytes, size_t size,
            const struct mt_elf64_section *secs, const char **names,
            uint16_t n)
{
	uint16_t i;
	for (i = 0; i < n; ++i) {
		if (secs[i].type == MT_SHT_RELA || secs[i].type == MT_SHT_REL) {
			uint64_t count, j;
			int is_rela = (secs[i].type == MT_SHT_RELA);
			if (secs[i].entry_size == 0)
				continue;
			count = secs[i].size / secs[i].entry_size;
			printf("\nRELOCATION RECORDS FOR [%s]:",
			       names[i] ? names[i] : "");
			printf("\nOFFSET           TYPE                     VALUE\n");
			for (j = 0; j < count; ++j) {
				struct mt_elf64_rela r;
				if (mt_elf64_get_rela(bytes, size, &secs[i], j,
				    &r) != MT_ELF_OK)
					continue;
				printf("%016" PRIx64 " %s %u\n", r.offset,
				       is_rela ? "R_X86_64" : "R_X86_64_REL",
				       MT_ELF64_R_SYM(r.info));
			}
			printf("\n");
		}
	}
	(void)names;
}

/* ---- 主处理 ---- */

static int
process_file(const char *path, const struct od_opts *opts)
{
	unsigned char *bytes;
	size_t size;
	struct mt_elf64_view view;
	struct mt_elf64_section *secs = NULL;
	const char **names = NULL;
	uint16_t n, i;
	enum mt_elf_status st;
	int rc = 1;
	struct od_symtab symtab = {0};
	struct mt_elf64_section shstrtab;

	bytes = read_file(path, &size);
	if (!bytes) {
		fprintf(stderr, "objdump: %s: cannot read\n", path);
		return 1;
	}
	st = mt_elf64_parse(bytes, size, &view);
	if (st != MT_ELF_OK) {
		fprintf(stderr, "objdump: %s: %s\n", path,
		        mt_elf_status_string(st));
		free(bytes);
		return 1;
	}
	n = view.section_count;
	if (n == 0) {
		fprintf(stderr, "objdump: %s: no sections\n", path);
		free(bytes);
		return 1;
	}
	secs = calloc(n, sizeof(*secs));
	names = calloc(n, sizeof(*names));
	if (!secs || !names) {
		fprintf(stderr, "objdump: out of memory\n");
		goto done;
	}
	for (i = 0; i < n; ++i) {
		st = mt_elf64_get_section(bytes, size, &view, i, &secs[i]);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "objdump: %s: %s\n", path,
			        mt_elf_status_string(st));
			goto done;
		}
	}
	st = mt_elf64_get_section(bytes, size, &view,
	                          view.section_name_index, &shstrtab);
	if (st == MT_ELF_OK && shstrtab.type == MT_SHT_STRTAB) {
		for (i = 0; i < n; ++i)
			(void)mt_elf64_get_string(bytes, size, &shstrtab,
			    secs[i].name, &names[i]);
	}

	/* 加载符号表（反汇编标注用） */
	if (opts->disasm || opts->disasm_all)
		load_symtab(bytes, size, secs, n, &symtab);

	/* -h 节区头 */
	if (opts->section_headers || opts->all_headers)
		show_section_headers(bytes, size, secs, names, n);

	/* -t 符号表 */
	if (opts->syms || opts->all_headers)
		show_symbols(bytes, size, secs, names, n);

	/* -r 重定位 */
	if (opts->relocs || opts->all_headers)
		show_relocs(bytes, size, secs, names, n);

	/* -s hex dump */
	if (opts->full_contents) {
		for (i = 0; i < n; ++i) {
			if (secs[i].size == 0)
				continue;
			if (opts->target_section &&
			    strcmp(names[i] ? names[i] : "",
			           opts->target_section) != 0)
				continue;
			hex_dump(bytes, size, &secs[i],
			         names[i] ? names[i] : "");
		}
	}

	/* -d / -D 反汇编 */
	if (opts->disasm || opts->disasm_all) {
		for (i = 0; i < n; ++i) {
			if (is_disasm_section(&secs[i],
			    names[i] ? names[i] : "", opts))
				disasm_section(bytes, size, &secs[i],
				    names[i] ? names[i] : "", 0, &symtab);
		}
	}

	/* 无选项时默认显示文件头摘要 */
	if (!opts->disasm && !opts->disasm_all && !opts->section_headers &&
	    !opts->all_headers && !opts->full_contents && !opts->syms &&
	    !opts->relocs && !opts->archive && !opts->info) {
		printf("\n%s:\tfile format elf64-x86-64\n", path);
		printf("architecture: i386:x86-64, flags 0x%08x:\n",
		       view.flags);
		printf("start address 0x%016" PRIx64 "\n", view.entry);
		show_section_headers(bytes, size, secs, names, n);
	}
	rc = 0;

done:
	free_symtab(&symtab);
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
	    "usage: objdump [options] file...\n"
	    "  -d, --disassemble      disassemble .text section\n"
	    "  -D, --disassemble-all  disassemble all sections\n"
	    "  -h, --section-headers  display section headers\n"
	    "  -x, --all-headers      display all headers\n"
	    "  -s, --full-contents    display section contents (hex dump)\n"
	    "  -t, --syms             display symbol table\n"
	    "  -r, --reloc            display relocations\n"
	    "  -a, --archive-headers  display archive headers\n"
	    "  -j SECTION             disassemble/dump SECTION only\n"
	    "  -i, --info             display object format info\n"
	    "  -H, --help             print this help\n"
	    "  -V, --version          print version\n");
}

int
main(int argc, char **argv)
{
	struct od_opts opts;
	int i;
	int first = 1;

	memset(&opts, 0, sizeof(opts));
	for (i = 1; i < argc; ++i) {
		const char *a = argv[i];
		if (strcmp(a, "--help") == 0 || strcmp(a, "-H") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
			printf("meuos-toolchain objdump %s (x86_64 bootstrap)\n",
			       MT_OBJDUMP_VERSION);
			return 0;
		}
		if (strcmp(a, "-d") == 0 || strcmp(a, "--disassemble") == 0) {
			opts.disasm = 1;
			continue;
		}
		if (strcmp(a, "-D") == 0 ||
		    strcmp(a, "--disassemble-all") == 0) {
			opts.disasm_all = 1;
			continue;
		}
		if (strcmp(a, "-h") == 0 ||
		    strcmp(a, "--section-headers") == 0) {
			opts.section_headers = 1;
			continue;
		}
		if (strcmp(a, "-x") == 0 || strcmp(a, "--all-headers") == 0) {
			opts.all_headers = 1;
			continue;
		}
		if (strcmp(a, "-s") == 0 ||
		    strcmp(a, "--full-contents") == 0) {
			opts.full_contents = 1;
			continue;
		}
		if (strcmp(a, "-t") == 0 || strcmp(a, "--syms") == 0) {
			opts.syms = 1;
			continue;
		}
		if (strcmp(a, "-r") == 0 || strcmp(a, "--reloc") == 0) {
			opts.relocs = 1;
			continue;
		}
		if (strcmp(a, "-a") == 0 ||
		    strcmp(a, "--archive-headers") == 0) {
			opts.archive = 1;
			continue;
		}
		if (strcmp(a, "-i") == 0 || strcmp(a, "--info") == 0) {
			opts.info = 1;
			printf("BFD header file: elf64-x86-64\n");
			continue;
		}
		if (strcmp(a, "-j") == 0) {
			if (++i >= argc) {
				usage(stderr);
				return 2;
			}
			opts.target_section = argv[i];
			continue;
		}
		if (a[0] == '-' && a[1] != '\0') {
			fprintf(stderr, "objdump: unknown option: %s\n", a);
			return 2;
		}
		/* 文件参数 */
		if (!first)
			printf("\n");
		if (process_file(a, &opts) != 0)
			return 1;
		first = 0;
	}

	if (first) {
		usage(stderr);
		return 2;
	}
	return 0;
}
