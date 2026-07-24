#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"

/* C23 labeled break/continue: maps label names to loop/switch targets */
#define MAX_LABELED 64
static struct {
	char *name;
	struct block *break_target;
	struct block *continue_target;
} labeled_blocks[MAX_LABELED];
static int n_labeled_blocks;

static void
register_labeled(const char *name, struct block *brk, struct block *cont)
{
	if (n_labeled_blocks < MAX_LABELED) {
		labeled_blocks[n_labeled_blocks].name = xmalloc(strlen(name) + 1);
		strcpy(labeled_blocks[n_labeled_blocks].name, name);
		labeled_blocks[n_labeled_blocks].break_target = brk;
		labeled_blocks[n_labeled_blocks].continue_target = cont;
		n_labeled_blocks++;
	}
}

static struct block *
find_labeled_break(const char *name)
{
	int i;
	for (i = n_labeled_blocks - 1; i >= 0; i--) {
		if (strcmp(labeled_blocks[i].name, name) == 0)
			return labeled_blocks[i].break_target;
	}
	return NULL;
}

static struct block *
find_labeled_continue(const char *name)
{
	int i;
	for (i = n_labeled_blocks - 1; i >= 0; i--) {
		if (strcmp(labeled_blocks[i].name, name) == 0)
			return labeled_blocks[i].continue_target;
	}
	return NULL;
}

/* Track the label name from label() so break/continue can use it */
static const char *pending_label;

/* 6.8.1 Labeled statements */
static bool
label(struct func *f, struct scope *s)
{
	char *name;
	struct gotolabel *g;
	struct block *b;
	unsigned long long i;

	attr(NULL, ATTRFALLTHROUGH);
	switch (tok.kind) {
	case TCASE:
		next();
		if (!s->switchcases)
			error(&tok.loc, "'case' label must be in switch");
		b = mkblock("switch_case");
		funclabel(f, b);
		i = intconstexpr(s, true);
		switchcase(s->switchcases, i, b);
		expect(TCOLON, "after case expression");
		break;
	case TDEFAULT:
		next();
		if (!s->switchcases)
			error(&tok.loc, "'default' label must be in switch");
		if (s->switchcases->defaultlabel)
			error(&tok.loc, "multiple 'default' labels");
		expect(TCOLON, "after 'default'");
		s->switchcases->defaultlabel = mkblock("switch_default");
		funclabel(f, s->switchcases->defaultlabel);
		break;
	default:
		if (tok.kind < TIDENT)
			return false;
		name = tokenstr(tok.kind);
		if (!peek(TCOLON))
			return false;
		g = funcgoto(f, name);
		g->defined = true;
		funclabel(f, g->label);
		pending_label = name;
		break;
	}
	return true;
}

static void
labelstmt(struct func *f, struct scope *s)
{
	const char *saved_label = NULL;

	pending_label = NULL;
	while (label(f, s)) {
		/* Last label wins for labeled break/continue */
		if (pending_label)
			saved_label = pending_label;
		pending_label = NULL;
	}
	pending_label = saved_label;
	stmt(f, s);
	pending_label = NULL;
}

/* 6.8 Statements and blocks */
void
stmt(struct func *f, struct scope *s)
{
	char *name;
	struct expr *e;
	struct type *t;
	struct value *v;
	struct block *b[4];
	struct switchcases swtch;

	curfunc = f;

	attr(NULL, ATTRFALLTHROUGH);
	switch (tok.kind) {
	/* 6.8.2 Compound statement */
	case TLBRACE:
		next();
		s = mkscope(s);
		while (tok.kind != TRBRACE) {
			if (!label(f, s) && !decl(s, f))
				stmt(f, s);
		}
		s = delscope(s);
		next();
		break;

	/* 6.8.3 Expression statement */
	case TSEMICOLON:
		next();
		break;
	default:
		e = expr(s);
		v = funcexpr(f, e);
		delexpr(e);
		expect(TSEMICOLON, "after expression statement");
		break;

	/* 6.8.4 Selection statement */
	case TIF:
		next();
		s = mkscope(s);
		expect(TLPAREN, "after 'if'");
		e = expr(s);
		t = e->type;
		if (!(t->prop & PROPSCALAR))
			error(&tok.loc, "controlling expression of if statement must have scalar type");
		b[0] = mkblock("if_true");
		b[1] = mkblock("if_false");
		funcbranch(f, e, b[0], b[1]);
		delexpr(e);
		expect(TRPAREN, "after expression");

		funclabel(f, b[0]);
		s = mkscope(s);
		labelstmt(f, s);
		s = delscope(s);

		if (consume(TELSE)) {
			b[2] = mkblock("if_join");
			funcjmp(f, b[2]);
			funclabel(f, b[1]);
			s = mkscope(s);
			labelstmt(f, s);
			s = delscope(s);
			funclabel(f, b[2]);
		} else {
			funclabel(f, b[1]);
		}
		s = delscope(s);
		break;
	case TSWITCH:
		next();

		s = mkscope(s);
		expect(TLPAREN, "after 'switch'");
		e = expr(s);
		expect(TRPAREN, "after expression");

		if (!(e->type->prop & PROPINT))
			error(&tok.loc, "controlling expression of switch statement must have integer type");
		e = exprpromote(e);

		swtch.root = NULL;
		swtch.type = e->type;
		swtch.defaultlabel = NULL;

		b[0] = mkblock("switch_cond");
		b[1] = mkblock("switch_join");

		v = funcexpr(f, e);
		funcjmp(f, b[0]);
		s = mkscope(s);
		if (pending_label) {
			register_labeled(pending_label, b[1], NULL);
			pending_label = NULL;
		}
		s->breaklabel = b[1];
		s->switchcases = &swtch;
		labelstmt(f, s);
		funcjmp(f, b[1]);

		funclabel(f, b[0]);
		funcswitch(f, v, &swtch, swtch.defaultlabel ? swtch.defaultlabel : b[1]);
		s = delscope(s);

		funclabel(f, b[1]);
		s = delscope(s);
		break;

	/* 6.8.5 Iteration statements */
	case TWHILE:
		next();
		s = mkscope(s);
		expect(TLPAREN, "after 'while'");
		e = expr(s);
		t = e->type;
		if (!(t->prop & PROPSCALAR))
			error(&tok.loc, "controlling expression of loop must have scalar type");
		expect(TRPAREN, "after expression");

		b[0] = mkblock("while_cond");
		b[1] = mkblock("while_body");
		b[2] = mkblock("while_join");

		funclabel(f, b[0]);
		funcbranch(f, e, b[1], b[2]);
		funclabel(f, b[1]);
		s = mkscope(s);
		if (pending_label) {
			register_labeled(pending_label, b[2], b[0]);
			pending_label = NULL;
		}
		s->continuelabel = b[0];
		s->breaklabel = b[2];
		labelstmt(f, s);
		s = delscope(s);
		funcjmp(f, b[0]);
		funclabel(f, b[2]);
		s = delscope(s);
		break;
	case TDO:
		next();

		b[0] = mkblock("do_body");
		b[1] = mkblock("do_cond");
		b[2] = mkblock("do_join");

		s = mkscope(s);
		s = mkscope(s);
		if (pending_label) {
			register_labeled(pending_label, b[2], b[1]);
			pending_label = NULL;
		}
		s->continuelabel = b[1];
		s->breaklabel = b[2];
		funclabel(f, b[0]);
		labelstmt(f, s);
		s = delscope(s);

		expect(TWHILE, "after 'do' statement");
		expect(TLPAREN, "after 'while'");
		funclabel(f, b[1]);
		e = expr(s);
		t = e->type;
		if (!(t->prop & PROPSCALAR))
			error(&tok.loc, "controlling expression of loop must have scalar type");
		expect(TRPAREN, "after expression");

		funcbranch(f, e, b[0], b[2]);
		funclabel(f, b[2]);
		s = delscope(s);
		expect(TSEMICOLON, "after 'do' statement");
		break;
	case TFOR:
		next();
		expect(TLPAREN, "after 'for'");
		s = mkscope(s);
		if (!decl(s, f)) {
			if (tok.kind != TSEMICOLON) {
				e = expr(s);
				funcexpr(f, e);
				delexpr(e);
			}
			expect(TSEMICOLON, NULL);
		}

		b[0] = mkblock("for_cond");
		b[1] = mkblock("for_body");
		b[2] = mkblock("for_cont");
		b[3] = mkblock("for_join");

		funclabel(f, b[0]);
		if (tok.kind != TSEMICOLON) {
			e = expr(s);
			t = e->type;
			if (!(t->prop & PROPSCALAR))
				error(&tok.loc, "controlling expression of loop must have scalar type");
			funcbranch(f, e, b[1], b[3]);
			delexpr(e);
		}
		expect(TSEMICOLON, NULL);
		e = tok.kind == TRPAREN ? NULL : expr(s);
		expect(TRPAREN, NULL);

		funclabel(f, b[1]);
		s = mkscope(s);
		if (pending_label) {
			register_labeled(pending_label, b[3], b[2]);
			pending_label = NULL;
		}
		s->breaklabel = b[3];
		s->continuelabel = b[2];
		labelstmt(f, s);
		s = delscope(s);

		funclabel(f, b[2]);
		if (e) {
			funcexpr(f, e);
			delexpr(e);
		}
		funcjmp(f, b[0]);
		funclabel(f, b[3]);
		s = delscope(s);
		break;

	/* 6.8.6 Jump statements */
	case TGOTO:
		next();
		name = expect(TIDENT, "after 'goto'");
		funcjmp(f, funcgoto(f, name)->label);
		expect(TSEMICOLON, "after 'goto' statement");
		break;
	case TCONTINUE:
		next();
		if (tok.kind >= TIDENT) {
			/* C23: continue LABEL */
			name = tokenstr(tok.kind);
			{
				struct block *target = find_labeled_continue(name);
				if (!target)
					error(&tok.loc, "label '%s' for continue not found", name);
				funcjmp(f, target);
			}
			next();
		} else {
			if (!s->continuelabel)
				error(&tok.loc, "'continue' statement must be in loop");
			funcjmp(f, s->continuelabel);
		}
		expect(TSEMICOLON, "after 'continue' statement");
		break;
	case TBREAK:
		next();
		if (tok.kind >= TIDENT) {
			/* C23: break LABEL */
			name = tokenstr(tok.kind);
			{
				struct block *target = find_labeled_break(name);
				if (!target)
					error(&tok.loc, "label '%s' for break not found", name);
				funcjmp(f, target);
			}
			next();
		} else {
			if (!s->breaklabel)
				error(&tok.loc, "'break' statement must be in loop or switch");
			funcjmp(f, s->breaklabel);
		}
		expect(TSEMICOLON, "after 'break' statement");
		break;
	case TRETURN:
		next();
		t = functype(f);
		if (t->base != &typevoid) {
			e = exprassign(expr(s), t->base);
			v = funcexpr(f, e);
			delexpr(e);
		} else {
			v = NULL;
		}
		funcret(f, v);
		expect(TSEMICOLON, "after 'return' statement");
		break;

	case T__ASM__: {
		/* Basic inline asm — no-op that compiles through.
		 * Supports __asm__ [volatile]("instructions" [: [outputs] [: [inputs] [: clobbers]]]) */
		next();
		if (tok.kind == TVOLATILE || tok.kind == TCONST || tok.kind == TRESTRICT)
			next();
		if (tok.kind == TLPAREN) {
			next();
			while (tok.kind == TSTRINGLIT) {
				next();
				if (tok.kind == TCOLON) {
					while (tok.kind != TRPAREN && tok.kind != TNEWLINE && tok.kind != TEOF) {
						if (tok.kind == TLPAREN) {
							int depth = 1;
							while (depth > 0 && tok.kind != TEOF) {
								if (tok.kind == TLPAREN) depth++;
								if (tok.kind == TRPAREN) depth--;
								if (depth > 0) next();
							}
						}
						if (tok.kind != TRPAREN) next();
					}
				}
				if (tok.kind == TCOMMA) next();
			}
			expect(TRPAREN, "after asm statement");
		}
		expect(TSEMICOLON, "after asm statement");
		break;
	}
	}
}
