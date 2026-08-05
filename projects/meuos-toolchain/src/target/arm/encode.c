/* encode.c — ARM (armv7) instruction encoder.
 *
 * Encodes an ARM instruction given its mnemonic+operands and fills a
 * struct mt_insn with the 4-byte fixed-length ARM encoding.
 * Supports the ARM subset emitted by mcc --target=armv7 and the
 * meuos-libc arch runtime assembly files.
 *
 * ARM instruction encoding overview:
 *   - Fixed 4-byte (32-bit) instructions, little-endian.
 *   - Condition code in bits [31:28] (0xE = ALways).
 *   - Data processing: cond(4)|00I|opcode(4)|S(1)|Rn(4)|Rd(4)|operand2(12)
 *   - Memory: cond|01|I|P|U|B|W|L|Rn|Rd|offset12
 *   - Branch: cond|101|L|offset24
 *   - VFP: cond|1110|... (coprocessor/data processing)
 */
#include "mt/target.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Helpers ---- */
static void emit32(unsigned char *p, uint32_t v) {
	p[0] = v & 0xFF; p[1] = (v>>8)&0xFF; p[2] = (v>>16)&0xFF; p[3] = (v>>24)&0xFF;
}

static void set_fixup(struct mt_insn *out, size_t offset, unsigned width,
                      unsigned type, const char *sym, int64_t addend) {
	/* The symbol may carry a `+N`/`-N` offset suffix (mcc emits
	 * `#:lower16:.Lstring.2+1` for address arithmetic).  Split it off
	 * so the relocation applies to a plain symbol with an addend,
	 * otherwise the linker cannot resolve `sym+1`. */
	char buf[256];
	if (sym) {
		const char *p;
		snprintf(buf, sizeof buf, "%s", sym);
		/* mcc quotes local labels containing dots (e.g. ".Lfp1"); the
		 * label definition is unquoted, so strip the quotes here. */
		{
			size_t bl = strlen(buf);
			if (bl >= 2 && buf[0] == '"' && buf[bl - 1] == '"') {
				memmove(buf, buf + 1, bl - 2);
				buf[bl - 2] = '\0';
			}
		}
		for (p = buf; *p; ++p)
			if ((*p == '+' || *p == '-') && p != buf) {
				addend += strtoll(p, NULL, 0);
				*(char *)p = '\0';
				break;
			}
		sym = buf;
	}
	size_t len = sym ? strlen(sym) : 0;
	out->fixup_offset = offset;
	out->fixup_width = width;
	out->reloc_type = type;
	out->fixup_symbol = malloc(len + 1);
	if (out->fixup_symbol) { memcpy((void*)out->fixup_symbol, sym, len + 1); }
	out->fixup_addend = addend;
}

static int reg_num(const char *s, int *r) {
	if (s[0] == 'r' && s[1] >= '0' && s[1] <= '9') {
		int n = s[1] - '0';
		if (s[2] >= '0' && s[2] <= '2') { n = n*10 + s[2]-'0'; if (n>15) return -1; *r = n; return 1; }
		*r = n; return 1;
	}
	if (s[0] == 's' && s[1]=='p') { *r=13; return 1; }
	if (s[0] == 'l' && s[1]=='r') { *r=14; return 1; }
	if (s[0] == 'p' && s[1]=='c') { *r=15; return 1; }
	if (s[0] == 'f' && s[1]=='p') { *r=11; return 1; } /* fp == r11 frame pointer alias */
	if (s[0] == 's' && s[1]>='0' && s[1]<='9') { int n = s[1]-'0'; if(s[2]>='0'&&s[2]<='9') n=n*10+s[2]-'0'; if(n>31)return -1; *r=16+n; return 1; }
	if (s[0] == 'd' && s[1]>='0' && s[1]<='9') { int n = s[1]-'0'; if(s[2]>='0'&&s[2]<='9') n=n*10+s[2]-'0'; if(n>31)return -1; *r=n; return 1; }
	return -1;
}

/* reg_num() returns 16+N for a single-precision register sN; convert
 * back to the raw N used in VFP encodings. */
static int sp_reg(int r) {
	return (r >= 16 && r <= 47) ? r - 16 : r;
}

/* VFP double-precision register field encodings (Vd/Vn/Vm = d0-d31).
 * D/N/M bits carry bit 4 of the register number. */
static uint32_t
dp_dst_field(int reg)
{
	return ((uint32_t)((reg >> 4) & 1) << 22) |
	       ((uint32_t)(reg & 0xF) << 12);
}
static uint32_t
dp_src_field(int reg)
{
	return ((uint32_t)((reg >> 4) & 1) << 5) |
	       ((uint32_t)(reg & 0xF));
}
/* Single-precision register field encodings (s0-s31). */
static uint32_t
sp_dst_field(int reg)
{
	return ((uint32_t)(reg & 1) << 22) |
	       ((uint32_t)(reg >> 1) << 12);
}
static uint32_t
sp_src_field(int reg)
{
	return ((uint32_t)(reg & 1) << 5) |
	       ((uint32_t)(reg >> 1));
}

/* ---- Helper: parse condition suffix from mnemonic like "addgt" -> ("add", 11) ---- */
static int parse_op_cond(const char *mnemonic, const char **base_out, int *cond_out) {
	static const struct { const char *s; int c; } conds[] = {
		{"eq",0},{"ne",1},{"cs",2},{"cc",3},{"mi",4},{"pl",5},
		{"vs",6},{"vc",7},{"hi",8},{"ls",9},{"ge",10},{"lt",11},
		{"gt",12},{"le",13},{"al",14},{0,0}
	};
	size_t len = strlen(mnemonic);
	if (len <= 3) { *base_out = mnemonic; *cond_out = 14; return 0; }
	/* Try last 2 chars as condition suffix */
	const char *suffix = mnemonic + len - 2;
	for (int i = 0; conds[i].s; i++) {
		if (suffix[0] == conds[i].s[0] && suffix[1] == conds[i].s[1]) {
			/* Need a static buffer for the base name */
			static char base_buf[16];
			size_t blen = len - 2;
			if (blen >= 16) return -1;
			memcpy(base_buf, mnemonic, blen);
			base_buf[blen] = '\0';
			*base_out = base_buf;
			*cond_out = conds[i].c;
			return 0;
		}
	}
	*base_out = mnemonic;
	*cond_out = 14; /* AL */
	return 0;
}

/* ---- Helper: parse a comma-separated core register list into a bitmask.
 * The operand text is already brace-stripped (e.g. "r4, r5, r6, r7").
 * Ranges (e.g. "r4-r9") are expanded.  Bits 0-15 map to r0-r15; VFP
 * single/double registers are rejected. */
static int parse_reglist(const char *s, uint16_t *mask) {
	const char *p = s;
	*mask = 0;
	while (*p) {
		while (*p == ' ' || *p == '\t') p++;
		if (!*p) break;
		const char *start = p;
		while (*p && *p != ',') p++;
		char tok[64];
		size_t len = (size_t)(p - start);
		if (len >= sizeof tok) return -1;
		memcpy(tok, start, len);
		tok[len] = '\0';
		/* Range token "rN-rM" (or "sp"/"lr"/"pc" endpoints are not
		 * ranges — only rN-rM is meaningful, as in `push {r4-r9, lr}`). */
		const char *dash = strchr(tok, '-');
		if (dash) {
			char lo[16], hi[16];
			size_t lo_len = (size_t)(dash - tok);
			if (lo_len == 0 || lo_len >= sizeof lo) return -1;
			memcpy(lo, tok, lo_len);
			lo[lo_len] = '\0';
			if (strlen(dash + 1) >= sizeof hi) return -1;
			strcpy(hi, dash + 1);
			int rlo, rhi;
			if (reg_num(lo, &rlo) < 0 || reg_num(hi, &rhi) < 0) return -1;
			if (rlo > 15 || rhi > 15 || rlo > rhi) return -1;
			for (int r = rlo; r <= rhi; r++)
				*mask |= (uint16_t)(1u << r);
		} else {
			int r;
			if (reg_num(tok, &r) < 0 || r > 15) return -1;
			*mask |= (uint16_t)(1u << r);
		}
		if (*p == ',') p++;
	}
	return 0;
}

/* ---- Helper: encode an ARM 8-bit-rotated immediate.
 * Stores the operand2 field (rot<<8 | imm8) in *out on success and
 * returns 0; returns -1 when the constant is not encodable (so the
 * caller fails instead of silently mis-encoding). */
static int arm_imm_encode(int64_t imm, uint32_t *out) {
	uint32_t v = (uint32_t)imm;
	if (v < 256) { *out = v; return 0; }
	for (int r = 1; r < 16; r++) {
		/* imm8 = v ROL (2*r), so the CPU's imm8 ROR (2*r) decodes
		 * back to v */
		uint32_t rv = (v << (r*2)) | (v >> (32 - r*2));
		if (rv < 256) { *out = ((uint32_t)r << 8) | rv; return 0; }
	}
	return -1;
}

/* ---- Main encode function ---- */
int arm_encode_insn(const struct mt_target *target,
                      const char *mnemonic, const char *operand_text,
                      struct mt_insn *out)
{
	(void)target;
	memset(out, 0, sizeof(*out));
	out->size = 4;

	/* Simple mnemonics */
	if (strcmp(mnemonic, "nop") == 0) {
		emit32(out->bytes, 0xE320F000);
		return 0;
	}
	if (strcmp(mnemonic, "dmb") == 0) {
		emit32(out->bytes, 0xF57FF05F); /* dmb ish */
		return 0;
	}
	if (strcmp(mnemonic, "bkpt") == 0) {
		uint32_t imm = 0;
		if (operand_text) sscanf(operand_text, "#%u", &imm);
		emit32(out->bytes, 0xE1200070 | ((imm & 0xFFF0) << 4) | (imm & 0xF));
		return 0;
	}
	if (strcmp(mnemonic, "svc") == 0) {
		uint32_t imm = 0;
		if (operand_text) sscanf(operand_text, "#%u", &imm);
		emit32(out->bytes, 0xEF000000 | (imm & 0xFFFFFF));
		return 0;
	}

	/* Parse operands: split by ',' and strip spaces.
	 * Respect matching brackets and braces: commas inside [...] or
	 * {...} (register lists) are not splitters. */
	char ops[20][128]; int nops = 0;
	const char *p = operand_text ? operand_text : "";
	while (*p && nops < 20) {
		while (*p == ' ' || *p == '\t') p++;
		int i = 0;
		int bracket_depth = 0, brace_depth = 0;
		while (*p && (bracket_depth > 0 || brace_depth > 0 || *p != ',')) {
			if (*p == '[') bracket_depth++;
			if (*p == ']' && bracket_depth > 0) bracket_depth--;
			if (*p == '{') brace_depth++;
			if (*p == '}' && brace_depth > 0) brace_depth--;
			if (i < (int)sizeof ops[nops] - 1) ops[nops][i++] = *p;
			p++;
		}
		ops[nops][i] = '\0'; nops++;
		if (*p == ',') p++;
	}

	/* Strip curly braces from operands (e.g., {lr} → lr for push/pop) */
	for (int oi = 0; oi < nops; oi++) {
		char *s = ops[oi];
		size_t sl = strlen(s);
		if (sl > 0 && s[0] == '{') {
			memmove(s, s+1, sl);
			sl--;
		}
		if (sl > 0 && s[sl-1] == '}')
			s[sl-1] = '\0';
	}
	/* ---- Branch: b label or bl label ---- */
	if (strcmp(mnemonic, "b") == 0 && nops >= 1) {
		out->fixed = 0;
		out->reloc_type = 29; /* R_ARM_JUMP24 */
		set_fixup(out, 0, 4, out->reloc_type, ops[0], 0);
		emit32(out->bytes, 0xEA000000);
		return 0;
	}
	if (strcmp(mnemonic, "bl") == 0 && nops >= 1) {
		out->fixed = 0;
		out->reloc_type = 28; /* R_ARM_CALL */
		set_fixup(out, 0, 4, out->reloc_type, ops[0], 0);
		emit32(out->bytes, 0xEB000000);
		return 0;
	}
	if (strcmp(mnemonic, "bx") == 0 && nops >= 1) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		emit32(out->bytes, 0xE12FFF10 | rd);
		return 0;
	}
	if (strcmp(mnemonic, "blx") == 0 && nops >= 1) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		emit32(out->bytes, 0xE12FFF30 | rd);
		return 0;
	}

	/* ---- mrc cp, op1, rt, crn, crm, op2 (coprocessor read) ----
	 * mcc emits `mrc p15, 0, r10, c13, c0, 3` to read the thread
	 * pointer (TPIDRURO, CP15 c13) into a general register.
	 * AT&T/GNU syntax: mrc coproc, opcode_1, Rd, CRn, CRm, opcode_2.
	 * Encoding: cond(0xE) 1110 op1 L(1) crn rt cpnum op2 crm, bit4=1.
	 * Verified against arm-linux-gnu-as: mrc p15,0,r10,c13,c0,3 =
	 * 0xEE1DAF70. */
	if (strcmp(mnemonic, "mrc") == 0 && nops == 6) {
		unsigned op1, cpnum, crn, rt, crm, op2;
		op1 = (unsigned)strtoul(ops[1], NULL, 0);
		cpnum = (unsigned)strtoul(ops[0][0]=='p'?ops[0]+1:ops[0], NULL, 0);
		crn   = (unsigned)strtoul(ops[3][0]=='c'?ops[3]+1:ops[3], NULL, 0);
		crm   = (unsigned)strtoul(ops[4][0]=='c'?ops[4]+1:ops[4], NULL, 0);
		op2   = (unsigned)strtoul(ops[5], NULL, 0);
		if (reg_num(ops[2], (int *)&rt) < 0) return -1;
		emit32(out->bytes,
		       0xEE000000 | (0xE << 28) /* cond AL, 1110 MRC */
		       | ((op1 & 7) << 21) | (1u << 20) /* L=1 */
		       | ((crn & 15) << 16) | ((rt & 15) << 12)
		       | ((cpnum & 15) << 8) | ((op2 & 7) << 5)
		       | (1u << 4) | (crm & 15));
		return 0;
	}

	/* ---- push/pop (pseudo-instructions) ---- */
	if (strcmp(mnemonic, "push") == 0 && nops >= 1) {
		uint16_t reglist = 0;
		if (parse_reglist(ops[0], &reglist) < 0) return -1;
		emit32(out->bytes, 0xE92D0000 | reglist);
		return 0;
	}
	if (strcmp(mnemonic, "pop") == 0 && nops >= 1) {
		uint16_t reglist = 0;
		if (parse_reglist(ops[0], &reglist) < 0) return -1;
		emit32(out->bytes, 0xE8BD0000 | reglist);
		return 0;
	}

/* ---- Data processing: add/sub/and/orr/eor/bic/mov/cmp etc. ---- */
	static const struct { const char *name; uint32_t opcode; } dp_ops[] = {
		{"and", 0}, {"eor", 1}, {"sub", 2}, {"rsb", 3},
		{"add", 4}, {"adc", 5}, {"sbc", 6}, {"rsc", 7},
		{"tst", 8}, {"teq", 9}, {"cmp", 10}, {"cmn", 11},
		{"orr", 12}, {"mov", 13}, {"bic", 14}, {"mvn", 15},
		{0, 0}
	};

	/* Parse condition suffix (e.g., "addgt" → "add", cond=12) */
	const char *dp_base = mnemonic;
	int cond_code = 14; /* AL */
	(void)parse_op_cond(mnemonic, &dp_base, &cond_code);

	/* Parse the S ("set flags") suffix: adds/adcs/subs/sbcs/rsbs/ands/
	 * orrs/eors/bics/movs/mvns.  The 64-bit (Kl) decomposition of mcc
	 * uses adds/adcs/subs/sbcs/rsbs to chain carry across the low and
	 * high 32-bit halves.  S is bit 20 of the data-processing word.
	 * Note: parse_op_cond() above greedily matches "cs" as a condition
	 * for "adcs"/"sbcs"; when the trailing 's' really is the S-bit
	 * (stripping it yields a valid opcode), there is no condition
	 * suffix, so reset the condition code to AL. */
	int setflags = 0;
	{
		size_t dlen = strlen(mnemonic);
		if (dlen > 3 && mnemonic[dlen-1] == 's') {
			static char sbase[16];
			size_t blen = dlen - 1;
			if (blen < 16) {
				memcpy(sbase, mnemonic, blen);
				sbase[blen] = '\0';
				for (int d = 0; dp_ops[d].name; d++)
					if (strcmp(sbase, dp_ops[d].name) == 0) {
						dp_base = sbase;
						setflags = 1;
						cond_code = 14; /* AL */
						break;
					}
			}
		}
	}

	if (nops >= 2) {
		for (int d = 0; dp_ops[d].name; d++) {
			if (strcmp(dp_base, dp_ops[d].name) != 0) continue;
			uint32_t opc = dp_ops[d].opcode;
			uint32_t cond = (uint32_t)cond_code << 28;
			uint32_t sbit = (uint32_t)setflags << 20;

			if (nops == 2 && (opc == 13 || opc == 15)) {
				/* mov/mvn rd, rm  or  mov/mvn rd, #imm */
				int rd; if (reg_num(ops[0], &rd) < 0) return -1;
				if (ops[1][0] == '#') {
					int imm = 0; sscanf(ops[1]+1, "%i", &imm);
					uint32_t op2;
					if (arm_imm_encode(imm, &op2) < 0) return -1;
					emit32(out->bytes, cond | sbit | 0x3A00000 | (opc==15?0x600000:0) | (rd<<12) | op2);
					return 0;
				}
				int rm; if (reg_num(ops[1], &rm) < 0) return -1;
				emit32(out->bytes, cond | sbit | 0x1A00000 | (opc==15?0x600000:0) | (rd<<12) | rm);
				return 0;
			}

			if (nops == 2 && (opc == 10 || opc == 11 || opc == 8 || opc == 9)) {
				/* cmp/cmn/tst/teq rn, rm  or  cmp rn, #imm
				 * (these always set flags; the S bit is fixed) */
				int rn; if (reg_num(ops[0], &rn) < 0) return -1;
				if (ops[1][0] == '#') {
					int imm = 0; sscanf(ops[1]+1, "%i", &imm);
					uint32_t op2;
					if (arm_imm_encode(imm, &op2) < 0) return -1;
					emit32(out->bytes, cond | sbit | 0x3500000 | (opc<<21) | (rn<<16) | op2);
					return 0;
				}
				int rm; if (reg_num(ops[1], &rm) < 0) return -1;
				emit32(out->bytes, cond | sbit | 0x1500000 | (opc<<21) | (rn<<16) | rm);
				return 0;
			}

			if (nops >= 3) {
				/* Three-address: add/sub/and/orr/eor/bic rd, rn, rm
				 * or rd, rn, #imm  or rd, rn, rm, LSL #N */
				int rd; if (reg_num(ops[0], &rd) < 0) return -1;
				int rn; if (reg_num(ops[1], &rn) < 0) return -1;

				if (ops[2][0] == '#') {
					/* TLS local-exec modifiers (mcc):
					 *   add rd, rn, #:tprel_hi12:sym  → upper 12 bits of TP offset
					 *   add rd, rn, #:tprel_lo12:sym  → lower 12 bits of TP offset
					 * Both use R_ARM_TLS_LE12 (110): a 12-bit TP-relative
					 * value placed in the add immediate field, patched by
					 * the linker.  hi12/lo12 differ only in which 12-bit
					 * slice of the offset the linker fills in. */
					if (strncmp(ops[2], "#:tprel_hi12:", 13) == 0 ||
					    strncmp(ops[2], "#:tprel_lo12:", 13) == 0) {
						int tls_hi = ops[2][8] == 'h';
						const char *sym = ops[2] + 13;
						out->fixed = 0;
						out->reloc_type = tls_hi ? 111 : 110;
						set_fixup(out, 0, 4, tls_hi ? 111 : 110, sym, 0);
						/* add rd, rn, #imm12 (I=1): shifter operand field
						 * (bits 11:0) left zero; linker fills it. */
						emit32(out->bytes,
						       cond | sbit | (opc<<21)
						       | (1<<25) | (rn<<16) | (rd<<12));
						return 0;
					}
					/* Immediate: rd, rn, #imm */
					int imm = 0; sscanf(ops[2]+1, "%i", &imm);
					uint32_t op2;
					if (arm_imm_encode(imm, &op2) < 0) return -1;
					emit32(out->bytes, cond | sbit | 0x2000000 | (opc<<21) | (1<<25) | (rn<<16) | (rd<<12) | op2);
					return 0;
				}

				/* Register: rd, rn, rm  or  rd, rn, rm, {LSL|LSR|ASR|ROR} #N */
				int rm; if (reg_num(ops[2], &rm) < 0) return -1;

				if (nops >= 4) {
					/* Barrel shifter: rd, rn, rm, SHIFT #N */
					uint32_t shift_type = 0; /* 0=LSL, 1=LSR, 2=ASR, 3=ROR */
					const char *sh = ops[3];
					if (strncmp(sh, "LSL", 3) == 0) shift_type = 0;
					else if (strncmp(sh, "LSR", 3) == 0) shift_type = 1;
					else if (strncmp(sh, "ASR", 3) == 0) shift_type = 2;
					else if (strncmp(sh, "ROR", 3) == 0) shift_type = 3;
					else return -1;
					const char *sv = sh + 3;
					while (*sv == ' ' || *sv == '\t' || *sv == '#') sv++;
					int shift_amt = 0; sscanf(sv, "%i", &shift_amt);
				emit32(out->bytes, cond | sbit | 0xE0000000 | (opc<<21) | (rn<<16) | (rd<<12)
				       | (shift_amt<<7) | (shift_type<<5) | rm);
				return 0;
			}

			emit32(out->bytes, cond | sbit | 0xE0000000 | (opc<<21) | (rn<<16) | (rd<<12) | rm);
				return 0;
			}

			if (nops == 2) {
				/* Two-address: {add,sub,and,...} rd, rm */
				int rd; if (reg_num(ops[0], &rd) < 0) return -1;
				int rm; if (reg_num(ops[1], &rm) < 0) return -1;
				emit32(out->bytes, cond | sbit | 0xE0000000 | (opc<<21) | (rd<<16) | (rd<<12) | rm);
				return 0;
			}
		}
	}

	/* ---- mul r0, r1, r2 ---- */
	if (strcmp(mnemonic, "mul") == 0 && nops >= 3) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		int rm; if (reg_num(ops[1], &rm) < 0) return -1;
		int rs; if (reg_num(ops[2], &rs) < 0) return -1;
		emit32(out->bytes, 0xE0000090 | (rd<<16) | (rs<<8) | rm);
		return 0;
	}

	/* ---- clz r0, r1 ---- */
	if (strcmp(mnemonic, "clz") == 0 && nops >= 2) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		int rm; if (reg_num(ops[1], &rm) < 0) return -1;
		emit32(out->bytes, 0xE16F0F10 | (rd<<12) | rm);
		return 0;
	}

	/* ---- asr/lsr/lsl/ror rd, rm, #N (standalone shifts) ---- */
	{
		const char *shift_mn[] = {"lsl", "lsr", "asr", "ror", 0};
		int shift_type = -1;
		const char *shift_base = mnemonic;
		int shift_cond = 14;
		(void)parse_op_cond(mnemonic, &shift_base, &shift_cond);
		for (int i = 0; shift_mn[i]; i++)
			if (strcmp(shift_base, shift_mn[i]) == 0) { shift_type = i; break; }
		if (shift_type >= 0 && nops >= 3) {
			int rd; if (reg_num(ops[0], &rd) < 0) return -1;
			int rm; if (reg_num(ops[1], &rm) < 0) return -1;
			int amt = 0; const char *sv = ops[2];
			while (*sv == '#' || *sv == ' ') sv++;
			sscanf(sv, "%i", &amt);
			emit32(out->bytes, ((uint32_t)shift_cond<<28) | 0x1A00000 | (rd<<12) | (amt<<7) | (shift_type<<5) | rm);
			return 0;
		}
		/* Two-operand: asr/lsr/lsl/ror rd, rm (shift by rN instead of #N) */
		/* Actually, these are encoded as MOV rd, rm, SHIFT rs. */
		/* But GCC rarely emits these as standalone; handle register shifts
		 * if the third operand is a register name. */
		if (shift_type >= 0 && nops >= 2) {
			/* Shift by register: asr rd, rm, rs */
			if (nops >= 3 && ops[2][0] != '#') {
				int rd; if (reg_num(ops[0], &rd) < 0) return -1;
				int rm; if (reg_num(ops[1], &rm) < 0) return -1;
				int rs; if (reg_num(ops[2], &rs) < 0) return -1;
				emit32(out->bytes, ((uint32_t)shift_cond<<28) | 0x1A00010 | (rd<<12) | (rs<<8) | (shift_type<<5) | rm);
				return 0;
			}
			/* Two operand: asr rd, rm (shift by r0 implicitly) */
			/* This is mostly used as alias, fall through to below */
		}
	}

	/* ---- Conditional branching (b<cond> label) ---- */
	if (mnemonic[0] == 'b' && strcmp(mnemonic, "bl") != 0 && strcmp(mnemonic, "bx") != 0 && strcmp(mnemonic, "blx") != 0) {
		static const struct { const char *s; int c; } conds[] = {
			{"eq",0}, {"ne",1}, {"cs",2}, {"cc",3}, {"mi",4}, {"pl",5},
			{"vs",6}, {"vc",7}, {"hi",8}, {"ls",9}, {"ge",10}, {"lt",11},
			{"gt",12}, {"le",13}, {"al",14}, {0,0}
		};
		const char *cs = mnemonic + 1;
		for (int i = 0; conds[i].s; i++) {
			if (strcmp(cs, conds[i].s) == 0) {
				out->fixed = 0;
				out->reloc_type = 1; /* R_ARM_PC24 */
				set_fixup(out, 0, 4, out->reloc_type, nops > 0 ? ops[0] : operand_text, 0);
				emit32(out->bytes, 0x0A000000 | (conds[i].c << 28));
				return 0;
			}
		}
	}

	/* ---- movw r0, #imm (or #:lower16:sym) ---- */
	if (strcmp(mnemonic, "movw") == 0 && nops >= 2) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		const char *op = ops[1];
		if (op[0] == '#' && op[1] == ':') {
			/* Symbol modifier: #:lower16:sym → R_ARM_MOVW_ABS_NC.
			 * Match the exact ":lower16:" prefix — the old char-set
			 * skip (which included 'r','e','1','6') would eat leading
			 * characters of the symbol itself, e.g. `remfn` became
			 * `mfn` and `r1` (a valid symbol name on arm, which also
			 * names a register) was mis-parsed. */
			const char *sym = op + 2;
			if (strncmp(sym, "lower16:", 8) == 0)
				sym += 8;
			else
				return -1;
			out->fixed = 0;
			out->reloc_type = 43; /* R_ARM_MOVW_ABS_NC */
			set_fixup(out, 0, 4, 43, sym, 0);
			emit32(out->bytes, 0xE3000000 | (rd<<12));
			return 0;
		}
		if (op[0] == '#') {
			int imm = 0; sscanf(op+1, "%i", &imm);
			uint32_t imm16 = (uint32_t)imm & 0xFFFF;
			emit32(out->bytes, 0xE3000000 | (rd<<12) | ((imm16>>12)&0xF)<<16 | (imm16 & 0xFFF));
			return 0;
		}
		return -1;
	}

	/* ---- movt r0, #imm (or #:upper16:sym) ---- */
	if (strcmp(mnemonic, "movt") == 0 && nops >= 2) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		const char *op = ops[1];
		if (op[0] == '#' && op[1] == ':') {
			/* Symbol modifier: #:upper16:sym → R_ARM_MOVT_ABS.
			 * Exact ":upper16:" prefix match (see movw above). */
			const char *sym = op + 2;
			if (strncmp(sym, "upper16:", 8) == 0)
				sym += 8;
			else
				return -1;
			out->fixed = 0;
			out->reloc_type = 44; /* R_ARM_MOVT_ABS */
			set_fixup(out, 0, 4, 44, sym, 0);
			emit32(out->bytes, 0xE3400000 | (rd<<12));
			return 0;
		}
		if (op[0] == '#') {
			int imm = 0; sscanf(op+1, "%i", &imm);
			/* GNU convention: the operand is the 16-bit value placed in
			 * the upper half (e.g. `movt r0, #0x3b9a` loads 0x3b9a0000).
			 * mcc emits `movt rd, #0x<(n>>16)&0xffff>` accordingly. */
			uint32_t imm16 = (uint32_t)imm & 0xFFFF;
			emit32(out->bytes, 0xE3400000 | (rd<<12) | ((imm16>>12)&0xF)<<16 | (imm16 & 0xFFF));
			return 0;
		}
		return -1;
	}

	/* ---- ldr rd, =sym (load address of global symbol) ---- */
	if (strcmp(mnemonic, "ldr") == 0 && nops >= 2) {
		int rd; if (reg_num(ops[0], &rd) < 0) goto ldr_mem;
		const char *mem = ops[1];
		if (mem[0] == '=') {
			/* =sym → R_ARM_ABS32 fixup on a PC-relative load */
			const char *sym = mem + 1;
			out->fixed = 0;
			out->reloc_type = 2; /* R_ARM_ABS32 */
			set_fixup(out, 0, 4, 2, sym, 0);
			/* Encode as literal-pool load: ldr rd, [pc, #0] */
			emit32(out->bytes, 0xE51F0000 | (rd<<12));
			return 0;
		}
	}
	/* fall through to ldr/str memory handling */

	/* ---- Memory: ldr/ldrb/ldrh/str/strb/strh rd, [rn] or rd, [rn, #off] ---- */
ldr_mem:
	{
		int is_load = 0, is_byte = 0, is_half = 0;
		if (strcmp(mnemonic, "ldr") == 0) { is_load = 1; }
		else if (strcmp(mnemonic, "str") == 0) { is_load = 0; }
		else if (strcmp(mnemonic, "ldrb") == 0) { is_load = 1; is_byte = 1; }
		else if (strcmp(mnemonic, "strb") == 0) { is_load = 0; is_byte = 1; }
		else if (strcmp(mnemonic, "ldrh") == 0) { is_load = 1; is_half = 1; }
		else if (strcmp(mnemonic, "strh") == 0) { is_load = 0; is_half = 1; }
		else if (strcmp(mnemonic, "ldrsb") == 0) { is_load = 1; is_half = 1; is_byte = 1; }
		else if (strcmp(mnemonic, "ldrsh") == 0) { is_load = 1; is_half = 1; is_byte = 1; }
		else is_load = -1; /* skip this block */

		if (is_load >= 0 && nops >= 2) {
			int rd = 0, rn;
			if (reg_num(ops[0], &rd) < 0) return -1;
			const char *mem = ops[1];
			if (mem[0] == '[') mem++;
			char rn_str[16]; int i = 0;
			while (*mem && *mem != ']' && *mem != ',' && *mem != '#' && i < 15)
				rn_str[i++] = *mem++;
			rn_str[i] = '\0';
			if (reg_num(rn_str, &rn) < 0) return -1;
			int32_t signed_off = 0;
			if (*mem == ',' || *mem == '#') {
				while (*mem && (*mem == ',' || *mem == ' ' || *mem == '#')) mem++;
				sscanf(mem, "%i", &signed_off);
			}
			int U = (signed_off >= 0) ? 1 : 0;
			uint32_t off = (uint32_t)(signed_off >= 0 ? signed_off : -signed_off);
			if (is_half) {
				uint32_t hi = (off & 0xF0) << 4;
				uint32_t lo = off & 0xF;
				if (strcmp(mnemonic, "ldrsb") == 0)
					emit32(out->bytes, 0xE1D000D0 | (rn<<16) | (rd<<12) | hi | lo);
				else if (strcmp(mnemonic, "ldrsh") == 0)
					emit32(out->bytes, 0xE1D000F0 | (rn<<16) | (rd<<12) | hi | lo);
				else if (is_load)
					emit32(out->bytes, 0xE1D000B0 | (rn<<16) | (rd<<12) | hi | lo);
				else
					emit32(out->bytes, 0xE1C000B0 | (rn<<16) | (rd<<12) | hi | lo);
			} else if (is_byte) {
				uint32_t mem_base = 0xE5000000 | ((uint32_t)U << 23) | (1<<22) | ((uint32_t)is_load << 20);
				emit32(out->bytes, mem_base | (rn<<16) | (rd<<12) | off);
			} else {
				uint32_t mem_base = 0xE5000000 | ((uint32_t)U << 23) | ((uint32_t)is_load << 20);
				emit32(out->bytes, mem_base | (rn<<16) | (rd<<12) | off);
			}
			return 0;
		}
	}

	/* ---- VFP load/store: flds/fsts/fldd/fstd rd, [rn, #off] ---- */
	if (nops >= 2 && (strcmp(mnemonic, "flds") == 0 ||
	    strcmp(mnemonic, "fsts") == 0 ||
	    strcmp(mnemonic, "fldd") == 0 ||
	    strcmp(mnemonic, "fstd") == 0)) {
		int rd, rn;
		if (reg_num(ops[0], &rd) < 0) return -1;
		int is_double = (mnemonic[3] == 'd');
		int is_load = (mnemonic[1] == 'l');
		const char *mem = ops[1];
		if (mem[0] == '[') mem++;
		char rn_str[16]; int i2 = 0;
		while (*mem && *mem != ']' && *mem != ',' && *mem != '#' && i2 < 15)
			rn_str[i2++] = *mem++;
		rn_str[i2] = '\0';
		if (reg_num(rn_str, &rn) < 0) return -1;
		uint32_t off = 0;
		if (*mem == ',' || *mem == '#') {
			while (*mem && (*mem == ',' || *mem == ' ' || *mem == '#')) mem++;
			sscanf(mem, "%u", &off);
		}
		if (off > 1023) off = 1023;
		uint32_t base = is_load ? 0xED100A00 : 0xED000A00;
		if (is_double) base |= 0x100;
		/* The register field differs by precision: a double dN uses
		 * D=bit4, Vd=N&0xF (dp_dst_field); a single sN uses D=N&1,
		 * Vd=N>>1 (sp_dst_field).  Previously both used the single
		 * layout, so fldd/fstd on even-numbered high double regs
		 * (e.g. d8 -> Vd=4) saved/restored the WRONG register. */
		base |= (rn << 16)
		      | (is_double ? dp_dst_field(rd) : sp_dst_field(sp_reg(rd)))
		      | (off & 0xFF);
		emit32(out->bytes, base);
		return 0;
	}

	/* ---- VFPv3 unified syntax (mcc output): vadd.f64/vadd.f32/... ----
	 * Encodings verified against arm-linux-gnu-as:
	 *   vadd.f64 d0, d2, d1 = 0xee320b01
	 *   vadd.f32 s0, s0, s1 = 0xee300a20 */
	if (nops >= 3 && mnemonic[0] == 'v') {
		int is_f64 = (strstr(mnemonic, ".f64") != NULL);
		int is_f32 = (strstr(mnemonic, ".f32") != NULL);
		uint32_t base = 0;
		if (strncmp(mnemonic, "vadd", 4) == 0) base = is_f64 ? 0xEE300B00 : (is_f32 ? 0xEE300A00 : 0);
		else if (strncmp(mnemonic, "vsub", 4) == 0) base = is_f64 ? 0xEE300B40 : (is_f32 ? 0xEE300A40 : 0);
		else if (strncmp(mnemonic, "vmul", 4) == 0) base = is_f64 ? 0xEE200B00 : (is_f32 ? 0xEE200A00 : 0);
		else if (strncmp(mnemonic, "vdiv", 4) == 0) base = is_f64 ? 0xEE800B00 : (is_f32 ? 0xEE800A00 : 0);
		if (base != 0) {
			int vd, vn, vm;
			if (reg_num(ops[0], &vd) < 0 || reg_num(ops[1], &vn) < 0 ||
			    reg_num(ops[2], &vm) < 0)
				return -1;
			if (is_f64) {
				base |= dp_dst_field(vd) | ((uint32_t)(vn & 0xF) << 16)
				     | ((uint32_t)((vn >> 4) & 1) << 7) | dp_src_field(vm);
			} else {
				vd = sp_reg(vd); vn = sp_reg(vn); vm = sp_reg(vm);
				base |= sp_dst_field(vd) | ((uint32_t)(vn >> 1) << 16)
				     | ((uint32_t)(vn & 1) << 7) | sp_src_field(vm);
			}
			emit32(out->bytes, base);
			return 0;
		}
	}

	/* ---- VFPv3 unary/compare (2 operands): vneg/vcmp ---- */
	if (nops >= 2 && mnemonic[0] == 'v') {
		int is_f64 = (strstr(mnemonic, ".f64") != NULL);
		int is_f32 = (strstr(mnemonic, ".f32") != NULL);
		uint32_t base = 0;
		if (strncmp(mnemonic, "vneg", 4) == 0) base = is_f64 ? 0xEEB10B40 : (is_f32 ? 0xEEB10A40 : 0);
		else if (strncmp(mnemonic, "vcmp", 4) == 0) base = is_f64 ? 0xEEB40B40 : (is_f32 ? 0xEEB40A40 : 0);
		if (base != 0) {
			int vd, vm;
			if (reg_num(ops[0], &vd) < 0 || reg_num(ops[1], &vm) < 0)
				return -1;
			if (is_f64) {
				base |= dp_dst_field(vd) | dp_src_field(vm);
			} else {
				vd = sp_reg(vd); vm = sp_reg(vm);
				base |= sp_dst_field(vd) | sp_src_field(vm);
			}
			emit32(out->bytes, base);
			return 0;
		}
	}

	/* ---- VFP convert: vcvt.<dst>.<src> ---- */
	if (nops >= 2 && strncmp(mnemonic, "vcvt", 4) == 0) {
		int vd, vm;
		if (reg_num(ops[0], &vd) < 0 || reg_num(ops[1], &vm) < 0)
			return -1;
		uint32_t base = 0;
		int dst_f64 = 0, src_f64 = 0;
		const char *m = mnemonic;
		if (strstr(m, ".f64.s32")) { base = 0xEEB80BC0; dst_f64 = 1; }
		else if (strstr(m, ".f64.u32")) { base = 0xEEB80B40; dst_f64 = 1; }
		else if (strstr(m, ".s32.f64")) { base = 0xEEBD0BC0; src_f64 = 1; }
		else if (strstr(m, ".u32.f64")) { base = 0xEEBC0BC0; src_f64 = 1; }
		else if (strstr(m, ".s32.f32")) { base = 0xEEBD0AC0; }
		else if (strstr(m, ".u32.f32")) { base = 0xEEBC0AC0; }
		else if (strstr(m, ".f64.f32")) { base = 0xEEB70AC0; dst_f64 = 1; }
		else if (strstr(m, ".f32.f64")) { base = 0xEEB70BC0; src_f64 = 1; }
		else if (strstr(m, ".f32.s32")) { base = 0xEEB80AC0; }
		else if (strstr(m, ".f32.u32")) { base = 0xEEB80A40; }
		else return -1;
		vd = sp_reg(vd); vm = sp_reg(vm);
		base |= dst_f64 ? dp_dst_field(vd) : sp_dst_field(vd);
		base |= src_f64 ? dp_src_field(vm) : sp_src_field(vm);
		emit32(out->bytes, base);
		return 0;
	}

	/* ---- VFP ALU: fadds/fsubs/fmuls/fdivs/fcpys/fnegs/fabss sd, sm ---- */
	if (nops >= 2) {
		static const struct { const char *name; uint32_t base; } vfp_alu[] = {
			{"fadds", 0x0E300A00}, {"fsubs", 0x0E300A40},
			{"fmuls", 0x0E200A00}, {"fdivs", 0x0E800A00},
			{"fcpys", 0x0EB00A40}, {"fnegs", 0x0EB10A40},
			{"fabss", 0x0EB00AC0},
			{"faddd", 0x0E300B00}, {"fsubd", 0x0E300B40},
			{"fmuld", 0x0E200B00}, {"fdivd", 0x0E800B00},
			{"fcpyd", 0x0EB00B40}, {"fnegd", 0x0EB10B40},
			{"fabsd", 0x0EB00BC0},
			{0, 0}
		};
		for (int vi = 0; vfp_alu[vi].name; vi++) {
			if (strcmp(mnemonic, vfp_alu[vi].name) != 0) continue;
			int rd, rm;
			if (reg_num(ops[0], &rd) < 0) return -1;
			if (reg_num(ops[1], &rm) < 0) return -1;
			uint32_t d = (uint32_t)(rd & 0x1F);
			uint32_t m = (uint32_t)(rm & 0x1F);
			uint32_t base = vfp_alu[vi].base;
			base |= ((d & 1) << 22) | ((d >> 1) << 12);
			base |= ((m & 1) << 5) | ((m >> 1) << 16);
			emit32(out->bytes, base);
			return 0;
		}
	}

	/* ---- VFP compare: fcmps/fcmpd sd, sm ---- */
	if (nops >= 2) {
		int is_double = (mnemonic[4] == 'd' || mnemonic[4] == 0);
		if (strcmp(mnemonic, "fcmps") == 0 || strcmp(mnemonic, "fcmpd") == 0) {
			int rd, rm;
			if (reg_num(ops[0], &rd) < 0) return -1;
			if (reg_num(ops[1], &rm) < 0) return -1;
			uint32_t d = (uint32_t)(rd & 0x1F);
			uint32_t m = (uint32_t)(rm & 0x1F);
			uint32_t base = is_double ? 0x0EB40B40 : 0x0EB40A40;
			base |= ((d & 1) << 22) | ((d >> 1) << 12);
			base |= ((m & 1) << 5) | ((m >> 1) << 16);
			emit32(out->bytes, base);
			return 0;
		}
	}

	/* ---- sdiv/udiv rd, rn, rm ---- */
	if (nops >= 3) {
		if (strcmp(mnemonic, "sdiv") == 0 || strcmp(mnemonic, "udiv") == 0) {
			int rd, rn, rm;
			if (reg_num(ops[0], &rd) < 0) return -1;
			if (reg_num(ops[1], &rn) < 0) return -1;
			if (reg_num(ops[2], &rm) < 0) return -1;
			/* ARM SDIV/UDIV bit fields: Rd = Rn / Rm with
			 *   Rd: bits 19-16, Rm: bits 11-8, Rn: bits 3-0.
			 * Division is NOT commutative, so Rn and Rm must land in
			 * the right fields (unlike mul where the operands could
			 * be swapped without changing the result). */
			uint32_t base = (mnemonic[0] == 's') ? 0xE710F010 : 0xE730F010;
			emit32(out->bytes, base | (rd<<16) | (rm<<8) | rn);
			return 0;
		}
	}

	/* ---- smull/umull rdlo, rdhi, rn, rm ---- */
	if (nops >= 4) {
		if (strcmp(mnemonic, "smull") == 0 || strcmp(mnemonic, "umull") == 0) {
			int rdlo, rdhi, rn, rm;
			if (reg_num(ops[0], &rdlo) < 0) return -1;
			if (reg_num(ops[1], &rdhi) < 0) return -1;
			if (reg_num(ops[2], &rn) < 0) return -1;
			if (reg_num(ops[3], &rm) < 0) return -1;
			uint32_t base = (mnemonic[0] == 's') ? 0xE0C00090 : 0xE0800090;
			emit32(out->bytes, base | (rdhi<<16) | (rdlo<<12) | (rn<<8) | rm);
			return 0;
		}
	}

	/* ---- rev/rev16/revsh rd, rm ---- */
	if (nops >= 2) {
		if (strcmp(mnemonic, "rev") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			emit32(out->bytes, 0xE6BF0F30 | (rd<<12) | rm); return 0;
		}
		if (strcmp(mnemonic, "rev16") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			emit32(out->bytes, 0xE6BF0FB0 | (rd<<12) | rm); return 0;
		}
		if (strcmp(mnemonic, "revsh") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			emit32(out->bytes, 0xE6FF0FB0 | (rd<<12) | rm); return 0;
		}
	}

	/* ---- ldmia/stmia rn, {rlist} ---- */
	if (nops >= 2 && (strcmp(mnemonic, "ldmia") == 0 || strcmp(mnemonic, "stmia") == 0)) {
		char rn_str[16];
		size_t rl = strlen(ops[0]);
		int wb = 0;
		if (rl > 0 && ops[0][rl-1] == '!') {
			/* writeback suffix: rn! */
			wb = 1;
			memcpy(rn_str, ops[0], rl - 1);
			rn_str[rl - 1] = '\0';
		} else {
			if (rl >= sizeof rn_str) return -1;
			memcpy(rn_str, ops[0], rl + 1);
		}
		int rn; if (reg_num(rn_str, &rn) < 0) return -1;
		uint16_t reglist = 0;
		if (parse_reglist(ops[1], &reglist) < 0) return -1;
		int is_load = (mnemonic[0] == 'l');
		uint32_t enc = (is_load ? 0xE8B00000 : 0xE8A00000) | ((uint32_t)rn << 16) | reglist;
		if (wb) enc |= (1u << 21);
		emit32(out->bytes, enc);
		return 0;
	}

	/* ---- VFP convert: fcvtsd (double->single), fcvtds (single->double) ---- */
	if (nops >= 2) {
		if (strcmp(mnemonic, "fcvtsd") == 0 || strcmp(mnemonic, "fcvtds") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			int to_double = (mnemonic[4] == 'd');
			uint32_t d = (uint32_t)(rd & 0x1F), m = (uint32_t)(rm & 0x1F);
			uint32_t base = to_double ? 0x0EB70BC0 : 0x0EB70AC0;
			base |= ((d & 1) << 22) | ((d >> 1) << 12);
			base |= ((m & 1) << 5) | ((m >> 1) << 16);
			emit32(out->bytes, base); return 0;
		}
	}

	/* ---- VFP <-> int conversion: fsitos/ftosiz/ftosis/fcvt = convert ---- */
	if (nops >= 2) {
		if (strcmp(mnemonic, "fsitos") == 0 || strcmp(mnemonic, "fsitod") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			int is_double = (mnemonic[5] == 'd');
			uint32_t d = (uint32_t)(rd & 0x1F), m = (uint32_t)(rm & 0x1F);
			uint32_t base = is_double ? 0x0EB80BC0 : 0x0EB80AC0;
			base |= ((d & 1) << 22) | ((d >> 1) << 12);
			base |= ((m & 1) << 5) | ((m >> 1) << 16);
			emit32(out->bytes, base); return 0;
		}
		if (strcmp(mnemonic, "ftosis") == 0 || strcmp(mnemonic, "ftosizs") == 0 ||
		    strcmp(mnemonic, "ftosid") == 0 || strcmp(mnemonic, "ftosizd") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			int is_double = (mnemonic[5] == 'd') || (mnemonic[6] == 'd');
			int is_roundz = (mnemonic[5] == 'z');
			uint32_t d = (uint32_t)(rd & 0x1F), m = (uint32_t)(rm & 0x1F);
			uint32_t base = is_double ? 0x0EBD0BC0 : 0x0EBD0AC0;
			if (is_roundz) base |= (1 << 16); /* use rm[0] bit to select round mode */
			base |= ((d & 1) << 22) | ((d >> 1) << 12);
			base |= ((m & 1) << 5) | ((m >> 1) << 16);
			emit32(out->bytes, base); return 0;
		}
	}

	/* ---- VFP move: vmov between core and VFP registers ----
	 * Encodings verified against arm-linux-gnu-as:
	 *   vmov s1, r0 = 0xee000a90   (core -> single)
	 *   vmov r0, s1 = 0xee100a90   (single -> core)
	 *   vmov.f32 s2, s0 = 0xeeb01a40 (fcpy.s)
	 *   vmov.f64 d3, d1 = 0xeeb03b41 (fcpy.d)
	 *   vmov d0, r0, r1 = 0xec410b10 (MCRR, core pair -> double)
	 *   vmov r0, r1, d0 = 0xec510b10 (MRRC, double -> core pair) */
	if (nops >= 2 && strcmp(mnemonic, "vmov") == 0) {
		int a0 = ops[0][0], a1 = ops[1][0];
		if (nops == 3 && a0 == 'd' && a1 == 'r' && ops[2][0] == 'r') {
			/* vmov dN, rM, rM2 — core register pair -> VFP double.
			 * MCRR p11, 0, rM2, rM, cN:  Vd[3:0] in CRm, Vd[4] in
			 * bit 5.  Encoded by GNU as as 0xEC400B10 plus the
			 * Rt/Rt2/Vd fields (verified for d0/d5/d16/d31). */
			int vd, rm, rm2;
			if (reg_num(ops[0], &vd) < 0 ||
			    reg_num(ops[1], &rm) < 0 ||
			    reg_num(ops[2], &rm2) < 0) return -1;
			uint32_t base = 0xEC400B10;
			base |= ((uint32_t)(vd >> 4) & 1) << 5;
			base |= ((uint32_t)rm2 & 0xF) << 16;
			base |= ((uint32_t)rm & 0xF) << 12;
			base |= (uint32_t)(vd & 0xF);
			emit32(out->bytes, base);
			return 0;
		}
		if (nops == 3 && a0 == 'r' && a1 == 'r' && ops[2][0] == 'd') {
			/* vmov rM, rM2, dN — VFP double -> core register pair.
			 * MRRC p11, 0, rM, rM2, cN. */
			int rd, rd2, vd;
			if (reg_num(ops[0], &rd) < 0 ||
			    reg_num(ops[1], &rd2) < 0 ||
			    reg_num(ops[2], &vd) < 0) return -1;
			uint32_t base = 0xEC500B10;
			base |= ((uint32_t)(vd >> 4) & 1) << 5;
			base |= ((uint32_t)rd2 & 0xF) << 16;
			base |= ((uint32_t)rd & 0xF) << 12;
			base |= (uint32_t)(vd & 0xF);
			emit32(out->bytes, base);
			return 0;
		}
		if ((a0 == 's' || a0 == 'd') && a1 == 'r') {
			/* vmov sN, rM — core -> single.  A double operand means
			 * the LOW half (dN aliases s(2N)); the two-operand
			 * `vmov dN, rM` moves a core register into that half. */
			int vd, rm;
			if (reg_num(ops[0], &vd) < 0 || reg_num(ops[1], &rm) < 0) return -1;
			int sn;
			if (a0 == 'd')
				sn = 2 * vd;   /* dN low half = s(2N); reg_num(dN) == N */
			else
				sn = sp_reg(vd);
			uint32_t base = 0xEE000A10 | ((uint32_t)(sn >> 1) << 16)
			              | ((uint32_t)(sn & 1) << 7) | ((uint32_t)rm << 12);
			emit32(out->bytes, base);
			return 0;
		}
		if (a0 == 'r' && (a1 == 's' || a1 == 'd')) {
			/* vmov rM, sN — single -> core.  A double operand reads
			 * the LOW half (dN aliases s(2N)); the two-operand
			 * `vmov rM, dN` moves that half into a core register. */
			int rd, vd;
			if (reg_num(ops[0], &rd) < 0 || reg_num(ops[1], &vd) < 0) return -1;
			int sn;
			if (a1 == 'd')
				sn = 2 * vd;   /* dN low half = s(2N); reg_num(dN) == N */
			else
				sn = sp_reg(vd);
			uint32_t base = 0xEE100A10 | ((uint32_t)(sn >> 1) << 16)
			              | ((uint32_t)(sn & 1) << 7) | ((uint32_t)rd << 12);
			emit32(out->bytes, base);
			return 0;
		}
		if (a0 == 's' && a1 == 's') {
			/* vmov sN, sM — fcpys */
			int vd, vm;
			if (reg_num(ops[0], &vd) < 0 || reg_num(ops[1], &vm) < 0) return -1;
			vd = sp_reg(vd); vm = sp_reg(vm);
			emit32(out->bytes, 0xEEB00A40 | sp_dst_field(vd) | sp_src_field(vm));
			return 0;
		}
		if (a0 == 'd' && a1 == 'd') {
			/* vmov dN, dM — fcpyd */
			int vd, vm;
			if (reg_num(ops[0], &vd) < 0 || reg_num(ops[1], &vm) < 0) return -1;
			emit32(out->bytes, 0xEEB00B40 | dp_dst_field(vd) | dp_src_field(vm));
			return 0;
		}
		return -1;
	}

	/* ---- preload: pld [rn, #off] ---- */
	if (nops >= 1 && strcmp(mnemonic, "pld") == 0) {
		int rn = 0; uint32_t off = 0;
		const char *mem = ops[0];
		if (mem[0] == '[') mem++;
		char rn_str[16]; int i2 = 0;
		while (*mem && *mem != ']' && *mem != ',' && *mem != '#' && i2 < 15) rn_str[i2++] = *mem++;
		rn_str[i2] = '\0';
		if (reg_num(rn_str, &rn) < 0) return -1;
		if (*mem == ',' || *mem == '#') {
			while (*mem && (*mem == ',' || *mem == ' ' || *mem == '#')) mem++;
			sscanf(mem, "%u", &off);
		}
		emit32(out->bytes, 0xF550F000 | (rn<<16) | off);
		return 0;
	}

	/* ---- sign/zero extension: sxtb/sxth/uxtb/uxth rd, rm ---- */
	if (nops >= 2) {
		if (strcmp(mnemonic, "sxtb") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			emit32(out->bytes, 0xE6AF0070 | (rd<<12) | rm); return 0;
		}
		if (strcmp(mnemonic, "sxth") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			emit32(out->bytes, 0xE6BF0070 | (rd<<12) | rm); return 0;
		}
		if (strcmp(mnemonic, "uxtb") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			emit32(out->bytes, 0xE6EF0070 | (rd<<12) | rm); return 0;
		}
		if (strcmp(mnemonic, "uxth") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			emit32(out->bytes, 0xE6FF0070 | (rd<<12) | rm); return 0;
		}
	}

	/* ---- bit manipulation: rbit rd, rm / clz rd, rm ---- */
	if (nops >= 2 && strcmp(mnemonic, "rbit") == 0) {
		int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
		emit32(out->bytes, 0xE6FF0F30 | (rd<<12) | rm); return 0;
	}

	/* ---- condition select: sel rd, rn, rm ---- */
	if (nops >= 3 && strcmp(mnemonic, "sel") == 0) {
		int rd, rn, rm;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
		emit32(out->bytes, 0xE68000B0 | (rd<<16) | (rn<<0) | (rm<<8));
		return 0;
	}

	/* ---- NEON: vadd/vsub/vand/vorr/veor/vmvn ---- */
	if (mnemonic[0] == 'v') {
		static const struct { const char *name; uint32_t base; int two_op; } neon_int[] = {
			{"vadd", 0xF2000D00, 0}, {"vsub", 0xF3000D00, 0},
			{"vand", 0xF2000D02, 0}, {"vorr", 0xF2200D02, 0},
			{"veor", 0xF3000D02, 0}, {"vmvn", 0xF3B00500, 1},
			{0, 0, 0}
		};
		for (int ni = 0; neon_int[ni].name; ni++) {
			if (strcmp(mnemonic, neon_int[ni].name) != 0) continue;
			int rd, rn = 0, rm = 0;
			if (reg_num(ops[0],&rd)<0) return -1;
			if (neon_int[ni].two_op) {
				if (nops < 2 || reg_num(ops[1],&rm)<0) return -1;
			} else {
				if (nops < 3 || reg_num(ops[1],&rn)<0 || reg_num(ops[2],&rm)<0) return -1;
			}
			uint32_t d=(uint32_t)(rd&0x1F), n=(uint32_t)(rn&0x1F), m=(uint32_t)(rm&0x1F);
			emit32(out->bytes, neon_int[ni].base|((d>>1)<<12)|((d&1)<<22)|((n>>1)<<16)|((n&1)<<7)|((m>>1)<<0)|((m&1)<<5));
			return 0;
		}
	}

	/* ---- NEON: vshl/vshr (immediate) ---- */
	if (nops >= 2 && mnemonic[0] == 'v' && (strncmp(mnemonic, "vshl", 4) == 0 || strncmp(mnemonic, "vshr", 4) == 0)) {
		int rd, rm, imm = 0;
		if (reg_num(ops[0],&rd)<0) return -1;
		if (nops >= 2 && reg_num(ops[1],&rm)<0) return -1;
		if (nops >= 3) sscanf(ops[2], "#%i", &imm);
		uint32_t d=(uint32_t)(rd&0x1F), m=(uint32_t)(rm&0x1F);
		uint32_t base = (mnemonic[1] == 's' && mnemonic[3] == 'l') ? 0xF2800D00 : 0xF2800D00;
		emit32(out->bytes, base|((d>>1)<<12)|((d&1)<<22)|((m>>1)<<0)|((m&1)<<5)|(imm&0x7F)<<16);
		return 0;
	}

	/* ---- mla rd, rn, rm, ra (multiply-accumulate) ---- */
	if (nops >= 4 && strcmp(mnemonic, "mla") == 0) {
		int rd, rn, rm, ra;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0||reg_num(ops[3],&ra)<0) return -1;
		emit32(out->bytes, 0xE0200090 | (rd<<16) | (ra<<12) | (rn<<8) | rm);
		return 0;
	}

	/* ---- mls rd, rn, rm, ra (multiply-subtract) ---- */
	if (nops >= 4 && strcmp(mnemonic, "mls") == 0) {
		int rd, rn, rm, ra;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0||reg_num(ops[3],&ra)<0) return -1;
		/* MLS bit fields: Rd = Ra - Rn*Rm with
		 *   Rd: bits 19-16, Ra: bits 15-12, Rm: bits 11-8, Rn: bits 3-0. */
		emit32(out->bytes, 0xE0600090 | (rd<<16) | (ra<<12) | (rm<<8) | rn);
		return 0;
	}

	/* ---- usad8 rd, rn, rm (unsigned sum of absolute differences) ---- */
	if (nops >= 3 && strcmp(mnemonic, "usad8") == 0) {
		int rd, rn, rm;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
		emit32(out->bytes, 0xE7800F10 | (rd<<16) | (rn<<0) | (rm<<8));
		return 0;
	}

	/* ---- bfi rd, rn, #lsb, #width ---- */
	if (nops >= 4 && strcmp(mnemonic, "bfi") == 0) {
		int rd, rn, lsb, width;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0) return -1;
		sscanf(ops[2], "#%i", &lsb);
		sscanf(ops[3], "#%i", &width);
		uint32_t msb = (uint32_t)(lsb + width - 1);
		emit32(out->bytes, 0xE7C00010 | (rd<<12) | (rn<<0) | (msb<<16) | (lsb<<7));
		return 0;
	}

	/* ---- bfc rd, #lsb, #width ---- */
	if (nops >= 3 && strcmp(mnemonic, "bfc") == 0) {
		int rd, lsb, width;
		if (reg_num(ops[0],&rd)<0) return -1;
		sscanf(ops[1], "#%i", &lsb);
		sscanf(ops[2], "#%i", &width);
		uint32_t msb = (uint32_t)(lsb + width - 1);
		emit32(out->bytes, 0xE7C0001F | (rd<<12) | (msb<<16) | (lsb<<7));
		return 0;
	}

	/* ---- sbfx rd, rn, #lsb, #width ---- */
	if (nops >= 4 && strcmp(mnemonic, "sbfx") == 0) {
		int rd, rn, lsb, width;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0) return -1;
		sscanf(ops[2], "#%i", &lsb);
		sscanf(ops[3], "#%i", &width);
		uint32_t msb = (uint32_t)(lsb + width - 1);
		emit32(out->bytes, 0xE7A00050 | (rd<<12) | (rn<<0) | (msb<<16) | (lsb<<7));
		return 0;
	}

	/* ---- ubfx rd, rn, #lsb, #width ---- */
	if (nops >= 4 && strcmp(mnemonic, "ubfx") == 0) {
		int rd, rn, lsb, width;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0) return -1;
		sscanf(ops[2], "#%i", &lsb);
		sscanf(ops[3], "#%i", &width);
		uint32_t msb = (uint32_t)(lsb + width - 1);
		emit32(out->bytes, 0xE7E00050 | (rd<<12) | (rn<<0) | (msb<<16) | (lsb<<7));
		return 0;
	}

	/* ---- count leading ones: cls rd, rm ---- */
	if (nops >= 2 && strcmp(mnemonic, "cls") == 0) {
		int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
		emit32(out->bytes, 0xE16F0F10 | (rd<<12) | rm); return 0;
	}

	/* ---- hint instructions: wfe/wfi/sev/yield/nop ---- */
	if (nops == 0) {
		if (strcmp(mnemonic, "wfe") == 0) { emit32(out->bytes, 0xE3200F02); return 0; }
		if (strcmp(mnemonic, "wfi") == 0) { emit32(out->bytes, 0xE3200F03); return 0; }
		if (strcmp(mnemonic, "sev") == 0) { emit32(out->bytes, 0xE3200F04); return 0; }
		if (strcmp(mnemonic, "yield") == 0) { emit32(out->bytes, 0xE3200F01); return 0; }
	}

	/* ---- dbg #n ---- */
	if (nops >= 1 && strcmp(mnemonic, "dbg") == 0) {
		int n = 0;
		const char *ds = ops[0];
		while (*ds == '#') ds++;
		if (*ds) sscanf(ds, "%i", &n);
		emit32(out->bytes, 0xE3200F10 | (n & 0xF)); return 0;
	}

	/* ---- VFP sqrt: fsqrts/fsqrtd sd, sm ---- */
	if (nops >= 2) {
		if (strcmp(mnemonic, "fsqrts") == 0 || strcmp(mnemonic, "fsqrtd") == 0) {
			int rd, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
			int is_double = (mnemonic[5] == 'd');
			uint32_t d=(uint32_t)(rd&0x1F), m=(uint32_t)(rm&0x1F);
			uint32_t base = is_double ? 0x0EB10BC0 : 0x0EB10AC0;
			base |= ((d&1)<<22)|((d>>1)<<12)|((m&1)<<5)|((m>>1)<<16);
			emit32(out->bytes, base); return 0;
		}
	}

	/* ---- VFP move immediate: fconsts/fconstd sd, #imm ---- */
	if (nops >= 2 && (strcmp(mnemonic, "fconsts") == 0 || strcmp(mnemonic, "fconstd") == 0)) {
		int rd; if (reg_num(ops[0],&rd)<0) return -1;
		int imm = 0;
		sscanf(ops[1], "#%i", &imm);
		int is_double = (mnemonic[6] == 'd');
		uint32_t d = (uint32_t)(rd & 0x1F);
		uint32_t imm8 = (uint32_t)(imm & 0xFF);
		uint32_t base = is_double ? 0x0EB00B00 : 0x0EB00A00;
		base |= ((d&1)<<22)|((d>>1)<<12)|imm8;
		emit32(out->bytes, base); return 0;
	}

	/* ---- pkhtb/pkhbt rd, rn, rm ---- */
	if (nops >= 3 && (strcmp(mnemonic, "pkhtb") == 0 || strcmp(mnemonic, "pkhbt") == 0)) {
		int rd, rn, rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
		emit32(out->bytes, (mnemonic[2]=='h'?0xE6800010:0xE6800000) | (rd<<16) | (rn<<0) | (rm<<8));
		return 0;
	}

	/* ---- ssat/usat rd, #sat, rn {,shift} ---- */
	if (nops >= 3 && (strcmp(mnemonic, "ssat") == 0 || strcmp(mnemonic, "usat") == 0)) {
		int rd, rn, sat = 0; if (reg_num(ops[0],&rd)<0||reg_num(ops[2],&rn)<0) return -1;
		sscanf(ops[1], "#%i", &sat);
		int is_usat = (mnemonic[0] == 'u');
		uint32_t base = is_usat ? 0xE6E00010 : 0xE6A00010;
		emit32(out->bytes, base | (rd<<16) | (rn<<0) | ((sat-1)<<16));
		return 0;
	}

	/* ---- smlad rd, rn, rm, ra ---- */
	if (nops >= 4 && strcmp(mnemonic, "smlad") == 0) {
		int rd, rn, rm, ra;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0||reg_num(ops[3],&ra)<0) return -1;
		emit32(out->bytes, 0xE7000F10 | (rd<<16) | (ra<<12) | (rn<<0) | (rm<<8));
		return 0;
	}

	/* ---- VFP compare to zero: fcmpzs/fcmpzd sd ---- */
	if (nops >= 1 && (strcmp(mnemonic, "fcmpzs") == 0 || strcmp(mnemonic, "fcmpzd") == 0)) {
		int rd; if (reg_num(ops[0],&rd)<0) return -1;
		int is_double = (mnemonic[5] == 'd');
		uint32_t d=(uint32_t)(rd&0x1F);
		emit32(out->bytes, (is_double?0x0EB50BC0:0x0EB50AC0)|((d&1)<<22)|((d>>1)<<12));
		return 0;
	}

	/* ---- VFP move: fmsr/fmrs (VFP<->core register) ---- */
	if (nops >= 2 && (strcmp(mnemonic, "fmsr") == 0 || strcmp(mnemonic, "fmrs") == 0)) {
		int to_vfp = (mnemonic[1] == 'm');
		int rd, rm;
		if (to_vfp) { /* fmsr: rd = core, rm = VFP */
			if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
		} else { /* fmrs: rd = VFP, rm = core */
			if (reg_num(ops[0],&rm)<0||reg_num(ops[1],&rd)<0) return -1;
		}
		uint32_t s = (uint32_t)((to_vfp?rm:rd)&0x1F);
		uint32_t core_r = (uint32_t)(to_vfp?rd:rm);
		uint32_t base = 0x0E000A10 | ((s&1)<<16) | ((s>>1)<<0) | (core_r<<12);
		emit32(out->bytes, base | (to_vfp?0:(1<<20)));
		return 0;
	}

	/* ---- VMRS/VMSR: move between VFP status register and core register ---- */
	if (nops >= 2 && (strcmp(mnemonic, "vmrs") == 0 || strcmp(mnemonic, "vmsr") == 0)) {
		if (strcmp(mnemonic, "vmrs") == 0 && nops >= 2) {
			/* vmrs Rd, fpscr — APSR_nzcv is r15.
			 * Verified: vmrs APSR_nzcv, fpscr = 0xeef1fa10
			 * = 0xEEF10A10 | (15 << 12). */
			int rd;
			if (strncmp(ops[0], "APSR", 4) == 0)
				rd = 15;
			else if (reg_num(ops[0], &rd) < 0)
				return -1;
			emit32(out->bytes, 0xEEF10A10 | (rd << 12));
			return 0;
		}
		if (strcmp(mnemonic, "vmsr") == 0 && nops >= 2) {
			/* vmsr fpscr, Rm — Verified: vmsr fpscr, r0 = 0xeee10a10 */
			int rm; if (reg_num(ops[1], &rm) < 0) return -1;
			emit32(out->bytes, 0xEEE10A10 | (rm << 12));
			return 0;
		}
	}

	/* ---- VFP vcmpe: compare with NaN check ---- */
	if (nops >= 2 && (strcmp(mnemonic, "vcmpe") == 0 || strcmp(mnemonic, "vcmpes") == 0 || strcmp(mnemonic, "vcmped") == 0)) {
		int rd, rm;
		if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0) return -1;
		int is_double = (mnemonic[5] == 'd');
		uint32_t d=(uint32_t)(rd&0x1F), m=(uint32_t)(rm&0x1F);
		emit32(out->bytes, (is_double?0x0EB40BC0:0x0EB40AC0)|((d&1)<<22)|((d>>1)<<12)|((m&1)<<5)|((m>>1)<<16)|(1<<7));
		return 0;
	}

	/* ---- VFP push/pop for VFP registers: vpush/vpop {d0-dN} or {s0-sN} ---- */
	if (nops >= 1 && (strcmp(mnemonic, "vpush") == 0 || strcmp(mnemonic, "vpop") == 0)) {
		const char *regtext = ops[0];
		int is_pop = (mnemonic[1] == 'p' && mnemonic[2] == 'o');
		/* Expecting {d0-dN} or d0-dN (braces already stripped) */
		if (regtext[0] == 'd') {
			int first, last;
			if (sscanf(regtext, "d%d-d%d", &first, &last) < 2) return -1;
			uint32_t dregs = (uint32_t)(last - first + 1) * 2;
			uint32_t base = is_pop ? 0xECBD0B00 : 0xED2D0B00;
			/* VSTMDB/VLDMIA register list: D=bit4 of the first
			 * register, Vd=first&0xF (dp_dst_field).  The old
			 * single-layout ((first>>1)<<12 | (first&1)<<22) mapped
			 * d8 -> Vd=4, encoding the wrong register.
			 * The register-count field (imm8) is 2*N for double
			 * registers (d0-d7 = 8 regs -> imm8=16). */
			emit32(out->bytes, base | dp_dst_field(first) | dregs);
			return 0;
		}
	}

	/* ---- VFP vldr/vstr (different offset encoding) ----
	 * Verified: vldr d1, [r10] = 0xed9a1b00; vldr s2, [r11, #4] = 0xed9b1a01 */
	if (nops >= 2 && (strcmp(mnemonic, "vldr") == 0 || strcmp(mnemonic, "vstr") == 0)) {
		int is_load = (mnemonic[1] == 'l');
		int is_double = (ops[0][0] == 'd');
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		const char *mem = ops[1];
		if (mem[0] == '[') mem++;
		char rn_str[16]; int i2 = 0;
		while (*mem && *mem != ']' && *mem != ',' && *mem != '#' && i2 < 15) rn_str[i2++] = *mem++;
		rn_str[i2] = '\0';
		int rn; if (reg_num(rn_str, &rn) < 0) return -1;
		int32_t off = 0;
		if (*mem == ',' || *mem == '#') {
			while (*mem && (*mem == ',' || *mem == ' ' || *mem == '#')) mem++;
			sscanf(mem, "%i", &off);
		}
		uint32_t imm8 = (uint32_t)(off >= 0 ? off : -off) >> 2;
		int U = (off >= 0) ? 1 : 0;
		uint32_t base = is_load ? (is_double ? 0xED100B00 : 0xED100A00)
		                        : (is_double ? 0xED000B00 : 0xED000A00);
		base |= (rn << 16) | (is_double ? dp_dst_field(rd) : sp_dst_field(sp_reg(rd)))
		      | ((uint32_t)U << 23) | (imm8 & 0xFF);
		emit32(out->bytes, base);
		return 0;
	}

	/* ---- NEON vmax/vmin/vabd/vaba/vceq/vcge/vcgt ---- */
	if (mnemonic[0] == 'v') {
		static const struct { const char *name; uint32_t base; int two_op; } neon_more[] = {
			{"vmax", 0xF2000D06, 0}, {"vmin", 0xF2000D46, 0},
			{"vabd", 0xF2000D70, 0}, {"vaba", 0xF2000D7C, 0},
			{"vceq", 0xF2000D08, 0}, {"vcge", 0xF3000D08, 0},
			{"vcgt", 0xF3200D08, 0}, {"vabs", 0xF3B00500, 1},
			{"vneg", 0xF3B00580, 1}, {"vqadd", 0xF2000D00, 0},
			{"vqsub", 0xF3000D00, 0}, {"vrhadd", 0xF2000D10, 0},
			{"vpadd", 0xF2000D02, 0}, {"vext", 0xF2E00000, 0},
			{0, 0, 0}
		};
		for (int ni = 0; neon_more[ni].name; ni++) {
			if (strcmp(mnemonic, neon_more[ni].name) != 0) continue;
			int rd, rn = 0, rm = 0;
			if (reg_num(ops[0],&rd)<0) return -1;
			if (neon_more[ni].two_op) {
				if (nops < 2 || reg_num(ops[1],&rm)<0) return -1;
			} else {
				if (nops < 3 || reg_num(ops[1],&rn)<0 || reg_num(ops[2],&rm)<0) return -1;
			}
			uint32_t d=(uint32_t)(rd&0x1F), n=(uint32_t)(rn&0x1F), m=(uint32_t)(rm&0x1F);
			emit32(out->bytes, neon_more[ni].base|((d>>1)<<12)|((d&1)<<22)|((n>>1)<<16)|((n&1)<<7)|((m>>1)<<0)|((m&1)<<5));
			return 0;
		}
	}

	/* ---- SMLAL/SMULL/UMLAL: widening multiplies ---- */
	if (nops >= 4 && (strcmp(mnemonic, "smlal") == 0 || strcmp(mnemonic, "umlal") == 0)) {
		int rdlo, rdhi, rn, rm;
		if (reg_num(ops[0],&rdlo)<0||reg_num(ops[1],&rdhi)<0||reg_num(ops[2],&rn)<0||reg_num(ops[3],&rm)<0) return -1;
		uint32_t base = (mnemonic[0]=='s')?0xE0E00090:0xE0A00090;
		emit32(out->bytes, base|(rdhi<<16)|(rdlo<<12)|(rn<<8)|rm);
		return 0;
	}

	/* ---- QADD/QSUB/QDADD/QDSUB: saturating arithmetic ---- */
	if (nops >= 3) {
		if (strcmp(mnemonic, "qadd") == 0) {
			int rd, rm, rn; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0||reg_num(ops[2],&rn)<0) return -1;
			emit32(out->bytes, 0xE1000050|(rd<<16)|(rm<<0)|(rn<<8)); return 0;
		}
		if (strcmp(mnemonic, "qsub") == 0) {
			int rd, rm, rn; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0||reg_num(ops[2],&rn)<0) return -1;
			emit32(out->bytes, 0xE1200050|(rd<<16)|(rm<<0)|(rn<<8)); return 0;
		}
		if (strcmp(mnemonic, "qdadd") == 0) {
			int rd, rm, rn; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0||reg_num(ops[2],&rn)<0) return -1;
			emit32(out->bytes, 0xE1400050|(rd<<16)|(rm<<0)|(rn<<8)); return 0;
		}
		if (strcmp(mnemonic, "qdsub") == 0) {
			int rd, rm, rn; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rm)<0||reg_num(ops[2],&rn)<0) return -1;
			emit32(out->bytes, 0xE1600050|(rd<<16)|(rm<<0)|(rn<<8)); return 0;
		}
	}

	/* ---- LDRD/STRD: doubleword load/store ---- */
	if (nops >= 2 && (strcmp(mnemonic, "ldrd") == 0 || strcmp(mnemonic, "strd") == 0)) {
		int rd, rn; if (reg_num(ops[0],&rd)<0) return -1;
		const char *mem = ops[1];
		if (mem[0]=='[') mem++;
		char rn_str[16]; int i2=0;
		while (*mem && *mem != ']' && *mem != ',' && *mem != '#' && i2 < 15) rn_str[i2++] = *mem++;
		rn_str[i2]='\0'; if (reg_num(rn_str,&rn)<0) return -1;
		uint32_t off=0;
		if (*mem==','||*mem=='#') { while(*mem&&(*mem==','||*mem==' '||*mem=='#'))mem++; sscanf(mem,"%u",&off); }
		int is_load = (mnemonic[0]=='l');
		emit32(out->bytes, (is_load?0xE1D000D0:0xE1C000D0)|(rn<<16)|(rd<<12)|((off&0xF0)<<4)|(off&0xF));
		return 0;
	}

	/* ---- RFE/SRS: return from exception / store return state ---- */
	if (nops >= 1 && strcmp(mnemonic, "rfe") == 0) {
		const char *mem = ops[0]; if (mem[0]=='[') mem++;
		int rn; sscanf(mem, "r%i", &rn);
		emit32(out->bytes, 0xF8100A00 | (rn<<16)); return 0;
	}

	/* ---- CPS: change processor state ---- */
	if (nops >= 1 && strcmp(mnemonic, "cps") == 0) {
		uint32_t mask=0;
		for (int ci=0; ci<nops; ci++) {
			if (strcmp(ops[ci],"ie")==0) mask|=0x10;
			if (strcmp(ops[ci],"id")==0) mask|=0x20;
			if (strcmp(ops[ci],"ae")==0) mask|=0x40;
			if (strcmp(ops[ci],"ad")==0) mask|=0x80;
		}
		emit32(out->bytes, 0xF0200000 | mask); return 0;
	}

	/* ---- MRS/MSR: move to/from status register ---- */
	if (nops >= 2 && strcmp(mnemonic, "mrs") == 0) {
		int rd; if (reg_num(ops[0],&rd)<0) return -1;
		emit32(out->bytes, 0xE10F0000 | (rd<<12)); return 0;
	}
	if (nops >= 2 && strcmp(mnemonic, "msr") == 0) {
		int rm; if (reg_num(ops[nops-1],&rm)<0) return -1;
		emit32(out->bytes, 0xE129F000 | rm); return 0;
	}

	/* ---- SXTAB/SXTAB16/UXTAB: extend and add ---- */
	if (nops >= 3) {
		if (strcmp(mnemonic,"sxtab")==0) {
			int rd,rn,rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
			emit32(out->bytes, 0xE6A00070|(rn<<16)|(rd<<12)|rm); return 0;
		}
		if (strcmp(mnemonic,"sxtab16")==0) {
			int rd,rn,rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
			emit32(out->bytes, 0xE6800070|(rn<<16)|(rd<<12)|rm); return 0;
		}
		if (strcmp(mnemonic,"uxtab")==0) {
			int rd,rn,rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
			emit32(out->bytes, 0xE6E00070|(rn<<16)|(rd<<12)|rm); return 0;
		}
	}

	/* ---- SMUAD/SMUSD: signed multiply dual ---- */
	if (nops >= 3 && (strcmp(mnemonic,"smuad")==0||strcmp(mnemonic,"smusd")==0)) {
		int rd,rn,rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
		emit32(out->bytes, (mnemonic[3]=='a'?0xE7000F10:0xE7400F10)|(rd<<16)|(rn<<0)|(rm<<8));
		return 0;
	}

	/* ---- SMMUL/SMMLA: signed multiply most significant ---- */
	if (nops >= 3 && strcmp(mnemonic,"smmul")==0) {
		int rd,rn,rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
		emit32(out->bytes, 0xE7500F10|(rd<<16)|(rn<<0)|(rm<<8)); return 0;
	}
	if (nops >= 4 && strcmp(mnemonic,"smmla")==0) {
		int rd,rn,rm,ra; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0||reg_num(ops[3],&ra)<0) return -1;
		emit32(out->bytes, 0xE7500010|(rd<<16)|(rn<<0)|(rm<<8)|(ra<<12)); return 0;
	}

	/* ---- NEON vmul/vmla/vmls: floating-point SIMD ---- */
	if (nops >= 3 && (strcmp(mnemonic,"vmul")==0||strcmp(mnemonic,"vmla")==0)) {
		int rd,rn,rm; if (reg_num(ops[0],&rd)<0||reg_num(ops[1],&rn)<0||reg_num(ops[2],&rm)<0) return -1;
		int is_mul = (mnemonic[1]=='m'&&mnemonic[2]=='u');
		uint32_t d=(uint32_t)(rd&0x1F),n=(uint32_t)(rn&0x1F),m=(uint32_t)(rm&0x1F);
		uint32_t base=is_mul?0xF3000D50:0xF2000D40;
		emit32(out->bytes, base|((d>>1)<<12)|((d&1)<<22)|((n>>1)<<16)|((n&1)<<7)|((m>>1)<<0)|((m&1)<<5));
		return 0;
	}

	/* ---- PLI (preload instruction) ---- */
	if (nops >= 1 && strcmp(mnemonic,"pli")==0) {
		int rn=0; const char *mem=ops[0]; if(mem[0]=='[')mem++;
		sscanf(mem,"r%i",&rn);
		emit32(out->bytes, 0xF450F000|(rn<<16)); return 0;
	}

	/* ---- SETEND: set endianness ---- */
	if (nops >= 1 && strcmp(mnemonic,"setend")==0) {
		int be = (strcmp(ops[0],"be")==0);
		emit32(out->bytes, be?0xF1010000:0xF1000000); return 0;
	}

	return -1; /* unsupported */
}
