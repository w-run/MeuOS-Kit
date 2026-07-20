/* value.c — Value / block construction helpers.
 *
 * `struct value` is the frontend's representation of a C expression result:
 * a global/static address, an integer/float constant, a temporary SSA
 * reference, or a type/label marker. `struct block` models a linear sequence
 * of instructions terminated by one jump (jmp/jnz/ret/hlt). The frontend
 * assembles a CFG of blocks via mkblock + funclabel, then emitfunc walks
 * that CFG to build the IR Fn in memory. */
#include "irgen.h"

void
switchcase(struct switchcases *cases, unsigned long long i, struct block *b)
{
	struct switchcase *c;

	c = treeinsert(&cases->root, i, sizeof(*c));
	if (!c->node.new)
		error(&tok.loc, "multiple 'case' labels with same value");
	c->body = b;
}

struct block *
mkblock(char *name)
{
	static unsigned id;
	struct block *b;

	b = xmalloc(sizeof(*b));
	b->label.kind = VALUE_LABEL;
	b->label.u.name = name;
	b->label.id = ++id;
	b->insts = (struct array){0};
	b->jump.kind = JUMP_NONE;
	b->phi.res.kind = VALUE_NONE;
	b->next = NULL;

	return b;
}

struct value *
mkglobal(struct decl *d)
{
	static unsigned id;
	struct value *v;

	v = xmalloc(sizeof(*v));
	v->kind = VALUE_GLOBAL;
	if (d->linkage == LINKEXTERN)
		v->kind |= VALUE_EXTERN;
	if (d->kind == DECLOBJECT && d->u.obj.storage == SDTHREAD)
		v->kind |= VALUE_THREAD;
	if (d->asmname) {
		v->kind |= VALUE_QUOTE;
		v->u.name = d->asmname;
		v->id = 0;
	} else {
		v->u.name = d->name;
		v->id = d->linkage == LINKNONE ? ++id : 0;
	}

	return v;
}

struct value *
mkintconst(unsigned long long n)
{
	struct value *v;

	v = xmalloc(sizeof(*v));
	v->kind = VALUE_INTCONST;
	v->u.i = n;

	return v;
}

struct value *
mkfltconst(int kind, double n)
{
	struct value *v;

	v = xmalloc(sizeof(*v));
	v->kind = kind;
	v->u.f = n;

	return v;
}

struct irtype
irtype(struct type *t)
{
	static const struct irtype
		ub = {'w', 'b', ILOADUB, ISTOREB},
		sb = {'w', 'b', ILOADSB, ISTOREB},
		uh = {'w', 'h', ILOADUH, ISTOREH},
		sh = {'w', 'h', ILOADSH, ISTOREH},
		w = {'w', 'w', ILOADW, ISTOREW},
		l = {'l', 'l', ILOADL, ISTOREL},
		s = {'s', 's', ILOADS, ISTORES},
		d = {'d', 'd', ILOADD, ISTORED},
		v = {0};

	if (t == &typevoid)
		return v;
	if (!(t->prop & PROPSCALAR))
		return l;
	switch (t->size) {
	case 1: return t->u.arith.issigned ? sb : ub;
	case 2: return t->u.arith.issigned ? sh : uh;
	case 4: return t->prop & PROPFLOAT ? s : w;
	case 8: return t->prop & PROPFLOAT ? d : l;
	case 16: fatal("long double is not yet supported");
	}
	assert(0);
}
