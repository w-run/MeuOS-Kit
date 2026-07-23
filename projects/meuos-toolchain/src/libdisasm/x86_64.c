/* x86_64.c - x86_64 disassembler (ATT syntax, objdump-compatible).
 *
 * Implements mt_disasm_x86_64_one(): decode a single x86_64 instruction from a
 * raw byte stream and produce an ATT-syntax mnemonic + operands string.
 *
 * Design: a small stateful decoder struct carries the byte cursor, decoded
 * prefixes and the effective operand/address size. Prefixes are parsed in a
 * loop, then a 1/2/3-byte opcode is dispatched to a handler which consumes the
 * ModR/M, SIB, displacement and immediate fields and fills the mnemonic and
 * operand buffers. Memory operands are formatted with the same disp(base,index,
 * scale) notation as GNU objdump; relative branches show the absolute target.
 *
 * Coverage: P0 integer/control-flow/stack (mcc/QBE/libc core), P1 SSE basic +
 * system instructions, P2 VEX/x87 handled as length-correct "(bad)".
 */
#include "mt/disasm.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Register name tables                                                */
/* ------------------------------------------------------------------ */

static const char *const reg64[16] = {
	"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};
static const char *const reg32[16] = {
	"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
	"r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
};
static const char *const reg16[16] = {
	"ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
	"r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"
};
/* 8-bit GPRs when a REX prefix is present (spl/bpl/sil/dil, r8b-r15b). */
static const char *const reg8_rex[16] = {
	"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
	"r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"
};
/* 8-bit GPRs without REX (only 0-7; 4-7 are ah/ch/dh/bh). */
static const char *const reg8_norex[8] = {
	"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"
};
static const char *const sreg_name[6] = {"es", "cs", "ss", "ds", "fs", "gs"};

/* group1 (0x00-0x3D, 0x80-0x83) operation names indexed by /reg. */
static const char *const grp1_name[8] = {
	"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"
};
/* group2 (shifts) indexed by /reg. /4 and /6 are both shl (sal is an alias). */
static const char *const grp2_name[8] = {
	"rol", "ror", "rcl", "rcr", "shl", "shr", "shl", "sar"
};
/* group3 (0xF6/0xF7) names. */
static const char *const grp3_name[8] = {
	"test", "test", "not", "neg", "mul", "imul", "div", "idiv"
};
/* group4 (0xFE) byte inc/dec. */
static const char *const grp4_name[8] = {
	"inc", "dec", "(bad)", "(bad)", "(bad)", "(bad)", "(bad)", "(bad)"
};
/* group5 (0xFF). */
static const char *const grp5_name[8] = {
	"inc", "dec", "call", "(bad)", "jmp", "(bad)", "push", "(bad)"
};
/* group8 (0x0F 0xBA) bt family. */
static const char *const grp8_name[8] = {
	"(bad)", "(bad)", "(bad)", "(bad)", "bt", "bts", "btr", "btc"
};

/* jcc / cmovcc / setcc condition names (jo..jg). */
static const char *const cc_name[16] = {
	"o", "no", "b", "ae", "e", "ne", "be", "a",
	"s", "ns", "p", "np", "l", "ge", "le", "g"
};

/* ------------------------------------------------------------------ */
/* Decoder state                                                       */
/* ------------------------------------------------------------------ */

#define REX_W 0x08
#define REX_R 0x04
#define REX_X 0x02
#define REX_B 0x01

struct mem_op {
	int base;     /* -1 if none */
	int index;     /* -1 if none */
	int scale;     /* 1, 2, 4 or 8 */
	int64_t disp;
	int has_disp;
	int rip;      /* RIP-relative addressing */
};

struct dec {
	const unsigned char *bytes;
	size_t size;
	size_t start;
	size_t pos;
	uint64_t addr;
	int rex;          /* 0 or the REX byte value */
	int osz;          /* effective operand size: 2, 4 or 8 (byte ops bypass) */
	int addr_sz;      /* 4 or 8 */
	int lock;
	int rep;           /* F3 */
	int repne;         /* F2 */
	int osize_pfx;     /* 66 */
	int seg;           /* -1 or 0..5 */
	char mnem[20];     /* slightly oversized for "lock "/"rep " prefixes */
	char ops[168];
};

/* Tiny append-only string builder used for memory/operand formatting. */
struct sbuf {
	char *p;
	size_t cap;
	size_t len;
};

static void
sb_putc(struct sbuf *s, char c)
{
	if (s->len + 1 < s->cap)
		s->p[s->len++] = c;
	s->p[s->len] = '\0';
}

static void
sb_puts(struct sbuf *s, const char *str)
{
	while (*str)
		sb_putc(s, *str++);
}

static void
sb_printf(struct sbuf *s, const char *fmt, ...)
{
	char tmp[80];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	sb_puts(s, tmp);
}

/* ------------------------------------------------------------------ */
/* Sign extension / masking helpers                                    */
/* ------------------------------------------------------------------ */

static int64_t
sign_ext(uint64_t val, int bytes_read)
{
	uint64_t u = val;
	unsigned bits;
	uint64_t mask;

	if (bytes_read >= 8)
		return (int64_t)u;
	bits = (unsigned)bytes_read * 8u;
	mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
	u &= mask;
	if (bits < 64 && (u & (1ULL << (bits - 1))))
		u |= ~0ULL << bits;
	return (int64_t)u;
}

static uint64_t
mask_osz(int64_t val, int osz_bytes)
{
	uint64_t u = (uint64_t)val;
	uint64_t mask;

	if (osz_bytes >= 8)
		return u;
	mask = (1ULL << (osz_bytes * 8)) - 1;
	return u & mask;
}

static char
hexdigit(unsigned v)
{
	return "0123456789abcdef"[v & 15];
}

/* ------------------------------------------------------------------ */
/* Byte fetch with bounds checking                                     */
/* ------------------------------------------------------------------ */

static int
fetch8(struct dec *d, int *v)
{
	if (d->pos >= d->size)
		return -1;
	*v = d->bytes[d->pos++];
	return 0;
}

static int
fetch16(struct dec *d, int *v)
{
	if (d->pos + 2 > d->size)
		return -1;
	*v = d->bytes[d->pos] | (d->bytes[d->pos + 1] << 8);
	d->pos += 2;
	return 0;
}

static int
fetch32(struct dec *d, uint32_t *v)
{
	if (d->pos + 4 > d->size)
		return -1;
	*v = (uint32_t)d->bytes[d->pos]
	     | ((uint32_t)d->bytes[d->pos + 1] << 8)
	     | ((uint32_t)d->bytes[d->pos + 2] << 16)
	     | ((uint32_t)d->bytes[d->pos + 3] << 24);
	d->pos += 4;
	return 0;
}

static int
fetch64(struct dec *d, uint64_t *v)
{
	uint64_t r = 0;
	int i;

	if (d->pos + 8 > d->size)
		return -1;
	for (i = 0; i < 8; ++i)
		r |= (uint64_t)d->bytes[d->pos + i] << (i * 8);
	*v = r;
	d->pos += 8;
	return 0;
}

static int
fetch_disp8(struct dec *d, int64_t *v)
{
	int b;

	if (fetch8(d, &b) < 0)
		return -1;
	*v = (int64_t)(int8_t)b;
	return 0;
}

static int
fetch_disp32(struct dec *d, int64_t *v)
{
	uint32_t u;

	if (fetch32(d, &u) < 0)
		return -1;
	*v = (int64_t)(int32_t)u;
	return 0;
}

/* Read an immediate of 1/2/4/8 bytes, sign-extended to 64 bits. */
static int
fetch_imm(struct dec *d, int bytes, int64_t *v)
{
	if (bytes == 1) {
		int b;
		if (fetch8(d, &b) < 0)
			return -1;
		*v = sign_ext((uint64_t)(unsigned)b, 1);
		return 0;
	}
	if (bytes == 2) {
		int b;
		if (fetch16(d, &b) < 0)
			return -1;
		*v = sign_ext((uint64_t)(unsigned)b, 2);
		return 0;
	}
	if (bytes == 4) {
		uint32_t u;
		if (fetch32(d, &u) < 0)
			return -1;
		*v = sign_ext((uint64_t)u, 4);
		return 0;
	}
	if (bytes == 8) {
		uint64_t u;
		if (fetch64(d, &u) < 0)
			return -1;
		*v = (int64_t)u;
		return 0;
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* Register / operand formatting                                       */
/* ------------------------------------------------------------------ */

static const char *
reg_name(int reg, int osz, int has_rex)
{
	if (osz == 8)
		return reg64[reg & 15];
	if (osz == 4)
		return reg32[reg & 15];
	if (osz == 2)
		return reg16[reg & 15];
	/* byte */
	if (has_rex)
		return reg8_rex[reg & 15];
	return reg8_norex[reg & 7];
}

static const char *
addr_reg_name(int reg, int addr_sz)
{
	if (addr_sz == 4)
		return reg32[reg & 15];
	return reg64[reg & 15];
}

static void
fmt_imm(char *buf, size_t bufsz, int64_t val, int osz)
{
	uint64_t u = mask_osz(val, osz);
	snprintf(buf, bufsz, "$0x%llx", (unsigned long long)u);
}

/* Format a memory operand: [seg:] disp(base,index,scale) in ATT form. */
static void
fmt_mem(struct dec *d, struct sbuf *o, struct mem_op *m)
{
	if (d->seg >= 0)
		sb_printf(o, "%%%s:", sreg_name[d->seg]);

	if (m->rip) {
		if (m->disp < 0)
			sb_printf(o, "-0x%llx", (unsigned long long)(-m->disp));
		else
			sb_printf(o, "0x%llx", (unsigned long long)m->disp);
		sb_puts(o, "(%rip)");
		return;
	}

	if (m->base < 0 && m->index < 0) {
		/* pure disp32 absolute address (SIB base=101 mod=00) */
		sb_printf(o, "0x%llx", (unsigned long long)(uint32_t)m->disp);
		return;
	}

	if (m->has_disp) {
		if (m->disp < 0)
			sb_printf(o, "-0x%llx", (unsigned long long)(-m->disp));
		else
			sb_printf(o, "0x%llx", (unsigned long long)m->disp);
	}
	sb_putc(o, '(');
	if (m->base >= 0)
		sb_printf(o, "%%%s", addr_reg_name(m->base, d->addr_sz));
	if (m->index >= 0) {
		sb_putc(o, ',');
		sb_printf(o, "%%%s", addr_reg_name(m->index, d->addr_sz));
		sb_printf(o, ",%d", m->scale);
	}
	sb_putc(o, ')');
}

/* Format the r/m operand (register or memory) of a ModR/M. */
static void
fmt_rm_operand(struct dec *d, char *buf, size_t bufsz,
               int is_mem, int rm, struct mem_op *mem, int is_byte)
{
	if (is_mem) {
		struct sbuf o = {buf, bufsz, 0};
		buf[0] = '\0';
		fmt_mem(d, &o, mem);
	} else {
		snprintf(buf, bufsz, "%%%s",
		         reg_name(rm, is_byte ? 1 : d->osz, d->rex != 0));
	}
}

static void
fmt_reg_operand(struct dec *d, char *buf, size_t bufsz, int reg, int is_byte)
{
	snprintf(buf, bufsz, "%%%s",
	         reg_name(reg, is_byte ? 1 : d->osz, d->rex != 0));
}

static void
fmt_xmm(char *buf, size_t bufsz, int reg)
{
	snprintf(buf, bufsz, "%%xmm%d", reg & 15);
}

static void
set_mnem(struct dec *d, const char *s)
{
	snprintf(d->mnem, sizeof d->mnem, "%s", s);
}

/* Bounded string copy that never triggers -Wformat-truncation. */
static void
copy_str(char *dst, size_t dstsz, const char *src)
{
	size_t n = 0;

	if (dstsz == 0)
		return;
	while (n < dstsz - 1 && src[n] != '\0') {
		dst[n] = src[n];
		++n;
	}
	dst[n] = '\0';
}

/* Append src to dst (NUL-terminated, bounded by dstsz). *len tracks the
 * current logical length of dst. */
static void
append_str(char *dst, size_t dstsz, size_t *len, const char *src)
{
	while (*len < dstsz - 1 && *src != '\0')
		dst[(*len)++] = *src++;
	dst[*len] = '\0';
}

/* ------------------------------------------------------------------ */
/* ModR/M + SIB decoder                                                */
/* ------------------------------------------------------------------ */

static int
decode_modrm(struct dec *d, int *reg, int *is_mem, int *rm_reg,
             struct mem_op *mem)
{
	int mrm, mod, ro, rm, rm_full;
	int sib, scale, idx, base, idx_full, base_full;

	if (fetch8(d, &mrm) < 0)
		return -1;
	mod = (mrm >> 6) & 3;
	ro = (mrm >> 3) & 7;
	rm = mrm & 7;
	*reg = ro | ((d->rex & REX_R) ? 8 : 0);

	if (mod == 3) {
		*is_mem = 0;
		*rm_reg = rm | ((d->rex & REX_B) ? 8 : 0);
		return 0;
	}
	*is_mem = 1;
	*rm_reg = -1;
	mem->base = -1;
	mem->index = -1;
	mem->scale = 1;
	mem->disp = 0;
	mem->has_disp = 0;
	mem->rip = 0;
	rm_full = rm | ((d->rex & REX_B) ? 8 : 0);

	if (rm == 4) {
		/* SIB byte follows. */
		if (fetch8(d, &sib) < 0)
			return -1;
		scale = 1 << ((sib >> 6) & 3);
		idx = (sib >> 3) & 7;
		base = sib & 7;
		idx_full = idx | ((d->rex & REX_X) ? 8 : 0);
		base_full = base | ((d->rex & REX_B) ? 8 : 0);
		if (idx != 4) {       /* rsp cannot be an index */
			mem->index = idx_full;
			mem->scale = scale;
		}
		if (mod == 0 && base == 5) {
			/* disp32 only, no base register */
			if (fetch_disp32(d, &mem->disp) < 0)
				return -1;
			mem->has_disp = 1;
		} else {
			mem->base = base_full;
			if (mod == 1) {
				if (fetch_disp8(d, &mem->disp) < 0)
					return -1;
				mem->has_disp = 1;
			} else if (mod == 2) {
				if (fetch_disp32(d, &mem->disp) < 0)
					return -1;
				mem->has_disp = 1;
			}
		}
	} else if (mod == 0 && rm == 5) {
		/* RIP-relative in 64-bit mode. */
		if (fetch_disp32(d, &mem->disp) < 0)
			return -1;
		mem->rip = 1;
		mem->has_disp = 1;
	} else {
		mem->base = rm_full;
		if (mod == 1) {
			if (fetch_disp8(d, &mem->disp) < 0)
				return -1;
			mem->has_disp = 1;
		} else if (mod == 2) {
			if (fetch_disp32(d, &mem->disp) < 0)
				return -1;
			mem->has_disp = 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Output finalization                                                 */
/* ------------------------------------------------------------------ */

static void
fill_hex(struct dec *d, struct mt_disasm_insn *out, size_t len)
{
	char *p = out->bytes_hex;
	size_t cap = sizeof out->bytes_hex;
	size_t n, i;

	n = len;
	if (d->start + n > d->size)
		n = (d->size > d->start) ? (d->size - d->start) : 0;
	if (n > 15)
		n = 15;
	p[0] = '\0';
	if (n == 0)
		return;
	for (i = 0; i < n; ++i) {
		unsigned byte = d->bytes[d->start + i];
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

static int
finish(struct dec *d, struct mt_disasm_insn *out)
{
	size_t len = d->pos - d->start;
	size_t mlen = 0;

	if (len == 0)
		len = 1;
	/* prepend "lock " if a LOCK prefix is present */
	out->mnemonic[0] = '\0';
	if (d->lock)
		append_str(out->mnemonic, sizeof out->mnemonic, &mlen, "lock ");
	append_str(out->mnemonic, sizeof out->mnemonic, &mlen, d->mnem);
	copy_str(out->operands, sizeof out->operands, d->ops);
	out->address = d->addr;
	out->offset = d->start;
	out->length = len;
	fill_hex(d, out, len);
	return 0;
}

static int
fail(struct dec *d, struct mt_disasm_insn *out)
{
	size_t len = d->pos - d->start;

	if (len == 0)
		len = 1;
	out->address = d->addr;
	out->offset = d->start;
	out->length = len;
	out->operands[0] = '\0';
	snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
	fill_hex(d, out, len);
	return -1;
}

/* ------------------------------------------------------------------ */
/* Common operand helpers                                              */
/* ------------------------------------------------------------------ */

/* reg/mem + reg operand. reg_is_src=1 -> ATT order "reg, rm",
 * reg_is_src=0 -> "rm, reg". is_byte selects byte register naming. */
static int
op_mr(struct dec *d, const char *mnem, int is_byte, int reg_is_src)
{
	int reg, is_mem, rm;
	struct mem_op mem;
	char rms[96], regs[16];

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, is_byte);
	fmt_reg_operand(d, regs, sizeof regs, reg, is_byte);
	if (reg_is_src)
		snprintf(d->ops, sizeof d->ops, "%s, %s", regs, rms);
	else
		snprintf(d->ops, sizeof d->ops, "%s, %s", rms, regs);
	set_mnem(d, mnem);
	return 0;
}

/* single r/m operand (reg or mem). */
static int
op_rm(struct dec *d, const char *mnem, int is_byte)
{
	int reg, is_mem, rm;
	struct mem_op mem;
	char rms[96];

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, is_byte);
	snprintf(d->ops, sizeof d->ops, "%s", rms);
	set_mnem(d, mnem);
	return 0;
}

/* lea: memory (address) -> reg. The rm operand is always a memory address. */
static int
op_lea(struct dec *d, const char *mnem)
{
	int reg, is_mem, rm;
	struct mem_op mem;
	char mems[96], regs[16];
	struct sbuf o = {mems, sizeof mems, 0};

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	(void)is_mem;
	(void)rm;
	mems[0] = '\0';
	fmt_mem(d, &o, &mem);
	snprintf(regs, sizeof regs, "%%%s", reg_name(reg, d->osz, 0));
	snprintf(d->ops, sizeof d->ops, "%s, %s", mems, regs);
	set_mnem(d, mnem);
	return 0;
}

/* AL, Ib form (op al, imm8). */
static int
op_al_imm(struct dec *d, const char *mnem)
{
	int64_t imm;
	char ib[32];

	if (fetch_imm(d, 1, &imm) < 0)
		return -1;
	fmt_imm(ib, sizeof ib, imm, 1);
	snprintf(d->ops, sizeof d->ops, "%s, %%al", ib);
	set_mnem(d, mnem);
	return 0;
}

/* eAX, Iz form (op eax/rax/ax, imm). */
static int
op_ax_imm(struct dec *d, const char *mnem)
{
	int iz = (d->osz == 2) ? 2 : 4;
	int64_t imm;
	char ib[32];

	if (fetch_imm(d, iz, &imm) < 0)
		return -1;
	fmt_imm(ib, sizeof ib, imm, d->osz);
	snprintf(d->ops, sizeof d->ops, "%s, %%%s", ib,
	         reg_name(0, d->osz, 0));
	set_mnem(d, mnem);
	return 0;
}

/* group1 (0x80/0x81/0x83): op r/m, imm. */
static int
op_grp1(struct dec *d, int form)
{
	int reg, is_mem, rm, ib_bytes;
	struct mem_op mem;
	const char *mnem;
	int is_byte, imm_osz;
	char rms[96], ib[32];
	int64_t imm;

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	mnem = grp1_name[reg & 7];
	is_byte = (form == 0x80);
	ib_bytes = (form == 0x81) ? ((d->osz == 2) ? 2 : 4) : 1;
	if (fetch_imm(d, ib_bytes, &imm) < 0)
		return -1;
	imm_osz = is_byte ? 1 : d->osz;
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, is_byte);
	fmt_imm(ib, sizeof ib, imm, imm_osz);
	snprintf(d->ops, sizeof d->ops, "%s, %s", ib, rms);
	set_mnem(d, mnem);
	return 0;
}

/* group2 shifts (0xC0/0xC1/0xD0/0xD1/0xD2/0xD3). */
static int
op_grp2(struct dec *d, int form)
{
	int reg, is_mem, rm, is_byte;
	struct mem_op mem;
	const char *mnem;
	char rms[96];

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	mnem = grp2_name[reg & 7];
	is_byte = (form == 0xC0 || form == 0xD0 || form == 0xD2);
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, is_byte);
	if (form == 0xD0 || form == 0xD1) {
		/* shift by implicit 1 (objdump shows "$1", decimal) */
		snprintf(d->ops, sizeof d->ops, "$1, %s", rms);
	} else if (form == 0xD2 || form == 0xD3) {
		/* shift by CL */
		snprintf(d->ops, sizeof d->ops, "%%cl, %s", rms);
	} else {
		/* shift by imm8 */
		int64_t imm;
		char ib[32];
		if (fetch_imm(d, 1, &imm) < 0)
			return -1;
		fmt_imm(ib, sizeof ib, imm, 1);
		snprintf(d->ops, sizeof d->ops, "%s, %s", ib, rms);
	}
	set_mnem(d, mnem);
	return 0;
}

/* group3 (0xF6/0xF7): test/not/neg/mul/imul/div/idiv. */
static int
op_grp3(struct dec *d, int form)
{
	int reg, is_mem, rm, sub, is_byte;
	struct mem_op mem;
	char rms[96];

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	sub = reg & 7;
	is_byte = (form == 0xF6);
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, is_byte);
	if (sub == 0 || sub == 1) {
		int ib = is_byte ? 1 : ((d->osz == 2) ? 2 : 4);
		int64_t imm;
		char ibuf[32];
		if (fetch_imm(d, ib, &imm) < 0)
			return -1;
		fmt_imm(ibuf, sizeof ibuf, imm, is_byte ? 1 : d->osz);
		snprintf(d->ops, sizeof d->ops, "%s, %s", ibuf, rms);
		set_mnem(d, "test");
		return 0;
	}
	snprintf(d->ops, sizeof d->ops, "%s", rms);
	set_mnem(d, grp3_name[sub]);
	return 0;
}

/* group4 (0xFE): inc/dec byte. */
static int
op_grp4(struct dec *d)
{
	int reg, is_mem, rm, sub;
	struct mem_op mem;
	char rms[96];

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	sub = reg & 7;
	if (sub >= 2)
		return -1;
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 1);
	snprintf(d->ops, sizeof d->ops, "%s", rms);
	set_mnem(d, grp4_name[sub]);
	return 0;
}

/* group5 (0xFF): inc/dec/call/jmp/push. ModR/M is decoded here once; indirect
 * call/jmp get a leading '*' marker. */
static int
op_grp5(struct dec *d)
{
	int reg, is_mem, rm, sub;
	struct mem_op mem;
	char rms[96];

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	sub = reg & 7;
	if (sub == 3 || sub == 5 || sub == 7)
		return -1; /* far call/jmp/invalid in 64-bit */
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
	if (sub == 2 || sub == 4)
		snprintf(d->ops, sizeof d->ops, "*%s", rms);
	else
		snprintf(d->ops, sizeof d->ops, "%s", rms);
	set_mnem(d, grp5_name[sub]);
	return 0;
}

/* group8 (0x0F 0xBA): bt/bts/btr/btc with imm8. */
static int
op_grp8(struct dec *d)
{
	int reg, is_mem, rm, sub;
	struct mem_op mem;
	char rms[96], ib[32];
	int64_t imm;

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	sub = reg & 7;
	if (sub < 4)
		return -1;
	if (fetch_imm(d, 1, &imm) < 0)
		return -1;
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
	fmt_imm(ib, sizeof ib, imm, 1);
	snprintf(d->ops, sizeof d->ops, "%s, %s", ib, rms);
	set_mnem(d, grp8_name[sub]);
	return 0;
}

/* group11 (0xC6/0xC7): mov r/m, imm. */
static int
op_grp11(struct dec *d, int form)
{
	int reg, is_mem, rm, is_byte, ib, imm_osz;
	struct mem_op mem;
	char rms[96], ibuf[32];
	int64_t imm;

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	if ((reg & 7) != 0)
		return -1;
	is_byte = (form == 0xC6);
	ib = is_byte ? 1 : ((d->osz == 2) ? 2 : 4);
	imm_osz = is_byte ? 1 : d->osz;
	if (fetch_imm(d, ib, &imm) < 0)
		return -1;
	fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, is_byte);
	fmt_imm(ibuf, sizeof ibuf, imm, imm_osz);
	snprintf(d->ops, sizeof d->ops, "%s, %s", ibuf, rms);
	set_mnem(d, "mov");
	return 0;
}

/* relative branch/call: target = addr + insn_length + disp. */
static int
op_rel(struct dec *d, const char *mnem, int bytes)
{
	int64_t disp;
	uint64_t target;

	if (bytes == 1) {
		if (fetch_disp8(d, &disp) < 0)
			return -1;
	} else {
		if (fetch_disp32(d, &disp) < 0)
			return -1;
	}
	target = d->addr + (d->pos - d->start) + (uint64_t)disp;
	snprintf(d->ops, sizeof d->ops, "0x%llx", (unsigned long long)target);
	set_mnem(d, mnem);
	return 0;
}

/* push/pop register (50-5F). dir: 0 push, 1 pop. */
static int
op_pushpop_reg(struct dec *d, int op, int dir)
{
	int reg = (op - (dir ? 0x58 : 0x50)) | ((d->rex & REX_B) ? 8 : 0);

	snprintf(d->ops, sizeof d->ops, "%%%s",
	         reg_name(reg, 8, 0));
	set_mnem(d, dir ? "pop" : "push");
	return 0;
}

/* movzx/movsx: src (r/m) is byte or word, dest (reg) is osz-sized. The two
 * operands have different widths, so a dedicated handler is needed. */
static int
op_movxz(struct dec *d, int is_signed, int src_is_byte)
{
	int reg, is_mem, rm, src_osz;
	struct mem_op mem;
	char rms[96], regs[16], mnem[16];
	const char *sc, *dc;

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	src_osz = src_is_byte ? 1 : 2;
	if (is_mem) {
		struct sbuf o = {rms, sizeof rms, 0};
		rms[0] = '\0';
		fmt_mem(d, &o, &mem);
	} else {
		snprintf(rms, sizeof rms, "%%%s",
		         reg_name(rm, src_osz, d->rex != 0));
	}
	snprintf(regs, sizeof regs, "%%%s", reg_name(reg, d->osz, 0));
	sc = src_is_byte ? "b" : "w";
	dc = (d->osz == 8) ? "q" : (d->osz == 2 ? "w" : "l");
	snprintf(mnem, sizeof mnem, "mov%s%s%s", is_signed ? "s" : "z", sc, dc);
	snprintf(d->ops, sizeof d->ops, "%s, %s", rms, regs);
	set_mnem(d, mnem);
	return 0;
}

/* movsxd (0x63): r/m32 -> r64 (or r32). Source is 32-bit, dest is osz. */
static int
op_movsxd(struct dec *d)
{
	int reg, is_mem, rm;
	struct mem_op mem;
	char rms[96], regs[16];

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	if (is_mem) {
		struct sbuf o = {rms, sizeof rms, 0};
		rms[0] = '\0';
		fmt_mem(d, &o, &mem);
	} else {
		snprintf(rms, sizeof rms, "%%%s", reg_name(rm, 4, d->rex != 0));
	}
	snprintf(regs, sizeof regs, "%%%s", reg_name(reg, d->osz, 0));
	snprintf(d->ops, sizeof d->ops, "%s, %s", rms, regs);
	set_mnem(d, (d->osz == 8) ? "movslq" : "movsxd");
	return 0;
}

/* mov r8, imm8 / mov r, imm (B0-BF). */
static int
op_mov_imm_reg(struct dec *d, int op)
{
	int reg = (op >= 0xB8 ? op - 0xB8 : op - 0xB0) |
	          ((d->rex & REX_B) ? 8 : 0);
	int is_byte = (op < 0xB8);
	int ib = is_byte ? 1 : d->osz;
	int64_t imm;
	char ibuf[32];

	if (fetch_imm(d, ib, &imm) < 0)
		return -1;
	fmt_imm(ibuf, sizeof ibuf, imm, is_byte ? 1 : d->osz);
	snprintf(d->ops, sizeof d->ops, "%s, %%%s", ibuf,
	         reg_name(reg, is_byte ? 1 : d->osz, d->rex != 0));
	/* mov r64, imm64 uses the "movabs" mnemonic in ATT syntax. */
	set_mnem(d, (!is_byte && d->osz == 8) ? "movabs" : "mov");
	return 0;
}

/* no-operand instruction. */
static int
op_none(struct dec *d, const char *mnem)
{
	d->ops[0] = '\0';
	set_mnem(d, mnem);
	return 0;
}

/* x87 FPU (D8-DF). Decodes ModR/M for correct length and produces the common
 * st(i) / memory mnemonics. Exotic forms fall back to "(bad)" but keep the
 * length correct so linear sweep stays in sync. */
static const char *const x87_mem_op[8][8] = {
	/* D8 */ {"fadd","fmul","fcom","fcomp","fsub","fsubr","fdiv","fdivr"},
	/* D9 */ {"fld",  NULL,  "fst",  "fstp", "fldenv","fldcw",NULL,  "fnstcw"},
	/* DA */ {"fiadd","fimul","ficom","ficomp","fisub","fisubr","fidiv","fidivr"},
	/* DB */ {"fild", NULL,  "fist", "fistp","fldenv","fldcw",NULL,  "fnstcw"},
	/* DC */ {"fadd","fmul","fcom3","fcomp3","fsub","fsubr","fdiv","fdivr"},
	/* DD */ {"fld",  NULL,  "fst",  "fstp", "frstor",NULL,  NULL,  "fnsave"},
	/* DE */ {"fiadd","fimul","ficom","ficomp","fisub","fisubr","fidiv","fidivr"},
	/* DF */ {"fild", NULL,  "fist", "fistp","fbstp","fldenv",NULL,  "fnstsw"},
};

static int
decode_x87(struct dec *d, int op)
{
	int reg, is_mem, rm, sub;
	struct mem_op mem;

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	sub = reg & 7;

	if (!is_mem) {
		/* register form: operate on st(i); rm field selects st(i) */
		char sti[16];
		const char *m = NULL;
		int i = rm & 7;

		snprintf(sti, sizeof sti, "%%st(%d)", i);
		if (op == 0xD9) {
			static const char *const t[8][8] = {
				{NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
				{NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
				{"fnop",NULL,NULL,NULL,NULL,NULL,NULL,NULL},
				{NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
				{"fchs","fabs",NULL,NULL,"ftst","fxam",NULL,NULL},
				{"fld1","fldl2t","fldl2e","fldpi","fldlg2","fldln2","fldz",NULL},
				{"f2xm1","fyl2x","fptan","fpatan","fxtract","fprem1",
				 "fdecstp","fincstp"},
				{"fprem","fyl2xp1","fsqrt","fsin","fcos","fscale",
				 "frndint","fsincos"},
			};
			m = t[sub][i];
		} else if (op == 0xDA) {
			static const char *const t[8] = {
				NULL,NULL,"fcmovb","fcmove","fcmovbe","fcmovu",NULL,NULL
			};
			m = t[sub];
		} else if (op == 0xDB) {
			if (sub == 4 && i == 0) m = "fninit";
			else if (sub == 5) {
				static const char *const t[8] = {
					"fucomi",NULL,NULL,NULL,NULL,NULL,NULL,NULL
				};
				m = t[i];
			}
		} else if (op == 0xDF) {
			if (sub == 4 && i == 0) m = "fnstsw";
			else if (sub == 5 && i == 0) m = "fucomip";
		} else if (op == 0xDD) {
			/* DD register forms: fld/fxch/fstp/fucom st(i) */
			if (sub == 0)
				m = "fld";
			else if (sub == 1)
				m = "fxch";
			else if (sub == 2)
				m = "fst";
			else if (sub == 3)
				m = "fstp";
			else if (sub == 4 && i == 0)
				m = "fnop";
			else if (sub == 5)
				m = "fucom";
			else if (sub == 6)
				m = "fucomp";
			if (m && sub <= 3) {
				snprintf(d->ops, sizeof d->ops, "%%st(%d)", i);
				set_mnem(d, m);
				return 0;
			}
			if (m && sub >= 4) {
				snprintf(d->ops, sizeof d->ops, "%%st(%d)", i);
				set_mnem(d, m);
				return 0;
			}
			m = NULL;
		} else if (op == 0xDE) {
			/* faddp/fmulp/fcomp/fsubp/fsubrp/fdivp/fdivrp st(i), st(0) */
			if (sub <= 7 && i == 1 && sub != 2) {
				static const char *const t[8] = {
					"faddp",NULL,"fcomp","fcompp","fsubp","fsubrp","fdivp","fdivrp"
				};
				m = t[sub];
				if (m) {
					snprintf(d->ops, sizeof d->ops, "%%st(1)");
					set_mnem(d, m);
					return 0;
				}
			}
		}
		if (m) {
			if (sub == 7 && i == 0 && op == 0xDE) {
				/* fdivrp */
				snprintf(d->ops, sizeof d->ops, "%%st(1)");
				set_mnem(d, "fdivrp");
				return 0;
			}
			if (op == 0xDF && sub == 4) {
				/* fnstsw %ax */
				snprintf(d->ops, sizeof d->ops, "%%ax");
				set_mnem(d, m);
				return 0;
			}
			d->ops[0] = '\0';
			set_mnem(d, m);
			return 0;
		}
		/* register arithmetic forms for D8/DC: fadd st(0),st(i) etc */
		if (op == 0xD8 || op == 0xDC) {
			static const char *const t[8] = {
				"fadd","fmul","fcom","fcomp","fsub","fsubr","fdiv","fdivr"
			};
			/* DC reverses fsub/fsubr and fdiv/fdivr direction */
			const char *mn = t[sub];
			if (op == 0xDC) {
				if (sub == 4) mn = "fadd";
				else if (sub == 5) mn = "fmul";
				else if (sub == 6) mn = "fdiv";
				else if (sub == 7) mn = "fdivr";
				else if (sub == 0) mn = "fadd";
				snprintf(d->ops, sizeof d->ops, "%%st(%d)", i);
				set_mnem(d, mn);
				return 0;
			}
			snprintf(d->ops, sizeof d->ops, "%%st(%d)", i);
			set_mnem(d, mn);
			return 0;
		}
		/* unhandled register form */
		d->ops[0] = '\0';
		set_mnem(d, "(bad)");
		return -1;
	}

	/* memory form */
	{
		const char *m = x87_mem_op[op - 0xD8][sub];
		char rms[96];
		if (m == NULL) {
			d->ops[0] = '\0';
			set_mnem(d, "(bad)");
			return -1;
		}
		fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
		snprintf(d->ops, sizeof d->ops, "%s", rms);
		set_mnem(d, m);
		return 0;
	}
}

/* ------------------------------------------------------------------ */
/* SSE helpers                                                          */
/* ------------------------------------------------------------------ */

/* SSE reg/mem + xmm. dir=0 -> "rm, reg" (reg is dest), dir=1 -> "reg, rm". */
static int
op_xmm_rm(struct dec *d, const char *mnem, int dir)
{
	int reg, is_mem, rm;
	struct mem_op mem;
	char rms[96], regs[16];

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	if (is_mem) {
		struct sbuf o = {rms, sizeof rms, 0};
		rms[0] = '\0';
		fmt_mem(d, &o, &mem);
	} else {
		fmt_xmm(rms, sizeof rms, rm);
	}
	fmt_xmm(regs, sizeof regs, reg);
	if (dir == 0)
		snprintf(d->ops, sizeof d->ops, "%s, %s", rms, regs);
	else
		snprintf(d->ops, sizeof d->ops, "%s, %s", regs, rms);
	set_mnem(d, mnem);
	return 0;
}

/* movd/movq between xmm and GPR/memory. */
static int
op_movd(struct dec *d, int dir, int is_q)
{
	int reg, is_mem, rm;
	struct mem_op mem;
	char rms[96], regs[16];
	int gpr_osz = is_q ? 8 : 4;

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	if (is_mem) {
		struct sbuf o = {rms, sizeof rms, 0};
		rms[0] = '\0';
		fmt_mem(d, &o, &mem);
	} else {
		snprintf(rms, sizeof rms, "%%%s",
		         reg_name(rm, gpr_osz, d->rex != 0));
	}
	fmt_xmm(regs, sizeof regs, reg);
	if (dir == 0)
		snprintf(d->ops, sizeof d->ops, "%s, %s", rms, regs);
	else
		snprintf(d->ops, sizeof d->ops, "%s, %s", regs, rms);
	set_mnem(d, is_q ? "movq" : "movd");
	return 0;
}

/* Determine the SSE pack suffix from mandatory prefix:
 * none -> "ps", 66 -> "pd", F3 -> "ss", F2 -> "sd". */
static const char *
sse_pack_suffix(struct dec *d)
{
	if (d->repne)
		return "sd";
	if (d->rep)
		return "ss";
	if (d->osize_pfx)
		return "pd";
	return "ps";
}

static int
op_sse_arith(struct dec *d, const char *base)
{
	char mnem[16];

	snprintf(mnem, sizeof mnem, "%s%s", base, sse_pack_suffix(d));
	return op_xmm_rm(d, mnem, 0);
}

/* SSE with ps/pd-only suffix (no scalar ss/sd variant): andps/andpd etc. */
static int
op_sse_psd(struct dec *d, const char *base)
{
	char mnem[16];

	snprintf(mnem, sizeof mnem, "%s%s", base, d->osize_pfx ? "pd" : "ps");
	return op_xmm_rm(d, mnem, 0);
}

/* SSE r/m, reg with an imm8 third operand (cmpps $imm, rm, reg). */
static int
op_sse_imm8(struct dec *d, const char *base)
{
	int reg, is_mem, rm;
	struct mem_op mem;
	char rms[96], regs[16], mnem[16], ib[32];
	int64_t imm;

	if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
		return -1;
	if (fetch_imm(d, 1, &imm) < 0)
		return -1;
	if (is_mem) {
		struct sbuf o = {rms, sizeof rms, 0};
		rms[0] = '\0';
		fmt_mem(d, &o, &mem);
	} else {
		fmt_xmm(rms, sizeof rms, rm);
	}
	fmt_xmm(regs, sizeof regs, reg);
	fmt_imm(ib, sizeof ib, imm, 1);
	snprintf(mnem, sizeof mnem, "%s%s", base, sse_pack_suffix(d));
	snprintf(d->ops, sizeof d->ops, "%s, %s, %s", ib, rms, regs);
	set_mnem(d, mnem);
	return 0;
}

/* MMX/SSE2 integer op on mm/xmm with reg, r/m operands (ATT: rm, reg). */
static int
op_mmx(struct dec *d, const char *mnem)
{
	return op_xmm_rm(d, mnem, 0);
}

/* ------------------------------------------------------------------ */
/* 2-byte (0F xx) dispatch                                             */
/* ------------------------------------------------------------------ */

static int
decode_2byte(struct dec *d)
{
	int op, reg, is_mem, rm, sub;
	struct mem_op mem;

	if (fetch8(d, &op) < 0)
		return -1;

	/* 0F 00 group6 (system: sldt/str/lldt/ltr/verr/verw) */
	if (op == 0x00) {
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		sub = reg & 7;
		{
			const char *n[] = {"sldt", "str", "lldt", "ltr",
			                   "verr", "verw", "(bad)", "(bad)"};
			char rms[96];
			fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
			snprintf(d->ops, sizeof d->ops, "%s", rms);
			set_mnem(d, n[sub]);
			return strcmp(n[sub], "(bad)") ? 0 : -1;
		}
	}
	/* 0F 01 group7 (various system; decode length via ModR/M) */
	if (op == 0x01) {
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		{
			const char *n[] = {"sgdt", "sidt", "lgdt", "lidt",
			                   "smsw", "(bad)", "lmsw", "invlpg"};
			set_mnem(d, n[reg & 7]);
			return 0;
		}
	}
	if (op == 0x02)
		return op_rm(d, "lar", 0);
	if (op == 0x03)
		return op_rm(d, "lsl", 0);
	if (op == 0x05)
		return op_none(d, "syscall");
	if (op == 0x06)
		return op_none(d, "clts");
	if (op == 0x07)
		return op_none(d, "sysret");
	if (op == 0x08)
		return op_none(d, "invd");
	if (op == 0x09)
		return op_none(d, "wbinvd");
	if (op == 0x0B)
		return op_none(d, "ud2");

	/* 0F 10/11 movups/movss/movsd/movupd */
	if (op == 0x10 || op == 0x11) {
		const char *m;
		if (d->repne)
			m = "movsd";
		else if (d->rep)
			m = "movss";
		else if (d->osize_pfx)
			m = (op == 0x10) ? "movupd" : "movupd";
		else
			m = "movups";
		return op_xmm_rm(d, m, op == 0x10 ? 0 : 1);
	}
	/* 0F 12/13 movlps/movhlps/movlpd ... */
	if (op == 0x12 || op == 0x13) {
		const char *m = d->osize_pfx ? "movlpd" : "movlps";
		return op_xmm_rm(d, m, op == 0x12 ? 0 : 1);
	}
	if (op == 0x14 || op == 0x15) {
		const char *m = d->osize_pfx ? "movhpd" : "movhps";
		return op_xmm_rm(d, m, op == 0x14 ? 0 : 1);
	}
	if (op == 0x16 || op == 0x17) {
		const char *m = d->osize_pfx ? "movhpd" : "movhps";
		return op_xmm_rm(d, m, op == 0x16 ? 0 : 1);
	}

	/* 0F 18-1F prefetch hints / multi-byte nops */
	if (op >= 0x18 && op <= 0x1F) {
		const char *suf;

		suf = (d->osz == 2) ? "w" : (d->osz == 8 ? "q" : "l");
		if (op == 0x18) {
			if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
				return -1;
			{
				const char *n[] = {"prefetchnta", "prefetcht0",
				                   "prefetcht1", "prefetcht2",
				                   "nop", "nop", "nop", "nop"};
				char rms[96];
				fmt_rm_operand(d, rms, sizeof rms, is_mem, rm,
				               &mem, 0);
				snprintf(d->ops, sizeof d->ops, "%s", rms);
				set_mnem(d, n[reg & 7]);
				return 0;
			}
		}
		/* 0F 1E: endbr64/endbr32 when F3 prefix + mod=11 reg=7 */
		if (op == 0x1E && d->rep) {
			if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
				return -1;
			if (is_mem == 0 && (reg & 7) == 7) {
				if (rm == 2)
					return op_none(d, "endbr64");
				if (rm == 3)
					return op_none(d, "endbr32");
			}
			{
				char mnem[16], rms[96];
				fmt_rm_operand(d, rms, sizeof rms, is_mem, rm,
				               &mem, 0);
				snprintf(d->ops, sizeof d->ops, "%s", rms);
				snprintf(mnem, sizeof mnem, "nop%s", suf);
				set_mnem(d, mnem);
				return 0;
			}
		}
		/* 0F 1F canonical multi-byte nop; 0F 19-1E reserved nops. */
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		{
			char mnem[16], rms[96];
			fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem,
			               (op == 0x1F && d->osz == 1) ? 1 : 0);
			snprintf(d->ops, sizeof d->ops, "%s", rms);
			snprintf(mnem, sizeof mnem, "nop%s", suf);
			set_mnem(d, mnem);
			return 0;
		}
	}

	/* 0F 28/29 movaps/movapd */
	if (op == 0x28 || op == 0x29) {
		const char *m = d->osize_pfx ? "movapd" : "movaps";
		return op_xmm_rm(d, m, op == 0x28 ? 0 : 1);
	}
	if (op == 0x2E || op == 0x2F) {
		const char *m;
		if (op == 0x2E)
			m = d->osize_pfx ? "ucomisd" : "ucomiss";
		else
			m = d->osize_pfx ? "comisd" : "comiss";
		return op_xmm_rm(d, m, 0);
	}

	if (op == 0x31)
		return op_none(d, "rdtsc");
	if (op == 0x32)
		return op_none(d, "rdmsr");
	if (op == 0x30)
		return op_none(d, "wrmsr");
	if (op == 0x33)
		return op_none(d, "rdpmc");
	if (op == 0x34)
		return op_none(d, "sysenter");
	if (op == 0x35)
		return op_none(d, "sysexit");
	if (op == 0xA2)
		return op_none(d, "cpuid");

	/* 0F 40-4F cmovcc Ev, Gv (ATT: rm, reg) */
	if (op >= 0x40 && op <= 0x4F) {
		char m[16];
		snprintf(m, sizeof m, "cmov%s", cc_name[op - 0x40]);
		return op_mr(d, m, 0, 0);
	}

	/* 0F 50-5F SSE moves / arithmetic / conversions */
	if (op == 0x50) {
		/* movmskps/movmskpd: reg, xmm (ATT: xmm, reg) */
		int reg, is_mem, rm;
		struct mem_op mem;
		char regs[16], rms[16];
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		if (is_mem)
			return -1;
		fmt_xmm(rms, sizeof rms, rm);
		snprintf(regs, sizeof regs, "%%%s", reg_name(reg, d->osz, 0));
		snprintf(d->ops, sizeof d->ops, "%s, %s", rms, regs);
		set_mnem(d, d->osize_pfx ? "movmskpd" : "movmskps");
		return 0;
	}
	if (op == 0x51)
		return op_sse_arith(d, "sqrt");
	if (op == 0x52)
		return op_sse_arith(d, "rsqrt");
	if (op == 0x53)
		return op_sse_arith(d, "rcp");
	if (op == 0x54)
		return op_sse_psd(d, "and");
	if (op == 0x55)
		return op_sse_psd(d, "andn");
	if (op == 0x56)
		return op_sse_psd(d, "or");
	if (op == 0x57)
		return op_sse_psd(d, "xor");
	if (op == 0x5A || op == 0x5B) {
		/* cvt* conversions; keep length correct with a generic name */
		const char *m;
		if (op == 0x5A)
			m = d->repne ? "cvtsd2si" : (d->rep ? "cvtss2si" :
			    (d->osize_pfx ? "cvtpd2pi" : "cvtps2pi"));
		else
			m = d->rep ? "cvttps2dq" : (d->osize_pfx ? "cvtdq2pd" :
			    "cvtdq2ps");
		return op_xmm_rm(d, m, 0);
	}
	if (op == 0x58)
		return op_sse_arith(d, "add");
	if (op == 0x59)
		return op_sse_arith(d, "mul");
	if (op == 0x5C)
		return op_sse_arith(d, "sub");
	if (op == 0x5D)
		return op_sse_arith(d, "min");
	if (op == 0x5E)
		return op_sse_arith(d, "div");
	if (op == 0x5F)
		return op_sse_arith(d, "max");

	if (op == 0x6E) {
		/* movd/movq xmm, r/m (66 -> movd, REX.W -> movq) */
		return op_movd(d, 0, (d->rex & REX_W) != 0);
	}
	if (op == 0x7E) {
		if (d->rep)
			return op_movd(d, 0, 1); /* F3 0F 7E movq xmm, xmm/m64 */
		return op_movd(d, 1, (d->rex & REX_W) != 0); /* movd r/m, xmm */
	}
	if (op == 0xD6) {
		/* 66 0F D6 movq xmm/m64, xmm */
		return op_movd(d, 1, 1);
	}
	if (op == 0x6F) {
		/* movdqa (66) / movdqu (F3) */
		const char *m = d->rep ? "movdqu" : "movdqa";
		return op_xmm_rm(d, m, 0);
	}
	if (op == 0x7F) {
		const char *m = d->rep ? "movdqu" : "movdqa";
		return op_xmm_rm(d, m, 1);
	}
	if (op == 0xD4)
		return op_xmm_rm(d, "paddq", 0);
	if (op == 0xFB)
		return op_xmm_rm(d, "psubq", 0);

	/* SSE2/MMX integer ops (66 prefix => xmm, otherwise mm). We use xmm
	 * register names for the register form; the mnemonic matches objdump. */
	if (op == 0x60)
		return op_mmx(d, "punpcklbw");
	if (op == 0x61)
		return op_mmx(d, "punpcklwd");
	if (op == 0x62)
		return op_mmx(d, "punpckldq");
	if (op == 0x63)
		return op_mmx(d, "packsswb");
	if (op == 0x64)
		return op_mmx(d, "pcmpgtb");
	if (op == 0x65)
		return op_mmx(d, "pcmpgtw");
	if (op == 0x66)
		return op_mmx(d, "pcmpgtd");
	if (op == 0x67)
		return op_mmx(d, "packuswb");
	if (op == 0x68)
		return op_mmx(d, "punpckhbw");
	if (op == 0x69)
		return op_mmx(d, "punpckhwd");
	if (op == 0x6A)
		return op_mmx(d, "punpckhdq");
	if (op == 0x6B)
		return op_mmx(d, "packssdw");
	if (op == 0x6C)
		return op_mmx(d, "punpcklqdq");
	if (op == 0x6D)
		return op_mmx(d, "punpckhqdq");
	if (op == 0x74)
		return op_mmx(d, "pcmpeqb");
	if (op == 0x75)
		return op_mmx(d, "pcmpeqw");
	if (op == 0x76)
		return op_mmx(d, "pcmpeqd");
	if (op == 0xEF)
		return op_mmx(d, "pxor");
	if (op == 0xEB)
		return op_mmx(d, "por");
	if (op == 0xDB)
		return op_mmx(d, "pand");
	if (op == 0xDF)
		return op_mmx(d, "pandn");
	if (op == 0xEC)
		return op_mmx(d, "paddsb");
	if (op == 0xED)
		return op_mmx(d, "paddsw");
	if (op == 0xE0)
		return op_mmx(d, "pavgb");
	if (op == 0xE3)
		return op_mmx(d, "pavgw");
	if (op == 0xE8)
		return op_mmx(d, "psubsb");
	if (op == 0xE9)
		return op_mmx(d, "psubsw");
	if (op == 0xD0)
		return op_mmx(d, "addsubps");
	if (op == 0xD1)
		return op_mmx(d, "psrlw");
	if (op == 0xD2)
		return op_mmx(d, "psrld");
	if (op == 0xD3)
		return op_mmx(d, "psrlq");
	if (op == 0xD5)
		return op_mmx(d, "pmullw");
	if (op == 0xD7)
		return op_mmx(d, "pmovmskb");
	if (op == 0xD8)
		return op_mmx(d, "psubusb");
	if (op == 0xD9)
		return op_mmx(d, "psubusw");
	if (op == 0xDA)
		return op_mmx(d, "pminub");
	if (op == 0xDC)
		return op_mmx(d, "paddusb");
	if (op == 0xDD)
		return op_mmx(d, "paddusw");
	if (op == 0xDE)
		return op_mmx(d, "pmaxub");
	if (op == 0xE1)
		return op_mmx(d, "psraw");
	if (op == 0xE2)
		return op_mmx(d, "psrad");
	if (op == 0xE4)
		return op_mmx(d, "pmulhuw");
	if (op == 0xE5)
		return op_mmx(d, "pmulhw");
	if (op == 0xEA)
		return op_mmx(d, "pminsw");
	if (op == 0xEE)
		return op_mmx(d, "pmaxsw");
	if (op == 0xF1)
		return op_mmx(d, "psllw");
	if (op == 0xF2)
		return op_mmx(d, "pslld");
	if (op == 0xF3)
		return op_mmx(d, "psllq");
	if (op == 0xF4)
		return op_mmx(d, "pmuludq");
	if (op == 0xF5)
		return op_mmx(d, "pmaddwd");
	if (op == 0xF6)
		return op_mmx(d, "psadbw");
	if (op == 0xF8)
		return op_mmx(d, "psubb");
	if (op == 0xF9)
		return op_mmx(d, "psubw");
	if (op == 0xFA)
		return op_mmx(d, "psubd");
	if (op == 0xFC)
		return op_mmx(d, "paddb");
	if (op == 0xFD)
		return op_mmx(d, "paddw");
	if (op == 0xFE)
		return op_mmx(d, "paddd");

	/* SSE with imm8 operand */
	if (op == 0xC2)
		return op_sse_imm8(d, "cmp");
	if (op == 0xC6)
		return op_sse_imm8(d, "shuf");
	/* 0F 70: pshuflw/pshufd/pshufhw/pshufw with imm8 (full mnemonic). */
	if (op == 0x70) {
		const char *m = d->rep ? "pshuflw" : (d->osize_pfx ? "pshufd" :
		               (d->repne ? "pshufhw" : "pshufw"));
		int reg, is_mem, rm;
		struct mem_op mem;
		char rms[96], regs[16], ib[32];
		int64_t imm;
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		if (fetch_imm(d, 1, &imm) < 0)
			return -1;
		if (is_mem) {
			struct sbuf o = {rms, sizeof rms, 0};
			rms[0] = '\0';
			fmt_mem(d, &o, &mem);
		} else {
			fmt_xmm(rms, sizeof rms, rm);
		}
		fmt_xmm(regs, sizeof regs, reg);
		fmt_imm(ib, sizeof ib, imm, 1);
		snprintf(d->ops, sizeof d->ops, "%s, %s, %s", ib, rms, regs);
		set_mnem(d, m);
		return 0;
	}

	/* 0F 80-8F jcc rel32 */
	if (op >= 0x80 && op <= 0x8F) {
		char m[16];
		snprintf(m, sizeof m, "j%s", cc_name[op - 0x80]);
		return op_rel(d, m, 4);
	}

	/* 0F 90-9F setcc Eb */
	if (op >= 0x90 && op <= 0x9F) {
		char m[16];
		snprintf(m, sizeof m, "set%s", cc_name[op - 0x90]);
		return op_rm(d, m, 1);
	}

	if (op == 0xA3)
		return op_mr(d, "bt", 0, 1);     /* bt Ev, Gv: ATT reg, rm */
	if (op == 0xAB)
		return op_mr(d, "bts", 0, 1);
	if (op == 0xB3)
		return op_mr(d, "btr", 0, 1);
	if (op == 0xBB)
		return op_mr(d, "btc", 0, 1);

	if (op == 0xA4) {
		/* shld Gv, Ev, Ib (ATT: $imm, rm, reg) */
		int64_t imm;
		char rms[96], regs[16], ib[32];
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		if (fetch_imm(d, 1, &imm) < 0)
			return -1;
		fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
		fmt_reg_operand(d, regs, sizeof regs, reg, 0);
		fmt_imm(ib, sizeof ib, imm, 1);
		snprintf(d->ops, sizeof d->ops, "%s, %s, %s", ib, rms, regs);
		set_mnem(d, "shld");
		return 0;
	}
	if (op == 0xA5) {
		/* shld Gv, Ev, CL */
		char rms[96], regs[16];
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
		fmt_reg_operand(d, regs, sizeof regs, reg, 0);
		snprintf(d->ops, sizeof d->ops, "%%cl, %s, %s", rms, regs);
		set_mnem(d, "shld");
		return 0;
	}
	if (op == 0xAC) {
		int64_t imm;
		char rms[96], regs[16], ib[32];
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		if (fetch_imm(d, 1, &imm) < 0)
			return -1;
		fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
		fmt_reg_operand(d, regs, sizeof regs, reg, 0);
		fmt_imm(ib, sizeof ib, imm, 1);
		snprintf(d->ops, sizeof d->ops, "%s, %s, %s", ib, rms, regs);
		set_mnem(d, "shrd");
		return 0;
	}
	if (op == 0xAD) {
		char rms[96], regs[16];
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
		fmt_reg_operand(d, regs, sizeof regs, reg, 0);
		snprintf(d->ops, sizeof d->ops, "%%cl, %s, %s", rms, regs);
		set_mnem(d, "shrd");
		return 0;
	}

	/* 0F AE group15: fxsave/fxrstor/ldmxcsr/stmxcsr + fences */
	if (op == 0xAE) {
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		if (is_mem == 0) {
			/* mod=11 register form: fences */
			if ((reg & 7) == 5 && rm == 0)
				return op_none(d, "lfence");
			if ((reg & 7) == 6 && rm == 0)
				return op_none(d, "mfence");
			if ((reg & 7) == 7 && rm == 0)
				return op_none(d, "sfence");
			if ((reg & 7) == 7 && rm == 1)
				return op_none(d, "clflush"); /* clflush r/m */
			return op_none(d, "(bad)");
		}
		{
			const char *n[] = {"fxsave", "fxrstor", "ldmxcsr",
			                  "stmxcsr", "(bad)", "lfence",
			                  "mfence", "clflush"};
			char rms[96];
			fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
			snprintf(d->ops, sizeof d->ops, "%s", rms);
			set_mnem(d, n[reg & 7]);
			return 0;
		}
	}

	/* 0F AF imul Gv, Ev (ATT: rm, reg) */
	if (op == 0xAF)
		return op_mr(d, "imul", 0, 0);

	if (op == 0xB0)
		return op_mr(d, "cmpxchg", 1, 0);
	if (op == 0xB1)
		return op_mr(d, "cmpxchg", 0, 0);
	if (op == 0xC0)
		return op_mr(d, "xadd", 1, 0);
	if (op == 0xC1)
		return op_mr(d, "xadd", 0, 0);

	if (op == 0xB6)
		return op_movxz(d, 0, 1);
	if (op == 0xB7)
		return op_movxz(d, 0, 0);
	if (op == 0xBE)
		return op_movxz(d, 1, 1);
	if (op == 0xBF)
		return op_movxz(d, 1, 0);

	/* 0F B8 popcnt (F3) / 0F BC bsf / 0F BD bsr */
	if (op == 0xB8) {
		if (d->rep)
			return op_rm(d, "popcnt", 0);
		return -1;
	}
	if (op == 0xBC) {
		if (d->rep)
			return op_rm(d, "tzcnt", 0);
		return op_rm(d, "bsf", 0);
	}
	if (op == 0xBD) {
		if (d->rep)
			return op_rm(d, "lzcnt", 0);
		return op_rm(d, "bsr", 0);
	}

	/* 0F BA group8 (bt/bts/btr/btc imm8) */
	if (op == 0xBA)
		return op_grp8(d);

	/* 0F C8-CF bswap r */
	if (op >= 0xC8 && op <= 0xCF) {
		int r = (op - 0xC8) | ((d->rex & REX_B) ? 8 : 0);
		snprintf(d->ops, sizeof d->ops, "%%%s",
		         reg_name(r, d->osz, 0));
		set_mnem(d, "bswap");
		return 0;
	}

	/* default: undecoded 2-byte -> (bad) but keep ModR/M length guess */
	return -1;
}

/* ------------------------------------------------------------------ */
/* VEX prefix (P2, minimal): decode prefix + ModR/M for length, emit   */
/* "(bad)". AVX is out of P0/P1 scope; this keeps the linear sweep in  */
/* sync by consuming the correct number of bytes.                       */
/* ------------------------------------------------------------------ */

static int
decode_vex(struct dec *d, int op_byte /* 0xC4 or 0xC5 */)
{
	int b1, b2 = 0, map, opcode;
	int saved_rex = d->rex;
	int saved_osz = d->osz;

	if (fetch8(d, &b1) < 0)
		return -1;
	if (op_byte == 0xC4) {
		if (fetch8(d, &b2) < 0)
			return -1;
		map = b1 & 0x1F;
		/* R/X/B inverted in b1; W in b2 */
		d->rex = 0;
		if (!(b1 & 0x80))
			d->rex |= REX_R;
		if (!(b1 & 0x40))
			d->rex |= REX_X;
		if (!(b1 & 0x20))
			d->rex |= REX_B;
		if (b2 & 0x80)
			d->rex |= REX_W;
		if (d->rex & REX_W)
			d->osz = 8;
		else if (d->osize_pfx)
			d->osz = 2;
		else
			d->osz = 4;
	} else {
		/* 2-byte VEX, map is 0F, X=B=0 (not set) */
		map = 1;
		d->rex = 0;
		if (!(b1 & 0x80))
			d->rex |= REX_R;
		if (d->osize_pfx)
			d->osz = 2;
		else
			d->osz = 4;
	}
	if (fetch8(d, &opcode) < 0)
		return -1;
	/* 0F3A map always carries an imm8; most AVX ops have ModR/M */
	{
		int reg, is_mem, rm;
		struct mem_op mem;
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) == 0) {
			if (map == 3) {
				int64_t imm;
				fetch_imm(d, 1, &imm);
			}
		}
	}
	(void)opcode;
	d->rex = saved_rex;
	d->osz = saved_osz;
	d->ops[0] = '\0';
	set_mnem(d, "(bad)");
	/* report success so length is preserved, but mnemonic is "(bad)" */
	return -1;
}

/* ------------------------------------------------------------------ */
/* String operations (A4-A7, AA-AF)                                     */
/* ------------------------------------------------------------------ */

static const char *
osz_suffix(struct dec *d, int is_byte)
{
	if (is_byte)
		return "b";
	if (d->osz == 2)
		return "w";
	if (d->osz == 8)
		return "q";
	return "d";
}

static int
op_string(struct dec *d, const char *base, int kind, int is_byte)
{
	/* kind: 0 movs, 1 cmps, 2 stos, 3 lods, 4 scas */
	char m[20];
	const char *sx = osz_suffix(d, is_byte);
	int repz = 0;

	if (d->repne)
		repz = 2;
	else if (d->rep)
		repz = 1;

	snprintf(m, sizeof m, "%s%s", base, sx);
	d->ops[0] = '\0';
	if (kind == 0 || kind == 1) {
		/* movs/cmps: (%rsi), (%rdi) with default %ds:/%es: */
		const char *seg1 = (d->seg >= 0) ? sreg_name[d->seg] : "ds";
		snprintf(d->ops, sizeof d->ops, "%%%s:(%%rsi), %%es:(%%rdi)",
		         seg1);
	} else if (kind == 2 || kind == 4) {
		/* stos/scas: (%rdi) */
		const char *seg = (d->seg >= 0) ? sreg_name[d->seg] : "es";
		snprintf(d->ops, sizeof d->ops, "%%%s:(%%rdi)", seg);
	} else {
		/* lods: (%rsi) */
		const char *seg = (d->seg >= 0) ? sreg_name[d->seg] : "ds";
		snprintf(d->ops, sizeof d->ops, "%%%s:(%%rsi)", seg);
	}
	if (repz == 1) {
		if (kind == 1 || kind == 4)
			snprintf(m, sizeof m, "repz %s%s", base, sx);
		else
			snprintf(m, sizeof m, "rep %s%s", base, sx);
	} else if (repz == 2) {
		snprintf(m, sizeof m, "repnz %s%s", base, sx);
	}
	set_mnem(d, m);
	return 0;
}

/* ------------------------------------------------------------------ */
/* 1-byte opcode dispatch                                              */
/* ------------------------------------------------------------------ */

static int
decode_1byte(struct dec *d, int op)
{
	/* arithmetic/logic group 0x00-0x3D (forms 0-5) */
	if (op <= 0x3D && (op & 7) <= 5 && op != 0x0F) {
		int idx = (op >> 3) & 7;
		int form = op & 7;
		const char *m = grp1_name[idx];

		if (form == 0)
			return op_mr(d, m, 1, 1); /* Eb, Gb -> reg, rm */
		if (form == 1)
			return op_mr(d, m, 0, 1); /* Ev, Gv -> reg, rm */
		if (form == 2)
			return op_mr(d, m, 1, 0); /* Gb, Eb -> rm, reg */
		if (form == 3)
			return op_mr(d, m, 0, 0); /* Gv, Ev -> rm, reg */
		if (form == 4)
			return op_al_imm(d, m);    /* AL, Ib */
		if (form == 5)
			return op_ax_imm(d, m);    /* eAX, Iz */
	}

	switch (op) {
	/* push/pop register */
	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57:
		return op_pushpop_reg(d, op, 0);
	case 0x58: case 0x59: case 0x5A: case 0x5B:
	case 0x5C: case 0x5D: case 0x5E: case 0x5F:
		return op_pushpop_reg(d, op, 1);

	/* push imm */
	case 0x68: {
		int64_t imm;
		char ib[32];
		int ib_bytes = (d->osz == 2) ? 2 : 4;
		if (fetch_imm(d, ib_bytes, &imm) < 0)
			return -1;
		fmt_imm(ib, sizeof ib, imm, d->osz);
		snprintf(d->ops, sizeof d->ops, "%s", ib);
		set_mnem(d, "push");
		return 0;
	}
	case 0x6A: {
		int64_t imm;
		char ib[32];
		if (fetch_imm(d, 1, &imm) < 0)
			return -1;
		fmt_imm(ib, sizeof ib, imm, d->osz);
		snprintf(d->ops, sizeof d->ops, "%s", ib);
		set_mnem(d, "push");
		return 0;
	}
	case 0x69: {
		/* imul Gv, Ev, Iz */
		int reg, is_mem, rm, ib_bytes;
		struct mem_op mem;
		char rms[96], regs[16], ib[32];
		int64_t imm;
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		ib_bytes = (d->osz == 2) ? 2 : 4;
		if (fetch_imm(d, ib_bytes, &imm) < 0)
			return -1;
		fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
		fmt_reg_operand(d, regs, sizeof regs, reg, 0);
		fmt_imm(ib, sizeof ib, imm, d->osz);
		snprintf(d->ops, sizeof d->ops, "%s, %s, %s", ib, rms, regs);
		set_mnem(d, "imul");
		return 0;
	}
	case 0x6B: {
		int reg, is_mem, rm;
		struct mem_op mem;
		char rms[96], regs[16], ib[32];
		int64_t imm;
		if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
			return -1;
		if (fetch_imm(d, 1, &imm) < 0)
			return -1;
		fmt_rm_operand(d, rms, sizeof rms, is_mem, rm, &mem, 0);
		fmt_reg_operand(d, regs, sizeof regs, reg, 0);
		fmt_imm(ib, sizeof ib, imm, d->osz);
		snprintf(d->ops, sizeof d->ops, "%s, %s, %s", ib, rms, regs);
		set_mnem(d, "imul");
		return 0;
	}

	/* jcc rel8 */
	case 0x70: case 0x71: case 0x72: case 0x73:
	case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7A: case 0x7B:
	case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
		char m[16];
		snprintf(m, sizeof m, "j%s", cc_name[op - 0x70]);
		return op_rel(d, m, 1);
	}

	/* group1 r/m, imm */
	case 0x80:
		return op_grp1(d, 0x80);
	case 0x81:
		return op_grp1(d, 0x81);
	case 0x83:
		return op_grp1(d, 0x83);

	/* test / xchg */
	case 0x84:
		return op_mr(d, "test", 1, 1);
	case 0x85:
		return op_mr(d, "test", 0, 1);
	case 0x86:
		return op_mr(d, "xchg", 1, 1);
	case 0x87:
		return op_mr(d, "xchg", 0, 1);

	/* mov */
	case 0x88:
		return op_mr(d, "mov", 1, 1);
	case 0x89:
		return op_mr(d, "mov", 0, 1);
	case 0x8A:
		return op_mr(d, "mov", 1, 0);
	case 0x8B:
		return op_mr(d, "mov", 0, 0);

	case 0x8D:
		return op_lea(d, "lea");

	case 0x90: case 0x91: case 0x92: case 0x93:
	case 0x94: case 0x95: case 0x96: case 0x97: {
		/* xchg eAX, r (ATT: "%eax, %r"); 0x90 is nop / pause. */
		int r = (op - 0x90) | ((d->rex & REX_B) ? 8 : 0);
		if (op == 0x90 && d->rep)
			return op_none(d, "pause");
		if (op == 0x90 && d->rex == 0 && !d->osize_pfx)
			return op_none(d, "nop");
		/* 66 90 displays as xchg %ax,%ax (objdump), REX forms as xchg. */
		snprintf(d->ops, sizeof d->ops, "%%%s, %%%s",
		         reg_name(0, d->osz, 0), reg_name(r, d->osz, 0));
		set_mnem(d, "xchg");
		return 0;
	}

	case 0x98: {
		const char *m = (d->osz == 8) ? "cdqe" :
		                (d->osz == 2 ? "cbw" : "cwde");
		return op_none(d, m);
	}
	case 0x99: {
		const char *m = (d->osz == 8) ? "cqo" :
		                (d->osz == 2 ? "cwd" : "cdq");
		return op_none(d, m);
	}
	case 0x9B:
		return op_none(d, "wait");
	case 0x9C: {
		const char *m = (d->osz == 2) ? "pushfw" : "pushfq";
		return op_none(d, m);
	}
	case 0x9D: {
		const char *m = (d->osz == 2) ? "popfw" : "popfq";
		return op_none(d, m);
	}
	case 0x9E:
		return op_none(d, "sahf");
	case 0x9F:
		return op_none(d, "lahf");

	/* mov moffs <-> AL/eAX */
	case 0xA0: case 0xA1: case 0xA2: case 0xA3: {
		uint64_t moffs;
		int is_byte = (op == 0xA0 || op == 0xA2);
		int addr_bytes = (d->addr_sz == 4) ? 4 : 8;
		int64_t raw;
		char addr[32];
		if (fetch_imm(d, addr_bytes, &raw) < 0)
			return -1;
		moffs = (uint64_t)raw;
		snprintf(addr, sizeof addr, "0x%llx", (unsigned long long)moffs);
		if (op == 0xA0 || op == 0xA1) {
			/* mov addr, reg (addr is source) */
			snprintf(d->ops, sizeof d->ops, "%s, %%%s", addr,
			         reg_name(0, is_byte ? 1 : d->osz, 0));
		} else {
			snprintf(d->ops, sizeof d->ops, "%%%s, %s",
			         reg_name(0, is_byte ? 1 : d->osz, 0), addr);
		}
		set_mnem(d, "mov");
		return 0;
	}

	case 0xA4:
		return op_string(d, "movs", 0, 1);
	case 0xA5:
		return op_string(d, "movs", 0, 0);
	case 0xA6:
		return op_string(d, "cmps", 1, 1);
	case 0xA7:
		return op_string(d, "cmps", 1, 0);
	case 0xA8:
		return op_al_imm(d, "test");
	case 0xA9:
		return op_ax_imm(d, "test");
	case 0xAA:
		return op_string(d, "stos", 2, 1);
	case 0xAB:
		return op_string(d, "stos", 2, 0);
	case 0xAC:
		return op_string(d, "lods", 3, 1);
	case 0xAD:
		return op_string(d, "lods", 3, 0);
	case 0xAE:
		return op_string(d, "scas", 4, 1);
	case 0xAF:
		return op_string(d, "scas", 4, 0);

	/* mov r8/r, imm */
	case 0xB0: case 0xB1: case 0xB2: case 0xB3:
	case 0xB4: case 0xB5: case 0xB6: case 0xB7:
	case 0xB8: case 0xB9: case 0xBA: case 0xBB:
	case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		return op_mov_imm_reg(d, op);

	/* shifts */
	case 0xC0:
		return op_grp2(d, 0xC0);
	case 0xC1:
		return op_grp2(d, 0xC1);

	case 0xC2: {
		/* ret imm16 */
		int imm16;
		if (fetch16(d, &imm16) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "$0x%x",
		         (unsigned)(unsigned short)imm16);
		set_mnem(d, "ret");
		return 0;
	}
	case 0xC3:
		return op_none(d, "ret");

	case 0xC4:
	case 0xC5:
		return decode_vex(d, op);

	case 0xC6:
		return op_grp11(d, 0xC6);
	case 0xC7:
		return op_grp11(d, 0xC7);

	case 0xC8: {
		/* enter imm16, imm8 */
		int imm16, imm8;
		if (fetch16(d, &imm16) < 0)
			return -1;
		if (fetch8(d, &imm8) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "$0x%x, $0x%x",
		         (unsigned)(unsigned short)imm16,
		         (unsigned)(unsigned char)imm8);
		set_mnem(d, "enter");
		return 0;
	}
	case 0xC9:
		return op_none(d, "leave");
	case 0xCA: {
		int imm16;
		if (fetch16(d, &imm16) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "$0x%x",
		         (unsigned)(unsigned short)imm16);
		set_mnem(d, "retf");
		return 0;
	}
	case 0xCB:
		return op_none(d, "retf");
	case 0xCC:
		return op_none(d, "int3");
	case 0xCD: {
		int v;
		if (fetch8(d, &v) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "$0x%x",
		         (unsigned)(unsigned char)v);
		set_mnem(d, "int");
		return 0;
	}
	case 0xCE:
		return op_none(d, "into");
	case 0xCF: {
		const char *m = (d->osz == 8) ? "iretq" :
		                (d->osz == 2 ? "iretw" : "iret");
		return op_none(d, m);
	}

	/* shifts by 1 / CL */
	case 0xD0:
		return op_grp2(d, 0xD0);
	case 0xD1:
		return op_grp2(d, 0xD1);
	case 0xD2:
		return op_grp2(d, 0xD2);
	case 0xD3:
		return op_grp2(d, 0xD3);

	case 0xD7:
		return op_none(d, "xlat");

	/* x87 FPU D8-DF */
	case 0xD8: case 0xD9: case 0xDA: case 0xDB:
	case 0xDC: case 0xDD: case 0xDE: case 0xDF:
		return decode_x87(d, op);

	/* loop / jecxz */
	case 0xE0: {
		int64_t disp;
		uint64_t t;
		if (fetch_disp8(d, &disp) < 0)
			return -1;
		t = d->addr + (d->pos - d->start) + (uint64_t)disp;
		snprintf(d->ops, sizeof d->ops, "0x%llx", (unsigned long long)t);
		set_mnem(d, "loopne");
		return 0;
	}
	case 0xE1: {
		int64_t disp;
		uint64_t t;
		if (fetch_disp8(d, &disp) < 0)
			return -1;
		t = d->addr + (d->pos - d->start) + (uint64_t)disp;
		snprintf(d->ops, sizeof d->ops, "0x%llx", (unsigned long long)t);
		set_mnem(d, "loope");
		return 0;
	}
	case 0xE2: {
		int64_t disp;
		uint64_t t;
		if (fetch_disp8(d, &disp) < 0)
			return -1;
		t = d->addr + (d->pos - d->start) + (uint64_t)disp;
		snprintf(d->ops, sizeof d->ops, "0x%llx", (unsigned long long)t);
		set_mnem(d, "loop");
		return 0;
	}
	case 0xE3: {
		int64_t disp;
		uint64_t t;
		const char *m = (d->addr_sz == 4) ? "jecxz" : "jrcxz";
		if (fetch_disp8(d, &disp) < 0)
			return -1;
		t = d->addr + (d->pos - d->start) + (uint64_t)disp;
		snprintf(d->ops, sizeof d->ops, "0x%llx", (unsigned long long)t);
		set_mnem(d, m);
		return 0;
	}

	case 0xE8:
		return op_rel(d, "call", 4);
	case 0xE9:
		return op_rel(d, "jmp", 4);
	case 0xEB:
		return op_rel(d, "jmp", 1);

	case 0xF4:
		return op_none(d, "hlt");
	case 0xF5:
		return op_none(d, "cmc");

	case 0xF6:
		return op_grp3(d, 0xF6);
	case 0xF7:
		return op_grp3(d, 0xF7);

	case 0xF8:
		return op_none(d, "clc");
	case 0xF9:
		return op_none(d, "stc");
	case 0xFA:
		return op_none(d, "cli");
	case 0xFB:
		return op_none(d, "sti");
	case 0xFC:
		return op_none(d, "cld");
	case 0xFD:
		return op_none(d, "std");

	case 0xFE:
		return op_grp4(d);
	case 0xFF:
		return op_grp5(d);

	/* in/out instructions (rare in 64-bit userspace) */
	case 0xE4: {
		int v;
		if (fetch8(d, &v) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "$0x%x, %%al",
		         (unsigned)(unsigned char)v);
		set_mnem(d, "in");
		return 0;
	}
	case 0xE5: {
		int v;
		if (fetch8(d, &v) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "$0x%x, %%%s",
		         (unsigned)(unsigned char)v, reg_name(0, d->osz, 0));
		set_mnem(d, "in");
		return 0;
	}
	case 0xE6: {
		int v;
		if (fetch8(d, &v) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "%%al, $0x%x",
		         (unsigned)(unsigned char)v);
		set_mnem(d, "out");
		return 0;
	}
	case 0xE7: {
		int v;
		if (fetch8(d, &v) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "%%%s, $0x%x",
		         reg_name(0, d->osz, 0), (unsigned)(unsigned char)v);
		set_mnem(d, "out");
		return 0;
	}
	case 0xEC:
		set_mnem(d, "in");
		snprintf(d->ops, sizeof d->ops, "%%dx, %%al");
		return 0;
	case 0xED:
		set_mnem(d, "in");
		snprintf(d->ops, sizeof d->ops, "%%dx, %%%s",
		        reg_name(0, d->osz, 0));
		return 0;
	case 0xEE:
		set_mnem(d, "out");
		snprintf(d->ops, sizeof d->ops, "%%al, %%dx");
		return 0;
	case 0xEF:
		set_mnem(d, "out");
		snprintf(d->ops, sizeof d->ops, "%%%s, %%dx",
		         reg_name(0, d->osz, 0));
		return 0;

	case 0xD4: {
		int v;
		if (fetch8(d, &v) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "$0x%x",
		         (unsigned)(unsigned char)v);
		set_mnem(d, "aam");
		return 0;
	}
	case 0xD5: {
		int v;
		if (fetch8(d, &v) < 0)
			return -1;
		snprintf(d->ops, sizeof d->ops, "$0x%x",
		         (unsigned)(unsigned char)v);
		set_mnem(d, "aad");
		return 0;
	}

	case 0x62:
		/* EVEX (AVX-512) prefix in 64-bit; treat as (bad) with ModR/M */
		{
			int reg, is_mem, rm;
			struct mem_op mem;
			if (decode_modrm(d, &reg, &is_mem, &rm, &mem) < 0)
				return -1;
			d->ops[0] = '\0';
			set_mnem(d, "(bad)");
			return -1;
		}

	case 0x63:
		/* movsxd r64, r/m32 (requires REX.W in practice) */
		return op_movsxd(d);

	default:
		break;
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* Prefix parsing                                                       */
/* ------------------------------------------------------------------ */

static int
parse_prefixes(struct dec *d)
{
	for (;;) {
		int b;
		if (d->pos >= d->size)
			return -1;
		b = d->bytes[d->pos];
		if (b == 0xF0) {
			d->lock = 1;
			d->pos++;
			continue;
		}
		if (b == 0xF2) {
			d->repne = 1;
			d->pos++;
			continue;
		}
		if (b == 0xF3) {
			d->rep = 1;
			d->pos++;
			continue;
		}
		if (b == 0x66) {
			d->osize_pfx = 1;
			d->pos++;
			continue;
		}
		if (b == 0x67) {
			d->addr_sz = 4;
			d->pos++;
			continue;
		}
		if (b == 0x26) { d->seg = 0; d->pos++; continue; }
		if (b == 0x2E) { d->seg = 1; d->pos++; continue; }
		if (b == 0x36) { d->seg = 2; d->pos++; continue; }
		if (b == 0x3E) { d->seg = 3; d->pos++; continue; }
		if (b == 0x64) { d->seg = 4; d->pos++; continue; }
		if (b == 0x65) { d->seg = 5; d->pos++; continue; }
		if (b >= 0x40 && b <= 0x4F) {
			/* REX must be the last prefix before the opcode. */
			d->rex = b;
			d->pos++;
			break;
		}
		break;
	}
	if (d->rex & REX_W)
		d->osz = 8;
	else if (d->osize_pfx)
		d->osz = 2;
	else
		d->osz = 4;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                   */
/* ------------------------------------------------------------------ */

int
mt_disasm_x86_64_one(const unsigned char *bytes, size_t size,
                     size_t offset, uint64_t addr,
                     struct mt_disasm_insn *out)
{
	struct dec d;
	int op, rc;

	memset(&d, 0, sizeof d);
	d.bytes = bytes;
	d.size = size;
	d.start = offset;
	d.pos = offset;
	d.addr = addr;
	d.seg = -1;
	d.osz = 4;
	d.addr_sz = 8;
	d.ops[0] = '\0';
	d.mnem[0] = '\0';

	if (offset >= size) {
		out->address = addr;
		out->offset = offset;
		out->length = 1;
		out->operands[0] = '\0';
		out->bytes_hex[0] = '\0';
		snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
		return -1;
	}

	if (parse_prefixes(&d) < 0)
		return fail(&d, out);

	if (fetch8(&d, &op) < 0)
		return fail(&d, out);

	if (op == 0x0F)
		rc = decode_2byte(&d);
	else
		rc = decode_1byte(&d, op);

	if (rc < 0) {
		/* decode_1byte may have set a "(bad)" mnemonic while returning -1
		 * to signal partial/unknown; honor the mnemonic if non-empty. */
		if (d.mnem[0] != '\0' && strcmp(d.mnem, "(bad)") == 0) {
			/* keep "(bad)" with the bytes consumed so far */
			return fail(&d, out);
		}
		return fail(&d, out);
	}
	return finish(&d, out);
}
