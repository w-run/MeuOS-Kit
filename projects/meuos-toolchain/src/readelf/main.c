/* readelf - MeuOS Toolchain ELF structure viewer.
 *
 * 取代 GNU readelf 的子集实现：复用 libelf 读取 API，输出格式尽量与
 * GNU readelf（LC_ALL=C）保持一致，便于 diff 和 configure 脚本的
 * `readelf | grep` 模式。仅支持 ELF64 little-endian（x86_64 首期边界）。
 *
 * 选项：-h -l -S -s -r -d -x<sec> -a -W -H -V
 */
#include "mt/elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MT_READELF_VERSION "0.2.0"
#define MT_READELF_MAX_HEX 64

/* 命令行选项集合。每位 flag 对应一个 dump 模式。 */
struct options {
	int dump_header;      /* -h --file-header       */
	int dump_programs;    /* -l --program-headers   */
	int dump_sections;    /* -S --section-headers   */
	int dump_symbols;     /* -s --symbols           */
	int dump_relocs;      /* -r --relocs            */
	int dump_dynamic;     /* -d --dynamic           */
	int dump_debug;       /* -w --debug-dump        */
	int wide;             /* -W --wide              */
	const char *hex_secs[MT_READELF_MAX_HEX]; /* -x --hex-dump */
	int hex_count;
};

static void
usage(FILE *out)
{
	fprintf(out,
	        "Usage: readelf <option(s)> elf-file(s)\n"
	        " Display information about the contents of ELF format files\n"
	        " Options are:\n"
	        "  -a --all               Equivalent to: -h -l -S -s -r -d\n"
	        "  -h --file-header       Display the ELF file header\n"
	        "  -l --program-headers   Display the program headers\n"
	        "  -S --section-headers   Display the section headers\n"
	        "  -s --symbols           Display the symbol table\n"
	        "  -r --relocs            Display the relocations\n"
	        "  -d --dynamic           Display the dynamic section\n"
	        "  -w --debug-dump        Dump DWARF debug sections (raw)\n"
	        "  -x <name|num>          Dump the contents of section <name|num>\n"
	        "     --hex-dump=<name|num>\n"
	        "  -W --wide              Allow output width beyond 80 columns\n"
	        "  -H --help              Display this information\n"
	        "  -V --version           Display the version number of readelf\n");
}

/* ---- 文件载入 ---- */

/* 将整个文件读入 malloc 缓冲区。成功返回 0，失败返回 -1。 */
static int
load_file(const char *path, unsigned char **out_buf, size_t *out_size)
{
	FILE *fp;
	unsigned char *buf;
	long end;
	size_t got;

	fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "readelf: %s: cannot open\n", path);
		return -1;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fprintf(stderr, "readelf: %s: seek failed\n", path);
		fclose(fp);
		return -1;
	}
	end = ftell(fp);
	if (end < 0) {
		fprintf(stderr, "readelf: %s: tell failed\n", path);
		fclose(fp);
		return -1;
	}
	rewind(fp);
	buf = (unsigned char *)malloc((size_t)end ? (size_t)end : 1);
	if (!buf) {
		fprintf(stderr, "readelf: %s: out of memory\n", path);
		fclose(fp);
		return -1;
	}
	got = fread(buf, 1, (size_t)end, fp);
	fclose(fp);
	if (got != (size_t)end) {
		fprintf(stderr, "readelf: %s: short read\n", path);
		free(buf);
		return -1;
	}
	*out_buf = buf;
	*out_size = (size_t)end;
	return 0;
}

/* ---- 名称映射（GNU readelf 兼容） ---- */

static const char *
etype_name(uint16_t type)
{
	switch (type) {
	case MT_ET_REL: return "REL (Relocatable file)";
	case MT_ET_EXEC: return "EXEC (Executable file)";
	case MT_ET_DYN: return "DYN (Shared object file)";
	default: return NULL;
	}
}

/* GNU readelf 的 machine 描述串。libelf 的 mt_elf_machine_name 返回短名，
 * 这里给出与 binutils 一致的长描述。 */
static const char *
machine_long(uint16_t machine)
{
	switch (machine) {
	case MT_EM_386: return "Intel 80386";
	case MT_EM_X86_64: return "Advanced Micro Devices X86-64";
	case MT_EM_AARCH64: return "AArch64";
	case MT_EM_RISCV: return "RISC-V";
	case MT_EM_LOONGARCH: return "LoongArch";
	default: return NULL;
	}
}

static const char *
osabi_name(uint8_t osabi)
{
	switch (osabi) {
	case 0: return "UNIX - System V";
	case 1: return "HP-UX";
	case 2: return "NetBSD";
	case 3: return "GNU/Linux";
	case 6: return "Solaris";
	case 7: return "AIX";
	case 8: return "IRIX";
	case 9: return "FreeBSD";
	case 12: return "OpenVMS";
	default: return NULL;
	}
}

static const char *
sym_bind_name(unsigned bind)
{
	switch (bind) {
	case MT_STB_LOCAL: return "LOCAL";
	case MT_STB_GLOBAL: return "GLOBAL";
	case MT_STB_WEAK: return "WEAK";
	case MT_STB_GNU_UNIQUE: return "UNIQUE";
	default: return "UNKNOWN";
	}
}

static const char *
sym_type_name(unsigned type)
{
	switch (type) {
	case MT_STT_NOTYPE: return "NOTYPE";
	case MT_STT_OBJECT: return "OBJECT";
	case MT_STT_FUNC: return "FUNC";
	case MT_STT_SECTION: return "SECTION";
	case MT_STT_FILE: return "FILE";
	case MT_STT_COMMON: return "COMMON";
	case MT_STT_TLS: return "TLS";
	case MT_STT_GNU_IFUNC: return "IFUNC";
	default: return "UNKNOWN";
	}
}

static const char *
sym_vis_name(unsigned vis)
{
	switch (vis & MT_STV_MASK) {
	case MT_STV_DEFAULT: return "DEFAULT";
	case MT_STV_INTERNAL: return "INTERNAL";
	case MT_STV_HIDDEN: return "HIDDEN";
	case MT_STV_PROTECTED: return "PROTECTED";
	default: return "UNKNOWN";
	}
}

/* x86_64 重定位类型名。libelf 只提供常量，这里映射到 binutils 字符串。 */
static const char *
reloc_type_name(uint16_t machine, uint32_t type)
{
	if (machine == MT_EM_X86_64) {
		switch (type) {
		case MT_R_X86_64_NONE: return "R_X86_64_NONE";
		case MT_R_X86_64_64: return "R_X86_64_64";
		case MT_R_X86_64_PC32: return "R_X86_64_PC32";
		case MT_R_X86_64_GOT32: return "R_X86_64_GOT32";
		case MT_R_X86_64_PLT32: return "R_X86_64_PLT32";
		case MT_R_X86_64_COPY: return "R_X86_64_COPY";
		case MT_R_X86_64_GLOB_DAT: return "R_X86_64_GLOB_DAT";
		case MT_R_X86_64_JUMP_SLOT: return "R_X86_64_JUMP_SLOT";
		case MT_R_X86_64_RELATIVE: return "R_X86_64_RELATIVE";
		case MT_R_X86_64_GOTPCREL: return "R_X86_64_GOTPCREL";
		case MT_R_X86_64_32: return "R_X86_64_32";
		case MT_R_X86_64_32S: return "R_X86_64_32S";
		case MT_R_X86_64_16: return "R_X86_64_16";
		case MT_R_X86_64_PC16: return "R_X86_64_PC16";
		case MT_R_X86_64_8: return "R_X86_64_8";
		case MT_R_X86_64_PC8: return "R_X86_64_PC8";
		case MT_R_X86_64_DTPMOD64: return "R_X86_64_DTPMOD64";
		case MT_R_X86_64_DTPOFF64: return "R_X86_64_DTPOFF64";
		case MT_R_X86_64_TPOFF64: return "R_X86_64_TPOFF64";
		case MT_R_X86_64_TLSGD: return "R_X86_64_TLSGD";
		case MT_R_X86_64_TLSLD: return "R_X86_64_TLSLD";
		case MT_R_X86_64_DTPOFF32: return "R_X86_64_DTPOFF32";
		case MT_R_X86_64_GOTTPOFF: return "R_X86_64_GOTTPOFF";
		case MT_R_X86_64_TPOFF32: return "R_X86_64_TPOFF32";
		case MT_R_X86_64_PC64: return "R_X86_64_PC64";
		case MT_R_X86_64_GOTOFF64: return "R_X86_64_GOTOFF64";
		case MT_R_X86_64_GOTPC32: return "R_X86_64_GOTPC32";
		default: return NULL;
		}
	}
	return NULL;
}

/* 程序头标志串：R/W/E，execute 前置一个空格（GNU 风格）。 */
static void
phdr_flags_str(uint32_t flags, char *buf)
{
	char *p = buf;
	if (flags & MT_PF_R)
		*p++ = 'R';
	if (flags & MT_PF_W)
		*p++ = 'W';
	if (flags & MT_PF_X) {
		*p++ = ' ';
		*p++ = 'E';
	}
	*p = '\0';
}

/* 节区标志串：W/A/X/M/S/I/L/G/T/E，顺序与 GNU "Key to Flags" 一致。 */
static void
shdr_flags_str(uint64_t flags, char *buf)
{
	char *p = buf;
	if (flags & MT_SHF_WRITE)
		*p++ = 'W';
	if (flags & MT_SHF_ALLOC)
		*p++ = 'A';
	if (flags & MT_SHF_EXECINSTR)
		*p++ = 'X';
	if (flags & MT_SHF_MERGE)
		*p++ = 'M';
	if (flags & MT_SHF_STRINGS)
		*p++ = 'S';
	if (flags & MT_SHF_INFO_LINK)
		*p++ = 'I';
	if (flags & MT_SHF_LINK_ORDER)
		*p++ = 'L';
	if (flags & MT_SHF_GROUP)
		*p++ = 'G';
	if (flags & MT_SHF_TLS)
		*p++ = 'T';
	*p = '\0';
}

/* 节区类型名；未知类型格式化为 "0x%x"。 */
static void
section_type_str(uint32_t type, char *buf, size_t bufsz)
{
	const char *name = mt_elf_section_type_name(type);
	if (name)
		snprintf(buf, bufsz, "%s", name);
	else
		snprintf(buf, bufsz, "0x%08x", type);
}

/* 程序头类型名；补充 GNU_PROPERTY 等未在 libelf 表中的项。 */
static void
phdr_type_str(uint32_t type, char *buf, size_t bufsz)
{
	const char *name;
	if (type == 0x6474e553) {
		snprintf(buf, bufsz, "GNU_PROPERTY");
		return;
	}
	name = mt_elf_pt_name(type);
	if (name)
		snprintf(buf, bufsz, "%s", name);
	else
		snprintf(buf, bufsz, "0x%08x", type);
}

/* 符号 Ndx 列：UND/ABS/COM 或数字。 */
static void
sym_ndx_str(uint16_t shndx, char *buf, size_t bufsz)
{
	if (shndx == MT_SHN_UNDEF)
		snprintf(buf, bufsz, "UND");
	else if (shndx == MT_SHN_ABS)
		snprintf(buf, bufsz, "ABS");
	else if (shndx == MT_SHN_COMMON)
		snprintf(buf, bufsz, "COM");
	else
		snprintf(buf, bufsz, "%u", shndx);
}

/* 解析符号显示名：strtab 名优先；STT_SECTION 符号 strtab 名为空时
 * 回退到 shstrtab 中 sym->section 对应的节区名（GNU readelf 行为）。 */
static const char *
symbol_display_name(const unsigned char *bytes, size_t size,
                    const struct mt_elf64_view *view,
                    const struct mt_elf64_symbol *sym,
                    const struct mt_elf64_section *strtab)
{
	struct mt_elf64_section shstrtab;
	struct mt_elf64_section target;
	const char *s = NULL;

	/* strtab 名非空时直接返回。 */
	if (strtab && strtab->type == MT_SHT_STRTAB) {
		if (mt_elf64_get_string(bytes, size, strtab, sym->name, &s)
		    == MT_ELF_OK && s && s[0] != '\0')
			return s;
	}

	/* STT_SECTION 符号：回退到 shstrtab 中对应节区名。 */
	if (MT_ELF64_ST_TYPE(sym->info) != MT_STT_SECTION)
		return "";
	if (sym->section == MT_SHN_UNDEF || sym->section >= view->section_count)
		return "";
	if (view->section_name_index >= view->section_count)
		return "";
	if (mt_elf64_get_section(bytes, size, view,
	                         view->section_name_index, &shstrtab) != MT_ELF_OK)
		return "";
	if (shstrtab.type != MT_SHT_STRTAB)
		return "";
	if (mt_elf64_get_section(bytes, size, view, sym->section, &target)
	    != MT_ELF_OK)
		return "";
	if (mt_elf64_get_string(bytes, size, &shstrtab, target.name, &s)
	    == MT_ELF_OK && s)
		return s;
	return "";
}

/* 判断节区是否属于某个段（地址范围检查，仅 SHF_ALLOC 节区）。 */
static int
section_in_segment(const struct mt_elf64_section *sec,
                   const struct mt_elf64_phdr *ph)
{
	uint64_t sec_start, sec_end, seg_start, seg_end;

	if (!(sec->flags & MT_SHF_ALLOC))
		return 0;
	if (sec->type == MT_SHT_NULL)
		return 0;
	sec_start = sec->address;
	sec_end = sec->address + sec->size;
	seg_start = ph->vaddr;
	seg_end = ph->vaddr + ph->memsz;
	return sec_start >= seg_start && sec_end <= seg_end;
}

/* ---- dump: ELF Header (-h) ---- */

static void
dump_ehdr(const unsigned char *bytes, size_t size,
          const struct mt_elf64_view *view)
{
	const unsigned char *ident = bytes;
	char buf[32];
	const char *s;
	int i;

	(void)size;
	printf("ELF Header:\n");

	/* Magic: 16 字节 e_ident，每字节 " %02x"，行尾一个空格。 */
	printf("  Magic:  ");
	for (i = 0; i < MT_ELF_NIDENT; ++i)
		printf(" %02x", ident[i]);
	printf(" \n");

	printf("  %-34s %s\n", "Class:",
	       ident[4] == MT_ELFCLASS64 ? "ELF64" : "ELF32");
	printf("  %-34s %s\n", "Data:",
	       ident[5] == MT_ELFDATA2LSB
	           ? "2's complement, little endian"
	           : "2's complement, big endian");
	printf("  %-34s %d (current)\n", "Version:", ident[6]);
	s = osabi_name(ident[7]);
	printf("  %-34s %s\n", "OS/ABI:", s ? s : "<unknown>");
	printf("  %-34s %u\n", "ABI Version:", ident[8]);

	s = etype_name(view->type);
	printf("  %-34s %s\n", "Type:", s ? s : "UNKNOWN");
	s = machine_long(view->machine);
	if (!s)
		s = mt_elf_machine_name(view->machine);
	printf("  %-34s %s\n", "Machine:", s ? s : "unknown");
	printf("  %-34s 0x%x\n", "Version:", view->version);
	printf("  %-34s 0x%llx\n", "Entry point address:",
	       (unsigned long long)view->entry);
	printf("  %-34s %llu (bytes into file)\n", "Start of program headers:",
	       (unsigned long long)view->program_offset);
	printf("  %-34s %llu (bytes into file)\n", "Start of section headers:",
	       (unsigned long long)view->section_offset);
	printf("  %-34s 0x%x\n", "Flags:", view->flags);
	printf("  %-34s %u (bytes)\n", "Size of this header:",
	       view->header_size);
	printf("  %-34s %u (bytes)\n", "Size of program headers:",
	       view->program_entry_size);
	printf("  %-34s %u\n", "Number of program headers:",
	       view->program_count);
	printf("  %-34s %u (bytes)\n", "Size of section headers:",
	       view->section_entry_size);
	printf("  %-34s %u\n", "Number of section headers:",
	       view->section_count);
	printf("  %-34s %u\n", "Section header string table index:",
	       view->section_name_index);
	(void)buf;
}

/* ---- dump: Program Headers (-l) ---- */

static void
dump_phdrs(const unsigned char *bytes, size_t size,
           const struct mt_elf64_view *view)
{
	const char *etype;
	uint16_t i;

	if (view->program_count == 0) {
		/* REL 文件无程序头：GNU readelf 只输出一行提示。 */
		printf("\nThere are no program headers in this file.\n");
		return;
	}

	etype = etype_name(view->type);
	printf("\nElf file type is %s\n", etype ? etype : "UNKNOWN");
	printf("Entry point 0x%llx\n", (unsigned long long)view->entry);
	printf("There are %u program headers, starting at offset %llu\n\n",
	       view->program_count, (unsigned long long)view->program_offset);

	printf("Program Headers:\n");
	printf("  Type           Offset             VirtAddr           PhysAddr\n");
	printf("                 FileSiz            MemSiz              Flags  Align\n");

	for (i = 0; i < view->program_count; ++i) {
		struct mt_elf64_phdr ph;
		enum mt_elf_status st;
		char tbuf[24];
		char fbuf[8];

		st = mt_elf64_get_phdr(bytes, size, view, i, &ph);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "readelf: program header %u: %s\n", i,
			        mt_elf_status_string(st));
			continue;
		}
		phdr_type_str(ph.type, tbuf, sizeof(tbuf));
		phdr_flags_str(ph.flags, fbuf);
		printf("  %-15s0x%016llx 0x%016llx 0x%016llx\n", tbuf,
		       (unsigned long long)ph.offset, (unsigned long long)ph.vaddr,
		       (unsigned long long)ph.paddr);
		printf("                 0x%016llx 0x%016llx  %-7s0x%llx\n",
		       (unsigned long long)ph.filesz, (unsigned long long)ph.memsz,
		       fbuf, (unsigned long long)ph.align);

		/* PT_INTERP：打印请求的解释器路径。 */
		if (ph.type == MT_PT_INTERP && ph.filesz > 0
		    && ph.filesz < 4096 && ph.offset + ph.filesz <= size) {
			char interp[4096];
			uint64_t len = ph.filesz;
			memcpy(interp, bytes + ph.offset, len);
			/* 去掉尾部 NUL。 */
			if (len > 0 && interp[len - 1] == '\0')
				len--;
			interp[len] = '\0';
			printf("      [Requesting program interpreter: %s]\n",
			       interp);
		}
	}

	/* Section to Segment mapping：列出每个段包含的节区。 */
	{
		struct mt_elf64_section shstrtab;

		printf("\n Section to Segment mapping:\n");
		printf("  Segment Sections...\n");

		/* 取 shstrtab 解析节区名。 */
		memset(&shstrtab, 0, sizeof(shstrtab));
		if (view->section_name_index < view->section_count) {
			if (mt_elf64_get_section(bytes, size, view,
			    view->section_name_index, &shstrtab) != MT_ELF_OK
			    || shstrtab.type != MT_SHT_STRTAB)
				memset(&shstrtab, 0, sizeof(shstrtab));
		}

		for (i = 0; i < view->program_count; ++i) {
			struct mt_elf64_phdr ph;
			struct mt_elf64_section sec;
			uint16_t j;

			if (mt_elf64_get_phdr(bytes, size, view, i, &ph)
			    != MT_ELF_OK)
				continue;
			printf("   %02u     ", i);
			for (j = 0; j < view->section_count; ++j) {
				const char *name = NULL;
				if (mt_elf64_get_section(bytes, size, view,
				    j, &sec) != MT_ELF_OK)
					continue;
				if (!section_in_segment(&sec, &ph))
					continue;
				if (shstrtab.type == MT_SHT_STRTAB)
					mt_elf64_get_string(bytes, size,
					    &shstrtab, sec.name, &name);
				if (!name)
					name = "<?>";
				printf("%s ", name);
			}
			printf("\n");
		}
	}
}

/* ---- dump: Section Headers (-S) ---- */

/* 将节区名截断到 GNU 非宽模式下的 17 字符（12 + "[...]"）。 */
static void
truncate_name(const char *name, char *buf, size_t bufsz)
{
	size_t len = strlen(name);
	if (len > 17)
		snprintf(buf, bufsz, "%.12s[...]", name);
	else
		snprintf(buf, bufsz, "%s", name);
}

static void
dump_shdrs(const unsigned char *bytes, size_t size,
           const struct mt_elf64_view *view)
{
	struct mt_elf64_section shstrtab;
	struct mt_elf64_section sec;
	enum mt_elf_status st;
	uint16_t i;

	if (view->section_count == 0) {
		printf("There are 0 section headers.\n");
		return;
	}

	printf("There are %u section headers, starting at offset 0x%llx:\n\n",
	       view->section_count, (unsigned long long)view->section_offset);

	/* 取 shstrtab 用于解析节区名。 */
	if (view->section_name_index >= view->section_count) {
		memset(&shstrtab, 0, sizeof(shstrtab));
	} else {
		st = mt_elf64_get_section(bytes, size, view,
		                          view->section_name_index, &shstrtab);
		if (st != MT_ELF_OK || shstrtab.type != MT_SHT_STRTAB)
			memset(&shstrtab, 0, sizeof(shstrtab));
	}

	printf("Section Headers:\n");
	printf("  [Nr] Name              Type             Address           Offset\n");
	printf("       Size              EntSize          Flags  Link  Info  Align\n");

	for (i = 0; i < view->section_count; ++i) {
		char namebuf[32];
		char tbuf[20];
		char fbuf[12];
		char fbuf2[14];
		const char *name;

		st = mt_elf64_get_section(bytes, size, view, i, &sec);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "readelf: section header %u: %s\n", i,
			        mt_elf_status_string(st));
			continue;
		}
		name = "<?>";
		if (shstrtab.type == MT_SHT_STRTAB) {
			const char *s;
			if (mt_elf64_get_string(bytes, size, &shstrtab,
			                        sec.name, &s) == MT_ELF_OK)
				name = s;
		}
		truncate_name(name, namebuf, sizeof(namebuf));
		section_type_str(sec.type, tbuf, sizeof(tbuf));
		shdr_flags_str(sec.flags, fbuf);
		/* GNU 先用 %2s 右对齐（单字符前补空格），再 %-8s 左对齐。 */
		snprintf(fbuf2, sizeof(fbuf2), "%2s", fbuf);
		printf("  [%2u] %-18s%-17s%016llx  %08llx\n", i, namebuf, tbuf,
		       (unsigned long long)sec.address,
		       (unsigned long long)sec.offset);
		printf("       %016llx  %016llx  %-8s%2u %5u     %llu\n",
		       (unsigned long long)sec.size,
		       (unsigned long long)sec.entry_size, fbuf2, sec.link,
		       sec.info, (unsigned long long)sec.alignment);
	}

	printf("Key to Flags:\n");
	printf("  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),\n");
	printf("  L (link order), O (extra OS processing required), G (group), T (TLS),\n");
	printf("  C (compressed), x (unknown), o (OS specific), E (exclude),\n");
	printf("  D (mbind), l (large), p (processor specific)\n");
}

/* ---- dump: Symbols (-s) ---- */

static void
dump_symbol_table(const unsigned char *bytes, size_t size,
                  const struct mt_elf64_view *view,
                  const struct mt_elf64_section *symtab,
                  const char *table_name)
{
	struct mt_elf64_section strtab;
	enum mt_elf_status st;
	uint64_t count, n;
	const char *tname;

	if (symtab->entry_size == 0 || symtab->size == 0)
		return;
	if (symtab->entry_size < MT_ELF32_SYM_SIZE)
		return;
	count = symtab->size / symtab->entry_size;

	/* sh_link 指向关联的 strtab。 */
	memset(&strtab, 0, sizeof(strtab));
	if (symtab->link < view->section_count) {
		st = mt_elf64_get_section(bytes, size, view,
		                          (uint16_t)symtab->link, &strtab);
		if (st != MT_ELF_OK || strtab.type != MT_SHT_STRTAB)
			memset(&strtab, 0, sizeof(strtab));
	}

	printf("\nSymbol table '%s' contains %llu entries:\n", table_name,
	       (unsigned long long)count);
	printf("   Num:    Value          Size Type    Bind   Vis      Ndx Name\n");

	for (n = 0; n < count; ++n) {
		struct mt_elf64_symbol sym;
		char ndxbuf[12];
		const char *name;

		st = mt_elf64_get_symbol(bytes, size, symtab, n, &sym);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "readelf: symbol %llu: %s\n",
			        (unsigned long long)n, mt_elf_status_string(st));
			continue;
		}
		name = symbol_display_name(bytes, size, view, &sym, &strtab);
		sym_ndx_str(sym.section, ndxbuf, sizeof(ndxbuf));
		tname = sym_type_name(MT_ELF64_ST_TYPE(sym.info));
		printf("%6llu: %016llx %5llu %-7s %-6s %-7s %4s %s\n",
		       (unsigned long long)n, (unsigned long long)sym.value,
		       (unsigned long long)sym.size, tname,
		       sym_bind_name(MT_ELF64_ST_BIND(sym.info)),
		       sym_vis_name(sym.other), ndxbuf, name);
	}
}

static void
dump_symbols(const unsigned char *bytes, size_t size,
             const struct mt_elf64_view *view)
{
	struct mt_elf64_section sec;
	enum mt_elf_status st;
	uint16_t i;
	int found = 0;

	/* GNU 先输出 .dynsym，再输出 .symtab。 */
	for (i = 0; i < view->section_count; ++i) {
		st = mt_elf64_get_section(bytes, size, view, i, &sec);
		if (st != MT_ELF_OK)
			continue;
		if (sec.type == MT_SHT_DYNSYM) {
			dump_symbol_table(bytes, size, view, &sec, ".dynsym");
			found = 1;
		}
	}
	for (i = 0; i < view->section_count; ++i) {
		st = mt_elf64_get_section(bytes, size, view, i, &sec);
		if (st != MT_ELF_OK)
			continue;
		if (sec.type == MT_SHT_SYMTAB) {
			dump_symbol_table(bytes, size, view, &sec, ".symtab");
			found = 1;
		}
	}
	if (!found)
		printf("\nNo symbols in this file.\n");
}

/* ---- dump: Relocations (-r) ---- */

static void
dump_reloc_section(const unsigned char *bytes, size_t size,
                   const struct mt_elf64_view *view,
                   const struct mt_elf64_section *relsec,
                   const char *sec_name)
{
	struct mt_elf64_section symtab;
	struct mt_elf64_section strtab;
	enum mt_elf_status st;
	uint64_t count, n;
	int is_rela;

	if (relsec->type != MT_SHT_RELA && relsec->type != MT_SHT_REL)
		return;
	is_rela = (relsec->type == MT_SHT_RELA);
	count = is_rela ? relsec->size / 24 : relsec->size / 16;
	if (relsec->entry_size && relsec->size / relsec->entry_size > 0)
		count = relsec->size / relsec->entry_size;

	printf("\nRelocation section '%s' at offset 0x%llx contains %llu %s:\n",
	       sec_name, (unsigned long long)relsec->offset,
	       (unsigned long long)count,
	       count == 1 ? "entry" : "entries");
	if (is_rela)
		printf("  Offset          Info           Type           Sym. Value    Sym. Name + Addend\n");
	else
		printf("  Offset          Info           Type           Sym. Value    Sym. Name\n");

	/* sh_link 指向关联的 symtab，symtab.sh_link 指向 strtab。 */
	memset(&symtab, 0, sizeof(symtab));
	if (relsec->link < view->section_count) {
		st = mt_elf64_get_section(bytes, size, view,
		                          (uint16_t)relsec->link, &symtab);
		if (st != MT_ELF_OK)
			memset(&symtab, 0, sizeof(symtab));
	}
	memset(&strtab, 0, sizeof(strtab));
	if ((symtab.type == MT_SHT_SYMTAB || symtab.type == MT_SHT_DYNSYM)
	    && symtab.link < view->section_count) {
		if (mt_elf64_get_section(bytes, size, view,
		    (uint16_t)symtab.link, &strtab) != MT_ELF_OK
		    || strtab.type != MT_SHT_STRTAB)
			memset(&strtab, 0, sizeof(strtab));
	}

	for (n = 0; n < count; ++n) {
		struct mt_elf64_rela r;
		struct mt_elf64_symbol sym;
		const char *tname;
		const char *sname = "";
		uint32_t sym_idx, r_type;
		unsigned long long symval = 0;
		int have_sym = 0;

		st = mt_elf64_get_rela(bytes, size, relsec, n, &r);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "readelf: reloc %llu: %s\n",
			        (unsigned long long)n, mt_elf_status_string(st));
			continue;
		}
		sym_idx = MT_ELF64_R_SYM(r.info);
		r_type = MT_ELF64_R_TYPE(r.info);
		tname = reloc_type_name(view->machine, r_type);
		if (!tname) {
			static char tbuf[20];
			snprintf(tbuf, sizeof(tbuf), "0x%08x", r_type);
			tname = tbuf;
		}
		if (symtab.type == MT_SHT_SYMTAB || symtab.type == MT_SHT_DYNSYM) {
			if (mt_elf64_get_symbol(bytes, size, &symtab, sym_idx,
			                        &sym) == MT_ELF_OK) {
				have_sym = 1;
				symval = sym.value;
				sname = symbol_display_name(bytes, size, view,
				                            &sym, &strtab);
			}
		}
		(void)have_sym;
		printf("%012llx  %012llx %-17.17s %016llx %s",
		       (unsigned long long)r.offset,
		       (unsigned long long)r.info, tname, symval, sname);
		if (is_rela) {
			/* GNU readelf addend：正值 " + %llx"，负值 " - %llx"（绝对值十六进制）。 */
			if (r.addend < 0)
				printf(" - %llx",
				       (unsigned long long)(-r.addend));
			else
				printf(" + %llx",
				       (unsigned long long)r.addend);
		}
		printf("\n");
	}
}

static void
dump_relocs(const unsigned char *bytes, size_t size,
            const struct mt_elf64_view *view)
{
	struct mt_elf64_section shstrtab;
	struct mt_elf64_section sec;
	enum mt_elf_status st;
	uint16_t i;
	int found = 0;

	/* 取 shstrtab 解析节区名。 */
	if (view->section_name_index >= view->section_count) {
		printf("\nThere are no relocations in this file.\n");
		return;
	}
	st = mt_elf64_get_section(bytes, size, view,
	                          view->section_name_index, &shstrtab);
	if (st != MT_ELF_OK || shstrtab.type != MT_SHT_STRTAB) {
		printf("\nThere are no relocations in this file.\n");
		return;
	}

	for (i = 0; i < view->section_count; ++i) {
		char namebuf[64];
		const char *name;
		st = mt_elf64_get_section(bytes, size, view, i, &sec);
		if (st != MT_ELF_OK)
			continue;
		if (sec.type != MT_SHT_RELA && sec.type != MT_SHT_REL)
			continue;
		name = "<?>";
		{
			const char *s;
			if (mt_elf64_get_string(bytes, size, &shstrtab,
			                        sec.name, &s) == MT_ELF_OK)
				name = s;
		}
		snprintf(namebuf, sizeof(namebuf), "%s", name);
		dump_reloc_section(bytes, size, view, &sec, namebuf);
		found = 1;
	}
	if (!found)
		printf("\nThere are no relocations in this file.\n");
}

/* ---- dump: Dynamic (-d) ---- */

/* 从 bytes 读取一个 Elf64_Dyn（16 字节，小端）。 */
static void
read_dyn(const unsigned char *bytes, uint64_t offset,
         int64_t *tag, uint64_t *val)
{
	const unsigned char *p = bytes + offset;
	uint64_t lo, hi;
	lo = (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
	     ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24);
	hi = (uint64_t)p[4] | ((uint64_t)p[5] << 8) |
	     ((uint64_t)p[6] << 16) | ((uint64_t)p[7] << 24);
	*tag = (int64_t)(lo | (hi << 32));
	lo = (uint64_t)p[8] | ((uint64_t)p[9] << 8) |
	     ((uint64_t)p[10] << 16) | ((uint64_t)p[11] << 24);
	hi = (uint64_t)p[12] | ((uint64_t)p[13] << 8) |
	     ((uint64_t)p[14] << 16) | ((uint64_t)p[15] << 24);
	*val = lo | (hi << 32);
}

/* libelf 未覆盖的 DT 标签名补充。 */
static const char *
dt_name_local(uint64_t tag)
{
	switch (tag) {
	case 0x6ffffff0: return "VERSYM";
	case 0x6ffffffc: return "VERDEF";
	case 0x6ffffffd: return "VERDEFNUM";
	case 0x6ffffffe: return "VERNEED";
	case 0x6fffffff: return "VERNEEDNUM";
	case 29: return "RUNPATH";
	default: return mt_elf_dt_name(tag);
	}
}

/* 判断 DT tag 是否为"字节数"类型（GNU 显示 "N (bytes)"）。 */
static int
dt_is_bytes(int64_t tag)
{
	switch (tag) {
	case MT_DT_PLTRELSZ:
	case MT_DT_RELASZ:
	case MT_DT_RELAENT:
	case MT_DT_RELSZ:
	case MT_DT_RELENT:
	case MT_DT_STRSZ:
	case MT_DT_SYMENT:
	case MT_DT_INIT_ARRAYSZ:
	case MT_DT_FINI_ARRAYSZ:
		return 1;
	default:
		return 0;
	}
}

static void
dump_dynamic(const unsigned char *bytes, size_t size,
             const struct mt_elf64_view *view)
{
	struct mt_elf64_section dyn;
	struct mt_elf64_section strtab;
	enum mt_elf_status st;
	uint16_t i;
	int found = 0;
	uint64_t total, count, n;

	/* 查找 SHT_DYNAMIC 节区。 */
	for (i = 0; i < view->section_count; ++i) {
		st = mt_elf64_get_section(bytes, size, view, i, &dyn);
		if (st != MT_ELF_OK)
			continue;
		if (dyn.type == MT_SHT_DYNAMIC) {
			found = 1;
			break;
		}
	}
	if (!found) {
		printf("\nThere is no dynamic section in this file.\n");
		return;
	}
	if (dyn.size == 0 || dyn.offset + dyn.size > size) {
		printf("\nThere is no dynamic section in this file.\n");
		return;
	}
	total = dyn.size / 16;

	/* 统计条目数：遇到第一个 DT_NULL 即收尾（含 NULL 本身）。 */
	count = total;
	for (n = 0; n < total; ++n) {
		int64_t tag;
		uint64_t val;
		read_dyn(bytes, dyn.offset + n * 16, &tag, &val);
		if (tag == MT_DT_NULL) {
			count = n + 1;
			break;
		}
	}

	/* sh_link 指向 .dynstr。 */
	memset(&strtab, 0, sizeof(strtab));
	if (dyn.link < view->section_count) {
		st = mt_elf64_get_section(bytes, size, view,
		                          (uint16_t)dyn.link, &strtab);
		if (st != MT_ELF_OK || strtab.type != MT_SHT_STRTAB)
			memset(&strtab, 0, sizeof(strtab));
	}

	printf("\nDynamic section at offset 0x%llx contains %llu entries:\n",
	       (unsigned long long)dyn.offset, (unsigned long long)count);
	printf("  Tag        Type                         Name/Value\n");

	for (n = 0; n < count; ++n) {
		int64_t tag;
		uint64_t val;
		const char *tname;
		char namebuf[40];
		char typebuf[44];
		char vbuf[64];

		read_dyn(bytes, dyn.offset + n * 16, &tag, &val);
		tname = dt_name_local((uint64_t)tag);
		if (!tname) {
			snprintf(namebuf, sizeof(namebuf), "0x%llx",
			         (unsigned long long)(uint64_t)tag);
			tname = namebuf;
		}

		/* 构造 "(NAME)" 格式的类型字段。 */
		snprintf(typebuf, sizeof(typebuf), "(%s)", tname);

		/* 格式化值。 */
		if (tag == MT_DT_NEEDED && strtab.type == MT_SHT_STRTAB) {
			const char *lib = "";
			const char *s;
			if (mt_elf64_get_string(bytes, size, &strtab,
			                        (uint32_t)val, &s) == MT_ELF_OK)
				lib = s;
			snprintf(vbuf, sizeof(vbuf),
			         "Shared library: [%s]", lib);
		} else if (tag == MT_DT_SONAME && strtab.type == MT_SHT_STRTAB) {
			const char *s = "<bad>";
			mt_elf64_get_string(bytes, size, &strtab,
			                    (uint32_t)val, &s);
			snprintf(vbuf, sizeof(vbuf),
			         "Library soname: [%s]", s ? s : "<bad>");
		} else if (tag == MT_DT_RPATH && strtab.type == MT_SHT_STRTAB) {
			const char *s = "<bad>";
			mt_elf64_get_string(bytes, size, &strtab,
			                    (uint32_t)val, &s);
			snprintf(vbuf, sizeof(vbuf),
			         "Library rpath: [%s]", s ? s : "<bad>");
		} else if (tag == 29 && strtab.type == MT_SHT_STRTAB) {
			/* DT_RUNPATH */
			const char *s = "<bad>";
			mt_elf64_get_string(bytes, size, &strtab,
			                    (uint32_t)val, &s);
			snprintf(vbuf, sizeof(vbuf),
			         "Library runpath: [%s]", s ? s : "<bad>");
		} else if (dt_is_bytes(tag)) {
			snprintf(vbuf, sizeof(vbuf), "%llu (bytes)",
			         (unsigned long long)val);
		} else if ((uint64_t)tag == 0x6fffffff) {
			/* VERNEEDNUM: 十进制无前缀。 */
			snprintf(vbuf, sizeof(vbuf), "%llu",
			         (unsigned long long)val);
		} else if (tag == MT_DT_PLTREL) {
			/* DT_PLTREL: 值为 DT_RELA(7) 或 DT_REL(17)，打印名称。 */
			if (val == 7)
				snprintf(vbuf, sizeof(vbuf), "RELA");
			else if (val == 17)
				snprintf(vbuf, sizeof(vbuf), "REL");
			else
				snprintf(vbuf, sizeof(vbuf), "%llu",
				         (unsigned long long)val);
		} else {
			snprintf(vbuf, sizeof(vbuf), "0x%llx",
			         (unsigned long long)val);
		}

		printf(" 0x%016llx %-21s%s\n",
		       (unsigned long long)(uint64_t)tag, typebuf, vbuf);

		if (tag == MT_DT_NULL)
			break;
	}
}

/* ---- dump: Hex Dump (-x) ---- */

/* 检查节区是否有重定位节区引用它（RELA/REL 节的 sh_info == sec_idx）。 */
static int
section_has_relocations(const unsigned char *bytes, size_t size,
                        const struct mt_elf64_view *view, uint16_t sec_idx)
{
	struct mt_elf64_section sec;
	enum mt_elf_status st;
	uint16_t i;

	for (i = 0; i < view->section_count; ++i) {
		st = mt_elf64_get_section(bytes, size, view, i, &sec);
		if (st != MT_ELF_OK)
			continue;
		if ((sec.type == MT_SHT_RELA || sec.type == MT_SHT_REL) &&
		    sec.info == sec_idx)
			return 1;
	}
	return 0;
}

static void
dump_hexdump_section(const unsigned char *bytes, size_t size,
                     const struct mt_elf64_section *sec,
                     const char *sec_name, int has_relocs)
{
	const unsigned char *data;
	uint64_t off = 0;

	/* GNU readelf 在 "Hex dump" 前输出一个空行。 */
	printf("\n");
	if (sec->type == MT_SHT_NOBITS) {
		printf("Hex dump of section '%s':\n", sec_name);
		printf("  Section '%s' has no data to dump.\n", sec_name);
		return;
	}
	if (sec->size == 0 || sec->offset + sec->size > size) {
		printf("Hex dump of section '%s':\n", sec_name);
		return;
	}
	data = bytes + sec->offset;
	printf("Hex dump of section '%s':\n", sec_name);
	if (has_relocs)
		printf(" NOTE: This section has relocations against it, but these have NOT been applied to this dump.\n");
	while (off < sec->size) {
		char hex[40];
		int hlen = 0;
		uint64_t chunk = sec->size - off < 16 ? sec->size - off : 16;
		uint64_t i, j;

		for (i = 0; i < 16; i += 4) {
			for (j = 0; j < 4; ++j) {
				if (i + j < chunk)
					hlen += snprintf(hex + hlen,
					                 sizeof(hex) - hlen,
					                 "%02x",
					                 data[off + i + j]);
				else
					hlen += snprintf(hex + hlen,
					                 sizeof(hex) - hlen,
					                 "  ");
			}
			if (i + 4 <= chunk)
				hex[hlen++] = ' ';
		}
		while (hlen < 36)
			hex[hlen++] = ' ';
		hex[hlen] = '\0';

		printf("  0x%08llx %s", (unsigned long long)(sec->address + off),
		       hex);
		for (i = 0; i < chunk; ++i) {
			unsigned char c = data[off + i];
			putchar(c >= 0x20 && c < 0x7f ? c : '.');
		}
		putchar('\n');
		off += 16;
	}
	printf("\n");
}

static void
dump_hexdump(const unsigned char *bytes, size_t size,
             const struct mt_elf64_view *view, const char *spec)
{
	struct mt_elf64_section shstrtab;
	struct mt_elf64_section sec;
	enum mt_elf_status st;
	uint16_t i;
	char *end;
	long num;

	/* 数字：按节区索引；否则按名称查找。 */
	num = strtol(spec, &end, 0);
	if (*spec != '\0' && *end == '\0' && num >= 0 &&
	    num < view->section_count) {
		st = mt_elf64_get_section(bytes, size, view,
		                          (uint16_t)num, &sec);
		if (st != MT_ELF_OK) {
			fprintf(stderr, "readelf: section %ld: %s\n", num,
			        mt_elf_status_string(st));
			return;
		}
		dump_hexdump_section(bytes, size, &sec, spec,
		                     section_has_relocations(bytes, size, view,
		                                              (uint16_t)num));
		return;
	}

	/* 按名称查找。 */
	if (view->section_name_index >= view->section_count) {
		fprintf(stderr, "readelf: section '%s' not found\n", spec);
		return;
	}
	st = mt_elf64_get_section(bytes, size, view,
	                          view->section_name_index, &shstrtab);
	if (st != MT_ELF_OK || shstrtab.type != MT_SHT_STRTAB) {
		fprintf(stderr, "readelf: section '%s' not found\n", spec);
		return;
	}
	for (i = 0; i < view->section_count; ++i) {
		const char *name;
		st = mt_elf64_get_section(bytes, size, view, i, &sec);
		if (st != MT_ELF_OK)
			continue;
		name = NULL;
		if (mt_elf64_get_string(bytes, size, &shstrtab,
		                        sec.name, &name) != MT_ELF_OK)
			continue;
		if (name && strcmp(name, spec) == 0) {
			dump_hexdump_section(bytes, size, &sec, spec,
			                     section_has_relocations(bytes, size,
			                                              view, i));
			return;
		}
	}
	fprintf(stderr, "readelf: section '%s' not found\n", spec);
	printf("\nSection '%s' does not exist.\n", spec);
}

/* ---- DWARF debug-section dump (-w --debug-dump) ---------------------- */

/* Read a DWARF LE uleb128 at *pp; advances *pp. */
static uint64_t
read_uleb(const unsigned char **pp, const unsigned char *end)
{
	const unsigned char *p = *pp;
	uint64_t val = 0;
	unsigned shift = 0;
	unsigned char b;

	if (p >= end)
		return 0;
	do {
		b = *p++;
		if (p > end)
			break;
		val |= (uint64_t)(b & 0x7f) << shift;
		shift += 7;
	} while (b & 0x80);
	*pp = p;
	return val;
}

/* Read a DWARF LE sleb128 at *pp; advances *pp. */
static int64_t
read_sleb(const unsigned char **pp, const unsigned char *end)
{
	const unsigned char *p = *pp;
	uint64_t val = 0;
	unsigned shift = 0;
	unsigned char b;

	if (p >= end)
		return 0;
	do {
		b = *p++;
		if (p > end)
			break;
		val |= (uint64_t)(b & 0x7f) << shift;
		shift += 7;
	} while (b & 0x80);
	if (shift < 64 && (b & 0x40))
		val |= (uint64_t)-1 << shift;
	*pp = p;
	return (int64_t)val;
}

/* Decode a DWARF v4 .debug_line line-number program and print the
 * resulting (address, line) table plus file names.  Follows the DWARF4
 * 6.2.2 line-number program header and 6.2.5.1 standard opcodes. */
static void
dump_debug_line(const unsigned char *data, size_t dsize)
{
	const unsigned char *p = data;
	uint64_t unit_length, header_length;
	uint16_t version;
	uint8_t min_ilen, max_ops, default_is_stmt,
	        line_range, opcode_base;
	int8_t line_base;
	const unsigned char *prog, *prog_end, *q;
	int64_t line;
	uint64_t addr = 0, file = 1, col = 0, isa = 0;
	int is_stmt;
	unsigned opcodes[255];
	unsigned i, op;

	if (dsize < 14)
		return;
	unit_length = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	              (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
	p += 4;
	if (unit_length >= 0xfffffff0u || (uint64_t)unit_length > (uint64_t)dsize - 4) {
		/* DWARF32 unit_length == 0xffffffff means DWARF64; not
		 * supported here yet. */
		return;
	}
	version = (uint16_t)p[0] | (uint16_t)p[1] << 8;
	p += 2;
	if (version < 3 || version > 5)
		return;    /* only decode DWARF v3/v4/v5 header layout */
	header_length = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	                (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
	p += 4;
	min_ilen = *p++;
	if (version >= 4) {
		max_ops = *p++;
		if (max_ops == 0)
			return;
	} else {
		max_ops = 1;
	}
	default_is_stmt = *p++;
	line_base = *p++;
	line_range = *p++;
	opcode_base = *p++;
	if (opcode_base == 0)
		return;
	for (i = 0; i + 1 < opcode_base; ++i)
		opcodes[i] = *p++;
	prog = p;
	prog_end = data + 4 + unit_length;
	if (prog > prog_end)
		return;

	printf("\nThe .debug_line section:\n");

	/* include_directories (empty in our output) */
	q = prog;
	while (q < prog_end && *q != 0) {
		while (q < prog_end && *q != 0)
			++q;
		if (q < prog_end)
			++q;
	}
	if (q < prog_end)
		++q;   /* skip the terminating 0 */

	/* file_names */
	printf("\nFile name                Line number    Starting address\n");
	{
		int idx = 1;
		unsigned char *fname = NULL;
		size_t flen = 0;
		while (q < prog_end && *q != 0) {
			const unsigned char *s = q;
			size_t n = 0;
			while (q < prog_end && *q != 0) {
				++q; ++n;
			}
			if (q < prog_end)
				++q;   /* skip null */
			/* directory index */
			read_uleb(&q, prog_end);
			/* mtime */
			read_uleb(&q, prog_end);
			/* length */
			read_uleb(&q, prog_end);
			if (n + 1 > flen) {
				fname = realloc(fname, n + 1);
				flen = n + 1;
			}
			if (fname && n > 0) {
				memcpy(fname, s, n);
				fname[n] = '\0';
				printf("%-25s%4d\n", fname, idx);
			}
			++idx;
		}
		free(fname);
	}

	/* line-number program starts after the header_length field, which is
	 * measured from the byte following the header_length field (i.e.
	 * offset 4+2+4 = 10 from the start of .debug_line). */
	prog = data + 10 + header_length;
	if (prog > prog_end)
		prog = prog_end;
	printf("\nAddress            Line   Column File   ISA  Discriminator Op\n");
	line = default_is_stmt ? 1 : 0;
	is_stmt = default_is_stmt;
	while (prog < prog_end) {
		unsigned char b = *prog++;
		if (b == 0) {
			/* extended opcode: 0x00, uleb length (incl. subopcode),
			 * subopcode, then length-1 bytes of arguments. */
			const unsigned char *op_start = prog - 1;
			unsigned exlen = (unsigned)read_uleb(&prog, prog_end);
			unsigned char subop;
			unsigned k;

			if (exlen == 0 || prog >= prog_end)
				break;
			subop = *prog++;
			if (subop == 1) {  /* DW_LNE_end_sequence */
				printf("0x%08llx %8lld\n",
				       (unsigned long long)addr,
				       (long long)line);
				line = default_is_stmt ? 1 : 0;
				addr = 0;
				is_stmt = default_is_stmt;
			} else if (subop == 2 && exlen >= 2) {  /* set_address */
				uint64_t a = 0;
				int addr_size = (int)exlen - 1;
				if (addr_size > 8) addr_size = 8;
				for (k = 0; k < (unsigned)addr_size; ++k)
					a |= (uint64_t)*prog++ << (8 * k);
				addr = a;
			} else if (subop == 3) {  /* define_file */
				while (prog < prog_end && *prog)
					++prog;
				if (prog < prog_end) ++prog;
				read_uleb(&prog, prog_end);
				read_uleb(&prog, prog_end);
				read_uleb(&prog, prog_end);
			}
			/* skip to end of the extended opcode */
			prog = op_start + 1 + exlen;
			if (prog > prog_end)
				prog = prog_end;
			continue;
		} else if (b < opcode_base) {
			switch (b) {
			case 1: /* DW_LNS_copy */
				printf("0x%08llx %8lld %3llu %2llu %3llu\n",
				       (unsigned long long)addr,
				       (long long)line, (unsigned long long)col,
				       (unsigned long long)file,
				       (unsigned long long)isa);
				/* (basic_block / prologue_end / epilogue_begin state
				 * reset after the row is emitted; not printed) */
				(void)is_stmt;
				break;			case 2: /* DW_LNS_advance_pc */
				addr += read_uleb(&prog, prog_end) * min_ilen;
				break;
			case 3: /* DW_LNS_advance_line */
				line += read_sleb(&prog, prog_end);
				break;
			case 4: /* DW_LNS_set_file */
				file = read_uleb(&prog, prog_end);
				break;
			case 5: /* DW_LNS_set_column */
				col = read_uleb(&prog, prog_end);
				break;
			case 6: /* DW_LNS_negate_stmt */
				is_stmt = !is_stmt;
				break;
			case 7: /* DW_LNS_set_basic_block */
				/* state consumed by the next row; not printed */
				break;
			case 8: /* DW_LNS_const_add_pc */
				addr += ((255 - opcode_base) / line_range) * min_ilen;
				break;
			case 9: /* DW_LNS_fixed_advance_pc */
				if (prog + 2 <= prog_end) {
					addr += (unsigned)prog[0] | (unsigned)prog[1] << 8;
					prog += 2;
				}
				break;
			case 10: /* DW_LNS_set_prologue_end */
			case 11: /* DW_LNS_set_epilogue_begin */
				/* row flags; not printed */
				break;
			case 12: /* DW_LNS_set_isa */
				isa = read_uleb(&prog, prog_end);
				break;
			default:
				for (op = 0; op < opcodes[b - 1] && prog < prog_end; ++op)
					read_uleb(&prog, prog_end);
				break;
			}
		} else {
			/* special opcode */
			unsigned adj = b - opcode_base;
			addr += (adj / line_range) * min_ilen;
			line += line_base + (adj % line_range);
			printf("0x%08llx %8lld %3llu %2llu %3llu\n",
			       (unsigned long long)addr,
			       (long long)line, (unsigned long long)col,
			       (unsigned long long)file, (unsigned long long)isa);
			(void)is_stmt;
		}
	}
	printf("\n");
}

/* Map a handful of DWARF tags / attributes to human-readable names;
 * unknown ones fall back to "DW_TAG_*"/"DW_AT_*" numeric form. */
static const char *
dwarf_tag_name(uint64_t tag)
{
	switch (tag) {
	case 0x01: return "array_type";
	case 0x02: return "class_type";
	case 0x03: return "entry_point";
	case 0x04: return "enumeration_type";
	case 0x05: return "formal_parameter";
	case 0x08: return "imported_declaration";
	case 0x0a: return "label";
	case 0x0b: return "lexical_block";
	case 0x0d: return "member";
	case 0x0f: return "pointer_type";
	case 0x10: return "reference_type";
	case 0x11: return "compile_unit";
	case 0x12: return "string_type";
	case 0x13: return "structure_type";
	case 0x15: return "subroutine_type";
	case 0x16: return "typedef";
	case 0x17: return "union_type";
	case 0x18: return "unspecified_parameters";
	case 0x1c: return "inheritance";
	case 0x1d: return "inlined_subroutine";
	case 0x1f: return "ptr_to_member_type";
	case 0x20: return "set_type";
	case 0x21: return "subrange_type";
	case 0x24: return "base_type";
	case 0x26: return "const_type";
	case 0x27: return "constant";
	case 0x28: return "enumerator";
	case 0x2a: return "friend";
	case 0x2e: return "subprogram";
	case 0x2f: return "template_type_parameter";
	case 0x30: return "template_value_parameter";
	case 0x34: return "variable";
	case 0x35: return "volatile_type";
	case 0x36: return "dwarf_procedure";
	case 0x37: return "restrict_type";
	case 0x39: return "namespace";
	case 0x3b: return "unspecified_type";
	case 0x40: return "call_site";
	default:  return NULL;
	}
}

static const char *
dwarf_attr_name(uint64_t attr)
{
	switch (attr) {
	case 0x01: return "sibling";
	case 0x02: return "location";
	case 0x03: return "name";
	case 0x09: return "ordering";
	case 0x0b: return "byte_size";
	case 0x0d: return "bit_size";
	case 0x10: return "stmt_list";
	case 0x11: return "low_pc";
	case 0x12: return "high_pc";
	case 0x13: return "language";
	case 0x16: return "encoding";
	case 0x17: return "visibility";
	case 0x18: return "import";
	case 0x1c: return "const_value";
	case 0x1b: return "comp_dir";
	case 0x20: return "inline";
	case 0x22: return "lower_bound";
	case 0x25: return "producer";
	case 0x27: return "prototyped";
	case 0x2f: return "upper_bound";
	case 0x31: return "abstract_origin";
	case 0x34: return "artificial";
	case 0x38: return "data_member_location";
	case 0x39: return "decl_column";
	case 0x3a: return "decl_file";
	case 0x3b: return "decl_line";
	case 0x3c: return "declaration";
	case 0x3e: return "encoding";
	case 0x3f: return "external";
	case 0x40: return "frame_base";
	case 0x42: return "identifier_case";
	case 0x47: return "specification";
	case 0x48: return "static_link";
	case 0x49: return "type";
	case 0x4f: return "associated";
	case 0x6b: return "data_bit_offset";
	case 0x6e: return "linkage_name";
	case 0x79: return "macros";
	case 0x7a: return "call_all_calls";
	case 0x7b: return "call_all_source_calls";
	case 0x7c: return "call_all_tail_calls";
	case 0x7d: return "call_return_pc";
	case 0x7e: return "call_value";
	case 0x7f: return "call_origin";
	case 0x2116: return "GNU_all_tail_call_sites";
	case 0x2117: return "GNU_all_call_sites";
	case 0x51: return "base_type"; /* reserved; unused in practice */
	default:  return NULL;
	}
}

static const char *
dwarf_form_name(uint64_t form)
{
	switch (form) {
	case 0x01: return "addr";
	case 0x03: return "block2";
	case 0x04: return "block4";
	case 0x05: return "data2";
	case 0x06: return "data4";
	case 0x07: return "data8";
	case 0x08: return "string";
	case 0x09: return "block";
	case 0x0a: return "block1";
	case 0x0b: return "data1";
	case 0x0c: return "flag";
	case 0x0d: return "sdata";
	case 0x0e: return "strp";
	case 0x0f: return "udata";
	case 0x10: return "ref_addr";
	case 0x11: return "ref1";
	case 0x12: return "ref2";
	case 0x13: return "ref4";
	case 0x14: return "ref8";
	case 0x15: return "ref_udata";
	case 0x16: return "indirect";
	case 0x17: return "sec_offset";
	case 0x18: return "exprloc";
	case 0x19: return "flag_present";
	case 0x1a: return "strx";
	case 0x1b: return "addrx";
	case 0x1c: return "ref_sup4";
	case 0x1d: return "strp_sup";
	case 0x1e: return "data16";
	case 0x1f: return "line_strp";
	case 0x20: return "ref_sig8";
	case 0x21: return "implicit_const";
	case 0x22: return "loclistx";
	case 0x23: return "rnglistx";
	case 0x24: return "ref_sup8";
	case 0x25: return "strx1";
	case 0x26: return "strx2";
	case 0x27: return "strx3";
	case 0x28: return "strx4";
	case 0x29: return "addrx1";
	case 0x2a: return "addrx2";
	case 0x2b: return "addrx3";
	case 0x2c: return "addrx4";
	default:  return NULL;
	}
}

/* Decode and print the .debug_abbrev compilation-unit abbreviation table
 * (a prefix of it, bounded by dsize).  Each entry: uleb abbreviation
 * code, uleb tag, children byte, then (uleb attr, uleb form)* pairs ended
 * by 0,0. */
static void
dump_debug_abbrev(const unsigned char *data, size_t dsize)
{
	const unsigned char *p = data, *end = data + dsize;
	int shown = 0;

	printf("\nThe .debug_abbrev section:\n");
	while (p < end) {
		uint64_t code = read_uleb(&p, end);
		uint64_t tag, children;
		const char *tag_name;
		uint64_t attr, form;
		int i;

		if (code == 0)
			break;
		if (shown++ > 200)
			break;
		tag = read_uleb(&p, end);
		children = (p < end) ? *p++ : 0;
		tag_name = dwarf_tag_name(tag);
		printf("  %llu DW_TAG_%s%s\n",
		       (unsigned long long)code,
		       tag_name ? tag_name : "<unknown>",
		       children ? "\tDW_CHILDREN_yes" : "\tDW_CHILDREN_no");
		for (i = 0; p < end; ++i) {
			const char *an, *fn;
			attr = read_uleb(&p, end);
			form = read_uleb(&p, end);
			if (attr == 0 && form == 0)
				break;
			if (i > 30)
				break;
			an = dwarf_attr_name(attr);
			fn = dwarf_form_name(form);
			if (form == 0x21) {
				/* DW_FORM_implicit_const: the attribute value is the
				 * SLEB128 that follows here in the abbrev entry. */
				int64_t c = read_sleb(&p, end);
				printf("    DW_AT_%-12s DW_FORM_implicit_const: %lld\n",
				       an ? an : "<unknown>", (long long)c);
			} else {
				printf("    DW_AT_%-12s DW_FORM_%s\n",
				       an ? an : "<unknown>",
				       fn ? fn : "<unknown>");
			}
		}
	}
	printf("\n");
}

/* Decode and print the .debug_info compilation units (DWARF4): walk the
 * DIE tree by looking each DIE's abbreviation code up in the
 * .debug_abbrev table and printing the decoded attribute values for the
 * common DWARF4 forms.  Defined below (after dump_debug). */
static void dump_debug_info(const unsigned char *data, size_t dsize,
                            int indent, const unsigned char *abbrev,
                            size_t abbrev_size);

/* Initial DWARF support: dump every .debug_* section's contents (name,
 * address, raw hex bytes), with structural decoding for .debug_line and
 * .debug_abbrev, and DIE traversal for .debug_info.  ELF32 reuses the same
 * 64-bit parser for the section map. */
static void
dump_debug(const unsigned char *bytes, size_t size,
           const struct mt_elf64_view *view)
{
	struct mt_elf64_section shstrtab, sec;
	enum mt_elf_status st;
	unsigned char *abbrv_data = NULL;
	size_t abbrv_size = 0;
	uint16_t i;

	if (view->section_name_index >= view->section_count)
		return;
	st = mt_elf64_get_section(bytes, size, view,
	                          view->section_name_index, &shstrtab);
	if (st != MT_ELF_OK || shstrtab.type != MT_SHT_STRTAB)
		return;

	/* First pass: locate .debug_abbrev (needed by .debug_info DIE
	 * decoding). */
	for (i = 0; i < view->section_count; ++i) {
		const char *name;

		if (mt_elf64_get_section(bytes, size, view, i, &sec) != MT_ELF_OK)
			continue;
		if (mt_elf64_get_string(bytes, size, &shstrtab, sec.name,
		                        &name) != MT_ELF_OK || !name)
			continue;
		if (strcmp(name, ".debug_abbrev") == 0) {
			abbrv_data = (unsigned char *)(bytes + sec.offset);
			abbrv_size = sec.size;
			break;
		}
	}

	for (i = 0; i < view->section_count; ++i) {
		const char *name;

		if (mt_elf64_get_section(bytes, size, view, i, &sec) != MT_ELF_OK)
			continue;
		if (mt_elf64_get_string(bytes, size, &shstrtab, sec.name,
		                        &name) != MT_ELF_OK || !name)
			continue;
		if (strncmp(name, ".debug_", 7) != 0 &&
		    strncmp(name, ".zdebug_", 8) != 0 &&
		    strcmp(name, ".stab") != 0)
			continue;
		if (strcmp(name, ".debug_line") == 0) {
			dump_debug_line(bytes + sec.offset, sec.size);
			continue;
		}
		if (strcmp(name, ".debug_abbrev") == 0) {
			dump_debug_abbrev(bytes + sec.offset, sec.size);
			continue;
		}
		if (strcmp(name, ".debug_info") == 0) {
			dump_debug_info(bytes + sec.offset, sec.size, 0,
			                abbrv_data, abbrv_size);
			continue;
		}
		dump_hexdump_section(bytes, size, &sec, name,
		                     section_has_relocations(bytes, size, view, i));
	}
}

/* ---- .debug_info DIE decode ------------------------------------------ */

/* A single attribute in one abbreviation. */
struct dw_abbrev_attr {
	uint64_t attr, form;
};

/* A resolved abbreviation declaration: tag, children flag, attribute
 * signature. */
struct dw_abbrev {
	uint64_t tag;
	unsigned char has_children;
	struct dw_abbrev_attr attrs[24];
	unsigned nattrs;
};

/* Parse the .debug_abbrev leaf list into `tab[1..ntab-1]` (indexed by
 * abbreviation code).  Returns the number of entries + 1 (index 0 unused). */
static int
parse_abbrev_table(const unsigned char *abbrev, size_t abbrev_size,
                   struct dw_abbrev *tab, int tabcap)
{
	const unsigned char *p = abbrev, *end = abbrev + abbrev_size;
	uint64_t code, tag, attr, form;
	unsigned char children;
	int n = 0;

	while (p < end) {
		unsigned i;
		code = read_uleb(&p, end);
		if (code == 0)
			break;
		if (code >= (uint64_t)tabcap || n >= tabcap - 1) {
			/* Reject overlarge/garbage codes; keep parsing to the
			 * next entry anyway. */
			if (p < end) {
				tag = read_uleb(&p, end);
				(void)tag;
				if (p < end) children = *p++;
				else children = 0;
				for (i = 0; p < end; ++i) {
					attr = read_uleb(&p, end);
					form = read_uleb(&p, end);
					if (attr == 0 && form == 0)
						break;
					if (form == 0x21)   /* implicit_const:
					                      * SLEB value follows */
						read_sleb(&p, end);
					if (i > 40)
						break;
				}
			}
			continue;
		}
		tag = read_uleb(&p, end);
		children = (p < end) ? *p++ : 0;
		tab[code].tag = tag;
		tab[code].has_children = children;
		tab[code].nattrs = 0;
		for (i = 0; p < end; ++i) {
			attr = read_uleb(&p, end);
			form = read_uleb(&p, end);
			if (attr == 0 && form == 0)
				break;
			if (tab[code].nattrs >= 24 || i > 40)
				break;
			tab[code].attrs[tab[code].nattrs].attr = attr;
			tab[code].attrs[tab[code].nattrs].form = form;
			tab[code].nattrs++;
			/* implicit_const carries its value in the abbrev entry
			 * (as SLEB128); there is nothing to read from .debug_info. */
			if (form == 0x21)
				read_sleb(&p, end);
		}
		if (code > (uint64_t)n)
			n = (int)code;
	}
	return n + 1;
}

/* Skip/decode one attribute value at *pp per `form`, printing a
 * human-readable value into `out` (bounded).  Advances *pp. */
static void
decode_attr_value(const unsigned char **pp, const unsigned char *end,
                  uint64_t form, int address_size, char *out, size_t outsz)
{
	const unsigned char *p = *pp;
	uint64_t u;
	int64_t s;
	size_t blk, i;

	if (!outsz)
		outsz = 1;
	out[0] = '\0';
	if (p >= end) {
		*pp = p;
		snprintf(out, outsz, "<eof>");
		return;
	}
	switch (form) {
	case 0x08: /* string */
	{
		size_t n = 0;
		while (p + n < end && p[n] != 0)
			++n;
		i = n < outsz - 1 ? n : outsz - 1;
		memcpy(out, p, i);
		out[i] = '\0';
		p += n + 1;
		break;
	}
	case 0x01: /* addr */
		u = 0;
		for (i = 0; i < (size_t)address_size && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "0x%llx", (unsigned long long)u);
		break;
	case 0x0f: /* udata */
		u = read_uleb(&p, end);
		snprintf(out, outsz, "%llu", (unsigned long long)u);
		break;
	case 0x0d: /* sdata */
		s = read_sleb(&p, end);
		snprintf(out, outsz, "%lld", (long long)s);
		break;
	case 0x0b: /* data1 */
		u = (p < end) ? *p++ : 0;
		snprintf(out, outsz, "%llu", (unsigned long long)u);
		break;
	case 0x05: /* data2 */
		u = 0;
		for (i = 0; i < 2 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "%llu", (unsigned long long)u);
		break;
	case 0x06: /* data4 */
		u = 0;
		for (i = 0; i < 4 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "%llu", (unsigned long long)u);
		break;
	case 0x07: /* data8 */
		u = 0;
		for (i = 0; i < 8 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "%llu", (unsigned long long)u);
		break;
	case 0x0c: /* flag */
		u = (p < end) ? *p++ : 0;
		snprintf(out, outsz, "%s", u ? "yes" : "no");
		break;
	case 0x19: /* flag_present */
		snprintf(out, outsz, "yes");
		break;
	case 0x13: /* ref4 */
		u = 0;
		for (i = 0; i < 4 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "0x%llx", (unsigned long long)u);
		break;
	case 0x11: /* ref1 */
		u = (p < end) ? *p++ : 0;
		snprintf(out, outsz, "0x%llx", (unsigned long long)u);
		break;
	case 0x12: /* ref2 */
		u = 0;
		for (i = 0; i < 2 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "0x%llx", (unsigned long long)u);
		break;
	case 0x14: /* ref8 */
		u = 0;
		for (i = 0; i < 8 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "0x%llx", (unsigned long long)u);
		break;
	case 0x15: /* ref_udata */
		u = read_uleb(&p, end);
		snprintf(out, outsz, "0x%llx", (unsigned long long)u);
		break;
	case 0x10: /* ref_addr */
		u = 0;
		for (i = 0; i < (size_t)address_size && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "0x%llx", (unsigned long long)u);
		break;
	case 0x0e: /* strp: offset into .debug_str; we print the raw offset */
		u = 0;
		for (i = 0; i < 4 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "(str) 0x%llx", (unsigned long long)u);
		break;
	case 0x0a: /* block1 */
	case 0x09: /* block */
	case 0x18: /* exprloc */
		blk = (form == 0x0a) ? ((p < end) ? *p++ : 0)
		                     : (size_t)read_uleb(&p, end);
		/* Decode a location-expression byte codes as readable DW_OP
		 * names + operands; unknown opcodes fall back to raw hex. */
		{
			const unsigned char *sp = p, *sex_end = p + blk;
			if (sex_end > end)
				sex_end = end;
			out[0] = '\0';
			while (sp < sex_end) {
				unsigned char opc = *sp++;
				const char *oname = NULL;
				size_t used;

				if (opc >= 0x50 && opc <= 0x6f) {
					snprintf(out + strlen(out), outsz - strlen(out),
					         "%s(DW_OP_reg%u)",
					         strlen(out) ? " " : "", opc - 0x50);
					continue;
				}
				switch (opc) {
				case 0x03: oname = "addr"; break;      /* 4/8-byte addr */
				case 0x06: oname = "deref"; break;
				case 0x08: oname = "const1u"; break;   /* 1-byte */
				case 0x09: oname = "const1s"; break;
				case 0x0a: oname = "const2u"; break;   /* 2-byte */
				case 0x0b: oname = "const2s"; break;
				case 0x0c: oname = "const4u"; break;   /* 4-byte */
				case 0x0d: oname = "const4s"; break;
				case 0x0e: oname = "const8u"; break;   /* 8-byte */
				case 0x0f: oname = "const8s"; break;
				case 0x10: oname = "constu"; break;    /* uleb */
				case 0x11: oname = "consts"; break;    /* sleb */
				case 0x12: oname = "dup"; break;
				case 0x13: oname = "drop"; break;
				case 0x14: oname = "over"; break;
				case 0x17: oname = "swap"; break;
				case 0x1c: oname = "plus"; break;
				case 0x1f: oname = "minus"; break;
				case 0x22: oname = "plus_uconst"; break; /* uleb */
				case 0x23: oname = "breg0"; break;       /* sleb */
				case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
				case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
				case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e:
				case 0x3f:
					oname = "lit0"; break;              /* lit0..lit15 */
				case 0x90: oname = "regx"; break;       /* uleb */
				case 0x91: oname = "fbreg"; break;      /* sleb */
				case 0x9d: oname = "bra"; break;        /* 2-byte */
				case 0x9f: oname = "stack_value"; break;
				case 0xa0: oname = "implicit_value"; break; /* 2-byte len */
				default:   oname = NULL; break;
				}
				if (!oname) {
					snprintf(out + strlen(out), outsz - strlen(out),
					         "%s0x%02x", strlen(out) ? " " : "", opc);
					continue;
				}
				if (opc >= 0x30 && opc <= 0x3f)
					snprintf(out + strlen(out), outsz - strlen(out),
					         "%sDW_OP_lit%u", strlen(out) ? " " : "",
					         opc - 0x30);
				else
					snprintf(out + strlen(out), outsz - strlen(out),
					         "%sDW_OP_%s", strlen(out) ? " " : "", oname);
				/* consume operands */
				used = 0;
				switch (opc) {
				case 0x03: used = 8; break;
				case 0x08: case 0x09: used = 1; break;
				case 0x0a: case 0x0b: used = 2; break;
				case 0x0c: case 0x0d: case 0x9d: used = 4; break;
				case 0x0e: case 0x0f: used = 8; break;
				default:
					if (opc == 0x10 || opc == 0x90 || opc == 0x22) {
						/* uleb operand */
						const unsigned char *s2 = sp;
						read_uleb(&s2, sex_end);
						used = (size_t)(s2 - sp);
					} else if (opc == 0x11 || opc == 0x91 ||
					           (opc >= 0x23 && opc <= 0x2e)) {
						/* sleb operand */
						const unsigned char *s2 = sp;
						read_sleb(&s2, sex_end);
						used = (size_t)(s2 - sp);
					} else if (opc == 0xa0) {
						used = 0;
					}
					break;
				}
				while (used > 0 && sp < sex_end) {
					snprintf(out + strlen(out), outsz - strlen(out),
					         "%s%02x", strlen(out) ? " " : "", *sp++);
					--used;
				}
			}
			p = sex_end;
		}
		break;
	case 0x17: /* sec_offset */
		u = 0;
		for (i = 0; i < 4 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "0x%llx", (unsigned long long)u);
		break;
	case 0x16: /* indirect */
		snprintf(out, outsz, "<indirect>");
		break;
	case 0x1f: /* line_strp: 4-byte offset into .debug_line_str */
		u = 0;
		for (i = 0; i < 4 && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "(line_str) 0x%llx", (unsigned long long)u);
		break;
	case 0x1a: /* strx: uleb index into .debug_str_offsets */
		u = read_uleb(&p, end);
		snprintf(out, outsz, "(strx) %llu", (unsigned long long)u);
		break;
	case 0x1b: /* addrx: uleb index into .debug_addr */
		u = read_uleb(&p, end);
		snprintf(out, outsz, "(addrx) 0x%llx", (unsigned long long)u);
		break;
	case 0x21: /* implicit_const: value lives in the abbrev entry, not here */
		snprintf(out, outsz, "<implicit>");
		break;
	case 0x25: case 0x26: case 0x27: case 0x28: { /* strx1..strx4 */
		unsigned nb = 1u + (unsigned)(form - 0x25);
		u = 0;
		for (i = 0; i < nb && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "(strx) %llu", (unsigned long long)u);
		break;
	}
	case 0x29: case 0x2a: case 0x2b: case 0x2c: { /* addrx1..addrx4 */
		unsigned nb = 1u + (unsigned)(form - 0x29);
		u = 0;
		for (i = 0; i < nb && p < end; ++i)
			u |= (uint64_t)*p++ << (8 * i);
		snprintf(out, outsz, "(addrx) 0x%llx", (unsigned long long)u);
		break;
	}
	default:
		snprintf(out, outsz, "<form 0x%llx>", (unsigned long long)form);
		break;
	}
	*pp = p;
}

/* Recursively walk DWARF4 DIEs and print the decoded tree. */
static void
dump_dies(const unsigned char **pp, const unsigned char *end,
          struct dw_abbrev *tab, int indent, int address_size)
{
	while (*pp < end) {
		uint64_t code = read_uleb(pp, end);
		struct dw_abbrev *a;
		const char *tn;
		unsigned ai;

		if (code == 0)     /* null DIE terminates a sibling list */
			return;
		if (code >= 256 || tab[(size_t)code].tag == 0)
			break;   /* unknown abbrev: stop this CU */
		a = &tab[(size_t)code];
		tn = dwarf_tag_name(a->tag);
		printf("%*s<%d> DW_TAG_%s\n", indent * 4, "", indent, tn ? tn : "<unknown>");
		for (ai = 0; ai < a->nattrs; ++ai) {
			char val[160];
			const char *an;
			decode_attr_value(pp, end, a->attrs[ai].form, address_size,
			                  val, sizeof val);
			an = dwarf_attr_name(a->attrs[ai].attr);
			printf("%*s  DW_AT_%-12s: %s\n", indent * 4, "",
			       an ? an : "<unknown>", val);
		}
		if (a->has_children)
			dump_dies(pp, end, tab, indent + 1, address_size);
	}
}

/* Parse a .debug_info CU header then walk its DIE tree.  abbrev points to
 * the .debug_abbrev section; size is the .debug_info section size. */
static void
dump_debug_info(const unsigned char *data, size_t dsize,
                int indent, const unsigned char *abbrev, size_t abbrev_size)
{
	struct dw_abbrev tab[256] = { { 0 } };
	const unsigned char *p = data, *end = data + dsize;
	uint64_t unit_length;
	uint16_t version;
	uint64_t abbrev_offset;
	uint8_t address_size;
	int ntab;
	int cu = 0;

	if (!abbrev || abbrev_size == 0)
		return;
	ntab = parse_abbrev_table(abbrev, abbrev_size, tab, 256);
	(void)ntab;

	printf("\nThe .debug_info section:\n");
	while (p + 11 <= end) {
		uint64_t cu_end;
		size_t cu_off = (size_t)(p - data);

		unit_length = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
		              (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
		p += 4;
		if (unit_length >= 0xfffffff0u || p + unit_length > end)
			break;
		version = (uint16_t)p[0] | (uint16_t)p[1] << 8;
		p += 2;
		if (version < 2 || version > 5)
			break;
		if (version >= 5) {
			/* DWARF5: version, unit_type(1), address_size(1),
			 * abbrev_offset(4). */
			p++;                     /* unit_type */
			address_size = *p++;
			abbrev_offset = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
			                (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
			p += 4;
		} else {
			/* DWARF v2-v4: version, abbrev_offset(4), address_size(1). */
			abbrev_offset = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
			                (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
			p += 4;
			address_size = *p++;
		}
		cu_end = (uint64_t)(size_t)(p - data) + unit_length;
		if (cu_end > dsize)
			cu_end = dsize;
		printf("  Compilation Unit @ offset 0x%llx:\n",
		       (unsigned long long)cu_off);
		printf("   Version: %u  Abbrev Offset: %llu  Pointer Size: %u\n",
		       version, (unsigned long long)abbrev_offset, address_size);
		dump_dies(&p, data + cu_end, tab, indent + 1, address_size);
		++cu;
		p = data + cu_end;
	}
	printf("\n");
}

/* ---- 单文件处理 ---- */

static int
process_file(const char *path, struct options *opts)
{
	unsigned char *bytes;
	size_t size;
	struct mt_elf64_view view;
	enum mt_elf_status st;
	int is_elf32;
	int unsupported = 0;
	int i;

	if (load_file(path, &bytes, &size) != 0)
		return 1;

	st = mt_elf64_parse(bytes, size, &view);
	if (st != MT_ELF_OK) {
		fprintf(stderr, "readelf: %s: %s\n", path,
		        mt_elf_status_string(st));
		free(bytes);
		return 1;
	}

	/* ELF32 is only partially supported: -h/-S/-s/-x decode ELF32 via the
	 * libelf is_elf32 paths, but the program-header, relocation and
	 * dynamic readers are ELF64-only.  Refuse those explicitly instead of
	 * printing spurious layout errors or garbage. */
	is_elf32 = (bytes[4] == MT_ELFCLASS32);
	if (is_elf32 && (opts->dump_programs || opts->dump_relocs ||
	    opts->dump_dynamic)) {
		fprintf(stderr,
		        "readelf: %s: ELF32 not supported for -l/-r/-d\n", path);
		unsupported = 1;
	}

	if (opts->dump_header)
		dump_ehdr(bytes, size, &view);
	if (!unsupported) {
		if (opts->dump_programs)
			dump_phdrs(bytes, size, &view);
		if (opts->dump_relocs)
			dump_relocs(bytes, size, &view);
		if (opts->dump_dynamic)
			dump_dynamic(bytes, size, &view);
	}
	if (opts->dump_sections)
		dump_shdrs(bytes, size, &view);
	if (opts->dump_symbols)
		dump_symbols(bytes, size, &view);
	for (i = 0; i < opts->hex_count; ++i)
		dump_hexdump(bytes, size, &view, opts->hex_secs[i]);
	if (opts->dump_debug)
		dump_debug(bytes, size, &view);

	free(bytes);
	return unsupported ? 1 : 0;
}

/* ---- 选项解析 ---- */

/* 解析 -a 等价组合。 */
static void
enable_all(struct options *opts)
{
	opts->dump_header = 1;
	opts->dump_programs = 1;
	opts->dump_sections = 1;
	opts->dump_symbols = 1;
	opts->dump_relocs = 1;
	opts->dump_dynamic = 1;
}

/* 处理单个短选项串（如 -hls 中的每个字符）。返回 0 成功，-1 失败。 */
static int
parse_short_cluster(const char *arg, struct options *opts)
{
	const char *p;
	for (p = arg; *p; ++p) {
		switch (*p) {
		case 'a': enable_all(opts); break;
		case 'h': opts->dump_header = 1; break;
		case 'l': opts->dump_programs = 1; break;
		case 'S': opts->dump_sections = 1; break;
		case 's': opts->dump_symbols = 1; break;
		case 'r': opts->dump_relocs = 1; break;
		case 'd': opts->dump_dynamic = 1; break;
		case 'w': opts->dump_debug = 1; break;
		case 'W': opts->wide = 1; break;
		case 'H': usage(stdout); return 1;
		case 'V':
			printf("meuos-toolchain readelf %s (x86_64 bootstrap)\n",
			       MT_READELF_VERSION);
			return 1;
		default:
			fprintf(stderr, "readelf: unknown option '-%c'\n", *p);
			return -1;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct options opts;
	int i;
	int first_file = -1;
	int status = 0;

	memset(&opts, 0, sizeof(opts));

	for (i = 1; i < argc; ++i) {
		const char *arg = argv[i];

		if (strcmp(arg, "--help") == 0 || strcmp(arg, "-H") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0) {
			printf("meuos-toolchain readelf %s (x86_64 bootstrap)\n",
			       MT_READELF_VERSION);
			return 0;
		}
		if (strcmp(arg, "--all") == 0 || strcmp(arg, "-a") == 0) {
			enable_all(&opts);
			continue;
		}
		if (strcmp(arg, "--file-header") == 0) {
			opts.dump_header = 1;
			continue;
		}
		if (strcmp(arg, "--program-headers") == 0) {
			opts.dump_programs = 1;
			continue;
		}
		if (strcmp(arg, "--section-headers") == 0) {
			opts.dump_sections = 1;
			continue;
		}
		if (strcmp(arg, "--symbols") == 0 || strcmp(arg, "--syms") == 0) {
			opts.dump_symbols = 1;
			continue;
		}
		if (strcmp(arg, "--relocs") == 0) {
			opts.dump_relocs = 1;
			continue;
		}
		if (strcmp(arg, "--dynamic") == 0) {
			opts.dump_dynamic = 1;
			continue;
		}
		if (strcmp(arg, "--debug-dump") == 0 ||
		    strcmp(arg, "--debug-dump=rawline") == 0) {
			opts.dump_debug = 1;
			continue;
		}
		if (strcmp(arg, "--wide") == 0 || strcmp(arg, "-W") == 0) {
			opts.wide = 1;
			continue;
		}
		if (strcmp(arg, "-x") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "readelf: -x requires an argument\n");
				return 2;
			}
			if (opts.hex_count >= MT_READELF_MAX_HEX) {
				fprintf(stderr, "readelf: too many -x options\n");
				return 2;
			}
			opts.hex_secs[opts.hex_count++] = argv[i];
			continue;
		}
		if (strncmp(arg, "-x", 2) == 0 && arg[2] != '\0') {
			if (opts.hex_count >= MT_READELF_MAX_HEX) {
				fprintf(stderr, "readelf: too many -x options\n");
				return 2;
			}
			opts.hex_secs[opts.hex_count++] = arg + 2;
			continue;
		}
		if (strncmp(arg, "--hex-dump=", 11) == 0) {
			if (opts.hex_count >= MT_READELF_MAX_HEX) {
				fprintf(stderr, "readelf: too many -x options\n");
				return 2;
			}
			opts.hex_secs[opts.hex_count++] = arg + 11;
			continue;
		}
		if (arg[0] == '-' && arg[1] != '\0' && arg[1] != '-') {
			/* 短选项簇：-hls 等。-x 已在上面单独处理。 */
			int rc = parse_short_cluster(arg + 1, &opts);
			if (rc == 1)
				return 0;
			if (rc < 0)
				return 2;
			continue;
		}
		if (arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
			fprintf(stderr, "readelf: unknown option '%s'\n", arg);
			return 2;
		}
		/* 非选项参数：文件名。 */
		if (first_file < 0)
			first_file = i;
	}

	if (first_file < 0) {
		usage(stderr);
		return 2;
	}

	/* 无任何 dump 选项时默认不输出（GNU 行为是报错，这里保持安静）。 */
	for (i = first_file; i < argc; ++i) {
		if (argv[i][0] == '-' && argv[i][1] != '\0')
			continue;
		if (process_file(argv[i], &opts) != 0)
			status = 1;
		/* 多文件之间空行分隔。 */
		if (i < argc - 1)
			printf("\n");
	}

	return status;
}
