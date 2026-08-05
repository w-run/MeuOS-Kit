/* as_parse.c - mt/as directive and operand parsing.
 *
 * Parses assembler directives (parse_directive), data emit lists
 * (parse_data_list / parse_data_value / parse_string_bytes), and symbol
 * lists.  Extracted from assemble.c (per-domain module split).
 */
#include "mt/as.h"
#include "mt/as_int.h"
#include "mt/elf.h"
#include "mt/elf32.h"
#include "mt/target.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int
parse_string_bytes(struct as_file *as, struct as_section *section,
                   const char *text, int terminate)
{
	const char *p = text;
	unsigned char byte;
	unsigned value;
	int digits;

	if (*p++ != '"')
		return as_error(as, "string directive requires a quoted string");
	while (*p && *p != '"') {
		if (*p != '\\') {
			byte = (unsigned char)*p++;
		} else {
			++p;
			if (!*p)
				return as_error(as, "unterminated string escape");
			switch (*p++) {
			case 'a': byte = '\a'; break;
			case 'b': byte = '\b'; break;
			case 'f': byte = '\f'; break;
			case 'n': byte = '\n'; break;
			case 'r': byte = '\r'; break;
			case 't': byte = '\t'; break;
			case 'v': byte = '\v'; break;
			case '\\': byte = '\\'; break;
			case '"': byte = '"'; break;
			/* \0 handled by default: octal parser (up to 3 digits) */
			case 'x':
				value = 0;
				digits = 0;
				while (isxdigit((unsigned char)*p) && digits < 2) {
					value <<= 4;
					if (*p >= '0' && *p <= '9') value += (unsigned)(*p - '0');
					else if (*p >= 'a' && *p <= 'f') value += (unsigned)(*p - 'a' + 10);
					else value += (unsigned)(*p - 'A' + 10);
					++p;
					++digits;
				}
				if (digits == 0)
					return as_error(as, "\\x escape requires hexadecimal digits");
				byte = (unsigned char)value;
				break;
			default:
				if (p[-1] >= '0' && p[-1] <= '7') {
					value = (unsigned)(p[-1] - '0');
					digits = 1;
					while (*p >= '0' && *p <= '7' && digits < 3) {
						value = (value << 3) + (unsigned)(*p++ - '0');
						++digits;
					}
					byte = (unsigned char)value;
				} else
					byte = (unsigned char)p[-1];
				break;
			}
		}
		if (as_append_bytes(as, section, &byte, 1) != 0)
			return -1;
	}
	if (*p != '"' || *trim((char *)(p + 1)) != '\0')
		return as_error(as, "malformed string directive");
	if (terminate) {
		byte = 0;
		if (as_append_bytes(as, section, &byte, 1) != 0)
			return -1;
	}
	return 0;
}

static char *
next_csv(char **cursor)
{
	char *start = trim(*cursor);
	char *p = start;
	int quote = 0;
	while (*p) {
		if (*p == '"')
			quote = !quote;
		else if (*p == ',' && !quote) {
			*p++ = '\0';
			*cursor = p;
			return trim(start);
		}
		++p;
	}
	*cursor = p;
	return trim(start);
}

/* Absolute (non-PC-relative) data relocation type for a symbol reference
 * of the given byte width, for the active target.  x86_64 uses the
 * generic MT_R_X86_64_* numbers; every other target uses its own ELF
 * relocation numbering, so the data directive cannot hardcode x86_64. */
static unsigned
data_reloc_type(const struct mt_target *target, unsigned width);

static int
parse_data_value(struct as_file *as, struct as_section *section,
                 char *text, unsigned width)
{
	char *symbol;
	char modifier[16];
	int64_t addend;
	int is_number;
	size_t offset = section->size;
	uint64_t value;

	if (parse_reference(trim(text), &symbol, modifier, sizeof(modifier),
	                    &addend, &is_number) != 0)
		return as_error(as, "invalid data expression: %s", text);
	if (modifier[0] != '\0') {
		free(symbol);
		return as_error(as, "unsupported data relocation modifier: %s", modifier);
	}
	if (is_number) {
		value = (uint64_t)addend;
		free(symbol);
		return as_emit_le(as, section, value, width);
	}
	{
		struct as_symbol *referenced = get_symbol(as, symbol);
		if (!referenced) {
			free(symbol);
			return -1;
		}
		if (!referenced->defined && symbol[0] != '.')
			referenced->bind = MT_STB_GLOBAL;
	}
	if (as_emit_le(as, section, 0, width) != 0 ||
	    as_add_fixup(as, section, offset, width,
	               data_reloc_type(as->target, width),
	               addend, symbol) != 0) {
		free(symbol);
		return -1;
	}
	free(symbol);
	return 0;
}

/* Absolute (non-PC-relative) data relocation type for a symbol reference
 * of the given byte width, for the active target.  x86_64 uses the
 * generic MT_R_X86_64_* numbers; every other target uses its own ELF
 * relocation numbering, so the data directive cannot hardcode x86_64. */
static unsigned
data_reloc_type(const struct mt_target *target, unsigned width)
{
	if (target && target->emachine == MT_EM_ARM) {
		/* ELF32 ARM: .byte/.short/.long/.quad → ABS8/ABS16/ABS32.
		 * .quad stores a zero-extended 32-bit address (low word). */
		switch (width) {
		case 1: return 8;  /* R_ARM_ABS8 */
		case 2: return 5;  /* R_ARM_ABS16 */
		case 4: return 2;  /* R_ARM_ABS32 */
		case 8: return 2;  /* R_ARM_ABS32 (low word; high word is 0) */
		}
		return 2;
	}
	if (target && target->emachine == MT_EM_AARCH64) {
		/* R_AARCH64_ABS64/ABS32 */
		return width == 8 ? 257 : 258;
	}
	if (target && target->emachine == MT_EM_RISCV) {
		return width == 8 ? 2 /* R_RISCV_64 */ : 1 /* R_RISCV_32 */;
	}
	if (target && target->emachine == MT_EM_LOONGARCH) {
		return width == 8 ? 2 /* R_LARCH_64 */ : 1 /* R_LARCH_32 */;
	}
	if (target && target->emachine == MT_EM_386) {
		switch (width) {
		case 1: return 22; /* R_386_8 */
		case 2: return 20; /* R_386_16 */
		default: return 1; /* R_386_32 */
		}
	}
	/* x86_64 (or unknown target): keep the historical numbers. */
	return width == 8 ? MT_R_X86_64_64 : MT_R_X86_64_32;
}

static int
parse_data_list(struct as_file *as, char *text, unsigned width)
{
	char *cursor = text;
	char *item;
	for (;;) {
		item = next_csv(&cursor);
		if (!*item)
			return as_error(as, "empty data expression");
		if (parse_data_value(as, &as->sections[as->current], item, width) != 0)
			return -1;
		if (!*cursor)
			break;
	}
	return 0;
}

static int
parse_symbol_list(struct as_file *as, char *text, unsigned bind)
{
	char *cursor = text;
	char *name;
	struct as_symbol *symbol;
	while (*(name = next_csv(&cursor))) {
		name = trim(name);
		if (!*name)
			return as_error(as, "empty symbol name");
		symbol = get_symbol(as, name);
		if (!symbol)
			return -1;
		symbol->bind = bind;
		if (bind == MT_STB_GLOBAL || bind == MT_STB_WEAK)
			symbol->other = 0;
		if (!*cursor)
			break;
	}
	return 0;
}

int
parse_directive(struct as_file *as, char *directive, char *rest)
{
	int section;
	int64_t value;
	int64_t count;
	int64_t element_size;
	int64_t fill_value;
	int64_t common_size;
	char *name;
	char *cursor;
	char *item;
	struct as_symbol *symbol;
	uint64_t align;

	if (strcmp(directive, ".text") == 0 || strcmp(directive, ".data") == 0 ||
	    strcmp(directive, ".bss") == 0 || strcmp(directive, ".rodata") == 0 ||
	    strcmp(directive, ".tdata") == 0 || strcmp(directive, ".tbss") == 0) {
		section = get_section(as, directive);
		if (section < 0)
			return -1;
		as->current = section;
		return 0;
	}
	if (strcmp(directive, ".pushsection") == 0) {
		char section_name[128];
		size_t length = 0;
		while (rest[length] && rest[length] != ',' &&
		       !isspace((unsigned char)rest[length]))
			++length;
		if (length == 0 || length >= sizeof(section_name))
			return as_error(as, "invalid section name");
		if (as->section_stack_depth >= 16)
			return as_error(as, "section stack overflow");
		as->section_stack[as->section_stack_depth++] = as->current;
		memcpy(section_name, rest, length);
		section_name[length] = '\0';
		section = get_section(as, section_name);
		if (section < 0)
			return -1;
		as->current = section;
		return 0;
	}
	if (strcmp(directive, ".popsection") == 0) {
		if (as->section_stack_depth <= 0)
			return as_error(as, "section stack underflow");
		as->current = as->section_stack[--as->section_stack_depth];
		return 0;
	}
	if (strcmp(directive, ".section") == 0) {
		char section_name[128];
		size_t length = 0;
		while (rest[length] && rest[length] != ',' &&
		       !isspace((unsigned char)rest[length]))
			++length;
		if (length == 0 || length >= sizeof(section_name))
			return as_error(as, "invalid section name");
		memcpy(section_name, rest, length);
		section_name[length] = '\0';
		section = get_section(as, section_name);
		if (section < 0)
			return -1;
		as->current = section;
		return 0;
	}
	if (strcmp(directive, ".globl") == 0 ||
	    strcmp(directive, ".global") == 0)
		return parse_symbol_list(as, rest, MT_STB_GLOBAL);
	if (strcmp(directive, ".weak") == 0)
		return parse_symbol_list(as, rest, MT_STB_WEAK);
	if (strcmp(directive, ".hidden") == 0) {
		cursor = rest;
		while (*(name = next_csv(&cursor))) {
			symbol = get_symbol(as, name);
			if (!symbol)
				return -1;
			symbol->other = MT_STV_HIDDEN;
			if (!*cursor)
				break;
		}
		return 0;
	}
	if (strcmp(directive, ".type") == 0) {
		name = next_csv(&rest);
		item = trim(rest);
		symbol = get_symbol(as, name);
		if (!symbol)
			return -1;
		if (strstr(item, "@function") || strstr(item, "%function"))
			symbol->type = MT_STT_FUNC;
		else if (strstr(item, "@object") || strstr(item, "%object"))
			symbol->type = MT_STT_OBJECT;
		return 0;
	}
	if (strcmp(directive, ".size") == 0) {
		name = next_csv(&rest);
		symbol = get_symbol(as, name);
		if (!symbol)
			return -1;
		if (parse_integer(trim(rest), &value) == 0)
			symbol->size = (uint64_t)value;
		return 0;
	}
	if (strcmp(directive, ".p2align") == 0) {
		if (parse_integer(rest, &value) != 0 || value < 0 || value > 30)
			return as_error(as, "invalid .p2align value");
		return align_section(as, &as->sections[as->current], 1ULL << value);
	}
	if (strcmp(directive, ".balign") == 0 || strcmp(directive, ".align") == 0) {
		if (parse_integer(rest, &value) != 0 || value <= 0)
			return as_error(as, "invalid alignment");
		return align_section(as, &as->sections[as->current], (uint64_t)value);
	}
	if (strcmp(directive, ".byte") == 0)
		return parse_data_list(as, rest, 1);
	if (strcmp(directive, ".short") == 0 || strcmp(directive, ".word") == 0)
		return parse_data_list(as, rest, 2);
	if (strcmp(directive, ".int") == 0 || strcmp(directive, ".long") == 0)
		return parse_data_list(as, rest, 4);
	if (strcmp(directive, ".quad") == 0)
		return parse_data_list(as, rest, 8);
	if (strcmp(directive, ".ascii") == 0 ||
	    strcmp(directive, ".asciz") == 0 ||
	    strcmp(directive, ".string") == 0)
		return parse_string_bytes(as, &as->sections[as->current], rest,
		                          strcmp(directive, ".ascii") != 0);
	if (strcmp(directive, ".zero") == 0 || strcmp(directive, ".space") == 0 ||
	    strcmp(directive, ".skip") == 0) {
		if (parse_integer(rest, &value) != 0 || value < 0 ||
		    (uint64_t)value > SIZE_MAX)
			return as_error(as, "invalid zero-fill size");
		return append_zeroes(as, &as->sections[as->current], (size_t)value);
	}
	if (strcmp(directive, ".fill") == 0) {
		cursor = rest;
		item = next_csv(&cursor);
		if (parse_integer(item, &count) != 0 || count < 0)
			return as_error(as, "invalid .fill repeat count");
		item = next_csv(&cursor);
		if (parse_integer(item, &element_size) != 0 || element_size != 1)
			return as_error(as, "only .fill element size 1 is supported");
		item = next_csv(&cursor);
		if (parse_integer(item, &fill_value) != 0 || (uint64_t)count > (uint64_t)SIZE_MAX)
			return as_error(as, "invalid .fill value");
		while (count-- > 0)
			if (as_emit_u8(as, &as->sections[as->current], (unsigned)fill_value) != 0)
				return -1;
		return 0;
	}
	if (strcmp(directive, ".comm") == 0) {
		cursor = rest;
		name = next_csv(&cursor);
		item = next_csv(&cursor);
		if (parse_integer(item, &common_size) != 0 || common_size < 0)
			return as_error(as, "invalid .comm size");
		item = next_csv(&cursor);
		align = 1;
		if (*item && (parse_integer(item, &value) != 0 || value <= 0))
			return as_error(as, "invalid .comm alignment");
		if (*item)
			align = (uint64_t)value;
		symbol = get_symbol(as, name);
		if (!symbol)
			return -1;
		symbol->defined = 1;
		symbol->section = -2;
		symbol->size = (uint64_t)common_size;
		symbol->value = align;
		symbol->bind = MT_STB_GLOBAL;
		return 0;
	}
	/* DWARF .file directive (.file uses space-delimited args) */
	if (strcmp(directive, ".file") == 0) {
		int index = 1;
		char *tok, *rest2 = rest;
		/* Extract first token (either a number or filename) */
		while (*rest2 && isspace((unsigned char)*rest2)) rest2++;
		tok = rest2;
		while (*rest2 && !isspace((unsigned char)*rest2)) rest2++;
		if (*rest2) *rest2++ = '\0';
		{
			int64_t num;
			if (parse_integer(tok, &num) == 0 && num >= 0) {
				index = (int)num;
				/* Next token is the filename */
				while (*rest2 && isspace((unsigned char)*rest2)) rest2++;
				tok = rest2;
				while (*rest2 && !isspace((unsigned char)*rest2)) rest2++;
				if (*rest2) *rest2++ = '\0';
			}
		}
		if (!*tok)
			return as_error(as, "missing filename in .file");
		{
			char *fn = trim(tok);
			size_t flen = strlen(fn);
			if (flen >= 2 && fn[0] == '"' && fn[flen - 1] == '"') {
				fn[flen - 1] = '\0';
				fn++;
			}
			if (!*fn)
				return as_error(as, "empty filename in .file");
			if (as->dwarf_file_count == as->dwarf_file_capacity) {
				size_t cap = as->dwarf_file_capacity ? as->dwarf_file_capacity * 2 : 4;
				struct as_dwarf_file *nf = (struct as_dwarf_file *)mt_realloc(
					as->dwarf_files, cap * sizeof(*nf));
				if (!nf) return as_error(as, "out of memory");
				as->dwarf_files = nf;
				as->dwarf_file_capacity = cap;
			}
			as->dwarf_files[as->dwarf_file_count].index = index;
			as->dwarf_files[as->dwarf_file_count].name = mt_strdup(fn);
			if (!as->dwarf_files[as->dwarf_file_count].name)
				return as_error(as, "out of memory");
			as->dwarf_file_count++;
		}
		return 0;
	}
	/* DWARF .loc directive */
	if (strcmp(directive, ".loc") == 0) {
		int64_t file_num, line_num, col_num = 0;
		/* .loc uses space-delimited args: .loc F L [C] */
		cursor = rest;
		{ char *p = rest; while (*p && !isspace((unsigned char)*p)) p++; if (*p) *p++ = '\0'; while (*p && isspace((unsigned char)*p)) p++; item = p; }
		if (parse_integer(rest, &file_num) != 0 || file_num < 0)
			return as_error(as, "invalid .loc file number");
		if (!*item) return as_error(as, "missing .loc line number");
		rest = item;
		{ char *p = rest; while (*p && !isspace((unsigned char)*p)) p++; if (*p) *p++ = '\0'; while (*p && isspace((unsigned char)*p)) p++; item = p; }
		if (parse_integer(rest, &line_num) != 0 || line_num < 0)
			return as_error(as, "invalid .loc line number");
		if (*item)
			parse_integer(item, &col_num);
		if (as->dwarf_loc_count == as->dwarf_loc_capacity) {
			size_t cap = as->dwarf_loc_capacity ? as->dwarf_loc_capacity * 2 : 32;
			struct as_dwarf_loc *nl = (struct as_dwarf_loc *)mt_realloc(
				as->dwarf_locs, cap * sizeof(*nl));
			if (!nl) return as_error(as, "out of memory");
			as->dwarf_locs = nl;
			as->dwarf_loc_capacity = cap;
		}
		as->dwarf_locs[as->dwarf_loc_count].section = as->current;
		as->dwarf_locs[as->dwarf_loc_count].offset = as->sections[as->current].size;
		as->dwarf_locs[as->dwarf_loc_count].file = (int)file_num;
		as->dwarf_locs[as->dwarf_loc_count].line = (unsigned)line_num;
		as->dwarf_locs[as->dwarf_loc_count].column = (unsigned)col_num;
		as->dwarf_loc_count++;
		return 0;
	}
	/* DWARF CFI directives */
	if (strcmp(directive, ".cfi_startproc") == 0) {
		if (as->cfi_active)
			return as_error(as, "nested .cfi_startproc");
		as->cfi_active = 1;
		as->cfi_prog_size = 0;
		as->cfi_func_start = as->sections[as->current].size;
		return 0;
	}
	if (strcmp(directive, ".cfi_endproc") == 0) {
		if (!as->cfi_active)
			return as_error(as, ".cfi_endproc without .cfi_startproc");
		as->cfi_active = 0;
		if (as->cfi_fde_count == as->cfi_fde_capacity) {
			size_t cap = as->cfi_fde_capacity ? as->cfi_fde_capacity * 2 : 4;
			uint64_t *no = (uint64_t *)mt_realloc(as->cfi_func_offsets, cap * sizeof(*no));
			uint64_t *ne = (uint64_t *)mt_realloc(as->cfi_func_end, cap * sizeof(*ne));
			char **nl = (char **)mt_realloc(as->cfi_func_labels, cap * sizeof(*nl));
			unsigned char **np = (unsigned char **)mt_realloc(as->cfi_fde_progs, cap * sizeof(*np));
			size_t *ns = (size_t *)mt_realloc(as->cfi_fde_sizes, cap * sizeof(*ns));
			if (!no || !ne || !nl || !np || !ns) {
				free(no); free(ne); free(nl); free(np); free(ns);
				return as_error(as, "out of memory");
			}
			as->cfi_func_offsets = no;
			as->cfi_func_end = ne;
			as->cfi_func_labels = nl;
			as->cfi_fde_progs = np;
			as->cfi_fde_sizes = ns;
			as->cfi_fde_capacity = cap;
		}
		as->cfi_func_offsets[as->cfi_fde_count] = as->cfi_func_start;
		as->cfi_func_end[as->cfi_fde_count] = as->sections[as->current].size;
		{
			size_t si;
			const char *fn = NULL;
			for (si = 0; si < as->symbol_count; ++si) {
				if (as->symbols[si].defined &&
				    as->symbols[si].section == as->current &&
				    as->symbols[si].value == as->cfi_func_start) {
					fn = as->symbols[si].name;
					break;
				}
			}
			as->cfi_func_labels[as->cfi_fde_count] = fn ? mt_strdup(fn) : mt_strdup("");
			if (!as->cfi_func_labels[as->cfi_fde_count])
				return as_error(as, "out of memory");
		}
		as->cfi_fde_progs[as->cfi_fde_count] = (unsigned char *)mt_malloc(as->cfi_prog_size ? as->cfi_prog_size : 1);
		if (!as->cfi_fde_progs[as->cfi_fde_count])
			return as_error(as, "out of memory");
		if (as->cfi_prog_size > 0)
			memcpy(as->cfi_fde_progs[as->cfi_fde_count], as->cfi_prog, as->cfi_prog_size);
		as->cfi_fde_sizes[as->cfi_fde_count] = as->cfi_prog_size;
		as->cfi_fde_count++;
		as->cfi_prog_size = 0;
		return 0;
	}
	if (strncmp(directive, ".cfi_", 5) == 0) {
		int64_t reg, off, r1, r2;
		/* Local CFI byte append helper */
#define CFI_B(b) do { \
	if (as->cfi_prog_size == as->cfi_prog_capacity) { \
		size_t cap2 = as->cfi_prog_capacity ? as->cfi_prog_capacity * 2 : 64; \
		unsigned char *np = (unsigned char *)mt_realloc(as->cfi_prog, cap2); \
		if (!np) return as_error(as, "out of memory"); \
		as->cfi_prog = np; \
		as->cfi_prog_capacity = cap2; \
	} \
	as->cfi_prog[as->cfi_prog_size++] = (unsigned char)(b); \
} while(0)
#define CFI_ULEB(v) do { \
	uint64_t __v = (uint64_t)(v); \
	do { \
		unsigned char __b = __v & 0x7f; \
		__v >>= 7; \
		if (__v) __b |= 0x80; \
		CFI_B(__b); \
	} while (__v); \
} while(0)
		if (!as->cfi_active)
			return as_error(as, "%s outside .cfi_startproc/.cfi_endproc", directive);
		cursor = rest;
		if (strcmp(directive, ".cfi_def_cfa") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi reg");
			item = next_csv(&cursor);
			if (parse_integer(item, &off) != 0) return as_error(as, "bad cfi off");
			CFI_B(0x0c); CFI_ULEB(reg); CFI_ULEB(off);
		} else if (strcmp(directive, ".cfi_offset") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi reg");
			item = next_csv(&cursor);
			if (parse_integer(item, &off) != 0) return as_error(as, "bad cfi off");
			CFI_B(0x80 | (unsigned)(reg & 0x3f));
			if (reg >= 64) CFI_ULEB(reg);
			CFI_ULEB((uint64_t)(off < 0 ? 0 : off));
		} else if (strcmp(directive, ".cfi_def_cfa_register") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi reg");
			CFI_B(0x07); CFI_ULEB(reg);
		} else if (strcmp(directive, ".cfi_def_cfa_offset") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &off) != 0) return as_error(as, "bad cfi off");
			CFI_B(0x0e); CFI_ULEB(off);
		} else if (strcmp(directive, ".cfi_register") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &r1) != 0) return as_error(as, "bad cfi reg1");
			item = next_csv(&cursor);
			if (parse_integer(item, &r2) != 0) return as_error(as, "bad cfi reg2");
			CFI_B(0x08); CFI_ULEB(r1); CFI_ULEB(r2);
		} else if (strcmp(directive, ".cfi_same_value") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi reg");
			CFI_B(0x09); CFI_ULEB(reg);
		} else if (strcmp(directive, ".cfi_remember_state") == 0) {
			CFI_B(0x0a);
		} else if (strcmp(directive, ".cfi_restore_state") == 0) {
			CFI_B(0x0b);
		} else if (strcmp(directive, ".cfi_rel_offset") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi reg");
			item = next_csv(&cursor);
			if (parse_integer(item, &off) != 0) return as_error(as, "bad cfi off");
			CFI_B(0x80 | (unsigned)(reg & 0x3f));
			if (reg >= 64) CFI_ULEB(reg);
			CFI_ULEB((uint64_t)(off < 0 ? 0 : off));
		} else {
			return as_error(as, "unsupported CFI directive: %s", directive);
		}
#undef CFI_B
#undef CFI_ULEB
		return 0;
	}
	if (strcmp(directive, ".ident") == 0 || strcmp(directive, ".version") == 0)
		return 0;
	if (strcmp(directive, ".equ") == 0 || strcmp(directive, ".set") == 0) {
		/* .equ SYM, VALUE — define absolute symbol constant */
		char *name = rest;
		char *p = rest;
		while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
		if (*p) {
			if (*p == ',') *p++ = '\0';
			else { *p++ = '\0'; while (*p == ' ' || *p == '\t') p++; }
		}
		while (*p == ' ' || *p == '\t') p++;
		if (!*p || !*name)
			return as_error(as, ".equ requires SYM, VALUE");
		int64_t value;
		if (parse_integer(p, &value) != 0)
			return as_error(as, ".equ: invalid value for '%s'", name);
		struct as_symbol *sym = get_symbol(as, name);
		if (!sym)
			return as_error(as, ".equ: cannot create symbol '%s'", name);
		if (sym->defined)
			return as_error(as, ".equ: symbol '%s' already defined", name);
		sym->defined = 1;
		sym->value = (uint64_t)value;
		sym->section = (int)-1;  /* absolute symbol */
		return 0;
	}
	if (strcmp(directive, ".abort") == 0) {
		/* .abort — unconditionally stop assembly */
		return as_error(as, "assembly aborted by .abort directive");
	}
	if (strcmp(directive, ".error") == 0) {
		/* .error "message" — emit error */
		char *msg = rest;
		while (*msg == ' ' || *msg == '\t') msg++;
		if (*msg == '"') {
			msg++;
			char *end = strchr(msg, '"');
			if (end) *end = '\0';
		}
		return as_error(as, "%s", msg);
	}
	if (strcmp(directive, ".warning") == 0) {
		/* .warning "message" — emit warning, continue */
		char *msg = rest;
		while (*msg == ' ' || *msg == '\t') msg++;
		if (*msg == '"') {
			msg++;
			char *end = strchr(msg, '"');
			if (end) *end = '\0';
		}
		fprintf(stderr, "%s:%u: warning: %s\n", as->filename, as->line, msg);
		return 0;
	}
	/* Architecture-announcing directives emitted by mcc/other frontends.
	 * These are assembler-metadata (the encoding is already resolved in
	 * the instruction mnemonics), so mt/as treats them as no-ops rather
	 * than failing: `.syntax unified` (ARM), `.arch <arch>`, `.fpu <fpu>`,
	 * plus the generic `.align`/.cpu` variants some frontends emit. */
	if (strcmp(directive, ".syntax") == 0 ||
	    strcmp(directive, ".arch") == 0 ||
	    strcmp(directive, ".arch_extension") == 0 ||
	    strcmp(directive, ".cpu") == 0 ||
	    strcmp(directive, ".fpu") == 0)
		return 0;
	return as_error(as, "unsupported directive: %s", directive);
}
