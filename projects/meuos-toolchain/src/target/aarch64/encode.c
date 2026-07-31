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
#define QREG 4  /* q0-q31 (128-bit NEON) */
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
	int writeback;  /* 0=none, 1=pre-indexed (!), 2=post-indexed (, offset) */
	int is_sp;      /* 1 when the register text was exactly "sp" (31 both
	                 * means sp and xzr, but they encode differently) */
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

	/* q0-q31 */
	if (s[0] == 'q' && s[1] >= '0' && s[1] <= '9') {
		int n = 0;
		int i = 1;
		while (s[i] >= '0' && s[i] <= '9') {
			n = n * 10 + (s[i] - '0');
			i++;
		}
		if (s[i] == '\0' && n >= 0 && n <= 31) {
			*wreg = QREG;
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

/* Strip surrounding double quotes from a symbol name.  mcc quotes symbol
 * names that contain '.' or other characters (e.g. adrp x0, ".Lfp0").
 * The text lives in the operand scratch buffer, so it can be modified. */
static void
strip_quotes(char *s)
{
	size_t n;
	if (!s)
		return;
	n = strlen(s);
	if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
		memmove(s, s + 1, n - 2);
		s[n - 2] = '\0';
	}
}

/* Check if the operand text starts with a symbol (not a register/immediate) */
static int is_symbol(const char *s)
{
	s = trim_start(s);
	if (!*s) return 0;
	/* Register? */
	if ((s[0] == 'x' || s[0] == 'w' || s[0] == 's' || s[0] == 'd' || s[0] == 'q') &&
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

	/* Split operands on ',' — but NOT inside [...] brackets */
	if (operands && *operands) {
		optext = operands;
		for (nops = 0; nops < 4; nops++) {
			optext = trim_start(optext);
			if (!*optext) break;
			/* Find the next comma that is NOT inside [...] */
			end = optext;
			while (*end) {
				if (*end == '[') {
					end = strchr(end, ']');
					if (!end) return -1; /* unclosed bracket */
				} else if (*end == ',') {
					break;
				}
				end++;
			}
			if (!*end) {
				/* No more commas — this is the last operand */
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

		/* Memory operand: [base, #offset] or [base] or [base, reg]
		 * Pre-indexed: [base, #imm]!  → writeback=1
		 * Post-indexed: [base], #imm  → handled via nops==4 in dispatch */
		if (s[0] == '[') {
			char *close = strchr(s, ']');
			if (!close) return -1;
			if (close[1] == '!') {
				ops[i].writeback = 1;
			}
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
					/* Check for symbol reference: #:modifier:symbol */
					if (*imm_s == ':') {
						const char *mod_end = strchr(imm_s + 1, ':');
						if (mod_end) {
							size_t mlen = (size_t)(mod_end - imm_s - 1);
							if (mlen < sizeof(ops[i].modifier)) {
								memcpy(ops[i].modifier, imm_s + 1, mlen);
								ops[i].modifier[mlen] = '\0';
							}
							ops[i].sym_start = mod_end + 1;
							strip_quotes((char *)ops[i].sym_start);
							/* Symbol ref in offset: flag by setting imm=1, kind stays 'm' */
							ops[i].imm = 1; /* non-zero to indicate symbol offset */
						} else return -1;
					} else if (parse_imm(imm_s, &ops[i].imm) != 0) return -1;
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
		const char *regtext = trim_start(s);
		int reg = parse_reg(regtext, &wreg);
		if (reg >= 0) {
			ops[i].kind = 'r';
			ops[i].reg = reg;
			ops[i].wreg = wreg;
			ops[i].is_sp = (strcmp(regtext, "sp") == 0);
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
						strip_quotes((char *)ops[i].sym_start);
					}
				}
			} else {
				/* Simple symbol name — point into trim(ed) opbuf */
				ops[i].sym_start = s;
				strip_quotes((char *)ops[i].sym_start);
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
		if (ops[1].is_sp && is64) {
			/* mov xd, sp — alias of add xd, sp, #0 (ORR cannot
			 * encode SP in Rm). */
			emit32(out, &off, 0x910003E0 | ((unsigned)rd & 0x1F));
			return 0;
		}
		/* mov rd, rm — ORR rd, xzr, rm: Rd[4:0], Rm[20:16] */
		emit32(out, &off, (is64 ? 0xAA0003E0 : 0x2A0003E0) |
		       (((unsigned)rm & 0x1F) << 16) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* mov rd, #imm (move immediate via ORR) */
	if (strcmp(mnemonic, "mov") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'i') {
		int rd = ops[0].reg;
		uint64_t val = (uint64_t)ops[1].imm;
		int is64 = (ops[0].wreg == XREG);

		/* For w-registers, zero-extend to 32-bit */
		if (!is64) val &= 0xFFFFFFFFULL;

		/* Try single MOVZ/MOVN first */
		int max_hw = is64 ? 4 : 2;
		for (int hw = 0; hw < max_hw; hw++) {
			uint64_t mask = (uint64_t)0xFFFF << (hw * 16);
			if ((val & ~mask) == 0) {
				uint32_t enc = 0xD2800000 |
					((unsigned)hw << 21) |
					((unsigned)((val >> (hw * 16)) & 0xFFFF) << 5) |
					((unsigned)rd & 0x1F);
				emit32(out, &off, enc);
				return 0;
			}
		}
		{
			uint64_t inv = ~val;
			if (!is64) inv &= 0xFFFFFFFFULL;
			for (int hw = 0; hw < max_hw; hw++) {
				uint64_t mask = (uint64_t)0xFFFF << (hw * 16);
				if ((inv & ~mask) == 0) {
					uint32_t enc = (is64 ? 0x92800000 : 0x12800000) |
						((unsigned)hw << 21) |
						((unsigned)((inv >> (hw * 16)) & 0xFFFF) << 5) |
						((unsigned)rd & 0x1F);
					emit32(out, &off, enc);
					return 0;
				}
			}
		}

		/* Fallback: MOVZ + MOVK expansion (up to 4 instructions).
		 * Use MOVZ for the most significant non-zero 16-bit chunk,
		 * and MOVK for the remaining chunks. */
		{
			int chunks[max_hw];
			int nchunks = 0;
			/* Build list of non-zero chunks from MSB to LSB */
			for (int hw = max_hw - 1; hw >= 0; hw--) {
				uint16_t chunk = (uint16_t)((val >> (hw * 16)) & 0xFFFF);
				if (chunk != 0 || (nchunks == 0 && hw == 0)) {
					chunks[nchunks++] = hw;
				}
			}
			if (nchunks == 0) {
				/* value is 0 — mov rd, #0 should use movz rd, #0 */
				emit32(out, &off, 0xD2800000 | ((unsigned)rd & 0x1F));
				out->size = off;
				return 0;
			}
			/* First chunk → MOVZ */
			int first_hw = chunks[0];
			uint16_t first_val = (uint16_t)((val >> (first_hw * 16)) & 0xFFFF);
			emit32(out, &off, 0xD2800000 |
			       ((unsigned)first_hw << 21) |
			       ((unsigned)first_val << 5) |
			       ((unsigned)rd & 0x1F));
			/* Remaining chunks → MOVK */
			for (int ci = 1; ci < nchunks; ci++) {
				int hw = chunks[ci];
				uint16_t chunk_val = (uint16_t)((val >> (hw * 16)) & 0xFFFF);
				emit32(out, &off, 0xF2800000 |
				       ((unsigned)hw << 21) |
				       ((unsigned)chunk_val << 5) |
				       ((unsigned)rd & 0x1F));
			}
			out->size = off;
			return 0;
		}
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

	/* add rd, rn, #:lo12:symbol — R_AARCH64_ADD_ABS_LO12_NC */
	if (strcmp(mnemonic, "add") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' &&
	    ops[2].kind == 'l' && ops[2].modifier[0] != '\0' &&
	    strcmp(ops[2].modifier, "lo12") == 0) {
		int rd = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x91000000 : 0x11000000) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		const char *sym = ops[2].sym_start;
		set_fixup(out, 0, 4, 277, sym, 0); /* R_AARCH64_ADD_ABS_LO12_NC */
		return 0;
	}

	/* add rd, rn, rm (register) */
	if (strcmp(mnemonic, "add") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x8B000000 : 0x0B000000) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
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
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xCB000000 : 0x4B000000) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* cmp rn, rm (2-arg form) */
	if (strcmp(mnemonic, "cmp") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rn = ops[0].reg, rm = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xEB00001F : 0x6B00001F) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5));
		return 0;
	}

	/* cmp rn, #imm{, lsl #12} */
	if (strcmp(mnemonic, "cmp") == 0 && nops >= 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'i') {
		int rn = ops[0].reg;
		uint64_t imm = (uint64_t)ops[1].imm;
		int shift = 0;
		/* Check for shift modifier like "lsl #12" in extra operands */
		for (int si = 2; si < nops; si++) {
			const char *s = opbuf[si];
			while (*s == ' ' || *s == '\t') s++;
			if (strncmp(s, "lsl", 3) == 0) {
				s += 3;
				while (*s == ' ' || *s == '\t') s++;
				if (*s == '#') s++;
				long sv = strtol(s, NULL, 0);
				shift = (sv == 12) ? 12 : 0;
			}
		}
		uint64_t adj_imm = (shift == 12) ? (imm << 12) : imm;
		if (adj_imm <= 0xFFF) {
			/* fits in 12 bits unshifted */
		} else if ((adj_imm & ~0xFFF000ULL) == 0 && (adj_imm >> 12) <= 0xFFF) {
			shift = 12;
			adj_imm >>= 12;
		} else return -1;
		int s_bit = (shift == 12) ? 1 : 0;
		emit32(out, &off, 0xF100001F | ((unsigned)s_bit << 22) |
		       ((unsigned)(adj_imm & 0xFFF) << 10) |
		       (((unsigned)rn & 0x1F) << 5));
		return 0;
	}

	/* cmn rn, #imm — compare negative with immediate (alias: adds xzr, rn, #imm) */
	if (strcmp(mnemonic, "cmn") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'i') {
		int rn = ops[0].reg;
		uint64_t imm = (uint64_t)ops[1].imm;
		int shift = 0;
		if (imm > 0xFFF) {
			if ((imm & ~0xFFF000ULL) == 0) { shift = 12; imm >>= 12; } else return -1;
		}
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xB100001F : 0x3100001F) |
		       ((unsigned)shift << 22) |
		       ((unsigned)(imm & 0xFFF) << 10) |
		       (((unsigned)rn & 0x1F) << 5));
		return 0;
	}

	/* cmn rn, rm — compare negative with register (alias: adds xzr, rn, rm) */
	if (strcmp(mnemonic, "cmn") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rn = ops[0].reg, rm = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xAB00001F : 0x2B00001F) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5));
		return 0;
	}

	/* ldr xt, [base] — or with immediate offset (general & float) */
	if ((strcmp(mnemonic, "ldr") == 0 || strcmp(mnemonic, "ldrb") == 0 ||
	     strcmp(mnemonic, "ldrh") == 0 || strcmp(mnemonic, "ldrsw") == 0 ||
	     strcmp(mnemonic, "ldrsb") == 0 || strcmp(mnemonic, "ldrsh") == 0) &&
	    nops == 2 && ops[0].kind == 'r' && ops[1].kind == 'm') {
		unsigned rt = (unsigned)ops[0].reg;
		unsigned rn = (unsigned)ops[1].reg;
		int wreg = ops[0].wreg;
		int is64 = (wreg == XREG);

		if (ops[1].sym_start) {
			/* Symbol offset (e.g. #:got_lo12:symbol) */
			unsigned base_op;
			unsigned reloc;
			if (strcmp(mnemonic, "ldr") == 0 && (is64 || wreg == DREG || wreg == QREG)) {
				if (strcmp(ops[1].modifier, "got_lo12") == 0) {
					if (wreg == DREG)
						base_op = 0xFD400000;
					else if (wreg == QREG)
						base_op = 0x3DC00000;
					else
						base_op = 0xF9400000;
					reloc = 312; /* R_AARCH64_LD64_GOT_LO12_NC */
				} else return -1;
			} else return -1;
			emit32(out, &off, base_op | (rn << 5) | rt);
			set_fixup(out, 0, 4, reloc, ops[1].sym_start, 0);
			return 0;
		}
		if (ops[1].imm >= 0) {
			/* Scalar immediate offset */
			unsigned size = 0;
			uint32_t base_op = 0;
			if (strcmp(mnemonic, "ldr") == 0) {
				if (wreg == QREG) {
					size = 4; base_op = 0x3DC00000;
				} else if (wreg == DREG) {
					size = 3; base_op = 0xFD400000;
				} else if (wreg == SREG) {
					size = 2; base_op = 0xBD400000;
				} else {
					size = is64 ? 3 : 2;
					base_op = is64 ? 0xF9400000 : 0xB9400000;
				}
			} else if (strcmp(mnemonic, "ldrb") == 0) {
				size = 0; base_op = 0x39400000;
			} else if (strcmp(mnemonic, "ldrh") == 0) {
				size = 1; base_op = 0x79400000;
			} else if (strcmp(mnemonic, "ldrsw") == 0) {
				size = 2; base_op = 0xB9800000;
			} else if (strcmp(mnemonic, "ldrsb") == 0) {
				size = 0; base_op = 0x39800000;
			} else if (strcmp(mnemonic, "ldrsh") == 0) {
				int is64_out = (wreg == XREG);
				size = 1; base_op = is64_out ? 0x79800000 : 0x78800000;
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
			if (strcmp(mnemonic, "ldr") == 0) {
				if (wreg == QREG)
					base_op = 0x3CE06800;
				else if (wreg == DREG)
					base_op = 0xFC606800;
				else if (wreg == SREG)
					base_op = 0xBC606800;
				else
					base_op = is64 ? 0xF8606800 : 0xB8606800;
			} else if (strcmp(mnemonic, "ldrb") == 0)
				base_op = 0x38606800;
			else
				return -1;
			emit32(out, &off, base_op | (rm << 16) | (rn << 5) | rt);
			return 0;
		}
	}

	/* str xt, [base, #imm] — general register and float (unsigned offset) */
	if ((strcmp(mnemonic, "str") == 0 || strcmp(mnemonic, "strb") == 0 ||
	     strcmp(mnemonic, "strh") == 0) &&
	    nops >= 2 && nops <= 3 && ops[0].kind == 'r' && ops[1].kind == 'm') {
		unsigned rt = (unsigned)ops[0].reg;
		unsigned rn = (unsigned)ops[1].reg;
		int wreg = ops[0].wreg;
		int is_float = (wreg == DREG || wreg == SREG || wreg == QREG);

		/* Post-indexed: str xt, [base], #imm  (nops==3) */
		if (nops == 3 && ops[2].kind == 'i') {
			int64_t imm = ops[2].imm;
			if (imm < -256 || imm > 255) return -1;
			if (is_float) {
				uint32_t base;
				if (wreg == DREG) base = 0xFC000400;
				else if (wreg == SREG) base = 0xBC000400;
				else if (wreg == QREG) base = 0x3CC00400;
				else return -1;
				emit32(out, &off, base |
				       ((unsigned)(imm & 0x1FF) << 12) |
				       (rn << 5) | rt);
				return 0;
			} else {
				/* General register post-indexed */
				uint32_t base;
				if (strcmp(mnemonic, "str") == 0)
					base = (wreg == XREG) ? 0xF8000400 : 0xB8000400;
				else if (strcmp(mnemonic, "strb") == 0)
					base = 0x38000400;
				else if (strcmp(mnemonic, "strh") == 0)
					base = 0x78000400;
				else return -1;
				emit32(out, &off, base |
				       ((unsigned)(imm & 0x1FF) << 12) |
				       (rn << 5) | rt);
				return 0;
			}
		}

		/* Pre-indexed: str xt, [base, #imm]!  (ops[1].writeback == 1) */
		if (ops[1].writeback) {
			int64_t imm = ops[1].imm;
			uint32_t base;
			if (is_float) {
				if (wreg == DREG) base = 0xFC000C00;
				else if (wreg == SREG) base = 0xBC000C00;
				else if (wreg == QREG) base = 0x3C800C00;
				else return -1;
			} else {
				if (strcmp(mnemonic, "str") == 0) {
					base = (wreg == XREG) ? 0xF8000C00 : 0xB8000C00;
				} else if (strcmp(mnemonic, "strb") == 0) {
					base = 0x38000C00;
				} else if (strcmp(mnemonic, "strh") == 0) {
					base = 0x78000C00;
				} else return -1;
			}
			if (imm < -256 || imm > 255) return -1;
			emit32(out, &off, base |
			       ((unsigned)(imm & 0x1FF) << 12) |
			       (rn << 5) | rt);
			return 0;
		}

		/* Unsigned offset: str xt, [base, #imm] (or [base] → imm=0) */
		unsigned size = 0;
		uint32_t base_op;
		if (strcmp(mnemonic, "str") == 0) {
			if (wreg == QREG) {
				size = 4; base_op = 0x3D800000;
			} else if (wreg == DREG) {
				size = 3; base_op = 0xFD000000;
			} else if (wreg == SREG) {
				size = 2; base_op = 0xBD000000;
			} else {
				int is64 = (wreg == XREG);
				size = is64 ? 3 : 2;
				base_op = is64 ? 0xF9000000 : 0xB9000000;
			}
		} else if (strcmp(mnemonic, "strb") == 0) {
			size = 0; base_op = 0x39000000;
		} else if (strcmp(mnemonic, "strh") == 0) {
			size = 1; base_op = 0x79000000;
		} else return -1;

		int64_t imm = ops[1].imm;
		if (imm < 0) return -1; /* negative offset needs pre-indexed or STUR */
		uint64_t scaled = (uint64_t)imm >> size;
		if (scaled > 0xFFF || (imm & ((1ULL << size) - 1)) != 0) return -1;
		emit32(out, &off, base_op | ((unsigned)scaled << 10) |
		       (rn << 5) | rt);
		return 0;
	}

	/* stp/ldp — store/load pair (register pair with base+offset).
	 *
	 * Syntax:
	 *   stp xt1, xt2, [base, #imm]     (offset,  nops=3, no writeback)
	 *   stp xt1, xt2, [base, #imm]!    (pre-idx, nops=3, writeback=1)
	 *   stp xt1, xt2, [base], #imm     (post-idx, nops=4)
	 *
	 * Encoding (general registers, V=0):
	 *   opc:  31:30, 10=64bit, 00=32bit
	 *   mode: 25:23, 010=offset, 011=pre-idx, 001=post-idx
	 *   L:    22, 0=store (stp), 1=load (ldp)
	 *   imm7: 21:15, scaled by element size (8 or 4)
	 *   Rt2:  14:10
	 *   Rn:   9:5
	 *   Rt:   4:0
	 */
	if ((strcmp(mnemonic, "stp") == 0 || strcmp(mnemonic, "ldp") == 0) &&
	    nops >= 3 && ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'm') {
		int rt  = ops[0].reg;
		int rt2 = ops[1].reg;
		int rn  = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		int is_load = (mnemonic[0] == 'l');

		int64_t imm;
		unsigned imode; /* 0=offset, 1=pre-indexed, 2=post-indexed */

		if (nops == 4 && ops[3].kind == 'i') {
			/* Post-indexed: [base], #imm */
			imode = 2;
			imm = ops[3].imm;
		} else if (ops[2].writeback) {
			/* Pre-indexed: [base, #imm]! */
			imode = 1;
			imm = ops[2].imm;
		} else {
			/* Offset: [base, #imm] */
			imode = 0;
			imm = ops[2].imm;
		}

		int scale = is64 ? 8 : 4;
		if (imm % scale != 0) return -1;
		int64_t imm7 = imm / scale;
		if (imm7 < -64 || imm7 > 63) return -1;

		uint32_t base;
		if (is_load) {
			static const uint32_t ldp_base[3][2] = {
				{0xA9400000, 0x29400000}, /* offset */
				{0xA9C00000, 0x29C00000}, /* pre-indexed */
				{0xA8C00000, 0x28C00000}, /* post-indexed */
			};
			base = ldp_base[imode][is64 ? 0 : 1];
		} else {
			static const uint32_t stp_base[3][2] = {
				{0xA9000000, 0x29000000}, /* offset */
				{0xA9800000, 0x29800000}, /* pre-indexed */
				{0xA8800000, 0x28800000}, /* post-indexed */
			};
			base = stp_base[imode][is64 ? 0 : 1];
		}

		emit32(out, &off, base |
			((unsigned)(imm7 & 0x7F) << 15) |
			(((unsigned)rt2 & 0x1F) << 10) |
			(((unsigned)rn  & 0x1F) << 5) |
			((unsigned)rt  & 0x1F));
		return 0;
	}

	/* ldrsb â load signed byte */
	if (strcmp(mnemonic, "ldrsb") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'm') {
		unsigned rt = (unsigned)ops[0].reg;
		unsigned rn = (unsigned)ops[1].reg;
		emit32(out, &off, 0x39800000 | ((unsigned)ops[1].imm << 10) | (rn << 5) | rt);
		return 0;
	}
	/* ldrsh â load signed halfword */
	if (strcmp(mnemonic, "ldrsh") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'm') {
		unsigned rt = (unsigned)ops[0].reg;
		unsigned rn = (unsigned)ops[1].reg;
		if (ops[1].imm & 1) return -1;
		uint64_t scaled = (uint64_t)ops[1].imm >> 1;
		unsigned is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x79800000 : 0x78800000) |
		       ((unsigned)scaled << 10) | (rn << 5) | rt);
		return 0;
	}

	/* orr rd, rn, rm */
	if ((strcmp(mnemonic, "orr") == 0 || strcmp(mnemonic, "mov") == 0) &&
	    nops == 3 && ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		/* Also handle mov rd, rm (3-arg form as orr rd, xzr, rm) */
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xAA000000 : 0x2A000000) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* eor rd, rn, rm */
	if (strcmp(mnemonic, "eor") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xCA000000 : 0x4A000000) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* and rd, rn, rm */
	if (strcmp(mnemonic, "and") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x8A000000 : 0x0A000000) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* mul rd, rn, rm */
	if (strcmp(mnemonic, "mul") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x9B007C00 : 0x1B007C00) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* msub rd, rn, rm, ra — multiply-subtract (Xd = Xa - Xn * Xm) */
	if (strcmp(mnemonic, "msub") == 0 && nops == 4 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r' && ops[3].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg, ra = ops[3].reg;
		int is64 = (ops[0].wreg == XREG);
		/* sf=1/0, op=00, 11011, o0=1(MSUB), Rm<<16, Ra<<10, Rn<<5, Rd */
		emit32(out, &off, (is64 ? 0x9B800000 : 0x1B800000) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)ra & 0x1F) << 10) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* sdiv rd, rn, rm */
	if (strcmp(mnemonic, "sdiv") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x9AC00C00 : 0x1AC00C00) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* udiv rd, rn, rm */
	if (strcmp(mnemonic, "udiv") == 0 && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x9AC00800 : 0x1AC00800) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* neg rd, rm */
	if (strcmp(mnemonic, "neg") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rm = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xCB0003E0 : 0x4B0003E0) |
		       (((unsigned)rm & 0x1F) << 16) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* lsl rd, rn, rm */
	if ((strcmp(mnemonic, "lsl") == 0) && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		/* LSLV (register): 1 0 11010110 00000 001000 rm rn rd */
		emit32(out, &off, (is64 ? 0x9AC02000 : 0x1AC02000) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* lsr rd, rn, rm */
	if ((strcmp(mnemonic, "lsr") == 0) && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x9AC02400 : 0x1AC02400) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* asr rd, rn, rm */
	if ((strcmp(mnemonic, "asr") == 0) && nops == 3 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x9AC02800 : 0x1AC02800) |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* sxtb rd, rn — SBFM: Rd[4:0], Rn[9:5] */
	if (strcmp(mnemonic, "sxtb") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x93401C00 : 0x13001C00) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* sxth rd, rn */
	if (strcmp(mnemonic, "sxth") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x93403C00 : 0x13003C00) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* sxtw rd, rn */
	if (strcmp(mnemonic, "sxtw") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x93407C00 : 0x13007C00) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* uxtb rd, rn — UBFM: Rd[4:0], Rn[9:5] */
	if (strcmp(mnemonic, "uxtb") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xD3401C00 : 0x53001C00) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* uxth rd, rn */
	if (strcmp(mnemonic, "uxth") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0xD3403C00 : 0x53003C00) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* fmov rd, rn (scalar float, both float regs) — Rd[4:0], Rn[9:5];
	 * also FMOV (general): core <-> float via w0..w30/x0..x30. */
	if (strcmp(mnemonic, "fmov") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		int t0 = ops[0].wreg, t1 = ops[1].wreg;
		uint32_t base;
		if (t0 == DREG && t1 == XREG) base = 0x9E670000; /* fmov d, x */
		else if (t0 == XREG && t1 == DREG) base = 0x9E660000; /* fmov x, d */
		else if (t0 == SREG && t1 == WREG) base = 0x1E270000; /* fmov s, w */
		else if (t0 == WREG && t1 == SREG) base = 0x1E260000; /* fmov w, s */
		else if (t0 == DREG && t1 == DREG) base = 0x1E604000; /* fmov d, d */
		else if (t0 == SREG && t1 == SREG) base = 0x1E204000; /* fmov s, s */
		else return -1;
		emit32(out, &off, base |
		       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		return 0;
	}

	/* fadd/fsub/fmul/fdiv/fsqrt scalar: Rd[4:0], Rn[9:5], Rm[20:16].
	 * Bit 22 (M=0/sf) selects precision: 0=.s single, 1=.d double. */
	if ((strcmp(mnemonic, "fadd") == 0 || strcmp(mnemonic, "fsub") == 0 ||
	     strcmp(mnemonic, "fmul") == 0 || strcmp(mnemonic, "fdiv") == 0) &&
	    nops == 3 && ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		uint32_t base;
		if (strcmp(mnemonic, "fadd") == 0) base = 0x1E602800;
		else if (strcmp(mnemonic, "fsub") == 0) base = 0x1E603800;
		else if (strcmp(mnemonic, "fmul") == 0) base = 0x1E600800;
		else base = 0x1E601800;
		if (ops[0].wreg == SREG) base &= ~0x400000u; /* single precision */
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		emit32(out, &off, base |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* fneg/fabs/fsqrt rd, rn — Rd[4:0], Rn[9:5] */
	if ((strcmp(mnemonic, "fneg") == 0 || strcmp(mnemonic, "fabs") == 0 ||
	     strcmp(mnemonic, "fsqrt") == 0) && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		uint32_t base;
		if (strcmp(mnemonic, "fneg") == 0) base = 0x1E614000;
		else if (strcmp(mnemonic, "fabs") == 0) base = 0x1E60C000;
		else base = 0x1E61C000;
		if (ops[0].wreg == SREG) base &= ~0x400000u; /* single precision */
		int rd = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, base |
		       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		return 0;
	}

	/* fcvt rd, rn — Rd[4:0], Rn[9:5] */
	if (strcmp(mnemonic, "fcvt") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		if (ops[0].wreg == DREG && ops[1].wreg == SREG) {
			emit32(out, &off, 0x1E22C000 |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else if (ops[0].wreg == SREG && ops[1].wreg == DREG) {
			emit32(out, &off, 0x1E624000 |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else return -1;
		return 0;
	}

	/* fcvtzs rd, rn (float to signed int) — Rd[4:0], Rn[9:5];
	 * base depends on dest width (W/X) and source precision (S/D) */
	if (strcmp(mnemonic, "fcvtzs") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		unsigned dest64 = (ops[0].wreg == XREG);
		if (ops[1].wreg == DREG) {
			emit32(out, &off, (dest64 ? 0x9E780000 : 0x1E780000) |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else if (ops[1].wreg == SREG) {
			emit32(out, &off, (dest64 ? 0x9E380000 : 0x1E380000) |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else return -1;
		return 0;
	}

	/* fcvtzu rd, rn */
	if (strcmp(mnemonic, "fcvtzu") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		unsigned dest64 = (ops[0].wreg == XREG);
		if (ops[1].wreg == DREG) {
			emit32(out, &off, (dest64 ? 0x9E790000 : 0x1E790000) |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else if (ops[1].wreg == SREG) {
			emit32(out, &off, (dest64 ? 0x9E390000 : 0x1E390000) |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else return -1;
		return 0;
	}

	/* scvtf rd, rn (signed int to float) — Rd[4:0], Rn[9:5];
	 * base depends on dest precision (S/D) and source width (W/X) */
	if (strcmp(mnemonic, "scvtf") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		unsigned src64 = (ops[1].wreg == XREG);
		if (ops[0].wreg == DREG) {
			emit32(out, &off, (src64 ? 0x9E620000 : 0x1E620000) |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else if (ops[0].wreg == SREG) {
			emit32(out, &off, (src64 ? 0x9E220000 : 0x1E220000) |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else return -1;
		return 0;
	}

	/* ucvtf rd, rn */
	if (strcmp(mnemonic, "ucvtf") == 0 && nops == 2 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rd = ops[0].reg, rn = ops[1].reg;
		unsigned src64 = (ops[1].wreg == XREG);
		if (ops[0].wreg == DREG) {
			emit32(out, &off, (src64 ? 0x9E630000 : 0x1E630000) |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
		} else if (ops[0].wreg == SREG) {
			emit32(out, &off, (src64 ? 0x9E230000 : 0x1E230000) |
			       (((unsigned)rn & 0x1F) << 5) | ((unsigned)rd & 0x1F));
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

	/* ---- Atomics (load-acquire / store-release) ---- */

	/* ldarb â load-acquire byte */
	if (strcmp(mnemonic, "ldarb") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x38DFFC00 | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* ldarh â load-acquire halfword */
	if (strcmp(mnemonic, "ldarh") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x38DFFC00 | (1u << 22) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* ldar â load-acquire */
	if (strcmp(mnemonic, "ldar") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x88DFFC00 : 0x08DFFC00) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* stlrb â store-release byte */
	if (strcmp(mnemonic, "stlrb") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x389FFC00 | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* stlrh â store-release halfword */
	if (strcmp(mnemonic, "stlrh") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x389FFC00 | (1u << 22) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* stlr â store-release */
	if (strcmp(mnemonic, "stlr") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x889FFC00 : 0x089FFC00) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* ldaxrb â load-acquire exclusive byte */
	if (strcmp(mnemonic, "ldaxrb") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x38DFFC00 | (1u << 23) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* ldaxrh â load-acquire exclusive halfword */
	if (strcmp(mnemonic, "ldaxrh") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		emit32(out, &off, 0x38DFFC00 | (1u << 23) | (1u << 22) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* ldaxr â load-acquire exclusive */
	if (strcmp(mnemonic, "ldaxr") == 0 && nops == 2 && ops[1].kind == 'm') {
		int rt = ops[0].reg, rn = ops[1].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x885FFC00 : 0x085FFC00) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* stlxrb â store-release exclusive byte */
	if (strcmp(mnemonic, "stlxrb") == 0 && nops == 3 && ops[1].kind == 'r' && ops[2].kind == 'm') {
		int rs = ops[0].reg, rt = ops[1].reg, rn = ops[2].reg;
		emit32(out, &off, 0x381FFC00 | ((unsigned)rs << 16) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* stlxrh â store-release exclusive halfword */
	if (strcmp(mnemonic, "stlxrh") == 0 && nops == 3 && ops[1].kind == 'r' && ops[2].kind == 'm') {
		int rs = ops[0].reg, rt = ops[1].reg, rn = ops[2].reg;
		emit32(out, &off, 0x381FFC00 | (1u << 22) | ((unsigned)rs << 16) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* stlxr â store-release exclusive */
	if (strcmp(mnemonic, "stlxr") == 0 && nops == 3 && ops[1].kind == 'r' && ops[2].kind == 'm') {
		int rs = ops[0].reg, rt = ops[1].reg, rn = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x881FFC00 : 0x081FFC00) | ((unsigned)rs << 16) | ((unsigned)rn << 5) | (unsigned)rt);
		return 0;
	}
	/* dmb â data memory barrier */
	if (strcmp(mnemonic, "dmb") == 0 && nops == 1) {
		const char *opt = opbuf[0];
		while (*opt == ' ' || *opt == '\t') opt++;
		unsigned barrier = 0;
		if (strcmp(opt, "ish") == 0) barrier = 11;
		else if (strcmp(opt, "ishst") == 0) barrier = 10;
		else if (strcmp(opt, "nsh") == 0) barrier = 7;
		else if (strcmp(opt, "nshst") == 0) barrier = 6;
		else if (strcmp(opt, "osh") == 0) barrier = 3;
		else if (strcmp(opt, "oshst") == 0) barrier = 2;
		else if (strcmp(opt, "sy") == 0) barrier = 15;
		else if (strcmp(opt, "st") == 0) barrier = 14;
		else return -1;
		emit32(out, &off, 0xD5033BBF | ((barrier & 0xF) << 16));
		return 0;
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

	/* brk #imm — breakpoint */
	if (strcmp(mnemonic, "brk") == 0 && nops == 1 && ops[0].kind == 'i') {
		uint32_t imm = (uint32_t)(ops[0].imm & 0xFFFF);
		emit32(out, &off, 0xD4200000 | (imm << 5));
		return 0;
	}

	/* hint #imm — hint instruction (e.g. BTI = hint #34, NOP = hint #0) */
	if (strcmp(mnemonic, "hint") == 0 && nops == 1 && ops[0].kind == 'i') {
		uint32_t imm = (uint32_t)(ops[0].imm & 0x7F);
		emit32(out, &off, 0xD503201F | (imm << 5));
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

	/* csinc rd, rn, rm, cond â conditional select increment */
	if (strcmp(mnemonic, "csinc") == 0 && nops == 4 &&
	    ops[0].kind == 'r' && ops[1].kind == 'r' && ops[2].kind == 'r') {
		static const struct { const char *name; unsigned cond; } conds[] = {
			{"eq", 0}, {"ne", 1}, {"cs", 2}, {"cc", 3},
			{"mi", 4}, {"pl", 5}, {"vs", 6}, {"vc", 7},
			{"hi", 8}, {"ls", 9}, {"ge", 10}, {"lt", 11},
			{"gt", 12}, {"le", 13}, {"hs", 2}, {"lo", 3},
			{NULL, 0}
		};
		const char *cname = opbuf[3];
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
		int rd = ops[0].reg, rn = ops[1].reg, rm = ops[2].reg;
		int is64 = (ops[0].wreg == XREG);
		emit32(out, &off, (is64 ? 0x9A800000 : 0x1A800000) |
		       (((unsigned)rm & 0x1F) << 16) |
		       ((cond & 0xF) << 12) |
		       (((unsigned)rn & 0x1F) << 5) |
		       ((unsigned)rd & 0x1F));
		return 0;
	}

	/* adrp rd, label */
	if ((strcmp(mnemonic, "adrp") == 0 || strcmp(mnemonic, "adr") == 0) &&
	    nops == 2 && ops[0].kind == 'r' && ops[1].kind == 'l') {
		int rd = ops[0].reg;
		/* Emit placeholder, set fixup */
		emit32(out, &off, (strcmp(mnemonic, "adrp") == 0 ? 0x90000000 : 0x10000000) |
		       ((unsigned)rd & 0x1F));

		unsigned reloc;
		if (strcmp(mnemonic, "adrp") == 0) {
			if (ops[1].modifier[0] != '\0' && strcmp(ops[1].modifier, "got") == 0)
				reloc = 311; /* R_AARCH64_ADR_GOT_PAGE */
			else
				reloc = 275; /* R_AARCH64_ADR_PREL_PG_HI21 */
		} else {
			reloc = 274; /* R_AARCH64_ADR_PREL_LO21 */
		}
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

	/* b.cond label (dot form: b.eq, b.ne, ...) */
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
		/* B.cond: cond sits in bits [4:0]; imm19 occupies [23:5]. */
		uint32_t base = 0x54000000 | (cond & 0xF);
		emit32(out, &off, base);
		const char *sym = ops[0].sym_start;
		set_fixup(out, 0, 4, 279, sym, 0); /* R_AARCH64_CONDBR19 */
		return 0;
	}

	/* bne/beq/blt/bgt/ble/bge label (no-dot form) */
	if (nops == 1 && ops[0].kind == 'l') {
		static const struct { const char *name; unsigned cond; } nodot_conds[] = {
			{"beq", 0}, {"bne", 1}, {"bhs", 2}, {"bcs", 2},
			{"blo", 3}, {"bcc", 3},
			{"bmi", 4}, {"bpl", 5}, {"bvs", 6}, {"bvc", 7},
			{"bhi", 8}, {"bls", 9},
			{"bge", 10}, {"blt", 11},
			{"bgt", 12}, {"ble", 13},
			{NULL, 0}
		};
		unsigned cond = 0;
		int found = 0;
		for (int ci = 0; nodot_conds[ci].name; ci++) {
			if (strcmp(mnemonic, nodot_conds[ci].name) == 0) {
				cond = nodot_conds[ci].cond;
				found = 1;
				break;
			}
		}
		if (found) {
			/* B.cond: cond sits in bits [4:0]; imm19 occupies [23:5]. */
			uint32_t base = 0x54000000 | (cond & 0xF);
			emit32(out, &off, base);
			const char *sym = ops[0].sym_start;
			set_fixup(out, 0, 4, 279, sym, 0); /* R_AARCH64_CONDBR19 */
			return 0;
		}
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

	/* fcmp/fcmpe rn, rm — Rn[9:5], Rm[20:16]; E bit (quiet) is bit 4.
	 * Verified against GNU as: fcmpe d0, d1 = 0x1E612010,
	 *                          fcmpe s0, s1 = 0x1E212010,
	 *                          fcmp  d0, d1 = 0x1E612000. */
	if ((strcmp(mnemonic, "fcmpe") == 0 || strcmp(mnemonic, "fcmp") == 0) &&
	    nops == 2 && ops[0].kind == 'r' && ops[1].kind == 'r') {
		int rn = ops[0].reg, rm = ops[1].reg;
		uint32_t base = strcmp(mnemonic, "fcmpe") == 0 ? 0x1E602010 : 0x1E602000;
		if (ops[0].wreg == SREG) base &= ~0x400000u; /* single precision */
		emit32(out, &off, base |
		       (((unsigned)rm & 0x1F) << 16) |
		       (((unsigned)rn & 0x1F) << 5));
		return 0;
	}

	return -1; /* unsupported */
}
