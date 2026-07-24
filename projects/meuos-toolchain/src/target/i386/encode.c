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

static int
lookup_reg(const char *name)
{
	size_t i;
	for (i = 0; i < sizeof regs / sizeof regs[0]; ++i)
		if (strcmp(regs[i].name, name) == 0)
			return (int)i;
	return -1;
}

/* Parse an operand string into an i386_op. */
static int
parse_operand(const char *text, struct i386_op *op)
{
	memset(op, 0, sizeof(*op));
	op->base = -1;
	op->index_reg = -1;
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
				/* number-like but with extra chars */
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

	/* Memory: displacement(base,index,scale) or (base) or displacement */
	if (text[0] == '(' || strchr(text, '(') != NULL) {
		const char *paren = strchr(text, '(');
		op->kind = OP_MEM;

		if (paren && paren > text) {
			char *end;
			long dv = strtol(text, &end, 0);
			if (end == paren) {
				op->disp = (int64_t)dv;
				op->addend = op->disp;
			} else {
				return 0;
			}
		}

		if (paren) {
			const char *start = paren + 1;
			const char *end = strchr(start, ')');
			int ri;

			if (!end) return 0;
			/* Simple (reg) format only for now */
			{
				char regname[32];
				size_t reglen = (size_t)(end - start);
				if (reglen >= sizeof(regname)) return 0;
				memcpy(regname, start, reglen);
				regname[reglen] = '\0';
				ri = lookup_reg(regname);
				if (ri >= 0)
					op->base = regs[ri].num;
			}
		}
		return 1;
	}

	/* Bare displacement (no parentheses) — likely a symbol */
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

	/* Register */
	{
		int ri = lookup_reg(text);
		if (ri >= 0) {
			op->kind = OP_REG;
			op->reg = regs[ri].num;
			op->width = regs[ri].width;
			op->base = op->reg; /* also set base for modrm */
			return 1;
		}
	}

	return 0;
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

/* The main instruction encoder */
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

	/* Split operands on ',' */
	if (operands && *operands) {
		char buf[256];
		char *tok;

		strncpy(buf, operands, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		tok = strtok(buf, ",");
		nops = 0;
		while (tok && nops < 3) {
			while (*tok == ' ') tok++;
			if (parse_operand(tok, &ops[nops]))
				nops++;
			tok = strtok(NULL, ",");
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

	if (strcmp(base, "mov") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_IMM && ops[1].kind == OP_REG) {
			/* mov $imm, reg — opcode 0xB8+reg */
			emit8(p, 0xB8 + ops[1].reg);
			p++;
			offset = (size_t)(p - out->bytes);
			if (ops[0].kind == OP_SYMBOL) {
				emit32(p, 0);
				set_fixup(out, offset, 4, 1 /* R_386_32 */,
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
		}
	}

	if (strcmp(base, "ret") == 0) {
		match = 1;
		emit8(p, 0xC3);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	if (strcmp(base, "nop") == 0) {
		match = 1;
		emit8(p, 0x90);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	if (strcmp(mnemonic, "int") == 0 && nops == 1 && ops[0].kind == OP_IMM) {
		match = 1;
		emit8(p, 0xCD);
		emit8(p + 1, (uint8_t)(ops[0].disp & 0xFF));
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	if (strcmp(mnemonic, "sysenter") == 0) {
		match = 1;
		emit8(p, 0x0F);
		emit8(p + 1, 0x34);
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

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
			if (ops[0].kind == OP_SYMBOL) {
				emit32(p, 0);
				set_fixup(out, offset, 4, 1, ops[0].sym, 0);
				p += 4;
			} else {
				emit32(p, (uint32_t)(ops[0].disp & 0xFFFFFFFF));
				p += 4;
			}
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	if (strcmp(base, "pop") == 0 && nops == 1 && ops[0].kind == OP_REG) {
		match = 1;
		emit8(p, 0x58 + ops[0].reg);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	if (strcmp(base, "leave") == 0) {
		match = 1;
		emit8(p, 0xC9);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	if (strcmp(base, "hlt") == 0) {
		match = 1;
		emit8(p, 0xF4);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* XOR: 0x31 (reg, reg) and 0x33 (reg, reg/mem) */
	if (strcmp(base, "xor") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x31);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ADD: 0x01 (reg, reg), 0x03 (reg, reg/mem), 0x05 (eax, imm) */
	if (strcmp(base, "add") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x01);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* SUB: 0x29 (reg, reg) */
	if (strcmp(base, "sub") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x29);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* CMP: 0x39 (reg, reg) */
	if (strcmp(base, "cmp") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x39);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	if (strcmp(base, "inc") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0x40 + ops[0].reg);
			p++;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	if (strcmp(base, "dec") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0x48 + ops[0].reg);
			p++;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	if (strcmp(base, "neg") == 0 && nops == 1 && ops[0].kind == OP_REG) {
		match = 1;
		emit8(p, 0xF7);
		emit8(p + 1, modrm(3, 3, ops[0].reg));  /* /3 */
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	if (strcmp(base, "not") == 0 && nops == 1 && ops[0].kind == OP_REG) {
		match = 1;
		emit8(p, 0xF7);
		emit8(p + 1, modrm(3, 2, ops[0].reg));
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	if (strcmp(mnemonic, "call") == 0 && nops == 1) {
		match = 1;
		if (ops[0].kind == OP_REG) {
			emit8(p, 0xFF);
			emit8(p + 1, modrm(3, 2, ops[0].reg));
			p += 2;
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
