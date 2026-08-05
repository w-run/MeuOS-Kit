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

/* Initial DWARF support: dump every .debug_* section's contents (name,
 * address, raw hex bytes).  This is deliberately a raw dump rather than a
 * DIE/line-program decode — it gives a faithful view of the debug data an
 * assembler/compiler emitted, and a stable base to add structural decoding
 * on top.  ELF32 reuses the same 64-bit parser for the section map. */
static void
dump_debug(const unsigned char *bytes, size_t size,
           const struct mt_elf64_view *view)
{
	struct mt_elf64_section shstrtab, sec;
	enum mt_elf_status st;
	uint16_t i;

	if (view->section_name_index >= view->section_count)
		return;
	st = mt_elf64_get_section(bytes, size, view,
	                          view->section_name_index, &shstrtab);
	if (st != MT_ELF_OK || shstrtab.type != MT_SHT_STRTAB)
		return;

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
		dump_hexdump_section(bytes, size, &sec, name,
		                     section_has_relocations(bytes, size, view, i));
	}
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
