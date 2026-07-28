/* encode.c — x86_64 instruction encoder.

 * Encodes AT&T-syntax assembly instructions into x86_64 machine code.
 * Used by the assembler via the mt_target.encode_insn dispatch mechanism.
 */

#include "mt/target.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

/* ---- Relocation type constants (matching <mt/elf.h> values) ---- */
#define R_X86_64_PC32     2
#define R_X86_64_PLT32    4
#define R_X86_64_GOTPCREL 9

/* ---- Operand model ---- */

#define OP_INVALID 0
#define OP_IMM     1
#define OP_REG     2
#define OP_MEM     3
#define OP_SYMBOL  4

struct x86_op {
	int kind;
	int reg;
	int width;
	int base;
	int index;
	int scale;
	int64_t displacement;
	char *symbol;
	char modifier[16];
	int64_t addend;
};

/* ---- Helpers --------------------------------------------------------- */

static void
x86_free_op(struct x86_op *op)
{
	free(op->symbol);
	op->symbol = NULL;
}

static void
emit_u8(unsigned char *buf, size_t *size, unsigned v)
{
	buf[(*size)++] = (unsigned char)v;
}

static void
emit_le(unsigned char *buf, size_t *size, uint64_t v, unsigned w)
{
	unsigned i;
	for (i = 0; i < w; ++i)
		buf[(*size)++] = (unsigned char)(v >> (i * 8));
}

static char *
x86_strdup(const char *s)
{
	size_t n = strlen(s);
	char *p = (char *)malloc(n + 1);
	if (p) memcpy(p, s, n + 1);
	return p;
}

static void
set_fixup(struct mt_insn *out, size_t offset, unsigned width,
          unsigned reloc_type, const char *sym, int64_t addend)
{
	out->fixed = 0;
	out->fixup_offset = offset;
	out->fixup_width = width;
	out->reloc_type = reloc_type;
	out->fixup_symbol = sym ? x86_strdup(sym) : NULL;
	out->fixup_addend = addend;
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
	if (errno == ERANGE || *end != '\0')
		return -1;
	*value = (int64_t)result;
	return 0;
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

static int
normalize_numeric_reference(char **symbol)
{
	char *p = *symbol;
	if (!p) return 0;
	if ((p[0] == '-' || (p[0] >= '0' && p[0] <= '9')) &&
	    !strchr(p, '@') && !strchr(p, '(') && !strchr(p, ')')) {
		free(p);
		*symbol = NULL;
	}
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
	if (buffer[0] == '"' && buffer[strlen(buffer) - 1] == '"' &&
	    strlen(buffer) >= 2) {
		size_t len = strlen(buffer);
		memmove(buffer, buffer + 1, len - 2);
		buffer[len - 2] = '\0';
	}
	*symbol = x86_strdup(buffer);
	return *symbol ? 0 : -1;
}

static int
parse_register(const char *text, int *reg, int *width)
{
	static const char *const names[16][4] = {
		{"rax", "eax", "ax", "al"}, {"rcx", "ecx", "cx", "cl"},
		{"rdx", "edx", "dx", "dl"}, {"rbx", "ebx", "bx", "bl"},
		{"rsp", "esp", "sp", "spl"}, {"rbp", "ebp", "bp", "bpl"},
		{"rsi", "esi", "si", "sil"}, {"rdi", "edi", "di", "dil"},
		{"r8", "r8d", "r8w", "r8b"}, {"r9", "r9d", "r9w", "r9b"},
		{"r10", "r10d", "r10w", "r10b"}, {"r11", "r11d", "r11w", "r11b"},
		{"r12", "r12d", "r12w", "r12b"}, {"r13", "r13d", "r13w", "r13b"},
		{"r14", "r14d", "r14w", "r14b"}, {"r15", "r15d", "r15w", "r15b"}
	};
	int i, j;
	char *copy;

	if (!text || text[0] != '%')
		return -1;
	copy = x86_strdup(text + 1);
	if (!copy)
		return -1;
	for (i = 0; i < 16; ++i)
		for (j = 0; j < 4; ++j)
			if (strcmp(copy, names[i][j]) == 0) {
				*reg = i;
				*width = j == 0 ? 8 : j == 1 ? 4 : j == 2 ? 2 : 1;
				free(copy);
				return 0;
			}
	/* XMM0-XMM15: reg=0..15, width=16 (distinguishes from GPRs) */
	if (copy[0] == 'x' && copy[1] == 'm' && copy[2] == 'm' &&
	    copy[3] >= '0' && copy[3] <= '9' && copy[4] == '\0') {
		*reg = copy[3] - '0';
		*width = 16;
		free(copy);
		return 0;
	}
	if (copy[0] == 'x' && copy[1] == 'm' && copy[2] == 'm' &&
	    copy[3] == '1' && copy[4] >= '0' && copy[4] <= '5' &&
	    copy[5] == '\0') {
		*reg = 10 + (copy[4] - '0');
		*width = 16;
		free(copy);
		return 0;
	}
	/* YMM0-YMM15: reg=0..15, width=32 (distinguishes from XMM=16) */
	if (copy[0] == 'y' && copy[1] == 'm' && copy[2] == 'm' &&
	    copy[3] >= '0' && copy[3] <= '9' && copy[4] == '\0') {
		*reg = copy[3] - '0';
		*width = 32;
		free(copy);
		return 0;
	}
	if (copy[0] == 'y' && copy[1] == 'm' && copy[2] == 'm' &&
	    copy[3] == '1' && copy[4] >= '0' && copy[4] <= '5' &&
	    copy[5] == '\0') {
		*reg = 10 + (copy[4] - '0');
		*width = 32;
		free(copy);
		return 0;
	}
	free(copy);
	return -1;
}

static int
split_operands(char *text, char *operands[4])
{
	int count = 0;
	int paren = 0;
	char *start = text;
	char *p;
	for (p = text;; ++p) {
		if (*p == '(')
			++paren;
		else if (*p == ')')
			--paren;
		if ((*p == ',' && paren == 0) || *p == '\0') {
			if (count >= 4)
				return -1;
			operands[count++] = trim(start);
			if (*p == '\0')
				break;
			*p = '\0';
			start = p + 1;
		}
	}
	return count;
}

static int
parse_operand(char *text, struct x86_op *op)
{
	char *open;
	char *close;
	char *prefix;
	char *inside;
	char *symbol;
	int is_number;

	memset(op, 0, sizeof(*op));
	op->base = -1;
	op->index = -1;
	op->scale = 1;
	text = trim(text);
	if (!*text)
		return -1;
	if (text[0] == '$') {
		if (parse_reference(text + 1, &symbol, op->modifier,
		                    sizeof(op->modifier), &op->addend,
		                    &is_number) != 0)
			return -1;
		op->kind = is_number ? OP_IMM : OP_SYMBOL;
		op->displacement = op->addend;
		op->symbol = symbol;
		if (op->symbol && normalize_numeric_reference(&op->symbol) != 0)
			return -1;
		return 0;
	}
	if (text[0] == '%' && parse_register(text, &op->reg, &op->width) == 0) {
		op->kind = OP_REG;
		return 0;
	}
	open = strchr(text, '(');
	if (open) {
		close = strrchr(open, ')');
		if (!close || close[1] != '\0')
			return -1;
		*open = '\0';
		*close = '\0';
		prefix = trim(text);
		inside = trim(open + 1);
		{
			char *parts[4];
			int part_count = split_operands(inside, parts);
			if (part_count == 1 && strcmp(parts[0], "%rip") == 0) {
				op->base = -2;
			} else if (part_count >= 1 && part_count <= 3 &&
			           parse_register(parts[0], &op->base, &op->width) == 0) {
				if (part_count >= 2 && *parts[1] &&
				    parse_register(parts[1], &op->index, &op->width) != 0)
					return -1;
				if (part_count == 3 && parse_integer(parts[2], &op->displacement) != 0)
					return -1;
				if (part_count == 3 && op->displacement != 1 &&
				    op->displacement != 2 && op->displacement != 4 &&
				    op->displacement != 8)
					return -1;
				if (part_count == 3) {
					op->scale = (int)op->displacement;
					op->displacement = 0;
				}
			} else {
				return -1;
			}
		}
		if (*prefix) {
			if (parse_reference(prefix, &symbol, op->modifier,
			                    sizeof(op->modifier), &op->addend,
			                    &is_number) != 0)
				return -1;
			if (is_number)
				op->displacement = op->addend;
			else {
				op->symbol = symbol;
				if (normalize_numeric_reference(&op->symbol) != 0)
					return -1;
			}
		}
		op->kind = OP_MEM;
		return 0;
	}
	if (parse_reference(text, &symbol, op->modifier, sizeof(op->modifier),
	                    &op->addend, &is_number) != 0)
		return -1;
	if (is_number) {
		op->kind = OP_IMM;
		op->displacement = op->addend;
	} else {
		op->kind = OP_SYMBOL;
		op->symbol = symbol;
		if (normalize_numeric_reference(&op->symbol) != 0)
			return -1;
	}
	return 0;
}

/* ---- Encoding helpers ------------------------------------------------- */

static void
emit_rex(unsigned char *buf, size_t *size, int w, int r, int b)
{
	if (w || r >= 8 || b >= 8)
		emit_u8(buf, size, 0x40 | (w ? 8 : 0) |
		        (r >= 8 ? 4 : 0) | (b >= 8 ? 1 : 0));
}

static void
emit_byte_rex(unsigned char *buf, size_t *size, int reg, int rm)
{
	if ((reg >= 4 && reg < 8) || (rm >= 4 && rm < 8))
		emit_u8(buf, size, 0x40);
}

static void
emit_modrm(unsigned char *buf, size_t *size, struct mt_insn *out,
           int reg, const struct x86_op *rm, unsigned fix_type,
           int64_t fix_addend)
{
	unsigned modrm;
	int base;
	int mod;
	int64_t disp;
	size_t fix_offset;

	if (rm->kind == OP_REG) {
		modrm = 0xc0 | ((unsigned)reg & 7) << 3 | ((unsigned)rm->reg & 7);
		emit_u8(buf, size, modrm);
		return;
	}
	if (rm->kind != OP_MEM)
		return;  /* caller should not reach here */
	base = rm->base;
	disp = rm->displacement;
	if (base == -2) {
		modrm = ((unsigned)reg & 7) << 3 | 5;
		emit_u8(buf, size, modrm);
		fix_offset = *size;
		if (rm->symbol) {
			emit_le(buf, size, 0, 4);
			set_fixup(out, fix_offset, 4, fix_type, rm->symbol, fix_addend);
		} else {
			emit_le(buf, size, (uint32_t)disp, 4);
		}
		return;
	}
	if (base < 0 || base >= 16)
		return;
	if (rm->index >= 8)
		return;
	if (!rm->symbol && disp == 0 && base != 5 && base != 13)
		mod = 0;
	else if (disp >= -128 && disp <= 127)
		mod = 1;
	else
		mod = 2;
	if (rm->index >= 0 || base == 4 || base == 12) {
		unsigned scale = rm->scale == 8 ? 3 : rm->scale == 4 ? 2 :
		                 rm->scale == 2 ? 1 : 0;
		unsigned index = rm->index >= 0 ? (unsigned)rm->index : 4;
		modrm = ((unsigned)mod << 6) | ((unsigned)reg & 7) << 3 | 4;
		emit_u8(buf, size, modrm);
		emit_u8(buf, size, (scale << 6) | (index << 3) |
		        ((unsigned)base & 7));
	} else {
		modrm = ((unsigned)mod << 6) | ((unsigned)reg & 7) << 3 |
		        ((unsigned)base & 7);
		emit_u8(buf, size, modrm);
	}
	if (mod == 1)
		emit_le(buf, size, (uint8_t)disp, 1);
	else if (mod == 2 || (mod == 0 && (base == 5 || base == 13)))
		emit_le(buf, size, (uint32_t)disp, 4);
}

static void
emit_rm_reg(unsigned char *buf, size_t *size, struct mt_insn *out,
            unsigned opcode, int width, const struct x86_op *source,
            const struct x86_op *destination)
{
	int reg;
	const struct x86_op *rm;
	unsigned fix_type = R_X86_64_PC32;

	if (source->kind == OP_REG) {
		reg = source->reg;
		rm = destination;
	} else if (destination->kind == OP_REG) {
		reg = destination->reg;
		rm = source;
	} else {
		return;
	}
	if (rm->kind == OP_MEM && rm->symbol) {
		if (strcmp(rm->modifier, "gotpcrel") == 0)
			fix_type = R_X86_64_GOTPCREL;
		else if (rm->base != -2)
			return;
	}
	if (width == 1)
		emit_byte_rex(buf, size, reg,
		              rm->kind == OP_REG ? rm->reg : -1);
	emit_rex(buf, size, width == 8, reg,
	          rm->kind == OP_REG ? rm->reg : rm->base);
	emit_u8(buf, size, opcode);
	emit_modrm(buf, size, out, reg, rm, fix_type, rm->addend - 4);
}

static void
emit_binary_immediate(unsigned char *buf, size_t *size, struct mt_insn *out,
                      unsigned ext, int width, const struct x86_op *src,
                      const struct x86_op *dst)
{
	if (src->kind != OP_IMM ||
	    (dst->kind != OP_REG && dst->kind != OP_MEM))
		return;
	if (width == 1)
		emit_byte_rex(buf, size, 0,
		              dst->kind == OP_REG ? dst->reg : -1);
	emit_rex(buf, size, width == 8, 0,
	          dst->kind == OP_REG ? dst->reg : dst->base);
	emit_u8(buf, size, width == 1 ? 0x80 : 0x81);
	emit_modrm(buf, size, out, ext, dst, R_X86_64_PC32, -4);
	emit_le(buf, size, (uint32_t)src->displacement, width == 1 ? 1 : 4);
}

static int
condition_code(const char *suffix)
{
	static const struct { const char *name; int code; } table[] = {
		{"a", 0x7}, {"ae", 0x3}, {"b", 0x2}, {"be", 0x6},
		{"c", 0x2}, {"e", 0x4}, {"eq", 0x4}, {"g", 0xf},
		{"ge", 0xd}, {"l", 0xc}, {"le", 0xe}, {"na", 0x6},
		{"nae", 0x2}, {"ne", 0x5}, {"neq", 0x5}, {"nz", 0x5}, {"nge", 0xc},
		{"ng", 0xe}, {"no", 0x1}, {"np", 0xb}, {"ns", 0x9},
		{"o", 0x0}, {"p", 0xa}, {"pe", 0xa}, {"po", 0xb},
		{"s", 0x8}, {"z", 0x4}
	};
	size_t i;
	for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
		if (strcmp(suffix, table[i].name) == 0)
			return table[i].code;
	return -1;
}

static void
emit_symbol_branch(unsigned char *buf, size_t *size, struct mt_insn *out,
                   unsigned opcode, const struct x86_op *target,
                   unsigned reloc_type)
{
	size_t offset;
	if (target->kind != OP_SYMBOL)
		return;
	emit_u8(buf, size, opcode);
	offset = *size;
	emit_le(buf, size, 0, 4);
	set_fixup(out, offset, 4, reloc_type, target->symbol, -4);
}

static int
is_xmm(const struct x86_op *op)
{
	return op->kind == OP_REG && op->width == 16;
}

static int
is_ymm(const struct x86_op *op)
{
	return op->kind == OP_REG && op->width == 32;
}

/* Emit a 2-byte VEX prefix (VEX.128/256).  For 2-byte VEX, the pp field
 * selects the mandatory SSE prefix equivalent (0=none, 1=0x66, 2=0xF3,
 * 3=0xF2), L selects between 128/256-bit, and the R/v fields carry the
 * inverted REX.R and inverted register-selector (vvvv).  When vvvv is
 * unused (no source register), pass -1. */
static void
emit_vex(unsigned char *buf, size_t *size,
         int need_256, int pp,
         int reg_num, int vvvv_in)
{
	unsigned vex_byte;
	int vvvv = (vvvv_in < 0) ? 0xf : (vvvv_in ^ 0xf);  /* invert */
	unsigned R = (reg_num < 8) ? 1 : 0;                 /* inverted REX.R */
	unsigned L = need_256 ? 1 : 0;
	vex_byte = (R << 7) | ((vvvv & 0xf) << 3) | (L << 2) | (pp & 3);
	emit_u8(buf, size, 0xC5);
	emit_u8(buf, size, vex_byte);
}

static void
emit_sse(unsigned char *buf, size_t *size, struct mt_insn *out,
         unsigned mandatory_prefix, unsigned opcode2,
         int reg_num, const struct x86_op *rm,
         int need_w, unsigned fix_type, int64_t fix_addend)
{
	int rm_num = (rm->kind == OP_REG) ? rm->reg : rm->base;
	unsigned char rex = 0;

	if (mandatory_prefix)
		emit_u8(buf, size, mandatory_prefix);
	if (need_w) rex |= 0x48;
	if (reg_num >= 8) rex |= 0x44;
	if (rm_num >= 8) rex |= 0x41;
	if (rex)
		emit_u8(buf, size, rex);
	emit_u8(buf, size, 0x0F);
	emit_u8(buf, size, opcode2);
	emit_modrm(buf, size, out, reg_num, rm, fix_type, fix_addend);
}

struct sse_entry { const char *name; unsigned pfx; unsigned op; };

static int
sse_arithmetic_lookup(const char *base, unsigned *prefix, unsigned *opcode2)
{
	static const struct sse_entry table[] = {
		{"addss", 0xF3, 0x58}, {"addsd", 0xF2, 0x58},
		{"subss", 0xF3, 0x5C}, {"subsd", 0xF2, 0x5C},
		{"mulss", 0xF3, 0x59}, {"mulsd", 0xF2, 0x59},
		{"divss", 0xF3, 0x5E}, {"divsd", 0xF2, 0x5E},
		{"sqrtss", 0xF3, 0x51}, {"sqrtsd", 0xF2, 0x51},
		{"minss", 0xF3, 0x5D}, {"minsd", 0xF2, 0x5D},
		{"maxss", 0xF3, 0x5F}, {"maxsd", 0xF2, 0x5F},
	};
	size_t i;
	for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
		if (strcmp(base, table[i].name) == 0) {
			*prefix = table[i].pfx;
			*opcode2 = table[i].op;
			return 0;
		}
	return -1;
}

/* ---- Main encoder ---------------------------------------------------- */

int
x86_64_encode_insn(const struct mt_target *target,
                   const char *mnemonic, const char *operands,
                   struct mt_insn *out)
{
	struct x86_op op[4];
	char *ops[4];
	char base[32];
	int n;
	int width;
	int code;
	int i;
	unsigned opcode;
	const struct { const char *name; unsigned opcode; unsigned ext; } bin[] = {
		{"add", 0x01, 0}, {"sub", 0x29, 5}, {"and", 0x21, 4},
		{"or", 0x09, 1}, {"xor", 0x31, 6}, {"cmp", 0x39, 7},
		{"test", 0x85, 0}
	};
	char operand_buf[1024];

	memset(out, 0, sizeof(*out));
	out->fixed = 1;

	/* Copy operands so we can tokenise them in-place (strtok-style). */
	operand_buf[0] = '\0';
	if (operands) {
		size_t olen = strlen(operands);
		if (olen >= sizeof(operand_buf))
			return -1;
		memcpy(operand_buf, operands, olen + 1);
	}

	for (i = 0; mnemonic[i] && i < (int)sizeof(base) - 1; ++i)
		base[i] = (char)tolower((unsigned char)mnemonic[i]);
	base[i] = '\0';

	/* ISA gating is handled by assemble.c emit_instruction() before calling
	 * this encoder (pre-encode heuristic), so the encoder itself does not
	 * need to check features — any instruction it can't handle returns -1. */

	if (strcmp(base, "endbr64") == 0) {
		emit_u8(out->bytes, &out->size, 0xf3);
		emit_u8(out->bytes, &out->size, 0x0f);
		emit_u8(out->bytes, &out->size, 0x1e);
		emit_u8(out->bytes, &out->size, 0xfa);
		return 0;
	}
	if (strcmp(base, "ret") == 0) {
		emit_u8(out->bytes, &out->size, 0xc3);
		return 0;
	}
	if (strcmp(base, "leave") == 0) {
		emit_u8(out->bytes, &out->size, 0xc9);
		return 0;
	}
	if (strcmp(base, "nop") == 0) {
		emit_u8(out->bytes, &out->size, 0x90);
		return 0;
	}
	if (strcmp(base, "ud2") == 0) {
		emit_u8(out->bytes, &out->size, 0x0f);
		emit_u8(out->bytes, &out->size, 0x0b);
		return 0;
	}
	if (strcmp(base, "syscall") == 0) {
		emit_u8(out->bytes, &out->size, 0x0f);
		emit_u8(out->bytes, &out->size, 0x05);
		return 0;
	}
	if (strcmp(base, "mfence") == 0) {
		emit_u8(out->bytes, &out->size, 0x0f);
		emit_u8(out->bytes, &out->size, 0xae);
		emit_u8(out->bytes, &out->size, 0xf0);
		return 0;
	}
	if (strcmp(base, "hlt") == 0) {
		emit_u8(out->bytes, &out->size, 0xf4);
		return 0;
	}
	if (strcmp(base, "lock") == 0) {
		char *inner = operand_buf;
		char *split = inner;
		while (*split && !isspace((unsigned char)*split))
			++split;
		if (!*inner || !*split)
			return -1;
		*split++ = '\0';
		trim(split);
		emit_u8(out->bytes, &out->size, 0xf0);
		return x86_64_encode_insn(target, inner, split, out);
	}
	if (strcmp(base, "cqto") == 0) {
		emit_u8(out->bytes, &out->size, 0x48);
		emit_u8(out->bytes, &out->size, 0x99);
		return 0;
	}
	if (strcmp(base, "cltd") == 0) {
		emit_u8(out->bytes, &out->size, 0x99);
		return 0;
	}
	if (strcmp(base, "pushq") == 0 || strcmp(base, "push") == 0 ||
	    strcmp(base, "popq") == 0 || strcmp(base, "pop") == 0) {
		n = split_operands(operand_buf, ops);
		if (n != 1 || parse_operand(ops[0], &op[0]) != 0 ||
		    op[0].kind != OP_REG) {
			x86_free_op(&op[0]);
			return -1;
		}
		if (strncmp(base, "push", 4) == 0) {
			if (op[0].reg >= 8)
				emit_u8(out->bytes, &out->size, 0x41);
			emit_u8(out->bytes, &out->size, 0x50 + (op[0].reg & 7));
		} else {
			if (op[0].reg >= 8)
				emit_u8(out->bytes, &out->size, 0x41);
			emit_u8(out->bytes, &out->size, 0x58 + (op[0].reg & 7));
		}
		x86_free_op(&op[0]);
		return 0;
	}
	n = split_operands(operand_buf, ops);
	if (n < 0) return -1;
	for (i = 0; i < n; ++i)
		if (parse_operand(ops[i], &op[i]) != 0)
			goto fail;
	if (strncmp(base, "call", 4) == 0 && n == 1) {
		if (op[0].kind == OP_SYMBOL) {
			code = strcmp(op[0].modifier, "plt") == 0 ?
			       R_X86_64_PLT32 : R_X86_64_PC32;
			emit_symbol_branch(out->bytes, &out->size, out, 0xe8, &op[0], code);
			goto done;
		}
		if (op[0].kind == OP_REG) {
			emit_rex(out->bytes, &out->size, 0, 0, op[0].reg);
			emit_u8(out->bytes, &out->size, 0xff);
			emit_modrm(out->bytes, &out->size, out, 2, &op[0], R_X86_64_PC32, -4);
			goto done;
		}
		goto unsupported;
	}
	if ((strcmp(base, "jmp") == 0 || base[0] == 'j') && n == 1) {
		int unconditional = strcmp(base, "jmp") == 0;
		code = unconditional ? 0 : condition_code(base + 1);
		if (code < 0)
			goto unsupported;
		if (unconditional) {
			emit_symbol_branch(out->bytes, &out->size, out, 0xe9, &op[0],
			                   R_X86_64_PC32);
		} else {
			emit_u8(out->bytes, &out->size, 0x0f);
			emit_u8(out->bytes, &out->size, 0x80 | code);
			if (op[0].kind != OP_SYMBOL)
				goto unsupported;
			{
				size_t off = out->size;
				emit_le(out->bytes, &out->size, 0, 4);
				set_fixup(out, off, 4, R_X86_64_PC32, op[0].symbol, -4);
			}
		}
		goto done;
	}
	if (strncmp(base, "set", 3) == 0 && n == 1) {
		code = condition_code(base + 3);
		if (code < 0 || op[0].kind != OP_REG)
			goto unsupported;
		emit_rex(out->bytes, &out->size, 0, 0, op[0].reg);
		emit_u8(out->bytes, &out->size, 0x0f);
		emit_u8(out->bytes, &out->size, 0x90 | code);
		emit_modrm(out->bytes, &out->size, out, 0, &op[0], R_X86_64_PC32, -4);
		goto done;
	}
	/* popcnt rdst, rsrc — SSE4.2 (F3 0F B8 /r), GPR only. Tagged with
	 * MT_FEATURE_SSE4_2 | MT_FEATURE_POPCNT so the assembler's ISA gate
	 * rejects it on baseline x86_64 unless -march=x86-64-v2+ is given. */
	if (strcmp(base, "popcnt") == 0 && n == 2) {
		if (op[0].kind != OP_REG || op[1].kind != OP_REG)
			goto unsupported;
		out->required_features |= MT_FEATURE_SSE4_2 | MT_FEATURE_POPCNT;
		width = op[0].width;
		emit_u8(out->bytes, &out->size, 0xf3);
		emit_rex(out->bytes, &out->size, width == 8, op[0].reg, op[1].reg);
		emit_u8(out->bytes, &out->size, 0x0f);
		emit_u8(out->bytes, &out->size, 0xb8);
		emit_modrm(out->bytes, &out->size, out, op[0].reg, &op[1],
		           R_X86_64_PC32, -4);
		goto done;
	}
	if (strncmp(base, "cmov", 4) == 0 && n == 2) {
		code = condition_code(base + 4);
		if (code < 0 || op[1].kind != OP_REG)
			goto unsupported;
		width = op[1].width;
		emit_rex(out->bytes, &out->size, width == 8, op[1].reg,
		          op[0].kind == OP_REG ? op[0].reg : -1);
		emit_u8(out->bytes, &out->size, 0x0f);
		emit_u8(out->bytes, &out->size, 0x40 | code);
		emit_modrm(out->bytes, &out->size, out, op[1].reg, &op[0],
		           R_X86_64_PC32, -4);
		goto done;
	}
	if ((strncmp(base, "shl", 3) == 0 || strncmp(base, "sal", 3) == 0 ||
	     strncmp(base, "shr", 3) == 0 || strncmp(base, "sar", 3) == 0) && n == 2) {
		int ext = (strncmp(base, "shl", 3) == 0 || strncmp(base, "sal", 3) == 0) ?
		          4 : strncmp(base, "shr", 3) == 0 ? 5 : 7;
		char suffix_char = base[3];
		width = suffix_char == 'b' ? 1 : suffix_char == 'w' ? 2 :
		        suffix_char == 'l' ? 4 : 8;
		if (op[1].kind != OP_REG && op[1].kind != OP_MEM)
			goto unsupported;
		if (op[0].kind == OP_IMM) {
			emit_rex(out->bytes, &out->size, width == 8, 0,
			          op[1].kind == OP_REG ? op[1].reg : op[1].base);
			emit_u8(out->bytes, &out->size, width == 1 ? 0xc0 : 0xc1);
			emit_modrm(out->bytes, &out->size, out, ext, &op[1],
			           R_X86_64_PC32, -4);
			emit_le(out->bytes, &out->size, (uint8_t)op[0].displacement, 1);
		} else if (op[0].kind == OP_REG && op[0].reg == 1 && op[0].width == 1) {
			emit_rex(out->bytes, &out->size, width == 8, 0,
			          op[1].kind == OP_REG ? op[1].reg : op[1].base);
			emit_u8(out->bytes, &out->size, width == 1 ? 0xd2 : 0xd3);
			emit_modrm(out->bytes, &out->size, out, ext, &op[1],
			           R_X86_64_PC32, -4);
		} else
			goto unsupported;
		goto done;
	}
	if ((strcmp(base, "div") == 0 || strcmp(base, "idiv") == 0 ||
	     strncmp(base, "div", 3) == 0 || strncmp(base, "idiv", 4) == 0) && n == 1) {
		char suffix_char = base[strlen(base) - 1];
		int ext = strncmp(base, "idiv", 4) == 0 ? 7 : 6;
		width = suffix_char == 'b' ? 1 : suffix_char == 'w' ? 2 :
		        suffix_char == 'l' ? 4 : 8;
		if (op[0].kind != OP_REG && op[0].kind != OP_MEM)
			goto unsupported;
		emit_rex(out->bytes, &out->size, width == 8, 0,
		          op[0].kind == OP_REG ? op[0].reg : -1);
		emit_u8(out->bytes, &out->size, width == 1 ? 0xf6 : 0xf7);
		emit_modrm(out->bytes, &out->size, out, ext, &op[0], R_X86_64_PC32, -4);
		goto done;
	}
	/* --- SSE scalar instructions --- */
	{
		unsigned sse_pfx, sse_op;
		if (n == 2 && sse_arithmetic_lookup(base, &sse_pfx, &sse_op) == 0) {
			if (!is_xmm(&op[1]))
				goto unsupported;
			emit_sse(out->bytes, &out->size, out, sse_pfx, sse_op,
			         op[1].reg, &op[0], 0, R_X86_64_PC32, -4);
			goto done;
		}
		if (n == 2 && (strcmp(base, "movss") == 0 ||
		                strcmp(base, "movsd") == 0)) {
			unsigned pfx = (strcmp(base, "movsd") == 0) ? 0xF2 : 0xF3;
			unsigned op2;
			if (is_xmm(&op[0]) && is_xmm(&op[1])) {
				op2 = 0x10;
				emit_sse(out->bytes, &out->size, out, pfx, op2,
				         op[1].reg, &op[0], 0, R_X86_64_PC32, -4);
			} else if (is_xmm(&op[0]) && op[1].kind == OP_MEM) {
				op2 = 0x11;
				emit_sse(out->bytes, &out->size, out, pfx, op2,
				         op[0].reg, &op[1], 0, R_X86_64_PC32, -4);
			} else if (op[0].kind == OP_MEM && is_xmm(&op[1])) {
				op2 = 0x10;
				emit_sse(out->bytes, &out->size, out, pfx, op2,
				         op[1].reg, &op[0], 0, R_X86_64_PC32, -4);
			} else
				goto unsupported;
			goto done;
		}
		if (n == 2 && (strcmp(base, "cvtss2sd") == 0 ||
		                strcmp(base, "cvtsd2ss") == 0)) {
			unsigned pfx = (strcmp(base, "cvtsd2ss") == 0) ? 0xF2 : 0xF3;
			if (!is_xmm(&op[1]))
				goto unsupported;
			emit_sse(out->bytes, &out->size, out, pfx, 0x5A,
			         op[1].reg, &op[0], 0, R_X86_64_PC32, -4);
			goto done;
		}
		if (n == 2 && (strncmp(base, "cvttss2si", 9) == 0 ||
		                strncmp(base, "cvttsd2si", 9) == 0)) {
			unsigned pfx = (base[5] == 'd') ? 0xF2 : 0xF3;
			int need_w = (base[9] == 'q');
			if (op[1].kind != OP_REG || is_xmm(&op[1]))
				goto unsupported;
			emit_sse(out->bytes, &out->size, out, pfx, 0x2C,
			         op[1].reg, &op[0], need_w, R_X86_64_PC32, -4);
			goto done;
		}
		if (n == 2 && (strncmp(base, "cvtsi2ss", 8) == 0 ||
		                strncmp(base, "cvtsi2sd", 8) == 0)) {
			unsigned pfx = (base[7] == 'd') ? 0xF2 : 0xF3;
			int need_w = (base[8] == 'q');
			if (!is_xmm(&op[1]) || op[0].kind != OP_REG)
				goto unsupported;
			emit_sse(out->bytes, &out->size, out, pfx, 0x2A,
			         op[1].reg, &op[0], need_w, R_X86_64_PC32, -4);
			goto done;
		}
		if (n == 2 && (strcmp(base, "ucomiss") == 0 ||
		                strcmp(base, "ucomisd") == 0)) {
			unsigned pfx = (strcmp(base, "ucomisd") == 0) ? 0x66 : 0;
			if (!is_xmm(&op[0]) || !is_xmm(&op[1]))
				goto unsupported;
			emit_sse(out->bytes, &out->size, out, pfx, 0x2E,
			         op[1].reg, &op[0], 0, R_X86_64_PC32, -4);
			goto done;
		}
		if (n == 2 && (strcmp(base, "movq") == 0 || strcmp(base, "movd") == 0) &&
		    (is_xmm(&op[0]) || is_xmm(&op[1]))) {
			if (is_xmm(&op[0]) && !is_xmm(&op[1])) {
				int need_w = (op[1].width == 8);
				emit_sse(out->bytes, &out->size, out, 0x66, 0x7E,
				         op[0].reg, &op[1], need_w, R_X86_64_PC32, -4);
			} else if (!is_xmm(&op[0]) && is_xmm(&op[1])) {
				int need_w = (op[0].width == 8);
				emit_sse(out->bytes, &out->size, out, 0x66, 0x6E,
				         op[1].reg, &op[0], need_w, R_X86_64_PC32, -4);
			} else {
				emit_sse(out->bytes, &out->size, out, 0, 0x28,
				         op[1].reg, &op[0], 0, R_X86_64_PC32, -4);
			}
			goto done;
		}
		if (n == 2 && strcmp(base, "xorps") == 0 &&
		    is_xmm(&op[0]) && is_xmm(&op[1])) {
			emit_sse(out->bytes, &out->size, out, 0, 0x57,
			         op[1].reg, &op[0], 0, R_X86_64_PC32, -4);
			goto done;
		}
		if (n == 2 && strcmp(base, "pxor") == 0 &&
		    is_xmm(&op[0]) && is_xmm(&op[1])) {
			emit_sse(out->bytes, &out->size, out, 0x66, 0xEF,
			         op[1].reg, &op[0], 0, R_X86_64_PC32, -4);
			goto done;
		}
	}
	/* --- AVX vector move instructions (as-isa-gating VEX gating) ---
	 * vmovups/aps/apd: VEX.128/256.0F.{10,11,28,29} /r
	 * The VEX prefix is emitted by emit_vex(), followed by the 0F opcode.
	 * Gate: feature MT_FEATURE_AVX is set at line 638 above for all vX
	 *       mnemonics, so the assembler rejects them on baseline targets. */
	if (n >= 1) {
		int is_avx_mov = 0;
		unsigned avx_pp = 0, avx_op_load = 0, avx_op_store = 0;
		int is_256 = (n == 2 && is_ymm(&op[0])) || is_ymm(&op[1]);
		if (strcmp(base, "vmovups") == 0) {
			avx_pp = 0; avx_op_load = 0x10; avx_op_store = 0x11; is_avx_mov = 1;
		} else if (strcmp(base, "vmovupd") == 0) {
			avx_pp = 1; avx_op_load = 0x10; avx_op_store = 0x11; is_avx_mov = 1;
		} else if (strcmp(base, "vmovaps") == 0) {
			avx_pp = 0; avx_op_load = 0x28; avx_op_store = 0x29; is_avx_mov = 1;
		} else if (strcmp(base, "vmovapd") == 0) {
			avx_pp = 1; avx_op_load = 0x28; avx_op_store = 0x29; is_avx_mov = 1;
		}
		if (is_avx_mov) {
			unsigned opcode;
			int reg_num;       /* ModRM.reg (destination / src when store) */
			const struct x86_op *rm_op;   /* ModRM.r/m (source or memory) */
			if (n == 2 && op[0].kind == OP_REG && op[1].kind == OP_REG) {
				/* vmov* %src, %dst: reg=dst, rm=src → load opcode */
				reg_num = op[1].reg;
				rm_op = &op[0];
				opcode = avx_op_load;
			} else if (n == 2 && op[0].kind == OP_REG && op[1].kind == OP_MEM) {
				/* vmov* %src, (mem): reg=src, rm=mem → store opcode */
				reg_num = op[0].reg;
				rm_op = &op[1];
				opcode = avx_op_store;
			} else if (n == 2 && op[0].kind == OP_MEM && op[1].kind == OP_REG) {
				/* vmov* (mem), %dst: reg=dst, rm=mem → load opcode */
				reg_num = op[1].reg;
				rm_op = &op[0];
				opcode = avx_op_load;
			} else
				goto unsupported;
			emit_vex(out->bytes, &out->size, is_256, avx_pp, reg_num, -1);
			emit_u8(out->bytes, &out->size, opcode);  /* VEX (mmmmm=00001) implies 0x0F */
			emit_modrm(out->bytes, &out->size, out, reg_num,
			           rm_op, R_X86_64_PC32, -4);
			goto done;
		}
	}
	if (strncmp(base, "movz", 4) == 0 || strncmp(base, "movs", 4) == 0) {
		int signed_move = base[3] == 's';
		int source_width;
		if (n != 2 || op[1].kind != OP_REG)
			goto unsupported;
		if (base[4] == 'b') source_width = 1;
		else if (base[4] == 'w') source_width = 2;
		else if (base[4] == 'l') source_width = 4;
		else goto unsupported;
		width = op[1].width;
		if (source_width == 4 && width == 8) {
			emit_rex(out->bytes, &out->size, 1, op[1].reg,
			          op[0].kind == OP_REG ? op[0].reg : -1);
			emit_u8(out->bytes, &out->size, 0x63);
			emit_modrm(out->bytes, &out->size, out, op[1].reg, &op[0],
			           R_X86_64_PC32, -4);
		} else {
			opcode = source_width == 1 ? 0xb6 : source_width == 2 ? 0xb7 :
			         signed_move ? 0xbe : 0xb6;
			if (source_width == 4)
				goto unsupported;
			emit_rex(out->bytes, &out->size, width == 8, op[1].reg,
			          op[0].kind == OP_REG ? op[0].reg : -1);
			emit_u8(out->bytes, &out->size, 0x0f);
			emit_u8(out->bytes, &out->size, opcode);
			emit_modrm(out->bytes, &out->size, out, op[1].reg, &op[0],
			           R_X86_64_PC32, -4);
		}
		goto done;
	}
	if (strncmp(base, "mov", 3) == 0 && n == 2) {
		char suffix_char = base[3];
		width = suffix_char == 'b' ? 1 : suffix_char == 'w' ? 2 :
		        suffix_char == 'l' ? 4 : 8;
		if (op[0].kind == OP_IMM && op[1].kind == OP_REG) {
			if (width == 8 && (op[0].displacement > INT32_MAX ||
			                   op[0].displacement < INT32_MIN)) {
				emit_rex(out->bytes, &out->size, 1, 0, op[1].reg);
				emit_u8(out->bytes, &out->size, 0xb8 + (op[1].reg & 7));
				emit_le(out->bytes, &out->size, (uint64_t)op[0].displacement, 8);
			} else {
				emit_rex(out->bytes, &out->size, width == 8, 0, op[1].reg);
				emit_u8(out->bytes, &out->size, width == 1 ? 0xb0 : 0xb8 +
				        (op[1].reg & 7));
				emit_le(out->bytes, &out->size, (uint64_t)op[0].displacement,
				        width == 1 ? 1 : width == 2 ? 2 : 4);
			}
			goto done;
		}
		if (op[0].kind == OP_IMM && (op[1].kind == OP_MEM ||
		                              op[1].kind == OP_REG)) {
			emit_rex(out->bytes, &out->size, width == 8, 0,
			          op[1].kind == OP_REG ? op[1].reg : op[1].base);
			emit_u8(out->bytes, &out->size, width == 1 ? 0xc6 : 0xc7);
			emit_modrm(out->bytes, &out->size, out, 0, &op[1], R_X86_64_PC32, -4);
			emit_le(out->bytes, &out->size, (uint64_t)op[0].displacement,
			        width == 1 ? 1 : 4);
			goto done;
		}
		if (op[0].kind == OP_REG || op[0].kind == OP_MEM) {
			opcode = op[0].kind == OP_REG ?
			         (width == 1 ? 0x88 : 0x89) :
			         (width == 1 ? 0x8a : 0x8b);
			emit_rm_reg(out->bytes, &out->size, out, opcode, width,
			            &op[0], &op[1]);
			goto done;
		}
		goto unsupported;
	}
	if (strncmp(base, "lea", 3) == 0 && n == 2 && op[1].kind == OP_REG) {
		width = op[1].width;
		emit_rex(out->bytes, &out->size, width == 8, op[1].reg,
		          op[0].kind == OP_REG ? op[0].reg : -1);
		emit_u8(out->bytes, &out->size, 0x8d);
		emit_modrm(out->bytes, &out->size, out, op[1].reg, &op[0],
		           R_X86_64_PC32, -4);
		goto done;
	}
	if ((strncmp(base, "xchg", 4) == 0 || strncmp(base, "xadd", 4) == 0 ||
	     strncmp(base, "cmpxchg", 7) == 0) && n == 2) {
		const char *prefix = strncmp(base, "cmpxchg", 7) == 0 ? base + 7 : base + 4;
		char suffix_char = *prefix;
		width = suffix_char == 'b' ? 1 : suffix_char == 'w' ? 2 :
		        suffix_char == 'l' ? 4 : 8;
		if (strncmp(base, "xchg", 4) == 0) {
			opcode = width == 1 ? 0x86 : 0x87;
			emit_rm_reg(out->bytes, &out->size, out, opcode, width,
			            &op[0], &op[1]);
		} else {
			opcode = strncmp(base, "xadd", 4) == 0 ?
			          (width == 1 ? 0xc0 : 0xc1) :
			          (width == 1 ? 0xb0 : 0xb1);
			if (width == 1)
				emit_byte_rex(out->bytes, &out->size,
				              op[0].kind == OP_REG ? op[0].reg : op[1].reg,
				              op[1].kind == OP_REG ? op[1].reg : -1);
			emit_rex(out->bytes, &out->size, width == 8,
			          op[0].kind == OP_REG ? op[0].reg : op[1].reg,
			          op[1].kind == OP_REG ? op[1].reg : op[1].base);
			emit_u8(out->bytes, &out->size, 0x0f);
			emit_u8(out->bytes, &out->size, opcode);
			emit_modrm(out->bytes, &out->size, out,
			           op[0].kind == OP_REG ? op[0].reg : op[1].reg,
			           &op[1], R_X86_64_PC32, -4);
		}
		goto done;
	}
	if ((strncmp(base, "inc", 3) == 0 || strncmp(base, "dec", 3) == 0) && n == 1) {
		char suffix_char = base[3];
		width = suffix_char == 'b' ? 1 : suffix_char == 'w' ? 2 :
		        suffix_char == 'l' ? 4 : 8;
		opcode = strncmp(base, "inc", 3) == 0 ? 0 : 1;
		if (width == 1)
			emit_byte_rex(out->bytes, &out->size, 0,
			              op[0].kind == OP_REG ? op[0].reg : -1);
		emit_rex(out->bytes, &out->size, width == 8, 0,
		          op[0].kind == OP_REG ? op[0].reg : -1);
		emit_u8(out->bytes, &out->size, width == 1 ? 0xfe : 0xff);
		emit_modrm(out->bytes, &out->size, out, opcode, &op[0],
		           R_X86_64_PC32, -4);
		goto done;
	}
	if (strncmp(base, "imul", 4) == 0 && (n == 2 || n == 3)) {
		if (n == 3 && op[0].kind == OP_IMM && op[2].kind == OP_REG) {
			width = op[2].width;
			emit_rex(out->bytes, &out->size, width == 8, op[2].reg,
			          op[1].kind == OP_REG ? op[1].reg : op[1].base);
			emit_u8(out->bytes, &out->size, 0x69);
			emit_modrm(out->bytes, &out->size, out, op[2].reg, &op[1],
			           R_X86_64_PC32, -4);
			emit_le(out->bytes, &out->size, (uint32_t)op[0].displacement, 4);
			goto done;
		}
		if (n == 2 && op[1].kind == OP_REG) {
			width = op[1].width;
			emit_rex(out->bytes, &out->size, width == 8, op[1].reg,
			          op[0].kind == OP_REG ? op[0].reg : -1);
			emit_u8(out->bytes, &out->size, 0x0f);
			emit_u8(out->bytes, &out->size, 0xaf);
			emit_modrm(out->bytes, &out->size, out, op[1].reg, &op[0],
			           R_X86_64_PC32, -4);
			goto done;
		}
		goto unsupported;
	}
	for (i = 0; i < (int)(sizeof(bin) / sizeof(bin[0])); ++i)
		if (strncmp(base, bin[i].name, strlen(bin[i].name)) == 0 && n == 2) {
			char suffix_char = base[strlen(bin[i].name)];
			width = suffix_char == 'b' ? 1 : suffix_char == 'w' ? 2 :
			        suffix_char == 'l' ? 4 : 8;
			if (op[0].kind == OP_IMM) {
				emit_binary_immediate(out->bytes, &out->size, out,
				                      bin[i].ext, width, &op[0], &op[1]);
			} else {
				unsigned binary_opcode = bin[i].opcode;
				if (strcmp(bin[i].name, "test") != 0 &&
				    op[0].kind == OP_MEM && op[1].kind == OP_REG)
					binary_opcode += 2;
				emit_rm_reg(out->bytes, &out->size, out, binary_opcode, width,
				            &op[0], &op[1]);
			}
			goto done;
		}
	if (strncmp(base, "neg", 3) == 0 && n == 1) {
		width = base[3] == 'b' ? 1 : base[3] == 'w' ? 2 :
		        base[3] == 'l' ? 4 : 8;
		if (width == 1)
			emit_byte_rex(out->bytes, &out->size, 0,
			              op[0].kind == OP_REG ? op[0].reg : -1);
		emit_rex(out->bytes, &out->size, width == 8, 0,
		          op[0].kind == OP_REG ? op[0].reg : -1);
		emit_u8(out->bytes, &out->size, width == 1 ? 0xf6 : 0xf7);
		emit_modrm(out->bytes, &out->size, out, 3, &op[0], R_X86_64_PC32, -4);
		goto done;
	}
	goto unsupported;

unsupported:
	for (i = 0; i < n; ++i)
		x86_free_op(&op[i]);
	return -1;
fail:
	for (i = 0; i < n; ++i)
		x86_free_op(&op[i]);
	return -1;
done:
	for (i = 0; i < n; ++i)
		x86_free_op(&op[i]);
	return 0;
}
