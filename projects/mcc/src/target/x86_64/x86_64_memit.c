/* x86_64_memit.c — MFnM -> x86-64 assembly (P3b).
 *
 * Emits the MIR machine layer as AT&T-syntax x86-64.  P3b uses an
 * "all-virtual-registers-on-stack" scheme so the instruction selector can
 * be validated end-to-end (convert + isel + ABI + emit + run) before the
 * real register allocator lands in P4: every MV_TEMP gets an 8-byte stack
 * slot, arithmetic flows through %rax (division uses %rcx for the divisor),
 * floating point through %xmm0.  Correct but slow; P4 replaces the slot
 * mapping with a linear-scan allocator.
 *
 * Purity rule: this reads only MFnM/MMOP/MAddr/MVal — no QBE structures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "x86_64_m.h"

/* ---- width suffixes ----------------------------------------------------- */

static char
width_suffix(MType t)
{
	switch (t) {
	case MT_I8:  return 'b';
	case MT_I16: return 'w';
	case MT_I32: return 'l';
	case MT_I64:
	case MT_PTR: return 'q';
	case MT_F32: return 's';   /* ss */
	case MT_F64: return 'd';   /* sd */
	default:     return 'q';
	}
}

static const char *
cc_suffix(MCC cc)
{
	switch (cc) {
	case MCC_EQ:  return "e";
	case MCC_NE:  return "ne";
	case MCC_CS:  return "ae";   /* carry set  -> unsigned >= */
	case MCC_CC:  return "b";    /* carry clear-> unsigned <  */
	case MCC_HI:  return "a";    /* unsigned > */
	case MCC_LS:  return "be";   /* unsigned <= */
	case MCC_GE:  return "ge";
	case MCC_LT:  return "l";
	case MCC_GT:  return "g";
	case MCC_LE:  return "le";
	default:      return "e";
	}
}

static const MTargetM *g_mt;
static int g_alloca_cur;   /* frame-relative cursor for static allocas */

static int
alloca_size(MMOP op)
{
	switch (op) {
	case MMOP_ALLOCA4:  return 4;
	case MMOP_ALLOCA8:  return 8;
	default:            return 16;
	}
}

/* total bytes reserved for static allocas */
static int
alloca_total(MFnM *fm)
{
	int t = 0;
	for (MBlkM *b = fm->link; b; b = b->link)
		for (uint32_t i = 0; i < b->nins; i++) {
			MMOP op = b->ins[i].op;
			if (op == MMOP_ALLOCA4 || op == MMOP_ALLOCA8 ||
			    op == MMOP_ALLOCA16)
				t += alloca_size(op);
		}
	return t;
}

/* ---- stack slot assignment ---------------------------------------------- */

/* Regalloc assigns MVal.reg (physical) or MVal.slot (spill); this pass
 * only backstops values the allocator missed (defensive; normally none),
 * below the callee-saved save area.  Returns the extra frame bytes. */
static int
assign_extra_slots(MFnM *fm)
{
	int savesz = 0;
	for (int i = 0; g_mt->rclob && g_mt->rclob[i] >= 0; i++)
		if ((fm->regsused >> g_mt->rclob[i]) & 1)
			savesz += 8;
	int off = savesz;
	MVal *ops[8];
	for (MBlkM *b = fm->link; b; b = b->link) {
		for (uint32_t i = 0; i < b->nins; i++) {
			MInsM *in = &b->ins[i];
			ops[0] = in->dst; ops[1] = in->src[0]; ops[2] = in->src[1];
			ops[3] = in->src[2]; ops[4] = in->addr.base;
			ops[5] = in->addr.index;
			ops[6] = b->term.src[0]; ops[7] = b->term.addr.base;
			for (int k = 0; k < 8; k++) {
				MVal *v = ops[k];
				if (v && v->kind == MV_TEMP && v->reg < 0 &&
				    v->slot == -1) {
					off -= 8;
					v->slot = -off;
				}
			}
		}
	}
	return off - savesz;
}

/* ---- float constant pool (per emitted function) -------------------------- */

static MConst **fp_pool;
static uint32_t nfp, cfp;

/* find or add a float constant, returning its .rodata label index */
static uint32_t
fp_label(MConst *c)
{
	for (uint32_t i = 0; i < nfp; i++) {
		MConst *x = fp_pool[i];
		if (x->type != c->type)
			continue;
		if (x->type == MT_F32 ? x->u.s == c->u.s : x->u.d == c->u.d)
			return i;
	}
	if (nfp == cfp) {
		cfp = cfp ? cfp * 2 : 8;
		fp_pool = realloc(fp_pool, cfp * sizeof *fp_pool);
	}
	fp_pool[nfp++] = c;
	return nfp - 1;
}

static void
fp_pool_emit(FILE *f)
{
	if (!nfp)
		return;
	fputs(".section .rodata\n", f);
	for (uint32_t i = 0; i < nfp; i++) {
		MConst *c = fp_pool[i];
		fprintf(f, ".LC%u:\n", i);
		if (c->type == MT_F32) {
			union { float s; uint32_t u; } x = { .s = c->u.s };
			fprintf(f, "\t.long %u\n", x.u);
		} else {
			union { double d; uint64_t u; } x = { .d = c->u.d };
			fprintf(f, "\t.quad %llu\n", (unsigned long long)x.u);
		}
	}
}

/* ---- operands ------------------------------------------------------------ */

static void
emit_const(FILE *f, MConst *c)
{
	if (!c) {
		fputs("$0", f);
		return;
	}
	switch (c->kind) {
	case MC_INT:
		fprintf(f, "$%lld", (long long)c->u.i);
		break;
	case MC_FLT:
		fprintf(f, ".LC%u(%%rip)", fp_label(c));
		break;
	case MC_ADDR:
		fprintf(f, "%s(%%rip)", c->u.addr.sym ? c->u.addr.sym : "0");
		break;
	default:
		fputs("$0", f);
		break;
	}
}

static void
emit_mval(FILE *f, MVal *v)
{
	if (!v) {
		fputs("$0", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "%%%s", v->name ? v->name : "rax");
		break;
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "%%%s", mreg_name(g_mt, v->reg));
		else
			fprintf(f, "%d(%%rbp)", v->slot);
		break;
	case MV_GLOBAL:
		fprintf(f, "%s(%%rip)", v->sym ? v->sym : "0");
		break;
	case MV_CONST:
		emit_const(f, v->con);
		break;
	default:
		fputs("$0", f);
		break;
	}
}

/* Emit loads of non-register base/index into r10/r11 (before the memory
 * instruction) so x86 addressing only sees registers.  Global symbols are
 * addresses -> leaq; virtual slots -> movq. */
static void
emit_addr_loads(FILE *f, MAddr a)
{
	if (a.base && a.base->kind != MV_REG) {
		if (a.base->kind == MV_GLOBAL)
			fprintf(f, "\tleaq\t%s(%%rip), %%r10\n",
			        a.base->sym ? a.base->sym : "0");
		else {
			fputs("\tmovq\t", f);
			emit_mval(f, a.base);
			fputs(", %r10\n", f);
		}
	}
	if (a.index && a.index->kind != MV_REG) {
		if (a.index->kind == MV_GLOBAL)
			fprintf(f, "\tleaq\t%s(%%rip), %%r11\n",
			        a.index->sym ? a.index->sym : "0");
		else {
			fputs("\tmovq\t", f);
			emit_mval(f, a.index);
			fputs(", %r11\n", f);
		}
	}
}

/* Print the AT&T memory operand: disp(base, index, scale).  The base
 * follows the displacement without a comma (`8(%rsp)`); a comma separates
 * base from index, and the index always carries a scale. */
static void
emit_addr(FILE *f, MAddr a)
{
	/* AT&T: disp is written before the parens: 8(%rsp) */
	if (a.offcon) {
		fprintf(f, "%s", a.offcon->u.addr.sym ? a.offcon->u.addr.sym : "0");
		if (a.off)
			fprintf(f, "%+lld", (long long)a.off);
	} else if (a.off) {
		fprintf(f, "%lld", (long long)a.off);
	}
	fputc('(', f);
	if (a.base) {
		if (a.base->kind == MV_REG)
			emit_mval(f, a.base);
		else
			fputs("%r10", f);
	}
	if (a.index) {
		fputc(',', f);
		if (a.index->kind == MV_REG)
			emit_mval(f, a.index);
		else
			fputs("%r11", f);
		fprintf(f, ",%u", a.scale ? a.scale : 1);
	}
	fputs(")", f);
}

/* Load a value (or immediate) into %rax.
 * MV_GLOBAL / MC_ADDR are POINTERS (symbol addresses) -> leaq.
 * MV_CONST(MC_INT) / MV_TEMP / MV_REG -> movq. */
static void
mov_to_rax(FILE *f, MVal *v, MConst *c)
{
	if (c) {
		if (c->kind == MC_ADDR) {
			fprintf(f, "\tleaq\t%s%+lld(%%rip), %%rax\n",
			        c->u.addr.sym ? c->u.addr.sym : "0",
			        (long long)c->u.addr.off);
		} else if (c->kind == MC_INT) {
			fprintf(f, "\tmovq\t$%lld, %%rax\n", (long long)c->u.i);
		} else {
			fputs("\tmovq\t$0, %rax\n", f);
		}
		return;
	}
	if (!v) {
		fputs("\tmovq\t$0, %rax\n", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "\tmovq\t%%%s, %%rax\n", v->name);
		break;
	case MV_GLOBAL:
		fprintf(f, "\tleaq\t%s(%%rip), %%rax\n", v->sym ? v->sym : "0");
		break;
	case MV_CONST:
		mov_to_rax(f, 0, v->con);
		break;
	default:
		fputs("\tmovq\t", f);
		emit_mval(f, v);
		fputs(", %rax\n", f);
		break;
	}
}

/* Store %rax to dst (register or virtual slot). */
static void
rax_to_dst(FILE *f, MVal *dst)
{
	if (!dst)
		return;
	if (dst->kind == MV_REG)
		fprintf(f, "\tmovq\t%%rax, %%%s\n", dst->name);
	else if (dst->kind == MV_TEMP) {
		fputs("\tmovq\t%rax, ", f);
		emit_mval(f, dst);
		fputs("\n", f);
	}
	/* MV_GLOBAL dst: unsupported for now */
}

/* Does an immediate fit in the x86-64 32-bit signed displacement? */
static bool
imm32(int64_t v)
{
	return v >= INT32_MIN && v <= INT32_MAX;
}

/* Emit the second operand of a binary op / cmp: either an immediate
 * (if it fits) or a value pre-loaded into %r9 (large immediates). */
static void
emit_rhs(FILE *f, MVal *s1, MConst *c)
{
	if (c) {
		if (imm32(c->u.i)) {
			emit_const(f, c);
		} else {
			fprintf(f, "%%r9");   /* caller emitted the load */
		}
		return;
	}
	emit_mval(f, s1);
}

/* Move src (reg/imm/slot/global) into dst slot/reg.  Immediates go
 * directly; symbol addresses need leaq; everything else flows through
 * %rax.  Floating-point moves flow through %xmm0. */
static void
emit_mov(FILE *f, MVal *dst, MVal *src, MConst *c, MType dtype)
{
	if (dtype == MT_F32 || dtype == MT_F64) {
		const char *sd = dtype == MT_F64 ? "sd" : "ss";
		/* direct move: x86 movss/movsd handles reg<->mem; do NOT route
		 * through %xmm0 (it may be an ABI argument/return register) */
		if (c && c->kind == MC_FLT) {
			if (dst) {
				fprintf(f, "\tmov%s\t", sd);
				emit_const(f, c);
				fputs(", ", f);
				emit_mval(f, dst);
				fputs("\n", f);
			}
		} else if (src) {
			bool smem = src->kind == MV_TEMP || src->kind == MV_GLOBAL;
			bool dmem = dst && (dst->kind == MV_TEMP ||
			                    dst->kind == MV_GLOBAL);
			if (smem && dmem) {
				/* mem->mem: bit-copy via r10 */
				fputs("\tmovq\t", f);
				emit_mval(f, src);
				fputs(", %r10\n", f);
				fputs("\tmovq\t%r10, ", f);
				emit_mval(f, dst);
				fputs("\n", f);
			} else if (dst) {
				fprintf(f, "\tmov%s\t", sd);
				emit_mval(f, src);
				fputs(", ", f);
				emit_mval(f, dst);
				fputs("\n", f);
			}
		}
		return;
	}
	if (c && c->kind == MC_INT) {
		if (!dst)
			return;
		fprintf(f, "\tmovq\t$%lld, ", (long long)c->u.i);
		emit_mval(f, dst);
		fputs("\n", f);
		return;
	}
	mov_to_rax(f, src, c);
	rax_to_dst(f, dst);
}

/* ---- per-instruction emit ------------------------------------------------- */

static void
emit_ins(FILE *f, MInsM *in)
{
	char w = width_suffix(in->dtype);
	MVal *d = in->dst, *s0 = in->src[0], *s1 = in->src[1];
	MConst *c = in->cst;

	switch (in->op) {
	case MMOP_MOV:
		emit_mov(f, d, s0, c, in->dtype);
		return;
	case MMOP_MOVSX:
	case MMOP_MOVZX: {
		MType st = s0 ? s0->type : in->dtype;
		bool zx = in->op == MMOP_MOVZX;
		if (st == MT_I64 || st == MT_PTR || st == MT_NONE) {
			/* nothing to extend: plain move */
			mov_to_rax(f, s0, 0);
			rax_to_dst(f, d);
			return;
		}
		/* load the value into %rax first, then extend from its low bits
		 * (movz/movs need byte/word/32-bit register or memory sources) */
		mov_to_rax(f, s0, 0);
		if (st == MT_I8)
			fputs(zx ? "\tmovzbq\t%al, %rax\n" : "\tmovsbq\t%al, %rax\n", f);
		else if (st == MT_I16)
			fputs(zx ? "\tmovzwq\t%ax, %rax\n" : "\tmovswq\t%ax, %rax\n", f);
		else if (!zx)
			fputs("\tmovslq\t%eax, %rax\n", f);
		else
			fputs("\tmovl\t%eax, %eax\n", f);   /* i32 zero-extend */
		rax_to_dst(f, d);
		return;
	}
	case MMOP_LEA:
		emit_addr_loads(f, in->addr);
		fputs("\tleaq\t", f);
		emit_addr(f, in->addr);
		fputs(", %rax\n", f);
		rax_to_dst(f, d);
		return;
	case MMOP_ADD: case MMOP_SUB: case MMOP_MUL:
	case MMOP_AND: case MMOP_OR:  case MMOP_XOR:
	case MMOP_SHL: case MMOP_SHR: case MMOP_SAR: {
		/* integer arithmetic is done 64-bit (low 32 bits match the
		 * 32-bit forms); only loads/stores honor the width */
		const char *op;
		switch (in->op) {
		case MMOP_ADD: op = "add"; break;
		case MMOP_SUB: op = "sub"; break;
		case MMOP_MUL: op = "imul"; break;
		case MMOP_AND: op = "and"; break;
		case MMOP_OR:  op = "or"; break;
		case MMOP_XOR: op = "xor"; break;
		case MMOP_SHL: op = "shl"; break;
		case MMOP_SHR: op = "shr"; break;
		default:       op = "sar"; break;
		}
		mov_to_rax(f, s0, 0);
		if (in->op >= MMOP_SHL && in->op <= MMOP_SAR) {
			/* shift count in %cl */
			fputs("\tmovq\t", f);
			emit_mval(f, s1);
			fputs(", %rcx\n", f);
			fprintf(f, "\t%sq\t%%cl, %%rax\n", op);
		} else if (c) {
			if (imm32(c->u.i)) {
				fprintf(f, "\t%sq\t", op);
				emit_const(f, c);
				fputs(", %rax\n", f);
			} else {
				/* large immediate: load into %r9 first */
				fprintf(f, "\tmovq\t$%lld, %%r9\n", (long long)c->u.i);
				fprintf(f, "\t%sq\t%%r9, %%rax\n", op);
			}
		} else {
			/* large immediates (MV_CONST operands > 32 bits) need a reg */
			if (s1 && s1->kind == MV_CONST && s1->con &&
			    !imm32(s1->con->u.i)) {
				fprintf(f, "\tmovq\t$%lld, %%r9\n",
				        (long long)s1->con->u.i);
				fprintf(f, "\t%sq\t%%r9, %%rax\n", op);
			} else {
				fprintf(f, "\t%sq\t", op);
				emit_mval(f, s1);
				fputs(", %rax\n", f);
			}
		}
		rax_to_dst(f, d);
		return;
	}
	case MMOP_NEG:
	case MMOP_NOT: {
		mov_to_rax(f, s0, 0);
		fprintf(f, "\t%sq\t%%rax\n", in->op == MMOP_NEG ? "neg" : "not");
		rax_to_dst(f, d);
		return;
	}
	case MMOP_DIV: case MMOP_UDIV:
	case MMOP_REM: case MMOP_UREM: {
		bool is32 = in->dtype == MT_I32;
		bool isf = in->op == MMOP_UDIV || in->op == MMOP_UREM;
		/* dividend into rax: sign/zero extend to 64 bits */
		if (is32) {
			mov_to_rax(f, s0, 0);
			if (isf)
				fputs("\tmovl\t%eax, %eax\n", f);   /* zero-extend */
			else
				fputs("\tmovslq\t%eax, %rax\n", f);  /* sign-extend */
		} else {
			mov_to_rax(f, s0, 0);
		}
		if (isf)
			fputs("\txorl\t%edx, %edx\n", f);
		else
			fputs("\tcqo\n", f);
		fputs("\tmovq\t", f);
		emit_mval(f, s1);
		fputs(", %rcx\n", f);
		fprintf(f, "\t%s%c\t%%rcx\n", isf ? "div" : "idiv", 'q');
		if (in->op == MMOP_REM || in->op == MMOP_UREM)
			fputs("\tmovq\t%rdx, %rax\n", f);
		rax_to_dst(f, d);
		return;
	}
	case MMOP_CMP:
	case MMOP_TEST: {
		if (in->dtype == MT_F32 || in->dtype == MT_F64) {
			/* floating-point compare: ucomiss/ucomisd sets the same
			 * flags the SETcc mapping expects (CF/ZF) */
			const char *sd = in->dtype == MT_F64 ? "sd" : "ss";
			fprintf(f, "\tmov%s\t", sd);
			emit_mval(f, s0);
			fprintf(f, ", %%xmm0\n");
			fprintf(f, "\tucomi%s\t", sd);
			emit_mval(f, s1);
			fputs(", %xmm0\n", f);
			return;
		}
		mov_to_rax(f, s0, 0);
		if (c) {
			if (imm32(c->u.i)) {
				fprintf(f, "\t%sq\t", in->op == MMOP_CMP ? "cmp" : "test");
				emit_const(f, c);
				fputs(", %rax\n", f);
			} else {
				fprintf(f, "\tmovq\t$%lld, %%r9\n", (long long)c->u.i);
				fprintf(f, "\t%sq\t%%r9, %%rax\n",
				        in->op == MMOP_CMP ? "cmp" : "test");
			}
		} else {
			if (s1 && s1->kind == MV_CONST && s1->con &&
			    !imm32(s1->con->u.i)) {
				fprintf(f, "\tmovq\t$%lld, %%r9\n",
				        (long long)s1->con->u.i);
				fprintf(f, "\t%sq\t%%r9, %%rax\n",
				        in->op == MMOP_CMP ? "cmp" : "test");
			} else {
				fprintf(f, "\t%sq\t", in->op == MMOP_CMP ? "cmp" : "test");
				emit_mval(f, s1);
				fputs(", %rax\n", f);
			}
		}
		return;
	}
	case MMOP_SETCC:
		fprintf(f, "\tset%s\t%%al\n", cc_suffix(in->cc));
		fputs("\tmovzbl\t%al, %eax\n", f);
		rax_to_dst(f, d);
		return;
	case MMOP_LOAD: {
		/* width + zero-extension for sub-32 loads */
		emit_addr_loads(f, in->addr);
		if (in->dtype == MT_I8)
			fprintf(f, "\tmovzbl\t");
		else if (in->dtype == MT_I16)
			fprintf(f, "\tmovzwl\t");
		else if (in->dtype == MT_I32)
			fprintf(f, "\tmovl\t");
		else
			fprintf(f, "\tmovq\t");
		emit_addr(f, in->addr);
		if (in->dtype == MT_I32 || in->dtype == MT_I8 || in->dtype == MT_I16)
			fputs(", %eax\n", f);
		else
			fputs(", %rax\n", f);
		rax_to_dst(f, d);
		return;
	}
	case MMOP_STORE: {
		if (in->dtype == MT_F32 || in->dtype == MT_F64) {
			/* floating-point store: value through %xmm0 */
			const char *sd = in->dtype == MT_F64 ? "sd" : "ss";
			if (c && c->kind == MC_FLT) {
				fprintf(f, "\tmov%s\t", sd);
				emit_const(f, c);
				fprintf(f, ", %%xmm0\n");
			} else if (s0) {
				fprintf(f, "\tmov%s\t", sd);
				emit_mval(f, s0);
				fprintf(f, ", %%xmm0\n");
			}
			emit_addr_loads(f, in->addr);
			fprintf(f, "\tmov%s\t%%xmm0, ", sd);
			emit_addr(f, in->addr);
			fputs("\n", f);
			return;
		}
		mov_to_rax(f, s0, c);
		emit_addr_loads(f, in->addr);
		if (in->dtype == MT_I8)
			fputs("\tmovb\t%al, ", f);
		else if (in->dtype == MT_I16)
			fputs("\tmovw\t%ax, ", f);
		else if (in->dtype == MT_I32)
			fputs("\tmovl\t%eax, ", f);
		else
			fputs("\tmovq\t%rax, ", f);
		emit_addr(f, in->addr);
		fputs("\n", f);
		return;
	}
	case MMOP_CALL: {
		if (s0 && s0->kind == MV_GLOBAL)
			fprintf(f, "\tcall\t%s\n", s0->sym);
		else {
			mov_to_rax(f, s0, 0);
			fputs("\tcall\t*%rax\n", f);
		}
		if (g_salloc) {
			/* caller cleans up the stack-argument space */
			fprintf(f, "\taddq\t$%d, %%rsp\n", g_salloc);
			g_salloc = 0;
		}
		return;
	}
	case MMOP_SALLOC: {
		/* dynamic stack adjustment (stack arguments): the ABI lowering
		 * emits +size before the call and -size after (caller cleanup) */
		int64_t n = c ? c->u.i : 16;
		fprintf(f, "\tsubq\t$%lld, %%rsp\n", (long long)n);
		return;
	}
	case MMOP_ALLOCA4:
	case MMOP_ALLOCA8:
	case MMOP_ALLOCA16: {
		/* static alloca: address a reserved frame slot below the spill
		 * area (never touch %rsp: it must stay balanced at calls) */
		g_alloca_cur -= alloca_size(in->op);
		fprintf(f, "\tleaq\t%d(%%rbp), %%rax\n", g_alloca_cur);
		rax_to_dst(f, d);
		return;
	}
	case MMOP_BLIT:
		/* aggregate copy: dst, src pointers + size (P4 handles inline) */
		fputs("\t# blit (P4)\n", f);
		return;
	case MMOP_FADD: case MMOP_FSUB: case MMOP_FMUL: case MMOP_FDIV: {
		const char *op;
		switch (in->op) {
		case MMOP_FADD: op = "add"; break;
		case MMOP_FSUB: op = "sub"; break;
		case MMOP_FMUL: op = "mul"; break;
		default:        op = "div"; break;
		}
		fprintf(f, "\tmov%s\t", w == 'd' ? "sd" : "ss");
		emit_mval(f, s0);
		fprintf(f, ", %%xmm0\n");
		fprintf(f, "\t%s%s\t", op, w == 'd' ? "sd" : "ss");
		emit_mval(f, s1);
		fputs(", %xmm0\n", f);
		if (d) {
			fprintf(f, "\tmov%s\t%%xmm0, ", w == 'd' ? "sd" : "ss");
			emit_mval(f, d);
			fputs("\n", f);
		}
		return;
	}
	case MMOP_FNEG: {
		/* negate: 0.0 - x */
		fputs("\tpxor\t%xmm0, %xmm0\n", f);
		fprintf(f, "\tsub%s\t", w == 'd' ? "sd" : "ss");
		emit_mval(f, s0);
		fputs(", %xmm0\n", f);
		if (d) {
			fprintf(f, "\tmov%s\t%%xmm0, ", w == 'd' ? "sd" : "ss");
			emit_mval(f, d);
			fputs("\n", f);
		}
		return;
	}
	case MMOP_FSQRT: {
		fprintf(f, "\tmov%s\t", w == 'd' ? "sd" : "ss");
		emit_mval(f, s0);
		fprintf(f, ", %%xmm0\n");
		fprintf(f, "\tsqrt%s\t%%xmm0, %%xmm0\n", w == 'd' ? "sd" : "ss");
		if (d) {
			fprintf(f, "\tmov%s\t%%xmm0, ", w == 'd' ? "sd" : "ss");
			emit_mval(f, d);
			fputs("\n", f);
		}
		return;
	}
	case MMOP_CVTSI2SS: case MMOP_CVTSI2SD:
	case MMOP_CVTTSS2SI: case MMOP_CVTTSD2SI:
	case MMOP_CVTSS2SD: case MMOP_CVTSD2SS: {
		/* int<->fp conversions: sources/targets live in stack slots;
		 * int regs are %rax, fp regs are %xmm0 */
		switch (in->op) {
		case MMOP_CVTSI2SS:
			mov_to_rax(f, s0, 0);
			fputs("\tcvtsi2ssl\t%eax, %xmm0\n", f);
			fputs("\tmovss\t%xmm0, ", f);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		case MMOP_CVTSI2SD:
			mov_to_rax(f, s0, 0);
			fputs("\tcvtsi2sdq\t%rax, %xmm0\n", f);
			fputs("\tmovsd\t%xmm0, ", f);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		case MMOP_CVTTSS2SI:
			fputs("\tcvttss2sil\t", f);
			emit_mval(f, s0);
			fputs(", %eax\n", f);
			rax_to_dst(f, d);
			break;
		case MMOP_CVTTSD2SI:
			fputs("\tcvttsd2siq\t", f);
			emit_mval(f, s0);
			fputs(", %rax\n", f);
			rax_to_dst(f, d);
			break;
		case MMOP_CVTSS2SD:
			fputs("\tcvtss2sd\t", f);
			emit_mval(f, s0);
			fputs(", %xmm0\n", f);
			fputs("\tmovsd\t%xmm0, ", f);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		default: /* CVTSD2SS */
			fputs("\tcvtsd2ss\t", f);
			emit_mval(f, s0);
			fputs(", %xmm0\n", f);
			fputs("\tmovss\t%xmm0, ", f);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		}
		return;
	}
	case MMOP_PUSH:
		mov_to_rax(f, s0, c);
		fputs("\tpushq\t%rax\n", f);
		return;
	case MMOP_POP:
		fputs("\tpopq\t%rax\n", f);
		rax_to_dst(f, d);
		return;
	default:
		fprintf(f, "\t# mmop %d (unhandled)\n", (int)in->op);
		return;
	}
}

/* ---- function emit -------------------------------------------------------- */

void
mfnm_emit_x86_64(MFnM *fm, FILE *f)
{
	g_mt = fm->mt;
	int extra = assign_extra_slots(fm);
	nfp = 0;   /* fresh float pool per function */
	g_salloc = 0;   /* fresh stack-argument reservation tracker */
	g_alloca_cur = -(fm->slot + extra);   /* allocas go below spill slots */

	int framesize = (fm->slot + extra + alloca_total(fm) + 15) & ~15;

	fprintf(f, ".text\n");
	if (fm->name)
		fprintf(f, ".globl %s\n%s:\n", fm->name, fm->name);
	fputs("\tpushq\t%rbp\n", f);
	fputs("\tmovq\t%rsp, %rbp\n", f);
	/* save callee-saved registers used by the allocator */
	for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0; i++) {
		int r = fm->mt->rclob[i];
		if ((fm->regsused >> r) & 1)
			fprintf(f, "\tpushq\t%%%s\n", mreg_name(fm->mt, r));
	}
	if (framesize > 0)
		fprintf(f, "\tsubq\t$%d, %%rsp\n", framesize);
	/* blocks are emitted in link order (reversed); jump to the real entry */
	if (fm->start)
		fprintf(f, "\tjmp\t.L%s.bb%u\n", fm->name ? fm->name : "f",
		        fm->start->id);

	for (MBlkM *b = fm->link; b; b = b->link) {
		fprintf(f, ".L%s.bb%u:\n", fm->name ? fm->name : "f", b->id);
		for (uint32_t i = 0; i < b->nins; i++)
			emit_ins(f, &b->ins[i]);
		switch (b->term.op) {
		case MMOP_JMP:
			fprintf(f, "\tjmp\t.L%s.bb%u\n", fm->name ? fm->name : "f",
			        b->s1 ? b->s1->id : 0);
			break;
		case MMOP_JCC:
			/* explicit taken + fallthrough jump: block order in the
			 * emitted file is not guaranteed to match the CFG */
			fprintf(f, "\tj%s\t.L%s.bb%u\n", cc_suffix(b->term.cc),
			        fm->name ? fm->name : "f",
			        b->s1 ? b->s1->id : 0);
			fprintf(f, "\tjmp\t.L%s.bb%u\n", fm->name ? fm->name : "f",
			        b->s2 ? b->s2->id : 0);
			break;
		case MMOP_RET:
			if (framesize > 0)
				fprintf(f, "\taddq\t$%d, %%rsp\n", framesize);
			/* restore callee-saved registers in reverse push order */
			{
				int used[16], n = 0;
				for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0 &&
				     n < 16; i++)
					if ((fm->regsused >> fm->mt->rclob[i]) & 1)
						used[n++] = fm->mt->rclob[i];
				while (n > 0)
					fprintf(f, "\tpopq\t%%%s\n",
					        mreg_name(fm->mt, used[--n]));
			}
			fputs("\tpopq\t%rbp\n\tret\n", f);
			break;
		default:
			break;
		}
	}

	fp_pool_emit(f);
}
