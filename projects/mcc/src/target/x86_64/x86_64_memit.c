/* x86_64_memit.c — MFnM -> x86-64 assembly (P3b).
 *
 * Emits the MIR machine layer as AT&T-syntax x86-64.  P3b uses an
 * "all-virtual-registers-on-stack" scheme so the instruction selector can
 * be validated end-to-end (convert + isel + ABI + emit + run) before the
 * real register allocator lands in P4: every MV_TEMP gets an 8-byte stack
 * slot, arithmetic flows through %rax (division uses %rcx for the divisor),
 * floating point through %xmm0.  Correct but slow; P4 replaces the slot
 * mapping with a linear-scan allocator.
 *
 * Purity rule: this reads only MFnM/MMOP/MAddr/MVal — no QBE structures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "x86_64_m.h"

/* PIC/shared-code flag, defined and owned by the MIR machine layer
 * (src/mir/machine.c).  The MIR machine layer deliberately does not read
 * the QBE `Target T` global (purity rule), so TLS emission and any other
 * PIC-sensitive codegen consult this flag instead. */
extern int g_pic;
/* 1 when --specs=meuos links libc-meuos (provides __gxx_personality_v0).
 * DWARF EH personality/lsda references are only emitted in this mode. */
extern int g_meuos_specs;
/* TLS access-model mirror (defined in src/mir/machine.c, set from the
 * driver's `enum tls_model tls_model`).  Values match enum MTlsModel
 * (include/mir.h): DEFAULT=0, GLOBAL_DYNAMIC=1, INITIAL_EXEC=2,
 * LOCAL_EXEC=3.  Consumed by emit_tls_addr to select general-dynamic
 * (GD) emission. */
extern int g_tls_model;

/* ---- width suffixes ----------------------------------------------------- */

static char
width_suffix(MType t)
{
	switch (t) {
	case MT_I8:  return 'b';
	case MT_I16: return 'w';
	case MT_I32: return 'l';
	case MT_I64:
	case MT_PTR: return 'q';
	case MT_F32: return 's';   /* ss */
	case MT_F64: return 'd';   /* sd */
	default:     return 'q';
	}
}

static const char *
cc_suffix(MCC cc)
{
	switch (cc) {
	case MCC_EQ:  return "e";
	case MCC_NE:  return "ne";
	case MCC_CS:  return "ae";   /* carry set  -> unsigned >= */
	case MCC_CC:  return "b";    /* carry clear-> unsigned <  */
	case MCC_HI:  return "a";    /* unsigned > */
	case MCC_LS:  return "be";   /* unsigned <= */
	case MCC_GE:  return "ge";
	case MCC_LT:  return "l";
	case MCC_GT:  return "g";
	case MCC_LE:  return "le";
	default:      return "e";
	}
}

static const MTargetM *g_mt;
static int g_alloca_cur;   /* frame-relative cursor for static allocas */
static MFnM *g_fm;         /* current function (for the VLA/dynalloc flag) */
static bool g_omit_fp;     /* -O2+ leaf: frame pointer elided, rsp base */
static int g_fp_off;       /* rsp-based slot offset adjustment when omitted */

/* DWARF location feedback (src/emit/dwarf.c): record each static alloca's
 * final frame offset so variable DIEs can carry DW_AT_location. */
extern int g_dwarf_level;
extern void dwarf_set_framebase(int);
extern void dwarf_loc_set_stack(uint32_t, int32_t);
extern int g_opt_z;        /* -Oz: aggressive size (driver/main.c) */

static int
alloca_size(MMOP op)
{
	switch (op) {
	case MMOP_ALLOCA4:  return 4;
	case MMOP_ALLOCA8:  return 8;
	default:            return 16;
	}
}

/* per-instruction alloca size: honour an explicit const size (e.g. the
 * 32-byte pad for a SysV va_list) and 16-align it */
static int
alloca_size_ins(const MInsM *in)
{
	if (in->src[0] && in->src[0]->kind == MV_CONST && in->src[0]->con &&
	    in->src[0]->con->kind == MC_INT && in->src[0]->con->u.i > 0)
		return (int)((in->src[0]->con->u.i + 15) & ~15);
	if (in->cst && in->cst->kind == MC_INT && in->cst->u.i > 0)
		return (int)((in->cst->u.i + 15) & ~15);
	return alloca_size(in->op);
}

/* total bytes reserved for static allocas */
static int
alloca_total(MFnM *fm)
{
	int t = 0;
	for (MBlkM *b = fm->link; b; b = b->link)
		for (uint32_t i = 0; i < b->nins; i++) {
			MMOP op = b->ins[i].op;
			if (op == MMOP_ALLOCA4 || op == MMOP_ALLOCA8 ||
			    op == MMOP_ALLOCA16)
				t += alloca_size_ins(&b->ins[i]);
		}
	return t;
}

/* ---- stack slot assignment ---------------------------------------------- */

/* Regalloc assigns MVal.reg (physical) or MVal.slot (spill); this pass
 * only backstops values the allocator missed (defensive; normally none),
 * below the callee-saved save area.  Returns the extra frame bytes. */
static int
assign_extra_slots(MFnM *fm)
{
	int savesz = 0;
	for (int i = 0; g_mt->rclob && g_mt->rclob[i] >= 0; i++)
		if ((fm->regsused >> g_mt->rclob[i]) & 1)
			savesz += 8;
	int off = savesz;
	MVal *ops[8];
	for (MBlkM *b = fm->link; b; b = b->link) {
		for (uint32_t i = 0; i < b->nins; i++) {
			MInsM *in = &b->ins[i];
			ops[0] = in->dst; ops[1] = in->src[0]; ops[2] = in->src[1];
			ops[3] = in->src[2]; ops[4] = in->addr.base;
			ops[5] = in->addr.index;
			ops[6] = b->term.src[0]; ops[7] = b->term.addr.base;
			for (int k = 0; k < 8; k++) {
				MVal *v = ops[k];
				if (v && v->kind == MV_TEMP && v->reg < 0 &&
				    v->slot == -1) {
					off -= 8;
					v->slot = -off;
				}
			}
		}
	}
	return off - savesz;
}

/* ---- float constant pool (per emitted function) -------------------------- */

static MConst **fp_pool;
static uint32_t nfp, cfp;
static const char *g_fname;   /* current function, for unique .LC labels */

/* find or add a float constant, returning its .rodata label index */
static uint32_t
fp_label(MConst *c)
{
	for (uint32_t i = 0; i < nfp; i++) {
		MConst *x = fp_pool[i];
		if (x->type != c->type)
			continue;
		/* Compare the IEEE bit pattern (memcmp), not the value: -0.0 == +0.0
		 * numerically but they differ in the sign bit and must stay distinct
		 * constants (mir_util.c con_pool_find does the same). */
		size_t sz = x->type == MT_F32 ? sizeof(float) : sizeof(double);
		if (memcmp(&x->u, &c->u, sz) == 0)
			return i;
	}
	if (nfp == cfp) {
		cfp = cfp ? cfp * 2 : 8;
		fp_pool = realloc(fp_pool, cfp * sizeof *fp_pool);
	}
	fp_pool[nfp++] = c;
	return nfp - 1;
}

static void
fp_pool_emit(FILE *f)
{
	if (!nfp)
		return;
	fputs(".section .rodata\n", f);
	for (uint32_t i = 0; i < nfp; i++) {
		MConst *c = fp_pool[i];
		fprintf(f, ".L%s.lc%u:\n", g_fname ? g_fname : "f", i);
		if (c->type == MT_F32) {
			union { float s; uint32_t u; } x = { .s = c->u.s };
			fprintf(f, "\t.long %u\n", x.u);
		} else {
			union { double d; uint64_t u; } x = { .d = c->u.d };
			fprintf(f, "\t.quad %llu\n", (unsigned long long)x.u);
		}
	}
}

/* ---- operands ------------------------------------------------------------ */

static void
emit_const(FILE *f, MConst *c)
{
	if (!c) {
		fputs("$0", f);
		return;
	}
	switch (c->kind) {
	case MC_INT:
		fprintf(f, "$%lld", (long long)c->u.i);
		break;
	case MC_FLT:
		fprintf(f, ".L%s.lc%u(%%rip)", g_fname ? g_fname : "f",
		        fp_label(c));
		break;
	case MC_ADDR:
		if (c->u.addr.tls) {
			/* TLS symbol reference: @tpoff for local-exec (non-PIC),
			 * @gottpoff for the initial-exec GOT form under PIC/shared
			 * (TPOFF64 — local-exec relocations are rejected in a DSO). */
			fprintf(f, "%s@%s(%%rip)",
			        c->u.addr.sym ? c->u.addr.sym : "0",
			        g_pic ? "gottpoff" : "tpoff");
		} else {
			fprintf(f, "%s(%%rip)", c->u.addr.sym ? c->u.addr.sym : "0");
		}
		break;
	default:
		fputs("$0", f);
		break;
	}
}

static void emit_mval(FILE *f, MVal *v);

/* 32-bit GPR names (rdi -> edi, r8 -> r8d) for width-aware compares */
static const char *
regname32(MReg r)
{
	static const char *n32[] = {
		"eax", "ecx", "edx", "esi", "edi", "r8d", "r9d", "r10d",
		"r11d", "ebx", "r12d", "r13d", "r14d", "r15d", "ebp", "esp",
	};
	if (r >= 0 && r < 16)
		return n32[r];
	return "eax";
}

/* like emit_mval but 32-bit register names for GPRs */
static void
emit_mval32(FILE *f, MVal *v)
{
	if (v && v->kind == MV_TEMP && v->reg >= 0)
		fprintf(f, "%%%s", regname32(v->reg));
	else if (v && v->kind == MV_REG)
		fprintf(f, "%%%s", regname32(v->reg));
	else
		emit_mval(f, v);
}

static void
emit_mval(FILE *f, MVal *v)
{
	if (!v) {
		fputs("$0", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "%%%s", v->name ? v->name : "rax");
		break;
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "%%%s", mreg_name(g_mt, v->reg));
		else if (g_omit_fp)
			fprintf(f, "%d(%%rsp)", v->slot + g_fp_off);
		else
			fprintf(f, "%d(%%rbp)", v->slot);
		break;
	case MV_GLOBAL:
		fprintf(f, "%s(%%rip)", v->sym ? v->sym : "0");
		break;
	case MV_CONST:
		emit_const(f, v->con);
		break;
	default:
		fputs("$0", f);
		break;
	}
}

/* Emit the address of a TLS global into a scratch register.  Model
 * selection mirrors the driver's `enum tls_model` via g_tls_model
 * (enum MTlsModel values):
 *   - MTLS_GLOBAL_DYNAMIC: general-dynamic via __tls_get_addr
 *       leaq sym@tlsgd(%rip), %rdi     ; R_X86_64_TLSGD, descriptor addr
 *       call __tls_get_addr@PLT        ; R_X86_64_PLT32, TLS addr in %rax
 *     The call clobbers caller-saved regs (incl. rdi), so the result is
 *     taken from %rax and copied to `reg`; this is safe for every caller
 *     (emit_mval writes rax; emit_addr_loads writes r10/r11 — we copy).
 *   - else, PIC (g_pic): initial-exec GOT form
 *       movq sym@gottpoff(%rip), %reg  ; R_X86_64_GOTTPOFF
 *       addq %fs:0, %reg
 *   - else, non-PIC: local-exec
 *       movq %fs:0, %reg ; leaq sym@tpoff(%reg), %reg
 *     (local-exec relocations are rejected in a DSO; mirrors the legacy
 *     x86_64_emit.c SThr path). */
static void
emit_tls_addr(FILE *f, const char *sym, int64_t off, const char *reg)
{
	if (g_tls_model == MTLS_GLOBAL_DYNAMIC) {
		fprintf(f, "\tleaq\t%s@tlsgd(%%rip), %%rdi\n", sym ? sym : "0");
		fprintf(f, "\tcall\t__tls_get_addr@PLT\n");
		if (strcmp(reg, "rax") != 0)
			fprintf(f, "\tmovq\t%%rax, %%%s\n", reg);
		if (off)
			fprintf(f, "\taddq\t$%lld, %%%s\n", (long long)off, reg);
		return;
	}
	if (g_pic) {
		fprintf(f, "\tmovq\t%s@gottpoff(%%rip), %%%s\n", sym ? sym : "0", reg);
		fprintf(f, "\taddq\t%%fs:0, %%%s\n", reg);
		if (off)
			fprintf(f, "\taddq\t$%lld, %%%s\n", (long long)off, reg);
		return;
	}
	fprintf(f, "\tmovq\t%%fs:0, %%%s\n", reg);
	fprintf(f, "\tleaq\t%s@tpoff", sym ? sym : "0");
	if (off)
		fprintf(f, "%+lld", (long long)off);
	fprintf(f, "(%%%s), %%%s\n", reg, reg);
}

/* Load a global symbol's address into `reg`.  PIC: via the GOT
 * (`movq sym@gotpcrel(%rip), %reg` — the base LIR path does the same);
 * otherwise a RIP-relative `leaq`. */
static void
emit_global_addr(FILE *f, const char *sym, int64_t off, const char *reg)
{
	if (g_pic) {
		fprintf(f, "\tmovq\t%s@gotpcrel(%%rip), %%%s\n",
		        sym ? sym : "0", reg);
		if (off)
			fprintf(f, "\taddq\t$%lld, %%%s\n", (long long)off, reg);
	} else {
		fprintf(f, "\tleaq\t%s%+lld(%%rip), %%%s\n",
		        sym ? sym : "0", (long long)off, reg);
	}
}

/* Emit loads of non-register base/index into r10/r11 (before the memory
 * instruction) so x86 addressing only sees registers.  Global symbols are
 * addresses -> leaq; TLS globals -> fs-relative address; virtual slots ->
 * movq. */
static void
emit_addr_loads(FILE *f, MAddr a)
{
	if (a.base && a.base->kind != MV_REG) {
		if (a.base->kind == MV_GLOBAL) {
			if (a.base->tls)
				emit_tls_addr(f, a.base->sym, 0, "r10");
			else
				emit_global_addr(f, a.base->sym, 0, "r10");
		} else {
			fputs("\tmovq\t", f);
			emit_mval(f, a.base);
			fputs(", %r10\n", f);
		}
	}
	if (a.index && a.index->kind != MV_REG) {
		if (a.index->kind == MV_GLOBAL) {
			if (a.index->tls)
				emit_tls_addr(f, a.index->sym, 0, "r11");
			else
				emit_global_addr(f, a.index->sym, 0, "r11");
		} else {
			fputs("\tmovq\t", f);
			emit_mval(f, a.index);
			fputs(", %r11\n", f);
		}
	}
}

/* Print the AT&T memory operand: disp(base, index, scale).  The base
 * follows the displacement without a comma (`8(%rsp)`); a comma separates
 * base from index, and the index always carries a scale. */
static void
emit_addr(FILE *f, MAddr a)
{
	/* AT&T: disp is written before the parens: 8(%rsp) */
	if (a.offcon) {
		fprintf(f, "%s", a.offcon->u.addr.sym ? a.offcon->u.addr.sym : "0");
		if (a.off)
			fprintf(f, "%+lld", (long long)a.off);
	} else if (a.off) {
		fprintf(f, "%lld", (long long)a.off);
	}
	fputc('(', f);
	if (a.base) {
		if (a.base->kind == MV_REG)
			emit_mval(f, a.base);
		else
			fputs("%r10", f);
	}
	if (a.index) {
		fputc(',', f);
		if (a.index->kind == MV_REG)
			emit_mval(f, a.index);
		else
			fputs("%r11", f);
		fprintf(f, ",%u", a.scale ? a.scale : 1);
	}
	fputs(")", f);
}

/* Load a value (or immediate) into %rax.
 * MV_GLOBAL / MC_ADDR are POINTERS (symbol addresses) -> leaq.
 * MV_CONST(MC_INT) / MV_TEMP / MV_REG -> movq. */
static void
mov_to_rax(FILE *f, MVal *v, MConst *c)
{
	if (c) {
		if (c->kind == MC_ADDR) {
			if (c->u.addr.tls) {
				if (g_pic) {
					fprintf(f, "\tmovq\t%s@gottpoff(%%rip), %%rax\n",
					        c->u.addr.sym ? c->u.addr.sym : "0");
					fputs("\taddq\t%fs:0, %rax\n", f);
					if (c->u.addr.off)
						fprintf(f, "\taddq\t$%lld, %%rax\n",
						        (long long)c->u.addr.off);
				} else {
					fprintf(f, "\tleaq\t%s@tpoff", c->u.addr.sym ? c->u.addr.sym : "0");
					if (c->u.addr.off)
						fprintf(f, "%+lld", (long long)c->u.addr.off);
					fprintf(f, "(%%rip), %%rax\n");
					fputs("\taddq\t%fs:0, %rax\n", f);
				}
			} else {
				emit_global_addr(f, c->u.addr.sym, c->u.addr.off, "rax");
			}
		} else if (c->kind == MC_INT) {
			uint64_t uv = (uint64_t)c->u.i;
			/* NOTE: movl (not xorl) for zero — mov never touches the
			 * flags, but xor sets ZF/SF/OF/CF and would corrupt a
			 * cmp->cmovcc sequence that reads the flags set by an
			 * earlier compare.  movl $0,%eax is still 5 bytes vs
			 * movq $0 (7 bytes). */
			if (g_opt_z && uv <= 0xFFFFFFFFULL)
				/* movl zero-extends, so safe only for values whose 64-bit
				 * form equals the 32-bit immediate (5 bytes vs 7) */
				fprintf(f, "\tmovl\t$%llu, %%eax\n", (unsigned long long)uv);
			else
				fprintf(f, "\tmovq\t$%lld, %%rax\n", (long long)c->u.i);
		} else {
			fputs("\tmovq\t$0, %rax\n", f);
		}
		return;
	}
	if (!v) {
		fputs("\tmovq\t$0, %rax\n", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "\tmovq\t%%%s, %%rax\n", v->name);
		break;
	case MV_GLOBAL:
		if (v->tls)
			emit_tls_addr(f, v->sym, 0, "rax");
		else
			fprintf(f, "\tleaq\t%s(%%rip), %%rax\n", v->sym ? v->sym : "0");
		break;
	case MV_CONST:
		mov_to_rax(f, 0, v->con);
		break;
	case MV_TEMP:
		if (v->type == MT_I32 && v->reg < 0) {
			/* 4-byte spill slot: movl zero-extends */
			fputs("\tmovl\t", f);
			emit_mval(f, v);
			fputs(", %eax\n", f);
		} else {
			fputs("\tmovq\t", f);
			emit_mval(f, v);
			fputs(", %rax\n", f);
		}
		break;
	default:
		fputs("\tmovq\t$0, %rax\n", f);
		break;
	}
}

/* Store %rax to dst (register or virtual slot).  4-byte values use movl
 * so a slot4 spill slot is not overrun. */
static void
rax_to_dst(FILE *f, MVal *dst)
{
	if (!dst)
		return;
	if (dst->kind == MV_TEMP && dst->type == MT_I32 && dst->reg < 0) {
		fputs("\tmovl\t%eax, ", f);
		emit_mval(f, dst);
		fputs("\n", f);
		return;
	}
	if (dst->kind == MV_REG)
		fprintf(f, "\tmovq\t%%rax, %%%s\n", dst->name);
	else if (dst->kind == MV_TEMP) {
		fputs("\tmovq\t%rax, ", f);
		emit_mval(f, dst);
		fputs("\n", f);
	}
	/* MV_GLOBAL dst: unsupported for now */
}

/* Does an immediate fit in the x86-64 32-bit signed displacement? */
static bool
imm32(int64_t v)
{
	return v >= INT32_MIN && v <= INT32_MAX;
}

/* Emit the second operand of a binary op / cmp: either an immediate
 * (if it fits) or a value pre-loaded into %r9 (large immediates). */
static void
emit_rhs(FILE *f, MVal *s1, MConst *c)
{
	if (c) {
		if (imm32(c->u.i)) {
			emit_const(f, c);
		} else {
			fprintf(f, "%%r9");   /* caller emitted the load */
		}
		return;
	}
	emit_mval(f, s1);
}

/* Move src (reg/imm/slot/global) into dst slot/reg.  Immediates go
 * directly; symbol addresses need leaq; everything else flows through
 * %rax.  Floating-point moves flow through %xmm0. */
static void
emit_mov(FILE *f, MVal *dst, MVal *src, MConst *c, MType dtype)
{
	if (dtype == MT_F32 || dtype == MT_F64) {
		const char *sd = dtype == MT_F64 ? "sd" : "ss";
		/* direct move: x86 movss/movsd handles reg<->mem; do NOT route
		 * through %xmm0 (it may be an ABI argument/return register) */
		if (c && c->kind == MC_FLT) {
			if (dst) {
				fprintf(f, "\tmov%s\t", sd);
				emit_const(f, c);
				fputs(", ", f);
				emit_mval(f, dst);
				fputs("\n", f);
			}
		} else if (src) {
			bool smem = src->kind == MV_TEMP || src->kind == MV_GLOBAL;
			bool dmem = dst && (dst->kind == MV_TEMP ||
			                    dst->kind == MV_GLOBAL);
			if (smem && dmem) {
				/* mem->mem: bit-copy via r10 */
				fputs("\tmovq\t", f);
				emit_mval(f, src);
				fputs(", %r10\n", f);
				fputs("\tmovq\t%r10, ", f);
				emit_mval(f, dst);
				fputs("\n", f);
			} else if (dst) {
				fprintf(f, "\tmov%s\t", sd);
				emit_mval(f, src);
				fputs(", ", f);
				emit_mval(f, dst);
				fputs("\n", f);
			}
		}
		return;
	}
	if (c && c->kind == MC_INT) {
		if (!dst)
			return;
		if (dtype == MT_I32 && dst->kind == MV_REG) {
			/* 32-bit move with zero-extend: used for setting %eax
			 * before variadic calls (SysV ABI: %al = XMM count). */
			fprintf(f, "\tmovl\t$%lld, ", (long long)c->u.i);
			emit_mval32(f, dst);
		} else {
			fprintf(f, "\tmovq\t$%lld, ", (long long)c->u.i);
			emit_mval(f, dst);
		}
		fputs("\n", f);
		return;
	}
	/* direct moves where x86 allows them (postra: avoid the %rax trip) */
	if (src && dst && src->kind == MV_REG && dst->kind == MV_REG) {
		if (src != dst)
			fprintf(f, "\tmovq\t%%%s, %%%s\n", src->name, dst->name);
		return;   /* same-register copy is a no-op */
	}
	if (src && src->kind == MV_REG && dst && dst->kind == MV_TEMP &&
	    (dst->type != MT_I32 || dst->reg >= 0)) {
		fprintf(f, "\tmovq\t%%%s, ", src->name);
		emit_mval(f, dst);
		fputs("\n", f);
		return;
	}
	if (src && src->kind == MV_TEMP && dst && dst->kind == MV_REG &&
	    (src->type != MT_I32 || src->reg >= 0)) {
		fputs("\tmovq\t", f);
		emit_mval(f, src);
		fprintf(f, ", %%%s\n", dst->name);
		return;
	}
	if (src && src->kind == MV_REG && src->reg == X64MREG_RAX) {
		/* value already in the accumulator */
		rax_to_dst(f, dst);
		return;
	}
	mov_to_rax(f, src, c);
	rax_to_dst(f, dst);
}

/* ---- per-instruction emit ------------------------------------------------- */

static void
emit_ins(FILE *f, MInsM *in)
{
	char w = width_suffix(in->dtype);
	MVal *d = in->dst, *s0 = in->src[0], *s1 = in->src[1];
	MConst *c = in->cst;

	switch (in->op) {
	case MMOP_MOV:
		emit_mov(f, d, s0, c, in->dtype);
		return;
	case MMOP_MOVSX:
	case MMOP_MOVZX: {
		MType st = s0 ? s0->type : in->dtype;
		bool zx = in->op == MMOP_MOVZX;
		if (st == MT_I64 || st == MT_PTR || st == MT_NONE) {
			/* nothing to extend: plain move */
			mov_to_rax(f, s0, 0);
			rax_to_dst(f, d);
			return;
		}
		/* load the value into %rax first, then extend from its low bits
		 * (movz/movs need byte/word/32-bit register or memory sources) */
		mov_to_rax(f, s0, 0);
		if (st == MT_I8)
			fputs(zx ? "\tmovzbq\t%al, %rax\n" : "\tmovsbq\t%al, %rax\n", f);
		else if (st == MT_I16)
			fputs(zx ? "\tmovzwq\t%ax, %rax\n" : "\tmovswq\t%ax, %rax\n", f);
		else if (!zx)
			fputs("\tmovslq\t%eax, %rax\n", f);
		else
			fputs("\tmovl\t%eax, %eax\n", f);   /* i32 zero-extend */
		rax_to_dst(f, d);
		return;
	}
	case MMOP_LEA:
		emit_addr_loads(f, in->addr);
		fputs("\tleaq\t", f);
		emit_addr(f, in->addr);
		fputs(", %rax\n", f);
		rax_to_dst(f, d);
		return;
	case MMOP_ADD: case MMOP_SUB: case MMOP_MUL:
	case MMOP_AND: case MMOP_OR:  case MMOP_XOR:
	case MMOP_SHL: case MMOP_SHR: case MMOP_SAR: {
		/* shifts must honour the operand width: a 64-bit shr pulls the
		 * sign/overflow high bits down into the low word, which breaks
		 * 32-bit bitfield extraction.  Other ops keep 64-bit forms
		 * (low 32 bits match). */
		const char *op;
		switch (in->op) {
		case MMOP_ADD: op = "add"; break;
		case MMOP_SUB: op = "sub"; break;
		case MMOP_MUL: op = "imul"; break;
		case MMOP_AND: op = "and"; break;
		case MMOP_OR:  op = "or"; break;
		case MMOP_XOR: op = "xor"; break;
		case MMOP_SHL: op = "shl"; break;
		case MMOP_SHR: op = "shr"; break;
		default:       op = "sar"; break;
		}
		bool isshift = in->op >= MMOP_SHL && in->op <= MMOP_SAR;
		const char *wsuf = (in->dtype == MT_I64 || in->dtype == MT_PTR)
		                   ? "q" : "l";
		mov_to_rax(f, s0, 0);
		if (isshift) {
			/* shift count in %cl; 32-bit shifts need the 32-bit
			 * accumulator name */
			fputs("\tmovq\t", f);
			emit_mval(f, s1);
			fputs(", %rcx\n", f);
			fprintf(f, "\t%s%s\t%%cl, %s\n", op, wsuf,
			        wsuf[0] == 'l' ? "%eax" : "%rax");
		} else if (c) {
			if (imm32(c->u.i)) {
				fprintf(f, "\t%sq\t", op);
				emit_const(f, c);
				fputs(", %rax\n", f);
			} else {
				/* large immediate: load into %r9 first */
				fprintf(f, "\tmovq\t$%lld, %%r9\n", (long long)c->u.i);
				fprintf(f, "\t%sq\t%%r9, %%rax\n", op);
			}
		} else {
			/* large immediates (MV_CONST operands > 32 bits) need a reg */
			if (s1 && s1->kind == MV_CONST && s1->con &&
			    !imm32(s1->con->u.i)) {
				fprintf(f, "\tmovq\t$%lld, %%r9\n",
				        (long long)s1->con->u.i);
				fprintf(f, "\t%sq\t%%r9, %%rax\n", op);
			} else {
				fprintf(f, "\t%sq\t", op);
				emit_mval(f, s1);
				fputs(", %rax\n", f);
			}
		}
		rax_to_dst(f, d);
		return;
	}
	case MMOP_NEG:
	case MMOP_NOT: {
		mov_to_rax(f, s0, 0);
		fprintf(f, "\t%sq\t%%rax\n", in->op == MMOP_NEG ? "neg" : "not");
		rax_to_dst(f, d);
		return;
	}
	case MMOP_DIV: case MMOP_UDIV:
	case MMOP_REM: case MMOP_UREM: {
		bool is32 = in->dtype == MT_I32;
		bool isf = in->op == MMOP_UDIV || in->op == MMOP_UREM;
		/* dividend into rax: sign/zero extend to 64 bits */
		if (is32) {
			mov_to_rax(f, s0, 0);
			if (isf)
				fputs("\tmovl\t%eax, %eax\n", f);   /* zero-extend */
			else
				fputs("\tmovslq\t%eax, %rax\n", f);  /* sign-extend */
		} else {
			mov_to_rax(f, s0, 0);
		}
		if (isf)
			fputs("\txorl\t%edx, %edx\n", f);
		else
			fputs("\tcqo\n", f);
		fputs("\tmovq\t", f);
		emit_mval(f, s1);
		fputs(", %rcx\n", f);
		fprintf(f, "\t%s%c\t%%rcx\n", isf ? "div" : "idiv", 'q');
		if (in->op == MMOP_REM || in->op == MMOP_UREM)
			fputs("\tmovq\t%rdx, %rax\n", f);
		rax_to_dst(f, d);
		return;
	}
	case MMOP_CMP:
	case MMOP_TEST: {
		if (in->dtype == MT_F32 || in->dtype == MT_F64) {
			/* floating-point compare: ucomiss/ucomisd sets the same
			 * flags the SETcc mapping expects (CF/ZF) */
			const char *sd = in->dtype == MT_F64 ? "sd" : "ss";
			fprintf(f, "\tmov%s\t", sd);
			emit_mval(f, s0);
			fprintf(f, ", %%xmm0\n");
			fprintf(f, "\tucomi%s\t", sd);
			emit_mval(f, s1);
			fputs(", %xmm0\n", f);
			return;
		}
		bool is32 = in->dtype != MT_I64 && in->dtype != MT_PTR;
		const char *ws = is32 ? "l" : "q";
		const char *rn = is32 ? "%eax" : "%rax";
		const char *rn9 = is32 ? "%r9d" : "%r9";
		const char *cc = in->op == MMOP_CMP ? "cmp" : "test";
		mov_to_rax(f, s0, 0);
		if (c) {
			if (imm32(c->u.i)) {
				fprintf(f, "\t%s%s\t", cc, ws);
				emit_const(f, c);
				fprintf(f, ", %s\n", rn);
			} else {
				fprintf(f, "\tmovq\t$%lld, %%r9\n", (long long)c->u.i);
				fprintf(f, "\t%s%s\t%s, %s\n", cc, ws, rn9, rn);
			}
		} else {
			if (s1 && s1->kind == MV_CONST && s1->con &&
			    !imm32(s1->con->u.i)) {
				fprintf(f, "\tmovq\t$%lld, %%r9\n",
				        (long long)s1->con->u.i);
				fprintf(f, "\t%s%s\t%s, %s\n", cc, ws, rn9, rn);
			} else {
				fprintf(f, "\t%s%s\t", cc, ws);
				if (is32)
					emit_mval32(f, s1);
				else
					emit_mval(f, s1);
				fprintf(f, ", %s\n", rn);
			}
		}
		return;
	}
	case MMOP_SETCC:
		fprintf(f, "\tset%s\t%%al\n", cc_suffix(in->cc));
		fputs("\tmovzbl\t%al, %eax\n", f);
		rax_to_dst(f, d);
		return;
	case MMOP_CMOV: {
		/* dst = flags.cc ? src : dst (if-conversion).  cmov needs a
		 * register destination (gas rejects cmov to memory) and cannot
		 * take an immediate source.  A slot-resident destination is
		 * routed through %r9 (a reserved emitter scratch): pre-load the
		 * old value, conditionally overwrite, store back.  Always use
		 * the 64-bit form with an explicit size suffix (cmov<cc>q —
		 * a bare `cmovl` would be parsed as a 32-bit size suffix); the
		 * low 32 bits are what a 32-bit consumer reads. */
		const char *cc = cc_suffix(in->cc);
		bool memdst = d && d->kind == MV_TEMP && d->reg < 0;
		/* constants and GLOBAL symbols cannot be cmov operands directly:
		 * constants need a register (no immediates), and a symbol is an
		 * ADDRESS — `emit_mval` would print sym(%rip), i.e. read the
		 * symbol's contents, not its address.  Materialize both via %rax
		 * (mov does not touch the flags). */
		bool mats = s0 && (s0->kind == MV_CONST || s0->kind == MV_GLOBAL);
		if (memdst) {
			/* gas rejects cmov to memory: dst = cc ? src : dst via %r9.
			 * Use width-matched loads/stores — an 8-byte movq to a 4-byte
			 * slot would clobber the neighbouring callee-saved save area
			 * (the 32-bit slot -12(%rbp) sits right above the pushed %rbx
			 * at -8(%rbp)).  The cmov itself stays 64-bit; only the low
			 * 32 bits are consumed by a 32-bit consumer. */
			bool is64 = in->dtype == MT_I64 || in->dtype == MT_PTR;
			const char *ws = is64 ? "q" : "l";
			const char *rn = is64 ? "%r9" : "%r9d";
			fprintf(f, "\tmov%s\t", ws);
			emit_mval(f, d);
			fprintf(f, ", %s\n", rn);
			/* the cmov always writes the full %r9; the width-matched
			 * store below consumes only the low bits for 32-bit slots */
			if (mats) {
				mov_to_rax(f, s0, 0);
				fprintf(f, "\tcmov%sq\t%%rax, %%r9\n", cc);
			} else {
				fprintf(f, "\tcmov%sq\t", cc);
				emit_mval(f, s0);
				fputs(", %r9\n", f);
			}
			fprintf(f, "\tmov%s\t%s, ", ws, rn);
			emit_mval(f, d);
			fputs("\n", f);
			return;
		}
		if (mats) {
			mov_to_rax(f, s0, 0);
			fprintf(f, "\tcmov%sq\t%%rax, ", cc);
			emit_mval(f, d);
		} else {
			fprintf(f, "\tcmov%sq\t", cc);
			emit_mval(f, s0);
			fputs(", ", f);
			emit_mval(f, d);
		}
		fputs("\n", f);
		return;
	}
	case MMOP_LOAD: {
		/* width + zero-extension for sub-32 loads */
		emit_addr_loads(f, in->addr);
		/* direct SSE load: an 8-byte movsd into the XMM register.  Do NOT
		 * route through %rax — selret's two-register aggregate return
		 * would let the second chunk's load clobber the first chunk just
		 * placed in %rax (mixed {INTEGER,SSE} 16B returns). */
		if (d && d->kind == MV_REG && in->dtype == MT_F64) {
			fputs("\tmovsd\t", f);
			emit_addr(f, in->addr);
			fprintf(f, ", %%%s\n", d->name);
			return;
		}
		/* 8-byte register destination: load straight there so a second
		 * load (e.g. selret's RAX+RDX chunks) does not clobber %rax */
		if (d && d->kind == MV_REG &&
		    (in->dtype == MT_I64 || in->dtype == MT_PTR)) {
			fputs("\tmovq\t", f);
			emit_addr(f, in->addr);
			fprintf(f, ", %%%s\n", d->name);
			return;
		}
		if (in->dtype == MT_I8)
			fprintf(f, "\tmovzbl\t");
		else if (in->dtype == MT_I16)
			fprintf(f, "\tmovzwl\t");
		else if (in->dtype == MT_I32)
			fprintf(f, "\tmovl\t");
		else
			fprintf(f, "\tmovq\t");
		emit_addr(f, in->addr);
		if (in->dtype == MT_I32 || in->dtype == MT_I8 || in->dtype == MT_I16)
			fputs(", %eax\n", f);
		else
			fputs(", %rax\n", f);
		rax_to_dst(f, d);
		return;
	}
	case MMOP_STORE: {
		/* Is this a store into a TLS global under general-dynamic TLS?
		 * The GD access (emit_tls_addr) issues `call __tls_get_addr`,
		 * which clobbers every caller-saved register (GPR and XMM), so
		 * the value operand must be preserved across the call (D3). */
		bool gd_tls = g_tls_model == MTLS_GLOBAL_DYNAMIC &&
			((in->addr.base && in->addr.base->kind == MV_GLOBAL &&
			  in->addr.base->tls) ||
			 (in->addr.index && in->addr.index->kind == MV_GLOBAL &&
			  in->addr.index->tls));
		if (in->dtype == MT_F32 || in->dtype == MT_F64) {
			/* floating-point store: value through %xmm0.  For GD the
			 * call clobbers caller-saved XMM (incl. the source reg the
			 * allocator chose), so stash the 64-bit float pattern in
			 * %rbx (callee-saved, reserved under GD) across the call. */
			const char *sd = in->dtype == MT_F64 ? "sd" : "ss";
			if (gd_tls) {
				if (c && c->kind == MC_FLT) {
					fprintf(f, "\tmov%s\t", sd);
					emit_const(f, c);
					fprintf(f, ", %%xmm0\n");
				} else if (s0) {
					fprintf(f, "\tmov%s\t", sd);
					emit_mval(f, s0);
					fprintf(f, ", %%xmm0\n");
				}
				fputs("\tmovq\t%xmm0, %rbx\n", f);
				emit_addr_loads(f, in->addr);
				fputs("\tmovq\t%rbx, %xmm0\n", f);
				fprintf(f, "\tmov%s\t%%xmm0, ", sd);
				emit_addr(f, in->addr);
				fputs("\n", f);
				return;
			}
			emit_addr_loads(f, in->addr);
			if (c && c->kind == MC_FLT) {
				fprintf(f, "\tmov%s\t", sd);
				emit_const(f, c);
				fprintf(f, ", %%xmm0\n");
			} else if (s0) {
				fprintf(f, "\tmov%s\t", sd);
				emit_mval(f, s0);
				fprintf(f, ", %%xmm0\n");
			}
			fprintf(f, "\tmov%s\t%%xmm0, ", sd);
			emit_addr(f, in->addr);
			fputs("\n", f);
			return;
		}
		/* Integer store.  Under general-dynamic TLS the address operand
		 * may be a TLS global whose access (emit_addr_loads →
		 * emit_tls_addr) issues `call __tls_get_addr`, which clobbers
		 * every caller-saved register including the %rax value operand
		 * (D3).  To preserve the value across the call we keep it in
		 * %rbx — a callee-saved register that regalloc leaves free under
		 * -ftls-model=global-dynamic (see mreg_pool_build) — store it to
		 * the TLS address, then move it back.  Non-GD stores have no
		 * call (LE/IE are pure address computation), so they keep the
		 * simple emit_addr_loads + mov_to_rax order. */
		if (gd_tls) {
			mov_to_rax(f, s0, c);
			fputs("\tmovq\t%rax, %rbx\n", f);
			emit_addr_loads(f, in->addr);
			if (in->dtype == MT_I8)
				fputs("\tmovb\t%bl, ", f);
			else if (in->dtype == MT_I16)
				fputs("\tmovw\t%bx, ", f);
			else if (in->dtype == MT_I32)
				fputs("\tmovl\t%ebx, ", f);
			else
				fputs("\tmovq\t%rbx, ", f);
			emit_addr(f, in->addr);
			fputs("\n", f);
			return;
		}
		emit_addr_loads(f, in->addr);
		mov_to_rax(f, s0, c);
		if (in->dtype == MT_I8)
			fputs("\tmovb\t%al, ", f);
		else if (in->dtype == MT_I16)
			fputs("\tmovw\t%ax, ", f);
		else if (in->dtype == MT_I32)
			fputs("\tmovl\t%eax, ", f);
		else
			fputs("\tmovq\t%rax, ", f);
		emit_addr(f, in->addr);
		fputs("\n", f);
		return;
	}
	case MMOP_CALL: {
		if (s0 && s0->kind == MV_GLOBAL)
			fprintf(f, "\tcall\t%s%s\n", s0->sym,
			        g_pic && s0->isext ? "@plt" : "");
		else {
			mov_to_rax(f, s0, 0);
			fputs("\tcall\t*%rax\n", f);
		}
		return;
	}
	case MMOP_SALLOC: {
		/* stack-argument reservation: the ABI lowering emits +size before
		 * the call and -size after (paired caller cleanup) */
		int64_t n = c ? c->u.i : 16;
		if (n >= 0)
			fprintf(f, "\tsubq\t$%lld, %%rsp\n", (long long)n);
		else
			fprintf(f, "\taddq\t$%lld, %%rsp\n", (long long)-n);
		return;
	}
	case MMOP_ALLOCA4:
	case MMOP_ALLOCA8:
	case MMOP_ALLOCA16: {
		/* dynamic alloca (VLA): the size is a runtime value; reserve the
		 * space below the current rsp (16-aligned) and return the new
		 * rsp as the block pointer.  The epilogue restores rsp from rbp
		 * for such functions (fm->dynalloc). */
		if (in->src[0] && in->src[0]->kind != MV_CONST) {
			mov_to_rax(f, in->src[0], 0);      /* size */
			fputs("\taddq\t$15, %rax\n", f);   /* round up */
			fputs("\tandq\t$-16, %rax\n", f);
			fputs("\tsubq\t%rax, %rsp\n", f);
			fputs("\tleaq\t0(%rsp), %rax\n", f);
			rax_to_dst(f, d);
			if (g_fm)
				g_fm->dynalloc = true;
			return;
		}
		/* static alloca: address a reserved frame slot below the spill
		 * area (never touch %rsp: it must stay balanced at calls) */
		g_alloca_cur -= alloca_size_ins(in);
		if (g_omit_fp)
			fprintf(f, "\tleaq\t%d(%%rsp), %%rax\n", g_alloca_cur + g_fp_off);
		else
			fprintf(f, "\tleaq\t%d(%%rbp), %%rax\n", g_alloca_cur);
		rax_to_dst(f, d);
		/* DWARF: record the variable's final location (frame-relative
		 * offset matching the frame base emitted below). */
		if (g_dwarf_level > 0 && d && d->kind == MV_TEMP) {
			dwarf_set_framebase(g_omit_fp ? 1 : 0);
			dwarf_loc_set_stack(d->id,
			    g_alloca_cur + (g_omit_fp ? g_fp_off : 0));
		}
		return;
	}
	case MMOP_BLIT: {
		/* aggregate copy: src[1] -> src[0], cst bytes.  Expand as
		 * 8/4/2/1-byte moves through %rax with base/index in r10/r11
		 * (scratch; never clobbers allocated registers). */
		int64_t sz = c ? c->u.i : 0;
		MVal *dp = s0, *sp = s1;
		if (dp && dp->kind == MV_REG)
			fprintf(f, "\tmovq\t%%%s, %%r10\n", dp->name);
		else {
			fputs("\tmovq\t", f);
			emit_mval(f, dp);
			fputs(", %r10\n", f);
		}
		if (sp && sp->kind == MV_REG)
			fprintf(f, "\tmovq\t%%%s, %%r11\n", sp->name);
		else {
			fputs("\tmovq\t", f);
			emit_mval(f, sp);
			fputs(", %r11\n", f);
		}
		int64_t off = 0;
		while (sz >= 8) {
			fprintf(f, "\tmovq\t%lld(%%r11), %%rax\n", (long long)off);
			fprintf(f, "\tmovq\t%%rax, %lld(%%r10)\n", (long long)off);
			off += 8;
			sz -= 8;
		}
		if (sz >= 4) {
			fprintf(f, "\tmovl\t%lld(%%r11), %%eax\n", (long long)off);
			fprintf(f, "\tmovl\t%%eax, %lld(%%r10)\n", (long long)off);
			off += 4;
			sz -= 4;
		}
		if (sz >= 2) {
			fprintf(f, "\tmovw\t%lld(%%r11), %%ax\n", (long long)off);
			fprintf(f, "\tmovw\t%%ax, %lld(%%r10)\n", (long long)off);
			off += 2;
			sz -= 2;
		}
		if (sz >= 1) {
			fprintf(f, "\tmovb\t%lld(%%r11), %%al\n", (long long)off);
			fprintf(f, "\tmovb\t%%al, %lld(%%r10)\n", (long long)off);
		}
		return;
	}
	case MMOP_FADD: case MMOP_FSUB: case MMOP_FMUL: case MMOP_FDIV: {
		const char *op;
		switch (in->op) {
		case MMOP_FADD: op = "add"; break;
		case MMOP_FSUB: op = "sub"; break;
		case MMOP_FMUL: op = "mul"; break;
		default:        op = "div"; break;
		}
		fprintf(f, "\tmov%s\t", w == 'd' ? "sd" : "ss");
		emit_mval(f, s0);
		fprintf(f, ", %%xmm0\n");
		fprintf(f, "\t%s%s\t", op, w == 'd' ? "sd" : "ss");
		emit_mval(f, s1);
		fputs(", %xmm0\n", f);
		if (d) {
			fprintf(f, "\tmov%s\t%%xmm0, ", w == 'd' ? "sd" : "ss");
			emit_mval(f, d);
			fputs("\n", f);
		}
		return;
	}
	case MMOP_FNEG: {
		/* negate: 0.0 - x */
		fputs("\tpxor\t%xmm0, %xmm0\n", f);
		fprintf(f, "\tsub%s\t", w == 'd' ? "sd" : "ss");
		emit_mval(f, s0);
		fputs(", %xmm0\n", f);
		if (d) {
			fprintf(f, "\tmov%s\t%%xmm0, ", w == 'd' ? "sd" : "ss");
			emit_mval(f, d);
			fputs("\n", f);
		}
		return;
	}
	case MMOP_FSQRT: {
		fprintf(f, "\tmov%s\t", w == 'd' ? "sd" : "ss");
		emit_mval(f, s0);
		fprintf(f, ", %%xmm0\n");
		fprintf(f, "\tsqrt%s\t%%xmm0, %%xmm0\n", w == 'd' ? "sd" : "ss");
		if (d) {
			fprintf(f, "\tmov%s\t%%xmm0, ", w == 'd' ? "sd" : "ss");
			emit_mval(f, d);
			fputs("\n", f);
		}
		return;
	}
	case MMOP_CVTSI2SS: case MMOP_CVTSI2SD:
	case MMOP_CVTSI2SS_U: case MMOP_CVTSI2SD_U:
	case MMOP_CVTTSS2SI: case MMOP_CVTTSD2SI:
	case MMOP_CVTSS2SD: case MMOP_CVTSD2SS: {
		/* int<->fp conversions: sources/targets live in stack slots;
		 * int regs are %rax, fp regs are %xmm0 */
		switch (in->op) {
		case MMOP_CVTSI2SS:
			mov_to_rax(f, s0, 0);
			fputs("\tcvtsi2ssl\t%eax, %xmm0\n", f);
			fputs("\tmovss\t%xmm0, ", f);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		case MMOP_CVTSI2SD:
			mov_to_rax(f, s0, 0);
			fputs("\tcvtsi2sdq\t%rax, %xmm0\n", f);
			fputs("\tmovsd\t%xmm0, ", f);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		case MMOP_CVTSI2SS_U:
		case MMOP_CVTSI2SD_U: {
			/* unsigned int -> fp.  cvtsi2ss/sd interprets the 64-bit
			 * register as signed, so values with the high bit set need
			 * the identity (double)u == (double)(u>>1)*2 + (double)(u&1)
			 * (mov_to_rax zero-extends 32-bit values into %rax). */
			bool isd = (in->op == MMOP_CVTSI2SD_U);
			const char *conv = isd ? "cvtsi2sdq" : "cvtsi2ssq";
			const char *add  = isd ? "addsd" : "addss";
			const char *movr = isd ? "movsd" : "movss";
			mov_to_rax(f, s0, 0);
			fputs("\tshrq\t$1, %rax\n", f);              /* u >> 1 */
			fprintf(f, "\t%s\t%%rax, %%xmm0\n", conv);
			fprintf(f, "\t%s\t%%xmm0, %%xmm0\n", add);   /* x2 */
			mov_to_rax(f, s0, 0);
			fputs("\tandq\t$1, %rax\n", f);              /* u & 1 */
			fprintf(f, "\t%s\t%%rax, %%xmm1\n", conv);
			fprintf(f, "\t%s\t%%xmm1, %%xmm0\n", add);
			fprintf(f, "\t%s\t%%xmm0, ", movr);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		}
		case MMOP_CVTTSS2SI:
			fputs("\tcvttss2sil\t", f);
			emit_mval(f, s0);
			fputs(", %eax\n", f);
			rax_to_dst(f, d);
			break;
		case MMOP_CVTTSD2SI:
			fputs("\tcvttsd2siq\t", f);
			emit_mval(f, s0);
			fputs(", %rax\n", f);
			rax_to_dst(f, d);
			break;
		case MMOP_CVTSS2SD:
			fputs("\tcvtss2sd\t", f);
			emit_mval(f, s0);
			fputs(", %xmm0\n", f);
			fputs("\tmovsd\t%xmm0, ", f);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		default: /* CVTSD2SS */
			fputs("\tcvtsd2ss\t", f);
			emit_mval(f, s0);
			fputs(", %xmm0\n", f);
			fputs("\tmovss\t%xmm0, ", f);
			emit_mval(f, d);
			fputs("\n", f);
			break;
		}
		return;
	}
	case MMOP_PUSH:
		mov_to_rax(f, s0, c);
		fputs("\tpushq\t%rax\n", f);
		return;
	case MMOP_POP:
		fputs("\tpopq\t%rax\n", f);
		rax_to_dst(f, d);
		return;
	default:
		fprintf(f, "\t# mmop %d (unhandled)\n", (int)in->op);
		return;
	}
}

/* ---- function emit -------------------------------------------------------- */

void
mfnm_emit_x86_64(MFnM *fm, FILE *f)
{
	g_mt = fm->mt;
	g_fm = fm;
	/* Blocks are emitted in reversed link order, so the epilogue of a
	 * later-emitted (earlier in CFG) return block may be printed before
	 * the dynamic-alloca instruction would set the flag.  Pre-scan so
	 * every return path restores rsp correctly. */
	fm->dynalloc = false;
	for (MBlkM *b = fm->link; !fm->dynalloc && b; b = b->link)
		for (uint32_t i = 0; i < b->nins; i++) {
			MMOP op = b->ins[i].op;
			if ((op == MMOP_ALLOCA4 || op == MMOP_ALLOCA8 ||
			     op == MMOP_ALLOCA16) &&
			    b->ins[i].src[0] && b->ins[i].src[0]->kind != MV_CONST) {
				fm->dynalloc = true;
				break;
			}
		}
	/* General-dynamic TLS store: the emitter keeps the value operand in
	 * %rbx across the __tls_get_addr call (D3).  %rbx is callee-saved
	 * and regalloc leaves it free under MTLS_GLOBAL_DYNAMIC, but the
	 * emitter's use of it must still be saved/restored by the prologue/
	 * epilogue, so mark it used here (before the callee-saved save loop
	 * that keys off fm->regsused). */
	if (g_tls_model == MTLS_GLOBAL_DYNAMIC)
		for (MBlkM *b = fm->link; b; b = b->link)
			for (uint32_t i = 0; i < b->nins; i++) {
				MInsM *in = &b->ins[i];
				if (in->op == MMOP_STORE &&
				    ((in->addr.base && in->addr.base->kind == MV_GLOBAL &&
				      in->addr.base->tls) ||
				     (in->addr.index && in->addr.index->kind == MV_GLOBAL &&
				      in->addr.index->tls))) {
					fm->regsused |= (1ull << X64MREG_RBX);
					goto gd_scan_done;
				}
			}
gd_scan_done:
	int extra = assign_extra_slots(fm);
	nfp = 0;   /* fresh float pool per function */
	g_fname = fm->name;
	/* allocas go below spill slots and (for varargs) the reg_save_area */
	g_alloca_cur = -(fm->slot + extra +
	                 ((fm->host && fm->host->vararg) ? 176 : 0));

	/* Leaf functions at -O2+ may omit the frame pointer (rsp base), as the
	 * legacy LIR backend does.  Keep rbp when debugging (-Og / g_force_fp),
	 * when the function calls others, when rsp moves at runtime (dynamic
	 * VLA alloca), for varargs, or when the ABI lowering addressed the
	 * caller-pushed stack arguments / sret pad via %rbp (mabi_selpar). */
	{
		extern int g_force_fp;
		bool hascall = false, hasrbp = false;
		for (MBlkM *b = fm->link; b && !(hascall && hasrbp); b = b->link)
			for (uint32_t i = 0; i < b->nins; i++) {
				MInsM *in = &b->ins[i];
				if (in->op == MMOP_CALL)
					hascall = true;
				if (in->addr.base && in->addr.base->kind == MV_REG &&
				    in->addr.base->reg == X64MREG_RBP)
					hasrbp = true;
			}
		g_omit_fp = fm->host->optlevel >= 2 && !g_force_fp &&
		            !hascall && !hasrbp && !fm->dynalloc &&
		            !(fm->host && fm->host->vararg);
	}
	g_fp_off = 0;

	/* frame covers spill slots + allocas, plus (for varargs) the 176-byte
	 * reg_save_area that the allocas sit below.  Align so that every call
	 * sees rsp % 16 == 0: after push rbp + the callee-saved pushes the
	 * stack offset depends on the entry alignment (callers leave rsp % 16
	 * == 8, but libc enters main with rsp % 16 == 0). */
	int framesize = fm->slot + extra + alloca_total(fm) +
	                ((fm->host && fm->host->vararg) ? 176 : 0);
	framesize = (framesize + 15) & ~15;
	int csaves = 0;
	for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0; i++)
		if ((fm->regsused >> fm->mt->rclob[i]) & 1)
			csaves++;
	/* this backend observes every function (including main, as launched
	 * by the host runtime) entering with rsp % 16 == 8 */
	int entryoff = 8;
	int postpush = (entryoff - 8 * (csaves + (g_omit_fp ? 0 : 1))) & 15;
	int align = (16 - postpush) & 15;
	framesize += align;   /* align must survive: subq restores %16 */
	/* rsp-based slot addressing: a rbp-relative slot (negative) sits at
	 * rsp + slot + csaves*8 + framesize - 8 (the elided pushq %rbp). */
	if (g_omit_fp)
		g_fp_off = csaves * 8 + framesize - 8;

	fprintf(f, ".text\n");
	/* Exception-handling flag (function-scoped): true when the function
	 * calls _meuos_exc_* helpers under --specs=meuos.  Emits
	 * .cfi_personality/.cfi_lsda and the .Llsda<name> label the LSDA
	 * reference points at.  Gated on g_meuos_specs: under --specs=host
	 * __gxx_personality_v0 is not linked, so personality/lsda must not
	 * be emitted (check-cpp-func links with the host libc). */
	bool has_eh = g_meuos_specs != 0;
	if (fm->name) {
		if (fm->host && fm->host->export)
			fprintf(f, ".globl %s\n", fm->name);
		/* .cfi_startproc / .cfi_endproc emit DWARF CFI for every function,
		 * enabling .eh_frame generation for stack unwinding.  For functions
		 * that call exception runtime helpers (_meuos_exc_*), also emit
		 * .cfi_personality and .cfi_lsda so the DWARF unwinder can route
		 * exceptions through __gxx_personality_v0.  We only emit these for
		 * functions that explicitly use C++ try/catch to avoid pulling
		 * __gxx_personality_v0 under --specs=host (where meuos-libc is not
		 * linked). */
		for (MBlkM *b = fm->link; !has_eh && b; b = b->link) {
			for (uint32_t i = 0; !has_eh && i < b->nins; i++) {
				MInsM *in = &b->ins[i];
				if (in->op == MMOP_CALL && in->src[0] &&
				    in->src[0]->kind == MV_GLOBAL) {
					const char *s = in->src[0]->sym;
					if (s && strstr(s, "_meuos_exc_") && g_meuos_specs)
						has_eh = true;
				}
			}
			if (!has_eh && b->term.op == MMOP_CALL && b->term.src[0] &&
			    b->term.src[0]->kind == MV_GLOBAL) {
				const char *s = b->term.src[0]->sym;
				if (s && strstr(s, "_meuos_exc_") && g_meuos_specs)
					has_eh = true;
			}
		}
		fputs("\t.cfi_startproc\n", f);
if (has_eh) {
			fputs("\t.cfi_personality 0x1b, __gxx_personality_v0\n", f);
			fprintf(f, "\t.cfi_lsda 0x1b, .Llsda%s\n",
			        fm->name ? fm->name : "f");
		}
		fprintf(f, "%s:\n", fm->name);
	}
	if (!g_omit_fp) {
		fputs("\tpushq\t%rbp\n", f);
		fputs("\t.cfi_def_cfa_offset 16\n", f);
		fputs("\t.cfi_offset 6, 16\n", f);
		fputs("\tmovq\t%rsp, %rbp\n", f);
		fputs("\t.cfi_def_cfa_register 6\n", f);
	}
	/* X64MReg -> DWARF x86_64 register number mapping (mt/as uses DWARF
	 * reg numbers, not register names).  See DWARF x86_64 ABI table. */
	static const int dwarf_rno[] = {
		[X64MREG_RAX] = 0,  [X64MREG_RDX] = 1,  [X64MREG_RCX] = 2,
		[X64MREG_RBX] = 3,  [X64MREG_RSI] = 4,  [X64MREG_RDI] = 5,
		[X64MREG_RBP] = 6,  [X64MREG_RSP] = 7,
		[X64MREG_R8]  = 8,  [X64MREG_R9]  = 9,  [X64MREG_R10] = 10,
		[X64MREG_R11] = 11, [X64MREG_R12] = 12, [X64MREG_R13] = 13,
		[X64MREG_R14] = 14, [X64MREG_R15] = 15,
	};
	/* save callee-saved registers used by the allocator */
	int callee_push_count = 0;
	for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0; i++) {
		int r = fm->mt->rclob[i];
		if ((fm->regsused >> r) & 1) {
			fprintf(f, "\tpushq\t%%%s\n", mreg_name(fm->mt, r));
			/* CFA offset (distance below CFA, always positive):
			 * with fp: rbp at 16, callee-saved at 24, 32, ...
			 * without fp: callee-saved at 16, 24, ... */
			int cfa_off = g_omit_fp
			    ? 8 * (callee_push_count + 2)
			    : 8 * (callee_push_count + 3);
			int dno = (r >= 0 && r < (int)(sizeof dwarf_rno / sizeof dwarf_rno[0]))
			    ? dwarf_rno[r] : -1;
			if (dno >= 0)
				fprintf(f, "\t.cfi_offset %d, %d\n",
				        dno, cfa_off);
			callee_push_count++;
		}
	}
	if (framesize > 0) {
		fprintf(f, "\tsubq\t$%d, %%rsp\n", framesize);
		/* .cfi_adjust_cfa_offset requires a preceding .cfi_def_cfa*.
		 * With -fomit-frame-pointer the push of %rbp is skipped, so
		 * no .cfi_def_cfa_offset was emitted — anchor the CFA first. */
		if (g_omit_fp)
			fputs("\t.cfi_def_cfa_offset 8\n", f);
		fprintf(f, "\t.cfi_adjust_cfa_offset %d\n", framesize);
	}
	/* blocks are emitted in link order (reversed); jump to the real entry */
	if (fm->start)
		fprintf(f, "\tjmp\t.L%s.bb%u\n", fm->name ? fm->name : "f",
		        fm->start->id);

	for (MBlkM *b = fm->link; b; b = b->link) {
		fprintf(f, ".L%s.bb%u:\n", fm->name ? fm->name : "f", b->id);
		for (uint32_t i = 0; i < b->nins; i++)
			emit_ins(f, &b->ins[i]);
		switch (b->term.op) {
		case MMOP_JMP:
			fprintf(f, "\tjmp\t.L%s.bb%u\n", fm->name ? fm->name : "f",
			        b->s1 ? b->s1->id : 0);
			break;
		case MMOP_JCC:
			/* explicit taken + fallthrough jump: block order in the
			 * emitted file is not guaranteed to match the CFG */
			fprintf(f, "\tj%s\t.L%s.bb%u\n", cc_suffix(b->term.cc),
			        fm->name ? fm->name : "f",
			        b->s1 ? b->s1->id : 0);
			fprintf(f, "\tjmp\t.L%s.bb%u\n", fm->name ? fm->name : "f",
			        b->s2 ? b->s2->id : 0);
			break;
		case MMOP_RET:
			if (g_fm && g_fm->dynalloc) {
				/* rsp was lowered by a runtime VLA alloca: restore it
				 * from the frame pointer, then rewind past the
				 * callee-saved pushes so the pops below work. */
				fprintf(f, "\tmovq\t%%rbp, %%rsp\n");
				fprintf(f, "\tsubq\t$%d, %%rsp\n", csaves * 8);
			} else if (framesize > 0) {
				fprintf(f, "\taddq\t$%d, %%rsp\n", framesize);
			}
			/* restore callee-saved registers in reverse push order */
			{
				int used[16], n = 0;
				for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0 &&
				     n < 16; i++)
					if ((fm->regsused >> fm->mt->rclob[i]) & 1)
						used[n++] = fm->mt->rclob[i];
				while (n > 0)
					fprintf(f, "\tpopq\t%%%s\n",
					        mreg_name(fm->mt, used[--n]));
			}
			if (g_omit_fp)
				fputs("\tret\n", f);
			else
				fputs("\tpopq\t%rbp\n\tret\n", f);
			break;
		default:
			break;
		}
	}

	fputs("\t.cfi_endproc\n", f);
	/* The .cfi_lsda above references .Llsda<name>; define the label so
	 * the personality routine has an LSDA anchor.  A zero-length LSDA
	 * (header only, no call-site table) tells the unwinder there are
	 * no landing pads for this function — it continues unwinding. */
	if (has_eh && fm->name) {
		fprintf(f, ".Llsda%s:\n", fm->name);
		fputs("\t.byte 0xff\n", f);  /* LPStart encoding: omitted */
		fputs("\t.byte 0xff\n", f);  /* TType encoding: omitted */
		fputs("\t.uleb128 0\n", f);  /* Call-site table length: 0 */
	}
fp_pool_emit(f);
}
