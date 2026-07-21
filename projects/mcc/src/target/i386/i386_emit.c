#include "i386.h"


typedef struct E E;

struct E {
	FILE *f;
	Fn *fn;
	int fp;
	uint64_t fsz;
	int nclob;
};

#define CMP(X) \
	X(Ciule,      "be", "a") \
	X(Ciult,      "b", "ae") \
	X(Cisle,      "le", "g") \
	X(Cislt,      "l", "ge") \
	X(Cisgt,      "g", "le") \
	X(Cisge,      "ge", "l") \
	X(Ciugt,      "a", "be") \
	X(Ciuge,      "ae", "b") \
	X(Cieq,       "z", "nz") \
	X(Cine,       "nz", "z") \
	X(NCmpI+Cfle, "?" , "?") \
	X(NCmpI+Cflt, "?",  "?") \
	X(NCmpI+Cfgt, "a", "be") \
	X(NCmpI+Cfge, "ae", "b") \
	X(NCmpI+Cfo,  "np", "p") \
	X(NCmpI+Cfuo, "p", "np")

enum {
	SLong = 0, /* 32-bit on i386 (was 64-bit on amd64) */
	SWord = 1,  /* 32-bit */
	SShort = 2, /* 16-bit */
	SByte = 3,  /* 8-bit */

	Ki = -1, /* matches Kw and Kl */
	Ka = -2, /* matches all classes */
};

/* Instruction format strings:
 *
 * if the format string starts with -, the instruction
 * is assumed to be 3-address and is put in 2-address
 * mode using an extra mov if necessary
 *
 * if the format string starts with +, the same as the
 * above applies, but commutativity is also assumed
 *
 * %k  is used to set the class of the instruction,
 *     it'll expand to "l" for both Kw and Kl on i386
 * %0  designates the first argument
 * %1  designates the second argument
 * %=  designates the result
 *
 * if %k is not used, a prefix to 0, 1, or = must be
 * added, it can be:
 *   M - memory reference
 *   L - long  (32 bits on i386)
 *   W - word  (32 bits)
 *   H - short (16 bits)
 *   B - byte  (8 bits)
 */
static struct {
	short op;
	short cls;
	char *fmt;
} omap[] = {
	{ Oadd,     Ka, "+add%k %1, %=" },
	{ Osub,     Ka, "-sub%k %1, %=" },
	{ Oand,     Ki, "+and%k %1, %=" },
	{ Oor,      Ki, "+or%k %1, %=" },
	{ Oxor,     Ki, "+xor%k %1, %=" },
	{ Osar,     Ki, "-sar%k %B1, %=" },
	{ Oshr,     Ki, "-shr%k %B1, %=" },
	{ Oshl,     Ki, "-shl%k %B1, %=" },
	{ Omul,     Ki, "+imul%k %1, %=" },
	{ Odiv,     Ka, "-div%k %1, %=" },
	{ Ostorel,  Ka, "movl %L0, %M1" },
	{ Ostorew,  Ka, "movl %W0, %M1" },
	{ Ostoreh,  Ka, "movw %H0, %M1" },
	{ Ostoreb,  Ka, "movb %B0, %M1" },
	{ Oload,    Ka, "mov%k %M0, %=" },
	{ Oloadsw,  Ki, "movl %M0, %W=" },
	{ Oloaduw,  Ki, "movl %M0, %W=" },
	{ Oloadsh,  Ki, "movsw%k %M0, %=" },
	{ Oloaduh,  Ki, "movzw%k %M0, %=" },
	{ Oloadsb,  Ki, "movsb%k %M0, %=" },
	{ Oloadub,  Ki, "movzb%k %M0, %=" },
	{ Oextsw,   Ki, "movl %W0, %W=" },
	{ Oextuw,   Ki, "movl %W0, %W=" },
	{ Oextsh,   Ki, "movsw%k %H0, %=" },
	{ Oextuh,   Ki, "movzw%k %H0, %=" },
	{ Oextsb,   Ki, "movsb%k %B0, %=" },
	{ Oextub,   Ki, "movzb%k %B0, %=" },

	{ Oaddr,    Ki, "lea%k %M0, %=" },
	{ Oswap,    Ki, "xchg%k %0, %1" },
	{ Osign,    Kl, "cltd" },
	{ Osign,    Kw, "cltd" },
	{ Oxdiv,    Ki, "div%k %0" },
	{ Oxidiv,   Ki, "idiv%k %0" },
	{ Oxcmp,    Ki, "cmp%k %0, %1" },
	{ Oxtest,   Ki, "test%k %0, %1" },
#define X(c, s, _) \
	{ Oflag+c,  Ki, "set" s " %B=\n\tmovzb%k %B=, %=" },
	CMP(X)
#undef X
	{ Oflagfeq, Ki, "setz %B=\n\tmovzb%k %B=, %=" },
	{ Oflagfne, Ki, "setnz %B=\n\tmovzb%k %B=, %=" },
	{ NOp, 0, 0 }
};

static char cmov[][2][16] = {
#define X(c, s0, s1) \
	[c] = { \
		"cmov" s0 " %0, %=", \
		"cmov" s1 " %1, %=", \
	},
	CMP(X)
#undef X
};

/* Jump-if-condition-NOT-met, indexed by xsel condition code.
 * Used for Kl xsel where we copy the false value first, then
 * skip the true-value copy if the condition is not met. */
static const char *xsel_jcc[] = {
#define X(c, s0, s1) [c] = "j" s1,
	CMP(X)
#undef X
};

/* i386 register names.
 * [0] = 32-bit (SLong), [1] = 32-bit (SWord),
 * [2] = 16-bit (SShort), [3] = 8-bit (SByte)
 * Note: ESI/EDI/EBP/ESP have no 8-bit sub-register;
 * isel ensures 8-bit ops don't use them.
 */
static char *rname[][4] = {
	[EAX] = {"eax", "eax", "ax", "al"},
	[ECX] = {"ecx", "ecx", "cx", "cl"},
	[EDX] = {"edx", "edx", "dx", "dl"},
	[EBX] = {"ebx", "ebx", "bx", "bl"},
	[ESI] = {"esi", "esi", "si", "esi"},
	[EDI] = {"edi", "edi", "di", "edi"},
	[EBP] = {"ebp", "ebp", "bp", "ebp"},
	[ESP] = {"esp", "esp", "sp", "esp"},
};


static int
slotoff(int s, E *e)
{
	assert(s <= e->fn->slot);
	/* specific to NAlign == 3 */
	if (s < 0) {
		if (e->fp == ESP)
			return 4*-s - 4 + e->fsz + e->nclob*4;
		else
			return 4*-s;
	}
	else if (e->fp == ESP)
		return 4*s + e->nclob*4;
	else
		return -4 * (e->fn->slot - s);
}

static int
slot(Ref r, E *e)
{
	return slotoff(rsval(r), e);
}

/* Forward declarations for Kl decomposition helpers (defined below). */
static char *regtoa(int reg, int sz);
static void emitf(char *s, Ins *i, E *e);

/* ---- Kl (64-bit) decomposition helpers ----
 *
 * i386 has no 64-bit GPR; kl_in_reg == 0. Kl-class values live in
 * 8-byte stack slots: low 32 bits at slot offset, high 32 bits at
 * slot offset + 4. spill.c assigns slots to Kl temps and rewrites
 * Kl RTmp arguments to RSlot before emit. Kl ops reach emit as the
 * original IR (e.g. Oadd Kl, to=RTmp Kl, args=RSlot/RCon), plus a
 * trailing Ostorel Kl that spills the Kl result to its slot (which
 * is redundant when the destination slot equals the result's own
 * slot, and is detected/skipped below).
 *
 * Convention: an Ocopy Kl with TMP(EAX) as one operand denotes the
 * EAX:EDX register pair (low:high). selret/selcall use this to model
 * 64-bit return values across the function ABI.
 *
 * All Kl decomposition uses EAX (and EDX for the EAX:EDX convention)
 * as scratch. movl does not modify EFLAGS, so addl/adcl, subl/sbbl,
 * and negl/sbbl sequences preserve carry between the low and high
 * halves. */

/* True if r is a Kl-class reference that lives in a stack slot. */
static int
kl_isslot(Ref r)
{
	if (rtype(r) == RSlot)
		return 1;
	if (rtype(r) == RTmp && !isreg(r))
		return 1;
	return 0;
}

/* Byte offset of Kl half within a slot-based reference.
 * hi=0 → low half, hi=1 → high half. */
static int
kloffset(Ref r, int hi, E *e)
{
	int s;
	if (rtype(r) == RSlot)
		s = rsval(r);
	else if (rtype(r) == RTmp && r.val >= Tmp0) {
		/* Kl temp: read its assigned slot. */
		s = e->fn->tmp[r.val].slot;
		assert(s != -1);
	} else {
		/* Machine register or other ref: this can happen when
		 * optimization passes replace a Kl temp with a Kw
		 * register. Return a sentinel offset; kl_load_to and
		 * kl_store_from callers must handle this case. */
		return -1;
	}
	return slotoff(s, e) + 4*hi;
}

/* Emit `movl <src_half>, %<reg>` for slot-based or constant sources.
 * For RCon: prints `$imm` (the high or low 32-bit half, sign-extended
 * to int32 — same bit pattern as the unsigned 32-bit half). */
static void
kl_load_to(Ref r, int hi, int reg, E *e)
{
	int64_t val;
	int off;
	if (rtype(r) == RCon) {
		assert(e->fn->con[r.val].type == CBits);
		val = e->fn->con[r.val].bits.i;
		fprintf(e->f, "\tmovl $%d, %%%s\n",
			(int)(int32_t)(hi ? (val >> 32) : val),
			regtoa(reg, SLong));
		return;
	}
	off = kloffset(r, hi, e);
	if (off == -1) {
		/* Machine register: use it directly for low half,
		 * zero for high half (unsigned extension). */
		if (!hi && rtype(r) == RTmp && r.val < Tmp0)
			fprintf(e->f, "\tmovl %%%s, %%%s\n",
				regtoa(r.val, SLong),
				regtoa(reg, SLong));
		else
			fprintf(e->f, "\txorl %%%s, %%%s\n",
				regtoa(reg, SLong),
				regtoa(reg, SLong));
	} else {
		fprintf(e->f, "\tmovl %d(%%%s), %%%s\n",
			off, regtoa(e->fp, SLong), regtoa(reg, SLong));
	}
}

/* Emit `movl %<reg>, <dst_half>` for slot-based destinations. */
static void
kl_store_from(Ref r, int hi, int reg, E *e)
{
	int off = kloffset(r, hi, e);
	if (off == -1)
		return; /* Machine register destination: skip store */
	fprintf(e->f, "\tmovl %%%s, %d(%%%s)\n",
		regtoa(reg, SLong),
		off, regtoa(e->fp, SLong));
}

/* Emit a Kl half as a source operand for instructions like addl/andl.
 * RCon → `$imm`; slot-based → `off(%ebp)`. No leading tab or trailing
 * newline; caller assembles the surrounding instruction. */
static void
kl_operand(Ref r, int hi, E *e)
{
	int64_t val;
	int off;
	if (rtype(r) == RCon) {
		assert(e->fn->con[r.val].type == CBits);
		val = e->fn->con[r.val].bits.i;
		fprintf(e->f, "$%d",
			(int)(int32_t)(hi ? (val >> 32) : val));
		return;
	}
	off = kloffset(r, hi, e);
	if (off == -1) {
		/* Machine register: use $0 for high half (workaround). */
		fprintf(e->f, "$0");
	} else {
		fprintf(e->f, "%d(%%%s)",
			off, regtoa(e->fp, SLong));
	}
}

/* Emit `movl <addr+delta>, %eax` for a Kl load (half = low if delta==0,
 * high if delta==4). Reuses emitf's %M0 memory-operand machinery by
 * temporarily adjusting the operand's offset, then restoring it. */
static void
kl_load_mem_eax(Ref addr, int delta, E *e)
{
	Ins fake;
	Con save_m, adj, *c;
	int64_t save_c;

	fake.op = Oload;
	fake.cls = Kw;
	fake.to = TMP(EAX);
	fake.arg[0] = addr;
	fake.arg[1] = R;

	if (delta == 0) {
		emitf("movl %M0, %=", &fake, e);
		return;
	}
	switch (rtype(addr)) {
	case RMem:
		save_m = e->fn->mem[addr.val].offset;
		adj.type = CBits;
		adj.bits.i = delta;
		addcon(&e->fn->mem[addr.val].offset, &adj, 1);
		emitf("movl %M0, %=", &fake, e);
		e->fn->mem[addr.val].offset = save_m;
		break;
	case RCon:
		c = &e->fn->con[addr.val];
		save_c = c->bits.i;
		c->bits.i += delta;
		emitf("movl %M0, %=", &fake, e);
		c->bits.i = save_c;
		break;
	case RSlot:
		fprintf(e->f, "\tmovl %d(%%%s), %%eax\n",
			slot(addr, e) + delta,
			regtoa(e->fp, SLong));
		break;
	default:
		die("i386: Kl load from rtype %d not supported", rtype(addr));
	}
}

/* Emit `movl %<vreg>, <addr+delta>` for a Kl store. The value register
 * `vreg` is chosen by the caller to avoid colliding with the address
 * base register (e.g., ECX when the address uses EAX). */
static void
kl_store_eax_mem_reg(Ref addr, int delta, int vreg, E *e)
{
	Ins fake;
	Con save_m, adj, *c;
	int64_t save_c;
	char *vregname = regtoa(vreg, SLong);

	fake.op = Ostorew;
	fake.cls = Kw;
	fake.to = R;
	fake.arg[0] = TMP(vreg);
	fake.arg[1] = addr;

	if (delta == 0) {
		emitf("movl %W0, %M1", &fake, e);
		return;
	}
	switch (rtype(addr)) {
	case RMem:
		save_m = e->fn->mem[addr.val].offset;
		adj.type = CBits;
		adj.bits.i = delta;
		addcon(&e->fn->mem[addr.val].offset, &adj, 1);
		emitf("movl %W0, %M1", &fake, e);
		e->fn->mem[addr.val].offset = save_m;
		break;
	case RCon:
		c = &e->fn->con[addr.val];
		save_c = c->bits.i;
		c->bits.i += delta;
		emitf("movl %W0, %M1", &fake, e);
		c->bits.i = save_c;
		break;
	case RSlot:
		fprintf(e->f, "\tmovl %%%s, %d(%%%s)\n",
			vregname,
			slot(addr, e) + delta,
			regtoa(e->fp, SLong));
		break;
	case RTmp:
		fprintf(e->f, "\tmovl %%%s, %d(%%%s)\n",
			vregname, delta,
			regtoa(addr.val, SLong));
		break;
	default:
		die("i386: Kl store to rtype %d not supported", rtype(addr));
	}
}

static void
emitcon(Con *con, E *e)
{
	char *p, *l;

	switch (con->type) {
	case CAddr:
		l = str(con->sym.id);
		p = l[0] == '"' ? "" : T.assym;
		if (con->sym.type == SThr) {
			/* Local-exec TLS (variant II): the symbol is a negative
			 * displacement from the %gs thread pointer.  Without
			 * @ntpoff the assembler treats sym as an absolute address
			 * and the load/store hits unmapped memory. */
			fprintf(e->f, "%%gs:%s%s@ntpoff", p, l);
		} else {
			assert((con->sym.type & ~SExt) == SGlo);
			fprintf(e->f, "%s%s", p, l);
		}
		if (con->bits.i)
			fprintf(e->f, "%+"PRId64, con->bits.i);
		break;
	case CBits:
		fprintf(e->f, "%"PRId64, con->bits.i);
		break;
	default:
		die("unreachable");
	}
}

static char *
regtoa(int reg, int sz)
{
	assert(reg <= ESP);
	return rname[reg][sz];
}

static Ref
getarg(char c, Ins *i)
{
	switch (c) {
	case '0':
		return i->arg[0];
	case '1':
		return i->arg[1];
	case '=':
		return i->to;
	default:
		die("invalid arg letter %c", c);
	}
}

static void emitins(Ins, E *);

static void
emitcopy(Ref r1, Ref r2, int k, E *e)
{
	Ins icp;

	icp.op = Ocopy;
	icp.arg[0] = r2;
	icp.to = r1;
	icp.cls = k;
	emitins(icp, e);
}

static void
emitf(char *s, Ins *i, E *e)
{
	static char clstoa[][3] = {"l", "l", "ss", "sd"};
	char c;
	int sz;
	Ref ref;
	Mem *m;
	Con off;

	switch (*s) {
	case '+':
		if (req(i->arg[1], i->to)) {
			ref = i->arg[0];
			i->arg[0] = i->arg[1];
			i->arg[1] = ref;
		}
		/* fall through */
	case '-':
		assert((!req(i->arg[1], i->to) || req(i->arg[0], i->to)) &&
			"cannot convert to 2-address");
		emitcopy(i->to, i->arg[0], i->cls, e);
		s++;
		break;
	}

	fputc('\t', e->f);
Next:
	while ((c = *s++) != '%')
		if (!c) {
			fputc('\n', e->f);
			return;
		} else
			fputc(c, e->f);
	switch ((c = *s++)) {
	case '%':
		fputc('%', e->f);
		break;
	case 'k':
		fputs(clstoa[i->cls], e->f);
		break;
	case '0':
	case '1':
	case '=':
		sz = SWord; /* always 32-bit on i386 */
		s--;
		goto Ref;
	case 'D':
	case 'S':
		sz = SLong;
	Ref:
		c = *s++;
		ref = getarg(c, i);
		switch (rtype(ref)) {
		case RTmp:
			assert(isreg(ref));
			fprintf(e->f, "%%%s", regtoa(ref.val, sz));
			break;
		case RSlot:
			fprintf(e->f, "%d(%%%s)",
				slot(ref, e),
				regtoa(e->fp, SLong)
			);
			break;
		case RMem:
		Mem:
			m = &e->fn->mem[ref.val];
			if (rtype(m->base) == RSlot) {
				off.type = CBits;
				off.bits.i = slot(m->base, e);
				addcon(&m->offset, &off, 1);
				m->base = TMP(e->fp);
			}
			if (m->offset.type != CUndef)
				emitcon(&m->offset, e);
			/* A symbol with no base is an absolute memory operand on
			 * i386; do not print the empty `()` accepted by neither GAS
			 * nor the linker. */
			if (!req(m->base, R) || !req(m->index, R)) {
				fputc('(', e->f);
				if (!req(m->base, R))
					fprintf(e->f, "%%%s",
						regtoa(m->base.val, SLong)
					);
				if (!req(m->index, R))
					fprintf(e->f, ", %%%s, %d",
						regtoa(m->index.val, SLong),
						m->scale
					);
				fputc(')', e->f);
			}
			break;
		case RCon:
			fputc('$', e->f);
			emitcon(&e->fn->con[ref.val], e);
			break;
		default:
			die("unreachable");
		}
		break;
	case 'L':
		sz = SLong;
		goto Ref;
	case 'W':
		sz = SWord;
		goto Ref;
	case 'H':
		sz = SShort;
		goto Ref;
	case 'B':
		sz = SByte;
		goto Ref;
	case 'M':
		c = *s++;
		ref = getarg(c, i);
		switch (rtype(ref)) {
		case RMem:
			goto Mem;
		case RSlot:
			fprintf(e->f, "%d(%%%s)",
				slot(ref, e),
				regtoa(e->fp, SLong)
			);
			break;
		case RCon:
			off = e->fn->con[ref.val];
			emitcon(&off, e);
			break;
		case RTmp:
			assert(isreg(ref));
			fprintf(e->f, "(%%%s)", regtoa(ref.val, SLong));
			break;
		default:
			die("unreachable");
		}
		break;
	default:
		die("invalid format specifier %%%c", c);
	}
	goto Next;
}

static int float_label;

/* The i386 target keeps Ks/Kd values in stack slots and uses the x87 stack
 * only as a short-lived execution stack.  This avoids pretending that x87
 * registers are ordinary allocatable SSA registers. */
static Ref
float_ref(Ref r, E *e)
{
	if (rtype(r) == RTmp && !isreg(r)) {
		int s = e->fn->tmp[r.val].slot;
		assert(s != -1);
		return SLOT(s);
	}
	return r;
}

static void
float_load(Ref r, int k, E *e)
{
	Ins f;
	int64_t v;
	int n = KWIDE(k) ? 8 : 4;

	r = float_ref(r, e);
	if (req(r, R))
		return; /* ST(0) already contains the value. */
	if (rtype(r) == RCon && e->fn->con[r.val].type == CBits) {
		v = e->fn->con[r.val].bits.i;
		fprintf(e->f, "\tsubl $%d, %%esp\n", n);
		fprintf(e->f, "\tmovl $%d, (%%esp)\n", (int)(uint32_t)v);
		if (n == 8)
			fprintf(e->f, "\tmovl $%d, 4(%%esp)\n", (int)(uint32_t)((uint64_t)v >> 32));
		fprintf(e->f, "\tfld%c (%%esp)\n", n == 8 ? 'l' : 's');
		fprintf(e->f, "\taddl $%d, %%esp\n", n);
		return;
	}
	f = (Ins){.op = Oload, .cls = k, .to = TMP(EAX), .arg = {r, R}};
	emitf(n == 8 ? "fldl %M0" : "flds %M0", &f, e);
}

static void
float_store(Ref r, int k, E *e)
{
	Ins f;

	if (req(r, R)) {
		fprintf(e->f, "\tfstp %%st(0)\n");
		return;
	}
	r = float_ref(r, e);
	f = (Ins){.op = Ostorew, .cls = k, .to = R, .arg = {R, r}};
	emitf(KWIDE(k) ? "fstpl %M1" : "fstps %M1", &f, e);
}

static void
float_binary(Ins i, E *e)
{
	char *op;

	float_load(i.arg[0], i.cls, e);
	switch (i.op) {
	case Oadd: op = KWIDE(i.cls) ? "faddl %M1" : "fadds %M1"; break;
	case Osub: op = KWIDE(i.cls) ? "fsubl %M1" : "fsubs %M1"; break;
	case Omul: op = KWIDE(i.cls) ? "fmull %M1" : "fmuls %M1"; break;
	case Odiv: op = KWIDE(i.cls) ? "fdivl %M1" : "fdivs %M1"; break;
	default: die("i386: invalid x87 binary op %d", i.op);
	}
	emitf(op, &i, e);
	float_store(i.to, i.cls, e);
}

static void
float_branch_false(int c, int label, E *e)
{
	int aux;

	/* x87 fcom/fnstsw/sahf exposes ZF/PF/CF with the same unordered
	 * convention as ucomiss/ucomisd.  The branch sequences below encode
	 * the C/QBE ordered and unordered predicates without touching GPRs. */
	switch (c) {
	case Cfeq:
		fprintf(e->f, "\tjp .Lff%d\n\tjne .Lff%d\n", label, label);
		break;
	case Cfne:
		aux = ++float_label;
		fprintf(e->f, "\tjnp .Lfo%d\n\tjmp .Lfc%d\n.Lfo%d:\n\tje .Lff%d\n.Lfc%d:\n",
			aux, label, aux, label, label);
		break;
	case Cfge:
		fprintf(e->f, "\tjp .Lff%d\n\tjb .Lff%d\n", label, label);
		break;
	case Cfgt:
		fprintf(e->f, "\tjp .Lff%d\n\tjbe .Lff%d\n", label, label);
		break;
	case Cfle:
		fprintf(e->f, "\tjp .Lff%d\n\tja .Lff%d\n", label, label);
		break;
	case Cflt:
		fprintf(e->f, "\tjp .Lff%d\n\tjae .Lff%d\n", label, label);
		break;
	case Cfo:
		fprintf(e->f, "\tjp .Lff%d\n", label);
		break;
	case Cfuo:
		fprintf(e->f, "\tjnp .Lff%d\n", label);
		break;
	default:
		die("i386: invalid x87 condition %d", c);
	}
}

static void
float_flag(Ins i, E *e)
{
	int c, bad, end;

	c = i.op - Oflag - NCmpI;
	bad = ++float_label;
	end = ++float_label;
	float_branch_false(c, bad, e);
	emitcopy(i.to, getcon(1, e->fn), Kw, e);
	fprintf(e->f, "\tjmp .Lfe%d\n.Lff%d:\n", end, bad);
	emitcopy(i.to, getcon(0, e->fn), Kw, e);
	fprintf(e->f, ".Lfe%d:\n", end);
}

static int
emit_float(Ins i, E *e)
{
	int c, false_label;

	if (i.op == Ocall || i.op == Odbgloc || i.op == Opar)
		return 0;
	if (i.op == Ostores || i.op == Ostored) {
		int fk = i.op == Ostores ? Ks : Kd;
		float_load(i.arg[0], fk, e);
		float_store(i.arg[1], fk, e);
		return 1;
	}
	if (i.op == Ocopy && KBASE(i.cls) == 1) {
		if (req(i.to, R))
			float_load(i.arg[0], i.cls, e);
		else {
			float_load(i.arg[0], i.cls, e);
			float_store(i.to, i.cls, e);
		}
		return 1;
	}
	if (KBASE(i.cls) == 1) {
		switch (i.op) {
		case Oload:
			float_load(i.arg[0], i.cls, e);
			float_store(i.to, i.cls, e);
			return 1;
		case Oadd:
		case Osub:
		case Omul:
		case Odiv:
			float_binary(i, e);
			return 1;
		case Oxcmp:
			/* fcompp writes the x87 condition bits; fnstsw/sahf maps
			 * them to ZF/PF/CF without consuming a GPR value. */
			float_load(i.arg[0], i.cls, e);
			float_load(i.arg[1], i.cls, e);
			fprintf(e->f, "\tfcompp\n\tpushl %%eax\n\tfnstsw %%ax\n\tsahf\n\tpopl %%eax\n");
			return 1;
		case Oneg:
			float_load(i.arg[0], i.cls, e);
			fprintf(e->f, "\tfchs\n");
			float_store(i.to, i.cls, e);
			return 1;
		case Oexts:
			float_load(i.arg[0], Ks, e);
			float_store(i.to, Kd, e);
			return 1;
		case Otruncd:
			float_load(i.arg[0], Kd, e);
			float_store(i.to, Ks, e);
			return 1;
		case Oswtof:
		case Osltof:
			if (i.op == Oswtof)
				emitf("fildl %M0", &i, e);
			else
				emitf("fildq %M0", &i, e);
			float_store(i.to, i.cls, e);
			return 1;
		case Ouwtof:
		case Oultof:
			die("i386: unsigned integer to float not yet supported");
		case Ocast:
			return 0;
		default:
			break;
		}
	}
	if (i.op == Ostosi || i.op == Odtosi) {
		float_load(i.arg[0], i.op == Ostosi ? Ks : Kd, e);
		if (i.cls == Kl)
			emitf("fisttpq %M=", &i, e);
		else
			emitf("fisttpl %M=", &i, e);
		return 1;
	}
	if (i.op == Oswap && KBASE(i.cls) == 1) {
		float_load(i.arg[0], i.cls, e);
		float_load(i.arg[1], i.cls, e);
		float_store(i.arg[0], i.cls, e);
		float_store(i.arg[1], i.cls, e);
		return 1;
	}
	if (isxsel(i.op) && KBASE(i.cls) == 1) {
		c = i.op - Oxsel;
		false_label = ++float_label;
		float_load(i.arg[1], i.cls, e);
		float_store(i.to, i.cls, e);
		float_branch_false(c, false_label, e);
		float_load(i.arg[0], i.cls, e);
		float_store(i.to, i.cls, e);
		fprintf(e->f, ".Lff%d:\n", false_label);
		return 1;
	}
	return 0;
}

static void
emitins(Ins i, E *e)
{
	Ref r;
	int64_t val;
	int o, t0;
	Ins ineg;
	Con *con;
	char *sym;

	if (i.op >= Oflag + NCmpI && i.op <= Oflag + NCmpI + Cfuo) {
		float_flag(i, e);
		return;
	}
	if (emit_float(i, e))
		return;

	/* ---- Kl (64-bit) op decomposition ----
	 * i386 has no 64-bit GPR; Kl ops are decomposed into Kw ops on
	 * the low/high halves of slot-based Kl values. See the helper
	 * block above for conventions. After this block returns, the
	 * generic switch below only handles Kw/Ks/Kd. */
	if (i.cls == Kl || i.op == Ostorel) {
		switch (i.op) {
		case Ocopy:
			/* selret/selcall convention: TMP(EAX) as one
			 * operand denotes the EAX:EDX pair (low:high). */
			if (req(i.to, R) || req(i.arg[0], R))
				return;
			if (req(i.to, i.arg[0]))
				return;
			if (req(i.to, TMP(EAX))) {
				/* Kl slot → EAX:EDX pair */
				assert(kl_isslot(i.arg[0]) || rtype(i.arg[0]) == RCon);
				kl_load_to(i.arg[0], 0, EAX, e);
				kl_load_to(i.arg[0], 1, EDX, e);
				return;
			}
			if (req(i.arg[0], TMP(EAX))) {
				/* EAX:EDX pair → Kl slot */
				assert(kl_isslot(i.to));
				kl_store_from(i.to, 0, EAX, e);
				kl_store_from(i.to, 1, EDX, e);
				return;
			}
			/* Generic slot/con → slot copy via EAX. */
			assert(kl_isslot(i.to));
			kl_load_to(i.arg[0], 0, EAX, e);
			kl_store_from(i.to, 0, EAX, e);
			kl_load_to(i.arg[0], 1, EAX, e);
			kl_store_from(i.to, 1, EAX, e);
			return;
		case Oload:
			/* Kl load from memory: two movl, low then high.
			 * kl_load_mem_eax hard-codes EAX as the load target, so
			 * if the address itself is in EAX the first movl
			 * clobbers the address before the high half is read.
			 * Move the address to ECX first in that case. */
			assert(kl_isslot(i.to));
			{
				Ref addr = i.arg[0];
				int stash = 0;
				/* kl_load_mem_eax hard-codes EAX as the load target.
				 * If the address (or a memory operand's base) is EAX,
				 * the first movl clobbers it before the high half is
				 * read.  Stash the address into ECX and load from
				 * there.  This covers both RTmp==EAX and RMem with
				 * base==EAX. */
				if (req(addr, TMP(EAX))) {
					fprintf(e->f, "\tmovl %%eax, %%ecx\n");
					addr = TMP(ECX);
					stash = 1;
				} else if (rtype(addr) == RMem
				&& req(e->fn->mem[addr.val].base, TMP(EAX))) {
					fprintf(e->f, "\tmovl %%eax, %%ecx\n");
					e->fn->mem[addr.val].base = TMP(ECX);
					stash = 2;
				}
				kl_load_mem_eax(addr, 0, e);
				kl_store_from(i.to, 0, EAX, e);
				kl_load_mem_eax(addr, 4, e);
				kl_store_from(i.to, 1, EAX, e);
				if (stash == 2)
					e->fn->mem[i.arg[0].val].base = TMP(EAX);
			}
			return;
		case Ostorel:
			/* Spill may emit `Ostorel Kl, R, RTmp(t), SLOT(s)`
			 * where s == tmp[t].slot, to spill a Kl result to its
			 * own slot. Detect and skip — the result is already
			 * there. */
			if (rtype(i.arg[0]) == RTmp && rtype(i.arg[1]) == RSlot
			&& e->fn->tmp[i.arg[0].val].slot == rsval(i.arg[1])
			&& !isreg(i.arg[0]))
				return;
			/* Otherwise emit two movl from a slot-based or
			 * constant source (i.arg[0]) to a memory destination
			 * (i.arg[1]). kl_load_to handles RCon and slot-based
			 * refs; RTmp Kl temps are slot-based (kl_in_reg=0).
			 *
			 * Normally we use EAX as the source register. But if
			 * the destination address (i.arg[1]) uses EAX as its
			 * base/index register, loading the value into EAX
			 * would clobber the address. In that case, use ECX
			 * instead. This happens when selcall's Osalloc result
			 * temp `r` is allocated to EAX by rega. */
		{
			int vreg = EAX;
			if (rtype(i.arg[1]) == RMem) {
				Mem *m = &e->fn->mem[i.arg[1].val];
				if ((rtype(m->base) == RTmp && m->base.val == EAX)
				||  (rtype(m->index) == RTmp && m->index.val == EAX))
					vreg = ECX;
			} else if (rtype(i.arg[1]) == RTmp && i.arg[1].val == EAX) {
				vreg = ECX;
			}
			kl_load_to(i.arg[0], 0, vreg, e);
			kl_store_eax_mem_reg(i.arg[1], 0, vreg, e);
			kl_load_to(i.arg[0], 1, vreg, e);
			kl_store_eax_mem_reg(i.arg[1], 4, vreg, e);
			return;
		}
		case Oshl:
		case Oshr:
		case Osar:
		{
			/* 64-bit shifts use EAX:EDX as low:high scratch.  The
			 * selector already keeps the count in ECX when it is not an
			 * immediate. */
			static int shlbl;
			int64_t n = -1;
			int l = ++shlbl;
			assert(kl_isslot(i.to));
			kl_load_to(i.arg[0], 0, EAX, e);
			kl_load_to(i.arg[0], 1, EDX, e);
			if (rtype(i.arg[1]) == RCon)
				n = e->fn->con[i.arg[1].val].bits.i & 63;
			else {
				if (rtype(i.arg[1]) == RSlot)
					fprintf(e->f, "\tmovl %d(%%ebp), %%ecx\n", slot(i.arg[1], e));
				fprintf(e->f, "\tandl $63, %%ecx\n\tcmpl $32, %%ecx\n\tjae .Lklsh%d\n", l);
			}
			if (n >= 32) {
				int c = (int)n - 32;
				if (i.op == Oshl) {
					fprintf(e->f, "\tmovl %%eax, %%edx\n\txorl %%eax, %%eax\n\tshll $%d, %%edx\n", c);
				} else {
					fprintf(e->f, "\tmovl %%edx, %%eax\n\txorl %%edx, %%edx\n\t%s $%d, %%eax\n",
						i.op == Osar ? "sarl" : "shrl", c);
				}
			} else if (n >= 0) {
				if (n == 0) {
					/* already in place */
				} else if (i.op == Oshl) {
					fprintf(e->f, "\tshldl $%d, %%eax, %%edx\n\tshll $%d, %%eax\n", (int)n, (int)n);
				} else {
					fprintf(e->f, "\tshrdl $%d, %%edx, %%eax\n\t%s $%d, %%edx\n",
						(int)n, i.op == Osar ? "sarl" : "shrl", (int)n);
				}
			} else {
				if (i.op == Oshl)
					fprintf(e->f, "\tshldl %%cl, %%eax, %%edx\n\tshll %%cl, %%eax\n");
				else
					fprintf(e->f, "\tshrdl %%cl, %%edx, %%eax\n\t%s %%cl, %%edx\n",
						i.op == Osar ? "sarl" : "shrl");
				fprintf(e->f, "\tjmp .Lklshdone%d\n.Lklsh%d:\n", l, l);
				if (i.op == Oshl)
					fprintf(e->f, "\tsubl $32, %%ecx\n\tmovl %%eax, %%edx\n\txorl %%eax, %%eax\n\tshll %%cl, %%edx\n");
				else
					fprintf(e->f, "\tsubl $32, %%ecx\n\tmovl %%edx, %%eax\n\txorl %%edx, %%edx\n\t%s %%cl, %%eax\n",
						i.op == Osar ? "sarl" : "shrl");
				fprintf(e->f, ".Lklshdone%d:\n", l);
			}
			kl_store_from(i.to, 0, EAX, e);
			kl_store_from(i.to, 1, EDX, e);
			return;
		}
		case Oadd:
			/* addl low + adcl high. movl preserves CF. */
			assert(kl_isslot(i.to));
			kl_load_to(i.arg[0], 0, EAX, e);
			fputs("\taddl ", e->f);
			kl_operand(i.arg[1], 0, e);
			fputs(", %eax\n", e->f);
			kl_store_from(i.to, 0, EAX, e);
			kl_load_to(i.arg[0], 1, EAX, e);
			fputs("\tadcl ", e->f);
			kl_operand(i.arg[1], 1, e);
			fputs(", %eax\n", e->f);
			kl_store_from(i.to, 1, EAX, e);
			return;
		case Osub:
			/* subl low + sbbl high. */
			assert(kl_isslot(i.to));
			kl_load_to(i.arg[0], 0, EAX, e);
			fputs("\tsubl ", e->f);
			kl_operand(i.arg[1], 0, e);
			fputs(", %eax\n", e->f);
			kl_store_from(i.to, 0, EAX, e);
			kl_load_to(i.arg[0], 1, EAX, e);
			fputs("\tsbbl ", e->f);
			kl_operand(i.arg[1], 1, e);
			fputs(", %eax\n", e->f);
			kl_store_from(i.to, 1, EAX, e);
			return;
		case Oneg:
			/* Two's complement of 64-bit value:
			 *   negl hi
			 *   negl lo    (sets CF = (lo != 0))
			 *   sbbl $0, hi  (decrement hi if lo was nonzero) */
			assert(kl_isslot(i.to));
			kl_load_to(i.arg[0], 1, EAX, e);
			fputs("\tnegl %eax\n", e->f);
			kl_store_from(i.to, 1, EAX, e);
			kl_load_to(i.arg[0], 0, EAX, e);
			fputs("\tnegl %eax\n", e->f);
			kl_store_from(i.to, 0, EAX, e);
			kl_load_to(i.to, 1, EAX, e);
			fputs("\tsbbl $0, %eax\n", e->f);
			kl_store_from(i.to, 1, EAX, e);
			return;
		case Oand:
		case Oor:
		case Oxor:
			/* Two independent Kw operations on each half. */
			assert(kl_isslot(i.to));
			kl_load_to(i.arg[0], 0, EAX, e);
			fputc('\t', e->f);
			fputs(i.op == Oand ? "andl " :
			      i.op == Oor  ? "orl "  : "xorl ", e->f);
			kl_operand(i.arg[1], 0, e);
			fputs(", %eax\n", e->f);
			kl_store_from(i.to, 0, EAX, e);
			kl_load_to(i.arg[0], 1, EAX, e);
			fputc('\t', e->f);
			fputs(i.op == Oand ? "andl " :
			      i.op == Oor  ? "orl "  : "xorl ", e->f);
			kl_operand(i.arg[1], 1, e);
			fputs(", %eax\n", e->f);
			kl_store_from(i.to, 1, EAX, e);
			return;
		case Ocall:
			/* A Kl-class Ocall denotes a function returning
			 * long long (or a struct <= 8 bytes). The call
			 * instruction itself is the same as Kw; the Kl
			 * class only matters for the result copy, which
			 * selcall emitted as a separate Ocopy Kl. Fall
			 * through to the generic Ocall handling below. */
			break;
		case Oxcmp:
			/* 64-bit comparison: set EFLAGS as if for a signed
			 * "cmp arg1, arg0" (arg1 - arg0, i.e. arg0 cmp arg1).
			 * Strategy:
			 *   - if high halves differ, the sign of their
			 *     difference determines the result (use cmpl);
			 *   - if high halves are equal, the unsigned
			 *     comparison of the low halves determines it.
			 *
			 * Emit:
			 *   movl arg1_hi, %eax
			 *   cmpl arg0_hi, %eax      (sets flags from hi diff)
			 *   jne 1f                   (hi differ -> flags done)
			 *   movl arg1_lo, %eax
			 *   cmpl arg0_lo, %eax       (unsigned compare lows)
			 * 1:
			 *
			 * Note: IR's Oxcmp has args (arg0, arg1) with the
			 * convention `cmp arg0, arg1` meaning arg1 - arg0
			 * (matches amd64). The setCC consumer reads the
			 * flags. jne does not modify flags.
			 */
		{
			static int klcmp_id = 0;
			int id = klcmp_id++;
			kl_load_to(i.arg[1], 1, EAX, e);
			fputs("\tcmpl ", e->f);
			kl_operand(i.arg[0], 1, e);
			fputs(", %eax\n", e->f);
			fprintf(e->f, "\tjne .Lklcmp%d\n", id);
			kl_load_to(i.arg[1], 0, EAX, e);
			fputs("\tcmpl ", e->f);
			kl_operand(i.arg[0], 0, e);
			fputs(", %eax\n", e->f);
			fprintf(e->f, ".Lklcmp%d:\n", id);
			return;
		}
		case Oxtest:
			/* 64-bit test: OR low + OR high, result in EFLAGS.
			 *   movl arg0_lo, %eax
			 *   orl  arg1_lo, %eax    (sets ZF if both low==0)
			 *   movl arg0_hi, %eax
			 *   orl  arg1_hi, %eax     (OR with high)
			 *   testl %eax, %eax        (final ZF)
			 * Simplified: just OR all 4 halves into EAX and test. */
			kl_load_to(i.arg[0], 0, EAX, e);
			fputs("\torl ", e->f);
			kl_operand(i.arg[1], 0, e);
			fputs(", %eax\n", e->f);
			kl_load_to(i.arg[0], 1, EAX, e);
			fputs("\torl ", e->f);
			kl_operand(i.arg[1], 1, e);
			fputs(", %eax\n", e->f);
			fputs("\ttestl %eax, %eax\n", e->f);
			return;
		case Oextsw:
		case Oextuw:
			/* 32-bit -> 64-bit extension.
			 * Low half: movl source -> EAX, store to dst.low.
			 * High half: extsw -> sarl $31; extuw -> xorl. */
			assert(kl_isslot(i.to));
			if (rtype(i.arg[0]) == RCon) {
				int64_t v = e->fn->con[i.arg[0].val].bits.i;
				fprintf(e->f, "\tmovl $%d, %%eax\n", (int)(int32_t)v);
			} else if (rtype(i.arg[0]) == RSlot) {
				fprintf(e->f, "\tmovl %d(%%%s), %%eax\n",
					slotoff(rsval(i.arg[0]), e),
					regtoa(e->fp, SLong));
			} else if (rtype(i.arg[0]) == RTmp && isreg(i.arg[0])) {
				fprintf(e->f, "\tmovl %%%s, %%eax\n",
					regtoa(i.arg[0].val, SLong));
			} else {
				/* RTmp slot-based: load from slot */
				int s = e->fn->tmp[i.arg[0].val].slot;
				assert(s != -1);
				fprintf(e->f, "\tmovl %d(%%%s), %%eax\n",
					slotoff(s, e),
					regtoa(e->fp, SLong));
			}
			kl_store_from(i.to, 0, EAX, e);
			if (i.op == Oextsw)
				fputs("\tsarl $31, %eax\n", e->f);
			else
				fputs("\txorl %eax, %eax\n", e->f);
			kl_store_from(i.to, 1, EAX, e);
			return;
		default:
			if (isxsel(i.op)) {
				/* Kl conditional select: copy false value,
				 * then conditionally copy true value. */
				static int lbl = 0;
				int l = ++lbl;
				kl_load_to(i.arg[1], 0, EAX, e);
				kl_store_from(i.to, 0, EAX, e);
				kl_load_to(i.arg[1], 1, EAX, e);
				kl_store_from(i.to, 1, EAX, e);
				fprintf(e->f, "\t%s .Lxsel%d\n",
					xsel_jcc[i.op-Oxsel], l);
				kl_load_to(i.arg[0], 0, EAX, e);
				kl_store_from(i.to, 0, EAX, e);
				kl_load_to(i.arg[0], 1, EAX, e);
				kl_store_from(i.to, 1, EAX, e);
				fprintf(e->f, ".Lxsel%d:\n", l);
				return;
			}
			die("i386: Kl op %s not yet supported",
				optab[i.op].name);
		}
	}

	switch (i.op) {
	default:
		if (isxsel(i.op))
			goto case_Oxsel;
		if (INRANGE(i.op, Oflag, Oflag1)
		&& (rtype(i.to) == RSlot
			|| (rtype(i.to) == RTmp
				&& isreg(i.to)
				&& i.to.val >= ESI))) {
			/* i386 setCC only accepts 8-bit registers
			 * (al/bl/cl/dl); ESI/EDI/EBP/ESP have no
			 * 8-bit sub-register (no REX prefix on i386).
			 * Also, movzb cannot do memory-to-memory.
			 * Use EAX as intermediate, saved on stack so
			 * a live EAX is not clobbered. pushl/popl do
			 * not modify EFLAGS, so the flags set by the
			 * preceding cmp are preserved for setCC. */
			r = i.to;
			fputs("\tpushl %eax\n", e->f);
			i.to = TMP(EAX);
			for (o=0;; o++) {
				if (omap[o].op == NOp)
					die("no match for %s(%c)",
						optab[i.op].name, "wlsd"[i.cls]);
				if (omap[o].op == i.op)
				if (omap[o].cls == i.cls
				|| (omap[o].cls == Ki && KBASE(i.cls) == 0)
				|| (omap[o].cls == Ka))
					break;
			}
			emitf(omap[o].fmt, &i, e);
			emitcopy(r, TMP(EAX), i.cls, e);
			fputs("\tpopl %eax\n", e->f);
			break;
		}
	Table:
		/* most instructions are just pulled out of
		 * the table omap[], some special cases are
		 * detailed below */
		for (o=0;; o++) {
			if (omap[o].op == NOp)
				die("no match for %s(%c)",
					optab[i.op].name, "wlsd"[i.cls]);
			if (omap[o].op == i.op)
			if (omap[o].cls == i.cls
			|| (omap[o].cls == Ki && KBASE(i.cls) == 0)
			|| (omap[o].cls == Ka))
				break;
		}
		emitf(omap[o].fmt, &i, e);
		break;
	case Onop:
		break;
	case Ostorew:
	case Ostoreh:
	case Ostoreb:
		/* Register allocation can leave a narrow value in a stack slot
		 * (notably the low word of a Kl load narrowed to long). x86 has
		 * no memory-to-memory move, so use EAX as the bridge. */
		if (rtype(i.arg[0]) == RSlot && rtype(i.arg[1]) != RTmp) {
			Ins load = i, store = i;
			load.op = Oload;
			load.cls = Kw;
			load.to = TMP(EAX);
			load.arg[0] = i.arg[0];
			load.arg[1] = R;
			emitf("movl %M0, %=", &load, e);
			store.arg[0] = TMP(EAX);
			emitf(i.op == Ostoreb ? "movb %B0, %M1" :
				 i.op == Ostoreh ? "movw %H0, %M1" :
				 "movl %W0, %M1", &store, e);
			break;
		}
		goto Table;
	case Omul:
		if (rtype(i.arg[1]) == RCon) {
			r = i.arg[0];
			i.arg[0] = i.arg[1];
			i.arg[1] = r;
		}
		if (KBASE(i.cls) == 0
		&& rtype(i.arg[0]) == RCon
		&& rtype(i.arg[1]) == RTmp) {
			emitf("imul%k %0, %1, %=", &i, e);
			break;
		}
		goto Table;
	case Osub:
		if (req(i.to, i.arg[1]) && !req(i.arg[0], i.to)) {
			ineg = (Ins){Oneg, i.cls, i.to, {i.to}};
			emitins(ineg, e);
			emitf("add%k %0, %=", &i, e);
			break;
		}
		goto Table;
	case Oneg:
		if (!req(i.to, i.arg[0]))
			emitf("mov%k %0, %=", &i, e);
		if (KBASE(i.cls) == 0)
			emitf("neg%k %=", &i, e);
		else
			die("i386: float not yet supported");
		break;
	case Odiv:
		/* use a temp to handle 3-address div when
		 * conversion to 2-address in emitf() fails */
		if (req(i.to, i.arg[1])) {
			i.arg[1] = TMP(EAX);
			emitf("mov%k %=, %1", &i, e);
			emitf("mov%k %0, %=", &i, e);
			i.arg[0] = i.to;
		}
		goto Table;
	case Ocopy:
		assert(rtype(i.to) != RMem);
		if (req(i.to, R) || req(i.arg[0], R))
			break;
		if (req(i.to, i.arg[0]))
			break;
		t0 = rtype(i.arg[0]);
		if (isreg(i.to)
		&& t0 == RCon
		&& e->fn->con[i.arg[0].val].type == CAddr) {
			emitf("lea%k %M0, %=", &i, e);
			break;
		}
		if (rtype(i.to) == RSlot
		&& (t0 == RSlot || t0 == RMem)) {
			i.arg[1] = TMP(EAX);
			emitf("mov%k %0, %1", &i, e);
			emitf("mov%k %1, %=", &i, e);
			break;
		}
		emitf("mov%k %0, %=", &i, e);
		break;
	case Oaddr:
		if (rtype(i.arg[0]) != RCon)
			goto Table;
		con = &e->fn->con[i.arg[0].val];
		assert(isreg(i.to) && con->type == CAddr);
		sym = str(con->sym.id);
		switch (con->sym.type) {
		case SThr:
			/* local-exec TLS (variant II): the symbol offset is a
			 * *negative* displacement from the thread pointer at
			 * %gs:0.  Emit @ntpoff so the assembler emits an R_386_TLS_LE
			 * relocation rather than treating the symbol as an absolute
			 * address (which would produce a huge bogus offset and fault). */
			fprintf(e->f, "\tmovl %%gs:0, %%%s\n",
				regtoa(i.to.val, SLong));
			fprintf(e->f, "\tleal %s%s@ntpoff",
				sym[0] == '"' ? "" : T.assym, sym);
			if (con->bits.i)
				fprintf(e->f, "%+"PRId64, con->bits.i);
			fprintf(e->f, "(%%%s), %%%s\n",
				regtoa(i.to.val, SLong),
				regtoa(i.to.val, SLong));
			break;
		case SExtThr:
			/* initial-exec TLS: load offset from GOT */
			assert(!con->bits.i);
			fprintf(e->f, "\tmovl %%gs:0, %%%s\n",
				regtoa(i.to.val, SLong));
			fprintf(e->f,
				"\taddl %s%s@gotntpoff, %%%s\n",
				sym[0] == '"' ? "" : T.assym, sym,
				regtoa(i.to.val, SLong));
			break;
		case SExt:
			/* load address from the GOT */
			assert(!con->bits.i);
			fprintf(e->f,
				"\tmovl %s%s@GOT(%%ebx), %%%s\n",
				sym[0] == '"' ? "" : T.assym, sym,
				regtoa(i.to.val, SLong));
			break;
		default:
			goto Table;
		}
		break;
	case Ocall:
		switch (rtype(i.arg[0])) {
		case RCon:
			con = &e->fn->con[i.arg[0].val];
			fprintf(e->f, "\tcall ");
			emitcon(con, e);
			if (con->type == CAddr
			&& (con->sym.type & SExt))
				fprintf(e->f, "@plt");
			fprintf(e->f, "\n");
			break;
		case RTmp:
			emitf("call *%L0", &i, e);
			break;
		default:
			die("invalid call argument");
		}
		break;
	case Osalloc:
		assert(e->fp == EBP);
		emitf("subl %L0, %%esp", &i, e);
		if (!req(i.to, R)) {
			/* On i386 ILP32, alloca returns a 32-bit pointer.
			 * salloc() in ir_util.c hardcodes Osalloc's class
			 * to Kl, so i.to is a Kl slot. Write ESP to the low
			 * half and 0 to the high half (the upper 32 bits of
			 * a 32-bit pointer). */
			if (i.cls == Kl) {
				assert(kl_isslot(i.to));
				fprintf(e->f, "\tmovl %%esp, %d(%%%s)\n",
					kloffset(i.to, 0, e),
					regtoa(e->fp, SLong));
				fprintf(e->f, "\tmovl $0, %d(%%%s)\n",
					kloffset(i.to, 1, e),
					regtoa(e->fp, SLong));
			} else
				emitcopy(i.to, TMP(ESP), i.cls, e);
		}
		break;
	case Oswap:
		goto Table;
	case Odbgloc:
		emitdbgloc(i.arg[0].val, i.arg[1].val, e->f);
		break;
	case_Oxsel:
		if (i.cls == Kl) {
			/* Kl conditional select: copy false value to
			 * destination, then conditionally copy true
			 * value over it. */
			static int lbl = 0;
			int l = ++lbl;
			/* Copy false value (arg[1]) to destination */
			kl_load_to(i.arg[1], 0, EAX, e);
			kl_store_from(i.to, 0, EAX, e);
			kl_load_to(i.arg[1], 1, EAX, e);
			kl_store_from(i.to, 1, EAX, e);
			/* Skip true-value copy if condition is not met */
			fprintf(e->f, "\t%s .Lxsel%d\n",
				xsel_jcc[i.op-Oxsel], l);
			/* Copy true value (arg[0]) to destination */
			kl_load_to(i.arg[0], 0, EAX, e);
			kl_store_from(i.to, 0, EAX, e);
			kl_load_to(i.arg[0], 1, EAX, e);
			kl_store_from(i.to, 1, EAX, e);
			fprintf(e->f, ".Lxsel%d:\n", l);
			return;
		}
		if (req(i.to, i.arg[1]))
			emitf(cmov[i.op-Oxsel][0], &i, e);
		else {
			if (!req(i.to, i.arg[0]))
				emitf("mov %0, %=", &i, e);
			emitf(cmov[i.op-Oxsel][1], &i, e);
		}
		break;
	}
}

static void
i386_framesz(E *e)
{
	uint64_t f, n, i, pad;

	f = e->fn->slot;
	f = (f + 3) & -4;

	/* count callee-saved registers that will be pushed */
	n = 0;
	for (i=0; i<NCLR; i++)
		if (e->fn->reg & BIT(i386_sysv_rclob[i]))
			n++;

	/* Frame layout with EBP:
	 *   pushl %ebp  (4 bytes)
	 *   ret addr    (4 bytes)
	 *   subl $fsz   (locals)
	 *   pushl x N   (callee-saved, 4 bytes each)
	 * Total below aligned entry: 8 + fsz + 4*n
	 * Want total ≡ 0 (mod 16) for 16-byte alignment.
	 */
	e->fsz = 4 * f;
	pad = (16 - (8 + e->fsz + 4*n) % 16) % 16;
	e->fsz += pad;
}

static int
float_cond_neg(int c)
{
	switch (c) {
	case Cfeq: return Cfne;
	case Cfne: return Cfeq;
	case Cfge: return Cflt;
	case Cfgt: return Cfle;
	case Cfle: return Cfgt;
	case Cflt: return Cfge;
	case Cfo: return Cfuo;
	case Cfuo: return Cfo;
	default: return c;
	}
}

static const char *
float_jcc(int c)
{
	switch (c) {
	case Cfeq: return "e";
	case Cfne: return "ne";
	case Cfge: return "ae";
	case Cfgt: return "a";
	case Cfle: return "be";
	case Cflt: return "b";
	case Cfo: return "np";
	case Cfuo: return "p";
	default: die("i386: invalid float jump condition %d", c);
	}
}

static int
float_ordered(int c)
{	return c != Cfuo && c != Cfne;
}

void
i386_sysv_emitfn(Fn *fn, FILE *f)
{
	static char *ctoa[][2] = {
	#define X(c, s, n) [c] = {s, n},
		CMP(X)
	#undef X
	};
	static int id0;
	Blk *b, *s;
	Ins *i, itmp;
	int *r, c, o, n, lbl;
	uint p;
	E *e;

	e = &(E){.f = f, .fn = fn};

	emitfnlnk(fn->name, &fn->lnk, f);
	e->fp = EBP;
	fputs("\tpushl %ebp\n\tmovl %esp, %ebp\n", f);
	i386_framesz(e);
	if (e->fsz)
		fprintf(f, "\tsubl $%"PRIu64", %%esp\n", e->fsz);
	for (r=i386_sysv_rclob; r<&i386_sysv_rclob[NCLR]; r++)
		if (fn->reg & BIT(*r)) {
			itmp.arg[0] = TMP(*r);
			emitf("pushl %L0", &itmp, e);
			e->nclob++;
		}

	for (lbl=0, b=fn->start; b; b=b->link) {
		if (lbl || b->npred > 1) {
			for (p=0; p<b->npred; p++)
				if (b->pred[p]->id >= b->id)
					break;
			if (p != b->npred)
				fprintf(f, ".p2align 4\n");
			fprintf(f, "%sbb%d:\n", T.asloc, id0+b->id);
		}
		for (i=b->ins; i!=&b->ins[b->nins]; i++)
			emitins(*i, e);
		lbl = 1;
		switch (b->jmp.type) {
		case Jhlt:
			fprintf(f, "\tud2\n");
			break;
		case Jret0:
			if (fn->dynalloc)
				fprintf(f,
					"\tmovl %%ebp, %%esp\n"
					"\tsubl $%"PRIu64", %%esp\n",
					e->fsz + e->nclob * 4);
			for (r=&i386_sysv_rclob[NCLR]; r>i386_sysv_rclob;)
				if (fn->reg & BIT(*--r)) {
					itmp.arg[0] = TMP(*r);
					emitf("popl %L0", &itmp, e);
				}
			fputs("\tleave\n\tret\n", f);
			break;
		case Jjmp:
		Jmp:
			if (b->s1 != b->link)
				fprintf(f, "\tjmp %sbb%d\n",
					T.asloc, id0+b->s1->id);
			else
				lbl = 0;
			break;
		default:
			c = b->jmp.type - Jjf;
			if (0 <= c && c <= NCmp) {
				if (b->link == b->s2) {
					s = b->s1;
					b->s1 = b->s2;
					b->s2 = s;
					n = 0;
				} else
					n = 1;
				if (c >= NCmpI) {
					int fc = c - NCmpI;
					if (n)
						fc = float_cond_neg(fc);
					/* An ordered predicate must route unordered values to
					 * the fall-through edge before testing its integer flags. */
					if (float_ordered(fc))
						fprintf(f, "\tjp %sbb%d\n", T.asloc, id0+b->link->id);
					fprintf(f, "\tj%s %sbb%d\n", float_jcc(fc),
						T.asloc, id0+b->s2->id);
					goto Jmp;
				}
				fprintf(f, "\tj%s %sbb%d\n", ctoa[c][n],
					T.asloc, id0+b->s2->id);
				goto Jmp;
			}
			die("unhandled jump %d", b->jmp.type);
		}
	}
	id0 += fn->nblk;
	elf_emitfnfin(fn->name, f);
}
