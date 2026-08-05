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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Relocation constants (matching <mt/elf.h> values) ---- */
#define R_386_32       1
#define R_386_PC32     2
#define R_386_PLT32    4
#define R_386_TLS_LE   17

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
	int is_imm;         /* operand came from '$' prefix (immediate) */
	int seg_gs;         /* %gs: segment override prefix (TLS variant II) */
	int tls_le;         /* symbol carries @ntpoff: R_386_TLS_LE */
	/* Symbol storage.  Operand text points into a scratch buffer that
	 * is reused later in the encoder, so symbol names are copied here
	 * to stay valid until the caller consumes the fixup. */
	char sym_buf[128];
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

/* Parse a register text and return its number (0-7) or -1.
 * Tolerates leading whitespace: mcc emits "8(%ebp, %eax, 4)" where the
 * index register is separated by ", " (space after comma). */
static int
parse_reg_text(const char *text, int *reg_out)
{
	int ri;
	text = strip_pct(text);
	while (*text == ' ' || *text == '\t')
		text++;
	ri = lookup_reg(text);
	if (ri < 0)
		return -1;
	if (reg_out)
		*reg_out = regs[ri].num;
	return 0;
}

/* Strip surrounding double quotes from a symbol name in place.  mcc
 * quotes local labels containing dots (e.g. ".Lfp1"), but the symbol
 * table entry (defined by the label line) is unquoted, so the fixup
 * must reference the bare name. */
static void
strip_sym_quotes(char *sym)
{
	size_t n = strlen(sym);
	if (n >= 2 && sym[0] == '"' && sym[n - 1] == '"') {
		memmove(sym, sym + 1, n - 2);
		sym[n - 2] = '\0';
	}
}

/* Split a trailing numeric offset off a symbol name ("sym+4"/"sym-8")
 * into the addend.  Quoted names are never split.  Mirrors the
 * riscv64 encoder's split_sym_offset. */
static void
split_sym_offset(char *sym, int64_t *off)
{
	char *p;
	*off = 0;
	if (!sym || !*sym || sym[0] == '"')
		return;
	for (p = sym; *p; p++) {
		if ((*p == '+' || *p == '-') && p[1] >= '0' && p[1] <= '9') {
			*off = strtoll(p + 1, NULL, 10);
			if (*p == '-')
				*off = -*off;
			*p = '\0';
			break;
		}
	}
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

	/* AT&T indirect marker '*': skip it and try again as register/memory
	 * (e.g. "call *%eax", "jmp *(mem)"). */
	if (text[0] == '*') {
		text++;
		while (*text == ' ')
			text++;
	}

	/* TLS segment override (variant II): %gs:sym@ntpoff / %gs:0 */
	if (strncmp(text, "%gs:", 4) == 0) {
		op->seg_gs = 1;
		text += 4;
		while (*text == ' ')
			text++;
	}

	/* Immediate: $<value> or $<symbol> */
	if (text[0] == '$') {
		const char *val = text + 1;
		char *end;
		long nv;

		op->kind = OP_IMM;
		op->is_imm = 1;
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
		strncpy(op->sym_buf, val, sizeof(op->sym_buf) - 1);
		op->sym_buf[sizeof(op->sym_buf) - 1] = '\0';
		/* mcc quotes local labels containing dots (e.g. ".Lfp1");
		 * the label definition is unquoted, so strip the quotes. */
		strip_sym_quotes(op->sym_buf);
		/* Split trailing "+N"/"-N" offset (e.g. $sym+4) into addend. */
		split_sym_offset(op->sym_buf, &op->addend);
		op->sym = op->sym_buf;
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
				/* Symbol displacement (e.g. global_var@GOT(%ebx)) */
				char sym_name[128];
				size_t slen = (size_t)(paren - text);
				if (slen >= sizeof(sym_name)) return 0;
				memcpy(sym_name, text, slen);
				sym_name[slen] = '\0';
				/* TLS LE: strip the @ntpoff modifier from a memory
				 * displacement like tls_var@ntpoff(%eax). */
				{
					char *at = strstr(sym_name, "@ntpoff");
					if (at) {
						*at = '\0';
						op->tls_le = 1;
					}
				}
				op->sym = strdup(sym_name);
				if (!op->sym) return 0;
				op->disp = 0;
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
			if (op->seg_gs) {
				/* %gs:0 — TLS thread pointer read: absolute disp32
				 * memory operand with GS prefix. */
				op->kind = OP_MEM;
				op->disp = strtol(text, NULL, 0);
				op->base = -1;
			} else {
				op->kind = OP_IMM;
				op->disp = strtol(text, NULL, 0);
				op->width = 4;
			}
			return 1;
		}
	}

	/* Symbol (plain text, e.g., function label for call/jmp) */
	{
		op->kind = OP_SYMBOL;
		strncpy(op->sym_buf, text, sizeof(op->sym_buf) - 1);
		op->sym_buf[sizeof(op->sym_buf) - 1] = '\0';
		strip_sym_quotes(op->sym_buf);
		/* Split trailing "+N"/"-N" offset (e.g. so+4) into addend. */
		split_sym_offset(op->sym_buf, &op->addend);
		/* TLS LE: strip the @ntpoff modifier; the symbol resolves via
		 * R_386_TLS_LE (TP-relative) instead of an absolute address. */
		{
			char *at = strstr(op->sym_buf, "@ntpoff");
			if (at) {
				*at = '\0';
				op->tls_le = 1;
			}
		}
		op->sym = op->sym_buf;
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
static size_t
emit_modrm_mem(unsigned char **pp, int reg_num, const struct i386_op *op)
{
	unsigned char *p = *pp;
	int base = op->base;
	int index = op->index_reg;
	int scale = op->scale;
	int64_t disp = op->disp;
	int mod;
	size_t disp_offset = 0;

	/* Determine mod field based on displacement */
	if (base < 0) {
		/* Pure absolute address: mod=00 rm=101 + disp32.  Covers both
		 * bare symbols (fixup applied later) and %gs:0 thread-pointer
		 * reads. */
		emit8(p, modrm(0, reg_num & 7, 5));
		p++;
		emit32(p, (uint32_t)(disp & 0xFFFFFFFFULL));
		p += 4;
		*pp = p;
		return 0;
	}
	if (op->sym) {
		/* Symbol displacement: always 32-bit */
		mod = 2;
	} else if (disp == 0 && base != 5)
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
		disp_offset = (size_t)(p - *pp);
		emit32(p, (uint32_t)(disp & 0xFFFFFFFFULL));
		p += 4;
	}

	*pp = p;
	return disp_offset;
}

/* Emit an ALU immediate operation: 0x83 /ext imm8 when the immediate fits
 * in a signed byte, otherwise 0x81 /ext imm32.  `dst` is a register or
 * memory operand (AT&T: add/sub $imm, dst).  Returns bytes emitted or -1. */
static int
emit_alu_imm(unsigned char *p, int ext, const struct i386_op *imm,
             const struct i386_op *dst)
{
	if (imm->kind != OP_IMM)
		return -1;
	if (imm->disp >= -128 && imm->disp <= 127) {
		p[0] = 0x83;
		if (dst->kind == OP_REG) {
			p[1] = modrm(3, ext, dst->reg);
			p[2] = (uint8_t)(imm->disp & 0xFF);
			return 3;
		}
		if (dst->kind == OP_MEM) {
			unsigned char *q = p + 1;
			emit_modrm_mem(&q, ext, dst);
			*q = (uint8_t)(imm->disp & 0xFF);
			q++;
			return (int)(q - p);
		}
	} else {
		p[0] = 0x81;
		if (dst->kind == OP_REG) {
			p[1] = modrm(3, ext, dst->reg);
			emit32(p + 2, (uint32_t)(imm->disp & 0xFFFFFFFFULL));
			return 6;
		}
		if (dst->kind == OP_MEM) {
			unsigned char *q = p + 1;
			emit_modrm_mem(&q, ext, dst);
			emit32(q, (uint32_t)(imm->disp & 0xFFFFFFFFULL));
			q += 4;
			return (int)(q - p);
		}
	}
	return -1;
}

/* Split a "symbol@modifier" reference (e.g. helper@plt) into a bare symbol
 * name and modifier.  The returned name lives in `name_buf`. */
static void
split_symbol_modifier(const char *sym, char *name_buf, size_t name_size,
                      char *mod_buf, size_t mod_size)
{
	const char *at = strchr(sym, '@');
	if (at) {
		size_t sl = (size_t)(at - sym);
		if (sl >= name_size)
			sl = name_size - 1;
		memcpy(name_buf, sym, sl);
		name_buf[sl] = '\0';
		snprintf(mod_buf, mod_size, "%s", at + 1);
	} else {
		snprintf(name_buf, name_size, "%s", sym);
		mod_buf[0] = '\0';
	}
}

/* Set fixup fields on an insn.
 *
 * The symbol is copied to the heap here: the operand text (and even the
 * i386_op array) lives on the encoder's stack and is gone by the time
 * the assembler reads insn.fixup_symbol after i386_encode_insn()
 * returns.  parse_operand first saves the name in op.sym_buf so the
 * value read here is the intact operand string. */
static void
set_fixup(struct mt_insn *out, size_t offset, unsigned width,
          unsigned reloc_type, const char *sym, int64_t addend)
{
	size_t len;
	out->fixed = 0;
	out->fixup_offset = offset;
	out->fixup_width = width;
	out->reloc_type = reloc_type;
	out->fixup_symbol = NULL;
	if (sym) {
		len = strlen(sym);
		out->fixup_symbol = (char *)malloc(len + 1);
		if (out->fixup_symbol)
			memcpy((char *)out->fixup_symbol, sym, len + 1);
	}
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
		/* TLS variant II: %gs: segment override prefix (0x65) precedes
		 * any instruction whose operand carries seg_gs. */
		if (ops[0].seg_gs || ops[1].seg_gs) {
			emit8(p, 0x65);
			p++;
		}
		/* Reloc type: @ntpoff operands resolve TP-relative via
		 * R_386_TLS_LE; plain symbols use an absolute R_386_32. */
		unsigned mov_reloc = (ops[0].tls_le || ops[1].tls_le)
		                         ? R_386_TLS_LE : R_386_32;
		if ((ops[0].kind == OP_IMM || ops[0].kind == OP_SYMBOL) &&
		    ops[0].is_imm && ops[1].kind == OP_REG) {
			/* mov $imm/$sym, reg — opcode 0xB8+reg */
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
		} else if ((ops[0].kind == OP_SYMBOL || ops[0].kind == OP_IMM) &&
		           !ops[0].is_imm && ops[1].kind == OP_REG) {
			/* mov sym, reg — 0x8B /r with disp32 + reloc.
			 * A bare symbol or bare number (no '$') is an absolute
			 * memory load, not an immediate. */
			emit8(p, 0x8B);
			p++;
			emit8(p, modrm(0, ops[1].reg, 5)); /* mod=00 rm=101 disp32 */
			p++;
			offset = (size_t)(p - out->bytes);
			emit32(p, 0);
			p += 4;
			if (ops[0].kind == OP_SYMBOL)
				set_fixup(out, offset, 4, mov_reloc,
				          ops[0].sym, ops[0].addend);
			else
				emit32(p - 4, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_REG && ops[1].kind == OP_SYMBOL &&
		           !ops[1].is_imm) {
			/* mov reg, sym — 0x89 /r with disp32 + reloc.
			 * Store a register to an absolute symbol address. */
			emit8(p, 0x89);
			p++;
			emit8(p, modrm(0, ops[0].reg, 5)); /* mod=00 rm=101 disp32 */
			p++;
			offset = (size_t)(p - out->bytes);
			emit32(p, 0);
			p += 4;
			set_fixup(out, offset, 4, mov_reloc,
			          ops[1].sym, ops[1].addend);
			out->size = (size_t)(p - out->bytes);
			goto done;
		} else if (ops[0].kind == OP_IMM && ops[1].kind == OP_SYMBOL &&
		           !ops[1].is_imm) {
			/* mov $imm, sym — 0xC7 /0 with disp32 + reloc + imm32.
			 * Store an immediate to an absolute symbol address. */
			emit8(p, 0xC7);
			p++;
			emit8(p, modrm(0, 0, 5)); /* mod=00 rm=101 disp32, /0 */
			p++;
			offset = (size_t)(p - out->bytes);
			emit32(p, 0);
			p += 4;
			set_fixup(out, offset, 4, mov_reloc,
			          ops[1].sym, ops[1].addend);
			emit32(p, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
			p += 4;
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
			if (ops[1].sym) {
				size_t fixup_off = (size_t)((p - out->bytes) - 4);
				set_fixup(out, fixup_off, 4, mov_reloc, ops[1].sym, 0);
			}
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
			if (ops[1].sym) {
				size_t fixup_off = (size_t)((p - out->bytes) - 4);
				set_fixup(out, fixup_off, 4, mov_reloc, ops[1].sym, 0);
			}
			goto done;
		} else if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			/* mov (mem), reg — 0x8B */
			emit8(p, 0x8B);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			if (ops[0].sym) {
				size_t fixup_off = (size_t)((p - out->bytes) - 4);
				set_fixup(out, fixup_off, 4, mov_reloc, ops[0].sym, 0);
			}
			goto done;
		}
	}

	/* ---- LEA ---- */
	if (strcmp(base, "lea") == 0 && nops == 2 && ops[1].kind == OP_REG &&
	    (ops[0].kind == OP_MEM ||
	     (ops[0].kind == OP_SYMBOL && !ops[0].is_imm))) {
		/* leal disp(%base,%idx,scale), %reg / leal sym, %reg — 0x8D /r */
		match = 1;
		unsigned lea_reloc = ops[0].tls_le ? R_386_TLS_LE : R_386_32;
		emit8(p, 0x8D);
		p++;
		if (ops[0].kind == OP_SYMBOL) {
			/* Pure symbol: mod=00 rm=101 + disp32 */
			emit8(p, modrm(0, ops[1].reg, 5));
			p++;
			offset = (size_t)(p - out->bytes);
			emit32(p, 0);
			p += 4;
			set_fixup(out, offset, 4, lea_reloc, ops[0].sym, ops[0].addend);
		} else {
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			if (ops[0].sym) {
				size_t fixup_off = (size_t)((p - out->bytes) - 4);
				set_fixup(out, fixup_off, 4, lea_reloc, ops[0].sym, 0);
			}
			goto done;
		}
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- MOVSX / MOVZX ---- */
	/* movsbl/movswl: sign-extend byte/word to long — 0F BE/BF /r.
	 * movzbl/movzwl: zero-extend byte/word to long — 0F B6/B7 /r.
	 * AT&T: movsbl src, dst (src is a byte mem or %al-class reg).
	 * NOTE: the trailing 'l' is consumed as a width suffix, so `base`
	 * arrives here as "movsb"/"movzb". */
	if ((strcmp(base, "movsb") == 0 || strcmp(base, "movzb") == 0 ||
	     strcmp(base, "movsw") == 0 || strcmp(base, "movzw") == 0) &&
	    nops == 2) {
		unsigned opc;
		if (strcmp(base, "movsb") == 0) opc = 0xBE;
		else if (strcmp(base, "movzb") == 0) opc = 0xB6;
		else if (strcmp(base, "movsw") == 0) opc = 0xBF;
		else opc = 0xB7;
		match = 1;
		emit8(p, 0x0F);
		p++;
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			emit8(p, opc);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			if (ops[0].sym) {
				size_t fixup_off = (size_t)((p - out->bytes) - 4);
				set_fixup(out, fixup_off, 4, R_386_32, ops[0].sym, 0);
			}
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			/* movzbl %al, %eax — mod=3 (register form).
			 * ModR/M: reg field = destination, rm field = source.
			 * AT&T `movsbl src, dst` → ops[0]=src, ops[1]=dst. */
			emit8(p, opc);
			emit8(p + 1, modrm(3, ops[1].reg, ops[0].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- XCHG ---- */
	if (strcmp(base, "xchg") == 0 && nops == 2 &&
	    ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
		/* xchgl %src, %dst — 0x87 /r; the swap is symmetric, so
		 * modrm(3, src, dst) matches GNU as for the general form. */
		match = 1;
		emit8(p, 0x87);
		emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- x87 floating point ----
	 * Memory operand forms:  opcode + ModR/M(/ext) [+ disp32 for symbols].
	 * The mnemonic suffix (fldl/flds/fmull/...) selects single/double.
	 * Verified against `as --32`: fldl 8(%ebp) = DD 45 08. */
	if (nops == 1) {
		static const struct { const char *name; unsigned op; int ext; } x87m[] = {
			{"flds", 0xD9, 0}, {"fldl", 0xDD, 0},
			{"fsts", 0xD9, 2}, {"fstl", 0xDD, 2},
			{"fstps", 0xD9, 3}, {"fstpl", 0xDD, 3},
			{"fadds", 0xD8, 0}, {"faddl", 0xDC, 0},
			{"fmuls", 0xD8, 1}, {"fmull", 0xDC, 1},
			{"fsubs", 0xD8, 4}, {"fsubl", 0xDC, 4},
			{"fsubrs", 0xD8, 5}, {"fsubrl", 0xDC, 5},
			{"fdivs", 0xD8, 6}, {"fdivl", 0xDC, 6},
			{"fdivrs", 0xD8, 7}, {"fdivrl", 0xDC, 7},
			{"fcoms", 0xD8, 2}, {"fcoml", 0xDC, 2},
			{"fcomps", 0xD8, 3}, {"fcompl", 0xDC, 3},
			{"filds", 0xDF, 0}, {"fildl", 0xDB, 0}, {"fildll", 0xDF, 5},
			{"fistps", 0xDB, 2}, {"fistpl", 0xDB, 3}, {"fistpll", 0xDF, 7},
			{"fisttps", 0xDB, 1}, {"fisttpl", 0xDB, 1}, {"fisttpll", 0xDD, 1},
			{"fnstenv", 0xD9, 6}, {"fnstcw", 0xD9, 7},
			{"fldcw", 0xD9, 5}, {"fstcw", 0xD9, 7},
		};
		size_t xi;
		for (xi = 0; xi < sizeof(x87m) / sizeof(x87m[0]); ++xi) {
			if (strcmp(mnemonic, x87m[xi].name) != 0)
				continue;
			if (ops[0].kind == OP_SYMBOL) {
				/* Absolute address: mod=00 rm=101 + disp32 */
				match = 1;
				emit8(p, (uint8_t)x87m[xi].op);
				p++;
				emit8(p, modrm(0, x87m[xi].ext, 5));
				p++;
				offset = (size_t)(p - out->bytes);
				emit32(p, 0);
				set_fixup(out, offset, 4, R_386_32,
				          ops[0].sym, ops[0].addend);
				p += 4;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
			if (ops[0].kind == OP_MEM) {
				match = 1;
				emit8(p, (uint8_t)x87m[xi].op);
				p++;
				emit_modrm_mem(&p, x87m[xi].ext, &ops[0]);
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
	}

	/* x87 no-operand / register forms */
	if (strcmp(mnemonic, "fcompp") == 0) {
		match = 1;
		emit8(p, 0xDE);
		emit8(p + 1, 0xD9);
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}
	if (strcmp(mnemonic, "fnstsw") == 0) {
		/* fnstsw %ax — fixed DF E0 encoding */
		match = 1;
		emit8(p, 0xDF);
		emit8(p + 1, 0xE0);
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}
	if (strcmp(mnemonic, "sahf") == 0) {
		match = 1;
		emit8(p, 0x9E);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}
	/* x87 no-operand forms (D9 D8-DF register array).  fchs = D9 E0. */
	if (strcmp(mnemonic, "fchs") == 0) {
		match = 1;
		emit8(p, 0xD9);
		emit8(p + 1, 0xE0);
		p += 2;
		out->size = (size_t)(p - out->bytes);
		goto done;
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

	/* ---- UD2 (undefined instruction, 0F 0B) ---- */
	if (strcmp(mnemonic, "ud2") == 0) {
		match = 1;
		emit8(p, 0x0F);
		emit8(p + 1, 0x0B);
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

	/* ---- CLTD / CDQ (sign-extend eax to edx:eax) ---- */
	if (strcmp(base, "cltd") == 0 || strcmp(base, "cdq") == 0) {
		match = 1;
		emit8(p, 0x99);
		p++;
		out->size = (size_t)(p - out->bytes);
		goto done;
	}

	/* ---- XOR ---- */
	if (strcmp(base, "xor") == 0 && nops == 2) {
		match = 1;  /* unmatched operand forms are rejected by the size-0 guard */
		if (ops[0].kind == OP_IMM &&
		    (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
			int len = emit_alu_imm(p, 6, &ops[0], &ops[1]);
			if (len > 0) {
				p += len;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
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
		match = 1;  /* unmatched operand forms are rejected by the size-0 guard */
		if (ops[0].kind == OP_IMM &&
		    (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
			int len = emit_alu_imm(p, 4, &ops[0], &ops[1]);
			if (len > 0) {
				p += len;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
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
		match = 1;  /* unmatched operand forms are rejected by the size-0 guard */
		if (ops[0].kind == OP_IMM &&
		    (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
			int len = emit_alu_imm(p, 1, &ops[0], &ops[1]);
			if (len > 0) {
				p += len;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
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
		match = 1;  /* unmatched operand forms are rejected by the size-0 guard */
		if (ops[0].kind == OP_IMM &&
		    (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
			int len = emit_alu_imm(p, 0, &ops[0], &ops[1]);
			if (len > 0) {
				p += len;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
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

	/* ---- ADC (add with carry) ---- */
	if (strcmp(base, "adc") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_IMM &&
		    (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
			/* adc $imm, dst — 0x83 /2 (imm8) or 0x81 /2 (imm32) */
			int len = emit_alu_imm(p, 2, &ops[0], &ops[1]);
			if (len > 0) {
				p += len;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x11);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			/* adc (mem), reg — 0x13 /r */
			emit8(p, 0x13);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- SUB ---- */
	if (strcmp(base, "sub") == 0 && nops == 2) {
		match = 1;  /* unmatched operand forms are rejected by the size-0 guard */
		if (ops[0].kind == OP_IMM &&
		    (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
			int len = emit_alu_imm(p, 5, &ops[0], &ops[1]);
			if (len > 0) {
				p += len;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
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

	/* ---- SBB (subtract with borrow) ---- */
	if (strcmp(base, "sbb") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_IMM &&
		    (ops[1].kind == OP_REG || ops[1].kind == OP_MEM)) {
			/* sbb $imm, dst — 0x83 /3 (imm8) or 0x81 /3 (imm32) */
			int len = emit_alu_imm(p, 3, &ops[0], &ops[1]);
			if (len > 0) {
				p += len;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			emit8(p, 0x19);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_MEM && ops[1].kind == OP_REG) {
			/* sbb (mem), reg — 0x1B /r */
			emit8(p, 0x1B);
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

	/* ---- TEST ---- */
	if (strcmp(base, "test") == 0 && nops == 2) {
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			/* testl %src, %dst — 0x85 /r */
			emit8(p, 0x85);
			emit8(p + 1, modrm(3, ops[0].reg, ops[1].reg));
			p += 2;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_IMM && ops[1].kind == OP_REG) {
			/* testl $imm, %reg — 0xF7 /0 */
			emit8(p, 0xF7);
			emit8(p + 1, modrm(3, 0, ops[1].reg));
			emit32(p + 2, (uint32_t)(ops[0].disp & 0xFFFFFFFFULL));
			p += 6;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			/* testl %reg, (mem) — 0x85 /r */
			emit8(p, 0x85);
			p++;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
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
			/* call sym / call sym@plt — 0xE8 rel32 */
			char symname[128];
			char mod[16];
			unsigned rtype;
			split_symbol_modifier(ops[0].sym, symname, sizeof(symname),
			                      mod, sizeof(mod));
			rtype = strcmp(mod, "plt") == 0 ? R_386_PLT32 : R_386_PC32;
			emit8(p, 0xE8);
			p++;
			offset = (size_t)(p - out->bytes);
			emit32(p, 0);
			set_fixup(out, offset, 4, rtype, symname, -4);
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
			/* jmp sym / jmp sym@plt — 0xE9 rel32 */
			char symname[128];
			char mod[16];
			unsigned rtype;
			split_symbol_modifier(ops[0].sym, symname, sizeof(symname),
			                      mod, sizeof(mod));
			rtype = strcmp(mod, "plt") == 0 ? R_386_PLT32 : R_386_PC32;
			emit8(p, 0xE9);
			p++;
			offset = (size_t)(p - out->bytes);
			emit32(p, 0);
			set_fixup(out, offset, 4, rtype, symname, -4);
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
	if ((strncmp(base, "shl", 3) == 0 || strncmp(base, "sal", 3) == 0 ||
	     strncmp(base, "shr", 3) == 0 || strncmp(base, "sar", 3) == 0 ||
	     strcmp(base, "rol") == 0 || strcmp(base, "ror") == 0) && nops == 2) {
		int ext;
		if (strncmp(base, "shl", 3) == 0 || strncmp(base, "sal", 3) == 0)
			ext = 4;
		else if (strncmp(base, "shr", 3) == 0)
			ext = 5;
		else if (strncmp(base, "sar", 3) == 0)
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
		if (ops[0].kind == OP_REG && ops[0].reg == 1 &&
		    (ops[0].width == 1 || ops[0].width == 4) &&
		    ops[1].kind == OP_REG) {
			/* shl %cl, reg — 0xD3 /ext (count lives in CL; accept a
			 * full ECX count operand too, since mcc may emit %ecx). */
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

	/* ---- SHLD / SHRD (double-precision shift) ---- */
	/* AT&T: shldl count, src, dst — count is %cl or $imm.
	 *   %cl form:     0F A5 /r
	 *   $imm8 form:   0F A4 /r imm8
	 * ModR/M reg field = src, rm field = dst. */
	if ((strcmp(base, "shld") == 0 || strcmp(base, "shrd") == 0) && nops == 3) {
		int is_shrd = strcmp(base, "shrd") == 0;
		match = 1;
		if (ops[2].kind == OP_REG && ops[0].kind == OP_REG &&
		    ops[0].reg == 1 && ops[0].width == 1) {
			/* shldl %cl, src, dst — 0F A5 /r */
			emit8(p, 0x0F);
			emit8(p + 1, is_shrd ? 0xAD : 0xA5);
			emit8(p + 2, modrm(3, ops[1].reg, ops[2].reg));
			p += 3;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[2].kind == OP_REG && ops[0].kind == OP_IMM) {
			/* shldl $imm, src, dst — 0F A4 /r imm8 */
			emit8(p, 0x0F);
			emit8(p + 1, is_shrd ? 0xAC : 0xA4);
			emit8(p + 2, modrm(3, ops[1].reg, ops[2].reg));
			emit8(p + 3, (uint8_t)(ops[0].disp & 0xFF));
			p += 4;
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

	/* ---- MOVZB / MOVSB / MOVZW / MOVSW (zero/sign extend) ---- */
	if ((strcmp(base, "movzb") == 0 || strcmp(base, "movsb") == 0 ||
	     strcmp(base, "movzw") == 0 || strcmp(base, "movsw") == 0) && nops == 2) {
		unsigned opcode;
		if (strcmp(base, "movzb") == 0) opcode = 0xB6;
		else if (strcmp(base, "movsb") == 0) opcode = 0xBE;
		else if (strcmp(base, "movzw") == 0) opcode = 0xB7;
		else opcode = 0xBF;
		match = 1;
		if (ops[0].kind == OP_REG && ops[1].kind == OP_REG) {
			/* movzbl/movsbl reg, reg */
			emit8(p, 0x0F);
			emit8(p + 1, opcode);
			emit8(p + 2, modrm(3, ops[1].reg, ops[0].reg));
			p += 3;
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
		if (ops[0].kind == OP_REG && ops[1].kind == OP_MEM) {
			/* movsbl (mem), reg */
			emit8(p, 0x0F);
			emit8(p + 1, opcode);
			p += 2;
			emit_modrm_mem(&p, ops[0].reg, &ops[1]);
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- CMOVcc (conditional move) ---- */
	if (strncmp(base, "cmov", 4) == 0 && nops == 2 &&
	    (ops[1].kind == OP_REG) &&
	    (ops[0].kind == OP_REG || ops[0].kind == OP_MEM)) {
		/* Condition suffix comes from the full mnemonic: the trailing
		 * 'l' width suffix may coincide with a single-letter condition
		 * (cmovl = cmov + l), and the suffix-stripping above has
		 * already trimmed `base` to "cmov". */
		int cond = condition_code(mnemonic + 4);
		if (cond >= 0) {
			match = 1;
			emit8(p, 0x0F);
			p++;
			emit8(p, 0x40 | cond);
			p++;
			if (ops[0].kind == OP_REG) {
				emit8(p, modrm(3, ops[0].reg, ops[1].reg));
				p++;
			} else {
				emit_modrm_mem(&p, ops[1].reg, &ops[0]);
			}
			out->size = (size_t)(p - out->bytes);
			goto done;
		}
	}

	/* ---- SETcc (set byte on condition) ---- */
	if (strncmp(mnemonic, "set", 3) == 0 && nops == 1) {
		int cond = condition_code(mnemonic + 3);
		if (cond >= 0) {
			match = 1;
			if (ops[0].kind == OP_REG) {
				emit8(p, 0x0F);
				emit8(p + 1, 0x90 | cond);
				emit8(p + 2, modrm(3, 0, ops[0].reg));
				p += 3;
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
			if (ops[0].kind == OP_MEM) {
				emit8(p, 0x0F);
				emit8(p + 1, 0x90 | cond);
				p += 2;
				emit_modrm_mem(&p, 0, &ops[0]);
				out->size = (size_t)(p - out->bytes);
				goto done;
			}
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
		if (nops == 2 && ops[1].kind == OP_REG && ops[0].kind == OP_MEM) {
			/* imul (mem), reg — 0x0F 0xAF */
			match = 1;
			emit8(p, 0x0F);
			p++;
			emit8(p, 0xAF);
			p++;
			emit_modrm_mem(&p, ops[1].reg, &ops[0]);
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
	/* Guard against a mnemonic "matched" by a block header but not by any
	 * operand form: previously this silently emitted zero bytes. */
	if (!match || out->size == 0) {
		out->ok = 0;
		out->fixed = 0;
		out->size = 0;
		return -1;
	}
	return 0;
}
