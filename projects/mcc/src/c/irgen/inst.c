/* inst.c — Instruction construction.
 *
 * `mkinst` allocates a `struct inst` and assigns a fresh temporary id to
 * its result (if the instruction produces a value). `funcinst` is the
 * top-level helper called by every IR builder in this module: it appends
 * the instruction to the current block's instruction list, automatically
 * opening a fresh "dead" block when the current block is already closed
 * by a jump (so subsequent instructions become unreachable but still
 * well-formed for the emit pass). */
#include "irgen.h"

void
functemp(struct func *f, struct value *v)
{
	v->kind = VALUE_TEMP;
	v->u.name = NULL;
	v->id = ++f->lastid;
}

static struct inst *
mkinst(struct func *f, int op, int class, struct value *arg0, struct value *arg1)
{
	struct inst *inst;

	inst = xmalloc(sizeof(*inst));
	inst->kind = op;
	inst->class = class;
	inst->flags = 0;
	inst->arg[0] = arg0;
	inst->arg[1] = arg1;
	if (class && op != IARG)
		functemp(f, &inst->res);
	else
		inst->res.kind = VALUE_NONE;
	return inst;
}

struct value *
funcinst(struct func *f, int op, int class, struct value *arg0, struct value *arg1)
{
	struct inst *inst;
	struct block *b;

	if (f->end->jump.kind) {
		b = mkblock("dead");
		funclabel(f, b);
	}
	inst = mkinst(f, op, class, arg0, arg1);
	arrayaddptr(&f->end->insts, inst);
	return &inst->res;
}
