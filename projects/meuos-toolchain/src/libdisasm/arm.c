/* arm.c - ARM 32-bit (ARM mode, not Thumb) disassembler.
 *
 * Implements mt_disasm_arm_one(): decodes a single 32-bit ARM instruction
 * and produces GNU/objdump-compatible output.
 *
 * Encoding overview (all 32-bit fixed-length, little-endian):
 *   - Bits[31:28] = condition code (0xE = ALways = omitted)
 *   - Data processing:  cond|00|I|opcode|S|Rn|Rd|operand2
 *   - Load/Store:      cond|01|I|P|U|B|W|L|Rn|Rd|offset12
 *   - Branch:          cond|101|L|offset24
 *   - LDM/STM:         cond|100|P|U|S|W|L|Rn|reglist
 *   - SWI:             cond|1111|imm24
 *   - BX/BLX:          cond|00010010|111111111111|0001|Rm
 *   - CLZ:             cond|00010110|1111|Rd|11110001|Rm
 *   - MOVW/MOVT:       cond|0011|0|imm4|Rd|imm12
 *
 * Coverage: data processing, load/store, branch, LDM/STM, PUSH/POP,
 * SWI, NOP, CLZ, MOVW, MOVT, BX, BLX.
 */
#include "mt/disasm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* ARM register / condition tables                                   */
/* ------------------------------------------------------------------ */

static const char *const arm_cc[16] = {
	"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
	"hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
};

static const char *const arm_reg_name[16] = {
	"r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
	"r8",  "r9",  "r10", "r11", "r12", "sp",  "lr",  "pc"
};

static const char *const arm_shift_name[4] = {"lsl", "lsr", "asr", "ror"};

static const char *const arm_dp_op[16] = {
	"and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
	"tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"
};

/* ------------------------------------------------------------------ */
/* Helper: set mnemonic with optional condition suffix.              */
/* ------------------------------------------------------------------ */
static void
arm_set_mnem(char *buf, size_t sz, const char *base, unsigned cond,
             int s_bit)
{
	const char *suf = (cond == 0xE) ? "" : arm_cc[cond];
	snprintf(buf, sz, "%s%s%s", base, suf, s_bit ? "s" : "");
}

/* ------------------------------------------------------------------ */
/* Fetch a 4-byte little-endian ARM instruction.                     */
/* ------------------------------------------------------------------ */
static int
arm_fetch32(const unsigned char *bytes, size_t size, size_t pos,
            uint32_t *insn)
{
	if (pos + 4 > size)
		return -1;
	*insn = (uint32_t)bytes[pos]
	      | ((uint32_t)bytes[pos + 1] << 8)
	      | ((uint32_t)bytes[pos + 2] << 16)
	      | ((uint32_t)bytes[pos + 3] << 24);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Format a shifted-register operand2 (I=0) into buf.               */
/* ------------------------------------------------------------------ */
static void
arm_fmt_shift_reg(char *buf, size_t sz, uint32_t insn)
{
	unsigned rm = insn & 0xF;
	unsigned type = (insn >> 5) & 3;
	unsigned imm5 = (insn >> 7) & 0x1F;
	int is_reg_shift = (insn >> 4) & 1;

	if (!is_reg_shift) {
		if (imm5 == 0 && type == 0)
			snprintf(buf, sz, "%s", arm_reg_name[rm]);
		else if (imm5 == 0 && type == 3)
			snprintf(buf, sz, "%s, rrx", arm_reg_name[rm]);
		else
			snprintf(buf, sz, "%s, %s #%u",
			         arm_reg_name[rm], arm_shift_name[type], imm5);
	} else {
		unsigned rs = (insn >> 8) & 0xF;
		snprintf(buf, sz, "%s, %s %s", arm_reg_name[rm],
		         arm_shift_name[type], arm_reg_name[rs]);
	}
}

/* ------------------------------------------------------------------ */
/* Format an immediate operand2 (I=1) into buf as #0xNN.            */
/* ------------------------------------------------------------------ */
static void
arm_fmt_imm_op2(char *buf, size_t sz, uint32_t insn)
{
	unsigned imm8 = insn & 0xFF;
	unsigned rot = (insn >> 8) & 0xF;
	uint32_t val;

	if (rot == 0)
		val = imm8;
	else
		val = (imm8 >> (rot * 2)) | (imm8 << (32 - rot * 2));
	snprintf(buf, sz, "#0x%x", (unsigned)val);
}

/* ------------------------------------------------------------------ */
/* Format operand2 (shifted register or immediate) into buf.        */
/* ------------------------------------------------------------------ */
static void
arm_fmt_op2(char *buf, size_t sz, uint32_t insn)
{
	if (insn & (1u << 25))
		arm_fmt_imm_op2(buf, sz, insn);
	else
		arm_fmt_shift_reg(buf, sz, insn);
}

/* ------------------------------------------------------------------ */
/* Format a register list for LDM/STM/PUSH/POP.                      */
/* ------------------------------------------------------------------ */
static void
arm_fmt_reglist(char *buf, size_t sz, uint32_t reglist)
{
	int first = 1;
	unsigned i;
	size_t pos = 0;
	int range_start = -1, prev = -1;

	buf[0] = '{';
	pos = 1;
	for (i = 0; i < 16; i++) {
		if (reglist & (1u << i)) {
			if (range_start < 0)
				range_start = (int)i;
			prev = (int)i;
		} else if (range_start >= 0) {
			if (!first && pos < sz - 1)
				buf[pos++] = ',';
			first = 0;
			if (prev == range_start)
				pos += (size_t)snprintf(buf + pos,
				       sz - pos, "%s",
				       arm_reg_name[range_start]);
			else
				pos += (size_t)snprintf(buf + pos,
				       sz - pos, "%s-%s",
				       arm_reg_name[range_start],
				       arm_reg_name[prev]);
			range_start = -1;
		}
	}
	if (range_start >= 0) {
		if (!first && pos < sz - 1)
			buf[pos++] = ',';
		if (prev == range_start)
			pos += (size_t)snprintf(buf + pos, sz - pos, "%s",
			                       arm_reg_name[range_start]);
		else
			pos += (size_t)snprintf(buf + pos, sz - pos, "%s-%s",
			                       arm_reg_name[range_start],
			                       arm_reg_name[prev]);
	}
	if (pos < sz)
		buf[pos] = '\0';
	if (pos + 1 < sz) {
		buf[pos++] = '}';
		buf[pos] = '\0';
	}
	if (pos >= sz)
		buf[sz - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* Format a memory address expression into buf.                     */
/* ------------------------------------------------------------------ */
static void
arm_fmt_mem(char *buf, size_t sz, uint32_t insn, uint64_t base_addr)
{
	unsigned rn = (insn >> 16) & 0xF;
	unsigned l_bit = (insn >> 20) & 1;
	unsigned p_bit = (insn >> 24) & 1;
	unsigned w_bit = (insn >> 21) & 1;
	unsigned u_bit = (insn >> 23) & 1;
	unsigned i_bit = (insn >> 25) & 1;
	int is_load = (int)l_bit;
	int pre_index = (int)p_bit;
	int writeback = (int)w_bit;
	int add_offset = (int)u_bit;

	(void)base_addr;

	/* LDR literal (PC-relative). */
	if (rn == 15 && pre_index && !writeback && is_load && i_bit == 0) {
		unsigned imm12 = insn & 0xFFF;
		if (add_offset)
			snprintf(buf, sz, "[pc, #0x%x]", imm12);
		else
			snprintf(buf, sz, "[pc, #-0x%x]", imm12);
		return;
	}

	if (i_bit == 0) {
		unsigned imm12 = insn & 0xFFF;
		if (pre_index) {
			if (imm12 == 0)
				snprintf(buf, sz, "[%s]", arm_reg_name[rn]);
			else if (add_offset)
				snprintf(buf, sz, "[%s, #0x%x]",
				         arm_reg_name[rn], imm12);
			else
				snprintf(buf, sz, "[%s, #-0x%x]",
				         arm_reg_name[rn], imm12);
			if (writeback) {
				size_t olen = strlen(buf);
				if (olen + 2 < sz) {
					buf[olen] = '!';
					buf[olen + 1] = '\0';
				}
			}
		} else {
			if (add_offset)
				snprintf(buf, sz, "[%s], #0x%x",
				         arm_reg_name[rn], imm12);
			else
				snprintf(buf, sz, "[%s], #-0x%x",
				         arm_reg_name[rn], imm12);
		}
	} else {
		unsigned rm = insn & 0xF;
		if (pre_index) {
			snprintf(buf, sz, "[%s, %s]",
			         arm_reg_name[rn], arm_reg_name[rm]);
			if (writeback) {
				size_t olen = strlen(buf);
				if (olen + 2 < sz) {
					buf[olen] = '!';
					buf[olen + 1] = '\0';
				}
			}
		} else {
			snprintf(buf, sz, "[%s], %s",
			         arm_reg_name[rn], arm_reg_name[rm]);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Decode a branch: B or BL (bit[27:25] = 101).                     */
/* ------------------------------------------------------------------ */
static int
arm_decode_branch(uint32_t insn, unsigned cond, uint64_t addr,
                  char *mnem, size_t mnem_sz,
                  char *ops, size_t ops_sz)
{
	unsigned l_bit = (insn >> 24) & 1;
	uint32_t offset;
	uint64_t target;

	offset = insn & 0x00FFFFFF;
	if (offset & 0x00800000)
		offset |= 0xFF000000;
	target = addr + 8 + (uint64_t)(int64_t)(int32_t)(offset << 2);

	arm_set_mnem(mnem, mnem_sz, l_bit ? "bl" : "b", cond, 0);
	snprintf(ops, ops_sz, "0x%llx", (unsigned long long)target);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode LDM/STM (bit[27:25] = 100).                                */
/* ------------------------------------------------------------------ */
static int
arm_decode_ldm_stm(uint32_t insn, unsigned cond, char *mnem,
                   size_t mnem_sz, char *ops, size_t ops_sz)
{
	unsigned rn = (insn >> 16) & 0xF;
	unsigned reglist = insn & 0xFFFF;
	unsigned p = (insn >> 24) & 1;
	unsigned u = (insn >> 23) & 1;
	unsigned w = (insn >> 21) & 1;
	unsigned l = (insn >> 20) & 1;
	char rl_buf[80];
	int rn_in_list;

	/* Detect PUSH = STMDB SP! and POP = LDMIA SP!. */
	if (p == 0 && u == 0 && w == 1 && l == 0 && rn == 13) {
		arm_set_mnem(mnem, mnem_sz, "push", cond, 0);
		arm_fmt_reglist(rl_buf, sizeof rl_buf, reglist);
		snprintf(ops, ops_sz, "%s", rl_buf);
		return 0;
	}
	if (p == 0 && u == 1 && w == 1 && l == 1 && rn == 13) {
		arm_set_mnem(mnem, mnem_sz, "pop", cond, 0);
		arm_fmt_reglist(rl_buf, sizeof rl_buf, reglist);
		snprintf(ops, ops_sz, "%s", rl_buf);
		return 0;
	}

	rn_in_list = (int)((reglist >> rn) & 1);

	if (u && p)
		arm_set_mnem(mnem, mnem_sz, l ? "ldmib" : "stmib", cond, 0);
	else if (u && !p)
		arm_set_mnem(mnem, mnem_sz, l ? "ldmia" : "stmia", cond, 0);
	else if (!u && p)
		arm_set_mnem(mnem, mnem_sz, l ? "ldmdb" : "stmdb", cond, 0);
	else
		arm_set_mnem(mnem, mnem_sz, l ? "ldmda" : "stmda", cond, 0);

	if (w && !rn_in_list)
		snprintf(ops, ops_sz, "%s!, ", arm_reg_name[rn]);
	else
		snprintf(ops, ops_sz, "%s, ", arm_reg_name[rn]);
	arm_fmt_reglist(ops + strlen(ops), ops_sz - strlen(ops), reglist);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode data-processing (bit[27:26] = 00).                         */
/* ------------------------------------------------------------------ */
static int
arm_decode_dp(uint32_t insn, unsigned cond, char *mnem,
              size_t mnem_sz, char *ops, size_t ops_sz)
{
	unsigned opcode = (insn >> 21) & 0xF;
	unsigned s_bit = (insn >> 20) & 1;
	unsigned rn = (insn >> 16) & 0xF;
	unsigned rd = (insn >> 12) & 0xF;
	char op2_buf[48];
	int is_compare, is_move;

	arm_fmt_op2(op2_buf, sizeof op2_buf, insn);

	is_compare = (opcode >= 8 && opcode <= 11);
	is_move = (opcode == 13 || opcode == 15);

	if (is_compare) {
		arm_set_mnem(mnem, mnem_sz, arm_dp_op[opcode], cond, 1);
		snprintf(ops, ops_sz, "%s, %s", arm_reg_name[rn], op2_buf);
	} else if (is_move) {
		arm_set_mnem(mnem, mnem_sz, arm_dp_op[opcode], cond, s_bit);
		snprintf(ops, ops_sz, "%s, %s", arm_reg_name[rd], op2_buf);
	} else {
		arm_set_mnem(mnem, mnem_sz, arm_dp_op[opcode], cond, s_bit);
		snprintf(ops, ops_sz, "%s, %s, %s",
		         arm_reg_name[rd], arm_reg_name[rn], op2_buf);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode MOVW / MOVT.                                                */
/* ------------------------------------------------------------------ */
static int
arm_decode_movw_movt(uint32_t insn, unsigned cond, char *mnem,
                     size_t mnem_sz, char *ops, size_t ops_sz)
{
	unsigned rd = (insn >> 12) & 0xF;
	unsigned imm4 = (insn >> 16) & 0xF;
	unsigned imm12 = insn & 0xFFF;
	unsigned imm16 = (imm4 << 12) | imm12;
	unsigned bit22 = (insn >> 22) & 1;

	arm_set_mnem(mnem, mnem_sz, bit22 ? "movt" : "movw", cond, 0);
	snprintf(ops, ops_sz, "%s, #0x%x", arm_reg_name[rd], imm16);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode SWI (bit[27:24] = 1111).                                   */
/* ------------------------------------------------------------------ */
static int
arm_decode_swi(uint32_t insn, unsigned cond, char *mnem,
               size_t mnem_sz, char *ops, size_t ops_sz)
{
	unsigned imm24 = insn & 0xFFFFFF;

	arm_set_mnem(mnem, mnem_sz, "svc", cond, 0);
	if (imm24 != 0)
		snprintf(ops, ops_sz, "#0x%x", imm24);
	else
		ops[0] = '\0';
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode load/store word/byte (bit[27:26] = 01).                   */
/* ------------------------------------------------------------------ */
static int
arm_decode_ldr_str(uint32_t insn, unsigned cond, uint64_t addr,
                   char *mnem, size_t mnem_sz,
                   char *ops, size_t ops_sz)
{
	unsigned rd = (insn >> 12) & 0xF;
	unsigned b_bit = (insn >> 22) & 1;
	unsigned l_bit = (insn >> 20) & 1;
	const char *base_mnem;
	char mem_buf[64];

	if (b_bit)
		base_mnem = l_bit ? "ldrb" : "strb";
	else
		base_mnem = l_bit ? "ldr" : "str";

	arm_set_mnem(mnem, mnem_sz, base_mnem, cond, 0);
	arm_fmt_mem(mem_buf, sizeof mem_buf, insn, addr);
	snprintf(ops, ops_sz, "%s, %s", arm_reg_name[rd], mem_buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode load/store halfword / signed byte/halfword.                */
/* ------------------------------------------------------------------ */
static int
arm_decode_ldrh_strh(uint32_t insn, unsigned cond, char *mnem,
                     size_t mnem_sz, char *ops, size_t ops_sz)
{
	unsigned rn = (insn >> 16) & 0xF;
	unsigned rd = (insn >> 12) & 0xF;
	unsigned l_bit = (insn >> 20) & 1;
	unsigned p_bit = (insn >> 24) & 1;
	unsigned w_bit = (insn >> 21) & 1;
	unsigned u_bit = (insn >> 23) & 1;
	unsigned s_bit = (insn >> 6) & 1;
	unsigned h_bit = (insn >> 5) & 1;
	unsigned off_hi = (insn >> 8) & 0xF;
	unsigned off_lo = insn & 0xF;
	unsigned offset = (off_hi << 4) | off_lo;
	const char *mnem_str;
	char mem_buf[64];

	if (s_bit == 0 && h_bit == 1)
		mnem_str = l_bit ? "ldrh" : "strh";
	else if (s_bit == 1 && h_bit == 0)
		mnem_str = l_bit ? "ldrsb" : "(bad)";
	else if (s_bit == 1 && h_bit == 1)
		mnem_str = l_bit ? "ldrsh" : "(bad)";
	else
		mnem_str = "(bad)";

	arm_set_mnem(mnem, mnem_sz, mnem_str, cond, 0);
	if (strcmp(mnem_str, "(bad)") == 0) {
		ops[0] = '\0';
		return -1;
	}

	if (!p_bit) {
		if (u_bit)
			snprintf(mem_buf, sizeof mem_buf, "[%s], #0x%x",
			         arm_reg_name[rn], offset);
		else
			snprintf(mem_buf, sizeof mem_buf, "[%s], #-0x%x",
			         arm_reg_name[rn], offset);
	} else {
		if (offset == 0)
			snprintf(mem_buf, sizeof mem_buf, "[%s]",
			         arm_reg_name[rn]);
		else if (u_bit)
			snprintf(mem_buf, sizeof mem_buf, "[%s, #0x%x]",
			         arm_reg_name[rn], offset);
		else
			snprintf(mem_buf, sizeof mem_buf, "[%s, #-0x%x]",
			         arm_reg_name[rn], offset);
		if (w_bit) {
			size_t olen = strlen(mem_buf);
			if (olen + 2 < sizeof mem_buf) {
				mem_buf[olen] = '!';
				mem_buf[olen + 1] = '\0';
			}
		}
	}

	snprintf(ops, ops_sz, "%s, %s", arm_reg_name[rd], mem_buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Fill hex string.                                                  */
/* ------------------------------------------------------------------ */
static void
arm_fill_hex(const unsigned char *bytes, size_t size, size_t start,
             size_t len, char *bytes_hex, size_t bytes_hex_sz)
{
	size_t n = len;
	char *p = bytes_hex;
	size_t cap = bytes_hex_sz;
	unsigned i;
	static const char hexdig[] = "0123456789abcdef";

	if (start + n > size)
		n = (size > start) ? (size - start) : 0;
	if (n > 4)
		n = 4;
	bytes_hex[0] = '\0';
	if (n == 0)
		return;
	for (i = 0; i < n; ++i) {
		unsigned byte = bytes[start + i];
		if (i) {
			if (p - bytes_hex + 1 < (ptrdiff_t)cap)
				*p++ = ' ';
		}
		if (p - bytes_hex + 2 < (ptrdiff_t)cap) {
			*p++ = hexdig[byte >> 4];
			*p++ = hexdig[byte & 15];
		}
	}
	*p = '\0';
}

/* ------------------------------------------------------------------ */
/* Public API: disassemble one ARM instruction.                     */
/* ------------------------------------------------------------------ */
int
mt_disasm_arm_one(const unsigned char *bytes, size_t size,
                  size_t offset, uint64_t addr,
                  struct mt_disasm_insn *out)
{
	uint32_t insn;
	unsigned cond;
	int rc;

	out->address = addr;
	out->offset = offset;
	out->length = 4;
	out->operands[0] = '\0';
	out->mnemonic[0] = '\0';

	if (arm_fetch32(bytes, size, offset, &insn) < 0) {
		out->length = 1;
		snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
		arm_fill_hex(bytes, size, offset, 1,
		             out->bytes_hex, sizeof out->bytes_hex);
		return -1;
	}

	cond = (insn >> 28) & 0xF;

	/* ---- NOP ---- */
	if (insn == 0xE1A00000 || insn == 0xE320F000) {
		snprintf(out->mnemonic, sizeof out->mnemonic, "nop");
		out->operands[0] = '\0';
		goto done;
	}

	/* ---- BX / BLX ---- */
	if ((insn & 0x0FFFFFF0) == ((cond << 28) | 0x012FFF10)) {
		unsigned rm = insn & 0xF;
		arm_set_mnem(out->mnemonic, sizeof out->mnemonic,
		             "bx", cond, 0);
		snprintf(out->operands, sizeof out->operands, "%s",
		         arm_reg_name[rm]);
		goto done;
	}
	if ((insn & 0x0FFFFFF0) == ((cond << 28) | 0x012FFF30)) {
		unsigned rm = insn & 0xF;
		arm_set_mnem(out->mnemonic, sizeof out->mnemonic,
		             "blx", cond, 0);
		snprintf(out->operands, sizeof out->operands, "%s",
		         arm_reg_name[rm]);
		goto done;
	}

	/* ---- CLZ ---- */
	if ((insn & 0x0FFF0FF0) == ((cond << 28) | 0x016F0F10)) {
		unsigned rd_clz = (insn >> 12) & 0xF;
		unsigned rm_clz = insn & 0xF;
		arm_set_mnem(out->mnemonic, sizeof out->mnemonic,
		             "clz", cond, 0);
		snprintf(out->operands, sizeof out->operands, "%s, %s",
		         arm_reg_name[rd_clz], arm_reg_name[rm_clz]);
		goto done;
	}

	/* ---- MOVW / MOVT ---- */
	if ((insn & 0x0FF00000) == 0x03000000 ||
	    (insn & 0x0FF00000) == 0x03400000) {
		rc = arm_decode_movw_movt(insn, cond,
		                          out->mnemonic, sizeof out->mnemonic,
		                          out->operands, sizeof out->operands);
		if (rc == 0)
			goto done;
	}

	/* ---- Branch: bit[27:25] = 101 ---- */
	if ((insn & 0x0E000000) == 0x0A000000) {
		rc = arm_decode_branch(insn, cond, addr,
		                       out->mnemonic, sizeof out->mnemonic,
		                       out->operands, sizeof out->operands);
		if (rc == 0)
			goto done;
	}

	/* ---- LDM / STM: bit[27:25] = 100 ---- */
	if ((insn & 0x0E000000) == 0x08000000) {
		rc = arm_decode_ldm_stm(insn, cond,
		                        out->mnemonic, sizeof out->mnemonic,
		                        out->operands, sizeof out->operands);
		if (rc == 0)
			goto done;
	}

	/* ---- Load/Store halfword / signed byte ---- */
	/* bit[27:24] = 0001, bit[7]=1, bit[4]=1, not BX/BLX */
	if ((insn & 0x0F000000) == 0x01000000 &&
	    (insn & 0x000000F0) == 0x000000B0) {
		if ((insn & 0x00000F00) != 0x00000F00 ||
		    ((insn >> 4) & 0xF) != 0x1) {
			rc = arm_decode_ldrh_strh(insn, cond,
			                          out->mnemonic, sizeof out->mnemonic,
			                          out->operands, sizeof out->operands);
			if (rc == 0)
				goto done;
		}
	}

	/* ---- Load/Store word/byte: bit[27:26] = 01 ---- */
	if ((insn & 0x0C000000) == 0x04000000) {
		rc = arm_decode_ldr_str(insn, cond, addr,
		                        out->mnemonic, sizeof out->mnemonic,
		                        out->operands, sizeof out->operands);
		if (rc == 0)
			goto done;
	}

	/* ---- SWI: bit[27:24] = 1111 ---- */
	if ((insn & 0x0F000000) == 0x0F000000) {
		rc = arm_decode_swi(insn, cond,
		                    out->mnemonic, sizeof out->mnemonic,
		                    out->operands, sizeof out->operands);
		if (rc == 0)
			goto done;
	}

	/* ---- Data processing: bit[27:26] = 00 ---- */
	if ((insn & 0x0C000000) == 0x00000000) {
		rc = arm_decode_dp(insn, cond,
		                   out->mnemonic, sizeof out->mnemonic,
		                   out->operands, sizeof out->operands);
		if (rc == 0)
			goto done;
	}

	/* ---- Unrecognized ---- */
	snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
	out->operands[0] = '\0';
	out->length = 4;

done:
	arm_fill_hex(bytes, size, offset, out->length,
	             out->bytes_hex, sizeof out->bytes_hex);
	return (out->mnemonic[0] != '(') ? 0 : -1;
}
