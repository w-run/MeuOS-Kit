/* print.c — MIR debug dump (-dmir). Prints a full MFn: types, constants,
 * values, blocks, instructions, phis, terminators. */
#include <stdio.h>
#include <string.h>

#include "mir.h"

static void print_const(FILE *f, MConst *c);

static const char *mtype_name(MType t)
{
	static const char *names[MT_NTYPE] = {
		[MT_NONE] = "none", [MT_VOID] = "void",
		[MT_I8] = "i8",  [MT_I16] = "i16", [MT_I32] = "i32", [MT_I64] = "i64",
		[MT_F32] = "f32", [MT_F64] = "f64",
		[MT_PTR] = "ptr", [MT_AGG] = "agg",
	};
	return (unsigned)t < MT_NTYPE ? names[t] : "?";
}

static const char *mop_name(MOP op)
{
	static const char *names[MOP_NOP] = {
		[MOP_NONE] = "none",
		[MOP_ADD] = "add", [MOP_SUB] = "sub", [MOP_MUL] = "mul",
		[MOP_DIV] = "div", [MOP_UDIV] = "udiv", [MOP_REM] = "rem",
		[MOP_UREM] = "urem", [MOP_NEG] = "neg", [MOP_AND] = "and",
		[MOP_OR] = "or", [MOP_XOR] = "xor",
		[MOP_SHL] = "shl", [MOP_SHR] = "shr", [MOP_SAR] = "sar",
		[MOP_CEQ] = "ceq", [MOP_CNE] = "cne",
		[MOP_CSLT] = "cslt", [MOP_CSLE] = "csle",
		[MOP_CSGT] = "csgt", [MOP_CSGE] = "csge",
		[MOP_CULT] = "cult", [MOP_CULE] = "cule",
		[MOP_CUGT] = "cugt", [MOP_CUGE] = "cuge",
		[MOP_CFEQ] = "cfeq", [MOP_CFNE] = "cfne",
		[MOP_CFLT] = "cflt", [MOP_CFLE] = "cfle",
		[MOP_CFGT] = "cfgt", [MOP_CFGE] = "cfge",
		[MOP_LOAD] = "load", [MOP_STORE] = "store",
		[MOP_ALLOCA] = "alloca",
		[MOP_SEXT] = "sext", [MOP_ZEXT] = "zext",
		[MOP_TRUNC] = "trunc", [MOP_CAST] = "cast",
		[MOP_F2I] = "f2i", [MOP_UF2I] = "uf2i", [MOP_I2F] = "i2f", [MOP_UI2F] = "ui2f",
		[MOP_FEXT] = "fext", [MOP_FTRUNC] = "ftrunc",
		[MOP_JMP] = "jmp", [MOP_JNZ] = "jnz",
		[MOP_RET] = "ret", [MOP_CALL] = "call",
		[MOP_ARG] = "arg", [MOP_PAR] = "par",
		[MOP_VARARG] = "vararg",
		[MOP_PHI] = "phi", [MOP_COPY] = "copy",
		[MOP_VASTART] = "vastart", [MOP_VAARG] = "vaarg",
		[MOP_SALLOC] = "salloc",
		[MOP_EXTRA] = "extra",
	};
	return (unsigned)op < MOP_NOP ? names[op] : "?";
}

static void print_val(FILE *f, MVal *v)
{
	if (!v) {
		fputs("(null)", f);
		return;
	}
	switch (v->kind) {
	case MV_TEMP:
		fprintf(f, "%%v%d", v->id);
		break;
	case MV_GLOBAL:
		fprintf(f, "@%s", v->sym ? v->sym : "?");
		break;
	case MV_CONST:
		fprintf(f, "$c%d", v->id);
		if (v->con)
			print_const(f, v->con);
		break;
	case MV_TYPE:
		fprintf(f, "!t%d", v->td ? v->td->id : -1);
		break;
	case MV_LABEL:
		fprintf(f, "&%s", v->defblk && v->defblk->name ? v->defblk->name : "?");
		break;
	case MV_REG:
		fprintf(f, "%%%s", v->name ? v->name : "reg");
		break;
	default:
		fprintf(f, "?");
		break;
	}
}

static void print_ref(FILE *f, MRef r)
{
	if (r.val)
		print_val(f, r.val);
	else if (r.con)
		fprintf(f, "$c%d", r.con->id);
	else
		fputs("_", f);
}

static void print_const(FILE *f, MConst *c)
{
	if (!c) {
		fputs("(null)", f);
		return;
	}
	switch (c->kind) {
	case MC_INT:
		fprintf(f, "(%s)%lld", mtype_name(c->type), (long long)c->u.i);
		break;
	case MC_FLT:
		if (c->type == MT_F32)
			fprintf(f, "(%s)%f", mtype_name(c->type), (double)c->u.s);
		else
			fprintf(f, "(%s)%f", mtype_name(c->type), c->u.d);
		break;
	case MC_ADDR:
		fprintf(f, "(%s)&%s%+lld%s", mtype_name(c->type),
		        c->u.addr.sym ? c->u.addr.sym : "?", (long long)c->u.addr.off,
		        c->u.addr.tls ? " [tls]" : "");
		break;
	default:
		fputs("undef", f);
		break;
	}
}

static void print_type_desc(FILE *f, MTypeDesc *td)
{
	fprintf(f, "!t%d = ", td->id);
	if (td->is_array) {
		fprintf(f, "array [%llu x ", (unsigned long long)td->nelem);
		if (td->elem_desc)
			fprintf(f, "!t%d", td->elem_desc->id);
		else
			fputs(mtype_name(td->elem_type), f);
		fputs("]", f);
		return;
	}
	fprintf(f, "%s %s (size %llu align %d){", td->is_union ? "union" : "struct",
	        td->name ? td->name : "?", (unsigned long long)td->size, td->align);
	for (uint32_t i = 0; i < td->nfield; i++) {
		MField *fl = &td->field[i];
		fputs(i ? ", " : " ", f);
		fprintf(f, "%s @%lld", fl->name ? fl->name : "?", (long long)fl->offset);
		if (fl->type == MT_AGG)
			fprintf(f, " [!t%d]", fl->sub ? fl->sub->id : -1);
		else
			fprintf(f, " (%s)", mtype_name(fl->type));
		if (fl->bits)
			fprintf(f, " bits[%d:%d]", fl->bitoff, fl->bits);
	}
	fputs(" }", f);
}

static void dump_blk(FILE *f, MBlk *b)
{
	fprintf(f, "\nblock %s (id %u) preds[%u]{%s%s}\n",
	        b->name ? b->name : "?", b->id, b->npred,
	        b->npred > 0 && b->pred[0] ? (b->pred[0]->name ? b->pred[0]->name : "?") : "-",
	        b->npred > 1 && b->pred[1] ? (b->pred[1]->name ? b->pred[1]->name : "?") : "");

	for (MPhi *p = b->phi; p; p = p->link) {
		fputs("  phi ", f);
		print_val(f, p->dst);
		fprintf(f, " (%s) = phi {", mtype_name(p->dtype));
		for (uint32_t i = 0; i < p->narg; i++) {
			fputs(i ? ", " : " ", f);
			print_val(f, p->arg[i]);
			fprintf(f, "[%s]", p->blk[i] && p->blk[i]->name ? p->blk[i]->name : "?");
		}
		fputs(" }\n", f);
	}

	for (uint32_t i = 0; i < b->nins; i++) {
		MIns *in = &b->ins[i];
		fputs("  ", f);
		if (in->dst && in->dst->kind == MV_TEMP) {
			print_val(f, in->dst);
			fputs(" = ", f);
		} else {
			fputs("      ", f);
		}
		fprintf(f, "%s (%s)", mop_name(in->op), mtype_name(in->dtype));
		if (in->src[0].val || in->src[0].con) {
			fputs(" ", f);
			print_ref(f, in->src[0]);
		}
		if (in->src[1].val || in->src[1].con) {
			fputs(", ", f);
			print_ref(f, in->src[1]);
		}
		if (in->extra)
			fprintf(f, " extra=%#llx", (unsigned long long)in->extra);
		fputs("\n", f);
	}

	fputs("  term ", f);
	switch (b->term.op) {
	case MOP_JMP:
		fprintf(f, "jmp %s\n", b->s1 && b->s1->name ? b->s1->name : "?");
		break;
	case MOP_JNZ:
		fputs("jnz ", f);
		print_ref(f, b->term.src[0]);
		fprintf(f, " -> %s / %s\n",
		        b->s1 && b->s1->name ? b->s1->name : "?",
		        b->s2 && b->s2->name ? b->s2->name : "?");
		break;
	case MOP_RET:
		fputs("ret ", f);
		print_ref(f, b->term.src[0]);
		fputs("\n", f);
		break;
	case MOP_CALL:
		fputs("call ", f);
		print_ref(f, b->term.src[0]);
		fputs("\n", f);
		break;
	default:
		fputs("(none)\n", f);
		break;
	}
}

void mfn_dump(MFn *fn, FILE *out)
{
	fprintf(out, "function %s (optlevel %d, nblk %u)\n",
	        fn->name ? fn->name : "?", fn->optlevel, fn->nblk);

	if (fn->ntyp) {
		fputs("types:\n", out);
		for (uint32_t i = 0; i < fn->ntyp; i++) {
			fputs("  ", out);
			print_type_desc(out, fn->typ[i]);
			fputs("\n", out);
		}
	}
	if (fn->ncon) {
		fputs("constants:\n", out);
		for (uint32_t i = 0; i < fn->ncon; i++) {
			fputs("  ", out);
			print_const(out, fn->con[i]);
			fputs("\n", out);
		}
	}
	if (fn->nval) {
		fputs("values:\n", out);
		for (uint32_t i = 0; i < fn->nval; i++) {
			MVal *v = fn->val[i];
			fputs("  ", out);
			print_val(out, v);
			fprintf(out, " (%s)", mtype_name(v->type));
			if (v->name)
				fprintf(out, " '%s'", v->name);
			fputs("\n", out);
		}
	}

	/* dump blocks in list order */
	for (MBlk *b = fn->link; b; b = b->link)
		dump_blk(out, b);
}
