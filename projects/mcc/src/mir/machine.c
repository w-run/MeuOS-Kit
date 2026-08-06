/* machine.c — MIR machine layer functions.
 *
 * Physical registers as MVal (MV_REG), addressing modes (MAddr), machine
 * opcodes (MMOP), condition codes (MCC), machine functions/blocks/instructions
 * (MFnM/MBlkM/MInsM), and dump/free.
 *
 * Per-architecture MTargetM register tables live in mtarget.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"

const char *
mreg_name(const MTargetM *mt, MReg r)
{
	if (!mt || r < 0 || r >= mt->nreg)
		return "?";
	return mt->regs[r].name;
}

int
mreg_id(const MTargetM *mt, const char *name)
{
	if (!mt || !name)
		return -1;
	for (uint32_t i = 0; i < mt->nreg; i++)
		if (mt->regs[i].name && strcmp(mt->regs[i].name, name) == 0)
			return (int)i;
	return -1;
}

/* Return the MVal for a physical register of `mt`, creating it on first
 * use.  Register values live in fn->reg[] and stay OUT of the SSA val
 * table (they carry no def/use chains).  id == MReg so dumps stay
 * readable. */
MVal *
mfn_reg(MFn *fn, const MTargetM *mt, MReg r)
{
	if (!mt || r < 0 || r >= mt->nreg)
		return 0;
	if (!fn->reg) {
		fn->reg = calloc(mt->nreg, sizeof *fn->reg);
		fn->nreg = mt->nreg;
	}
	if (!fn->reg[r]) {
		MVal *v = calloc(1, sizeof *v);
		v->id = (uint32_t)r;
		v->kind = MV_REG;
		/* machine word type follows the target pointer width: i386/arm
		 * (ILP32) registers are 32-bit, LP64 targets are 64-bit. */
		v->type = mt->regs[r].cls == MRC_FPR ? MT_F64 :
		          (mt->ptrsize == 4 ? MT_I32 : MT_I64);
		v->reg = r;
		v->slot = -1;
		v->hint = -1;
		v->name = mx_strdup(mt->regs[r].name);
		fn->reg[r] = v;
	}
	return fn->reg[r];
}

/* ---- addressing modes -------------------------------------------------- */

MAddr
maddr(MVal *base, MVal *index, uint8_t scale, int64_t off)
{
	MAddr a = {0};
	a.base = base;
	a.index = index;
	a.scale = scale == 0 ? 1 : scale;
	a.off = off;
	return a;
}

MAddr
maddr_sym(MVal *base, MConst *offcon, int64_t off)
{
	MAddr a = {0};
	a.base = base;
	a.offcon = offcon;
	a.off = off;
	return a;
}

/* ---- opcode / condition names ----------------------------------------- */

const char *
mmop_name(MMOP op)
{
	static const char *names[MMOP_NOP] = {
		[MMOP_NONE]     = "none",
		[MMOP_MOV]      = "mov",
		[MMOP_MOVSX]    = "movsx",
		[MMOP_MOVZX]    = "movzx",
		[MMOP_LEA]      = "lea",
		[MMOP_PUSH]     = "push",
		[MMOP_POP]      = "pop",
		[MMOP_ADD]      = "add",
		[MMOP_SUB]      = "sub",
		[MMOP_MUL]      = "mul",
		[MMOP_AND]      = "and",
		[MMOP_OR]       = "or",
		[MMOP_XOR]      = "xor",
		[MMOP_SHL]      = "shl",
		[MMOP_SHR]      = "shr",
		[MMOP_SAR]      = "sar",
		[MMOP_NEG]      = "neg",
		[MMOP_NOT]      = "not",
		[MMOP_DIV]      = "div",
		[MMOP_UDIV]     = "udiv",
		[MMOP_REM]      = "rem",
		[MMOP_UREM]     = "urem",
		[MMOP_FADD]     = "fadd",
		[MMOP_FSUB]     = "fsub",
		[MMOP_FMUL]     = "fmul",
		[MMOP_FDIV]     = "fdiv",
		[MMOP_FNEG]     = "fneg",
		[MMOP_FSQRT]    = "fsqrt",
		[MMOP_CVTSI2SS] = "cvtsi2ss",
		[MMOP_CVTSI2SD] = "cvtsi2sd",
		[MMOP_CVTSI2SS_U] = "cvtsi2ss_u",
		[MMOP_CVTSI2SD_U] = "cvtsi2sd_u",
		[MMOP_CVTSS2SD] = "cvtss2sd",
		[MMOP_CVTSD2SS] = "cvtsd2ss",
		[MMOP_CVTTSS2SI]= "cvttss2si",
		[MMOP_CVTTSD2SI]= "cvttsd2si",
		[MMOP_LOAD]     = "load",
		[MMOP_LOAD_S8]  = "load_s8",
		[MMOP_LOAD_S16] = "load_s16",
		[MMOP_LOAD_S32] = "load_s32",
		[MMOP_LOAD_Z8]  = "load_z8",
		[MMOP_LOAD_Z16] = "load_z16",
		[MMOP_LOAD_Z32] = "load_z32",
		[MMOP_STORE]    = "store",
		[MMOP_BLIT]     = "blit",
		[MMOP_ALLOCA4]  = "alloca4",
		[MMOP_ALLOCA8]  = "alloca8",
		[MMOP_ALLOCA16] = "alloca16",
		[MMOP_SALLOC]   = "salloc",
		[MMOP_VASTART]  = "vastart",
		[MMOP_VAARG]    = "vaarg",
		[MMOP_CMP]      = "cmp",
		[MMOP_TEST]     = "test",
		[MMOP_SETCC]    = "setcc",
		[MMOP_CMOV]     = "cmov",
		[MMOP_SETCCR]   = "setccr",
		[MMOP_PARM]     = "parm",
		[MMOP_ARG]      = "arg",
		[MMOP_JMP]      = "jmp",
		[MMOP_JCC]      = "jcc",
		[MMOP_CALL]     = "call",
		[MMOP_RET]      = "ret",
	};
	return (unsigned)op < MMOP_NOP ? names[op] : "?";
}

const char *
mcc_name(MCC cc)
{
	static const char *names[MCC_NCC] = {
		[MCC_NONE] = "none",
		[MCC_EQ]  = "eq",  [MCC_NE] = "ne",
		[MCC_CS]  = "cs",  [MCC_CC] = "cc",
		[MCC_MI]  = "mi",  [MCC_PL] = "pl",
		[MCC_VS]  = "vs",  [MCC_VC] = "vc",
		[MCC_HI]  = "hi",  [MCC_LS] = "ls",
		[MCC_GE]  = "ge",  [MCC_LT] = "lt",
		[MCC_GT]  = "gt",  [MCC_LE] = "le",
		[MCC_AL]  = "al",
	};
	return (unsigned)cc < MCC_NCC ? names[cc] : "?";
}

/* ---- machine function / block construction ----------------------------- */

MFnM *
mfnm_new(MFn *host, const MTargetM *mt, const char *name)
{
	MFnM *fm = calloc(1, sizeof *fm);
	fm->name = name ? mx_strdup(name) : 0;
	fm->mt = mt ? mt : &mtarget_x86_64;
	fm->host = host;
	return fm;
}

MBlkM *
mblkm_new(MFnM *fm, const char *name)
{
	MBlkM *b = calloc(1, sizeof *b);
	b->id = fm->nblk;
	b->name = name ? mx_strdup(name) : 0;
	b->term.op = MMOP_NONE;
	return b;
}

void
mfnm_addblk(MFnM *fm, MBlkM *b)
{
	b->id = fm->nblk;
	if (fm->nblk == 0)
		fm->start = b;
	b->link = fm->link;
	fm->link = b;
	fm->nblk++;
}

/* ---- machine instruction builders -------------------------------------- */

static MInsM *
minsm_alloc(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst)
{
	(void)fm;
	if (b->nins == b->cins) {
		b->cins = b->cins ? b->cins * 2 : 16;
		b->ins = realloc(b->ins, b->cins * sizeof *b->ins);
	}
	MInsM *in = &b->ins[b->nins++];
	memset(in, 0, sizeof *in);
	in->id = b->nins - 1;
	in->op = op;
	in->dtype = dtype;
	in->dst = dst;
	in->blk = b;
	return in;
}

MInsM *
maddm(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
      MVal *s0, MVal *s1)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->src[0] = s0;
	in->src[1] = s1;
	return in;
}

MInsM *
maddm3(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
       MVal *s0, MVal *s1, MVal *s2)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->src[0] = s0;
	in->src[1] = s1;
	in->src[2] = s2;
	return in;
}

MInsM *
maddm_addr(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
           MAddr a, MVal *s0)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->addr = a;
	in->src[0] = s0;
	return in;
}

MInsM *
maddm_cst(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
          MVal *s0, MConst *cst)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->src[0] = s0;
	in->cst = cst;
	return in;
}

MInsM *
maddm_cc(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
         MVal *s0, MVal *s1, MCC cc)
{
	MInsM *in = minsm_alloc(fm, b, op, dtype, dst);
	in->src[0] = s0;
	in->src[1] = s1;
	in->cc = cc;
	return in;
}

MInsM *
maddm_blit(MFnM *fm, MBlkM *b, MVal *dstptr, MVal *srcptr, MConst *size)
{
	MInsM *in = minsm_alloc(fm, b, MMOP_BLIT, MT_NONE, 0);
	in->src[0] = dstptr;
	in->src[1] = srcptr;
	in->cst = size;
	return in;
}

void
mfnm_term(MFnM *fm, MBlkM *b, MMOP op, MVal *s0, MBlkM *s1, MBlkM *s2,
          MCC cc)
{
	(void)fm;
	b->term.op = op;
	b->term.src[0] = s0;
	b->term.cc = cc;
	b->term.blk = b;
	b->s1 = s1;
	b->s2 = s2;
}

/* ---- dump -------------------------------------------------------------- */

static const char *
mtname(MType t)
{
	static const char *names[MT_NTYPE] = {
		[MT_NONE] = "none", [MT_VOID] = "void",
		[MT_I8] = "i8",  [MT_I16] = "i16", [MT_I32] = "i32", [MT_I64] = "i64",
		[MT_F32] = "f32", [MT_F64] = "f64",
		[MT_PTR] = "ptr", [MT_AGG] = "agg",
	};
	return (unsigned)t < MT_NTYPE ? names[t] : "?";
}

static void
print_mval(FILE *f, MVal *v)
{
	if (!v) {
		fputs("_", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "%%%s", v->name ? v->name : "reg");
		break;
	case MV_TEMP:
		fprintf(f, "%%v%u", v->id);
		break;
	case MV_GLOBAL:
		fprintf(f, "@%s", v->sym ? v->sym : "?");
		break;
	case MV_CONST:
		fprintf(f, "$c%u", v->id);
		break;
	case MV_TYPE:
		fprintf(f, "!t%u", v->td ? v->td->id : 0);
		break;
	case MV_LABEL:
		fprintf(f, "&%s", v->defblk && v->defblk->name ? v->defblk->name : "?");
		break;
	default:
		fputs("?", f);
		break;
	}
}

static void
print_mconst(FILE *f, MConst *c)
{
	if (!c) {
		fputs("_", f);
		return;
	}
	switch (c->kind) {
	case MC_INT:
		fprintf(f, "$%lld", (long long)c->u.i);
		break;
	case MC_FLT:
		fprintf(f, "$%f", c->type == MT_F32 ? (double)c->u.s : c->u.d);
		break;
	case MC_ADDR:
		fprintf(f, "&%s%+lld", c->u.addr.sym ? c->u.addr.sym : "?",
		        (long long)c->u.addr.off);
		break;
	default:
		fputs("$?", f);
		break;
	}
}

static void
print_maddr(FILE *f, MAddr a)
{
	fputs("[", f);
	if (a.offcon) {
		print_mconst(f, a.offcon);
	} else if (a.off) {
		fprintf(f, "%lld", (long long)a.off);
	}
	if (a.base) {
		fputs(a.offcon || a.off ? "+" : "", f);
		print_mval(f, a.base);
	}
	if (a.index) {
		fprintf(f, "+%s*%u", a.index->name ? a.index->name : "idx", a.scale);
	}
	fputs("]", f);
}

static void
dump_mblk(FILE *f, MBlkM *b)
{
	fprintf(f, "\nblock %s (id %u)\n", b->name ? b->name : "?", b->id);
	for (uint32_t i = 0; i < b->nins; i++) {
		MInsM *in = &b->ins[i];
		fputs("  ", f);
		if (in->dst) {
			print_mval(f, in->dst);
			fputs(" = ", f);
		} else {
			fputs("      ", f);
		}
		fprintf(f, "%s (%s)", mmop_name(in->op), mtname(in->dtype));
		for (int k = 0; k < 3 && in->src[k]; k++) {
			fputs(k ? ", " : " ", f);
			print_mval(f, in->src[k]);
		}
		if (in->cst) {
			fputs(" ", f);
			print_mconst(f, in->cst);
		}
		if (in->op == MMOP_LOAD || in->op == MMOP_STORE ||
		    in->op == MMOP_LEA || in->op == MMOP_BLIT) {
			fputs(" @", f);
			print_maddr(f, in->addr);
		}
		if (in->op == MMOP_SETCC || in->op == MMOP_JCC)
			fprintf(f, " cc=%s", mcc_name(in->cc));
		fputs("\n", f);
	}
	fputs("  term ", f);
	switch (b->term.op) {
	case MMOP_JMP:
		fprintf(f, "jmp %s\n", b->s1 && b->s1->name ? b->s1->name : "?");
		break;
	case MMOP_JCC:
		fprintf(f, "jcc %s -> %s / %s\n", mcc_name(b->term.cc),
		        b->s1 && b->s1->name ? b->s1->name : "?",
		        b->s2 && b->s2->name ? b->s2->name : "?");
		break;
	case MMOP_CALL:
		fputs("call ", f);
		print_mval(f, b->term.src[0]);
		fputs("\n", f);
		break;
	case MMOP_RET:
		fputs("ret ", f);
		print_mval(f, b->term.src[0]);
		fputs("\n", f);
		break;
	default:
		fputs("(none)\n", f);
		break;
	}
}

void
mfnm_dump(MFnM *fm, FILE *out)
{
	fprintf(out, "machine function %s (host %s, nblk %u)\n",
	        fm->name ? fm->name : "?", fm->host && fm->host->name ? fm->host->name : "?",
	        fm->nblk);
	fprintf(out, "  slot %d salign %d nspill %u regsused %#llx\n",
	        fm->slot, fm->salign, fm->nspill, (unsigned long long)fm->regsused);
	for (MBlkM *b = fm->link; b; b = b->link)
		dump_mblk(out, b);
}

void
mfnm_free(MFnM *fm)
{
	if (!fm)
		return;
	for (MBlkM *b = fm->link; b;) {
		MBlkM *next = b->link;
		free(b->name);
		free(b->ins);
		free(b);
		b = next;
	}
	free((char *)fm->name);
	free(fm);
}