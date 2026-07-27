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
	if (s[0] == 's' && s[1]>='0' && s[1]<='9') { *r=16+(s[1]-'0'); return 1; }
	if (s[0] == 'd' && s[1]>='0' && s[1]<='9') { int n = s[1]-'0'; if(s[2]>='0'&&s[2]<='9') n=n*10+s[2]-'0'; if(n>31)return -1; *r=n; return 1; }
	return -1;
}

/* ---- Main encode function ---- */
int armv7_encode_insn(const struct mt_target *target,
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

	/* Parse operands: split by ',' and strip spaces */
	char ops[4][64]; int nops = 0;
	const char *p = operand_text ? operand_text : "";
	while (*p && nops < 4) {
		while (*p == ' ' || *p == '\t') p++;
		int i = 0;
		while (*p && *p != ',') {
			if (i < 63) ops[nops][i++] = *p;
			p++;
		}
		ops[nops][i] = '\0'; nops++;
		if (*p == ',') p++;
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

	/* ---- push/pop (pseudo-instructions) ---- */
	if (strcmp(mnemonic, "push") == 0 && nops >= 1) {
		uint16_t reglist = 0;
		for (int i = 0; i < nops; i++) {
			int r; if (reg_num(ops[i], &r) < 0) return -1;
			reglist |= 1 << r;
		}
		emit32(out->bytes, 0xE92D0000 | reglist);
		return 0;
	}
	if (strcmp(mnemonic, "pop") == 0 && nops >= 1) {
		uint16_t reglist = 0;
		for (int i = 0; i < nops; i++) {
			int r; if (reg_num(ops[i], &r) < 0) return -1;
			reglist |= 1 << r;
		}
		emit32(out->bytes, 0xE8BD0000 | reglist);
		return 0;
	}

	/* ---- Data processing: add, sub, mov, cmp, and/orr/eor/bic ---- */
	static const struct { const char *name; uint32_t opcode; } dp_ops[] = {
		{"and", 0}, {"eor", 1}, {"sub", 2}, {"rsb", 3},
		{"add", 4}, {"adc", 5}, {"sbc", 6}, {"rsc", 7},
		{"tst", 8}, {"teq", 9}, {"cmp", 10}, {"cmn", 11},
		{"orr", 12}, {"mov", 13}, {"bic", 14}, {"mvn", 15},
		{0, 0}
	};

	/* Data processing with 3 operands: add/sub/and/orr/eor r0, r1, r2
	 * or with 2 operands: mov/cmp r0, r1 or mov/cmp r0, #imm */
	if (nops >= 2) {
		int rd, rn;
		for (int d = 0; dp_ops[d].name; d++) {
			if (strcmp(mnemonic, dp_ops[d].name) != 0) continue;

			if (nops == 2) {
				/* Two-address: {mov,cmp} rd, rm */
				if (reg_num(ops[1], &rd) < 0) {
					/* Try immediate: #N */
					int imm = 0;
					if (ops[1][0] == '#') {
						sscanf(ops[1]+1, "%i", &imm);
						/* Encode rotated immediate */
						uint32_t val = (uint32_t)imm;
						if (val < 256) {
							emit32(out->bytes, 0xE3A00000 | (dp_ops[d].opcode<<21) | 0x2000000);
							// need to refine this with actual rd, rn
						}
					}
				}
				/* Simple case: register operand */
				if (reg_num(ops[0], &rd) < 0) return -1;
				if (reg_num(ops[1], &rn) < 0) return -1;
				if (dp_ops[d].opcode == 13) /* mov */
					emit32(out->bytes, 0xE1A00000 | (rd<<12) | rn);
				else if (dp_ops[d].opcode == 10) /* cmp */
					emit32(out->bytes, 0xE1500000 | (rd<<16) | rn);
				else /* not supported for 2-addr */
					return -1;
				return 0;
			}
			return -1;
		}
	}

	/* ---- Conditional branching (b<cond> label) ---- */
	if (mnemonic[0] == 'b' && mnemonic[1] != 'l' && mnemonic[1] != 'x') {
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

	/* ---- movw r0, #imm ---- */
	if (strcmp(mnemonic, "movw") == 0 && nops >= 2) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		int imm = 0;
		if (ops[1][0] == '#') sscanf(ops[1]+1, "%i", &imm);
		else return -1;
		uint32_t imm16 = (uint32_t)imm & 0xFFFF;
		emit32(out->bytes, 0xE3000000 | (rd<<12) | ((imm16>>12)&0xF)<<16 | (imm16 & 0xFFF));
		return 0;
	}

	/* ---- movt r0, #imm ---- */
	if (strcmp(mnemonic, "movt") == 0 && nops >= 2) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		int imm = 0;
		if (ops[1][0] == '#') sscanf(ops[1]+1, "%i", &imm);
		else return -1;
		uint32_t imm16 = ((uint32_t)imm >> 16) & 0xFFFF;
		emit32(out->bytes, 0xE3400000 | (rd<<12) | ((imm16>>12)&0xF)<<16 | (imm16 & 0xFFF));
		return 0;
	}

	/* ---- Simple memory: ldr r0, [r1] or str r0, [r1] ---- */
	if ((strcmp(mnemonic, "ldr") == 0 || strcmp(mnemonic, "str") == 0) && nops >= 2) {
		int rd, rn;
		if (reg_num(ops[0], &rd) < 0) return -1;
		int store = mnemonic[0] == 's';
		const char *mem = ops[1];
		if (mem[0] == '[') mem++;
		char rn_str[16]; int i = 0;
		while (*mem && *mem != ']' && *mem != ',' && *mem != '#' && i < 15)
			rn_str[i++] = *mem++;
		rn_str[i] = '\0';
		if (reg_num(rn_str, &rn) < 0) return -1;
		uint32_t off = 0;
		if (*mem == ',' || *mem == '#') {
			while (*mem && (*mem == ',' || *mem == ' ' || *mem == '#')) mem++;
			sscanf(mem, "%u", &off);
		}
		emit32(out->bytes, 0xE5100000 | (store?0:1<<20) | (rn<<16) | (rd<<12) | off);
		return 0;
	}

	/* ---- Conditional move: mov<cond> r0, #N ---- */
	if (strncmp(mnemonic, "mov", 3) == 0 && nops >= 2) {
		static const struct { const char *s; int c; } conds[] = {
			{"eq",0},{"ne",1},{"cs",2},{"cc",3},{"mi",4},{"pl",5},
			{"vs",6},{"vc",7},{"hi",8},{"ls",9},{"ge",10},{"lt",11},
			{"gt",12},{"le",13},{"al",14},{0,0}
		};
		const char *cs = mnemonic + 3;
		int cond = 14; /* AL */
		for (int i = 0; conds[i].s; i++) {
			if (strcmp(cs, conds[i].s) == 0) { cond = conds[i].c; break; }
		}
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		int imm = 0;
		if (ops[1][0] == '#') sscanf(ops[1]+1, "%i", &imm);
		else return -1;
		emit32(out->bytes, (cond<<28) | 0x3A00000 | (rd<<12) | (imm & 0xFF));
		return 0;
	}

	return -1; /* unsupported */
}
