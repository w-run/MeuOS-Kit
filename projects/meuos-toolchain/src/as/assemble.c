/* assemble.c - AT&T subset assembler and ELF ET_REL writer. */
#include "mt/as.h"
#include "mt/as_int.h"
#include "mt/elf.h"
#include "mt/elf32.h"
#include "mt/target.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MT_ST_INFO(bind, type) MT_ELF64_ST_INFO(bind, type)

struct out_section {
	const char *name;
	uint32_t type;
	uint64_t flags;
	uint64_t align;
	unsigned char *data;
	size_t size;
	uint64_t file_offset;
	uint32_t link;
	uint32_t info;
	uint64_t entry_size;
	int nobits;
};

struct out_reloc {
	uint64_t offset;
	uint32_t type;
	uint32_t symbol;
	int64_t addend;
};

struct reloc_group {
	struct out_reloc *items;
	size_t count;
	size_t capacity;
};

struct string_table {
	unsigned char *data;
	size_t size;
	size_t capacity;
};

static void *
mt_malloc(size_t size)
{
	/* malloc(0) is implementation-defined (may return NULL or a unique ptr).
	 * We guarantee a minimum 1-byte allocation to avoid ambiguity. */
	void *p = malloc(size == 0 ? 1 : size);
	return p;
}

static void *
mt_realloc(void *old, size_t size)
{
	return realloc(old, size == 0 ? 1 : size);
}

static char *
mt_strdup(const char *text)
{
	size_t n = strlen(text);
	char *copy = (char *)mt_malloc(n + 1);
	if (copy)
		memcpy(copy, text, n + 1);
	return copy;
}

int
as_error(struct as_file *as, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vsnprintf(as->error, sizeof(as->error), format, ap);
	va_end(ap);
	return -1;
}

static char *
trim(char *text)
{
	char *end;
	while (*text && isspace((unsigned char)*text))
		++text;
	end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1]))
		--end;
	*end = '\0';
	return text;
}

static void
strip_comments(struct as_file *as, char *line)
{
	char *source = line;
	char *destination = line;
	int quote = 0;
	int escape = 0;

	while (*source) {
		if (as->block_comment) {
			if (source[0] == '*' && source[1] == '/') {
				as->block_comment = 0;
				source += 2;
			} else
				++source;
			continue;
		}
		if (escape) {
			*destination++ = *source++;
			escape = 0;
			continue;
		}
		if (*source == '\\' && quote) {
			*destination++ = *source++;
			escape = 1;
			continue;
		}
		if (*source == '"') {
			quote = !quote;
			*destination++ = *source++;
			continue;
		}
		if (!quote && source[0] == '/' && source[1] == '*') {
			as->block_comment = 1;
			source += 2;
			continue;
		}
		if (!quote && source[0] == '/' && source[1] == '/') {
			break; /* GAS line comment */
		}
		if (!quote && *source == '#' &&
		    as->target && strcmp(as->target->name, "x86_64") == 0)
			break;
		*destination++ = *source++;
	}
	*destination = '\0';
}

static void
free_as(struct as_file *as)
{
	size_t i;
	for (i = 0; i < as->section_count; ++i) {
		free(as->sections[i].name);
		free(as->sections[i].data);
	}
	for (i = 0; i < as->symbol_count; ++i)
		free(as->symbols[i].name);
	for (i = 0; i < as->fixup_count; ++i)
		free(as->fixups[i].symbol);
	for (i = 0; i < as->dwarf_file_count; ++i)
		free(as->dwarf_files[i].name);
	for (i = 0; i < as->cfi_fde_count; ++i) {
		free(as->cfi_func_labels[i]);
		free(as->cfi_fde_progs[i]);
	}
	free(as->sections);
	free(as->symbols);
	free(as->fixups);
	free(as->dwarf_files);
	free(as->dwarf_locs);
	free(as->cfi_prog);
	free(as->cfi_func_offsets);
	free(as->cfi_func_end);
	free(as->cfi_func_labels);
	free(as->cfi_fde_progs);
	free(as->cfi_fde_sizes);
	memset(as, 0, sizeof(*as));
}

int
as_append_bytes(struct as_file *as, struct as_section *section,
             const void *data, size_t size)
{
	size_t capacity;
	unsigned char *buffer;

	if (size > SIZE_MAX - section->size)
		return as_error(as, "section size overflow");
	if (section->type == MT_SHT_NOBITS) {
		section->size += size;
		return 0;
	}
	if (section->size + size > section->capacity) {
		capacity = section->capacity ? section->capacity * 2 : 256;
		while (capacity < section->size + size) {
			if (capacity > SIZE_MAX / 2) {
				capacity = section->size + size;
				break;
			}
			capacity *= 2;
		}
		buffer = (unsigned char *)mt_realloc(section->data, capacity);
		if (!buffer)
			return as_error(as, "out of memory");
		section->data = buffer;
		section->capacity = capacity;
	}
	if (size != 0)
		memcpy(section->data + section->size, data, size);
	section->size += size;
	return 0;
}

static int
append_zeroes(struct as_file *as, struct as_section *section, size_t count)
{
	static const unsigned char zero[256];
	while (count != 0) {
		size_t n = count < sizeof(zero) ? count : sizeof(zero);
		if (as_append_bytes(as, section, zero, n) != 0)
			return -1;
		count -= n;
	}
	return 0;
}

static int
align_section(struct as_file *as, struct as_section *section, uint64_t align)
{
	uint64_t mask;
	size_t padding;

	if (align == 0 || (align & (align - 1)) != 0)
		return as_error(as, "alignment must be a power of two");
	if (align > SIZE_MAX)
		return as_error(as, "alignment is too large");
	section->align = section->align < align ? align : section->align;
	mask = align - 1;
	padding = (size_t)((align - (section->size & mask)) & mask);
	if (section->name && strcmp(section->name, ".text") == 0 &&
	    section->type != MT_SHT_NOBITS) {
		static const unsigned char nop = 0x90;
		while (padding != 0) {
			if (as_append_bytes(as, section, &nop, 1) != 0)
				return -1;
			--padding;
		}
		return 0;
	}
	return append_zeroes(as, section, padding);
}

static void
section_attributes(const char *name, uint32_t *type, uint64_t *flags,
                   uint64_t *align)
{
	*type = MT_SHT_PROGBITS;
	*flags = 0;
	*align = 1;
	if (strcmp(name, ".text") == 0) {
		*flags = MT_SHF_ALLOC | MT_SHF_EXECINSTR;
		*align = 16;
	} else if (strcmp(name, ".rodata") == 0 ||
	           strcmp(name, ".eh_frame") == 0) {
		*flags = MT_SHF_ALLOC;
		*align = 8;
	} else if (strcmp(name, ".data") == 0 ||
	           strcmp(name, ".tdata") == 0) {
		*flags = MT_SHF_ALLOC | MT_SHF_WRITE;
		*align = 8;
		if (strcmp(name, ".tdata") == 0)
			*flags |= MT_SHF_TLS;
	} else if (strcmp(name, ".bss") == 0 ||
	           strcmp(name, ".tbss") == 0) {
		*type = MT_SHT_NOBITS;
		*flags = MT_SHF_ALLOC | MT_SHF_WRITE;
		*align = 8;
		if (strcmp(name, ".tbss") == 0)
			*flags |= MT_SHF_TLS;
	}
}

static int
get_section(struct as_file *as, const char *name)
{
	struct as_section *sections;
	size_t i;
	uint32_t type;
	uint64_t flags, align;

	for (i = 0; i < as->section_count; ++i)
		if (strcmp(as->sections[i].name, name) == 0)
			return (int)i;
	if (as->section_count == as->section_capacity) {
		size_t capacity = as->section_capacity ? as->section_capacity * 2 : 8;
		sections = (struct as_section *)mt_realloc(
		    as->sections, capacity * sizeof(*sections));
		if (!sections) {
			as_error(as, "out of memory");
			return -1;
		}
		as->sections = sections;
		as->section_capacity = capacity;
	}
	section_attributes(name, &type, &flags, &align);
	as->sections[as->section_count].name = mt_strdup(name);
	if (!as->sections[as->section_count].name) {
		as_error(as, "out of memory");
		return -1;
	}
	as->sections[as->section_count].type = type;
	as->sections[as->section_count].flags = flags;
	as->sections[as->section_count].align = align;
	as->sections[as->section_count].data = NULL;
	as->sections[as->section_count].size = 0;
	as->sections[as->section_count].capacity = 0;
	return (int)as->section_count++;
}

static struct as_symbol *
find_symbol(struct as_file *as, const char *name)
{
	size_t i;
	for (i = 0; i < as->symbol_count; ++i)
		if (strcmp(as->symbols[i].name, name) == 0)
			return &as->symbols[i];
	return NULL;
}

static struct as_symbol *
get_symbol(struct as_file *as, const char *name)
{
	struct as_symbol *symbols;
	struct as_symbol *symbol = find_symbol(as, name);
	if (symbol)
		return symbol;
	if (as->symbol_count == as->symbol_capacity) {
		size_t capacity = as->symbol_capacity ? as->symbol_capacity * 2 : 32;
		symbols = (struct as_symbol *)mt_realloc(
		    as->symbols, capacity * sizeof(*symbols));
		if (!symbols) {
			as_error(as, "out of memory");
			return NULL;
		}
		as->symbols = symbols;
		as->symbol_capacity = capacity;
	}
	symbol = &as->symbols[as->symbol_count++];
	memset(symbol, 0, sizeof(*symbol));
	symbol->name = mt_strdup(name);
	if (!symbol->name) {
		--as->symbol_count;
		as_error(as, "out of memory");
		return NULL;
	}
	symbol->section = -1;
	symbol->type = MT_STT_NOTYPE;
	return symbol;
}

int
as_add_fixup(struct as_file *as, struct as_section *section, size_t offset,
          unsigned width, unsigned type, int64_t addend, const char *symbol)
{
	struct as_fixup *fixups;
	if (as->fixup_count == as->fixup_capacity) {
		size_t capacity = as->fixup_capacity ? as->fixup_capacity * 2 : 32;
		fixups = (struct as_fixup *)mt_realloc(
		    as->fixups, capacity * sizeof(*fixups));
		if (!fixups)
			return as_error(as, "out of memory");
		as->fixups = fixups;
		as->fixup_capacity = capacity;
	}
	as->fixups[as->fixup_count].section = (int)(section - as->sections);
	as->fixups[as->fixup_count].offset = offset;
	as->fixups[as->fixup_count].width = width;
	as->fixups[as->fixup_count].type = type;
	as->fixups[as->fixup_count].addend = addend;
	as->fixups[as->fixup_count].symbol = mt_strdup(symbol);
	if (!as->fixups[as->fixup_count].symbol)
		return as_error(as, "out of memory");
	{
		struct as_symbol *referenced = get_symbol(as, symbol);
		if (!referenced)
			return -1;
		if (!referenced->defined && symbol[0] != '.')
			referenced->bind = MT_STB_GLOBAL;
	}
	++as->fixup_count;
	return 0;
}

static int
parse_integer(const char *text, int64_t *value)
{
	char *end;
	long long result;
	if (!text || !*text)
		return -1;
	errno = 0;
	result = strtoll(text, &end, 0);
	if (errno == ERANGE || *trim(end) != '\0')
		return -1;
	*value = (int64_t)result;
	return 0;
}

static int
parse_reference(const char *text, char **symbol, char *modifier,
                size_t modifier_size, int64_t *addend, int *is_number)
{
	char buffer[512];
	char *at;
	char *split;
	char *end;
	int64_t number;

	*symbol = NULL;
	*addend = 0;
	*is_number = 0;
	if (strlen(text) >= sizeof(buffer))
		return -1;
	strcpy(buffer, text);
	trim(buffer);
	at = strchr(buffer, '@');
	if (at) {
		*at++ = '\0';
		if (strlen(at) >= modifier_size)
			return -1;
		strcpy(modifier, at);
	} else
		modifier[0] = '\0';
	split = NULL;
	for (end = buffer + 1; *end; ++end)
		if ((*end == '+' || *end == '-') && end[-1] != 'e' && end[-1] != 'E') {
			split = end;
			break;
		}
	if (split) {
		*split++ = '\0';
		if (parse_integer(split, addend) != 0)
			return -1;
	}
	if (parse_integer(buffer, &number) == 0) {
		if (split)
			*addend += number;
		else
			*addend = number;
		*is_number = 1;
		return 0;
	}
	if (!buffer[0])
		return -1;
	/* Strip surrounding quotes (e.g., mcc emits "symbol" for names
	 * containing dots or other special characters). */
	if (buffer[0] == '"' && buffer[strlen(buffer) - 1] == '"' &&
	    strlen(buffer) >= 2) {
		size_t len = strlen(buffer);
		memmove(buffer, buffer + 1, len - 2);
		buffer[len - 2] = '\0';
	}
	*symbol = mt_strdup(buffer);
	return *symbol ? 0 : -1;
}


int
as_emit_u8(struct as_file *as, struct as_section *section, unsigned value)
{
	unsigned char byte = (unsigned char)value;
	return as_append_bytes(as, section, &byte, 1);
}

int
as_emit_le(struct as_file *as, struct as_section *section, uint64_t value,
        unsigned width)
{
	unsigned char bytes[8];
	unsigned i;
	for (i = 0; i < width; ++i)
		bytes[i] = (unsigned char)(value >> (i * 8));
	return as_append_bytes(as, section, bytes, width);
}

static int
emit_instruction(struct as_file *as, char *mnemonic, char *operand_text)
{
	struct as_section *section = &as->sections[as->current];

	/* If the target has its own encoder, delegate entirely. */
	if (as->target && as->target->encode_insn) {
		struct mt_insn insn;
		memset(&insn, 0, sizeof(insn));

		/* Pre-encode ISA gating: heuristic check for VEX-prefix (AVX)
		 * instructions before calling the encoder — because the encoder
		 * itself returns -1 for any instruction it can't handle (including
		 * VEX), we need to distinguish "missing CPU feature" from
		 * "encoder doesn't support this yet".  The heuristic is simple:
		 * mnemonic starts with 'v' → AVX required.  Future improvements
		 * should use a proper VEX/EVEX opcode table.
		 * Only apply on x86/x86_64 targets (non-x86 targets like ARM
		 * have 'v'-prefixed VFP/NEON instructions). */
		const char *mn = mnemonic;
		while (*mn == '_' || *mn == '.') mn++;  /* skip decorators */
		if (mn[0] == 'v' && mn[1] != '\0' &&
		    (as->target->emachine == MT_EM_X86_64 || as->target->emachine == MT_EM_386)) {
			if ((as->target->features & MT_FEATURE_AVX) == 0)
				return as_error(as,
					"instruction %s requires AVX (use -march=x86-64-v3 or higher)",
					mnemonic);
			/* AVX is enabled — fall through to the encoder (it may still
			 * reject the instruction if VEX encoding is not implemented). */
		}

		if (as->target->encode_insn(as->target, mnemonic, operand_text,
		                             &insn) < 0) {
			return as_error(as, "unsupported instruction: %s", mnemonic);
		}
		/* ISA feature gating (as-isa-gating): reject instructions whose
		 * required ISA extension is not enabled for the active target.
		 * The encoder sets insn.required_features; we report a precise
		 * diagnostic naming the missing extension. */
		if (insn.required_features) {
			const char *missing =
				mt_feature_name_missing(as->target->features,
				                        insn.required_features);
			if (missing)
				return as_error(as,
					"instruction %s requires disabled ISA extension '%s' "
					"(enable via -march=)", mnemonic, missing);
		}
		if (as_append_bytes(as, section, insn.bytes, insn.size) != 0)
			return -1;
		if (!insn.fixed && insn.fixup_symbol) {
			size_t section_offset = section->size - insn.size;
			if (as_add_fixup(as, section,
			                 section_offset + insn.fixup_offset,
			                 insn.fixup_width,
			                 insn.reloc_type, insn.fixup_addend,
			                 insn.fixup_symbol) != 0)
				return -1;
		}
		if (insn.fixup2_present) {
			size_t section_offset = section->size - insn.size;
			if (as_add_fixup(as, section,
			                 section_offset + insn.fixup2_offset,
			                 insn.fixup2_width,
			                 insn.reloc_type2, insn.fixup2_addend,
			                 insn.fixup2_symbol) != 0)
				return -1;
		}
		return 0;
	}

	return as_error(as, "unsupported instruction: %s", mnemonic);
}

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
	               width == 8 ? MT_R_X86_64_64 : MT_R_X86_64_32,
	               addend, symbol) != 0) {
		free(symbol);
		return -1;
	}
	free(symbol);
	return 0;
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

static int
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
	return as_error(as, "unsupported directive: %s", directive);
}

static int
parse_line(struct as_file *as, char *line)
{
	char *text = trim(line);
	char *colon;
	char *end;
	char *mnemonic;
	char *rest;
	struct as_symbol *symbol;

	strip_comments(as, text);
	text = trim(text);
	while (*text) {
		colon = strchr(text, ':');
		if (!colon)
			break;
		for (end = text; end < colon && !isspace((unsigned char)*end); ++end)
			;
		if (end != colon)
			break;
		*colon = '\0';
		if (!*text)
			return as_error(as, "empty label");
		{
			char numeric_name[64];
			int numeric = text[0] >= '0' && text[0] <= '9' && text[1] == '\0';
			if (numeric) {
				int number = text[0] - '0';
				++as->numeric_counts[number];
				snprintf(numeric_name, sizeof(numeric_name),
				         ".Lmt_num_%d_%zu", number, as->numeric_counts[number]);
				symbol = get_symbol(as, numeric_name);
			} else
				symbol = get_symbol(as, text);
		}

		if (!symbol)
			return -1;
		if (symbol->defined)
			return as_error(as, "duplicate symbol: %s", text);
		symbol->defined = 1;
		symbol->section = as->current;
		symbol->value = as->sections[as->current].size;
		if (text[0] == '.')
			symbol->bind = MT_STB_LOCAL;
		text = trim(colon + 1);
	}
	if (!*text)
		return 0;
	mnemonic = text;
	while (*text && !isspace((unsigned char)*text))
		++text;
	if (*text) {
		*text++ = '\0';
		rest = trim(text);
	} else
		rest = text;
	if (mnemonic[0] == '.')
		return parse_directive(as, mnemonic, rest);
	return emit_instruction(as, mnemonic, rest);
}

/* Returns 1 when we are inside a false branch (should skip). */
static int
is_skipping(struct as_file *as)
{
	int i;
	for (i = 0; i < as->cond_depth; i++)
		if (as->cond_stack[i])
			return 1;
	return 0;
}

/* Parse a conditional assembly directive (.if/.ifdef/.ifndef/.else/.endif).
 * Called from both the skipping path and parse_directive. */
static int
parse_conditional(struct as_file *as, const char *directive, const char *rest)
{
	/* .endif: pop the stack */
	if (strcmp(directive, ".endif") == 0) {
		if (as->cond_depth <= 0)
			return as_error(as, ".endif without .if");
		as->cond_depth--;
		return 0;
	}
	/* .else: toggle the current level (only if not already toggled) */
	if (strcmp(directive, ".else") == 0) {
		if (as->cond_depth <= 0)
			return as_error(as, ".else without .if");
		/* .else toggles: if we were skipping (false branch), now we're
		 * in the else part (active), and vice versa. But we need to
		 * check whether there's a parent skip. */
		int parent_skip = 0;
		int i;
		for (i = 0; i < as->cond_depth - 1; i++)
			if (as->cond_stack[i]) parent_skip = 1;
		/* Toggle only if no parent skip; otherwise the entire block is skipped */
		if (!parent_skip)
			as->cond_stack[as->cond_depth - 1] =
				!as->cond_stack[as->cond_depth - 1];
		return 0;
	}
	/* .if / .ifdef / .ifndef: push and evaluate */
	if (as->cond_depth >= 16)
		return as_error(as, "conditional nesting too deep");
	/* Default: if we are already skipping, push 1 (still skipping) */
	int result = 0;
	if (is_skipping(as)) {
		result = 1; /* still skipping */
	} else {
		/* Evaluate the condition */
		int64_t value = 0;
		char name[256];
		const char *rp = rest;
		while (*rp && isspace((unsigned char)*rp)) rp++;
		if (strcmp(directive, ".ifdef") == 0 || strcmp(directive, ".ifndef") == 0) {
			/* Extract symbol name */
			size_t nl = 0;
			while (rp[nl] && !isspace((unsigned char)rp[nl]) && rp[nl] != ',')
				nl++;
			if (nl == 0 || nl >= sizeof(name))
				return as_error(as, "invalid .ifdef symbol");
			memcpy(name, rp, nl);
			name[nl] = '\0';
			/* Look up in symbol table */
			int found = 0;
			size_t si;
			for (si = 0; si < as->symbol_count; si++) {
				if (strcmp(as->symbols[si].name, name) == 0 &&
				    as->symbols[si].defined) {
					found = 1;
					break;
				}
			}
			value = (strcmp(directive, ".ifdef") == 0) ? found : !found;
		} else {
			/* .if: parse integer expression */
			char *end;
			value = strtoll(rp, &end, 0);
		}
		result = (value == 0) ? 1 : 0;
	}
	as->cond_stack[as->cond_depth++] = result;
	return 0;
}

static int
parse_source(struct as_file *as, FILE *input)
{
	char line[4096];
	as->current = get_section(as, ".text");
	if (as->current < 0)
		return -1;
	while (fgets(line, sizeof(line), input)) {
		++as->line;
		if (strchr(line, '\n') == NULL && !feof(input))
			return as_error(as, "source line is too long");

		/* Check for conditional directives (always, even when skipping) */
		int cond_handled = 0;
		char *tp = line;
		while (*tp && isspace((unsigned char)*tp)) tp++;
		if (*tp == '.') {
			char d[64];
			char *ep = tp;
			while (*ep && !isspace((unsigned char)*ep)) ep++;
			size_t dlen = (size_t)(ep - tp);
			if (dlen > 0 && dlen < 64) {
				memcpy(d, tp, dlen);
				d[dlen] = '\0';
			if (strcmp(d, ".if") == 0 || strcmp(d, ".ifdef") == 0 ||
			    strcmp(d, ".ifndef") == 0 ||
			    strcmp(d, ".else") == 0 || strcmp(d, ".endif") == 0) {
				if (parse_conditional(as, d, ep) != 0)
					return -1;
				cond_handled = 1;
			} else if (strcmp(d, ".rept") == 0) {
				/* .rept N: repeat the following lines N times */
				if (as->rept_count != 0)
					return as_error(as, "nested .rept not supported");
				int64_t n = strtoll(ep, NULL, 10);
				if (n <= 0)
					return as_error(as, "invalid .rept count");
				as->rept_count = (int)n;
				/* Save position of the NEXT line so .endr can seek back */
				as->rept_pos = ftell(input);
				cond_handled = 1; /* skip assembling the .rept line */
			} else if (strcmp(d, ".endr") == 0) {
				if (as->rept_count == 0)
					return as_error(as, ".endr without .rept");
				as->rept_count--;
				if (as->rept_count > 0)
					fseek(input, as->rept_pos, SEEK_SET);
				cond_handled = 1;
			}
			}
		}
		/* Skip lines when inside a false conditional branch.
		 * Conditionals have already been processed above. */
		if (cond_handled || is_skipping(as))
			continue;
		if (parse_line(as, line) != 0)
			return -1;
	}
	if (as->cond_depth != 0)
		return as_error(as, "unclosed .if/.ifdef");
	if (ferror(input))
		return as_error(as, "input read failed");
	return 0;
}

struct elf_sym_out {
	uint32_t name;
	uint8_t info;
	uint8_t other;
	uint16_t section;
	uint64_t value;
	uint64_t size;
};

static int
string_init(struct string_table *table)
{
	memset(table, 0, sizeof(*table));
	table->data = (unsigned char *)mt_malloc(1);
	if (!table->data)
		return -1;
	table->data[0] = 0;
	table->size = 1;
	table->capacity = 1;
	return 0;
}

static int
string_add(struct string_table *table, const char *text, uint32_t *offset)
{
	size_t length = strlen(text);
	size_t needed;
	unsigned char *data;
	size_t capacity;

	if (length > UINT32_MAX || table->size > SIZE_MAX - length - 1)
		return -1;
	needed = table->size + length + 1;
	if (needed > table->capacity) {
		capacity = table->capacity ? table->capacity * 2 : 64;
		while (capacity < needed) {
			if (capacity > SIZE_MAX / 2) {
				capacity = needed;
				break;
			}
			capacity *= 2;
		}
		data = (unsigned char *)mt_realloc(table->data, capacity);
		if (!data)
			return -1;
		table->data = data;
		table->capacity = capacity;
	}
	*offset = (uint32_t)table->size;
	memcpy(table->data + table->size, text, length + 1);
	table->size = needed;
	return 0;
}

static int
reloc_add(struct reloc_group *group, uint64_t offset, unsigned type,
          uint32_t symbol, int64_t addend)
{
	struct out_reloc *items;
	if (group->count == group->capacity) {
		size_t capacity = group->capacity ? group->capacity * 2 : 16;
		items = (struct out_reloc *)mt_realloc(group->items,
		                                       capacity * sizeof(*items));
		if (!items)
			return -1;
		group->items = items;
		group->capacity = capacity;
	}
	group->items[group->count++] = (struct out_reloc){
		.offset = offset, .type = type, .symbol = symbol, .addend = addend
	};
	return 0;
}

static int
patch_le(struct as_file *as, struct as_section *section, size_t offset,
         unsigned width, uint64_t value)
{
	unsigned i;
	if (offset > section->size || width > section->size - offset)
		return as_error(as, "internal relocation offset out of range");
	if (width != 4 && width != 8 && width != 2 && width != 1)
		return as_error(as, "unsupported relocation width");
	for (i = 0; i < width; ++i)
		section->data[offset + i] = (unsigned char)(value >> (i * 8));
	return 0;
}

static int
build_output_sections(struct as_file *as, struct out_section **out_sections,
                      size_t *out_count, int **section_map,
                      struct reloc_group *groups, int elf_class)
{
	struct out_section *out;
	int *map;
	size_t count = 1;
	size_t i;
	int include;

	for (i = 0; i < as->section_count; ++i)
		++count;
	for (i = 0; i < as->section_count; ++i)
		if (groups[i].count != 0)
			++count;
	count += 3; /* symtab, strtab, shstrtab */
	out = (struct out_section *)calloc(count, sizeof(*out));
	map = (int *)mt_malloc(as->section_count * sizeof(*map));
	if (!out || (!map && as->section_count != 0)) {
		free(out);
		free(map);
		return -1;
	}
	for (i = 0; i < as->section_count; ++i) {
		map[i] = (int)(i + 1);
		out[i + 1].name = as->sections[i].name;
		out[i + 1].type = as->sections[i].type;
		out[i + 1].flags = as->sections[i].flags;
		out[i + 1].align = as->sections[i].align;
		out[i + 1].data = as->sections[i].data;
		out[i + 1].size = as->sections[i].size;
		out[i + 1].nobits = as->sections[i].type == MT_SHT_NOBITS;
	}
	include = (int)(as->section_count + 1);
	for (i = 0; i < as->section_count; ++i) {
		if (groups[i].count == 0)
			continue;
		out[include].name = NULL; /* assigned after section name is built */
		out[include].type = MT_SHT_RELA;
		out[include].flags = 0;
		out[include].align = 8;
		out[include].entry_size = (elf_class == 1) ? 12 : 24;
		out[include].info = (uint32_t)map[i];
		++include;
	}
	*out_count = count;
	*out_sections = out;
	*section_map = map;
	return 0;
}

static char *
reloc_section_name(const char *name)
{
	const char prefix[] = ".rela";
	size_t n = sizeof(prefix) - 1 + strlen(name) + 1;
	char *result = (char *)mt_malloc(n);
	if (!result)
		return NULL;
	strcpy(result, prefix);
	strcat(result, name);
	return result;
}

static int
build_symbols(struct as_file *as, const int *section_map, struct string_table *strtab,
              struct elf_sym_out **symbols_out, size_t *symbol_count,
              size_t *local_count)
{
	struct elf_sym_out *symbols;
	size_t count = 1;
	size_t locals = 1;
	size_t i;
	uint16_t shndx;
	uint32_t name_offset;

	if (string_init(strtab) != 0)
		return -1;
	/* Null symbol plus one local section symbol per input section. */
	count += as->section_count;
	locals += as->section_count;
	for (i = 0; i < as->symbol_count; ++i) {
		if (as->symbols[i].bind == MT_STB_LOCAL)
			++locals;
		++count;
	}
	symbols = (struct elf_sym_out *)calloc(count, sizeof(*symbols));
	if (!symbols)
		return -1;
	count = 1;
	for (i = 0; i < as->section_count; ++i) {
		symbols[count++] = (struct elf_sym_out){
			.info = MT_ST_INFO(MT_STB_LOCAL, MT_STT_SECTION),
			.section = (uint16_t)section_map[i]
		};
	}
	/* Reorder source symbols so all locals precede globals as ELF requires. */
	for (i = 0; i < as->symbol_count; ++i) {
		if (as->symbols[i].bind != MT_STB_LOCAL)
			continue;
		if (string_add(strtab, as->symbols[i].name, &name_offset) != 0)
			goto fail;
		shndx = as->symbols[i].defined && as->symbols[i].section >= 0 ?
		        (uint16_t)section_map[as->symbols[i].section] :
		        as->symbols[i].section == -2 ? MT_SHN_COMMON :
		        as->symbols[i].defined && as->symbols[i].section == -1 ? MT_SHN_ABS : 0;
		symbols[count] = (struct elf_sym_out){
			.name = name_offset,
			.info = MT_ST_INFO(as->symbols[i].bind, as->symbols[i].type),
			.other = (uint8_t)as->symbols[i].other,
			.section = shndx,
			.value = as->symbols[i].value,
			.size = as->symbols[i].size
		};
		as->symbols[i].output_index = (uint32_t)count++;
	}
	*local_count = count;
	for (i = 0; i < as->symbol_count; ++i) {
		if (as->symbols[i].bind == MT_STB_LOCAL)
			continue;
		if (string_add(strtab, as->symbols[i].name, &name_offset) != 0)
			goto fail;
		shndx = as->symbols[i].defined && as->symbols[i].section >= 0 ?
		        (uint16_t)section_map[as->symbols[i].section] :
		        as->symbols[i].section == -2 ? MT_SHN_COMMON :
		        as->symbols[i].defined && as->symbols[i].section == -1 ? MT_SHN_ABS : 0;
		symbols[count] = (struct elf_sym_out){
			.name = name_offset,
			.info = MT_ST_INFO(as->symbols[i].bind, as->symbols[i].type),
			.other = (uint8_t)as->symbols[i].other,
			.section = shndx,
			.value = as->symbols[i].value,
			.size = as->symbols[i].size
		};
		as->symbols[i].output_index = (uint32_t)count++;
	}
	*symbols_out = symbols;
	*symbol_count = count;
	*local_count = locals;
	return 0;
fail:
	free(symbols);
	return -1;
}

static int
resolve_fixups(struct as_file *as, const int *section_map,
               struct reloc_group *groups)
{
	size_t i;
	struct as_fixup *fix;
	struct as_symbol *symbol;
	struct as_section *section;
	int can_resolve;
	int64_t value;

	(void)section_map;
	for (i = 0; i < as->fixup_count; ++i) {
		fix = &as->fixups[i];
		section = &as->sections[fix->section];
		symbol = find_symbol(as, fix->symbol);
		if (!symbol)
			return as_error(as, "undefined internal symbol: %s", fix->symbol);
		/* Only plain "value patch" relocations can be folded at assembly
		 * time.  Every other relocation must go to the linker, which
		 * knows how to re-encode the instruction bits (e.g. ARM branch
		 * relocations need the offset stuffed into the 24-bit imm field;
		 * LoongArch/AArch64/RISC-V relocs are all >= 64).  The numeric
		 * constants collide across targets, so key off the machine. */
		{
			int is_x86 = as->target &&
			             (as->target->emachine == MT_EM_X86_64 ||
			              as->target->emachine == MT_EM_386);
			int is_arm = as->target &&
			             as->target->emachine == MT_EM_ARM;
			int raw_resolvable =
			    (is_x86 && (fix->type == 1 || fix->type == 2 ||
			                fix->type == 4)) ||
			    (is_arm && (fix->type == 2 || fix->type == 3));
			can_resolve = symbol->defined &&
			              symbol->section == fix->section &&
			              raw_resolvable;
		}
		if (can_resolve) {
			value = (int64_t)symbol->value + fix->addend -
			        (fix->type == MT_R_X86_64_PC32 ||
			         fix->type == MT_R_X86_64_PLT32 ?
			         (int64_t)fix->offset : 0);
			if (patch_le(as, section, fix->offset, fix->width,
			             (uint64_t)value) != 0)
				return -1;
			continue;
		}
		if (reloc_add(&groups[fix->section], fix->offset, fix->type,
		              symbol->output_index, fix->addend) != 0)
			return as_error(as, "out of memory");
	}
	return 0;
}

static int
write_u16(FILE *file, uint16_t value)
{
	unsigned char p[2] = {(unsigned char)value, (unsigned char)(value >> 8)};
	return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
}

static int
write_u32(FILE *file, uint32_t value)
{
	unsigned char p[4] = {(unsigned char)value, (unsigned char)(value >> 8),
	                      (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
	return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
}

static int
write_u64(FILE *file, uint64_t value)
{
	unsigned char p[8];
	unsigned i;
	for (i = 0; i < 8; ++i)
		p[i] = (unsigned char)(value >> (i * 8));
	return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
}

static int
write_zeros(FILE *file, uint64_t count)
{
	static const unsigned char zero[256];
	while (count != 0) {
		size_t n = count < sizeof(zero) ? (size_t)count : sizeof(zero);
		if (fwrite(zero, 1, n, file) != n)
			return -1;
		count -= n;
	}
	return 0;
}

static uint64_t
align_up_u64(uint64_t value, uint64_t align)
{
	uint64_t mask = align - 1;
	return (value + mask) & ~mask;
}

static int
write_elf_header(FILE *file, const struct mt_target *target,
                 uint64_t section_offset, uint16_t section_count,
                 uint16_t section_string_index)
{
	unsigned char ident[16] = {0x7f, 'E', 'L', 'F',
	                           target->elf_class, target->elf_endian, 1, 0};
	if (target->elf_class == 1) {
		/* ELF32 header (52 bytes) */
		if (fseek(file, 0, SEEK_SET) != 0 ||
		    fwrite(ident, 1, sizeof(ident), file) != sizeof(ident) ||
		    write_u16(file, 1) != 0 ||
		    write_u16(file, target->emachine) != 0 ||
		    write_u32(file, 1) != 0 ||
		    write_u32(file, 0) != 0 ||          /* entry */
		    write_u32(file, 0) != 0 ||          /* phoff */
		    write_u32(file, (uint32_t)section_offset) != 0 ||
		    write_u32(file, target->e_flags) != 0 ||
		    write_u16(file, target->ehdr_size) != 0 ||
		    write_u16(file, 0) != 0 ||          /* phentsize */
		    write_u16(file, 0) != 0 ||          /* phnum */
		    write_u16(file, target->shdr_size) != 0 ||
		    write_u16(file, section_count) != 0 ||
		    write_u16(file, section_string_index) != 0)
			return -1;
	} else {
		/* ELF64 header (64 bytes) */
		if (fseek(file, 0, SEEK_SET) != 0 ||
		    fwrite(ident, 1, sizeof(ident), file) != sizeof(ident) ||
		    write_u16(file, 1) != 0 ||
		    write_u16(file, target->emachine) != 0 ||
		    write_u32(file, 1) != 0 ||
		    write_u64(file, 0) != 0 ||
		    write_u64(file, 0) != 0 ||
		    write_u64(file, section_offset) != 0 ||
		    write_u32(file, target->e_flags) != 0 ||
		    write_u16(file, target->ehdr_size) != 0 ||
		    write_u16(file, 0) != 0 ||
		    write_u16(file, 0) != 0 ||
		    write_u16(file, target->shdr_size) != 0 ||
		    write_u16(file, section_count) != 0 ||
		    write_u16(file, section_string_index) != 0)
			return -1;
	}
	return 0;
}

static int
write_section_header(FILE *file, const struct out_section *section,
                     uint32_t name_offset, int elf_class)
{
	if (write_u32(file, name_offset) != 0 || write_u32(file, section->type) != 0)
		return -1;
	if (elf_class == 1) {
		/* ELF32: 40-byte section header */
		if (write_u32(file, (uint32_t)section->flags) != 0 ||
		    write_u32(file, 0) != 0 ||  /* addr */
		    write_u32(file, (uint32_t)section->file_offset) != 0 ||
		    write_u32(file, (uint32_t)section->size) != 0 ||
		    write_u32(file, section->link) != 0 ||
		    write_u32(file, section->info) != 0 ||
		    write_u32(file, section->align ? (uint32_t)section->align : 1) != 0 ||
		    write_u32(file, (uint32_t)section->entry_size) != 0)
			return -1;
	} else {
		/* ELF64: 64-byte section header */
		if (write_u64(file, section->flags) != 0 || write_u64(file, 0) != 0 ||
		    write_u64(file, section->file_offset) != 0 || write_u64(file, section->size) != 0 ||
		    write_u32(file, section->link) != 0 || write_u32(file, section->info) != 0 ||
		    write_u64(file, section->align ? section->align : 1) != 0 ||
		    write_u64(file, section->entry_size) != 0)
			return -1;
	}
	return 0;
}

static void
mem_u32(unsigned char **cursor, uint32_t value)
{
	(*cursor)[0] = (unsigned char)value;
	(*cursor)[1] = (unsigned char)(value >> 8);
	(*cursor)[2] = (unsigned char)(value >> 16);
	(*cursor)[3] = (unsigned char)(value >> 24);
	*cursor += 4;
}

static void
mem_u64(unsigned char **cursor, uint64_t value)
{
	unsigned i;
	for (i = 0; i < 8; ++i)
		(*cursor)[i] = (unsigned char)(value >> (i * 8));
	*cursor += 8;
}

static void
build_symbol_data(unsigned char *data, const struct elf_sym_out *symbols,
                  size_t count, int elf_class)
{
	unsigned char *cursor = data;
	size_t i;
	for (i = 0; i < count; ++i) {
		mem_u32(&cursor, symbols[i].name);
		if (elf_class == 1) {
			/* ELF32 symbol (16 bytes): name, value, size, info, other, shndx */
			mem_u32(&cursor, (uint32_t)symbols[i].value);
			mem_u32(&cursor, (uint32_t)symbols[i].size);
			*cursor++ = symbols[i].info;
			*cursor++ = symbols[i].other;
			*cursor++ = (unsigned char)symbols[i].section;
			*cursor++ = (unsigned char)(symbols[i].section >> 8);
		} else {
			/* ELF64 symbol (24 bytes): name, info, other, shndx, value, size */
			*cursor++ = symbols[i].info;
			*cursor++ = symbols[i].other;
			*cursor++ = (unsigned char)symbols[i].section;
			*cursor++ = (unsigned char)(symbols[i].section >> 8);
			mem_u64(&cursor, symbols[i].value);
			mem_u64(&cursor, symbols[i].size);
		}
	}
}

static void
build_reloc_data(unsigned char *data, const struct reloc_group *group,
                 int elf_class)
{
	unsigned char *cursor = data;
	size_t i;
	uint64_t info;
	for (i = 0; i < group->count; ++i) {
		info = ((uint64_t)group->items[i].symbol << 32) |
		       group->items[i].type;
		if (elf_class == 1) {
			/* ELF32 RELA entry (12 bytes): offset, info, addend.
			 * ELF32 r_info packs the symbol index in bits [31:8]
			 * and the type in the low 8 bits. */
			mem_u32(&cursor, (uint32_t)group->items[i].offset);
			mem_u32(&cursor,
			        ((uint32_t)group->items[i].symbol << 8) |
			        group->items[i].type);
			mem_u32(&cursor, (uint32_t)group->items[i].addend);
		} else {
			/* ELF64 RELA entry (24 bytes): offset, info, addend */
			mem_u64(&cursor, group->items[i].offset);
			mem_u64(&cursor, info);
			mem_u64(&cursor, (uint64_t)group->items[i].addend);
		}
	}
}

static void
free_reloc_groups(struct reloc_group *groups, size_t count)
{
	size_t i;
	for (i = 0; i < count; ++i)
		free(groups[i].items);
	free(groups);
}

static int
write_object(struct as_file *as, const struct mt_target *target,
             const char *output_path)
{
	struct reloc_group *groups = NULL;
	struct out_section *out = NULL;
	struct elf_sym_out *symbols = NULL;
	struct string_table strtab;
	struct string_table shstrtab;
	int *section_map = NULL;
	uint32_t *section_name_offsets = NULL;
	uint32_t *reloc_output_map = NULL;
	unsigned char *symtab_data = NULL;
	FILE *file = NULL;
	size_t out_count = 0;
	size_t symbol_count = 0;
	size_t local_count = 0;
	size_t symtab_index;
	size_t strtab_index;
	size_t shstrtab_index;
	size_t i, pos;
	uint32_t shstr_string_index;
	uint64_t offset;
	uint64_t section_offset;
	uint64_t section_size;
	uint64_t align;
	int result = -1;

	memset(&strtab, 0, sizeof(strtab));
	memset(&shstrtab, 0, sizeof(shstrtab));
	if (as->section_count > SIZE_MAX / sizeof(*groups))
		return as_error(as, "too many sections");
	groups = (struct reloc_group *)calloc(as->section_count, sizeof(*groups));
	section_map = (int *)mt_malloc(as->section_count * sizeof(*section_map));
	if (!groups || (!section_map && as->section_count != 0)) {
		as_error(as, "out of memory");
		goto out;
	}
	for (i = 0; i < as->section_count; ++i)
		section_map[i] = (int)i + 1;
	if (build_symbols(as, section_map, &strtab, &symbols,
	                  &symbol_count, &local_count) != 0) {
		as_error(as, "unable to build ELF symbol table");
		goto out;
	}
	if (resolve_fixups(as, section_map, groups) != 0)
		goto out;
	if (build_output_sections(as, &out, &out_count, &section_map, groups,
	                          target->elf_class) != 0) {
		as_error(as, "unable to build ELF section table");
		goto out;
	}
	if (out_count > UINT16_MAX) {
		as_error(as, "too many output sections");
		goto out;
	}
	free(reloc_output_map);
	reloc_output_map = (uint32_t *)calloc(as->section_count,
	                                      sizeof(*reloc_output_map));
	if (!reloc_output_map && as->section_count != 0) {
		as_error(as, "out of memory");
		goto out;
	}
	pos = as->section_count + 1;
	for (i = 0; i < as->section_count; ++i) {
		char *reloc_name;
		if (groups[i].count == 0)
			continue;
		reloc_output_map[i] = (uint32_t)pos;
		reloc_name = reloc_section_name(as->sections[i].name);
		if (!reloc_name) {
			as_error(as, "out of memory");
			goto out;
		}
		out[pos].name = reloc_name;
		++pos;
	}
	symtab_index = out_count - 3;
	strtab_index = out_count - 2;
	shstrtab_index = out_count - 1;
	for (i = as->section_count + 1; i < symtab_index; ++i)
		out[i].link = (uint32_t)symtab_index;
	out[symtab_index].name = ".symtab";
	out[symtab_index].type = MT_SHT_SYMTAB;
	out[symtab_index].flags = 0;
	out[symtab_index].align = 8;
	out[symtab_index].entry_size = (target->elf_class == 1) ? 16 : 24;
	out[symtab_index].link = (uint32_t)strtab_index;
	out[symtab_index].info = (uint32_t)local_count;
	out[strtab_index].name = ".strtab";
	out[strtab_index].type = MT_SHT_STRTAB;
	out[strtab_index].align = 1;
	out[shstrtab_index].name = ".shstrtab";
	out[shstrtab_index].type = MT_SHT_STRTAB;
	out[shstrtab_index].align = 1;
	if (string_init(&shstrtab) != 0)
		goto out;
	section_name_offsets = (uint32_t *)calloc(out_count, sizeof(*section_name_offsets));
	if (!section_name_offsets) {
		as_error(as, "out of memory");
		goto out;
	}
	for (i = 1; i < out_count; ++i) {
		if (!out[i].name || string_add(&shstrtab, out[i].name,
		                               &section_name_offsets[i]) != 0) {
			as_error(as, "unable to build section name table");
			goto out;
		}
	}
	if (string_add(&shstrtab, ".shstrtab", &shstr_string_index) != 0)
		goto out;
	(void)shstr_string_index;
	out[symtab_index].size = symbol_count * ((target->elf_class == 1) ? 16 : 24);
	out[symtab_index].data = (unsigned char *)mt_malloc(out[symtab_index].size);
	if (!out[symtab_index].data)
		goto out;
	symtab_data = out[symtab_index].data;
	build_symbol_data(symtab_data, symbols, symbol_count, target->elf_class);
	out[strtab_index].data = strtab.data;
	out[strtab_index].size = strtab.size;
	strtab.data = NULL;
	out[shstrtab_index].data = shstrtab.data;
	out[shstrtab_index].size = shstrtab.size;
	shstrtab.data = NULL;
	for (i = 0; i < as->section_count; ++i) {
		if (groups[i].count == 0)
			continue;
		section_size = groups[i].count *
		               ((target->elf_class == 1) ? 12 : 24);
		out[reloc_output_map[i]].data =
		    (unsigned char *)mt_malloc((size_t)section_size);
		if (!out[reloc_output_map[i]].data)
			goto out;
		out[reloc_output_map[i]].size = section_size;
		build_reloc_data(out[reloc_output_map[i]].data, &groups[i],
		                 target->elf_class);
	}
	/* Assign file offsets. NOBITS reserves virtual size but no file bytes. */
	offset = target->ehdr_size;
	for (i = 1; i < out_count; ++i) {
		align = out[i].align ? out[i].align : 1;
		offset = align_up_u64(offset, align);
		out[i].file_offset = offset;
		if (!out[i].nobits) {
			if (out[i].size > UINT64_MAX - offset)
				goto out;
			offset += out[i].size;
		}
	}
	section_offset = align_up_u64(offset, 8);
	file = fopen(output_path, "wb+");
	if (!file) {
		as_error(as, "cannot open output %s: %s", output_path, strerror(errno));
		goto out;
	}
	if (write_elf_header(file, target, section_offset, (uint16_t)out_count,
	                     (uint16_t)shstrtab_index) != 0)
		goto out;
	for (i = 1; i < out_count; ++i) {
		if (out[i].nobits || out[i].size == 0)
			continue;
		if (fseek(file, (long)out[i].file_offset, SEEK_SET) != 0 ||
		    fwrite(out[i].data, 1, (size_t)out[i].size, file) != out[i].size)
			goto out;
	}
	if (fseek(file, (long)section_offset, SEEK_SET) != 0 ||
	    write_zeros(file, target->shdr_size) != 0)
		goto out;
	for (i = 1; i < out_count; ++i)
		if (write_section_header(file, &out[i], section_name_offsets[i], target->elf_class) != 0)
			goto out;
	if (fclose(file) != 0) {
		file = NULL;
		goto out;
	}
	file = NULL;
	result = 0;
out:
	if (file)
		fclose(file);
	free(symtab_data);
	free(symbols);
	free(section_name_offsets);
	free(reloc_output_map);
	free(section_map);
	if (out) {
		for (i = as->section_count + 1; i < out_count - 3; ++i)
			free((char *)out[i].name);
		free(out[out_count - 2].data);
		free(out[out_count - 1].data);
		free(out);
	}
	free(strtab.data);
	free(shstrtab.data);
	free_reloc_groups(groups, as->section_count);
	return result;
}

/* --- DWARF helpers (write via as_append_bytes) --- */

static int
dwarf_uleb128(struct as_file *as, struct as_section *sec, uint64_t value)
{
	unsigned char b;
	do {
		b = value & 0x7f;
		value >>= 7;
		if (value) b |= 0x80;
		if (as_append_bytes(as, sec, &b, 1) != 0)
			return -1;
	} while (value);
	return 0;
}

static int
dwarf_u32(struct as_file *as, struct as_section *sec, uint32_t v)
{
	unsigned char buf[4];
	buf[0] = (unsigned char)v;
	buf[1] = (unsigned char)(v >> 8);
	buf[2] = (unsigned char)(v >> 16);
	buf[3] = (unsigned char)(v >> 24);
	return as_append_bytes(as, sec, buf, 4);
}

static int
dwarf_u8(struct as_file *as, struct as_section *sec, unsigned char v)
{
	return as_append_bytes(as, sec, &v, 1);
}

static int
dwarf_string(struct as_file *as, struct as_section *sec, const char *s)
{
	return as_append_bytes(as, sec, s, strlen(s) + 1);
}

/* Build .debug_line (DWARF v4) and .eh_frame from buffered data. */
static int
emit_dwarf(struct as_file *as)
{
	size_t i;
	int sec_idx;
	struct as_section *dl;
	struct as_section *eh;
	uint64_t prev_offset, prev_line, header_length, total_length;

	/* ---- .debug_line ---- */
	if (as->dwarf_loc_count == 0)
		goto eh_frame;

	sec_idx = get_section(as, ".debug_line");
	if (sec_idx < 0)
		return -1;
	dl = &as->sections[sec_idx];

	/* Phase 1: build the line number program into a temp buffer */
	{
		unsigned char *prog = (unsigned char *)mt_malloc(4096);
		size_t prog_cap = 4096, prog_size = 0;
		prev_offset = 0;
		prev_line = 1;
		for (i = 0; i < as->dwarf_loc_count; ++i) {
			struct as_dwarf_loc *loc = &as->dwarf_locs[i];
			uint64_t delta = loc->offset - prev_offset;
			unsigned char b;
			if (loc->offset > prev_offset) {
				/* DW_LNS_advance_pc (opcode 2) */
				b = 2;
				if (prog_size + 8 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
				prog[prog_size++] = b;
				/* ULEB128 delta */
				do {
					b = delta & 0x7f; delta >>= 7;
					if (delta) b |= 0x80;
					if (prog_size + 1 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
					prog[prog_size++] = b;
				} while (delta);
			}
			/* DW_LNS_set_file (4) */
			b = 4;
			if (prog_size + 8 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
			prog[prog_size++] = b;
			{ uint64_t v = loc->file; do { b = v & 0x7f; v >>= 7; if (v) b |= 0x80; prog[prog_size++] = b; } while (v); }
			/* DW_LNS_advance_line (3) — takes signed delta; for positive, ULEB128 works */
			if (prog_size + 8 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
			prog[prog_size++] = 3;
			{ uint64_t v = loc->line - prev_line; 
			  do { b = v & 0x7f; v >>= 7; if (v) b |= 0x80; prog[prog_size++] = b; } while (v); }
			prev_line = loc->line;
			if (loc->column != 0) {
				if (prog_size + 8 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
				prog[prog_size++] = 5; /* DW_LNS_set_column */
				{ uint64_t v = loc->column; do { b = v & 0x7f; v >>= 7; if (v) b |= 0x80; prog[prog_size++] = b; } while (v); }
			}
			/* DW_LNS_copy (1) */
			if (prog_size + 1 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
			prog[prog_size++] = 1;
			prev_offset = loc->offset;
		}

		/* Phase 2: compute header_length in DWARF 2/3 format */
		header_length = 1 + 1 + 1 + 1 + 1 + 12;  /* min_instr..std_op_len (no max_ops) */
		header_length += 1; /* include_directories terminator */
		for (i = 0; i < as->dwarf_file_count; ++i) {
			header_length += (uint64_t)(strlen(as->dwarf_files[i].name) + 1);
			header_length += 1 + 1 + 1; /* dir=0, time=0, size=0 */
		}
		header_length += 1; /* file_names terminator */

		/* total_length = version(2) + header_length(4) + header_length + prog_size,
		 * but note: in DWARF 2/3, total_length does NOT include unit_length(4) itself */
		total_length = 2 + 4 + header_length + prog_size;

		/* Phase 3: emit header */
		{
			unsigned char hdr[512];
			size_t hpos = 0;
			unsigned char ul[4] = { (unsigned char)total_length, (unsigned char)(total_length >> 8),
				(unsigned char)(total_length >> 16), (unsigned char)(total_length >> 24) };
			memcpy(hdr + hpos, ul, 4); hpos += 4;
			hdr[hpos++] = 2; hdr[hpos++] = 0; /* version 2 */
			{ unsigned char hl[4] = { (unsigned char)header_length, (unsigned char)(header_length >> 8),
				(unsigned char)(header_length >> 16), (unsigned char)(header_length >> 24) };
				memcpy(hdr + hpos, hl, 4); hpos += 4; }
			hdr[hpos++] = 1; /* min_inst_len */
			hdr[hpos++] = 1; /* default_is_stmt */
			hdr[hpos++] = 251; /* line_base = -5 */
			hdr[hpos++] = 14; /* line_range */
			hdr[hpos++] = 13; /* opcode_base */
			hdr[hpos++] = 0; hdr[hpos++] = 1; hdr[hpos++] = 1; hdr[hpos++] = 1;
			hdr[hpos++] = 1; hdr[hpos++] = 0; hdr[hpos++] = 0; hdr[hpos++] = 0;
			hdr[hpos++] = 1; hdr[hpos++] = 0; hdr[hpos++] = 0; hdr[hpos++] = 1;
			/* (was DWARF 4 max_ops; now DWARF 2/3 without it) */
			hdr[hpos++] = 0; /* include_directories terminator */
			for (i = 0; i < as->dwarf_file_count; ++i) {
				const char *fn = as->dwarf_files[i].name;
				size_t flen = strlen(fn);
				memcpy(hdr + hpos, fn, flen + 1); hpos += flen + 1;
				hdr[hpos++] = 0; hdr[hpos++] = 0; hdr[hpos++] = 0;
			}
			hdr[hpos++] = 0; /* file_names terminator */
			as_append_bytes(as, dl, hdr, hpos);
		}
		/* Phase 4: emit program */
		as_append_bytes(as, dl, prog, prog_size);
		free(prog);
	}

eh_frame:
	/* ---- .eh_frame ---- */
	if (as->cfi_fde_count == 0)
		return 0;

	sec_idx = get_section(as, ".eh_frame");
	if (sec_idx < 0)
		return -1;
	eh = &as->sections[sec_idx];

	/* ---- CIE ---- */
	{   uint32_t cie_offset = (uint32_t)eh->size;
		uint32_t cie_len_pos = (uint32_t)eh->size;
		unsigned char pad = 0;

		dwarf_u32(as, eh, 0);  /* placeholder length */
		dwarf_u32(as, eh, 0);  /* CIE id */
		dwarf_u8(as, eh, 1);   /* version */
		dwarf_string(as, eh, "zR"); /* augmentation */
		dwarf_uleb128(as, eh, 1);   /* code alignment */
		dwarf_uleb128(as, eh, 1);   /* data alignment */
		dwarf_u8(as, eh, 16);       /* return address reg (x86_64) */
		dwarf_uleb128(as, eh, 1);   /* augmentation data length */
		dwarf_u8(as, eh, 0x00);     /* FDE encoding: absolute */
		dwarf_u8(as, eh, 0x00);     /* DW_CFA_nop */
		while (eh->size % 4 != 0)
			as_append_bytes(as, eh, &pad, 1);
		{ uint32_t cie_len = (uint32_t)(eh->size - cie_len_pos - 4);
			eh->data[cie_len_pos] = (unsigned char)cie_len;
			eh->data[cie_len_pos + 1] = (unsigned char)(cie_len >> 8);
			eh->data[cie_len_pos + 2] = (unsigned char)(cie_len >> 16);
			eh->data[cie_len_pos + 3] = (unsigned char)(cie_len >> 24);
		}

		/* ---- FDEs ---- */
		for (i = 0; i < as->cfi_fde_count; ++i) {
			uint32_t fde_len_pos = (uint32_t)eh->size;
			uint64_t fde_initial_loc_pos, func_size;
			const char *label = as->cfi_func_labels[i];

			dwarf_u32(as, eh, 0); /* FDE length placeholder */
			dwarf_u32(as, eh, (uint32_t)((uint64_t)cie_offset)); /* CIE pointer */
			fde_initial_loc_pos = eh->size;
			dwarf_u32(as, eh, (uint32_t)as->cfi_func_offsets[i]);
			func_size = as->cfi_func_end[i] - as->cfi_func_offsets[i];
			dwarf_u32(as, eh, (uint32_t)func_size);
			if (as->cfi_fde_sizes[i] > 0)
				as_append_bytes(as, eh, as->cfi_fde_progs[i], as->cfi_fde_sizes[i]);
			dwarf_u8(as, eh, 0x00);
			while (eh->size % 4 != 0)
				as_append_bytes(as, eh, &pad, 1);
			{ uint32_t fde_len = (uint32_t)(eh->size - fde_len_pos - 4);
				eh->data[fde_len_pos] = (unsigned char)fde_len;
				eh->data[fde_len_pos + 1] = (unsigned char)(fde_len >> 8);
				eh->data[fde_len_pos + 2] = (unsigned char)(fde_len >> 16);
				eh->data[fde_len_pos + 3] = (unsigned char)(fde_len >> 24);
			}
			if (label && label[0] != '\0')
				as_add_fixup(as, eh, (size_t)fde_initial_loc_pos, 4,
				             MT_R_X86_64_32, 0, label);
		}
		/* terminator: length = 0 */
		dwarf_u32(as, eh, 0);
	}
	return 0;
}

int
mt_as_assemble(const char *input, const char *output,
               const char *target_name, const char *march,
               const char **error_message, unsigned *error_line)
{
	struct as_file as;
	FILE *file;
	const struct mt_target *target;
	struct mt_target target_buf;
	int result;

	if (target_name) {
		target = mt_target_lookup(target_name);
		if (!target) {
			if (error_message)
				*error_message = "unsupported target architecture";
			return 1;
		}
	} else {
		target = &mt_target_x86_64;
	}

	/* Apply -march= by cloning the base target with an overridden feature
	 * bitmask.  The clone is stack-local so the shared static is untouched. */
	if (march) {
		uint64_t f = mt_target_features_for_march(target->name, march);
		target_buf = mt_target_clone_with_features(target, f);
		target = &target_buf;
	}

	memset(&as, 0, sizeof(as));
	as.target = target;
	as.filename = input;
	as.line = 0;
	file = fopen(input, "r");
	if (!file) {
		strncpy(as.error, strerror(errno), sizeof(as.error) - 1);
		as.error[sizeof(as.error) - 1] = '\0';
		as.line = 1;
		result = -1;
	} else {
		result = parse_source(&as, file);
		fclose(file);
		if (result == 0)
			result = emit_dwarf(&as);
		if (result == 0)
			result = write_object(&as, target, output);
	}
	if (result != 0) {
		static char reported_error[256];
		if (!as.error[0])
			strcpy(as.error, "assembly failed");
		strncpy(reported_error, as.error, sizeof(reported_error) - 1);
		reported_error[sizeof(reported_error) - 1] = '\0';
		if (error_message)
			*error_message = reported_error;
		if (error_line)
			*error_line = as.line ? as.line : 1;
	}
	free_as(&as);
	return result;
}
