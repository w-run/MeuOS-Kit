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
	char *diff_sym2 = NULL;
	int64_t diff_const = 0;
	char *diff_cursor;

	/* DWARF and other debug sections use symbol-difference expressions
	 * such as `.4byte .Lend - .Lstart - 4`.  Detect `SYMA - SYMB` here
	 * before the plain single-symbol parse_reference path. */
	diff_cursor = trim(text);
	{
		char *minus = strstr(diff_cursor, " - ");
		if (minus) {
			char *tail = minus + 3;
			char *sym1 = mt_strdup(diff_cursor);
			sym1[minus - diff_cursor] = '\0';
			/* tail is SYMB [+- CONST]; split a trailing arithmetic const */
			char *op = tail;
			while (*op && *op != '+' && *op != '-')
				++op;
			if (*op) {
				char saved = *op;
				*op = '\0';
				if (parse_integer(op + 1, &diff_const) != 0) {
					free(sym1);
					return as_error(as,
					    "invalid symbol-difference expression: %s",
					    text);
				}
				if (saved == '-')
					diff_const = -diff_const;
			}
			diff_sym2 = mt_strdup(trim(tail));
			text = sym1;   /* first symbol becomes the primary operand */
		}
	}

	if (parse_reference(trim(text), &symbol, modifier, sizeof(modifier),
	                    &addend, &is_number) != 0)
		return as_error(as, "invalid data expression: %s", text);
	if (modifier[0] != '\0') {
		free(symbol);
		free(diff_sym2);
		return as_error(as, "unsupported data relocation modifier: %s", modifier);
	}
	if (is_number && !diff_sym2) {
		value = (uint64_t)addend;
		free(symbol);
		return as_emit_le(as, section, value, width);
	}
	{
		struct as_symbol *referenced = get_symbol(as, symbol);
		if (!referenced) {
			free(symbol);
			free(diff_sym2);
			return -1;
		}
		if (!referenced->defined && symbol[0] != '.')
			referenced->bind = MT_STB_GLOBAL;
	}
	if (diff_sym2) {
		/* symbol-difference fixup: resolved at assembly time when both
		 * symbols share the section (the delta is base-independent, e.g.
		 * DWARF unit_length / high_pc).  Otherwise deferred to the linker
		 * as a SYMA-relative relocation carrying SYMB via addend. */
		struct as_symbol *ref2 = get_symbol(as, diff_sym2);
		if (!ref2) {
			free(symbol);
			free(diff_sym2);
			return -1;
		}
		if (!ref2->defined && diff_sym2[0] != '.')
			ref2->bind = MT_STB_GLOBAL;
		if (as_emit_le(as, section, 0, width) != 0 ||
		    as_add_fixup_diff(as, section, offset, width,
		                     data_reloc_type(as->target, width),
		                     addend + diff_const, symbol,
		                     diff_sym2) != 0) {
			free(symbol);
			free(diff_sym2);
			return -1;
		}
		free(symbol);
		free(diff_sym2);
		return 0;
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
parse_leb128(struct as_file *as, char *text, int signedness)
{
	char *cursor = text;
	char *item;
	for (;;) {
		item = next_csv(&cursor);
		if (!*item)
			return as_error(as, "empty LEB128 expression");
		/* DWARF .uleb128/.sleb128 in mcc output may use symbol-difference
		 * expressions (e.g. `.uleb128 main - add` in the .debug_line
		 * program).  Detect and resolve at assembly time when both symbols
		 * are in the same section, so the delta is a plain constant. */
		int64_t sval;
		{
			char *minus = strstr(item, " - ");
			if (minus) {
				char *tail = minus + 3;
				char *sym1 = mt_strdup(item);
				sym1[minus - item] = '\0';
				char *s1 = trim(sym1);
				char *s2 = trim(tail);
				struct as_symbol *a = find_symbol(as, s1);
				struct as_symbol *b = find_symbol(as, s2);
				if (!a || !b) {
					free(sym1);
					return as_error(as, "undefined symbol in LEB128 expression");
				}
				if (a->section < 0 || a->section != b->section) {
					free(sym1);
					return as_error(as, "cross-section symbol difference in LEB128");
				}
				sval = (int64_t)(a->value - b->value);
				free(sym1);
			} else {
				if (parse_integer(item, &sval) != 0)
					return as_error(as, "invalid LEB128 value");
			}
		}
		/* Encode as LEB128 */
		unsigned char out[16];
		size_t n = 0;
		if (signedness) {
			/* signed LEB128 */
			int more = 1;
			while (more) {
				unsigned char b = (unsigned char)(sval & 0x7f);
				sval >>= 7; /* arithmetic shift preserves sign */
				if ((sval == 0 && !(b & 0x40)) ||
				    (sval == -1 && (b & 0x40)))
					more = 0;
				else
					b |= 0x80;
				out[n++] = b;
			}
		} else {
			uint64_t value = (uint64_t)sval;
			do {
				unsigned char b = (unsigned char)(value & 0x7f);
				value >>= 7;
				if (value)
					b |= 0x80;
				out[n++] = b;
			} while (value);
		}
		if (as_append_bytes(as, &as->sections[as->current], out, n) != 0)
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
	/* GNU-as size-suffixed aliases used by some DWARF emitters */
	if (strcmp(directive, ".2byte") == 0)
		return parse_data_list(as, rest, 2);
	if (strcmp(directive, ".4byte") == 0)
		return parse_data_list(as, rest, 4);
	if (strcmp(directive, ".8byte") == 0)
		return parse_data_list(as, rest, 8);
	if (strcmp(directive, ".uleb128") == 0)
		return parse_leb128(as, rest, 0);
	if (strcmp(directive, ".sleb128") == 0)
		return parse_leb128(as, rest, 1);
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
	/* DWARF .cfi_sections — select output section */
	if (strcmp(directive, ".cfi_sections") == 0) {
		/* .cfi_sections .debug_frame  or  .cfi_sections .eh_frame */
		char *p = trim(rest);
		if (*p == '\0')
			return as_error(as, ".cfi_sections requires an argument");
		if (strcmp(p, ".debug_frame") == 0) {
			as->cfi_section_type = 1;
		} else if (strcmp(p, ".eh_frame") == 0) {
			as->cfi_section_type = 0;
		} else {
			return as_error(as, "unsupported .cfi_sections section: %s", p);
		}
		return 0;
	}
	/* DWARF CFI directives */
	if (strcmp(directive, ".cfi_startproc") == 0) {
		if (as->cfi_active)
			return as_error(as, "nested .cfi_startproc");
		as->cfi_active = 1;
		as->cfi_prog_size = 0;
		as->cfi_func_start = as->sections[as->current].size;
		as->cfi_cfa_offset_valid = 0;
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
			char **ll = (char **)mt_realloc(as->cfi_lsda_pointers, cap * sizeof(*ll));
			int *npfde = (int *)mt_realloc(as->cfi_fde_personality_set, cap * sizeof(*npfde));
			uint8_t *npfe = (uint8_t *)mt_realloc(as->cfi_fde_personality_encoding, cap * sizeof(*npfe));
			char **npfs = (char **)mt_realloc(as->cfi_fde_personality_symbol, cap * sizeof(*npfs));
			int *npsf = (int *)mt_realloc(as->cfi_fde_signal_frame, cap * sizeof(*npsf));
			int *nrc = (int *)mt_realloc(as->cfi_fde_return_column, cap * sizeof(*nrc));
			if (!no || !ne || !nl || !np || !ns || !ll ||
			    !npfde || !npfe || !npfs || !npsf || !nrc) {
				free(no); free(ne); free(nl); free(np); free(ns); free(ll);
				free(npfde); free(npfe); free(npfs); free(npsf);
				free(nrc);
				return as_error(as, "out of memory");
			}
			as->cfi_func_offsets = no;
			as->cfi_func_end = ne;
			as->cfi_func_labels = nl;
			as->cfi_fde_progs = np;
			as->cfi_fde_sizes = ns;
			as->cfi_lsda_pointers = ll;
			as->cfi_fde_personality_set = npfde;
			as->cfi_fde_personality_encoding = npfe;
			as->cfi_fde_personality_symbol = npfs;
			as->cfi_fde_signal_frame = npsf;
			as->cfi_fde_return_column = nrc;
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
		/* Save LSDA pointer for this FDE */
		if (as->cfi_lsda_current) {
			as->cfi_lsda_pointers[as->cfi_fde_count] = as->cfi_lsda_current;
			as->cfi_lsda_current = NULL;
		} else {
			as->cfi_lsda_pointers[as->cfi_fde_count] = mt_strdup("");
			if (!as->cfi_lsda_pointers[as->cfi_fde_count])
				return as_error(as, "out of memory");
		}
		/* Save personality and signal_frame for this FDE */
		as->cfi_fde_personality_set[as->cfi_fde_count] = as->cfi_personality_set;
		as->cfi_fde_personality_encoding[as->cfi_fde_count] = as->cfi_personality_encoding;
		as->cfi_fde_personality_symbol[as->cfi_fde_count] =
			as->cfi_personality_symbol ? mt_strdup(as->cfi_personality_symbol) : NULL;
		as->cfi_fde_signal_frame[as->cfi_fde_count] = as->cfi_signal_frame;
		as->cfi_fde_return_column[as->cfi_fde_count] = as->cfi_return_column;
		/* Reset CIE-level state for next FDE */
		as->cfi_signal_frame = 0;
		as->cfi_return_column = 0;
		as->cfi_personality_set = 0;
		if (as->cfi_personality_symbol) {
			free(as->cfi_personality_symbol);
			as->cfi_personality_symbol = NULL;
		}
		as->cfi_lsda_set = 0;
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
			as->cfi_cfa_offset = off;
			as->cfi_cfa_offset_valid = 1;
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
			as->cfi_cfa_offset = off;
			as->cfi_cfa_offset_valid = 1;
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
		} else if (strcmp(directive, ".cfi_restore") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi reg");
			if (reg < 64) {
				CFI_B(0xc0 | (unsigned char)(reg & 0x3f));
			} else {
				CFI_B(0x0d); /* DW_CFA_restore_extended */
				CFI_ULEB(reg);
			}
		} else if (strcmp(directive, ".cfi_advance_loc") == 0) {
			char *item = next_csv(&cursor);
			if (parse_integer(item, &off) != 0) return as_error(as, "bad cfi delta");
			if (off >= 0 && off < 64) {
				CFI_B(0x40 | (unsigned char)(off & 0x3f));
			} else {
				CFI_B(0x02); /* DW_CFA_advance_loc1 */
				CFI_B((unsigned char)(off & 0xff));
			}
		} else if (strcmp(directive, ".cfi_personality") == 0) {
			/* .cfi_personality ENC, SYM */
			char *item = next_csv(&cursor);
			int64_t enc;
			if (parse_integer(item, &enc) != 0 || enc < 0 || enc > 0xff)
				return as_error(as, "bad cfi personality encoding");
			item = next_csv(&cursor);
			if (!item || !*item)
				return as_error(as, "missing symbol in .cfi_personality");
			as->cfi_personality_set = 1;
			as->cfi_personality_encoding = (uint8_t)enc;
			if (as->cfi_personality_symbol)
				free(as->cfi_personality_symbol);
			as->cfi_personality_symbol = mt_strdup(trim(item));
			if (!as->cfi_personality_symbol)
				return as_error(as, "out of memory");
		} else if (strcmp(directive, ".cfi_lsda") == 0) {
			/* .cfi_lsda ENC, SYM */
			char *item = next_csv(&cursor);
			int64_t enc;
			if (parse_integer(item, &enc) != 0 || enc < 0 || enc > 0xff)
				return as_error(as, "bad cfi lsda encoding");
			item = next_csv(&cursor);
			if (!item || !*item)
				return as_error(as, "missing symbol in .cfi_lsda");
			as->cfi_lsda_set = 1;
			as->cfi_lsda_encoding = (uint8_t)enc;
			if (as->cfi_lsda_current)
				free(as->cfi_lsda_current);
			as->cfi_lsda_current = mt_strdup(trim(item));
			if (!as->cfi_lsda_current)
				return as_error(as, "out of memory");
		} else if (strcmp(directive, ".cfi_signal_frame") == 0) {
			/* .cfi_signal_frame — mark CIE as signal frame */
			as->cfi_signal_frame = 1;
		} else if (strcmp(directive, ".cfi_window_save") == 0) {
			/* DW_CFA_GNU_window_save (0x2d) — SPARC window save */
			CFI_B(0x2d);
		} else if (strcmp(directive, ".cfi_return_column") == 0) {
			/* .cfi_return_column REG — set return address register column */
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi return column");
			as->cfi_return_column = (int)reg;
		} else if (strcmp(directive, ".cfi_escape") == 0) {
			/* .cfi_escape BYTES... — raw DW_CFA byte sequence (comma-separated) */
			do {
				char *item = next_csv(&cursor);
				if (!*item) break;
				if (parse_integer(item, &value) != 0 || value < 0 || value > 255)
					return as_error(as, "bad cfi_escape byte");
				CFI_B((unsigned char)(value & 0xff));
			} while (*cursor);
		} else if (strcmp(directive, ".cfi_adjust_cfa_offset") == 0) {
			/* .cfi_adjust_cfa_offset OFF — adjust CFA offset by delta */
			char *item = next_csv(&cursor);
			if (parse_integer(item, &off) != 0) return as_error(as, "bad cfi adjust cfa offset");
			if (!as->cfi_cfa_offset_valid)
				return as_error(as, ".cfi_adjust_cfa_offset requires preceding .cfi_def_cfa");
			as->cfi_cfa_offset += off;
			CFI_B(0x0e); /* DW_CFA_def_cfa_offset */
			CFI_ULEB((uint64_t)as->cfi_cfa_offset);
		} else if (strcmp(directive, ".cfi_val_offset") == 0) {
			/* .cfi_val_offset REG, OFF — DW_CFA_val_offset (0x14) */
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi val_offset reg");
			item = next_csv(&cursor);
			if (parse_integer(item, &off) != 0) return as_error(as, "bad cfi val_offset off");
			CFI_B(0x14);
			CFI_ULEB(reg);
			CFI_ULEB(off);
		} else if (strcmp(directive, ".cfi_val_expression") == 0) {
			/* .cfi_val_expression REG, EXPR — DW_CFA_val_expression (0x0f) */
			char *item = next_csv(&cursor);
			if (parse_integer(item, &reg) != 0) return as_error(as, "bad cfi val_expression reg");
			item = trim(cursor);
			if (!*item)
				return as_error(as, "missing expression in .cfi_val_expression");
			/* Parse the expression as a comma-separated list of bytes */
			/* First, we need to emit the expression into a temp buffer to know its length */
			unsigned char expr_buf[256];
			size_t expr_len = 0;
			char *ec = item;
			do {
				char *eitem = next_csv(&ec);
				if (!*eitem) break;
				int64_t ev;
				if (parse_integer(eitem, &ev) != 0 || ev < 0 || ev > 255)
					return as_error(as, "bad cfi_val_expression byte");
				if (expr_len >= sizeof(expr_buf))
					return as_error(as, "cfi_val_expression too long");
				expr_buf[expr_len++] = (unsigned char)(ev & 0xff);
			} while (*ec);
			CFI_B(0x0f); /* DW_CFA_val_expression */
			CFI_ULEB(reg);
			CFI_ULEB(expr_len);
			{ size_t ei; for (ei = 0; ei < expr_len; ei++) CFI_B(expr_buf[ei]); }
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
