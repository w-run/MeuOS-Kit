/* build.c — MIR construction API: functions, blocks, values, instructions.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

void *m_alloc(MFn *, size_t);
char *mx_strdup(const char *);

/* ---- functions --------------------------------------------------------- */

MFn *mfn_new(const char *name, int optlevel)
{
	MFn *fn = calloc(1, sizeof *fn);
	fn->name = name ? mx_strdup(name) : 0;
	fn->optlevel = optlevel;
	fn->retty = -1;
	fn->slot = 0;
	return fn;
}

MBlk *mblk_new(MFn *fn, const char *name)
{
	MBlk *b = calloc(1, sizeof *b);
	b->id = fn->nblk;
	b->name = name ? mx_strdup(name) : 0;
	b->term.op = MOP_NONE;
	return b;
}

void mfn_addblk(MFn *fn, MBlk *b)
{
	b->id = fn->nblk;
	if (fn->nblk == 0)
		fn->start = b;
	b->link = fn->link;
	fn->link = b;
	fn->nblk++;
}

uint32_t mfn_addtype(MFn *fn, MTypeDesc *td)
{
	td->id = fn->ntyp;
	fn->typ = realloc(fn->typ, (fn->ntyp + 1) * sizeof *fn->typ);
	fn->typ[fn->ntyp++] = td;
	return td->id;
}

/* ---- values ------------------------------------------------------------ */

MVal *mval_new(MFn *fn, MValKind kind, MType t, MTypeDesc *td,
               const char *name)
{
	MVal *v = calloc(1, sizeof *v);
	v->id = fn->nval;
	v->kind = kind;
	v->type = t;
	v->td = td;
	v->name = name ? mx_strdup(name) : 0;
	v->slot = -1;
	v->hint = -1;
	v->lirtmp = -1;
	fn->val = realloc(fn->val, (fn->nval + 1) * sizeof *fn->val);
	fn->val[fn->nval++] = v;
	return v;
}

MVal *mval_const(MFn *fn, MType t, MConst *c)
{
	MVal *v = mval_new(fn, MV_CONST, t, 0, "const");
	v->kind = MV_CONST;
	v->con = c;
	return v;
}

MVal *mval_global(MFn *fn, const char *sym, bool isext, bool tls)
{
	MVal *v = mval_new(fn, MV_GLOBAL, MT_PTR, 0, sym);
	v->sym = mx_strdup(sym);
	v->hint = isext ? 1 : 0;
	v->tls = tls;
	v->isext = isext;
	return v;
}

MVal *mval_type(MFn *fn, MTypeDesc *td)
{
	MVal *v = mval_new(fn, MV_TYPE, MT_AGG, td, td->name ? td->name : "type");
	return v;
}

MVal *mval_label(MFn *fn, MBlk *b)
{
	MVal *v = mval_new(fn, MV_LABEL, MT_NONE, 0, b->name ? b->name : "label");
	v->defblk = b;
	return v;
}

/* ---- instructions ------------------------------------------------------ */

static MIns *ins_alloc(MFn *fn, MBlk *b, MOP op, MType dtype, MVal *dst)
{
	if (b->nins == b->cins) {
		b->cins = b->cins ? b->cins * 2 : 16;
		b->ins = realloc(b->ins, b->cins * sizeof *b->ins);
	}
	MIns *in = &b->ins[b->nins++];
	memset(in, 0, sizeof *in);
	in->id = fn->nval + fn->ncon + fn->nblk; /* debug only */
	in->op = op;
	in->dtype = dtype;
	in->dst = dst;
	in->blk = b;
	if (dst && dst->kind == MV_TEMP) {
		dst->def = in;
		dst->defblk = b;
	}
	return in;
}

MIns *madd(MFn *fn, MBlk *b, MOP op, MType dtype, MVal *dst,
           MRef a0, MRef a1)
{
	MIns *in = ins_alloc(fn, b, op, dtype, dst);
	in->src[0] = a0;
	in->src[1] = a1;
	return in;
}

MIns *madd0(MFn *fn, MBlk *b, MOP op, MType dtype, MVal *dst)
{
	return ins_alloc(fn, b, op, dtype, dst);
}

MIns *madd1(MFn *fn, MBlk *b, MOP op, MType dtype, MVal *dst, MRef a0)
{
	MIns *in = ins_alloc(fn, b, op, dtype, dst);
	in->src[0] = a0;
	return in;
}

void mterm(MFn *fn, MBlk *b, MOP op, MRef a0, MBlk *s1, MBlk *s2)
{
	(void)fn;
	b->term.op = op;
	b->term.src[0] = a0;
	b->term.blk = b;
	b->s1 = s1;
	b->s2 = s2;
	if (s1)
		b->term.dtype = MT_PTR; /* label type marker for dump */
}

void mret(MFn *fn, MBlk *b, MRef a0)
{
	(void)fn;
	b->term.op = MOP_RET;
	b->term.src[0] = a0;
	b->term.blk = b;
}

void mretvoid(MFn *fn, MBlk *b)
{
	(void)fn;
	b->term.op = MOP_RET;
	b->term.src[0] = (MRef){0};
	b->term.blk = b;
}

MVal *mphi_add(MFn *fn, MBlk *b, MType dtype, MVal *dst)
{
	MPhi *p = calloc(1, sizeof *p);
	p->id = fn->nblk * 1000 + b->nins;
	p->dst = dst;
	p->dtype = dtype;
	p->visit = 0;
	p->link = b->phi;
	b->phi = p;
	if (dst)
		dst->defphi = p;
	return dst;
}

/* ---- teardown ---------------------------------------------------------- */

void mfn_free(MFn *fn)
{
	if (!fn)
		return;
	for (MBlk *b = fn->link; b;) {
		MBlk *next = b->link;
		free(b->name);
		free(b->ins);
		free(b->pred);
		free(b->fron);
		for (MPhi *p = b->phi; p;) {
			MPhi *pn = p->link;
			free(p->arg);
			free(p->blk);
			free(p);
			p = pn;
		}
		free(b);
		b = next;
	}
	for (uint32_t i = 0; i < fn->nval; i++) {
		free(fn->val[i]->use);
		free((void *)fn->val[i]->name);
		free((void *)fn->val[i]->sym);
		free(fn->val[i]);
	}
	free(fn->val);
	/* constants are arena-allocated; the arena is freed below */
	free(fn->con);
	for (uint32_t i = 0; i < fn->ntyp; i++) {
		MTypeDesc *td = fn->typ[i];
		free((char *)td->name);
		for (uint32_t j = 0; j < td->nfield; j++) {
			free((char *)td->field[j].name);
		}
		free(td->field);
		free(td);
	}
	free(fn->typ);
	free(fn->param);
	free((char *)fn->name);
	m_arena_free(fn);
	free(fn);
}
