/* aarch64/encode.c — AArch64 instruction encoder.
 *
 * Parses GAS-syntax mnemonic + operands and encodes into
 * fixed-width 4-byte little-endian words.
 *
 * The subset supports what mcc emits for AArch64 — not the full ISA.
 */

#include "mt/target.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- helpers ---- */

static void set_fixup(struct mt_insn *out, size_t offset, unsigned width,
                      unsigned reloc_type, const char *sym, int64_t addend)
{
	out->fixed = 0;
	out->fixup_offset = offset;
	out->fixup_width = width;
	out->reloc_type = reloc_type;
	out->fixup_symbol = sym;
	out->fixup_addend = addend;
}

static void emit32(struct mt_insn *out, size_t *off, uint32_t val)
{
	out->bytes[(*off)++] = (uint8_t)(val);
	out->bytes[(*off)++] = (uint8_t)(val >> 8);
	out->bytes[(*off)++] = (uint8_t)(val >> 16);
	out->bytes[(*off)++] = (uint8_t)(val >> 24);
}

/* ---- operand parsing (GAS AArch64 syntax) ---- */

/* Register kinds */
#define XREG 0  /* x0-x30, xzr, sp, lr, fp */
#define WREG 1  /* w0-w30, wzr */
#define SREG 2  /* s0-s31 (float) */
#define DREG 3  /* d0-d31 (double) */
#define REG_ZR 31
#define REG_SP 31
#define REG_FP 29
#define REG_LR 30

struct operand {
	int kind;       /* 'r' = register, 'i' = immediate, 'l' = label, 'm' = memory */
	int reg;        /* register number, -1 if none */
	int wreg;       /* 0=x register, 1=w register, 2=s, 3=d */
	int64_t imm;    /* immediate value */
	int shift;      /* shift type: 0=none, 1=lsl */
	int shift_imm;  /* shift amount */
	char *symbol;   /* symbol name (malloc'd) — or NULL */
	const char *sym_start; /* pointer into original operands for symbol */
	char modifier[32]; /* e.g. "lo12" */
};

/* Parse a register name. Returns reg number or -1 on error. */
static int parse_reg(const char *s, int *wreg)
{
	if (!s || !*s) return -1;

	/* x0-x30 */
	if (s[0] == 'x' && s[1] >= '0' && s[1] <= '9') {
		int n = 0;
		int i = 1;
		while (s[i] >= '0' && s[i] <= '9') {
			n = n * 10 + (s[i] - '0');
			i++;
		}
		if (s[i] == '\0' && n >= 0 && n <= 30) {
			*wreg = XREG;
			return n;
		}
	}

	/* w0-w30 */
	if (s[0] == 'w' && s[1] >= '0' && s[1] <= '9') {
		int n = 0;
		int i = 1;
		while (s[i] >= '0' && s[i] <= '9') {
			n = n * 10 + (s[i] - '0');
			i++;
		}
		if (s[i] == '\0' && n >= 0 && n <= 30) {
			*wreg = WREG;
			return n;
		}
	}

	/* s0-s31 */
	if (s[0] == 's' && s[1] >= '0' && s[1] <= '9') {
		int n = 0;
		int i = 1;
		while (s[i] >= '0' && s[i] <= '9') {
			n = n * 10 + (s[i] - '0');
			i++;
		}
		if (s[i] == '\0' && n >= 0 && n <= 31) {
			*wreg = SREG;
			return n;
		}
	}

	/* d0-d31 */
	if (s[0] == 'd' && s[1] >= '0' && s[1] <= '9') {
		int n = 0;
		int i = 1;
		while (s[i] >= '0' && s[i] <= '9') {
			n = n * 10 + (s[i] - '0');
			i++;
		}
		if (s[i] == '\0' && n >= 0 && n <= 31) {
			*wreg = DREG;
			return n;
		}
	}

	/* sp */
	if (strcmp(s, "sp") == 0) { *wreg = XREG; return REG_SP; }
	/* fp / x29 */
	if (strcmp(s, "fp") == 0 || strcmp(s, "x29") == 0) { *wreg = XREG; return REG_FP; }
	/* lr / x30 */
	if (strcmp(s, "lr") == 0 || strcmp(s, "x30") == 0) { *wreg = XREG; return REG_LR; }
	/* xzr / wzr */
	if (strcmp(s, "xzr") == 0) { *wreg = XREG; return REG_ZR; }
	if (strcmp(s, "wzr") == 0) { *wreg = WREG; return REG_ZR; }

	return -1;
}

/* Parse an optional leading '#' from immediate */
static const char *skip_hash(const char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '#') s++;
	return s;
}

static int parse_imm(const char *s, int64_t *val)
{
	char *end;
	s = skip_hash(s);
	if (!*s) return -1;
	*val = (int64_t)strtoll(s, &end, 0);
	return (*end == '\0' || *end == ' ') ? 0 : -1;
}

/* Trim leading whitespace */
static const char *trim_start(const char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	return s;
}

/* Check if the operand text starts with a symbol (not a register/immediate) */
static int is_symbol(const char *s)
{
	s = trim_start(s);
	if (!*s) return 0;
	/* Register? */
	if ((s[0] == 'x' || s[0] == 'w' || s[0] == 's' || s[0] == 'd') &&
	    s[1] >= '0' && s[1] <= '9') return 0;
	if (strncmp(s, "sp", 2) == 0 && (s[2] == '\0' || s[2] == ',' || s[2] == ' ')) return 0;
	if (strncmp(s, "fp", 2) == 0 && (s[2] == '\0' || s[2] == ',' || s[2] == ' ')) return 0;
	if (strncmp(s, "lr", 2) == 0 && (s[2] == '\0' || s[2] == ',' || s[2] == ' ')) return 0;
	if (strncmp(s, "xzr", 3) == 0 && (s[3] == '\0' || s[3] == ',' || s[3] == ' ')) return 0;
	if (strncmp(s, "wzr", 3) == 0 && (s[3] == '\0' || s[3] == ',' || s[3] == ' ')) return 0;
	/* Immediate? */
	if (*s == '#' || *s == '-' || (*s >= '0' && *s <= '9') || *s == ':') return 0;
	return 1;
}

/* ---- main encoder ---- */

int
aarch64_encode_insn(const struct mt_target *target,
                    const char *mnemonic, const char *operands,
                    struct mt_insn *out)
{
	(void)target;
	memset(out, 0, sizeof(*out));
	out->fixed = 1;
	out->size = 4;  /* aarch64 instructions are always 4 bytes */

	struct operand ops[4];
	int nops = 0;
	char opbuf[4][64];
	const char *end;
	const char *optext;
	int i;

	/* Split operands on ',' */
	if (operands && *operands) {
		optext = operands;
		for (nops = 0; nops < 4; nops++) {
			optext = trim_start(optext);
			if (!*optext) break;
			end = strchr(optext, ',');
			if (!end) {
				size_t len = strlen(optext);
				if (len >= sizeof(opbuf[0])) return -1;
				memcpy(opbuf[nops], optext, len);
				opbuf[nops][len] = '\0';
				nops++;
				break;
			}
			size_t len = (size_t)(end - optext);
			if (len >= sizeof(opbuf[0])) return -1;
			memcpy(opbuf[nops], optext, len);
			opbuf[nops][len] = '\0';
			optext = end + 1;
		}
	}

	/* Parse each operand */
	for (i = 0; i < nops; i++) {
		char *s = opbuf[i];
		while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
		char *e = s + strlen(s);
		while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';

		memset(&ops[i], 0, sizeof(ops[i]));

		/* Check for modifier: #:lo12: or #:got_lo12: or similar */
		if (strstr(s, ":") != NULL && s[0] != '"') {
			char *mod = strstr(s, ":");
			if (mod > s && mod[-1] == '#') mod--;
			/* Extract modifier for later use */
		}

		/* Memory operand: [base, #offset] or [base] or [base, reg] */
		if (s[0] == '[') {
			char *close = strchr(s, ']');
			if (!close) return -1;
			*close = '\0';
			char *inside = s + 1;
			while (*inside == ' ') inside++;
			char *comma = strchr(inside, ',');
			ops[i].kind = 'm';
			if (comma) {
				*comma++ = '\0';
				while (*comma == ' ') comma++;
				/* Register offset or immediate offset */
				int wreg2;
				int reg = parse_reg(trim_start(comma), &wreg2);
				if (reg >= 0) {
					ops[i].imm = -1; /* flag: register offset */
					ops[i].reg = reg;
					ops[i].wreg = wreg2;
				} else {
					const char *imm_s = skip_hash(comma);
					if (parse_imm(imm_s, &ops[i].imm) != 0) return -1;
				}
			} else {
				ops[i].imm = 0;
			}
			int wreg2;
			int base_reg = parse_reg(trim_start(inside), &wreg2);
			if (base_reg < 0) return -1;
			ops[i].reg = base_reg;
			ops[i].wreg = wreg2;
			continue;
		}

		/* Check for immediate (starts with # or digit or -) */
		const char *trimmed = s;
		while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
		if (*trimmed == '#' || *trimmed == '-' || (*trimmed >= '0' && *trimmed <= '9')) {
			const char *imm_s = skip_hash(trimmed);
			if (parse_imm(imm_s, &ops[i].imm) == 0) {
				ops[i].kind = 'i';
				continue;
			}
		}

		/* Check for register */
		int wreg;
		int reg = parse_reg(trim_start(s), &wreg);
		if (reg >= 0) {
			ops[i].kind = 'r';
			ops[i].reg = reg;
			ops[i].wreg = wreg;
			continue;
		}

		/* Check for symbol (potentially with modifier like :lo12:) */
		if (is_symbol(s) || (strchr(s, ':') != NULL)) {
			ops[i].kind = 'l';
			/* Check for modifier pattern like #:lo12:sym or :got:sym */
			char *colon = strchr(s, ':');
			if (colon) {
				char *end_mod = strchr(colon + 1, ':');
				if (end_mod) {
					size_t mlen = (size_t)(end_mod - colon - 1);
					if (mlen < sizeof(ops[i].modifier)) {
						memcpy(ops[i].modifier, colon + 1, mlen);
						ops[i].modifier[mlen] = '\0';
						ops[i].sym_start = end_mod + 1;
					}
				}
			} else {
				/* Simple symbol name — point into trim(ed) opbuf */
				ops[i].sym_start = s;
			}
			continue;
		}

		/* If nothing matched, it's invalid */
		return -1;
	}

	size_t off = 0;

	/* ---- instruction dispatch ---- */

	/* ret */
	if (strcmp(mnemonic, "ret") == 0 && nops == 0) {
		emit32(out, &off, 0xD65F03C0);
		return 0;
	}

	/* br xn */
	if (strcmp(mnemonic, "br") == 0 && nops == 1 && ops[0].kind == 'r') {
		emit32(out, &off, 0xD61F0000 | ((unsigned)ops[0].reg & 0x1F));
		return 0;
	}

	/* blr xn */
	if (strcmp(mnemonic, "blr") == 0 && nops == 1 && ops[0].kind == 'r') {
		emit32(out, &off, 0xD63F0000 | ((unsigned)ops[0].reg & 0x1F));
		return 0;
	}

	/* mov rd, rm (register) */
	if (strcmp(mnemonic, "mov") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rm = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xAA0003E0 : 0x2A0003E0) |
		       ((unsigned)rm & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* mov rd, #imm (move immediate via ORR) */
	if (strcmp(mnemonic, "mov") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'i') {
		int rd = ops[0].reg;
		uint64_t val = (uint64_t)ops[1].imm;

		if (val <= 0xFFFF) {
			/* Use MOVZ: 1 10 1 0 0 hw imm16 Rd
			 * MOVZ: sf=1, opc=10, hw=00, imm16=val, Rd=rd */
			uint32_t enc = 0xD2800000 |
				((unsigned)(val & 0xFFFF) << 5) |
				((unsigned)rd & 0x1F);
			emit32(out, &off, enc);
			return 0;
		}
		if ((val & 0xFFFFFFFFFFFF0000ULL) == 0) {
			/* MOVZ with lsl #16 */
			uint32_t enc = 0xD2A00000 |
				((unsigned)((val >> 16) & 0xFFFF) << 5) |
				((unsigned)rd & 0x1F);
			emit32(out, &off, enc);
			return 0;
		}
		return -1; /* imm too large for now */
	}

	/* movz rd, #imm{, lsl #N} */
	if ((strcmp(mnemonic, "movz") == 0) && nops >= 2 && nops <= 4 &&
	    ops[0].kind == 'r' && ops[1].kind == 'i') {
		int rd = ops[0].reg;
		uint64_t imm = (uint64_t)ops[1].imm;
		int shift = 0;
		if (nops >= 3 && ops[2].kind == 'i') shift = (int)ops[2].imm;
		if (nops >= 4 && ops[3].kind == 'i' && ops[3].imm == 12) shift = (int)ops[3].imm; /* lsl #12 */
		int hw = shift / 16;
		if (hw > 3 || (shift % 16) != 0) return -1;
		uint32_t enc = 0xD2800000 | ((unsigned)hw << 21) |
			((unsigned)(imm & 0xFFFF) << 5) |
			((unsigned)rd & 0x1F);
		emit32(out, &off, enc);
		return 0;
	}

	/* movk rd, #imm{, lsl #N} */
	if ((strcmp(mnemonic, "movk") == 0) && nops >= 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'i') {
		int rd = ops[0].reg;
		uint64_t imm = (uint64_t)ops[1].imm;
		int shift = 0;
		if (nops >= 3 && ops[2].kind == 'i') shift = (int)ops[2].imm;
		int hw = shift / 16;
		if (hw > 3 || (shift % 16) != 0) return -1;
		uint32_t enc = 0xF2800000 | ((unsigned)hw << 21) |
			((unsigned)(imm & 0xFFFF) << 5) |
			((unsigned)rd & 0x1F);
		emit32(out, &off, enc);
		return 0;
	}

	/* add rd, rn, #imm */
	if (strcmp(mnemonic, "add") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' &&
	    ops[2].kind == 'i') {
		int rd = ops[0].reg, rn = ops[1].reg;
		uint64_t imm = (uint64_t)ops[2].imm;
		int shift = 0;
		if (imm > 0xFFF) {
			if ((imm & ~0xFFF000ULL) == 0) {
				shift = 12;
				imm >>= 12;
			} else return -1;
		}
		uint32_t enc = 0x91000000 |
			((unsigned)shift << 22) |
			((unsigned)(imm & 0xFFF) << 10) |
			(((unsigned)rn & 0x1F) << 5) |
			((unsigned)rd & 0x1F);
		emit32(out, &off, enc);
		return 0;
	}

	/* add rd, rn, rm (register) */
	if (strcmp(mnemonic, "add") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x8B000000 : 0x0B000000) |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* sub rd, rn, #imm */
	if (strcmp(mnemonic, "sub") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'i') {
		int rd = ops[0].reg, rn = ops[1].reg;
		uint64_t imm = (uint64_t)ops[2].imm;
		int shift = 0;
		if (imm > 0xFFF) {
			if ((imm & ~0xFFF000ULL) == 0) {
				shift = 12;
				imm >>= 12;
			} else return -1;
		}
		uint32_t enc = 0xD1000000 | ((unsigned)shift << 22) |
			((unsigned)(imm & 0xFFF) << 10) |
			(((unsigned)rn & 0x1F) << 5) |
			((unsigned)rd & 0x1F);
		emit32(out, &off, enc);
		return 0;
	}

	/* sub rd, rn, rm (register) */
	if ((strcmp(mnemonic, "sub") == 0 || strcmp(mnemonic, "cmp") == 0) &&
	    nops == 3 && ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int is_cmp = (strcmp(mnemonic, "cmp") == 0);
		int rd = is_cmp ? REG_ZR : ops[0].reg;
		int rn = is_cmp ? ops[0].reg : ops[1].reg;
		int rm = is_cmp ? ops[1].reg : ops[2].reg;
		emit32(out, &off, 0xCB000000 |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* cmp rn, rm (2-arg form) */
	if (strcmp(mnemonic, "cmp") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rn = ops[0].reg, rm = ops[1].reg;
		emit32(out, &off, 0xEB00001F |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5));
		return 0;
	}

	/* cmp rn, #imm */
	if (strcmp(mnemonic, "cmp") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'i') {
		int rn = ops[0].reg;
		uint64_t imm = (uint64_t)ops[1].imm;
		int shift = 0;
		if (imm > 0xFFF) {
			if ((imm & ~0xFFF000ULL) == 0) { shift = 12; imm >>= 12; } else return -1;
		}
		emit32(out, &off, 0xF100001F | ((unsigned)shift << 22) |
		       ((unsigned)(imm & 0xFFF) << 10) |
		       (((unsigned)rn & 0x1F) << 5));
		return 0;
	}

	/* ldr xt, [base] — or with immediate offset */
	if ((strcmp(mnemonic, "ldr") == 0 || strcmp(mnemonic, "ldrb") == 0 ||
	     strcmp(mnemonic, "ldrh") == 0 || strcmp(mnemonic, "ldrsw") == 0 ||
	     strcmp(mnemonic, "ldrsb") == 0 || strcmp(mnemonic, "ldrsh") == 0) &&
	    nops == 2 && ops[0].kind == 'r' && ops[1].kind == 'm') {
		unsigned rt = (unsigned)ops[0].reg;
		unsigned rn = (unsigned)ops[1].reg;
		int wreg = ops[0].wreg;
		int is64 = (wreg == XREG);

		if (ops[1].imm >= 0) {
			/* Scalar immediate offset */
			unsigned size = 0;
			uint32_t base_op = 0;
			if (strcmp(mnemonic, "ldr") == 0) {
				size = is64 ? 3 : 2;
				base_op = is64 ? 0xF9400000 : 0xB9400000;
			} else if (strcmp(mnemonic, "ldrb") == 0) {
				size = 0; base_op = 0x39400000;
			} else if (strcmp(mnemonic, "ldrh") == 0) {
				size = 1; base_op = 0x39400000; /* actually 0x79400000 for LDRH */
				base_op = 0x79400000;
			} else if (strcmp(mnemonic, "ldrsw") == 0) {
				size = 2; base_op = 0xB9800000;
			} else return -1;

			uint64_t imm = (uint64_t)ops[1].imm;
			uint64_t scaled = imm >> size;
			if (scaled > 0xFFF || (imm & ((1ULL << size) - 1)) != 0) return -1;
			emit32(out, &off, base_op | ((unsigned)scaled << 10) |
			       (rn << 5) | rt);
			return 0;
		} else {
			/* Register offset */
			unsigned rm = (unsigned)ops[1].reg;
			uint32_t base_op;
			if (strcmp(mnemonic, "ldr") == 0)
				base_op = is64 ? 0xF8606800 : 0xB8606800;
			else if (strcmp(mnemonic, "ldrb") == 0)
				base_op = 0x38606800;
			else
				return -1;
			emit32(out, &off, base_op | (rm << 16) | (rn << 5) | rt);
			return 0;
		}
	}

	/* str xt, [base, #imm] */
	if ((strcmp(mnemonic, "str") == 0 || strcmp(mnemonic, "strb") == 0 ||
	     strcmp(mnemonic, "strh") == 0) &&
	    nops == 2 && ops[0].kind == 'r' && ops[1].kind == 'm') {
		unsigned rt = (unsigned)ops[0].reg;
		unsigned rn = (unsigned)ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		unsigned size = 0;
		uint32_t base_op;
		if (strcmp(mnemonic, "str") == 0) {
			size = is64 ? 3 : 2;
			base_op = is64 ? 0xF9000000 : 0xB9000000;
		} else if (strcmp(mnemonic, "strb") == 0) {
			size = 0; base_op = 0x39000000;
		} else if (strcmp(mnemonic, "strh") == 0) {
			size = 1; base_op = 0x79000000;
		} else return -1;

		if (ops[1].imm >= 0) {
			uint64_t imm = (uint64_t)ops[1].imm;
			uint64_t scaled = imm >> size;
			if (scaled > 0xFFF || (imm & ((1ULL << size) - 1)) != 0) return -1;
			emit32(out, &off, base_op | ((unsigned)scaled << 10) |
			       (rn << 5) | rt);
			return 0;
		}
		return -1;
	}

	/* orr rd, rn, rm */
	if ((strcmp(mnemonic, "orr") == 0 || strcmp(mnemonic, "mov") == 0) &&
	    nops == 3 && ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		/* Also handle mov rd, rm (3-arg form as orr rd, xzr, rm) */
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xAA000000 : 0x2A000000) |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* eor rd, rn, rm */
	if (strcmp(mnemonic, "eor") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xCA000000 : 0x4A000000) |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* and rd, rn, rm */
	if (strcmp(mnemonic, "and") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x8A000000 : 0x0A000000) |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* mul rd, rn, rm */
	if (strcmp(mnemonic, "mul") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x9B007C00 : 0x1B007C00) |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* sdiv rd, rn, rm */
	if (strcmp(mnemonic, "sdiv") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		emit32(out, &off, 0x9AC00C00 |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* udiv rd, rn, rm */
	if (strcmp(mnemonic, "udiv") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		emit32(out, &off, 0x9AC00800 |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* neg rd, rm */
	if (strcmp(mnemonic, "neg") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rm = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xCB0003E0 : 0x4B0003E0) |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* lsl rd, rn, rm */
	if ((strcmp(mnemonic, "lsl") == 0) && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		/* LSL is UBFM rd, rn, (-shift % 64), (shift - 1) for immediate,
		 * or LSLV for register. Use LSLV: 1 0 11010110 00000 001000 rm rn rd */
		emit32(out, &off, 0x9AC02000 |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* lsr rd, rn, rm */
	if ((strcmp(mnemonic, "lsr") == 0) && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		emit32(out, &off, 0x9AC02400 |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* asr rd, rn, rm */
	if ((strcmp(mnemonic, "asr") == 0) && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		emit32(out, &off, 0x9AC02800 |
		       ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* sxtb rd, rn */
	if (strcmp(mnemonic, "sxtb") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x93401C00 |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* sxth rd, rn */
	if (strcmp(mnemonic, "sxth") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x93403C00 |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* sxtw rd, rn */
	if (strcmp(mnemonic, "sxtw") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x93407C00 |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* uxtb rd, rn */
	if (strcmp(mnemonic, "uxtb") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x53001C00 |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* uxth rd, rn */
	if (strcmp(mnemonic, "uxth") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x53003C00 |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* fmov rd, rn  (scalar float) */
	if (strcmp(mnemonic, "fmov") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		int is_double = (ops[0].wreg == DREG);
		emit32(out, &off, (is_double ? 0x1E604000 : 0x1E204000) |
		       ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* fadd/fsub/fmul/fdiv/fsqrt: only scalar double for now */
	if ((strcmp(mnemonic, "fadd") == 0 || strcmp(mnemonic, "fsub") == 0 ||
	     strcmp(mnemonic, "fmul") == 0 || strcmp(mnemonic, "fdiv") == 0) &&
	    nops == 3 && ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		uint32_t base;
		if (strcmp(mnemonic, "fadd") == 0) base = 0x1E602800;
		else if (strcmp(mnemonic, "fsub") == 0) base = 0x1E603800;
		else if (strcmp(mnemonic, "fmul") == 0) base = 0x1E600800;
		else base = 0x1E601800;
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		emit32(out, &off, base | ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5) |
		       (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* fneg rd, rn */
	if (strcmp(mnemonic, "fneg") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x1E614000 |
		       ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		return 0;
	}

	/* fcvt rd, rn */
	if (strcmp(mnemonic, "fcvt") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		if (ops[0].wreg == DREG && ops[1].wreg == SREG) {
			emit32(out, &off, 0x1E22C000 |
			       ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else if (ops[0].wreg == SREG && ops[1].wreg == DREG) {
			emit32(out, &off, 0x1E624000 |
			       ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else return -1;
		return 0;
	}

	/* fcvtzs rd, rn (float to signed int) */
	if (strcmp(mnemonic, "fcvtzs") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		if (ops[1].wreg == DREG) {
			emit32(out, &off, 0x9E780000 | ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else if (ops[1].wreg == SREG) {
			emit32(out, &off, 0x1E380000 | ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else return -1;
		return 0;
	}

	/* fcvtzu rd, rn */
	if (strcmp(mnemonic, "fcvtzu") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		if (ops[1].wreg == DREG) {
			emit32(out, &off, 0x9E790000 | ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else if (ops[1].wreg == SREG) {
			emit32(out, &off, 0x1E390000 | ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else return -1;
		return 0;
	}

	/* scvtf rd, rn (signed int to float) */
	if (strcmp(mnemonic, "scvtf") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		if (ops[0].wreg == DREG) {
			emit32(out, &off, 0x9E620000 | ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else if (ops[0].wreg == SREG) {
			emit32(out, &off, 0x1E220000 | ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else return -1;
		return 0;
	}

	/* ucvtf rd, rn */
	if (strcmp(mnemonic, "ucvtf") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		if (ops[0].wreg == DREG) {
			emit32(out, &off, 0x9E630000 | ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else if (ops[0].wreg == SREG) {
			emit32(out, &off, 0x1E230000 | ((unsigned)rn & 0x1F) | (((unsigned)rd & 0x1F) << 16));
		} else return -1;
		return 0;
	}

	/* mrs rd, sysreg */
	if (strcmp(mnemonic, "mrs") == 0 && nops == 2 &&
	    ops[0].kind == 'r') {
		int rd = ops[0].reg;
		const char *regname = opbuf[1];
		while (*regname == ' ' || *regname == '\t') regname++;
		/* Only tpidr_el0 for now */
		if (strcmp(regname, "tpidr_el0") == 0) {
			emit32(out, &off, 0xD53BD040 | (((unsigned)rd & 0x1F) << 16));
			return 0;
		}
		return -1;
	}

	/* svc #imm */
	if (strcmp(mnemonic, "svc") == 0 && nops == 1 && ops[0].kind == 'i') {
		uint32_t imm = (uint32_t)ops[0].imm;
		if (imm > 0xFFFF) return -1;
		emit32(out, &off, 0xD4000001 | (imm << 5));
		return 0;
	}

	/* nop */
	if (strcmp(mnemonic, "nop") == 0 && nops == 0) {
		emit32(out, &off, 0xD503201F);
		return 0;
	}

	/* cset rd, cond */
	if (strcmp(mnemonic, "cset") == 0 && nops == 2 &&
	    ops[0].kind == 'r') {
		static const struct { const char *name; unsigned cond; } conds[] = {
			{"eq", 0}, {"ne", 1}, {"cs", 2}, {"cc", 3},
			{"mi", 4}, {"pl", 5}, {"vs", 6}, {"vc", 7},
			{"hi", 8}, {"ls", 9}, {"ge", 10}, {"lt", 11},
			{"gt", 12}, {"le", 13}, {"hs", 2}, {"lo", 3},
			{NULL, 0}
		};
		const char *cname = opbuf[1];
		while (*cname == ' ' || *cname == '\t') cname++;
		unsigned cond = 0;
		int found = 0;
		for (int ci = 0; conds[ci].name; ci++) {
			if (strcmp(cname, conds[ci].name) == 0) {
				cond = conds[ci].cond;
				found = 1;
				break;
			}
		}
		if (!found) return -1;
		int rd = ops[0].reg;
		emit32(out, &off, 0x1A9F07E0 | (cond << 12) | ((unsigned)rd & 0x1F));
		return 0;
	}

	/* adrp rd, label */
	if ((strcmp(mnemonic, "adrp") == 0 || strcmp(mnemonic, "adr") == 0) &&
	    nops == 2 && ops[0].kind == 'r' && ops[1].kind == 'l') {
		int rd = ops[0].reg;
		/* Emit placeholder, set fixup */
		emit32(out, &off, (strcmp(mnemonic, "adrp") == 0 ? 0x90000000 : 0x10000000) |
		       ((unsigned)rd & 0x1F));

		unsigned reloc = (strcmp(mnemonic, "adrp") == 0) ?
			275 : 274; /* R_AARCH64_ADR_PREL_PG_HI21, ADR_PREL_LO21 */
		int64_t addend = ops[1].imm;
		const char *sym = ops[1].sym_start;

		set_fixup(out, 0, 4, reloc, sym, addend);
		return 0;
	}

	/* b label / bl label */
	if ((strcmp(mnemonic, "b") == 0 || strcmp(mnemonic, "bl") == 0) &&
	    nops == 1 && ops[0].kind == 'l') {
		uint32_t base = strcmp(mnemonic, "bl") == 0 ? 0x94000000 : 0x14000000;
		emit32(out, &off, base);

		const char *sym = ops[0].sym_start;
		set_fixup(out, 0, 4, 283, sym, 0); /* R_AARCH64_CALL26 */
		return 0;
	}

	/* b.cond label */
	if (mnemonic[0] == 'b' && mnemonic[1] == '.' && nops == 1 && ops[0].kind == 'l') {
		static const struct { const char *name; unsigned cond; } conds[] = {
			{"eq", 0}, {"ne", 1}, {"cs", 2}, {"cc", 3},
			{"mi", 4}, {"pl", 5}, {"vs", 6}, {"vc", 7},
			{"hi", 8}, {"ls", 9}, {"ge", 10}, {"lt", 11},
			{"gt", 12}, {"le", 13}, {"hs", 2}, {"lo", 3},
			{NULL, 0}
		};
		const char *cname = mnemonic + 2;
		unsigned cond = 0;
		int found = 0;
		for (int ci = 0; conds[ci].name; ci++) {
			if (strcmp(cname, conds[ci].name) == 0) {
				cond = conds[ci].cond;
				found = 1;
				break;
			}
		}
		if (!found) return -1;
		uint32_t base = 0x54000000 | (cond << 12);
		emit32(out, &off, base);
		const char *sym = ops[0].sym_start;
		set_fixup(out, 0, 4, 279, sym, 0); /* R_AARCH64_CONDBR19 */
		return 0;
	}

	/* cbz/cbnz rt, label */
	if ((strcmp(mnemonic, "cbz") == 0 || strcmp(mnemonic, "cbnz") == 0) &&
	    nops == 2 && ops[0].kind == 'r' && ops[1].kind == 'l') {
		int rt = ops[0].reg;
		uint32_t base = strcmp(mnemonic, "cbnz") == 0 ? 0x35000000 : 0x34000000;
		emit32(out, &off, base | ((unsigned)rt & 0x1F));
		const char *sym = ops[1].sym_start;
		set_fixup(out, 0, 4, 279, sym, 0); /* R_AARCH64_CONDBR19 */
		return 0;
	}

	/* fcmpe rn, rm */
	if (strcmp(mnemonic, "fcmpe") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rn = ops[0].reg, rm = ops[1].reg;
		emit32(out, &off, 0x1E6020A0 | ((unsigned)rm & 0x1F) |
		       (((unsigned)rn & 0x1F) << 5));
		return 0;
	}

	return -1; /* unsupported */
}
