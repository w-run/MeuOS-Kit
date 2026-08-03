/* func.c — Function IR construction: entry, parameters, control flow.
 *
 * `mkfunc` allocates the `struct func` for a C function definition,
 * creates the entry block, materializes parameter temporaries (either
 * by reusing `d->value` when the frontend already bound it, or by
 * `funcalloc`-ing a stack slot and storing the parameter into it), and
 * injects the implicit `__func__` literal. The control-flow builders
 * (funclabel/funcjmp/funcjnz/funcret/funchlt/funcgoto) populate
 * `struct block`'s jump slot; emitfunc reads them to emit IR jump
 * instructions. */
#include "irgen.h"

/* Current function context for statement-expression ({}) parsing.
 * Set by stmt() before entering expression parsing; used by
 * parse_stmt_expr_body() to pass stmt() control-flow constructs. */
struct func *curfunc;struct func *
mkfunc(struct decl *decl, char *name, struct type *t, struct scope *s)
{
	struct func *f;
	struct decl *d;
	struct value *v;

	f = xmalloc(sizeof(*f));
	f->decl = decl;
	f->name = name;
	f->type = t;
	f->bodyend = (struct location){0};
	f->start = f->end = mkblock("start");
	f->lastid = 0;
	mapinit(&f->gotos, 8);
	emittype(t->base);

	/* allocate space for parameters */
	f->paramtemps = xreallocarray(NULL, t->u.func.nparam, sizeof *f->paramtemps);
	for (d = t->u.func.params, v = f->paramtemps; d; d = d->next, ++v) {
		emittype(d->type);
		functemp(f, v);
		if(!d->name)
			continue;
		if (d->type->value) {
			d->value = v;
		} else {
			funcalloc(f, d);
			funcstore(f, d->type, QUALNONE, (struct lvalue){d->value}, v);
		}
	}

	t = mkarraytype(&typechar, QUALCONST, strlen(name) + 1);
	d = mkdecl("__func__", DECLOBJECT, t, QUALNONE, LINKNONE);
	d->u.obj.storage = SDSTATIC;
	d->value = mkglobal(d);
	scopeputdecl(s, d);
	f->namedecl = d;

	funclabel(f, mkblock("body"));

	return f;
}

void
delfunc(struct func *f)
{
	struct block *b;
	struct inst **inst;

	while (b = f->start) {
		f->start = b->next;
		arrayforeach (&b->insts, inst)
			free(*inst);
		free(b->insts.val);
		free(b);
	}
	mapfree(&f->gotos, free);
	free(f);
}

struct type *
functype(struct func *f)
{
	return f->type;
}

void
funclabel(struct func *f, struct block *b)
{
	f->end->next = b;
	f->end = b;
}

void
funcjmp(struct func *f, struct block *l)
{
	struct block *b = f->end;

	if (!b->jump.kind) {
		b->jump.kind = JUMP_JMP;
		b->jump.blk[0] = l;
	}
}

void
funcjnz(struct func *f, struct value *v, struct type *t, struct block *l1,
        struct block *l2)
{
	struct block *b = f->end;

	if (b->jump.kind)
		return;
	if (t) {
		assert(t->prop & PROPSCALAR);
		/*
		Ideally we would just do this conversion unconditionally,
		but IR is not currently able to optimize the conversion
		away for int.
		*/
		if (t->prop & PROPINT && t->size < 4)
			v = convert(f, &typeint, t, v);
		else if (t->prop & PROPFLOAT || t->size > 4)
			v = convert(f, &typebool, t, v);
	}
	b->jump.kind = JUMP_JNZ;
	b->jump.arg = v;
	b->jump.blk[0] = l1;
	b->jump.blk[1] = l2;
}

void
funcret(struct func *f, struct value *v)
{
	struct block *b = f->end;

	if (!b->jump.kind) {
		b->jump.kind = JUMP_RET;
		b->jump.arg = v;
	}
}

void
funchlt(struct func *f)
{
	struct block *b = f->end;

	if (!b->jump.kind)
		b->jump.kind = JUMP_HLT;
}

struct gotolabel *
funcgoto(struct func *f, char *name)
{
	struct gotolabel *g;
	struct mapkey key;
	size_t idx;

	mapkey(&key, name, strlen(name));
	if (mapput(&f->gotos, &key, &idx)) {
		g = xmalloc(sizeof(*g));
		g->label = mkblock(name);
		f->gotos.vals[idx].p = g;
	}

	return f->gotos.vals[idx].p;
}

void
funcset_bodyend(struct func *f, struct location loc)
{
	f->bodyend = loc;
}

struct location
funcget_bodyend(struct func *f)
{
	return f->bodyend;
}

/* Return true if control can reach the end of the function body without a
 * return — i.e. the final block is reachable and falls off (JUMP_NONE).
 * Walks the block CFG from the entry: JUMP_JMP/JUMP_JNZ follow their
 * targets, JUMP_NONE falls through to the next block (or off the function
 * for the final block), JUMP_RET/JUMP_HLT are terminal.  Dead blocks after
 * a return, and the unreachable join of an infinite loop (`while (1) {}`),
 * are correctly treated as non-fall-through. */
bool
func_falls_off_end(struct func *f)
{
	struct array work = {0}, seen = {0};
	struct block **s;
	size_t i;

	if (f->end->jump.kind != JUMP_NONE)
		return false;
	arrayaddptr(&work, f->start);
	while (work.len) {
		struct block **w = work.val;
		struct block *b = w[work.len / sizeof b - 1];
		bool skip = false;

		work.len -= sizeof b;   /* struct array len is a byte count */
		arrayforeach (&seen, s)
			if (*s == b) {
				skip = true;
				break;
			}
		if (skip)
			continue;
		arrayaddptr(&seen, b);
		if (b == f->end)
			return true;   /* reachable final block falls off the function */
		switch (b->jump.kind) {
		case JUMP_JMP:
			arrayaddptr(&work, b->jump.blk[0]);
			break;
		case JUMP_JNZ:
			/* A constant condition (e.g. `while (1)`, `if (0)`) makes
			 * the untaken branch unreachable, so do not follow it. */
			if (b->jump.arg && b->jump.arg->kind == VALUE_INTCONST)
				arrayaddptr(&work,
				    b->jump.arg->u.i ? b->jump.blk[0] : b->jump.blk[1]);
			else {
				arrayaddptr(&work, b->jump.blk[0]);
				arrayaddptr(&work, b->jump.blk[1]);
			}
			break;
		case JUMP_NONE:
			if (b->next)
				arrayaddptr(&work, b->next);
			break;
		default: /* JUMP_RET / JUMP_HLT are terminal */
			break;
		}
	}
	free(work.val);
	free(seen.val);
	return false;
}
