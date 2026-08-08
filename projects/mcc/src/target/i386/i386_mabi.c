/* i386_mabi.c — i386 cdecl ABI lowering (MIR-native, scalar + i64 core,
 * plus aggregate and varargs support).
 *
 * Lowers the pre-ABI machine IR for scalar + i64 + aggregate + varargs
 * functions:
 *   - selpar: load arguments from the caller-pushed stack area at
 *     ebp+8 (i32: 4-byte slots; i64: 8-byte slots; aggregates: size
 *     rounded up to 4); for varargs, record the frame data for vastart.
 *   - selcall: push call arguments onto the stack (right-to-left,
 *     cdecl), then emit the call instruction; aggregate args are blitted
 *     to the stack; aggregate returns use a hidden sret pointer.
 *   - selret: move the return value into EAX/xmm0 (i32/FP),
 *     EAX:EDX pair (i64), or blit aggregate data to the sret buffer.
 *   - vastart / vaarg: cdecl varargs — all arguments on the stack,
 *     va_list is just a pointer into the caller's stack frame.
 *
 * i386 cdecl ABI: all arguments on the stack.  No argument registers.
 * The prologue pushes {ebp} (4 bytes), so the frame pointer is
 * old_esp - 4 and caller-pushed stack arguments start at ebp+8.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "i386_m.h"

/* ---- output stream (block rewrite) ------------------------------------- */

typedef struct MOut {
	MBlkM *b;
	MInsM *ins;
	uint32_t nins, cins;
} MOut;

static void
mout_end(MOut *o)
{
	free(o->b->ins);
	o->b->ins = o->ins;
	o->b->nins = o->nins;
	o->b->cins = o->cins;
}

static MInsM *
mout_alloc(MOut *o)
{
	if (o->nins == o->cins) {
		o->cins = o->cins ? o->cins * 2 : 16;
		o->ins = realloc(o->ins, o->cins * sizeof *o->ins);
	}
	MInsM *in = &o->ins[o->nins++];
	memset(in, 0, sizeof *in);
	in->id = o->nins - 1;
	in->blk = o->b;
	return in;
}

static MInsM *
mout(MOut *o, MMOP op, MType dt, MVal *dst, MVal *s0, MVal *s1)
{
	MInsM *in = mout_alloc(o);
	in->op = op;
	in->dtype = dt;
	in->dst = dst;
	in->src[0] = s0;
	in->src[1] = s1;
	return in;
}

static MInsM *
mout_addr(MOut *o, MMOP op, MType dt, MVal *dst, MAddr a, MVal *s0)
{
	MInsM *in = mout_alloc(o);
	in->op = op;
	in->dtype = dt;
	in->dst = dst;
	in->addr = a;
	in->src[0] = s0;
	return in;
}

static MInsM *
mout_cst(MOut *o, MMOP op, MType dt, MVal *dst, MVal *s0, MConst *c)
{
	MInsM *in = mout_alloc(o);
	in->op = op;
	in->dtype = dt;
	in->dst = dst;
	in->src[0] = s0;
	in->cst = c;
	return in;
}

/* Generate a BLIT instruction (aggregate copy). */
static MInsM *
mout_blit(MFnM *fm, MOut *o, MVal *dstp, MVal *srcp, uint32_t size)
{
	MInsM *in = mout_alloc(o);
	in->op = MMOP_BLIT;
	in->src[0] = dstp;
	in->src[1] = srcp;
	in->cst = mconst_int(fm->host, MT_I32, size);
	return in;
}

/* ---- helpers ------------------------------------------------------------ */

static MVal *
reg(MFnM *fm, int r)
{
	return mfn_reg(fm->host, fm->mt, r);
}

static MVal *
tmp(MFnM *fm, MType t, const char *tag)
{
	return mval_new(fm->host, MV_TEMP, t, 0, tag);
}

static MConst *
imm(MFnM *fm, MType t, int64_t v)
{
	return mconst_int(fm->host, t, v);
}

/* Round size up to the cdecl stack slot boundary (4 bytes). */
static uint32_t
agg_stack_size(uint32_t sz)
{
	return (sz + 3) & ~3;
}

/* ---- selpar: entry parameter reception --------------------------------- */

static void
mabi_selpar(MFnM *fm, MOut *o, MInsM *parms, int n, uint32_t *vafa)
{
	int off = 8;   /* caller-pushed stack args start at ebp+8 (the
	                * prologue pushed {ebp}, so ebp = old_esp - 4) */

	/* Aggregate return via hidden sret pointer.
	 * i386 cdecl: the sret pointer is pushed as a hidden first argument
	 * (rightmost on the stack, i.e. the last thing the caller pushed,
	 * so it's the first thing on the stack from the callee's perspective
	 * — at ebp+8).  We stash it in a local alloca slot so body calls
	 * don't clobber it. */
	if (fm->retty) {
		MVal *sret_ptr = tmp(fm, MT_PTR, "sret");
		mout_addr(o, MMOP_LOAD, MT_PTR, sret_ptr,
		          maddr(reg(fm, I386MREG_EBP), 0, 1, off), 0);
		off += 4;
		MVal *pad = tmp(fm, MT_PTR, "sret");
		mout(o, MMOP_ALLOCA16, MT_PTR, pad, 0, 0);
		mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(pad, 0, 1, 0), sret_ptr);
		fm->has_sret = true;
		fm->sret_pad = pad;
	}

	for (int i = 0; i < n; i++) {
		MInsM *p = &parms[i];
		MVal *dst = p->dst;

		if (p->td) {
			/* Aggregate parameter: caller pushed the full struct copy
			 * onto the stack.  dst is a pointer to the callee's local
			 * pad.  Allocate a pad and blit from the stack into it. */
			uint32_t sz = p->td->size;
			uint32_t stk_sz = agg_stack_size(sz);
			MVal *pad = tmp(fm, MT_PTR, "abi");
			mout_cst(o, MMOP_ALLOCA16, MT_PTR, pad, 0,
			         mconst_int(fm->host, MT_I32, sz));
			mout(o, MMOP_MOV, MT_PTR, dst, pad, 0);
			/* Source address on the stack */
			MVal *srcp = tmp(fm, MT_PTR, "abi");
			mout_addr(o, MMOP_LEA, MT_PTR, srcp,
			          maddr(reg(fm, I386MREG_EBP), 0, 1, off), 0);
			mout_blit(fm, o, pad, srcp, sz);
			off += stk_sz;
			continue;
		}

		/* Scalar parameter */
		if (p->dtype == MT_I64) {
			/* i64 param: 8 bytes at [ebp+off]/[ebp+off+4].  A single
			 * MMOP_LOAD (MT_I64) into dst lets the emitter's emit_load
			 * write both halves into dst's frame slot via i64_dst_base.
			 * The MIR regalloc forces i64 values on 32-bit targets to a
			 * stack slot (regalloc: kl_in_reg==0 => phislot), so dst has
			 * a real slot by the time the emitter runs; we must NOT
			 * pre-split into i32 temps and hard-wire dst->slot (which is
			 * -1 until regalloc), or the halves read garbage (x86-i64param). */
			mout_addr(o, MMOP_LOAD, MT_I64, dst,
			          maddr(reg(fm, I386MREG_EBP), 0, 1, off), 0);
			off += 8;
		} else if (p->dtype == MT_F64) {
			/* double param: 8 bytes on the cdecl stack.  A single
			 * MMOP_LOAD (MT_F64) emits movsd into xmm0 then stores the
			 * pair; the emitter keeps F64 values in a slot pair, so the
			 * load must also advance the incoming-arg cursor by 8. */
			mout_addr(o, MMOP_LOAD, MT_F64, dst,
			          maddr(reg(fm, I386MREG_EBP), 0, 1, off), 0);
			off += 8;
		} else {
			mout_addr(o, MMOP_LOAD, p->dtype, dst,
			          maddr(reg(fm, I386MREG_EBP), 0, 1, off), 0);
			off += 4;
		}
	}

	/* i386 cdecl varargs: all arguments are on the stack.  va_list is
	 * just a pointer to the first stack argument.  Record the offset
	 * from ebp of the first stack argument after the named parameters
	 * (i.e. the current off value).  If this function is not vararg,
	 * vafa is unused (set to 0 like the scalar-only core). */
	if (fm->host && fm->host->vararg)
		*vafa = off;   /* byte offset of the first unnamed arg from ebp */
	else
		*vafa = 0;
}

/* ---- selcall: call lowering -------------------------------------------- */

static void
mabi_selcall(MFnM *fm, MOut *o, MInsM *args, int n, MInsM *call)
{
	/* Count stack bytes needed (cdecl: all args on stack; i32/f32=4,
	 * i64/double=8, aggregates = agg_stack_size(td->size)) */
	int stk = 0;
	for (int i = 0; i < n; i++) {
		if (args[i].td)
			stk += agg_stack_size(args[i].td->size);
		else
			stk += (args[i].dtype == MT_I64 || args[i].dtype == MT_F64)
			           ? 8 : 4;
		/* sret pointer: if call has aggregate return, add 4 bytes for
		 * the hidden sret pointer */
	}
	if (call->td) {
		/* For aggregate return, the caller also passes a hidden sret
		 * pointer on the stack.  It is pushed last (rightmost), so it
		 * sits at the lowest address (first pushed).  We'll push it
		 * after the args.  For now, add its size. */
		stk += 4;
	}
	int argbytes = stk;   /* unaligned outgoing-argument bytes */
	stk = (stk + 15) & ~15;   /* 16-align the stack */

	/* Reserve stack space for the outgoing arguments */
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, I386MREG_ESP),
		         imm(fm, MT_I64, stk));

	/* Push arguments right-to-left (cdecl convention).
	 * On the stack, the rightmost argument is at the lowest address
	 * (the last push).  We write to [esp + soff] in order, so arg[0]
	 * goes to the lowest address, arg[n-1] goes to the highest.
	 * soff starts at argbytes (NOT the 16-aligned stk): the alignment
	 * padding sits above the args, so arg[0] lands at [esp+0], which is
	 * what the callee reads as [ebp+8].  (Starting soff at stk placed the
	 * args 8 bytes too high: [ebp+16] instead of [ebp+8] -> rr_call
	 * read uninitialized stack.) */
	int soff = argbytes;   /* args occupy the bottom of the reserved block */

	/* Aggregate return: the hidden sret pointer sits at the LOWEST stack
	 * offset ([esp+0]), which the callee reads as [ebp+8] (its first
	 * incoming arg slot); the regular args follow it at higher offsets,
	 * so arg[0] lands at [esp+4] and is read as the callee's [ebp+12]. */
	if (call->td) {
		/* Allocate a local pad for the return value, pass its address
		 * as the hidden sret pointer */
		MVal *pad = tmp(fm, MT_PTR, "abi");
		mout_cst(o, MMOP_ALLOCA16, MT_PTR, pad, 0,
		         mconst_int(fm->host, MT_I32, call->td->size));
		mout(o, MMOP_MOV, MT_PTR, call->dst, pad, 0);
		/* Store the pad address to the stack at the sret slot ([esp+0]).
		 * Note we do NOT advance soff here: the args are placed above the
		 * sret, so arg[0] naturally lands at [esp+4] -> callee [ebp+12]. */
		mout_addr(o, MMOP_STORE, MT_PTR, 0,
		          maddr(reg(fm, I386MREG_ESP), 0, 1, 0),
		          call->dst);
	}

	for (int i = n - 1; i >= 0; i--) {
		MInsM *a = &args[i];
		if (a->td) {
			uint32_t sz = agg_stack_size(a->td->size);
			soff -= sz;
			/* Aggregate argument: src[0] is a pointer to the data.
			 * Blit it to the stack. */
			MVal *dstp = tmp(fm, MT_PTR, "abi");
			mout_addr(o, MMOP_LEA, MT_PTR, dstp,
			          maddr(reg(fm, I386MREG_ESP), 0, 1, soff), 0);
			mout_blit(fm, o, dstp, a->src[0], a->td->size);
		} else if (a->dtype == MT_I64) {
			soff -= 8;
			/* i64: 8-byte value stored at [esp+soff]/[esp+soff+4].
			 * A single MMOP_STORE (MT_I64) lets the emitter's emit_store
			 * materialise the source (constant, register, or slot-resident
			 * value) via i64_base, so the caller pushes the real halves
			 * rather than reading a possibly-unset src[0]->slot. */
			mout_addr(o, MMOP_STORE, MT_I64, 0,
			          maddr(reg(fm, I386MREG_ESP), 0, 1, soff),
			          a->src[0]);
		} else if (a->dtype == MT_F64) {
			/* double: 8 bytes on the cdecl stack.  The MMOP_STORE's
			 * emit_store emits movsd for an F64 destination, so a single
			 * store at [esp+soff] pushes the full 8-byte value. */
			soff -= 8;
			mout_addr(o, MMOP_STORE, MT_F64, 0,
			          maddr(reg(fm, I386MREG_ESP), 0, 1, soff),
			          a->src[0]);
		} else {
			soff -= 4;
			mout_addr(o, MMOP_STORE, a->dtype, 0,
			          maddr(reg(fm, I386MREG_ESP), 0, 1, soff),
			          a->src[0]);
		}
	}

	/* the call itself (dst = result; src[0] = callee) */
	mout(o, MMOP_CALL, call->dtype, call->dst, call->src[0], 0);
	o->ins[o->nins - 1].td = call->td;

	/* Caller cleanup: restore the stack-argument space */
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, I386MREG_ESP),
		         imm(fm, MT_I64, -(int64_t)stk));
}

/* ---- selret: return lowering ------------------------------------------- */

static void
mabi_selret(MFnM *fm, MOut *o, MInsM *term)
{
	MVal *s0 = term->src[0];
	if (term->td) {
		/* Aggregate return */
		if (!s0) {
			/* dead path */
			term->op = MMOP_RET;
			term->src[0] = 0;
			term->td = 0;
			return;
		}
		uint32_t sz = term->td->size;
		if (sz <= 4) {
			/* ≤4 bytes: return in EAX */
			mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, I386MREG_EAX),
			          maddr(s0, 0, 1, 0), 0);
		} else if (sz <= 8) {
			/* ≤8 bytes: return in EAX:EDX (low:EAX, high:EDX) */
			mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, I386MREG_EAX),
			          maddr(s0, 0, 1, 0), 0);
			mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, I386MREG_EDX),
			          maddr(s0, 0, 1, 4), 0);
		} else {
			/* sret: copy the aggregate data into the hidden return
			 * buffer.  Reload the sret pad pointer from the alloca
			 * slot stashed by selpar. */
			if (fm->sret_pad) {
				MVal *rdi = tmp(fm, MT_PTR, "abi");
				mout_addr(o, MMOP_LOAD, MT_PTR, rdi,
				          maddr(fm->sret_pad, 0, 1, 0), 0);
				mout_blit(fm, o, rdi, s0, sz);
				/* EAX = return buffer pointer (per cdecl convention) */
				mout(o, MMOP_MOV, MT_PTR, reg(fm, I386MREG_EAX), rdi, 0);
			}
		}
		term->op = MMOP_RET;
		term->src[0] = 0;
		term->td = 0;
		return;
	}

	if (!s0) {
		term->op = MMOP_RET;
		term->src[0] = 0;
		term->td = 0;
		return;
	}
	bool isf = s0->type == MT_F32 || s0->type == MT_F64;
	bool ret64 = false;
	if (s0->type == MT_I64) {
		/* i64 return: EAX (low 32) + EDX (high 32). */
		if (s0->kind == MV_CONST && s0->con) {
			/* Immediate constant return (e.g. `return 42`): load the
			 * low/high 32-bit halves directly as cdecl i32 immediates.
			 * A constant has no stack slot, so loading off s0->slot would
			 * read garbage.  Load the high half into EDX first, then the
			 * low half into EAX (memit's i32 const->mov materializes the
			 * immediate through the %eax scratch, so loading the low half
			 * last keeps the return value in EAX intact). */
			int64_t cv = s0->con->u.i;
			mout(o, MMOP_MOV, MT_I32, reg(fm, I386MREG_EDX),
			     mval_const(fm->host, MT_I32,
			                imm(fm, MT_I32, (int32_t)(cv >> 32))), 0);
			mout(o, MMOP_MOV, MT_I32, reg(fm, I386MREG_EAX),
			     mval_const(fm->host, MT_I32,
			                imm(fm, MT_I32, (int32_t)cv)), 0);
		} else {
			/* Slot/register-resident value: leave s0 in term so the
			 * emitter's MMOP_RET loads both halves via i64_base
			 * (uniformly handling a constant, register-resident, or
			 * slot-resident value).  Loading EAX/EDX off s0->slot here is
			 * wrong for values whose slot is still -1 (x86-i64param). */
			term->src[0] = s0;
			ret64 = true;
		}
	} else {
		MVal *rreg = isf ? reg(fm, I386MREG_XMM0) : reg(fm, I386MREG_EAX);
		mout(o, MMOP_MOV, s0->type, rreg, s0, 0);
	}
	term->op = MMOP_RET;
	if (!ret64)
		term->src[0] = 0;
	term->td = 0;
}

/* ---- varargs ------------------------------------------------------------- */

/* i386 cdecl va_start: all arguments are on the stack.  va_list is a
 * simple pointer into the caller's stack frame.  The MIR-level va_list
 * is a pointer-sized value; we store the address of the first unnamed
 * stack argument (ebp + vafa).  If the function is not vararg, vafa is
 * 0 and this is unreachable. */
static void
mabi_vastart(MFnM *fm, MOut *o, MVal *ap, uint32_t vafa)
{
	(void)fm;
	/* ap = ebp + vafa  (address of the first stack vararg) */
	MVal *base = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LEA, MT_PTR, base,
	          maddr(reg(fm, I386MREG_EBP), 0, 1, vafa), 0);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 0), base);
}

/* i386 cdecl va_arg: load the next argument from the va_list pointer,
 * then advance the pointer past the argument.  The va_list is a pointer
 * to the next argument on the stack.  All arguments are at least 4 bytes
 * (32-bit). */
static void
mabi_vaarg(MFnM *fm, MOut *o, MInsM *in)
{
	MVal *ap = in->src[0];
	MVal *dst = in->dst;
	MType dt = in->dtype;
	/* i386 cdecl: every stack argument occupies at least 4 bytes, and
	 * 64-bit values (i64 and double) occupy 8.  The advance must use the
	 * argument's true stack footprint, not the register width. */
	int slotsize = (dt == MT_I64 || dt == MT_F64) ? 8 : 4;

	/* Load the current va_list value (pointer to next arg) */
	MVal *cur = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LOAD, MT_PTR, cur, maddr(ap, 0, 1, 0), 0);

	/* Load the argument value */
	if (dt == MT_I64) {
		/* i64: a single MT_I64 load from [cur] into dst.  The emitter's
		 * emit_load writes both halves into dst's frame slot via
		 * i64_dst_base (dst is phislot-forced to a slot), rather than
		 * hard-wiring dst->slot (= -1 until regalloc) like the old
		 * two-i32-load split. */
		mout_addr(o, MMOP_LOAD, MT_I64, dst, maddr(cur, 0, 1, 0), 0);
	} else {
		mout_addr(o, MMOP_LOAD, dt, dst, maddr(cur, 0, 1, 0), 0);
	}

	/* Advance the va_list pointer (must use mval_const, not mout_cst,
	 * because the i386 emitter reads src[0]/src[1], not cst, for ADD) */
	MVal *adv = tmp(fm, MT_PTR, "va");
	{
		/* i386 is ILP32; the pointer type is 32-bit, so the advance
		 * constant must also be 32-bit (MT_I32), not MT_I64. */
		MVal *sizv = mval_const(fm->host, MT_I32,
		                       imm(fm, MT_I32, slotsize));
		mout(o, MMOP_ADD, MT_PTR, adv, cur, sizv);
	}
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 0), adv);
}

/* ---- block rewrite ------------------------------------------------------ */

static void
mabi_block(MFnM *fm, MBlkM *b, uint32_t start, MOut *o)
{
	MInsM *args = 0;
	uint32_t nargs = 0, cargs = 0;

	for (uint32_t k = start; k < b->nins; k++) {
		MInsM *in = &b->ins[k];
		if (in->op == MMOP_ARG) {
			if (nargs == cargs) {
				cargs = cargs ? cargs * 2 : 8;
				args = realloc(args, cargs * sizeof *args);
			}
			args[nargs++] = *in;
			continue;
		}
		if (in->op == MMOP_CALL) {
			mabi_selcall(fm, o, args, nargs, in);
			nargs = 0;
			continue;
		}
		if (in->op == MMOP_VASTART) {
			mabi_vastart(fm, o, in->src[0], fm->vafa);
			continue;
		}
		if (in->op == MMOP_VAARG) {
			mabi_vaarg(fm, o, in);
			continue;
		}
		mout(o, in->op, in->dtype, in->dst, in->src[0], in->src[1]);
		o->ins[o->nins - 1].addr = in->addr;
		o->ins[o->nins - 1].cst = in->cst;
		o->ins[o->nins - 1].cc = in->cc;
		o->ins[o->nins - 1].td = in->td;
		o->ins[o->nins - 1].extra = in->extra;
	}
	free(args);

	if (b->term.op == MMOP_RET && (b->term.src[0] || b->term.td))
		mabi_selret(fm, o, &b->term);
}

void
mfnm_abi_i386(MFnM *fm)
{
	if (fm->start) {
		MBlkM *b = fm->start;
		MOut o = {.b = b};
		uint32_t start = 0;
		while (start < b->nins && b->ins[start].op == MMOP_PARM)
			start++;
		mabi_selpar(fm, &o, b->ins, start, &fm->vafa);
		mabi_block(fm, b, start, &o);
		mout_end(&o);
	}
	for (MBlkM *b = fm->link; b; b = b->link) {
		if (b == fm->start)
			continue;
		MOut o = {.b = b};
		mabi_block(fm, b, 0, &o);
		mout_end(&o);
	}
}