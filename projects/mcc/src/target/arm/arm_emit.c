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
	{ Oneg,    Ki, "rsb %=, %0, #0" },
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
	{ Orem,    Ki, "sdiv %?, %0, %1\n\tmls\t%=, %?, %1, %0" },
	{ Ourem,   Ki, "udiv %?, %0, %1\n\tmls\t%=, %?, %1, %0" },
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
	{ Ocast,   Ks, "vmov %=, %W0" },
	/* int<->float casts.  ARM has no VCVT between a core register and
	 * a VFP double, so the conversion goes through the s16 scratch
	 * (the low half of D8, which arm_targ.c reserves from rega's
	 * allocation pool):
	 *   int -> double: vmov s16, rN ; vcvt.f64.s32 dN, s16
	 *   double -> int: vcvt.s32.f64 s16, dN ; vmov rN, s16
	 * D8 is never assigned by rega, so conversions cannot clobber a
	 * live value (unlike the old s2/D1 scratch, which collided with
	 * the 2nd double argument register). */
	{ Ostosi,  Ka, "vcvt.s32.f64 s16, %S0\n\tvmov\t%=, s16" },
	{ Ostoui,  Ka, "vcvt.u32.f64 s16, %S0\n\tvmov\t%=, s16" },
	{ Odtosi,  Ka, "vcvt.s32.f64 s16, %D0\n\tvmov\t%=, s16" },
	{ Odtoui,  Ka, "vcvt.u32.f64 s16, %D0\n\tvmov\t%=, s16" },
	{ Oswtof,  Kd, "vmov s16, %W0\n\tvcvt.f64.s32\t%=, s16" },
	{ Oswtof,  Ks, "vmov s16, %W0\n\tvcvt.f32.s32\t%=, s16" },
	{ Ouwtof,  Kd, "vmov s16, %W0\n\tvcvt.f64.u32\t%=, s16" },
	{ Ouwtof,  Ks, "vmov s16, %W0\n\tvcvt.f32.u32\t%=, s16" },
	/* Kl 结果的 Odtosi/Odtoui/Ostosi/Ostoui 和 Osltof/Oultof (Kl 源)
	 * 由 kl_emit 走 EABI softfp helper(__aeabi_d2lz 等),见
	 * kl_emit 的转换 case。 Ocast Kl/Kd(64 位 bitcast)同样由
	 * kl_emit 用 MCRR/MRRC 处理。 */
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
			/* ARM VFP: a double register Dn aliases the single
			 * pair {s2n, s2n+1} — D0=s0/s1, D1=s2/s3, ...
			 * rega allocates FP registers by D number; the Ks name
			 * must be 2×(D-D0) (the low half of the pair).  A bare
			 * `s(r-D0)` would name s1 for D1, which is D0's high
			 * half, silently corrupting any live d0 value. */
			snprintf(b, 8, k == Ks ? "s%d" : "d%d",
				k == Ks ? 2 * (r - D0) : r - D0);
			return b;
		}
		break;
	}
	die("invalid register %d for class %d", r, k);
}

/* Callee stack frame size in bytes: the local slots plus the d8-d15
 * save area, rounded up to 8 so sp stays AAPCS-aligned at calls (the
 * libc's va_arg aligns 8-byte reads to 8 and relies on it). */
static int
arm32_framesz(Fn *fn)
{
	int csfp = 0;
	for (int rr = D8; rr <= D15; rr++)
		if (fn->reg & BIT(rr))
			csfp++;
	return (4 * (fn->slot + 2 * csfp) + 7) & -8;
}

/* Bytes pushed by the prologue below the register save area: 32 for
 * the full {r4..r9, r11, lr} set, 8 for {r11, lr}. */
static int
arm32_pushbytes(Fn *fn)
{
	for (int rr = R4; rr <= R9; rr++)
		if (fn->reg & BIT(rr))
			return 32;
	return 8;
}

static uint64_t
slot(Ref r, Fn *fn, uint64_t frame)
{
	int s = rsval(r);
	if (s < 0) {
		if (fn->vararg) {
			/* Variadic functions keep a 16-byte register save area
			 * (R0-R3) immediately above the saved registers (hence
			 * arm32_pushbytes) and below the caller's stack
			 * arguments, so va_arg can walk from it into the stack.
			 * SLOT(-k) addresses word k-1 of that area.  The frame
			 * size is only final at emit time (spill/rega run after
			 * the ABI pass), so va_start resolves it here. */
			return (uint64_t)arm32_framesz(fn) + arm32_pushbytes(fn)
				+ 4 * (-(s + 1));
		}
		/* Non-variadic stack parameters (word 5+ of a plain call):
		 * the caller pushed them on the stack below the return
		 * address, so they sit at [r11 + framesz + pushbytes + 4k]
		 * after the prologue.  arm32_selpar assigns SLOT(-(k+1)). */
		return (uint64_t)arm32_framesz(fn) + arm32_pushbytes(fn)
			+ 4 * (-(s + 1));
	}
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
		if (c->type == CAddr) {
			/* Address constant (e.g. a string literal passed as a
			 * Kl argument).  The 32-bit address goes in the low
			 * word; the high word is zero. */
			if (hi)
				fprintf(f, "\tmov\t%s, #0\n", rname(reg, Kw));
			else
				loadaddr(c, (char *)rname(reg, Kl), f);
			return;
		}
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

/* Return 1 when an RSlot address operand holds a POINTER value that
 * must be dereferenced.  arm keeps every Kl value in a stack slot
 * (kl_in_reg == 0); addresses are Kl, so an address operand that rega
 * spilled to a slot is a slot whose CONTENT is the pointer (e.g. a
 * local array's `&a` temp living in a slot).  Slots owned by non-Kl
 * temps (spilled Kw/Ks/Kd values, spill write-backs/reloads) are
 * plain variable locations and are accessed directly. */
static int
slot_is_ptr(Ref r, Fn *fn)
{
	int s, t;

	if (rtype(r) != RSlot)
		return 0;
	s = rsval(r);
	if (s < 0)
		return 0;
	for (t = Tmp0; t < fn->ntmp; t++)
		if (fn->tmp[t].slot == s && fn->tmp[t].cls == Kl)
			return 1;
	return 0;
}

/* Emit `str reg, [addr, #+4*hi]` where addr is a memory address ref:
 * RSlot → local slot, RTmp → address register, RCon(CAddr) → global. */
static void
kl_staddr(Ref addr, int hi, int reg, Fn *fn, FILE *f)
{
	if (rtype(addr) == RSlot) {
		/* The slot holds the destination ADDRESS (a Kl pointer
		 * value).  Load it into the r12 scratch and store through
		 * it; a direct `str reg, [r11, #off]` would overwrite the
		 * pointer slot instead of the pointed-to location. */
		fprintf(f, "\tldr\t%s, [r11, #%u]\n", rname(R12, Kw),
			(unsigned)slot(addr, fn, 0));
		fprintf(f, "\tstr\t%s, [%s, #%u]\n", rname(reg, Kw),
			rname(R12, Kw), 4u * hi);
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
		/* Same as kl_staddr: the slot holds the source ADDRESS. */
		fprintf(f, "\tldr\t%s, [r11, #%u]\n", rname(R12, Kw),
			(unsigned)slot(addr, fn, 0));
		fprintf(f, "\tldr\t%s, [%s, #%u]\n", rname(reg, Kw),
			rname(R12, Kw), 4u * hi);
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
	&& rtype(i->arg[1]) == RSlot
	&& ((rtype(i->arg[0]) == RTmp && i->arg[0].val >= Tmp0
	    && fn->tmp[i->arg[0].val].slot == rsval(i->arg[1]))
	    || (rtype(i->arg[0]) == RSlot
	        && rsval(i->arg[0]) == rsval(i->arg[1]))))
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
		/* Spill reload of a slot-resident Kl temp (e.g. a phi edge
		 * copy): `Oload Kl %t, SLOT(tmp[t].slot)` — the value already
		 * lives in the temp's own slot, so the reload is a no-op.
		 * Dereferencing the slot would mis-read the VALUE as a
		 * pointer.  Genuine pointer derefs have a DIFFERENT result
		 * temp than the address-slot owner, so they fall through to
		 * kl_ldaddr below. */
		if (rtype(i->arg[0]) == RSlot
		&& rtype(i->to) == RTmp && i->to.val >= Tmp0
		&& fn->tmp[i->to.val].slot == rsval(i->arg[0]))
			break;
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
		/* 64 位移位。 arg[1] 为常量时用展开编码；
		 * 非常量（寄存器移位量）时逐位循环。 */
		if (rtype(i->arg[1]) != RCon) {
			/* Non-constant 64-bit shift: rotate one bit at a time.
			 * lo→R10, hi→R12, shift count→R0, counter→R1.
			 * R0/R1 are clobbered, so save them like the Kl
			 * divmod path does. */
			/* Large base avoids the per-block labels emitted by
			 * arm32_emitfn (".L" + id0 + block id). */
			static int shlbl = 1000000;
			char l0[32], l1[32];
			sprintf(l0, ".Lsh%d", shlbl++);
			sprintf(l1, ".Lsh%d", shlbl++);
			kl_ldhalf(i->arg[0], 0, R10, fn, f);
			kl_ldhalf(i->arg[0], 1, R12, fn, f);
			fputs("\tpush\t{r0, r1}\n", f);
			if (rtype(i->arg[1]) == RSlot)
				fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
					rname(R0, Kw), slot(i->arg[1], fn, 0));
			else
				fprintf(f, "\tmov\t%s, %s\n",
					rname(R0, Kw), rname(i->arg[1].val, Kw));
			fputs("\tmov\tr1, #0\n", f);
			fprintf(f, "%s:\n", l0);
			fputs("\tcmp\tr1, r0\n", f);
			fprintf(f, "\tbeq\t%s\n", l1);
			if (i->op == Oshl) {
				/* hi = (hi<<1) | (lo>>31), lo <<= 1 (no S-suffix:
				 * mt/as has no lsls). */
				fputs("\tlsl\tr12, r12, #1\n", f);
				fputs("\torr\tr12, r12, r10, LSR #31\n", f);
				fputs("\tlsl\tr10, r10, #1\n", f);
			} else {
				const char *sh = i->op == Osar ? "asr" : "lsr";
				/* lo = lo>>1 | (hi<<31), hi >>= 1 (arithmetic or
				 * logical).  lo must shift before reading hi's low
				 * bit into lo's top bit. */
				fputs("\tlsr\tr10, r10, #1\n", f);
				fputs("\torr\tr10, r10, r12, LSL #31\n", f);
				fprintf(f, "\t%s\tr12, r12, #1\n", sh);
			}
			fputs("\tadd\tr1, r1, #1\n", f);
			fprintf(f, "\tb\t%s\n", l0);
			fprintf(f, "%s:\n", l1);
			fputs("\tpop\t{r0, r1}\n", f);
			kl_sthalf(i->to, 0, R10, fn, f);
			kl_sthalf(i->to, 1, R12, fn, f);
			break;
		}
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
	case Odtosi: case Odtoui: case Ostosi: case Ostoui:
	case Osltof: case Oultof: case Ocast: {
		/* 浮点 ↔ 64 位整数转换。
		 *
		 * ARMv7 VFP 没有 f64↔s64 指令(只有 32 位 vcvt),所以
		 * Kl 与 FP 之间的转换走 EABI softfp helper
		 * (__aeabi_d2lz/d2ulz/f2lz/f2ulz/l2d/l2f/ul2d/ul2f,
		 * 由 meuos-libc 提供)。这些 helper 遵循 softfp 调用
		 * 约定: double 参数/返回值经 r0:r1 传递位模式, float 经
		 * r0; 按 AAPCS 破坏 r0-r3, r12, d0-d7, lr。
		 *
		 * 调用序列(方向 d→l 与 l→d 对称):
		 *   - 源装入 r0:r1(FP 源用 MRRC 或 vmov 从 d/s 寄存器取)
		 *   - push {r0,r1,r2,r3,lr} + vpush {d0-d7} 保护调用者
		 *   - bl helper; 结果在 r0:r1
		 *   - 立即把结果写入目标(在 pop 之前,pop 会覆盖 r0:r1)
		 *   - 恢复保护
		 *
		 * Ocast 是纯位拷贝(union 语义),不需要 helper: 用 MRRC
		 * 把 dN 的位模式移入 r0:r1 直接存。 */
		const char *h;
		Ref src = i->arg[0];
		int is_d;
		int ss;

		switch (i->op) {
		case Odtosi: h = "__aeabi_d2lz"; is_d = 1; break;
		case Odtoui: h = "__aeabi_d2ulz"; is_d = 1; break;
		case Ostosi: h = "__aeabi_f2lz"; is_d = 0; break;
		case Ostoui: h = "__aeabi_f2ulz"; is_d = 0; break;
		case Osltof: h = i->cls == Kd ? "__aeabi_l2d" : "__aeabi_l2f"; is_d = 1; break;
		case Oultof: h = i->cls == Kd ? "__aeabi_ul2d" : "__aeabi_ul2f"; is_d = 1; break;
		default: h = 0; is_d = 1; break; /* Ocast */
		}

		if (i->op != Ocast) {
			/* 装载源到 r0:r1:
			 *   - Odtosi/Odtoui/Ostosi/Ostoui: FP 源(Kd/Ks)
			 *   - Osltof/Oultof: Kl 源(slot 或寄存器对)
			 * helper 的 softfp 参数 = 源位模式。 */
			if (i->op == Osltof || i->op == Oultof) {
				kl_ldhalf(src, 0, R0, fn, f);
				kl_ldhalf(src, 1, R1, fn, f);
			} else if (rtype(src) == RTmp && src.val < Tmp0) {
				if (is_d)
					fprintf(f, "\tvmov\tr0, r1, %s\n",
						rname(src.val, Kd));
				else
					fprintf(f, "\tvmov\tr0, %s\n",
						rname(src.val, Ks));
			} else {
				if (rtype(src) == RSlot)
					ss = rsval(src);
				else
					ss = klslot(src, fn);
				assert(ss != -1);
				if (is_d) {
					fprintf(f, "\tvldr\td0, [r11, #%u]\n",
						(unsigned)klslotoff(ss));
					fputs("\tvmov\tr0, r1, d0\n", f);
				} else {
					fprintf(f, "\tvldr\ts0, [r11, #%u]\n",
						(unsigned)klslotoff(ss));
					fputs("\tvmov\tr0, s0\n", f);
				}
			}
			/* 保护调用者寄存器: helper 破坏 r0-r3, r12, d0-d7, lr。
			 * r12 已由 kl_emit 外层 push {r10, r12} 保护。 */
			fputs("\tpush\t{r0, r1, r2, r3, lr}\n", f);
			fputs("\tvpush\t{d0-d7}\n", f);
			fprintf(f, "\tbl\t%s\n", h);
			/* 结果在 r0:r1(Kd)或 r0(Ks)。
			 * d→l 方向: 目标 Kl slot, 在 pop/vpop 前直接 str 写入
			 *   (slot 不会被寄存器恢复覆盖)。
			 * l→d 方向: 目标 FP 寄存器可能落在 d0-d7(vpop 会覆盖),
			 *   所以先暂存到 d8(callee-saved, 不在 vpush 保护范围,
			 *   也被 rega 保留为转换 scratch), 恢复后再写目标。 */
			if (i->op == Osltof || i->op == Oultof) {
				if (i->cls == Kd)
					fputs("\tvmov\td8, r0, r1\n", f);
				else
					fputs("\tvmov\ts16, r0\n", f); /* s16=d8 低半 */
				fputs("\tvpop\t{d0-d7}\n", f);
				fputs("\tpop\t{r0, r1, r2, r3, lr}\n", f);
				/* 目标 FP 寄存器 */
				if (rtype(i->to) == RTmp && i->to.val < Tmp0) {
					if (i->cls == Kd)
						fprintf(f, "\tvmov\t%s, d8\n",
							rname(i->to.val, Kd));
					else
						fprintf(f, "\tvmov\t%s, s16\n",
							rname(i->to.val, Ks));
				} else {
					/* 目标溢出到 FP slot */
					if (rtype(i->to) == RSlot)
						ss = rsval(i->to);
					else
						ss = klslot(i->to, fn);
					assert(ss != -1);
					if (i->cls == Kd)
						fprintf(f, "\tvstr\td8, [r11, #%u]\n",
							(unsigned)klslotoff(ss));
					else
						fprintf(f, "\tvstr\ts16, [r11, #%u]\n",
							(unsigned)klslotoff(ss));
				}
			} else {
				kl_sthalf(i->to, 0, R0, fn, f);
				kl_sthalf(i->to, 1, R1, fn, f);
				fputs("\tvpop\t{d0-d7}\n", f);
				fputs("\tpop\t{r0, r1, r2, r3, lr}\n", f);
			}
			break;
		}

		/* Ocast: bitcast。 方向由 i->cls 决定:
		 *   - Kd → Kl (i->cls==Kl, 源 Kd): MRRC 把 dN 位模式移入
		 *     r0:r1 直存。
		 *   - Kl → Kd (i->cls==Kd, 源 Kl): 先加载源低/高半到
		 *     r0:r1, MCRR 移入目标 dN。 */
		if (i->cls == Kl) {
			if (rtype(src) == RTmp && src.val < Tmp0) {
				fprintf(f, "\tvmov\tr0, r1, %s\n",
					rname(src.val, Kd));
			} else {
				if (rtype(src) == RSlot)
					ss = rsval(src);
				else
					ss = klslot(src, fn);
				assert(ss != -1);
				fprintf(f, "\tvldr\td0, [r11, #%u]\n",
					(unsigned)klslotoff(ss));
				fputs("\tvmov\tr0, r1, d0\n", f);
			}
			kl_sthalf(i->to, 0, R0, fn, f);
			kl_sthalf(i->to, 1, R1, fn, f);
		} else {
			/* Kl → Kd: 源 Kl 槽/寄存器对 → r0:r1 → dN */
			kl_ldhalf(src, 0, R0, fn, f);
			kl_ldhalf(src, 1, R1, fn, f);
			if (rtype(i->to) == RTmp && i->to.val < Tmp0) {
				fprintf(f, "\tvmov\t%s, r0, r1\n",
					rname(i->to.val, Kd));
			} else {
				if (rtype(i->to) == RSlot)
					ss = rsval(i->to);
				else
					ss = klslot(i->to, fn);
				assert(ss != -1);
				fprintf(f, "\tvmov\td0, r0, r1\n"
					"\tvstr\td0, [r11, #%u]\n",
					(unsigned)klslotoff(ss));
			}
		}
		break;
	}
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
		if (i->op == Omul && i->cls != Kl && rtype(i->arg[1]) == RCon) {
			/* ARM MUL 无立即数形式：常数乘法必须加载到寄存器。
			 * 2 的幂用 lsl 代替（mul r0,r0,#2 是非法指令）；
			 * 其他常数 mov 到 r12 后走 mul %=, %0, r12。 */
			uint64_t m;
			c = &fn->con[i->arg[1].val];
			assert(c->type == CBits);
			m = (uint64_t)c->bits.i;
			if (rtype(i->arg[0]) == RSlot)
				fixarg(&i->arg[0], 0, IP, fn, f);
			if (m && !(m & (m - 1)) && m <= 0x80000000ull) {
				unsigned n = 0;
				for (uint64_t t = m; t > 1; t >>= 1)
					n++;
				fprintf(f, "\tlsl\t%s, %s, #%u\n",
					rname(i->to.val, Kw), rname(i->arg[0].val, Kw), n);
				break;
			}
			fprintf(f, "\tmov\tr12, #%" PRIu64 "\n", m);
			i->arg[1] = TMP(R12);
		}
		if (INRANGE(i->op, Oceql, Ocultl)) {
			/* 64-bit comparisons: operands are Kl slots, the generic
			 * omap Ki entry would emit `cmp [r11,#off], [r11,#off]`
			 * (no ARM memory operand). */
			kl_cmp(i, fn, f);
			break;
		}
		if (i->cls == Kl || i->op == Ostorel
		|| i->op == Osltof || i->op == Oultof
		|| (i->op == Ocast && i->cls == Kd)) {
			/* 64-bit ops: no ARM instruction handles a 64-bit GPR
			 * value (kl_in_reg == 0), so decompose into 32-bit ops
			 * on the low/high slot halves (see kl_emit below).
			 * Ocall Kl is a function returning long long: the call
			 * itself is the same as Kw (the result copy is a
			 * separate Ocopy Kl), so let it fall through.
			 * Osltof/Oultof (Kl → float/double) have a Kl source in
			 * a slot; kl_emit loads it into r0:r1 and calls the
			 * EABI softfp helper.  Ocast with a Kd result (Kl →
			 * double bitcast) also moves a Kl slot value into the
			 * FP register pair, which the omap's single-register
			 * `vmov %=, %L0` cannot express. */
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
		 * Oload handles its own slot/pointer distinction below. */
		if (isload(i->op) && i->op != Oload && rtype(i->arg[0]) == RSlot) {
			fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
				rname(IP, Kl), slot(i->arg[0], fn, 0));
			i->arg[0] = TMP(IP);
		}
		/* Oload with an RSlot address: dereference when the slot holds
		 * a pointer (owned by a Kl temp).  Slots owned by non-Kl temps
		 * are plain spilled values (e.g. spill reloads) and are read
		 * directly. */
		if (isload(i->op) && i->op == Oload && rtype(i->arg[0]) == RSlot
		&& slot_is_ptr(i->arg[0], fn)) {
			fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
				rname(IP, Kl), slot(i->arg[0], fn, 0));
			i->arg[0] = TMP(IP);
		}
		/* Stores: the value must sit in a register (ARM has no
		 * immediate memory-store form), and an RSlot address that
		 * holds a pointer must be dereferenced.  The address goes
		 * into the r12 scratch first so the value may use IP. */
		if (isstore(i->op)) {
			if (rtype(i->arg[1]) == RCon) {
				c = &fn->con[i->arg[1].val];
				assert(c->type == CAddr);
				loadaddr(c, (char *)rname(R12, Kl), f);
				i->arg[1] = TMP(R12);
			} else if (rtype(i->arg[1]) == RSlot
			&& slot_is_ptr(i->arg[1], fn)) {
				fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
					rname(R12, Kl), slot(i->arg[1], fn, 0));
				i->arg[1] = TMP(R12);
			}
			if (rtype(i->arg[0]) == RCon && KBASE(i->cls) == 0) {
				c = &fn->con[i->arg[0].val];
				assert(c->type == CBits);
				loadcon(c, IP, Kw, f);
				i->arg[0] = TMP(IP);
			}
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
		if (i->cls == Kd && rtype(i->to) == RTmp && i->to.val < Tmp0
		&& i->to.val >= R0 && i->to.val <= R3) {
			/* A vararg double passed in a GPR pair (AAPCS varargs
			 * never use VFP): vmov rN, rN+1, dM.  A spilled source is
			 * reloaded into the d8 scratch (never allocated by rega)
			 * under vpush/vpop so a live d8 survives. */
			if (rtype(i->arg[0]) == RSlot) {
				fputs("\tvpush\t{d8}\n", f);
				fprintf(f, "\tvldr\td8, [r11, #%" PRIu64 "]\n",
					slot(i->arg[0], fn, 0));
				fprintf(f, "\tvmov\t%s, %s, d8\n",
					rname(i->to.val, Kw), rname(i->to.val + 1, Kw));
				fputs("\tvpop\t{d8}\n", f);
			} else {
				fprintf(f, "\tvmov\t%s, %s, %s\n",
					rname(i->to.val, Kw), rname(i->to.val + 1, Kw),
					rname(i->arg[0].val, Kd));
			}
			break;
		}
		if (i->cls == Kl) {
			/* 64 位复制：走 Kl 分解（to 可能是未重写为 slot 的
			 * Kl temp，generic 路径的 assert(isreg) 会失败）。 */
			kl_emit(i, fn, f);
			break;
		}
		if (req(i->to, i->arg[0]))
			break;
		if (rtype(i->to) == RSlot) {
			/* Direct store of a VALUE into the variable's slot.
			 * Ocopy never dereferences: routing it through the
			 * generic Ostore path would misinterpret a Kl-owned
			 * slot (pointer slot) as a location to dereference
			 * instead of overwriting. */
			r = i->to;
			if (!isreg(i->arg[0])) {
				i->to = TMP(IP);
				emitins(i, fn, f);
				i->arg[0] = i->to;
			}
			switch (i->cls) {
			case Kw:
				fprintf(f, "\tstr\t%s, [r11, #%" PRIu64 "]\n",
					rname(i->arg[0].val, Kw), slot(r, fn, 0));
				break;
			case Ks:
				fprintf(f, "\tvstr\t%s, [r11, #%" PRIu64 "]\n",
					rname(i->arg[0].val, Ks), slot(r, fn, 0));
				break;
			case Kd:
				fprintf(f, "\tvstr\t%s, [r11, #%" PRIu64 "]\n",
					rname(i->arg[0].val, Kd), slot(r, fn, 0));
				break;
			default:
				die("arm: Ocopy-to-slot class %d", i->cls);
			}
			break;
		}
		assert(isreg(i->to));
		switch (rtype(i->arg[0])) {
		case RCon:
			c = &fn->con[i->arg[0].val];
			if (KBASE(i->cls) != 0) {
				/* movw/movt only address core registers.  A float
				 * destination copies the constant's bit pattern from
				 * the literal pool.  This normally goes through the
				 * isel fixarg(), but rega-generated phi-edge copies
				 * of constants (e.g. `double s = 0` in a loop)
				 * reach the emitter directly. */
				Con cc = {.type = CAddr};
				char buf[32];
				int n = stashbits(c->bits.i, KWIDE(i->cls) ? 8 : 4);
				sprintf(buf, "\"%sfp%d\"", T.asloc, n);
				cc.sym.id = intern(buf);
				loadaddr(&cc, (char *)rname(IP, Kl), f);
				fprintf(f, "\tvldr\t%s, [%s]\n",
					rname(i->to.val, i->cls), rname(IP, Kl));
				break;
			}
			loadcon(c, i->to.val, i->cls, f);
			break;
		case RSlot:
			/* Direct load of the slot's VALUE (copy semantics).
			 * Routing through Oload would make a Kl-owned slot
			 * look like a pointer to dereference (e.g. `int w =
			 * x` truncating a long whose slot is Kl). */
			switch (i->cls) {
			case Kw:
				fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
					rname(i->to.val, Kw), slot(i->arg[0], fn, 0));
				break;
			case Ks:
				fprintf(f, "\tvldr\t%s, [r11, #%" PRIu64 "]\n",
					rname(i->to.val, Ks), slot(i->arg[0], fn, 0));
				break;
			case Kd:
				fprintf(f, "\tvldr\t%s, [r11, #%" PRIu64 "]\n",
					rname(i->to.val, Kd), slot(i->arg[0], fn, 0));
				break;
			default:
				die("arm: Ocopy-from-slot class %d", i->cls);
			}
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
		if (rtype(i->arg[0]) != RCon) {
			/* Indirect call through a function pointer materialized
			 * by selcall (e.g. `%armc =l copy extern $f; call S2`).
			 * The pointer lives in a Kl slot (low word) or a
			 * register. */
			if (rtype(i->arg[0]) == RSlot) {
				fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
					rname(R12, Kw), slot(i->arg[0], fn, 0));
				fputs("\tblx\tr12\n", f);
			} else if (rtype(i->arg[0]) == RTmp) {
				fprintf(f, "\tblx\t%s\n", rname(i->arg[0].val, Kw));
			} else
				die("invalid call argument");
			break;
		}
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
		/* salloc() normally sets arg[0] to a constant (the aligned
		 * allocation size); when the size has been spilled to a slot
		 * it is loaded into IP by fixarg above.  Emit the sub either
		 * way rather than through emitf(), whose %0/%= handlers only
		 * support register operands. */
		if (rtype(i->arg[0]) == RCon) {
			int64_t sz = fn->con[i->arg[0].val].bits.i;
			fprintf(f, "\tsub\tsp, sp, #%" PRIi64 "\n", sz);
		} else {
			fprintf(f, "\tsub\tsp, sp, %s\n", rname(IP, Kw));
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
	int csfp = 0;  /* count of callee-saved FPRs (d8-d15) used by rega */

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
	/* Callee-saved FPRs (d8-d15) follow the same rule: rega keeps
	 * values in them across calls (they are in rclob), so a callee that
	 * uses them must preserve them for its caller.  Save each used
	 * d8-d15 into the frame top ([r11, #4*fn->slot + 8*i]) and restore
	 * them in the epilogue, mirroring aarch64's V8-V15 handling. */
	for (int rr = D8; rr <= D15; rr++)
		if (fn->reg & BIT(rr))
			csfp++;
	/* Variadic functions: spill R0-R3 into a 16-byte register save
	 * area at the very top of the callee frame, immediately below the
	 * caller's stack arguments.  va_start/va_arg walk it and then the
	 * stack, so it must be contiguous with the stack arguments and is
	 * therefore pushed before the saved r11/lr. */
	if (fn->vararg) {
		fprintf(f, "\tsub\tsp, sp, #16\n");
		for (int rr = R0; rr <= R3; rr++)
			fprintf(f, "\tstr\t%s, [sp, #%d]\n",
				rname(rr, Kw), 4 * (rr - R0));
	}
	if (csgpr) {
		fprintf(f, "\tpush\t{r4, r5, r6, r7, r8, r9, r11, lr}\n");
	} else if (fn->slot || csfp || fn->vararg) {
		fprintf(f, "\tpush\t{r11, lr}\n");
	} else {
		/* Keep the stack 8-byte aligned (AAPCS): a bare push {lr}
		 * would leave sp ≡ 4 (mod 8) at the calls below, which
		 * misaligns the libc's 8-byte-aligned va_arg reads. */
		fprintf(f, "\tpush\t{r11, lr}\n");
	}
	if (fn->slot || csfp || fn->dynalloc || fn->vararg) {
		/* Local slots are addressed as [r11, #off] (see Oload/Ostore/
		 * Oaddr), so r11 is the frame pointer.  Point it at the new
		 * stack top after the frame is allocated.  Without this, r11
		 * holds whatever the caller left (crt1._start zeroes it), so
		 * any local-variable access dereferences a null/invalid base.
		 * A function with salloc() also needs r11 so the epilogue can
		 * restore sp across the dynamic allocation.  The frame also
		 * holds the d8-d15 save area (8 bytes each) above the slots. */
		fprintf(f, "\tsub\tsp, sp, #%d\n", arm32_framesz(fn));
		fprintf(f, "\tmov\tr11, sp\n");
		for (int rr = D8, i = 0; rr <= D15; rr++)
			if (fn->reg & BIT(rr))
				fprintf(f, "\tfstd\t%s, [r11, #%d]\n",
					rname(rr, Kd), 4 * fn->slot + 8 * i++);
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
			if (csfp)
				/* Restore callee-saved FPRs from the frame top; r11
				 * is still the frame pointer at this point. */
				for (int rr = D8, i = 0; rr <= D15; rr++)
					if (fn->reg & BIT(rr))
						fprintf(f, "\tfldd\t%s, [r11, #%d]\n",
							rname(rr, Kd), 4 * fn->slot + 8 * i++);
			if (fn->dynalloc)
				/* salloc() lowered sp at runtime; restore it from the
				 * frame pointer (r11) before popping saved registers. */
				fprintf(f, "\tadd\tsp, r11, #%d\n", arm32_framesz(fn));
			else if (fn->slot || csfp || fn->vararg)
				fprintf(f, "\tadd\tsp, sp, #%d\n", arm32_framesz(fn));
			if (fn->vararg) {
				/* Deallocate the register save area after popping the
				 * saved registers; the save area sits above them. */
				if (csgpr)
					fprintf(f, "\tpop\t{r4, r5, r6, r7, r8, r9, r11, lr}\n");
				else
					fprintf(f, "\tpop\t{r11, lr}\n");
				fprintf(f, "\tadd\tsp, sp, #16\n");
				fprintf(f, "\tbx\tlr\n");
			} else if (csgpr)
				fprintf(f, "\tpop\t{r4, r5, r6, r7, r8, r9, r11, pc}\n");
			else
				/* The prologue always pushes r11 (8-byte stack
				 * alignment), so it must be popped back. */
				fprintf(f, "\tpop\t{r11, pc}\n");
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
			 * a fallthrough b to s2.
			 *
			 * The condition may be a spilled Kl temp (arm keeps Kl
			 * values in slots, kl_in_reg == 0, so a Kl value used as a
			 * branch condition arrives as a slot reference): load the
			 * low word, and when the slot belongs to a Kl temp also OR
			 * in the high word so a value like 1<<32 tests non-zero. */
			if (rtype(b->jmp.arg) == RSlot) {
				int js = rsval(b->jmp.arg);
				uint64_t off = slot(b->jmp.arg, fn, 0);
				int iskl = 0;
				for (int tt = Tmp0; tt < fn->ntmp; tt++)
					if (fn->tmp[tt].slot == js && fn->tmp[tt].cls == Kl) {
						iskl = 1;
						break;
					}
				fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
					rname(R10, Kw), off);
				if (iskl) {
					fprintf(f, "\tldr\t%s, [r11, #%" PRIu64 "]\n",
						rname(R12, Kw), off + 4);
					fprintf(f, "\torr\t%s, %s, %s\n",
						rname(R10, Kw), rname(R10, Kw), rname(R12, Kw));
				}
				fprintf(f, "\tcmp\t%s, #0\n", rname(R10, Kw));
			} else {
				fprintf(f, "\tcmp\t%s, #0\n", rname(b->jmp.arg.val, Kw));
			}
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
