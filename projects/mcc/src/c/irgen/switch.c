/* switch.c — Binary-search lowering for C `switch` statements.
 *
 * The frontend emits switch as a balanced binary search tree of
 * conditional jumps (one per case). `casesearch` recursively walks the
 * tree and emits ICEQW/ICEQL (==) + ICULTW/ICULTL (<) comparisons with
 * `funcjnz`, recursing into the left/right subtrees. This is the same
 * lowering the reference cproc/qbe used; a jump-table optimization can be layered on
 * later by inspecting case density. */
#include "irgen.h"

static void
casesearch(struct func *f, int class, struct value *v, struct switchcase *c,
           struct block *defaultlabel, unsigned long long min,
           unsigned long long max)
{
	struct value *res, *key;
	struct block *label[3];

	if (!c) {
		funcjmp(f, defaultlabel);
		return;
	}
	if (min == max) {
		funcjmp(f, c->body);
		return;
	}
	label[0] = mkblock("switch_ne");
	label[1] = mkblock("switch_lt");
	label[2] = mkblock("switch_gt");

	key = mkintconst(c->node.key);
	res = funcinst(f, class == 'w' ? ICEQW : ICEQL, 'w', v, key);
	funcjnz(f, res, NULL, c->body, label[0]);
	funclabel(f, label[0]);
	res = funcinst(f, class == 'w' ? ICULTW : ICULTL, 'w', v, key);
	funcjnz(f, res, NULL, label[1], label[2]);
	funclabel(f, label[1]);
	casesearch(f, class, v, c->node.child[0], defaultlabel, min, c->node.key - 1);
	funclabel(f, label[2]);
	casesearch(f, class, v, c->node.child[1], defaultlabel, c->node.key + 1, max);
}

void
funcswitch(struct func *f, struct value *v, struct switchcases *c,
           struct block *defaultlabel)
{
	casesearch(f, irtype(c->type).base, v, c->root, defaultlabel, 0, -1);
}
