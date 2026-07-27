/* encode.c — i386 instruction encoder.
 *
 * Implements a subset of the i386 instruction set sufficient for typical
 * assembly output from mcc and libc runtime .S files.
 *
 * AT&T syntax, operand order: mnemonic src, dst.
 * Does NOT support REX prefixes, %r8–%r15, or %rip-relative addressing.
 * Address/displacement size is 32-bit. */

#include "mt/target.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- Relocation constants (matching <mt/elf.h> values) ---- */
#define R_386_32       1
#define R_386_PC32     2
#define R_386_PLT32    4

/* ---- Operand parsing ---- */

#define OP_INVALID 0
#define OP_IMM     1
#define OP_REG     2
#define OP_MEM     3
#define OP_SYMBOL  4

struct i386_op {
	int kind;           /* OP_* */
	int reg;            /* register number (0-7) or -1 */
	int width;          /* 1=byte, 2=word, 4=dword (not used for modrm) */
	int base;           /* base register for mem, or -1 */
	int index_reg;      /* index register for SIB, or -1 */
	int scale;          /* scale factor (1/2/4/8) */
	int64_t disp;       /* displacement */
	const char *sym;    /* symbol name (for fixups) */
	int64_t addend;
};

/* Register name table (i386: only 8 general-purpose regs, no REX) */
struct reg_entry {
	const char *name;
	int num;       /* 0-7 */
	int width;     /* 1=8bit, 2=16bit, 4=32bit */
};

/* Highest-numbered 8-bit register accessible without REX is bh (7).
 * We define the standard names only. */
static const struct reg_entry regs[] = {
	{"al",   0, 1}, {"cl",   1, 1}, {"dl",   2, 1}, {"bl",   3, 1},
	{"ah",   4, 1}, {"ch",   5, 1}, {"dh",   6, 1}, {"bh",   7, 1},
	{"ax",   0, 2}, {"cx",   1, 2}, {"dx",   2, 2}, {"bx",   3, 2},
	{"sp",   4, 2}, {"bp",   5, 2}, {"si",   6, 2}, {"di",   7, 2},
	{"eax",  0, 4}, {"ecx",  1, 4}, {"edx",  2, 4}, {"ebx",  3, 4},
	{"esp",  4, 4}, {"ebp",  5, 4}, {"esi",  6, 4}, {"edi",  7, 4},
};

/* Strip leading '%' if present (AT&T syntax). */
static const char *
strip_pct(const char *name)
{
	if (name && name[0] == '%')
		return name + 1;
	return name;
}

static int
lookup_reg(const char *name)
{
	size_t i;
	name = strip_pct(name);
	for (i = 0; i < sizeof regs / sizeof regs[0]; ++i)
		if (strcmp(regs[i].name, name) == 0)
			return (int)i;
	return -1;
}

/* Parse a register text and return its number (0-7) or -1. */
static int
parse_reg_text(const char *text, int *reg_out)
{
	int ri;
	text = strip_pct(text);
	ri = lookup_reg(text);
	if (ri < 0)
		return -1;
	if (reg_out)
		*reg_out = regs[ri].num;
	return 0;
}

/* Parse an operand string into an i386_op. */
static int
parse_operand(const char *text, struct i386_op *op)
{
	memset(op, 0, sizeof(*op));
	op->base = -1;
	op->index_reg = -1;
	op->scale = 1;
	op->kind = OP_INVALID;

	while (*text == ' ')
		text++;

	/* Immediate: $<value> or $<symbol> */
	if (text[0] == '$') {
		const char *val = text + 1;
		char *end;
		long nv;

		op->kind = OP_IMM;
		op->width = 4;  /* default for i386 */

		/* Try parse as number first */
		nv = strtol(val, &end, 0);
		if (end != val && *end == '\0') {
			op->disp = (int64_t)nv;
			return 1;
		}

		/* Try as negative number */
		{
			const char *sym = val;
			if (*sym == '-') sym++;
			if (*sym == '0' || (*sym >= '1' && *sym <= '9')) {
				nv = strtol(val, &end, 0);
				if (end != val) {
					op->disp = (int64_t)nv;
					return 1;
				}
			}
		}

		/* Symbol reference */
		op->kind = OP_SYMBOL;
		op->sym = val;
		return 1;
	}

	/* Register (AT&T syntax: %eax)
	 * Must check before memory because '(%eax)' starts with '(' */
	{
		const char *stripped = strip_pct(text);
		int ri = lookup_reg(stripped);
		if (ri >= 0 && text[0] != '(') {
			op->kind = OP_REG;
			op->reg = regs[ri].num;
			op->width = regs[ri].width;
			op->base = op->reg;
			return 1;
		}
	}

	/* Memory: displacement(base,index,scale) or (base) or displacement */
	if (text[0] == '(' || strchr(text, '(') != NULL) {
		const char *paren = strchr(text, '(');
		char *end;

		op->kind = OP_MEM;

		/* Optional displacement before '(' */
		if (paren && paren > text) {
			long dv = strtol(text, &end, 0);
			if (end == paren) {
				op->disp = (int64_t)dv;
			} else {
				/* Not a numeric prefix — might be a symbol */
				return 0;
			}
		}

		if (paren) {
			const char *start = paren + 1;
			const char *close = strchr(start, ')');
			char inner[64];
			size_t inner_len;
			char *comma;

			if (!close) return 0;
			inner_len = (size_t)(close - start);
			if (inner_len >= sizeof(inner)) return 0;
			memcpy(inner, start, inner_len);
			inner[inner_len] = '\0';

			/* Parse inner: base or base,index or base,index,scale */
			comma = strchr(inner, ',');
			if (comma) {
				char *comma2;
				*comma = '\0';
				/* base part */
				if (parse_reg_text(inner, &op->base) != 0)
					return 0;
				/* index part */
				comma2 = strchr(comma + 1, ',');
				if (comma2) {
					*comma2 = '\0';
					if (parse_reg_text(comma + 1, &op->index_reg) != 0)
						return 0;
					/* scale part */
					{
						char *s_end;
						long sv = strtol(comma2 + 1, &s_end, 0);
						if (s_end == comma2 + 1 || *s_end != '\0')
							return 0;
						if (sv != 1 && sv != 2 && sv != 4 && sv != 8)
							return 0;
						op->scale = (int)sv;
					}
				} else {
					if (parse_reg_text(comma + 1, &op->index_reg) != 0)
						return 0;
					/* scale defaults to 1 (no explicit scale) */
				}
			} else {
				/* Single register inside parentheses */
				if (parse_reg_text(inner, &op->base) != 0)
					return 0;
			}
		}
		return 1;
	}

	/* Bare displacement (no parentheses, no $) — numeric absolute address */
	{
		const char *p = text;
		int has_digit = 0;
		while (*p) {
			if (*p == '+' || *p == '-') { p++; continue; }
			if (*p >= '0' && *p <= '9') has_digit = 1;
			p++;
			break;
		}
		if (has_digit && strchr(text, '(') == NULL && strchr(text, '$') == NULL) {
			op->kind = OP_IMM;
			op->disp = strtol(text, NULL, 0);
			op->width = 4;
			return 1;
		}
	}

	/* Symbol (plain text, e.g., function label for call/jmp) */
	{
		op->kind = OP_SYMBOL;
		op->sym = text;
		return 1;
	}
}

/* ---- Encoding helpers ---- */

static void
emit8(unsigned char *p, uint8_t v) { p[0] = v; }

static void
emit32(unsigned char *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* Encode ModR/M byte for register/register or register/memory */
static unsigned char
modrm(unsigned mod, unsigned reg, unsigned rm)
{
	return (unsigned char)(((mod) << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* Emit ModR/M + optional SIB + displacement for a memory operand.
 * Returns bytes emitted (1 or 2 for ModR/M+SIB, plus displacement). */
static void
emit_modrm_mem(unsigned char **pp, int reg_num, const struct i386_op *op)
{
	unsigned char *p = *pp;
	int base = op->base;
	int index = op->index_reg;
	int scale = op->scale;
	int64_t disp = op->disp;
	int mod;

	/* Determine mod field based on displacement */
	if (disp == 0 && base != 5)
		mod = 0;
	else if (disp >= -128 && disp <= 127)
		mod = 1;
	else
		mod = 2;

	/* Decode scale to SIB scale field */
	unsigned sib_scale = (scale == 8) ? 3 : (scale == 4) ? 2 :
	                     (scale == 2) ? 1 : 0;
	unsigned sib_index = (index >= 0) ? (unsigned)index : 4; /* 4 = none */
	int need_sib = (index >= 0) || (base == 4);

	if (need_sib) {
		/* ModR/M: mod=mod, reg=reg_num, rm=4 (SIB follows) */
		emit8(p, modrm(mod, reg_num & 7, 4));
		p++;
		/* SIB: scale, index, base */
		emit8(p, (sib_scale << 6) | (sib_index << 3) | ((unsigned)base & 7));
		p++;
	} else {
		/* No SIB */
		emit8(p, modrm(mod, reg_num & 7, (unsigned)base & 7));
		p++;
	}

	/* Emit displacement */
	if (mod == 1) {
		emit8(p, (uint8_t)(disp & 0xFF));
		p++;
	} else if (mod == 2 || (mod == 0 && base == 5)) {
		emit32(p, (uint32_t)(disp & 0xFFFFFFFFULL));
		p += 4;
	}

	*pp = p;
}

/* Set fixup fields on an insn. */
static void
set_fixup(struct mt_insn *out, size_t offset, unsigned width,
          unsigned reloc_type, const char *sym, int64_t addend)
{
	out->fixed = 0;
	out->fixup_offset = offset;
	out->fixup_width = width;
	out->reloc_type = reloc_type;
	out->fixup_symbol = sym;
	out->fixup_addend = addend;
}

/* Condition codes for jcc / setcc / cmovcc */
static int
condition_code(const char *suffix)
{
	static const struct { const char *name; int code; } table[] = {
		{"o", 0x0}, {"no", 0x1},
		{"b", 0x2}, {"c", 0x2}, {"nae", 0x2},
		{"ae", 0x3}, {"nb", 0x3}, {"nc", 0x3},
		{"e", 0x4}, {"z", 0x4},
		{"ne", 0x5}, {"nz", 0x5},
		{"be", 0x6}, {"na", 0x6},
		{"a", 0x7}, {"nbe", 0x7},
		{"s", 0x8},
		{"ns", 0x9},
		{"p", 0xa}, {"pe", 0xa},
		{"np", 0xb}, {"po", 0xb},
		{"l", 0xc}, {"nge", 0xc},
		{"ge", 0xd}, {"nl", 0xd},
		{"le", 0xe}, {"ng", 0xe},
		{"g", 0xf}, {"nle", 0xf},
	};
	size_t i;
	for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
		if (strcmp(suffix, table[i].name) == 0)
			return table[i].code;
	return -1;
}

/* ---- Main instruction encoder ---- */

int
i386_encode_insn(const struct mt_target *target,
                 const char *mnemonic, const char *operands,
                 struct mt_insn *out)
{
	struct i386_op ops[3];
	int nops = 0;
	size_t offset;
	unsigned char *p;

	(void)target;
	memset(out, 0, sizeof(*out));
	out->fixed = 1;

	/* Split operands on ',' respecting parentheses nesting. */
	if (operands && *operands) {
		char buf[256];
		char *p_in;
		int paren = 0;
		size_t start = 0;
		size_t i;
		size_t olen = strlen(operands);

		if (olen >= sizeof(buf))
			return -1;
		memcpy(buf, operands, olen + 1);

		nops = 0;
		for (i = 0; buf[i] && nops < 3; ++i) {
			if (buf[i] == '(')
				++paren;
			else if (buf[i] == ')')
				--paren;
			if (buf[i] == ',' && paren == 0) {
				buf[i] = '\0';
				p_in = buf + start;
				while (*p_in == ' ') p_in++;
				if (parse_operand(p_in, &ops[nops]))
					nops++;
				start = i + 1;
			}
		}
		/* Last operand */
		if (buf[start] && nops < 3) {
			p_in = buf + start;
			while (*p_in == ' ') p_in++;
			if (parse_operand(p_in, &ops[nops]))
				nops++;
		}
	}

	/* Determine instruction width from mnemonic suffix. */
	size_t mlen = strlen(mnemonic);
	char suffix = '\0';
	if (mlen > 0) {
		suffix = mnemonic[mlen - 1];
		if (suffix == 'b') ;
		else if (suffix == 'w') ;
		else if (suffix == 'l') ;
		else if (suffix == 'q') ;
		else suffix = '\0';
	}
	/* For byte instructions, the operand might have 'b' suffix */
	const char *base = mnemonic;
	char base_buf[64];
	if (suffix) {
		size_t i;
		for (i = 0; i < mlen - 1 && i < sizeof(base_buf) - 1; ++i)
			base_buf[i] = mnemonic[i];
		base_buf[i] = '\0';
		base = base_buf;
	}

	p = out->bytes;
	offset = 0;

	/* Match based on base mnemonic */
	int match = 0;

	/* ---- MOV ---- */
	if (strcmp(base, "mov") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_IMM && ops[1].kind == OP_REG) {
			/* mov $imm, reg — opcode 0xB8+reg */
			emit8(p, 0xB8 + ops[1].reg);
			p++;
			offset = (size_t)(p - out->bytes);
			if (ops[0].kind == OP_SYMBOL) {
				emit32(p, 0);
				set_fixup(out, offset, 4, R_386_32,
				          ops[0].sym, ops[0].addend);
				p += 4;
			} else {
				emit32(p, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
				p += 4;
			}
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			/* mov reg, reg — 0x89 */
			emit8(p, 0x89);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_IMM && ops[1].kind == OP_MEM) {
			/* mov $imm, (mem) — 0xC7 /0 */
			int width = 4;
			emit8(p, width == 1 ? 0xC6 : 0xC7);
			p++;
			emit_modrm_mem(&p, 0, &ops[1]);
			emit32(p, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
			p += 4;
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			/* mov reg, (mem) — 0x89 */
			emit8(p, 0x89);
			p++;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			/* mov (mem), reg — 0x8B */
			emit8(p, 0x8B);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- RET ---- */
	if (strcmp(base, "ret") == 0) {
		match = 1;
		emit8(p, 0xC3);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- NOP ---- */
	if (strcmp(base, "nop") == 0) {
		match = 1;
		emit8(p, 0x90);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- INT ---- */
	if (strcmp(mnemonic, "int") == 0 && nops == 1 && ops[0].kind == OP_IMM) {
		match = 1;
		emit8(p, 0xCD);
		emit8(p + 1, (uint8_t)(ops[0].disp & 0xFF));
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- SYSENTER ---- */
	if (strcmp(mnemonic, "sysenter") == 0) {
		match = 1;
		emit8(p, 0x0F);
		emit8(p + 1, 0x34);
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- PUSH ---- */
	if (strcmp(base, "push") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0x50 + ops[0].reg);
			p++;
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_IMM) {
			emit8(p, 0x68);
			offset = (size_t)(p - out->bytes + 1);
			p++;
			emit32(p, (uint32_t)(ops[0].disp & 0xFFFFFFFF));
			p += 4;
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_MEM) {
			/* push (mem) — 0xFF /6 */
			emit8(p, 0xFF);
			p++;
			emit_modrm_mem(&p, 6, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- POP ---- */
	if (strcmp(base, "pop") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0x58 + ops[0].reg);
			p++;
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_MEM) {
			/* pop (mem) — 0x8F /0 */
			emit8(p, 0x8F);
			p++;
			emit_modrm_mem(&p, 0, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- LEAVE ---- */
	if (strcmp(base, "leave") == 0) {
		match = 1;
		emit8(p, 0xC9);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- HLT ---- */
	if (strcmp(base, "hlt") == 0) {
		match = 1;
		emit8(p, 0xF4);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- XOR ---- */
	if (strcmp(base, "xor") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x31);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			/* xor reg, (mem) — 0x31 */
			emit8(p, 0x31);
			p++;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			/* xor (mem), reg — 0x33 */
			emit8(p, 0x33);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- AND ---- */
	if (strcmp(base, "and") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x21);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			emit8(p, 0x21);
			p++;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			emit8(p, 0x23);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- OR ---- */
	if (strcmp(base, "or") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x09);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			emit8(p, 0x09);
			p++;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			emit8(p, 0x0B);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- ADD ---- */
	if (strcmp(base, "add") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x01);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			emit8(p, 0x01);
			p++;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			emit8(p, 0x03);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- SUB ---- */
	if (strcmp(base, "sub") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x29);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			emit8(p, 0x29);
			p++;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			emit8(p, 0x2B);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- CMP ---- */
	if (strcmp(base, "cmp") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x39);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_IMM && ops[1].kind == OP_REG) {
			/* cmp $imm, reg — 0x83 /7 for small imm, 0x81 /7 for 32-bit */
			if (ops[0].disp >= -128 && ops[0].disp <= 127) {
				emit8(p, 0x83);
				emit8(p + 1, modrm(3, 7, ops[1].reg));
				emit8(p + 2, (uint8_t)(ops[0].disp & 0xFF));
				p += 3;
			} else {
				emit8(p, 0x81);
				emit8(p + 1, modrm(3, 7, ops[1].reg));
				emit32(p + 2, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
				p += 6;
			}
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_IMM && ops[1].kind == OP_MEM) {
			/* cmp $imm, (mem) — 0x83 /7 or 0x81 /7 */
			if (ops[0].disp >= -128 && ops[0].disp <= 127) {
				emit8(p, 0x83);
				p++;
				emit_modrm_mem(&p, 7, &ops[1]);
				emit8(p, (uint8_t)(ops[0].disp & 0xFF));
				p++;
			} else {
				emit8(p, 0x81);
				p++;
				emit_modrm_mem(&p, 7, &ops[1]);
				emit32(p, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
				p += 4;
			}
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			emit8(p, 0x39);
			p++;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			emit8(p, 0x3B);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- INC ---- */
	if (strcmp(base, "inc") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0x40 + ops[0].reg);
			p++;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM) {
			/* inc (mem) — 0xFF /0 */
			emit8(p, 0xFF);
			p++;
			emit_modrm_mem(&p, 0, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- DEC ---- */
	if (strcmp(base, "dec") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0x48 + ops[0].reg);
			p++;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM) {
			/* dec (mem) — 0xFF /1 */
			emit8(p, 0xFF);
			p++;
			emit_modrm_mem(&p, 1, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- NEG ---- */
	if (strcmp(base, "neg") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0xF7);
			emit8(p + 1, modrm(3, 3, ops[0].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM) {
			emit8(p, 0xF7);
			p++;
			emit_modrm_mem(&p, 3, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- NOT ---- */
	if (strcmp(base, "not") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0xF7);
			emit8(p + 1, modrm(3, 2, ops[0].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM) {
			emit8(p, 0xF7);
			p++;
			emit_modrm_mem(&p, 2, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- CALL ---- */
	if (strcmp(mnemonic, "call") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_SYMBOL) {
			/* call sym — 0xE8 rel32 */
			emit8(p, 0xE8);
			p++;
			offset = (size_t)(p - out->bytes);
			emit32(p, 0);
			set_fixup(out, offset, 4, R_386_PC32, ops[0].sym, -4);
			p += 4;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG) {
			emit8(p, 0xFF);
			emit8(p + 1, modrm(3, 2, ops[0].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM) {
			/* call *(mem) — 0xFF /2 */
			emit8(p, 0xFF);
			p++;
			emit_modrm_mem(&p, 2, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- JMP (unconditional) ---- */
	if (strcmp(mnemonic, "jmp") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_SYMBOL) {
			/* jmp sym — 0xE9 rel32 */
			emit8(p, 0xE9);
			p++;
			offset = (size_t)(p - out->bytes);
			emit32(p, 0);
			set_fixup(out, offset, 4, R_386_PC32, ops[0].sym, -4);
			p += 4;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG) {
			/* jmp *%reg — 0xFF /4 */
			emit8(p, 0xFF);
			emit8(p + 1, modrm(3, 4, ops[0].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM) {
			emit8(p, 0xFF);
			p++;
			emit_modrm_mem(&p, 4, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- Conditional jumps (jcc) ---- */
	if (mnemonic[0] == 'j' && nops == 1) {
		int cc;
		/* Check it's a conditional jump, not jmp (handled above) */
		if (strcmp(mnemonic, "jmp") == 0)
			goto skip_jcc;
		cc = condition_code(mnemonic + 1);
		if (cc >= 0) {
			match = 1;
			if (ops[0].kind == OP_SYMBOL) {
				/* Near jcc: 0x0F 0x80|cc rel32 */
				emit8(p, 0x0F);
				emit8(p + 1, 0x80 | (unsigned char)cc);
				p += 2;
				offset = (size_t)(p - out->bytes);
				emit32(p, 0);
				set_fixup(out, offset, 4, R_386_PC32, ops[0].sym, -4);
				p += 4;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
	}
skip_jcc:

	/* ---- SHIFT / ROTATE (shl/sal/shr/sar/rol/ror) ---- */
	if ((strcmp(base, "shl") == 0 || strcmp(base, "sal") == 0 ||
	     strcmp(base, "shr") == 0 || strcmp(base, "sar") == 0 ||
	     strcmp(base, "rol") == 0 || strcmp(base, "ror") == 0) && nops == 2) {
		int ext;
		if (strcmp(base, "shl") == 0 || strcmp(base, "sal") == 0)
			ext = 4;
		else if (strcmp(base, "shr") == 0)
			ext = 5;
		else if (strcmp(base, "sar") == 0)
			ext = 7;
		else if (strcmp(base, "rol") == 0)
			ext = 0;
		else  /* ror */
			ext = 1;

		match = 1;

		if (ops[0].kind == OP_IMM && ops[1].kind == OP_REG) {
			/* shl $imm, reg — 0xC1 /ext, imm8 */
			emit8(p, 0xC1);
			emit8(p + 1, modrm(3, ext, ops[1].reg));
			emit8(p + 2, (uint8_t)(ops[0].disp & 0xFF));
			p += 3;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[0].reg == 1 && ops[0].width == 1 &&
		    ops[1].kind == OP_REG) {
			/* shl %cl, reg — 0xD3 /ext */
			emit8(p, 0xD3);
			emit8(p + 1, modrm(3, ext, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_IMM && ops[1].kind == OP_MEM) {
			/* shl $imm, (mem) — 0xC1 /ext, imm8 */
			emit8(p, 0xC1);
			p++;
			emit_modrm_mem(&p, ext, &ops[1]);
			emit8(p, (uint8_t)(ops[0].disp & 0xFF));
			p++;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[0].reg == 1 && ops[0].width == 1 &&
		    ops[1].kind == OP_MEM) {
			/* shl %cl, (mem) — 0xD3 /ext */
			emit8(p, 0xD3);
			p++;
			emit_modrm_mem(&p, ext, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- DIV / IDIV ---- */
	if ((strcmp(base, "div") == 0 || strcmp(base, "idiv") == 0) && nops == 1) {
		int ext = strcmp(base, "idiv") == 0 ? 7 : 6;
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0xF7);
			emit8(p + 1, modrm(3, ext, ops[0].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM) {
			emit8(p, 0xF7);
			p++;
			emit_modrm_mem(&p, ext, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- MUL (unsigned multiply) ---- */
	if (strcmp(base, "mul") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0xF7);
			emit8(p + 1, modrm(3, 4, ops[0].reg));  /* /4 */
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM) {
			emit8(p, 0xF7);
			p++;
			emit_modrm_mem(&p, 4, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- IMUL (signed multiply, 2 or 3 operand) ---- */
	if (strcmp(base, "imul") == 0) {
		if (nops == 1) {
			/* imul reg — implicit %eax * reg = %edx:%eax, 0xF7 /5 */
			match = 1;
			if (ops[0].kind == OP_REG) {
				emit8(p, 0xF7);
				emit8(p + 1, modrm(3, 5, ops[0].reg));
				p += 2;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
			if (ops[0].kind == OP_MEM) {
				emit8(p, 0xF7);
				p++;
				emit_modrm_mem(&p, 5, &ops[0]);
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
		if (nops == 2 && ops[1].kind == OP_REG && ops[0].kind == OP_REG) {
			/* imul reg, reg — 0x0F 0xAF */
			match = 1;
			emit8(p, 0x0F);
			emit8(p + 1, 0xAF);
			emit8(p + 2, modrm(3, ops[1].reg, ops[0].reg));
			p += 3;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (nops == 3 && ops[0].kind == OP_IMM &&
		    ops[2].kind == OP_REG) {
			/* imul $imm, reg/mem, reg — 0x69 */
			match = 1;
			emit8(p, 0x69);
			p++;
			if (ops[1].kind == OP_REG) {
				emit8(p, modrm(3, ops[2].reg, ops[1].reg));
				p++;
			} else if (ops[1].kind == OP_MEM) {
				emit_modrm_mem(&p, ops[2].reg, &ops[1]);
			} else
				goto skip_imul3;
			emit32(p, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
			p += 4;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
skip_imul3:
		if (nops == 2 && ops[0].kind == OP_IMM && ops[1].kind == OP_REG) {
			/* imul $imm, reg — 0x69 reg,reg,imm  (reg = reg * imm) */
			match = 1;
			emit8(p, 0x69);
			emit8(p + 1, modrm(3, ops[1].reg, ops[1].reg));
			emit32(p + 2, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
			p += 6;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

done:
	out->ok = match;
	if (!match) {
		out->fixed = 0;
		out->size = 0;
		return -1;
	}
	return 0;
}
