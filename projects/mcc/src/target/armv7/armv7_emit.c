#include "armv7.h"

/* ARMv7 assembly emitter — mirrors aarch64's emit framework.
 *
 * The omap[] table maps (IR op, class) → `emitf` format strings.
 * %=   → destination register
 * %0   → first argument register
 * %1   → second argument (register or immediate)
 * %M0  → memory addressing of first argument (load)
 * %M1  → memory addressing of second argument (store)
 * %W0  → force Kw width for argument 0
 * %L0  → Kl width
 * %S0  → Ks (single) width
 * %D0  → Kd (double) width
 * %?   → scratch register (IP1 or V31)
 */

#define CMP(X) \
	X(Cieq,       "eq", "ne") \
	X(Cine,       "ne", "eq") \
	X(Cisge,      "ge", "lt") \
	X(Cisgt,      "gt", "le") \
	X(Cisle,      "le", "gt") \
	X(Cislt,      "lt", "ge") \
	X(Ciuge,      "cs", "cc") \
	X(Ciugt,      "hi", "ls") \
	X(Ciule,      "ls", "hi") \
	X(Ciult,      "cc", "cs")

enum { Ki = -1, Ka = -2 };
#define IP R10

static struct {
	short op;
	short cls;
	char *fmt;
} omap[] = {
	{ Oadd,    Ki, "add %=, %0, %1" },
	{ Oadd,    Ka, "vadd.f64 %=, %0, %1" }, /* FIXME for 32-bit floats, would be vadd.f32 */
	{ Osub,    Ki, "sub %=, %0, %1" },
	{ Osub,    Ka, "vsub.f64 %=, %0, %1" },
	{ Oneg,    Ki, "neg %=, %0" },
	{ Oneg,    Ka, "vneg.f64 %=, %0" },
	{ Oand,    Ki, "and %=, %0, %1" },
	{ Oor,     Ki, "orr %=, %0, %1" },
	{ Oxor,    Ki, "eor %=, %0, %1" },
	{ Osar,    Ki, "asr %=, %0, %1" },
	{ Oshr,    Ki, "lsr %=, %0, %1" },
	{ Oshl,    Ki, "lsl %=, %0, %1" },
	{ Omul,    Ki, "mul %=, %0, %1" },
	{ Omul,    Ka, "vmul.f64 %=, %0, %1" },
	{ Odiv,    Ki, "sdiv %=, %0, %1" },
	{ Odiv,    Ka, "vdiv.f64 %=, %0, %1" },
	{ Oudiv,   Ki, "udiv %=, %0, %1" },
	{ Orem,    Ki, "sdiv %?, %0, %1\n\tmsub\t%=, %?, %1, %0" },
	{ Ourem,   Ki, "udiv %?, %0, %1\n\tmsub\t%=, %?, %1, %0" },
	{ Ocopy,   Ki, "mov %=, %0" },
	{ Ocopy,   Ka, "vmov %=, %0" },
	{ Oswap,   Ki, "mov %?, %0\n\tmov\t%0, %1\n\tmov\t%1, %?" },
	{ Oswap,   Ka, "vmov %?, %0\n\tvmov\t%0, %1\n\tvmov\t%1, %?" },
	{ Ostoreb, Ki, "strb %W0, %M1" },
	{ Ostoreh, Ki, "strh %W0, %M1" },
	{ Ostorew, Ki, "str %W0, %M1" },
	{ Ostorel, Ki, "str %L0, %M1" },   /* 64-bit: store low word */
	{ Ostores, Ka, "vstr %S0, %M1" },
	{ Ostored, Ka, "vstr %D0, %M1" },
	{ Oloadsb, Ki, "ldrsb %=, %M0" },
	{ Oloadub, Ki, "ldrb %W=, %M0" },
	{ Oloadsh, Ki, "ldrsh %=, %M0" },
	{ Oloaduh, Ki, "ldrh %W=, %M0" },
	{ Oloadsw, Kw, "ldr %=, %M0" },
	{ Oloadsw, Kl, "ldr %=, %M0" },    /* 64-bit load: first 32 bits */
	{ Oloaduw, Ki, "ldr %W=, %M0" },
	{ Oload,   Ka, "vldr %=, %M0" },
	{ Oextsb,  Ki, "sxtb %=, %W0" },
	{ Oextub,  Ki, "uxtb %W=, %W0" },
	{ Oextsh,  Ki, "sxth %=, %W0" },
	{ Oextuh,  Ki, "uxth %W=, %W0" },
	{ Oextsw,  Ki, "sxth %=, %W0" },  /* 64-bit uses same 32-bit */
	{ Oextuw,  Ki, "uxth %W=, %W0" },
	{ Oexts,   Kd, "vcvt.f64.f32 %=, %S0" },
	{ Otruncd, Ks, "vcvt.f32.f64 %=, %D0" },
	{ Ocast,   Kw, "vmov %=, %S0" },
	{ Ocast,   Kl, "vmov %=, %D0" },
	{ Ocast,   Ks, "vmov %=, %W0" },
	{ Ocast,   Kd, "vmov %=, %L0" },
	{ Ostosi,  Ka, "vcvt.s32.f64 %=, %S0" },
	{ Ostoui,  Ka, "vcvt.u32.f64 %=, %S0" },
	{ Odtosi,  Ka, "vcvt.s32.f64 %=, %D0" },
	{ Odtoui,  Ka, "vcvt.u32.f64 %=, %D0" },
	{ Oswtof,  Ka, "vcvt.f64.s32 %=, %W0" },
	{ Ouwtof,  Ka, "vcvt.f64.u32 %=, %W0" },
	{ Osltof,  Ka, "vcvt.f64.s32 %=, %W0" }, /* FIXME: 64-bit int → float: use __aeabi */
	{ Oultof,  Ka, "vcvt.f64.u32 %=, %W0" },
	{ Ocall,   Kw, "blx %L0" },
	{ Oacmp,   Ki, "cmp %0, %1" },
	{ Oacmn,   Ki, "cmn %0, %1" },
	{ Oafcmp,  Ka, "vcmp.f64 %0, %1\n\tvmrs\tAPSR_nzcv, fpscr" },
#define X(c, str, _) { Oflag+c, Ki, "mov %=, #0\n\tmov" str "\t%=, #1" },
	CMP(X)
#undef X
	{ Oaddr, Ki, "add %=, r11, %M0" },
	{ NOp, 0, 0 }
};

static const char *
rname(int r, int k)
{
	static char buf[8];
	switch (k) {
	default:
	case Kw:
	case Kx:
	case Kl:
		if (r == SP) return "sp";
		if (r == LR) return "lr";
		if (R0 <= r && r <= R12) {
			snprintf(buf, sizeof buf, "r%d", r - R0); return buf;
		}
		break;
	case Ks:
	case Kd:
		if (D0 <= r && r <= D15) {
			snprintf(buf, sizeof buf, k == Ks ? "s%d" : "d%d", r - D0);
			return buf;
		}
		break;
	}
	die("invalid register %d for class %d", r, k);
}

static uint64_t
slot(Ref r, Fn *fn, uint64_t frame)
{
	int s = rsval(r);
	if (s < 0)
		return (uint64_t)(-(s + 1)) * 4;
	else
		return frame + (uint64_t)s * 4;
}

static void
emitf(char *s, Ins *i, Fn *fn, FILE *f)
{
	Ref r;
	int k, c;
	Con *pc;
	uint64_t n;
	uint sp;

	fputc('\t', f);
	sp = 0;
	for (;;) {
		k = i->cls;
		while ((c = *s++) != '%')
			if (c == ' ' && !sp) { fputc('\t', f); sp = 1; }
			else if (!c) { fputc('\n', f); return; }
			else fputc(c, f);
	Switch:
		switch ((c = *s++)) {
		default: die("invalid escape");
		case 'W': k = Kw; goto Switch;
		case 'L': k = Kl; goto Switch;
		case 'S': k = Ks; goto Switch;
		case 'D': k = Kd; goto Switch;
		case '?':
			fputs("r12", f);
			break;
		case '=':
		case '0':
			r = c == '=' ? i->to : i->arg[0];
			assert(isreg(r));
			fputs(rname(r.val, k), f);
			break;
		case '1':
			r = i->arg[1];
			switch (rtype(r)) {
			default: die("invalid second argument");
			case RTmp:
				assert(isreg(r));
				fputs(rname(r.val, k), f);
				break;
			case RCon:
				pc = &fn->con[r.val];
				n = pc->bits.i;
				assert(pc->type == CBits);
				/* Try 8-bit rotated immediate; if not encodable emit via spilling.
				 * For simplicity, always emit as #imm (GAS will error if unencodable) */
				fprintf(f, "#%" PRIu64, n);
				break;
			}
			break;
		case 'M':
			c = *s++;
			assert(c == '0' || c == '1' || c == '=');
			r = c == '=' ? i->to : i->arg[c - '0'];
			switch (rtype(r)) {
			default: die("todo: unhandled ref for memory");
			case RTmp:
				assert(isreg(r));
				fprintf(f, "[%s]", rname(r.val, Kl));
				break;
			case RSlot:
				fprintf(f, "[r11, #%" PRIu64 "]", slot(r, fn, 0));
				break;
			}
			break;
		}
	}
}

static void
loadaddr(Con *c, char *rn, FILE *f)
{
	/* For global symbols, emit movw/movt pair. */
	switch (c->sym.type) {
	default: die("unreachable");
	case SGlo:
		fprintf(f, "\tmovw\t%s, #:lower16:%s%c%" PRIi64 "%s\n", rn, T.assym, str(c->sym.id), c->bits.i, T.assym[0] ? "" : "");
		fprintf(f, "\tmovt\t%s, #:upper16:%s%c%" PRIi64 "\n", rn, T.assym, str(c->sym.id), c->bits.i);
		break;
	case SThr:
		fprintf(f, "\tmrc\tp15, 0, %s, c13, c0, 3\n", rn);
		fprintf(f, "\tadd\t%s, %s, #:tprel_lo12_nc:%s\n", rn, rn, str(c->sym.id));
		fprintf(f, "\tadd\t%s, %s, #:tprel_lo12_nc:%s\n", rn, rn, str(c->sym.id)); /* FIXME: use hi12/lo12 pair */
		break;
	case SExt:
		fprintf(f, "\tmovw\t%s, #:lower16:%s\n", rn, str(c->sym.id));
		fprintf(f, "\tmovt\t%s, #:upper16:%s\n", rn, str(c->sym.id));
		break;
	}
}

static void
loadcon(Con *c, int r, int k, FILE *f)
{
	if (c->type == CAddr) {
		loadaddr(c, (char *)rname(r, Kl), f);
		return;
	}
	/* Simple constant load via movw/movt */
	int64_t n = c->bits.i;
	if (k == Kw) n = (int32_t)n;
	fprintf(f, "\tmovw\t%s, #0x%x\n", rname(r, k), (unsigned)(n & 0xFFFF));
	if (n & ~0xFFFF)
		fprintf(f, "\tmovt\t%s, #0x%x\n", rname(r, k), (unsigned)((n >> 16) & 0xFFFF));
}

static void emitins(Ins *, Fn *, FILE *);

static int
fixarg(Ref *pr, int sz, int t, Fn *fn, FILE *f)
{
	(void)sz; (void)t; (void)fn; (void)f;
	return 0;  /* no fixup needed for small ARM immediates */
}

static void
emitins(Ins *i, Fn *fn, FILE *f)
{
	Ref r; Con *c; uint64_t s; char *l, *p; int t, o;

	switch (i->op) {
	default:
	Table:
		if (isload(i->op))
			fixarg(&i->arg[0], 0, IP, fn, f);
		if (isstore(i->op))
			fixarg(&i->arg[1], 0, IP, fn, f);
		for (o = 0;; o++) {
			if (omap[o].op == NOp)
				die("no match for %s(%c)", optab[i->op].name, "wlsd"[i->cls]);
			if (omap[o].op == i->op)
				if (omap[o].cls == i->cls || omap[o].cls == Ka
				 || (omap[o].cls == Ki && KBASE(i->cls) == 0))
					break;
		}
		emitf(omap[o].fmt, i, fn, f);
		break;
	case Onop:
		break;
	case Ocopy:
		if (req(i->to, i->arg[0]))
			break;
		if (rtype(i->to) == RSlot) {
			r = i->to;
			if (!isreg(i->arg[0])) {
				i->to = TMP(IP);
				emitins(i, fn, f);
				i->arg[0] = i->to;
			}
			i->op = Ostorew + i->cls;
			i->cls = Kw;
			i->arg[1] = r;
			emitins(i, fn, f);
			break;
		}
		assert(isreg(i->to));
		switch (rtype(i->arg[0])) {
		case RCon:
			c = &fn->con[i->arg[0].val];
			loadcon(c, i->to.val, i->cls, f);
			break;
		case RSlot:
			i->op = Oload;
			emitins(i, fn, f);
			break;
		default:
			assert(i->to.val != IP);
			goto Table;
		}
		break;
	case Oaddr:
		emitf("add %=, r11, #%M0", i, fn, f);
		break;
	case Ocall:
		if (rtype(i->arg[0]) != RCon)
			goto Table;
		c = &fn->con[i->arg[0].val];
		if (c->type != CAddr || (c->sym.type & SThr) || c->bits.i)
			die("invalid call argument");
		l = str(c->sym.id);
		p = l[0] == '"' ? "" : T.assym;
		fprintf(f, "\tbl\t%s%s\n", p, l);
		break;
	case Osalloc:
		emitf("sub sp, sp, #%0", i, fn, f);
		if (!req(i->to, R))
			emitf("mov %=, sp", i, fn, f);
		break;
	case Odbgloc:
		emitdbgloc(i->arg[0].val, i->arg[1].val, f);
		break;
	}
}

void
arm32_emitfn(Fn *fn, FILE *f)
{
	static char *ctoa[][2] = {
		#define X(c, s, n) [c] = {s, n},
		CMP(X)
		#undef X
	};
	static int id0;
	int n, c, lbl;
	Blk *b, *t;
	Ins *i;

	emitfnlnk(fn->name, &fn->lnk, f);
	fprintf(f, "\tpush\t{lr}\n");
	if (fn->slot)
		fprintf(f, "\tsub\tsp, sp, #%d\n", 4 * fn->slot);
	lbl = 0;
	for (b = fn->start; b; b = b->link) {
		if (lbl || b->npred > 1)
			fprintf(f, "%s%d:\n", T.asloc, id0 + b->id);
		for (i = b->ins; i != &b->ins[b->nins]; i++)
			emitins(i, fn, f);
		lbl = 1;
		switch (b->jmp.type) {
		case Jhlt:
			fprintf(f, "\tbrk\t#1000\n");
			break;
		case Jret0:
			if (fn->slot)
				fprintf(f, "\tadd\tsp, sp, #%d\n", 4 * fn->slot);
			fprintf(f, "\tpop\t{pc}\n");
			break;
		case Jjmp:
		Jmp:
			if (b->s1 != b->link)
				fprintf(f, "\tb\t%s%d\n", T.asloc, id0 + b->s1->id);
			else
				lbl = 0;
			break;
		default:
			c = b->jmp.type - Jjf;
			if (c < 0 || c > NCmp)
				die("unhandled jump %d", b->jmp.type);
			if (b->link == b->s2) {
				t = b->s1; b->s1 = b->s2; b->s2 = t;
				n = 0;
			} else
				n = 1;
			fprintf(f, "\tb%s\t%s%d\n", ctoa[c][n], T.asloc, id0 + b->s2->id);
			goto Jmp;
		}
	}
	id0 += fn->nblk;
	if (!T.apple)
		elf_emitfnfin(fn->name, f);
}
