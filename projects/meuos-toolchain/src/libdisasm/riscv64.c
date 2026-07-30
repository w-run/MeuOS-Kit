/* riscv64.c - RISC-V 64-bit disassembler.
 *
 * Implements mt_disasm_riscv64_one(): decode a single RV64 instruction from a
 * raw byte stream.  Supports the base RV64I integer instruction set plus the
 * M extension (multiply/divide) and the privileged ecall/ebreak/csr family.
 *
 * Instruction encoding follows the RISC-V Unprivileged Spec v20191213.
 * 32-bit instructions are identified by the lower two bits of opcode == 3.
 * 16-bit compressed instructions (lower bits 00/01/10) are returned as
 * "(bad)" with a 2-byte length guess.
 *
 * Operand syntax (GNU objdump-compatible for RISC-V):
 *   R-type:   mnemonic rd, rs1, rs2        (e.g. "add a0, a1, a2")
 *   I-type:   mnemonic rd, rs1, imm         (e.g. "addi a0, a1, 42")
 *   S-type:   mnemonic rs2, imm(rs1)        (e.g. "sw a2, 8(sp)")
 *   B-type:   mnemonic rs1, rs2, offset     (e.g. "beq a0, a1, 0x1004")
 *   U-type:   mnemonic rd, imm              (e.g. "lui a0, 0x12345")
 *   J-type:   mnemonic rd, offset           (e.g. "jal ra, 0x1004")
 *   CSR:      csr<op> rd, csr, rs1          (or rs1/uimm for CSRWI variants)
 *   ECALL/EBREAK: no operands
 */

#include "mt/disasm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Register ABI names (x0–x31)                                        */
/* ------------------------------------------------------------------ */

static const char *const riscv_reg[32] = {
	"zero", "ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
	"s0",   "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
	"a6",   "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
	"s8",   "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"
};

/* ------------------------------------------------------------------ */
/* Hex digit / string builder helpers                                  */
/* ------------------------------------------------------------------ */

static char
hexdigit(unsigned v)
{
	return "0123456789abcdef"[v & 15];
}

/* ------------------------------------------------------------------ */
/* Instruction fetch helpers (little-endian)                           */
/* ------------------------------------------------------------------ */

static int
fetch32(const unsigned char *bytes, size_t size, size_t pos, uint32_t *v)
{
	if (pos + 4 > size)
		return -1;
	*v = (uint32_t)bytes[pos]
	     | ((uint32_t)bytes[pos + 1] << 8)
	     | ((uint32_t)bytes[pos + 2] << 16)
	     | ((uint32_t)bytes[pos + 3] << 24);
	return 0;
}

/* Sign-extend a value of `bits` bit width (bits includes sign bit). */
static int64_t
sign_ext64(uint64_t val, int bits)
{
	uint64_t m = 1ULL << (bits - 1);
	uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
	val &= mask;
	if (val & m)
		val |= ~mask;
	return (int64_t)val;
}

/* ------------------------------------------------------------------ */
/* RV32I field extractors (operate on a fetched insn word)            */
/* ------------------------------------------------------------------ */

static inline unsigned
opcode(uint32_t insn)
{
	return insn & 0x7f;
}

static inline unsigned
rd(uint32_t insn)
{
	return (insn >> 7) & 0x1f;
}

static inline unsigned
funct3(uint32_t insn)
{
	return (insn >> 12) & 0x7;
}

static inline unsigned
rs1(uint32_t insn)
{
	return (insn >> 15) & 0x1f;
}

static inline unsigned
rs2(uint32_t insn)
{
	return (insn >> 20) & 0x1f;
}

static inline unsigned
funct7(uint32_t insn)
{
	return (insn >> 25) & 0x7f;
}

/* I-immediate: 12-bit, sign-extended. */
static inline int64_t
i_imm(uint32_t insn)
{
	return sign_ext64((insn >> 20), 12);
}

/* S-immediate: {imm[11:5], imm[4:0]} */
static inline int64_t
s_imm(uint32_t insn)
{
	unsigned hi = (insn >> 25) & 0x7f; /* imm[11:5] */
	unsigned lo = (insn >> 7)  & 0x1f; /* imm[4:0]  */
	return sign_ext64((hi << 5) | lo, 12);
}

/* B-immediate: {imm[12], imm[10:5], imm[4:1], imm[11], 0} */
static inline int64_t
b_imm(uint32_t insn)
{
	unsigned b12  = (insn >> 31) & 1;
	unsigned b11  = (insn >> 7)  & 1;
	unsigned b10_5 = (insn >> 25) & 0x3f;
	unsigned b4_1  = (insn >> 8)  & 0xf;
	return sign_ext64((b12 << 12) | (b11 << 11) | (b10_5 << 5) | (b4_1 << 1), 13);
}

/* U-immediate: upper 20 bits. */
static inline int64_t
u_imm(uint32_t insn)
{
	return (int64_t)(insn & 0xfffff000);
}

/* J-immediate: {imm[20], imm[10:1], imm[11], imm[19:12], 0} */
static inline int64_t
j_imm(uint32_t insn)
{
	unsigned j20   = (insn >> 31) & 1;
	unsigned j19_12 = (insn >> 12) & 0xff;
	unsigned j11   = (insn >> 20) & 1;
	unsigned j10_1 = (insn >> 21) & 0x3ff;
	return sign_ext64((j20 << 20) | (j19_12 << 12) | (j11 << 11) | (j10_1 << 1), 21);
}

/* CSR immediate (5-bit zero-extended). */
static inline unsigned
csr_zimm(uint32_t insn)
{
	return (insn >> 15) & 0x1f; /* rs1 field used as uimm */
}

static inline unsigned
csr_idx(uint32_t insn)
{
	return (insn >> 20); /* upper 12 bits */
}

/* ------------------------------------------------------------------ */
/* Write the byte hex into out->bytes_hex                              */
/* ------------------------------------------------------------------ */

static void
fill_hex(const unsigned char *bytes, size_t size, size_t start,
         size_t len, struct mt_disasm_insn *out)
{
	char *p = out->bytes_hex;
	size_t cap = sizeof out->bytes_hex;
	size_t n, i;

	n = len;
	if (start + n > size)
		n = (size > start) ? (size - start) : 0;
	if (n > 15)
		n = 15;
	p[0] = '\0';
	for (i = 0; i < n; ++i) {
		unsigned byte = bytes[start + i];
		if (i) {
			if (p - out->bytes_hex + 1 < (ptrdiff_t)cap)
				*p++ = ' ';
		}
		if (p - out->bytes_hex + 2 < (ptrdiff_t)cap) {
			*p++ = hexdigit(byte >> 4);
			*p++ = hexdigit(byte & 15);
		}
	}
	*p = '\0';
}

/* ------------------------------------------------------------------ */
/* Output finalisation helpers                                        */
/* ------------------------------------------------------------------ */

static int
fail(struct mt_disasm_insn *out, uint64_t addr, size_t offset,
     size_t length, const unsigned char *bytes, size_t size)
{
	if (length == 0)
		length = 1;
	out->address = addr;
	out->offset = offset;
	out->length = length;
	out->operands[0] = '\0';
	snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
	fill_hex(bytes, size, offset, length, out);
	return -1;
}

static int
success(struct mt_disasm_insn *out, uint64_t addr, size_t offset,
        size_t length, const unsigned char *bytes, size_t size,
        const char *mnemonic, const char *operands)
{
	out->address = addr;
	out->offset = offset;
	out->length = length;
	snprintf(out->mnemonic, sizeof out->mnemonic, "%s", mnemonic);
	snprintf(out->operands, sizeof out->operands, "%s", operands);
	fill_hex(bytes, size, offset, length, out);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode one RISC-V 64-bit instruction                                */
/* ------------------------------------------------------------------ */

int
mt_disasm_riscv64_one(const unsigned char *bytes, size_t size,
                      size_t offset, uint64_t addr,
                      struct mt_disasm_insn *out)
{
	uint32_t insn;
	unsigned op, r3, r1, r2, f3, f7;
	int64_t imm;
	char ops[MT_DISASM_OPS_CAP];

	if (fetch32(bytes, size, offset, &insn) < 0)
		return fail(out, addr, offset, 0, bytes, size);

	/* Check for compressed instruction: lower two bits != 3 */
	if ((insn & 3) != 3)
		return fail(out, addr, offset, 2, bytes, size);

	op = opcode(insn);
	r3 = rd(insn);
	r1 = rs1(insn);
	r2 = rs2(insn);
	f3 = funct3(insn);
	f7 = funct7(insn);

	(void)addr; /* used implicitly in branch/jal target computation below */

	switch (op) {

	/* ============================================================== */
	/* OP-IMM (0x13) — arithmetic with immediate                      */
	/* ============================================================== */
	case 0x13:
		imm = i_imm(insn);
		switch (f3) {
		case 0: /* ADDI */
			snprintf(ops, sizeof ops, "%s, %s, %lld",
			         riscv_reg[r3], riscv_reg[r1], (long long)imm);
			return success(out, addr, offset, 4, bytes, size,
			               "addi", ops);
		case 1: /* SLLI */
			snprintf(ops, sizeof ops, "%s, %s, %lld",
			         riscv_reg[r3], riscv_reg[r1],
			         (long long)((insn >> 20) & 0x3f));
			return success(out, addr, offset, 4, bytes, size,
			               "slli", ops);
		case 2: /* SLTI */
			snprintf(ops, sizeof ops, "%s, %s, %lld",
			         riscv_reg[r3], riscv_reg[r1], (long long)imm);
			return success(out, addr, offset, 4, bytes, size,
			               "slti", ops);
		case 3: /* SLTIU */
			snprintf(ops, sizeof ops, "%s, %s, %lld",
			         riscv_reg[r3], riscv_reg[r1], (long long)imm);
			return success(out, addr, offset, 4, bytes, size,
			               "sltiu", ops);
		case 4: /* XORI */
			snprintf(ops, sizeof ops, "%s, %s, %lld",
			         riscv_reg[r3], riscv_reg[r1], (long long)imm);
			return success(out, addr, offset, 4, bytes, size,
			               "xori", ops);
		case 5: /* SRLI / SRAI */
			{
				unsigned shamt = (insn >> 20) & 0x3f;
				if (f7 == 0x00) {
					snprintf(ops, sizeof ops,
					         "%s, %s, %u",
					         riscv_reg[r3], riscv_reg[r1],
					         shamt);
					return success(out, addr, offset,
					               4, bytes, size, "srli", ops);
				} else if (f7 == 0x20) {
					snprintf(ops, sizeof ops,
					         "%s, %s, %u",
					         riscv_reg[r3], riscv_reg[r1],
					         shamt);
					return success(out, addr, offset,
					               4, bytes, size, "srai", ops);
				}
			}
			return fail(out, addr, offset, 4, bytes, size);
		case 6: /* ORI */
			snprintf(ops, sizeof ops, "%s, %s, %lld",
			         riscv_reg[r3], riscv_reg[r1], (long long)imm);
			return success(out, addr, offset, 4, bytes, size,
			               "ori", ops);
		case 7: /* ANDI */
			snprintf(ops, sizeof ops, "%s, %s, %lld",
			         riscv_reg[r3], riscv_reg[r1], (long long)imm);
			return success(out, addr, offset, 4, bytes, size,
			               "andi", ops);
		default:
			return fail(out, addr, offset, 4, bytes, size);
		}

	/* ============================================================== */
	/* OP (0x33) — register-register operations (I + M extensions)    */
	/* ============================================================== */
	case 0x33:
		switch (f3) {
		case 0: /* ADD / SUB / MUL */
			if (f7 == 0x00) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "add", ops);
			} else if (f7 == 0x20) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "sub", ops);
			} else if (f7 == 0x01) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "mul", ops);
			}
			return fail(out, addr, offset, 4, bytes, size);
		case 1: /* SLL / MULH */
			if (f7 == 0x00) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "sll", ops);
			} else if (f7 == 0x01) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "mulh", ops);
			}
			return fail(out, addr, offset, 4, bytes, size);
		case 2: /* SLT / MULHSU */
			if (f7 == 0x00) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "slt", ops);
			} else if (f7 == 0x01) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "mulhsu", ops);
			}
			return fail(out, addr, offset, 4, bytes, size);
		case 3: /* SLTU / MULHU */
			if (f7 == 0x00) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "sltu", ops);
			} else if (f7 == 0x01) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "mulhu", ops);
			}
			return fail(out, addr, offset, 4, bytes, size);
		case 4: /* XOR / DIV */
			if (f7 == 0x00) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "xor", ops);
			} else if (f7 == 0x01) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "div", ops);
			}
			return fail(out, addr, offset, 4, bytes, size);
		case 5: /* SRL / SRA / DIVU */
			if (f7 == 0x00) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "srl", ops);
			} else if (f7 == 0x20) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "sra", ops);
			} else if (f7 == 0x01) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "divu", ops);
			}
			return fail(out, addr, offset, 4, bytes, size);
		case 6: /* OR / REM */
			if (f7 == 0x00) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "or", ops);
			} else if (f7 == 0x01) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "rem", ops);
			}
			return fail(out, addr, offset, 4, bytes, size);
		case 7: /* AND / REMU */
			if (f7 == 0x00) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "and", ops);
			} else if (f7 == 0x01) {
				snprintf(ops, sizeof ops, "%s, %s, %s",
				         riscv_reg[r3], riscv_reg[r1],
				         riscv_reg[r2]);
				return success(out, addr, offset, 4,
				               bytes, size, "remu", ops);
			}
			return fail(out, addr, offset, 4, bytes, size);
		default:
			return fail(out, addr, offset, 4, bytes, size);
		}

	/* ============================================================== */
	/* LOAD (0x03)                                                     */
	/* ============================================================== */
	case 0x03:
		imm = i_imm(insn);
		switch (f3) {
		case 0:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r3], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "lb", ops);
		case 1:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r3], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "lh", ops);
		case 2:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r3], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "lw", ops);
		case 3:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r3], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "ld", ops);
		case 4:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r3], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "lbu", ops);
		case 5:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r3], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "lhu", ops);
		case 6:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r3], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "lwu", ops);
		default:
			return fail(out, addr, offset, 4, bytes, size);
		}

	/* ============================================================== */
	/* STORE (0x23)                                                    */
	/* ============================================================== */
	case 0x23:
		imm = s_imm(insn);
		switch (f3) {
		case 0:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r2], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "sb", ops);
		case 1:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r2], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "sh", ops);
		case 2:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r2], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "sw", ops);
		case 3:
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r2], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "sd", ops);
		default:
			return fail(out, addr, offset, 4, bytes, size);
		}

	/* ============================================================== */
	/* BRANCH (0x63)                                                   */
	/* ============================================================== */
	case 0x63: {
		uint64_t target;

		imm = b_imm(insn);
		target = addr + 4 + (uint64_t)imm;
		switch (f3) {
		case 0:
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         riscv_reg[r1], riscv_reg[r2],
			         (unsigned long long)target);
			return success(out, addr, offset, 4, bytes, size,
			               "beq", ops);
		case 1:
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         riscv_reg[r1], riscv_reg[r2],
			         (unsigned long long)target);
			return success(out, addr, offset, 4, bytes, size,
			               "bne", ops);
		case 4:
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         riscv_reg[r1], riscv_reg[r2],
			         (unsigned long long)target);
			return success(out, addr, offset, 4, bytes, size,
			               "blt", ops);
		case 5:
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         riscv_reg[r1], riscv_reg[r2],
			         (unsigned long long)target);
			return success(out, addr, offset, 4, bytes, size,
			               "bge", ops);
		case 6:
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         riscv_reg[r1], riscv_reg[r2],
			         (unsigned long long)target);
			return success(out, addr, offset, 4, bytes, size,
			               "bltu", ops);
		case 7:
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         riscv_reg[r1], riscv_reg[r2],
			         (unsigned long long)target);
			return success(out, addr, offset, 4, bytes, size,
			               "bgeu", ops);
		default:
			return fail(out, addr, offset, 4, bytes, size);
		}
	}

	/* ============================================================== */
	/* JAL (0x6F) — jump and link                                      */
	/* ============================================================== */
	case 0x6F: {
		uint64_t target;

		imm = j_imm(insn);
		target = addr + 4 + (uint64_t)imm;
		snprintf(ops, sizeof ops, "%s, 0x%llx",
		         riscv_reg[r3], (unsigned long long)target);
		return success(out, addr, offset, 4, bytes, size, "jal", ops);
	}

	/* ============================================================== */
	/* JALR (0x67) — jump and link register                            */
	/* ============================================================== */
	case 0x67:
		if (f3 == 0) {
			imm = i_imm(insn);
			if (r1 == 0 && imm == 0 && r3 == 0) {
				/* JALR x0, 0(x0) is a canonical HINT */
				ops[0] = '\0';
				return success(out, addr, offset, 4,
				               bytes, size, "jalr", ops);
			}
			snprintf(ops, sizeof ops, "%s, %lld(%s)",
			         riscv_reg[r3], (long long)imm,
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "jalr", ops);
		}
		return fail(out, addr, offset, 4, bytes, size);

	/* ============================================================== */
	/* LUI (0x37) — load upper immediate                               */
	/* ============================================================== */
	case 0x37:
		snprintf(ops, sizeof ops, "%s, 0x%llx",
		         riscv_reg[r3],
		         (unsigned long long)(insn & 0xfffff000));
		return success(out, addr, offset, 4, bytes, size, "lui", ops);

	/* ============================================================== */
	/* AUIPC (0x17) — add upper immediate to PC                        */
	/* ============================================================== */
	case 0x17:
		snprintf(ops, sizeof ops, "%s, 0x%llx",
		         riscv_reg[r3],
		         (unsigned long long)(insn & 0xfffff000));
		return success(out, addr, offset, 4, bytes, size,
		               "auipc", ops);

	/* ============================================================== */
	/* SYSTEM (0x73)                                                   */
	/* ============================================================== */
	case 0x73:
		/* ECALL / EBREAK: funct12 encodes the sub-function. */
		if (f3 == 0 && r3 == 0 && r1 == 0 && (insn >> 20) == 0x000) {
			ops[0] = '\0';
			return success(out, addr, offset, 4, bytes, size,
			               "ecall", ops);
		}
		if (f3 == 0 && r3 == 0 && r1 == 0 && (insn >> 20) == 0x001) {
			ops[0] = '\0';
			return success(out, addr, offset, 4, bytes, size,
			               "ebreak", ops);
		}
		/* CSR instructions */
		switch (f3) {
		/* CSRRW */
		case 1:
			snprintf(ops, sizeof ops, "%s, %u, %s",
			         riscv_reg[r3], csr_idx(insn),
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "csrrw", ops);
		/* CSRRS */
		case 2:
			snprintf(ops, sizeof ops, "%s, %u, %s",
			         riscv_reg[r3], csr_idx(insn),
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "csrrs", ops);
		/* CSRRC */
		case 3:
			snprintf(ops, sizeof ops, "%s, %u, %s",
			         riscv_reg[r3], csr_idx(insn),
			         riscv_reg[r1]);
			return success(out, addr, offset, 4, bytes, size,
			               "csrrc", ops);
		/* CSRRWI */
		case 5:
			snprintf(ops, sizeof ops, "%s, %u, %u",
			         riscv_reg[r3], csr_idx(insn),
			         csr_zimm(insn));
			return success(out, addr, offset, 4, bytes, size,
			               "csrrwi", ops);
		/* CSRRSI */
		case 6:
			snprintf(ops, sizeof ops, "%s, %u, %u",
			         riscv_reg[r3], csr_idx(insn),
			         csr_zimm(insn));
			return success(out, addr, offset, 4, bytes, size,
			               "csrrsi", ops);
		/* CSRRCI */
		case 7:
			snprintf(ops, sizeof ops, "%s, %u, %u",
			         riscv_reg[r3], csr_idx(insn),
			         csr_zimm(insn));
			return success(out, addr, offset, 4, bytes, size,
			               "csrrci", ops);
		default:
			return fail(out, addr, offset, 4, bytes, size);
		}

	/* ============================================================== */
	/* FENCE (0x0F) — not currently decoded, return bad                */
	/* ============================================================== */
	case 0x0F:
	default:
		return fail(out, addr, offset, 4, bytes, size);
	}
}
