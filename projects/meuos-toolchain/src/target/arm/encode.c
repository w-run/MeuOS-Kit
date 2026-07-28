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

/* ---- Data processing: add/sub/and/orr/eor/bic/mov/cmp etc. ---- */
	static const struct { const char *name; uint32_t opcode; } dp_ops[] = {
		{"and", 0}, {"eor", 1}, {"sub", 2}, {"rsb", 3},
		{"add", 4}, {"adc", 5}, {"sbc", 6}, {"rsc", 7},
		{"tst", 8}, {"teq", 9}, {"cmp", 10}, {"cmn", 11},
		{"orr", 12}, {"mov", 13}, {"bic", 14}, {"mvn", 15},
		{0, 0}
	};

	if (nops >= 2) {
		for (int d = 0; dp_ops[d].name; d++) {
			if (strcmp(mnemonic, dp_ops[d].name) != 0) continue;
			uint32_t opc = dp_ops[d].opcode;

			if (nops == 2 && (opc == 13 || opc == 15)) {
				/* mov/mvn rd, rm  or  mov/mvn rd, #imm */
				int rd; if (reg_num(ops[0], &rd) < 0) return -1;
				if (ops[1][0] == '#') {
					int imm = 0; sscanf(ops[1]+1, "%i", &imm);
					uint32_t imm8 = 0, rot = 0;
					uint32_t v = (uint32_t)imm;
					if (v < 256) { imm8 = v; rot = 0; }
					else {
						for (int r = 1; r < 16; r++) {
							uint32_t rv = (v >> (r*2)) | (v << (32 - r*2));
							if (rv < 256) { imm8 = rv; rot = r; break; }
						}
					}
					emit32(out->bytes, 0xE3A00000 | (opc==15?0x600000:0) | (rd<<12) | (rot<<8) | imm8);
					return 0;
				}
				int rm; if (reg_num(ops[1], &rm) < 0) return -1;
				emit32(out->bytes, 0xE1A00000 | (opc==15?0x600000:0) | (rd<<12) | rm);
				return 0;
			}

			if (nops == 2 && (opc == 10 || opc == 11 || opc == 8 || opc == 9)) {
				/* cmp/cmn/tst/teq rn, rm  or  cmp rn, #imm */
				int rn; if (reg_num(ops[0], &rn) < 0) return -1;
				if (ops[1][0] == '#') {
					int imm = 0; sscanf(ops[1]+1, "%i", &imm);
					uint32_t imm8 = 0, rot = 0;
					uint32_t v = (uint32_t)imm;
					if (v < 256) { imm8 = v; rot = 0; }
					else { for (int r = 1; r < 16; r++) {
						uint32_t rv = (v >> (r*2)) | (v << (32 - r*2));
						if (rv < 256) { imm8 = rv; rot = r; break; }
					}}
					emit32(out->bytes, 0xE3500000 | (opc<<21) | (rn<<16) | (rot<<8) | imm8);
					return 0;
				}
				int rm; if (reg_num(ops[1], &rm) < 0) return -1;
				emit32(out->bytes, 0xE1500000 | (opc<<21) | (rn<<16) | rm);
				return 0;
			}

			if (nops >= 3) {
				/* Three-address: add/sub/and/orr/eor/bic rd, rn, rm
				 * or rd, rn, #imm  or rd, rn, rm, LSL #N */
				int rd; if (reg_num(ops[0], &rd) < 0) return -1;
				int rn; if (reg_num(ops[1], &rn) < 0) return -1;

				if (ops[2][0] == '#') {
					/* Immediate: rd, rn, #imm */
					int imm = 0; sscanf(ops[2]+1, "%i", &imm);
					uint32_t imm8 = 0, rot = 0;
					uint32_t v = (uint32_t)imm;
					if (v < 256) { imm8 = v; rot = 0; }
					else { for (int r = 1; r < 16; r++) {
						uint32_t rv = (v >> (r*2)) | (v << (32 - r*2));
						if (rv < 256) { imm8 = rv; rot = r; break; }
					}}
					emit32(out->bytes, 0xE2000000 | (opc<<21) | (1<<25) | (rn<<16) | (rd<<12) | (rot<<8) | imm8);
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
					emit32(out->bytes, 0xE0000000 | (opc<<21) | (rn<<16) | (rd<<12)
					       | (shift_amt<<7) | (shift_type<<5) | rm);
					return 0;
				}

				emit32(out->bytes, 0xE0000000 | (opc<<21) | (rn<<16) | (rd<<12) | rm);
				return 0;
			}

			if (nops == 2) {
				/* Two-address: {add,sub,and,...} rd, rm */
				int rd; if (reg_num(ops[0], &rd) < 0) return -1;
				int rm; if (reg_num(ops[1], &rm) < 0) return -1;
				emit32(out->bytes, 0xE0000000 | (opc<<21) | (rd<<16) | (rd<<12) | rm);
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
		const char *shift_mn[] = {"asr", "lsr", "lsl", "ror", 0};
		int shift_type = -1;
		for (int i = 0; shift_mn[i]; i++)
			if (strcmp(mnemonic, shift_mn[i]) == 0) { shift_type = i; break; }
		if (shift_type >= 0 && nops >= 3) {
			int rd; if (reg_num(ops[0], &rd) < 0) return -1;
			int rm; if (reg_num(ops[1], &rm) < 0) return -1;
			int amt = 0; const char *sv = ops[2];
			while (*sv == '#' || *sv == ' ') sv++;
			sscanf(sv, "%i", &amt);
			emit32(out->bytes, 0xE1A00000 | (rd<<12) | (amt<<7) | (shift_type<<5) | rm);
			return 0;
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

	/* ---- movw r0, #imm (or #:lower16:sym) ---- */
	if (strcmp(mnemonic, "movw") == 0 && nops >= 2) {
		int rd; if (reg_num(ops[0], &rd) < 0) return -1;
		const char *op = ops[1];
		if (op[0] == '#' && op[1] == ':') {
			/* Symbol modifier: #:lower16:sym → R_ARM_MOVW_ABS_NC */
			const char *sym = op + 2;
			while (*sym == ':' || *sym == 'l' || *sym == 'o' || *sym == 'w'
			       || *sym == 'e' || *sym == 'r' || *sym == '1' || *sym == '6')
				sym++;
			if (*sym == ':') sym++;
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
			/* Symbol modifier: #:upper16:sym → R_ARM_MOVT_ABS */
			const char *sym = op + 2;
			while (*sym == ':' || *sym == 'u' || *sym == 'p' || *sym == 'e'
			       || *sym == 'r' || *sym == '1' || *sym == '6')
				sym++;
			if (*sym == ':') sym++;
			out->fixed = 0;
			out->reloc_type = 44; /* R_ARM_MOVT_ABS */
			set_fixup(out, 0, 4, 44, sym, 0);
			emit32(out->bytes, 0xE3400000 | (rd<<12));
			return 0;
		}
		if (op[0] == '#') {
			int imm = 0; sscanf(op+1, "%i", &imm);
			uint32_t imm16 = ((uint32_t)imm >> 16) & 0xFFFF;
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
			uint32_t off = 0;
			if (*mem == ',' || *mem == '#') {
				while (*mem && (*mem == ',' || *mem == ' ' || *mem == '#')) mem++;
				sscanf(mem, "%u", &off);
			}
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
				if (is_load)
					emit32(out->bytes, 0xE5D00000 | (rn<<16) | (rd<<12) | off);
				else
					emit32(out->bytes, 0xE5C00000 | (rn<<16) | (rd<<12) | off);
			} else {
				emit32(out->bytes, 0xE5100000 | (is_load?1<<20:0) | (rn<<16) | (rd<<12) | off);
			}
			return 0;
		}
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
