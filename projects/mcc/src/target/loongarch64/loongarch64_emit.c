#include "loongarch64.h"

enum { Ki = -1, Ka = -2 };

static struct {
	short op;
	short cls;
	char *fmt;
} omap[] = {
	{ Oadd, Ki, "add.%k %=, %0, %1" }, { Oadd, Ka, "fadd.%k %=, %0, %1" },
	{ Osub, Ki, "sub.%k %=, %0, %1" }, { Osub, Ka, "fsub.%k %=, %0, %1" },
	{ Oneg, Ki, "sub.%k %=, $zero, %0" }, { Oneg, Ka, "fneg.%k %=, %0" },
	{ Omul, Ki, "mul.%k %=, %0, %1" }, { Omul, Ka, "fmul.%k %=, %0, %1" },
	{ Odiv, Ki, "div.%k %=, %0, %1" }, { Odiv, Ka, "fdiv.%k %=, %0, %1" },
	{ Orem, Ki, "mod.%k %=, %0, %1" },
	{ Oudiv, Ki, "div.%ku %=, %0, %1" }, { Ourem, Ki, "mod.%ku %=, %0, %1" },
	{ Oand, Ki, "and %=, %0, %1" }, { Oor, Ki, "or %=, %0, %1" },
	{ Oxor, Ki, "xor %=, %0, %1" },
	{ Osar, Ki, "sra.%k %=, %0, %1" }, { Oshr, Ki, "srl.%k %=, %0, %1" },
	{ Oshl, Ki, "sll.%k %=, %0, %1" },
	{ Ocsltl, Ki, "slt %=, %0, %1" }, { Ocultl, Ki, "sltu %=, %0, %1" },
	{ Ostoreb, Ki, "st.b %0, %M1" }, { Ostoreh, Ki, "st.h %0, %M1" },
	{ Ostorew, Ki, "st.w %0, %M1" }, { Ostorel, Ki, "st.d %0, %M1" },
	{ Ostores, Ks, "fst.s %0, %M1" }, { Ostored, Kd, "fst.d %0, %M1" },
	{ Oloadsb, Ki, "ld.b %=, %M0" }, { Oloadub, Ki, "ld.bu %=, %M0" },
	{ Oloadsh, Ki, "ld.h %=, %M0" }, { Oloaduh, Ki, "ld.hu %=, %M0" },
	{ Oloadsw, Ki, "ld.w %=, %M0" }, { Oloaduw, Kw, "ld.w %=, %M0" },
	{ Oloaduw, Kl, "ld.wu %=, %M0" }, { Oload, Kw, "ld.w %=, %M0" },
	{ Oload, Kl, "ld.d %=, %M0" }, { Oload, Ks, "fld.s %=, %M0" },
	{ Oload, Kd, "fld.d %=, %M0" },
	{ Oextsb, Ki, "ext.w.b %=, %0" }, { Oextub, Ki, "bstrpick.d %=, %0, 7, 0" },
	{ Oextsh, Ki, "ext.w.h %=, %0" }, { Oextuh, Ki, "bstrpick.d %=, %0, 15, 0" },
	{ Oextsw, Kl, "add.w %=, %0, $zero" }, { Oextuw, Kl, "bstrpick.d %=, %0, 31, 0" },
	{ Otruncd, Ks, "fcvt.s.d %=, %0" }, { Oexts, Kd, "fcvt.d.s %=, %0" },
	{ Ostosi, Kw, "ftintrz.w.s $ft15, %0\n\tmovfr2gr.s %=, $ft15" },
	{ Ostosi, Kl, "ftintrz.l.s $ft15, %0\n\tmovfr2gr.d %=, $ft15" },
	{ Ostoui, Kw, "ftintrz.wu.s $ft15, %0\n\tmovfr2gr.s %=, $ft15" },
	{ Ostoui, Kl, "ftintrz.lu.s $ft15, %0\n\tmovfr2gr.d %=, $ft15" },
	{ Odtosi, Kw, "ftintrz.w.d $ft15, %0\n\tmovfr2gr.s %=, $ft15" },
	{ Odtosi, Kl, "ftintrz.l.d $ft15, %0\n\tmovfr2gr.d %=, $ft15" },
	{ Odtoui, Kw, "ftintrz.wu.d $ft15, %0\n\tmovfr2gr.s %=, $ft15" },
	{ Odtoui, Kl, "ftintrz.lu.d $ft15, %0\n\tmovfr2gr.d %=, $ft15" },
	{ Oswtof, Ka, "movgr2fr.w $ft15, %0\n\tffint.%k.w %=, $ft15" },
	{ Ouwtof, Ka, "movgr2fr.w $ft15, %0\n\tffint.%k.wu %=, $ft15" },
	{ Osltof, Ka, "movgr2fr.d $ft15, %0\n\tffint.%k.l %=, $ft15" },
	{ Oultof, Ka, "movgr2fr.d $ft15, %0\n\tffint.%k.lu %=, $ft15" },
	{ Ocast, Kw, "movfr2gr.s %=, %0" }, { Ocast, Kl, "movfr2gr.d %=, %0" },
	{ Ocast, Ks, "movgr2fr.w %=, %0" }, { Ocast, Kd, "movgr2fr.d %=, %0" },
	{ Ocopy, Ki, "or %=, %0, $zero" }, { Ocopy, Ka, "fmov.%k %=, %0" },
	{ Oswap, Ki, "or %?, %0, $zero\n\tor %0, %1, $zero\n\tor %1, %?, $zero" },
	{ Oswap, Ka, "fmov.%k %?, %0\n\tfmov.%k %0, %1\n\tfmov.%k %1, %?" },
	{ Oreqz, Ki, "sltui %=, %0, 1" }, { Ornez, Ki, "sltu %=, $zero, %0" },
	{ Ocall, Kl, "jirl $ra, %0, 0" }, { NOp, 0, 0 }
};

static char *rname[] = {
	[FP] = "$fp", [SP] = "$sp", [TP] = "$tp", [RA] = "$ra",
	[T0] = "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8",
	[A0] = "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7",
	[S0] = "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7", "$s8",
	[FT0] = "$ft0", "$ft1", "$ft2", "$ft3", "$ft4", "$ft5", "$ft6", "$ft7",
	         "$ft8", "$ft9", "$ft10", "$ft11", "$ft12", "$ft13", "$ft14", "$ft15",
	[FA0] = "$fa0", "$fa1", "$fa2", "$fa3", "$fa4", "$fa5", "$fa6", "$fa7",
	[FS0] = "$fs0", "$fs1", "$fs2", "$fs3", "$fs4", "$fs5", "$fs6", "$fs7",
};

static int64_t
slot(Ref r, Fn *fn)
{
	int s = rsval(r);
	assert(s <= fn->slot);
	return s < 0 ? 8 * -s : -4 * (fn->slot - s);
}

static void
emitaddr(Con *c, FILE *f)
{
	const char *name;
	assert((c->sym.type & ~(SExt|SThr)) == SGlo
	    || c->sym.type == SGenThr);
	name = str(c->sym.id);
	if (name[0] == '"') name++;
	if (name[strlen(name)-1] == '"') {
		int len = strlen(name)-1;
		char buf[128];
		memcpy(buf, name, len);
		buf[len] = 0;
		fputs(buf, f);
	} else {
		fputs(name, f);
	}
	if (c->bits.i)
		fprintf(f, "+%"PRIi64, c->bits.i);
}

static void
emitf(char *s, Ins *i, Fn *fn, FILE *f)
{
	static char clschr[] = {'w', 'd', 's', 'd'};
	Ref r;
	Con *pc;
	int c, k;
	int64_t offset;

	fputc('\t', f);
	for (;;) {
		k = i->cls;
		while ((c = *s++) != '%') {
			if (!c) {
				fputc('\n', f);
				return;
			}
			fputc(c, f);
		}
		switch ((c = *s++)) {
		default: die("invalid escape");
		case '?': fputs(KBASE(k) == 0 ? "$t8" : "$ft15", f); break;
		case 'k': fputc(clschr[i->cls], f); break;
		case '=':
		case '0':
			r = c == '=' ? i->to : i->arg[0];
			assert(isreg(r));
			fputs(rname[r.val], f);
			break;
		case '1':
			r = i->arg[1];
			if (rtype(r) == RTmp) {
				assert(isreg(r));
				fputs(rname[r.val], f);
			} else {
				pc = &fn->con[r.val];
				assert(rtype(r) == RCon && pc->type == CBits);
				assert(pc->bits.i >= -2048 && pc->bits.i < 2048);
				fprintf(f, "%d", (int)pc->bits.i);
			}
			break;
		case 'M':
			c = *s++;
			r = i->arg[c - '0'];
			if (rtype(r) == RTmp)
				fprintf(f, "%s, 0", rname[r.val]);
			else if (rtype(r) == RSlot) {
				offset = slot(r, fn);
				assert(offset >= -2048 && offset <= 2047);
				fprintf(f, "$fp, %d", (int)offset);
			} else {
				pc = &fn->con[r.val];
				assert(rtype(r) == RCon && pc->type == CAddr);
				emitaddr(pc, f);
			}
			break;
		}
	}
}

static void
loadaddr(Con *c, char *rn, FILE *f)
{
	switch (c->sym.type) {
	case SGlo:
		fprintf(f, "\tpcalau12i %s, %%pc_hi20(", rn);
		emitaddr(c, f);
		fprintf(f, ")\n\taddi.d %s, %s, %%pc_lo12(", rn, rn);
		emitaddr(c, f);
		fputs(")\n", f);
		break;
	case SExt:
		fprintf(f, "\tpcalau12i %s, %%got_pc_hi20(", rn);
		emitaddr(c, f);
		fprintf(f, ")\n\tld.d %s, %s, %%got_pc_lo12(", rn, rn);
		emitaddr(c, f);
		fputs(")\n", f);
		break;
	case SThr:
		/* Local-exec TLS: materialize the link-time TLS offset using the
		 * LoongArch ELF psABI relocations, then add the thread pointer. */
		fprintf(f, "\tlu12i.w %s, %%le_hi20(", rn);
		emitaddr(c, f);
		fprintf(f, ")\n\tori %s, %s, %%le_lo12(", rn, rn);
		emitaddr(c, f);
		fprintf(f, ")\n\tlu32i.d %s, %%le64_lo20(", rn);
		emitaddr(c, f);
		fprintf(f, ")\n\tlu52i.d %s, %s, %%le64_hi12(", rn, rn);
		emitaddr(c, f);
		fprintf(f, ")\n\tadd.d %s, %s, $tp\n", rn, rn);
		break;
	case SExtThr:
		die("extern thread-local address unavailable on loongarch64");
	case SGenThr:
		/* general-dynamic TLS descriptor address: pcalau12i + addi.d
		 * compute the @tlsgd descriptor's address into rn (LoongArch
		 * psABI R_LARCH_TLS_GD_PC_HI20 / R_LARCH_TLS_GD_PC_LO12).  The
		 * __tls_get_addr call is emitted as a real Ocall by IR generation
		 * (irgen/emit.c valref), so it is visible to the optimizer and
		 * forces fn->leaf=0 — the prologue saves $ra, fixing the leaf-
		 * function bl/ret hazard on loongarch64. */
		fprintf(f, "\tpcalau12i %s, %%gd_pc_hi20(", rn);
		emitaddr(c, f);
		fprintf(f, ")\n\taddi.d %s,%s,%%gd_pc_lo12(", rn, rn);
		emitaddr(c, f);
		fprintf(f, ")\n");
		break;
	default:
		die("invalid address relocation class");
	}
}

static void
loadcon(Con *c, int r, int k, FILE *f)
{
	int64_t n = c->bits.i;
	if (c->type == CAddr) {
		loadaddr(c, rname[r], f);
		return;
	}
	if (!KWIDE(k))
		n = (int32_t)n;
	fprintf(f, "\tli.%c %s, %"PRIi64"\n", KWIDE(k) ? 'd' : 'w', rname[r], n);
}

static void
fixmem(Ref *pr, Fn *fn, FILE *f)
{
	Ref r = *pr;
	int64_t s;
	if (rtype(r) == RCon && fn->con[r.val].type == CAddr) {
		loadcon(&fn->con[r.val], T8, Kl, f);
		*pr = TMP(T8);
	}
	if (rtype(r) == RSlot) {
		s = slot(r, fn);
		if (s < -2048 || s > 2047) {
			fprintf(f, "\tli.d $t8, %"PRId64"\n\tadd.d $t8, $fp, $t8\n", s);
			*pr = TMP(T8);
		}
	}
}

static void
emitcmp(Ins *i, int c, FILE *f)
{
	static char *name[] = {
		[Cfeq] = "ceq", [Cfge] = "cge", [Cfgt] = "cgt", [Cfle] = "cle", [Cflt] = "clt", [Cfne] = "cne",
		[Cfo] = "cor", [Cfuo] = "cun"
	};
	/* LoongArch has no cgt/cge; swap operands and use clt/cle instead. */
	int swap = 0;
	static const char *loongarch_name[] = {
		"ceq", "cle", "clt", "cle", "clt", "cne", "cor", "cun"
	};
	c -= NCmpI;
	if (c < 0 || c >= NCmpF || !name[c])
		die("unsupported floating comparison");
	/* Cfge (c=1) → cle with swapped args; Cfgt (c=2) → clt with swapped args */
	if (c == (Cfge - Cfeq) || c == (Cfgt - Cfeq))
		swap = 1;
	fprintf(f, "\tfcmp.%s.%c $fcc0, %s, %s\n\tmovcf2gr %s, $fcc0\n",
		loongarch_name[c],
		i->cls == Ks ? 's' : 'd',
		swap ? rname[i->arg[1].val] : rname[i->arg[0].val],
		swap ? rname[i->arg[0].val] : rname[i->arg[1].val],
		rname[i->to.val]);
}

static void
emitins(Ins *i, Fn *fn, FILE *f)
{
	int o, k;
	int64_t offset;
	Con *c;
	if (iscmp(i->op, &k, &o) && o >= NCmpI) {
		emitcmp(i, o, f);
		return;
	}
	switch (i->op) {
	default:
		if (isload(i->op)) fixmem(&i->arg[0], fn, f);
		else if (isstore(i->op)) fixmem(&i->arg[1], fn, f);
		for (o = 0; omap[o].op != NOp; o++)
			if (omap[o].op == i->op &&
			    (isstore(i->op) || omap[o].cls == i->cls ||
			     omap[o].cls == Ka || (omap[o].cls == Ki && KBASE(i->cls) == 0)))
				break;
		if (omap[o].op == NOp) die("no match for %s(%c)", optab[i->op].name, "wlsd"[i->cls]);
		/* LoongArch: immediate-variant mnemonic when arg[1] is constant */
		if (!isload(i->op) && !isstore(i->op) && rtype(i->arg[1]) == RCon) {
			const char *fmt = omap[o].fmt;
			static char buf[128];
			static char cls[] = {'w','d','s','d'};
			int64_t imm = fn->con[i->arg[1].val].bits.i;
			/* Check if the immediate fits in the instruction's bounds.
			 * andi/ori/xori: 12-bit unsigned (0..4095)
			 * addi: 12-bit signed (-2048..2047)
			 * shifts: 0..63 (5-bit for w, 6-bit for d) */
			int fits = 1;
			if (fmt[0] == 'a' && fmt[1] == 'd' && fmt[2] == 'd' && fmt[3] == '.')
				fits = (imm >= -2048 && imm <= 2047);
			else if (fmt[0] == 's' && (fmt[1] == 'l' || fmt[1] == 'r' || fmt[1] == 'a'))
				fits = (imm >= 0 && imm <= 63);
			else if (fmt[0] == 'a' || fmt[0] == 'o' || fmt[0] == 'x' || strncmp(fmt, "slt", 3) == 0)
				fits = (imm >= 0 && imm <= 4095);
			if (!fits) {
				/* Large immediate: materialize in $t8, then use reg-reg form */
				fprintf(f, "\tli.d $t8, %ld\n", (long)imm);
				/* Replace %1 with $t8 in the format string */
				for (o = 0; omap[o].op != NOp && omap[o].fmt != fmt; o++);
				if (omap[o].op != NOp && strstr(fmt, "%1")) {
					char fmt2[128]; const char *p = fmt, *q;
					/* Find %1 in format and replace with $t8 */
					q = strstr(fmt, "%1");
					snprintf(fmt2, sizeof fmt2, "%.*s$t8%s", (int)(q - fmt), fmt, q + 2);
					emitf(fmt2, i, fn, f);
				} else
					emitf(fmt, i, fn, f);
				break;
			}
			if (strncmp(fmt, "add", 3) == 0 && fmt[3] == '.')
				snprintf(buf, sizeof buf, "addi.%c %%=, %0, %1", cls[i->cls]);
			else if (strncmp(fmt, "and", 3) == 0)
				snprintf(buf, sizeof buf, "andi %%=, %0, %1");
			else if (strncmp(fmt, "or ", 3) == 0)
				snprintf(buf, sizeof buf, "ori %%=, %0, %1");
			else if (strncmp(fmt, "xor", 3) == 0) {
				if (fn->con[i->arg[1].val].bits.i == 0)
					snprintf(buf, sizeof buf, "or %%=, %0, $zero");
				else
					snprintf(buf, sizeof buf, "xori %%=, %0, %1");
			} else if (strncmp(fmt, "sltu", 4) == 0)
				snprintf(buf, sizeof buf, "sltui %%=, %0, %1");
			else if (strncmp(fmt, "slt ", 4) == 0)
				snprintf(buf, sizeof buf, "slti %%=, %0, %1");
			else if (strncmp(fmt, "sll", 3) == 0)
				snprintf(buf, sizeof buf, "slli.%c %%=, %0, %1", cls[i->cls]);
			else if (strncmp(fmt, "srl", 3) == 0)
				snprintf(buf, sizeof buf, "srli.%c %%=, %0, %1", cls[i->cls]);
			else if (strncmp(fmt, "sra", 3) == 0)
				snprintf(buf, sizeof buf, "srai.%c %%=, %0, %1", cls[i->cls]);
			else {
				emitf(omap[o].fmt, i, fn, f);
				break;
			}
			emitf(buf, i, fn, f);
		} else
			emitf(omap[o].fmt, i, fn, f);
		break;
	case Onop: break;
	case Ocopy:
		if (req(i->to, i->arg[0])) break;
		if (rtype(i->to) == RSlot) {
			if (!isreg(i->arg[0])) die("unimplemented slot copy");
			i->arg[1] = i->to;
			i->to = R;
			i->op = Ostorew + i->cls;
			fixmem(&i->arg[1], fn, f);
			emitins(i, fn, f);
			break;
		}
		assert(isreg(i->to));
		if (rtype(i->arg[0]) == RCon) loadcon(&fn->con[i->arg[0].val], i->to.val, i->cls, f);
		else if (rtype(i->arg[0]) == RSlot) {
			i->op = Oload;
			emitins(i, fn, f);
		} else emitf(KBASE(i->cls) ? "fmov.%k %=, %0" : "or %=, %0, $zero", i, fn, f);
		break;
	case Oaddr:
		assert(rtype(i->arg[0]) == RSlot);
		offset = slot(i->arg[0], fn);
		if (offset >= -2048 && offset <= 2047)
			fprintf(f, "\taddi.d %s, $fp, %"PRId64"\n", rname[i->to.val], offset);
		else
			fprintf(f, "\tli.d %s, %"PRId64"\n\tadd.d %s, $fp, %s\n",
				rname[i->to.val], offset, rname[i->to.val], rname[i->to.val]);
		break;
	case Ocall:
		if (rtype(i->arg[0]) == RCon) {
			c = &fn->con[i->arg[0].val];
			if (c->type != CAddr || c->bits.i || (c->sym.type & SThr)) die("invalid call argument");
			fprintf(f, "\tbl %s\n", str(c->sym.id));
		} else emitf("jirl $ra, %0, 0", i, fn, f);
		break;
	case Osalloc:
		emitf("sub.d sp, sp, %0", i, fn, f);
		if (!req(i->to, R)) emitf("or %=, sp, zero", i, fn, f);
		break;
	case Odbgloc:
		emitdbgloc(i->arg[0].val, i->arg[1].val, f);
		break;
	}
}

void
la64_emitfn(Fn *fn, FILE *f)
{
	static int id0;
	int frame, off, lbl, neg, r, *pr;
	Blk *b, *s;
	Ins *i, ii;
	emitfnlnk(fn->name, &fn->lnk, f);
	frame = (16 + 4 * fn->slot + 15) & -16;
	for (pr = la64_rclob; *pr >= 0; pr++) if (fn->reg & BIT(*pr)) frame += 8;
	frame = (frame + 15) & -16;
	if (fn->vararg) {
		fprintf(f, "\taddi.d $sp, $sp, -64\n");
		for (r=A0+fn->va_gpregs; r<=A7; r++)
			fprintf(f, "\tst.d %s, $sp, %d\n", rname[r], 8*(r-A0));
	}
	fprintf(f, "\taddi.d $sp, $sp, -16\n\tst.d $fp, $sp, 0\n\tst.d $ra, $sp, 8\n\taddi.d $fp, $sp, 0\n");
	if (frame <= 2047) fprintf(f, "\taddi.d $sp, $sp, -%d\n", frame);
	else fprintf(f, "\tli.d $t8, %d\n\tsub.d $sp, $sp, $t8\n", frame);
	for (pr = la64_rclob, off = 0; *pr >= 0; pr++) if (fn->reg & BIT(*pr)) {
		fprintf(f, "\t%s %s, $sp, %d\n", *pr < FT0 ? "st.d" : "fst.d", rname[*pr], off);
		off += 8;
	}
	for (lbl = 0, b = fn->start; b; b = b->link) {
		if (lbl || b->npred > 1) fprintf(f, ".L%d:\n", id0 + b->id);
		for (i = b->ins; i != &b->ins[b->nins]; i++) emitins(i, fn, f);
		lbl = 1;
		switch (b->jmp.type) {
		case Jhlt: fputs("\tbreak 0\n", f); break;
		case Jret0:
			if (fn->dynalloc) {
				if (frame <= 2047)
					fprintf(f, "\taddi.d $sp, $fp, -%d\n", frame);
				else
					fprintf(f, "\tli.d $t8, %d\n\tsub.d $sp, $fp, $t8\n", frame);
			}
			for (pr = la64_rclob, off = 0; *pr >= 0; pr++) if (fn->reg & BIT(*pr)) {
				fprintf(f, "\t%s %s, $sp, %d\n", *pr < FT0 ? "ld.d" : "fld.d", rname[*pr], off);
				off += 8;
			}
			fprintf(f, "\taddi.d $sp, $fp, 0\n\tld.d $ra, $sp, 8\n\tld.d $fp, $sp, 0\n\taddi.d $sp, $sp, %d\n\tjirl $zero, $ra, 0\n", 16 + 64*fn->vararg);
			break;
		case Jjmp:
		Jmp:
			if (b->s1 != b->link) fprintf(f, "\tb .L%d\n", id0 + b->s1->id); else lbl = 0;
			break;
		case Jjnz:
			neg = 0;
			if (b->link == b->s2) { s = b->s1; b->s1 = b->s2; b->s2 = s; neg = 1; }
			if (rtype(b->jmp.arg) == RSlot) { ii.arg[0] = b->jmp.arg; emitf("ld.w $t8, %M0", &ii, fn, f); b->jmp.arg = TMP(T8); }
			fprintf(f, "\tb%s %s, $zero, .L%d\n", neg ? "ne" : "eq", rname[b->jmp.arg.val], id0 + b->s2->id);
			goto Jmp;
		}
	}
	id0 += fn->nblk;
	elf_emitfnfin(fn->name, f);
}
