#include "arm.h"

/* ARM architecture version (set by driver from -march=armvN).
 * gate movw/movt use: ARMv6T2+ (movw available) vs ARMv6 (literal pool). */
extern int g_arm_arch_ver;

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
	{ Oadd,    Ks, "vadd.f32 %=, %0, %1" },
	{ Oadd,    Kd, "vadd.f64 %=, %0, %1" },
	{ Osub,    Ki, "sub %=, %0, %1" },
	{ Osub,    Ks, "vsub.f32 %=, %0, %1" },
	{ Osub,    Kd, "vsub.f64 %=, %0, %1" },
	{ Oneg,    Ki, "neg %=, %0" },
	{ Oneg,    Ks, "vneg.f32 %=, %0" },
	{ Oneg,    Kd, "vneg.f64 %=, %0" },
	{ Oand,    Ki, "and %=, %0, %1" },
	{ Oor,     Ki, "orr %=, %0, %1" },
	{ Oxor,    Ki, "eor %=, %0, %1" },
	{ Osar,    Ki, "asr %=, %0, %1" },
	{ Oshr,    Ki, "lsr %=, %0, %1" },
	{ Oshl,    Ki, "lsl %=, %0, %1" },
	{ Omul,    Ki, "mul %=, %0, %1" },
	{ Omul,    Ks, "vmul.f32 %=, %0, %1" },
	{ Omul,    Kd, "vmul.f64 %=, %0, %1" },
	{ Odiv,    Ki, "sdiv %=, %0, %1" },
	{ Odiv,    Ks, "vdiv.f32 %=, %0, %1" },
	{ Odiv,    Kd, "vdiv.f64 %=, %0, %1" },
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
	/* Integer Oload (e.g. Ocopy from RSlot) must use ldr — the Ka
	 * entry below is a wildcard and would otherwise route integer
	 * loads through vldr into a floating-point register. */
	{ Oload,   Kw, "ldr %W=, %M0" },
	{ Oload,   Kl, "ldr %L=, %M0" },   /* 64-bit load: first 32 bits */
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
	{ Osltof,  Kd, "bl __aeabi_l2d\n\tvmov\t%=, r0, r1" },
	{ Osltof,  Ks, "bl __aeabi_l2f\n\tvmov\t%=, r0" },
	{ Oultof,  Ka, "vcvt.f64.u32 %=, %W0" },
	{ Ocall,   Kw, "blx %L0" },
	{ Oacmp,   Ki, "cmp %0, %1" },
	{ Oacmn,   Ki, "cmn %0, %1" },
	{ Oafcmp,  Ka, "vcmp.f64 %0, %1\n\tvmrs\tAPSR_nzcv, fpscr" },
	{ Oceqw,   Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmoveq\t%=, #1" },
	{ Oceql,   Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmoveq\t%=, #1" },
	{ Ocnew,   Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovne\t%=, #1" },
	{ Ocnel,   Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovne\t%=, #1" },
	{ Ocsgew,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovge\t%=, #1" },
	{ Ocsgel,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovge\t%=, #1" },
	{ Ocsgtw,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovgt\t%=, #1" },
	{ Ocsgtl,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovgt\t%=, #1" },
	{ Ocslew,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovle\t%=, #1" },
	{ Ocslel,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovle\t%=, #1" },
	{ Ocsltw,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovlt\t%=, #1" },
	{ Ocsltl,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovlt\t%=, #1" },
	{ Ocugew,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovcs\t%=, #1" },
	{ Ocugel,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovcs\t%=, #1" },
	{ Ocugtw,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovhi\t%=, #1" },
	{ Ocugtl,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovhi\t%=, #1" },
	{ Oculew,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovls\t%=, #1" },
	{ Oculel,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovls\t%=, #1" },
	{ Ocultw,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovcc\t%=, #1" },
	{ Ocultl,  Ki, "cmp %0, %1\n\tmov\t%=, #0\n\tmovcc\t%=, #1" },
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
		if (rtype(r) == RSlot) {
			fprintf(f, "[r11, #%" PRIu64 "]", slot(r, fn, 0));
			break;
		}
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
			default:
				die("todo: unhandled ref for memory");
			case RTmp:
				assert(isreg(r));
				fprintf(f, "[%s]", rname(r.val, Kl));
				break;
			case RCon:
				pc = &fn->con[r.val];
				assert(pc->type == CAddr);
				fputs("=", f);
				fputs(str(pc->sym.id), f);
				if (pc->bits.i) fprintf(f, "+%"PRIi64, pc->bits.i);
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
	/* For global symbols, emit movw/movt pair (ARMv6T2+).
	 * For ARMv6, fall back to ldr rd, =sym (literal pool). */
	switch (c->sym.type) {
	default: die("unreachable");
	case SGlo:
		if (g_arm_arch_ver >= 7) {
			fprintf(f, "\tmovw\t%s, #:lower16:%s%c%" PRIi64 "%s\n", rn, T.assym, str(c->sym.id), c->bits.i, T.assym[0] ? "" : "");
			fprintf(f, "\tmovt\t%s, #:upper16:%s%c%" PRIi64 "\n", rn, T.assym, str(c->sym.id), c->bits.i);
		} else {
			fprintf(f, "\tldr\t%s, =%s%c%" PRIi64 "%s\n", rn, T.assym, str(c->sym.id), c->bits.i, T.assym[0] ? "" : "");
		}
		break;
	case SThr:
		fprintf(f, "\tmrc\tp15, 0, %s, c13, c0, 3\n", rn);
		fprintf(f, "\tadd\t%s, %s, #:tprel_hi12:%s\n", rn, rn, str(c->sym.id));
		fprintf(f, "\tadd\t%s, %s, #:tprel_lo12:%s\n", rn, rn, str(c->sym.id));
		break;
	case SExt:
		if (g_arm_arch_ver >= 7) {
			fprintf(f, "\tmovw\t%s, #:lower16:%s\n", rn, str(c->sym.id));
			fprintf(f, "\tmovt\t%s, #:upper16:%s\n", rn, str(c->sym.id));
		} else {
			fprintf(f, "\tldr\t%s, =%s\n", rn, str(c->sym.id));
		}
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
	/* Simple constant load via movw/movt (ARMv6T2+) or ldr =literal. */
	int64_t n = c->bits.i;
	if (k == Kw) n = (int32_t)n;
	if (g_arm_arch_ver >= 7) {
		fprintf(f, "\tmovw\t%s, #0x%x\n", rname(r, k), (unsigned)(n & 0xFFFF));
		if (n & ~0xFFFF)
			fprintf(f, "\tmovt\t%s, #0x%x\n", rname(r, k), (unsigned)((n >> 16) & 0xFFFF));
	} else {
		fprintf(f, "\tldr\t%s, =0x%llx\n", rname(r, k), (unsigned long long)n);
	}
}

static void emitins(Ins *, Fn *, FILE *);

static int
fixarg(Ref *pr, int sz, int t, Fn *fn, FILE *f)
{
	(void)sz; (void)t;
	if (rtype(*pr) == RSlot) {
		int64_t off = slot(*pr, fn, 0);
		fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n", rname(IP, Kl), off);
		*pr = TMP(IP);
	}
	return 0;
}

static void
emitins(Ins *i, Fn *fn, FILE *f)
{
	Ref r; Con *c; uint64_t s; char *l, *p; int t, o;

	switch (i->op) {
	case Opar: case Oparc: case Opare:
	case Oarg: case Oargc: case Oargv:
		break;	/* 伪指令，emit 阶段无输出 */
	default:
	Table:
		/* RSlot operands on load/store are rendered directly by %M0/%M1
		 * as [r11, #off]; fixarg must NOT load them into IP first (that
		 * would emit an indirect access through the slot's value). */
		if (!isload(i->op) && rtype(i->arg[0]) == RSlot)
			fixarg(&i->arg[0], 0, IP, fn, f);
		if (!isstore(i->op) && rtype(i->arg[1]) == RSlot)
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
		/* %M0 outputs [r11, #offset] for RSlot (memory dereference),
		 * but Oaddr needs the raw address (r11 + offset).  Handle
		 * RSlot directly and delegate everything else to emitf. */
		if (rtype(i->arg[0]) == RSlot) {
			fprintf(f, "\tadd\t%s, r11, #%" PRIu64 "\n",
				rname(i->to.val, Kl), slot(i->arg[0], fn, 0));
		} else {
			emitf("add %=, r11, %M0", i, fn, f);
		}
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
		if (rtype(i->arg[0]) == RSlot)
			fixarg(&i->arg[0], 0, IP, fn, f);
		/* salloc() sets arg[0] to a constant (the aligned allocation size).
		 * Emit the sub directly rather than through emitf(), whose %0/%= 
		 * handlers only support register operands. */
		assert(rtype(i->arg[0]) == RCon);
		{
			int64_t sz = fn->con[i->arg[0].val].bits.i;
			fprintf(f, "\tsub\tsp, sp, #%" PRIi64 "\n", sz);
		}
		if (!req(i->to, R))
			fprintf(f, "\tmov\t%s, sp\n", rname(i->to.val, Kl));
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
	int csgpr = 0; /* any callee-saved GPR (r4-r9) used by rega */

	emitfnlnk(fn->name, &fn->lnk, f);
	/* Save every callee-saved GPR that rega assigned (r4-r9), plus the
	 * frame pointer r11 and lr.  These are callee-saved by AAPCS, so a
	 * recursive/leaf callee would otherwise clobber the caller's values
	 * (e.g. fib() keeping fib(n-1) in r4 across the second recursive
	 * call).  Only the caller-saved registers need no saving. */
	for (int rr = R4; rr <= R9; rr++)
		if (fn->reg & BIT(rr)) {
			csgpr = 1;
			break;
		}
	if (csgpr) {
		fprintf(f, "\tpush\t{r4, r5, r6, r7, r8, r9, r11, lr}\n");
	} else if (fn->slot) {
		fprintf(f, "\tpush\t{r11, lr}\n");
	} else {
		fprintf(f, "\tpush\t{lr}\n");
	}
	if (fn->slot) {
		/* Local slots are addressed as [r11, #off] (see Oload/Ostore/
		 * Oaddr), so r11 is the frame pointer.  Point it at the new
		 * stack top after the frame is allocated.  Without this, r11
		 * holds whatever the caller left (crt1._start zeroes it), so
		 * any local-variable access dereferences a null/invalid base. */
		fprintf(f, "\tsub\tsp, sp, #%d\n", 4 * fn->slot);
		fprintf(f, "\tmov\tr11, sp\n");
	}
	lbl = 0;
	for (b = fn->start; b; b = b->link) {
		if (lbl || b->npred > 1)
			fprintf(f, "%s%d:\n", T.asloc, id0 + b->id);
		for (i = b->ins; i != &b->ins[b->nins]; i++)
			emitins(i, fn, f);
		lbl = 1;
		switch (b->jmp.type) {
		case Jhlt:
			fprintf(f, "\tbkpt\t#1000\n");
			break;
		case Jret0:
			if (fn->slot)
				fprintf(f, "\tadd\tsp, sp, #%d\n", 4 * fn->slot);
			if (csgpr)
				fprintf(f, "\tpop\t{r4, r5, r6, r7, r8, r9, r11, pc}\n");
			else if (fn->slot)
				fprintf(f, "\tpop\t{r11, pc}\n");
			else
				fprintf(f, "\tpop\t{pc}\n");
			break;
		case Jjmp:
		Jmp:
			if (b->s1 != b->link)
				fprintf(f, "\tb\t%s%d\n", T.asloc, id0 + b->s1->id);
			else
				lbl = 0;
			break;
		case Jjnz:
			/* jnz r, s1, s2: branch to s1 when r != 0.  The flag-setting
			 * mov/movle sequence for comparisons doesn't leave a usable
			 * condition code, so compare the value explicitly.  When s1
			 * falls through, invert to beq s2; otherwise emit bne s1 and
			 * a fallthrough b to s2. */
			fprintf(f, "\tcmp\t%s, #0\n", rname(b->jmp.arg.val, Kw));
			if (b->s1 != b->link) {
				fprintf(f, "\tbne\t%s%d\n", T.asloc, id0 + b->s1->id);
				if (b->s2 != b->link)
					fprintf(f, "\tb\t%s%d\n", T.asloc, id0 + b->s2->id);
				else
					lbl = 0;
			} else {
				fprintf(f, "\tbeq\t%s%d\n", T.asloc, id0 + b->s2->id);
			}
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
	fprintf(f, ".type %s, %%function\n", fn->name);
	fprintf(f, ".size %s, .-%s\n", fn->name, fn->name);
}
