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

void *
mt_malloc(size_t size)
{
	/* malloc(0) is implementation-defined (may return NULL or a unique ptr).
	 * We guarantee a minimum 1-byte allocation to avoid ambiguity. */
	void *p = malloc(size == 0 ? 1 : size);
	return p;
}

void *
mt_realloc(void *old, size_t size)
{
	return realloc(old, size == 0 ? 1 : size);
}

char *
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

char *
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

int
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

int
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

int
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

struct as_symbol *
find_symbol(struct as_file *as, const char *name)
{
	size_t i;
	for (i = 0; i < as->symbol_count; ++i)
		if (strcmp(as->symbols[i].name, name) == 0)
			return &as->symbols[i];
	return NULL;
}

struct as_symbol *
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

int
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

int
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
	for (end = buffer + 1; *end; ++end) {
		int sci_e = 0;
		if (end[-1] == 'e' || end[-1] == 'E')
			if (end > buffer + 1 && isdigit((unsigned char)end[-2]))
				sci_e = 1;  /* 1e+5: '+' follows a scientific mantissa */
		if ((*end == '+' || *end == '-') && !sci_e) {
			split = end;
			break;
		}
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
