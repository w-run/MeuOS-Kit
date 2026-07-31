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
	/* int<->float casts.  ARM has no VCVT between a core register and
	 * a VFP double, so the conversion goes through the s2 scratch
	 * (D1, excluded from rega's allocation pool — see arm.h NFPS):
	 *   int -> double: vmov s2, rN ; vcvt.f64.s32 dN, s2
	 *   double -> int: vcvt.s32.f64 s2, dN ; vmov rN, s2 */
	{ Ostosi,  Ka, "vcvt.s32.f64 s2, %S0\n\tvmov\t%=, s2" },
	{ Ostoui,  Ka, "vcvt.u32.f64 s2, %S0\n\tvmov\t%=, s2" },
	{ Odtosi,  Ka, "vcvt.s32.f64 s2, %D0\n\tvmov\t%=, s2" },
	{ Odtoui,  Ka, "vcvt.u32.f64 s2, %D0\n\tvmov\t%=, s2" },
	{ Oswtof,  Ka, "vmov s2, %W0\n\tvcvt.f64.s32\t%=, s2" },
	{ Ouwtof,  Ka, "vmov s2, %W0\n\tvcvt.f64.u32\t%=, s2" },
	{ Osltof,  Kd, "bl __aeabi_l2d\n\tvmov\t%=, r0, r1" },
	{ Osltof,  Ks, "bl __aeabi_l2f\n\tvmov\t%=, r0" },
	{ Oultof,  Ka, "vmov s2, %W0\n\tvcvt.f64.u32\t%=, s2" },
	{ Ocall,   Kw, "blx %L0" },
	{ Oacmp,   Ki, "cmp %0, %1" },
	{ Oacmn,   Ki, "cmn %0, %1" },
	{ Oafcmp,  Ka, "vcmp.f64 %0, %1\n\tvmrs\tAPSR_nzcv, fpscr" },
	/* Floating-point comparisons: vcmp sets NZCV (via fpscr), then a
	 * conditional mov materializes 0/1 in a GPR.  The comparison's
	 * i->cls is Kw (result class), so entries use Ki; %S0/%S1 and
	 * %D0/%D1 force the fp operand widths.  Condition codes mirror
	 * aarch64: eq/ne, ge/gt, ls (<=), mi (<). */
#define FPCMP(o, cc) \
	{ O##o##s, Ki, "vcmp.f32 %S0, %S1\n\tvmrs\tAPSR_nzcv, fpscr\n\tmov\t%W=, #0\n\tmov" cc "\t%W=, #1" }, \
	{ O##o##d, Ki, "vcmp.f64 %D0, %D1\n\tvmrs\tAPSR_nzcv, fpscr\n\tmov\t%W=, #0\n\tmov" cc "\t%W=, #1" },
	FPCMP(ceq, "eq")
	FPCMP(cne, "ne")
	FPCMP(cge, "ge")
	FPCMP(cgt, "gt")
	FPCMP(cle, "ls")
	FPCMP(clt, "mi")
#undef FPCMP
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
	/* Ring of buffers so multiple rname() calls in one fprintf (e.g.
	 * "ldr %s, [%s]" with two registers) don't alias: a single static
	 * buffer would be overwritten by the later call and both %s would
	 * print the same register name.  Six slots cover the deepest use
	 * (4 registers in one call). */
	static char buf[6][8];
	static unsigned idx;
	char *b = buf[idx++ % 6];
	switch (k) {
	default:
	case Kw:
	case Kx:
	case Kl:
		if (r == SP) return "sp";
		if (r == LR) return "lr";
		if (R0 <= r && r <= R12) {
			snprintf(b, 8, "r%d", r - R0); return b;
		}
		break;
	case Ks:
	case Kd:
		if (D0 <= r && r <= D15) {
			snprintf(b, 8, k == Ks ? "s%d" : "d%d", r - D0);
			return b;
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
			/* 符号偏移作为 addend 拼在符号名后（mt/as 的
			 * parse_reference 会把 `sym+N` 拆成 symbol + addend）。 */
			if (c->bits.i) {
				fprintf(f, "\tmovw\t%s, #:lower16:%s%s%+" PRIi64 "\n",
					rn, T.assym, str(c->sym.id), c->bits.i);
				fprintf(f, "\tmovt\t%s, #:upper16:%s%s%+" PRIi64 "\n",
					rn, T.assym, str(c->sym.id), c->bits.i);
			} else {
				fprintf(f, "\tmovw\t%s, #:lower16:%s%s\n",
					rn, T.assym, str(c->sym.id));
				fprintf(f, "\tmovt\t%s, #:upper16:%s%s\n",
					rn, T.assym, str(c->sym.id));
			}
		} else {
			if (c->bits.i)
				fprintf(f, "\tldr\t%s, =%s%s%+" PRIi64 "\n",
					rn, T.assym, str(c->sym.id), c->bits.i);
			else
				fprintf(f, "\tldr\t%s, =%s%s\n",
					rn, T.assym, str(c->sym.id));
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

/* ---- Kl (64-bit) decomposition ----
 *
 * ARMv7 has no 64-bit GPR (kl_in_reg == 0): Kl values live in 8-byte
 * stack slots, low 32 bits at the slot offset, high 32 bits at
 * slot+4.  rega rewrites Kl RTmp arguments to their slots, but Kl
 * results (i->to) remain RTmp (the store of the result happens in a
 * trailing Ostorel), so the generic emitf paths — which assert isreg
 * on every register operand — cannot handle Kl ops.  Decompose each
 * Kl op into 32-bit ops on the low/high halves, mirroring the i386
 * backend's kl_* helpers.
 *
 * Scratch: r10 (IP) and r12, saved/restored around each op because
 * rega does not know about these implicit clobbers.  The EABI
 * 64-bit-return convention (R0:R1, low:high) is honored by Ocopy to/
 * from R0.
 */

/* Slot index of a Kl reference, or -1 when r is a machine register. */
static int
klslot(Ref r, Fn *fn)
{
	if (rtype(r) == RSlot)
		return rsval(r);
	if (rtype(r) == RTmp && r.val >= Tmp0)
		return fn->tmp[r.val].slot;
	return -1;
}

static uint64_t
klslotoff(int s)
{
	if (s < 0)
		return (uint64_t)(-(s + 1)) * 4;
	return (uint64_t)s * 4;
}

/* Load half hi (0=low, 1=high) of Kl ref r into register reg.
 * RCon yields the corresponding 32-bit half of the 64-bit constant;
 * a machine register yields its (32-bit) value for either half
 * (callers responsible for sign/zero-extension). */
static void
kl_ldhalf(Ref r, int hi, int reg, Fn *fn, FILE *f)
{
	int s;
	if (rtype(r) == RCon) {
		Con *c = &fn->con[r.val];
		assert(c->type == CBits);
		Con h = *c;
		h.bits.i = hi ? (int32_t)(c->bits.i >> 32) : (int32_t)c->bits.i;
		loadcon(&h, reg, Kw, f);
		return;
	}
	if (rtype(r) == RTmp && r.val < Tmp0) {
		fprintf(f, "\tmov\t%s, %s\n", rname(reg, Kw), rname(r.val, Kw));
		return;
	}
	s = klslot(r, fn);
	assert(s != -1);
	fprintf(f, "\tldr\t%s, [r11, #%u]\n", rname(reg, Kw),
		(unsigned)(klslotoff(s) + 4u * hi));
}

/* Store register reg into half hi of Kl ref r.  Machine-register
 * destinations (e.g. the R0:R1 return pair) are handled by the
 * callers, so a non-slot destination is a no-op here. */
static void
kl_sthalf(Ref r, int hi, int reg, Fn *fn, FILE *f)
{
	int s = klslot(r, fn);
	if (s == -1)
		return;
	fprintf(f, "\tstr\t%s, [r11, #%u]\n", rname(reg, Kw),
		(unsigned)(klslotoff(s) + 4u * hi));
}

/* Emit `str reg, [addr, #+4*hi]` where addr is a memory address ref:
 * RSlot → local slot, RTmp → address register, RCon(CAddr) → global. */
static void
kl_staddr(Ref addr, int hi, int reg, Fn *fn, FILE *f)
{
	if (rtype(addr) == RSlot) {
		fprintf(f, "\tstr\t%s, [r11, #%u]\n", rname(reg, Kw),
			(unsigned)(slot(addr, fn, 0) + 4u * hi));
		return;
	}
	assert(rtype(addr) == RTmp && isreg(addr));
	fprintf(f, "\tstr\t%s, [%s, #%u]\n", rname(reg, Kw),
		rname(addr.val, Kw), 4u * hi);
}

/* Emit `ldr reg, [addr, #+4*hi]` for a memory address ref. */
static void
kl_ldaddr(Ref addr, int hi, int reg, Fn *fn, FILE *f)
{
	if (rtype(addr) == RSlot) {
		fprintf(f, "\tldr\t%s, [r11, #%u]\n", rname(reg, Kw),
			(unsigned)(slot(addr, fn, 0) + 4u * hi));
		return;
	}
	assert(rtype(addr) == RTmp && isreg(addr));
	fprintf(f, "\tldr\t%s, [%s, #%u]\n", rname(reg, Kw),
		rname(addr.val, Kw), 4u * hi);
}

/* 64-bit comparison (Oceql..Ocultl): operands are Kl, result is a
 * 32-bit (or 64-bit) boolean.  The `subs lo; sbcs hi` idiom does NOT
 * leave correct condition flags for 64-bit comparisons on ARM (the Z
 * flag after SBCS does not propagate the low-word result), so compare
 * the high words first and, only when they are equal, the low words
 * (as unsigned): for a 64-bit value with equal high words the
 * comparison reduces to the unsigned low words for both signed and
 * unsigned operators. */
static void
kl_cmp(Ins *i, Fn *fn, FILE *f)
{
	static const char *cc[] = {
		[Cieq] = "eq", [Cine] = "ne",
		[Cisge] = "ge", [Cisgt] = "gt",
		[Cisle] = "le", [Cislt] = "lt",
		[Ciuge] = "cs", [Ciugt] = "hi",
		[Ciule] = "ls", [Ciult] = "cc",
	};
	static int cmp_id;
	int c = i->op - Oceql;
	int l = ++cmp_id;
	Ref to = i->to;

	assert(c >= 0 && c < NCmpI);
	fputs("\tpush\t{r0, r1, r10, r12}\n", f);
	kl_ldhalf(i->arg[0], 0, R10, fn, f);
	kl_ldhalf(i->arg[1], 0, R0, fn, f);
	kl_ldhalf(i->arg[0], 1, R12, fn, f);
	kl_ldhalf(i->arg[1], 1, R1, fn, f);
	fprintf(f, "\tcmp\t%s, %s\n", rname(R12, Kw), rname(R1, Kw));
	fprintf(f, "\tbne\t.Lklcmp%d\n", l);
	fprintf(f, "\tcmp\t%s, %s\n", rname(R10, Kw), rname(R0, Kw));
	fprintf(f, ".Lklcmp%d:\n", l);
	/* Restore the scratch registers before materializing the boolean:
	 * pop does not affect the flags, and the conditional mov must write
	 * the result directly to its destination (which may itself be a
	 * scratch register like r0). */
	fputs("\tpop\t{r0, r1, r10, r12}\n", f);
	if (rtype(to) == RTmp && to.val < Tmp0) {
		/* boolean in a register (Kw result) */
		fprintf(f, "\tmov\t%s, #0\n", rname(to.val, Kw));
		fprintf(f, "\tmov%s\t%s, #1\n", cc[c], rname(to.val, Kw));
	} else {
		/* boolean in a Kl slot: low half = bool, high half = 0.
		 * Materialize via r10 (protected, does not affect flags). */
		fputs("\tpush\t{r10}\n", f);
		fprintf(f, "\tmov\t%s, #0\n", rname(R10, Kw));
		fprintf(f, "\tmov%s\t%s, #1\n", cc[c], rname(R10, Kw));
		kl_sthalf(to, 0, R10, fn, f);
		fprintf(f, "\tmov\t%s, #0\n", rname(R10, Kw));
		kl_sthalf(to, 1, R10, fn, f);
		fputs("\tpop\t{r10}\n", f);
	}
}

static void
kl_emit(Ins *i, Fn *fn, FILE *f)
{
	Con *c;
	Ref r;
	int s, n;

	/* no-ops */
	if (i->op == Ocopy && (req(i->to, R) || req(i->arg[0], R)))
		return;
	if (i->op == Ocopy && req(i->to, i->arg[0]))
		return;
	if (i->op == Ostorel
	&& rtype(i->arg[0]) == RTmp && rtype(i->arg[1]) == RSlot
	&& fn->tmp[i->arg[0].val].slot == rsval(i->arg[1]))
		return;	/* spill 写回自己的 slot，跳过 */

	fputs("\tpush\t{r10, r12}\n", f);
	switch (i->op) {
	case Ocopy:
		/* ABI register pairs: a Kl Ocopy with a machine-register
		 * operand comes only from ABI lowering — the return value in
		 * R0:R1 and 64-bit call arguments in Rn:Rn+1 (R2:R3 for the
		 * second, etc.).  With the register in the destination we
		 * load the source's low/high halves into the pair; with it in
		 * the source we store the pair into the destination slot. */
		if (rtype(i->to) == RTmp && i->to.val < Tmp0) {
			kl_ldhalf(i->arg[0], 0, i->to.val, fn, f);
			kl_ldhalf(i->arg[0], 1, i->to.val + 1, fn, f);
			break;
		}
		if (rtype(i->arg[0]) == RTmp && i->arg[0].val < Tmp0) {
			kl_sthalf(i->to, 0, i->arg[0].val, fn, f);
			kl_sthalf(i->to, 1, i->arg[0].val + 1, fn, f);
			break;
		}
		kl_ldhalf(i->arg[0], 0, R10, fn, f);
		kl_sthalf(i->to, 0, R10, fn, f);
		kl_ldhalf(i->arg[0], 1, R10, fn, f);
		kl_sthalf(i->to, 1, R10, fn, f);
		break;
	case Oload:
		/* 64-bit load from memory/global/local into slot. */
		if (rtype(i->arg[0]) == RCon) {
			c = &fn->con[i->arg[0].val];
			assert(c->type == CAddr);
			loadaddr(c, (char *)rname(R12, Kw), f);
			fprintf(f, "\tldr\t%s, [%s]\n", rname(R10, Kw), rname(R12, Kw));
			kl_sthalf(i->to, 0, R10, fn, f);
			fprintf(f, "\tldr\t%s, [%s, #4]\n", rname(R10, Kw), rname(R12, Kw));
			kl_sthalf(i->to, 1, R10, fn, f);
			break;
		}
		if (rtype(i->arg[0]) == RTmp && i->arg[0].val == R12)
			fprintf(f, "\tmov\t%s, %s\n", rname(R10, Kw), rname(R12, Kw));
		kl_ldaddr(i->arg[0], 0, R10, fn, f);
		kl_sthalf(i->to, 0, R10, fn, f);
		kl_ldaddr(i->arg[0], 1, R10, fn, f);
		kl_sthalf(i->to, 1, R10, fn, f);
		break;
	case Ostorel:
		/* 64-bit store of slot/con value to memory. */
		if (rtype(i->arg[1]) == RCon) {
			c = &fn->con[i->arg[1].val];
			assert(c->type == CAddr);
			loadaddr(c, (char *)rname(R12, Kw), f);
			kl_ldhalf(i->arg[0], 0, R10, fn, f);
			fprintf(f, "\tstr\t%s, [%s]\n", rname(R10, Kw), rname(R12, Kw));
			kl_ldhalf(i->arg[0], 1, R10, fn, f);
			fprintf(f, "\tstr\t%s, [%s, #4]\n", rname(R10, Kw), rname(R12, Kw));
			break;
		}
		if (rtype(i->arg[1]) == RTmp && i->arg[1].val == R12)
			fprintf(f, "\tmov\t%s, %s\n", rname(R10, Kw), rname(R12, Kw));
		kl_ldhalf(i->arg[0], 0, R10, fn, f);
		kl_staddr(i->arg[1], 0, R10, fn, f);
		kl_ldhalf(i->arg[0], 1, R10, fn, f);
		kl_staddr(i->arg[1], 1, R10, fn, f);
		break;
	case Oadd:
	case Osub:
		kl_ldhalf(i->arg[0], 0, R10, fn, f);
		kl_ldhalf(i->arg[1], 0, R12, fn, f);
		fprintf(f, "\t%s\t%s, %s, %s\n", i->op == Oadd ? "adds" : "subs",
			rname(R10, Kw), rname(R10, Kw), rname(R12, Kw));
		kl_sthalf(i->to, 0, R10, fn, f);
		kl_ldhalf(i->arg[0], 1, R10, fn, f);
		kl_ldhalf(i->arg[1], 1, R12, fn, f);
		fprintf(f, "\t%s\t%s, %s, %s\n", i->op == Oadd ? "adcs" : "sbcs",
			rname(R10, Kw), rname(R10, Kw), rname(R12, Kw));
		kl_sthalf(i->to, 1, R10, fn, f);
		break;
	case Oneg:
		/* 64-bit 取负：hi=-hi, lo=-lo, hi-=lo!=0 */
		kl_ldhalf(i->arg[0], 1, R10, fn, f);
		fprintf(f, "\trsbs\t%s, %s, #0\n", rname(R10, Kw), rname(R10, Kw));
		kl_sthalf(i->to, 1, R10, fn, f);
		kl_ldhalf(i->arg[0], 0, R10, fn, f);
		fprintf(f, "\trsbs\t%s, %s, #0\n", rname(R10, Kw), rname(R10, Kw));
		kl_sthalf(i->to, 0, R10, fn, f);
		kl_ldhalf(i->to, 1, R10, fn, f);
		fprintf(f, "\tsbcs\t%s, %s, #0\n", rname(R10, Kw), rname(R10, Kw));
		kl_sthalf(i->to, 1, R10, fn, f);
		break;
	case Oand:
	case Oor:
	case Oxor:
		n = i->op == Oand ? 0 : i->op == Oor ? 1 : 2;
		kl_ldhalf(i->arg[0], 0, R10, fn, f);
		kl_ldhalf(i->arg[1], 0, R12, fn, f);
		fprintf(f, "\t%s\t%s, %s, %s\n",
			(char *[]){"and", "orr", "eor"}[n],
			rname(R10, Kw), rname(R10, Kw), rname(R12, Kw));
		kl_sthalf(i->to, 0, R10, fn, f);
		kl_ldhalf(i->arg[0], 1, R10, fn, f);
		kl_ldhalf(i->arg[1], 1, R12, fn, f);
		fprintf(f, "\t%s\t%s, %s, %s\n",
			(char *[]){"and", "orr", "eor"}[n],
			rname(R10, Kw), rname(R10, Kw), rname(R12, Kw));
		kl_sthalf(i->to, 1, R10, fn, f);
		break;
	case Oextsw: case Oextuw:
	case Oextsb: case Oextub:
	case Oextsh: case Oextuh:
		/* 源是 32 位值（装载器已扩展到 32 位）：低字复制，
		 * 高字符号扩展 asr #31 / 零扩展 mov #0。 */
		kl_ldhalf(i->arg[0], 0, R10, fn, f);
		kl_sthalf(i->to, 0, R10, fn, f);
		if (i->op == Oextsw || i->op == Oextsb || i->op == Oextsh)
			fprintf(f, "\tasr\t%s, %s, #31\n", rname(R10, Kw), rname(R10, Kw));
		else
			fprintf(f, "\tmov\t%s, #0\n", rname(R10, Kw));
		kl_sthalf(i->to, 1, R10, fn, f);
		break;
	case Omul:
		/* 64 位乘法：结果 = a_lo*b_lo + (a_lo*b_hi + a_hi*b_lo)<<32。
		 * 模 2^64 下与符号无关，用 umull/umlal。 */
		fputs("\tpush\t{r0, r1, r2, r3}\n", f);
		kl_ldhalf(i->arg[0], 0, R0, fn, f);
		kl_ldhalf(i->arg[0], 1, R1, fn, f);
		kl_ldhalf(i->arg[1], 0, R2, fn, f);
		kl_ldhalf(i->arg[1], 1, R3, fn, f);
		fprintf(f, "\tumull\t%s, %s, %s, %s\n",
			rname(R10, Kw), rname(R12, Kw), rname(R0, Kw), rname(R2, Kw));
		fprintf(f, "\tumlal\t%s, %s, %s, %s\n",
			rname(R10, Kw), rname(R12, Kw), rname(R0, Kw), rname(R3, Kw));
		fprintf(f, "\tumlal\t%s, %s, %s, %s\n",
			rname(R10, Kw), rname(R12, Kw), rname(R1, Kw), rname(R2, Kw));
		kl_sthalf(i->to, 0, R10, fn, f);
		kl_sthalf(i->to, 1, R12, fn, f);
		fputs("\tpop\t{r0, r1, r2, r3}\n", f);
		break;
	case Odiv: case Oudiv: case Orem: case Ourem:
		/* EABI __aeabi_[u]ldivmod(n=r0:r1, d=r2:r3) →
		 * q=r0:r1, rem=r2:r3。 */
		fputs("\tpush\t{r0, r1, r2, r3, lr}\n", f);
		kl_ldhalf(i->arg[0], 0, R0, fn, f);
		kl_ldhalf(i->arg[0], 1, R1, fn, f);
		kl_ldhalf(i->arg[1], 0, R2, fn, f);
		kl_ldhalf(i->arg[1], 1, R3, fn, f);
		fprintf(f, "\tbl\t%s\n",
			(i->op == Odiv || i->op == Orem) ?
				"__aeabi_ldivmod" : "__aeabi_uldivmod");
		if (i->op == Odiv || i->op == Oudiv) {
			kl_sthalf(i->to, 0, R0, fn, f);
			kl_sthalf(i->to, 1, R1, fn, f);
		} else {
			kl_sthalf(i->to, 0, R2, fn, f);
			kl_sthalf(i->to, 1, R3, fn, f);
		}
		fputs("\tpop\t{r0, r1, r2, r3, lr}\n", f);
		break;
	case Oshl:
	case Oshr:
	case Osar:
		/* 64 位移位（立即数）。 arg[1] 必须为常量；
		 * 寄存器移位量尚未支持。 */
		if (rtype(i->arg[1]) != RCon)
			die("arm: %s Kl with non-constant shift not supported",
				optab[i->op].name);
		n = (int)(fn->con[i->arg[1].val].bits.i & 63);
		if (n == 0) {
			kl_ldhalf(i->arg[0], 0, R10, fn, f);
			kl_sthalf(i->to, 0, R10, fn, f);
			kl_ldhalf(i->arg[0], 1, R10, fn, f);
			kl_sthalf(i->to, 1, R10, fn, f);
		} else if (i->op == Oshl) {
			kl_ldhalf(i->arg[0], 0, R10, fn, f);
			kl_ldhalf(i->arg[0], 1, R12, fn, f);
			if (n < 32) {
				/* hi = (hi<<n)|(lo>>(32-n)), lo <<= n */
				fprintf(f, "\tlsl\t%s, %s, #%d\n", rname(R12, Kw), rname(R12, Kw), n);
				fprintf(f, "\torr\t%s, %s, %s, LSR #%d\n", rname(R12, Kw),
					rname(R12, Kw), rname(R10, Kw), 32 - n);
				fprintf(f, "\tlsl\t%s, %s, #%d\n", rname(R10, Kw), rname(R10, Kw), n);
				kl_sthalf(i->to, 1, R12, fn, f);
			} else if (n == 32) {
				/* hi=lo, lo=0 (r12 == 0) */
				fprintf(f, "\tmov\t%s, #0\n", rname(R12, Kw));
				kl_sthalf(i->to, 1, R10, fn, f);
			} else {
				/* hi=lo<<(n-32), lo=0 (r12 == 0) */
				fprintf(f, "\tmov\t%s, #0\n", rname(R12, Kw));
				fprintf(f, "\tlsl\t%s, %s, #%d\n", rname(R10, Kw), rname(R10, Kw), n - 32);
				kl_sthalf(i->to, 1, R10, fn, f);
			}
			kl_sthalf(i->to, 0, n == 32 || n > 32 ? R12 : R10, fn, f);
		} else {
			/* Oshr (逻辑) / Osar (算术) */
			kl_ldhalf(i->arg[0], 0, R10, fn, f);
			kl_ldhalf(i->arg[0], 1, R12, fn, f);
			const char *sh = i->op == Osar ? "asr" : "lsr";
			if (n < 32) {
				/* lo = (lo>>n)|(hi<<(32-n)), hi >>= n */
				fprintf(f, "\t%s\t%s, %s, #%d\n", sh, rname(R10, Kw), rname(R10, Kw), n);
				fprintf(f, "\torr\t%s, %s, %s, LSL #%d\n", rname(R10, Kw),
					rname(R10, Kw), rname(R12, Kw), 32 - n);
				fprintf(f, "\t%s\t%s, %s, #%d\n", sh, rname(R12, Kw), rname(R12, Kw), n);
				kl_sthalf(i->to, 1, R12, fn, f);
			} else if (n == 32) {
				/* lo=hi, hi=0（逻辑）或符号扩展（算术） */
				fprintf(f, "\tmov\t%s, %s\n", rname(R10, Kw), rname(R12, Kw));
				if (i->op == Osar)
					fprintf(f, "\tasr\t%s, %s, #31\n", rname(R12, Kw), rname(R12, Kw));
				else
					fprintf(f, "\tmov\t%s, #0\n", rname(R12, Kw));
				kl_sthalf(i->to, 1, R12, fn, f);
			} else {
				/* lo=hi>>(n-32); hi=0 或符号扩展 */
				fprintf(f, "\t%s\t%s, %s, #%d\n", sh, rname(R10, Kw), rname(R12, Kw), n - 32);
				if (i->op == Osar)
					fprintf(f, "\tasr\t%s, %s, #31\n", rname(R12, Kw), rname(R12, Kw));
				else
					fprintf(f, "\tmov\t%s, #0\n", rname(R12, Kw));
				kl_sthalf(i->to, 1, R12, fn, f);
			}
			kl_sthalf(i->to, 0, R10, fn, f);
		}
		break;
	default:
		die("arm: Kl op %s not supported", optab[i->op].name);
	}
	fputs("\tpop\t{r10, r12}\n", f);
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
		if (INRANGE(i->op, Oceql, Ocultl)) {
			/* 64-bit comparisons: operands are Kl slots, the generic
			 * omap Ki entry would emit `cmp [r11,#off], [r11,#off]`
			 * (no ARM memory operand). */
			kl_cmp(i, fn, f);
			break;
		}
		if (i->cls == Kl || i->op == Ostorel) {
			/* 64-bit ops: no ARM instruction handles a 64-bit GPR
			 * value (kl_in_reg == 0), so decompose into 32-bit ops
			 * on the low/high slot halves (see kl_emit below).
			 * Ocall Kl is a function returning long long: the call
			 * itself is the same as Kw (the result copy is a
			 * separate Ocopy Kl), so let it fall through. */
			if (i->op != Ocall) {
				kl_emit(i, fn, f);
				break;
			}
		}
	Table:
		/* RSlot operands on load/store are rendered directly by %M0/%M1
		 * as [r11, #off]; fixarg must NOT load them into IP first (that
		 * would emit an indirect access through the slot's value). */
		if (!isload(i->op) && rtype(i->arg[0]) == RSlot)
			fixarg(&i->arg[0], 0, IP, fn, f);
		if (!isstore(i->op) && rtype(i->arg[1]) == RSlot)
			fixarg(&i->arg[1], 0, IP, fn, f);
		/* Loads other than Oload take a memory-ADDRESS operand.  When
		 * that address is a slot ref, it is a spilled address/pointer
		 * temp whose VALUE is the location to dereference (arm keeps Kl
		 * temps in slots, kl_in_reg==0) — NOT the slot's own memory.
		 * Materialize the pointer into IP first, then dereference it.
		 * Oload keeps reading the slot's content directly (scalar local
		 * variable access). */
		if (isload(i->op) && i->op != Oload && rtype(i->arg[0]) == RSlot) {
			fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
				rname(IP, Kl), slot(i->arg[0], fn, 0));
			i->arg[0] = TMP(IP);
		}
		/* Byte/half loads and stores have no ARM `=addr` (literal-pool)
		 * form (only ldr does), so a global-address operand must be
		 * materialized into a register first.  Load it into IP (r10)
		 * and rewrite the operand so %M0/%M1 emit [r10]. */
		if (isload(i->op) && rtype(i->arg[0]) == RCon) {
			c = &fn->con[i->arg[0].val];
			assert(c->type == CAddr);
			loadaddr(c, (char *)rname(IP, Kl), f);
			i->arg[0] = TMP(IP);
		}
		if (isstore(i->op) && rtype(i->arg[1]) == RCon) {
			c = &fn->con[i->arg[1].val];
			assert(c->type == CAddr);
			loadaddr(c, (char *)rname(IP, Kl), f);
			i->arg[1] = TMP(IP);
		}
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
		if (i->cls == Kl) {
			/* 64 位复制：走 Kl 分解（to 可能是未重写为 slot 的
			 * Kl temp，generic 路径的 assert(isreg) 会失败）。 */
			kl_emit(i, fn, f);
			break;
		}
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
		if (rtype(i->to) == RTmp && i->to.val < Tmp0) {
			fprintf(f, "\tmov\t%s, sp\n", rname(i->to.val, Kl));
		} else if (rtype(i->to) == RTmp && i->to.val >= Tmp0) {
			/* kl_in_reg==0: a live allocation address (32-bit) is kept
			 * in its Kl slot (low word); the high word is zero.  A dead
			 * salloc result (e.g. fixed-size local array whose address
			 * is never materialized) is simply dropped. */
			int ss = klslot(i->to, fn);
			if (ss != -1) {
				fprintf(f, "\tstr\tsp, [r11, #%u]\n",
					(unsigned)klslotoff(ss));
				fputs("\tpush\t{r10}\n", f);
				fputs("\tmov\tr10, #0\n", f);
				fprintf(f, "\tstr\tr10, [r11, #%u]\n",
					(unsigned)(klslotoff(ss) + 4));
				fputs("\tpop\t{r10}\n", f);
			}
		}
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
	if (fn->slot || fn->dynalloc) {
		/* Local slots are addressed as [r11, #off] (see Oload/Ostore/
		 * Oaddr), so r11 is the frame pointer.  Point it at the new
		 * stack top after the frame is allocated.  Without this, r11
		 * holds whatever the caller left (crt1._start zeroes it), so
		 * any local-variable access dereferences a null/invalid base.
		 * A function with salloc() also needs r11 so the epilogue can
		 * restore sp across the dynamic allocation. */
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
			if (fn->dynalloc)
				/* salloc() lowered sp at runtime; restore it from the
				 * frame pointer (r11) before popping saved registers. */
				fprintf(f, "\tadd\tsp, r11, #%d\n", 4 * fn->slot);
			else if (fn->slot)
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
