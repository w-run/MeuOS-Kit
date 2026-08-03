/* emit.c — per-function lowering + global data emission.
 *
 * `emitfunc` lowers the frontend's `struct func` AST to MIR (func_to_mir,
 * src/c/irgen/func_to_mir.c), runs the MIR pass pipeline, then either
 * emits through the target's MIR-native machine backend (x86_64/riscv64/
 * loongarch64/aarch64) or falls back to the MIR → LIR bridge + legacy LIR
 * pipeline for targets without a machine backend yet.
 *
 * The legacy direct-LIR Fn construction (previously the body of this
 * function) was removed in Phase 4 step 1 — it was unreachable since
 * Phase 2 forced g_use_mir=1 (MCC_USE_MIR=0 no longer exists).
 *
 * `emitdata` translates a global/static object initializer into a stream
 * of IR `Dat` records emitted via `emitdat()` (DStart/DB/DH/DW/DL/DZ/DEnd),
 * covering string literals, scalar constants, and address-of-identifier
 * references. Bit-fields and wide strings are deferred to later phases. */
#include <ctype.h>
#include "irgen.h"
#include "mir.h"

extern int emit_debug;  /* from emit/emit.c */
extern void emitdbgloc(uint, uint, FILE *);  /* from emit/emit.c */
extern bool mfnm_backend_x86_64(struct MFn *);   /* x86_64_mbe.c (P3b machine backend) */
extern bool mfnm_backend_riscv64(struct MFn *);   /* riscv64_mbe.c (P3a machine backend) */
extern bool mfnm_backend_loongarch64(struct MFn *); /* loongarch64_mbe.c (P3b) */
extern bool mfnm_backend_aarch64(struct MFn *);   /* aarch64_mbe.c (P3b machine backend) */
extern bool mfnm_backend_arm(struct MFn *);       /* arm_mbe.c (P3c machine backend) */

/* DWARF variable-type classification for the base-type DIEs. */
static int
dwarf_type_of(struct type *t)
{
	if (!t)
		return 0; /* DT_INT */
	if (t->kind == TYPEPOINTER)
		return 3; /* DT_PTR */
	if (t->kind == TYPECHAR || (t->prop & PROPCHAR))
		return 1; /* DT_CHAR */
	if (t->prop & PROPINT) {
		if (t->u.arith.issigned)
			return 0; /* DT_INT */
		return 2; /* DT_UINT */
	}
	return 0;
}

/* Record the function's local variables / parameters for DWARF variable
 * DIEs.  Each variable recorded by funcalloc() gets a DIE (name, type,
 * declaration line) plus a DW_AT_location: the x86_64 machine emitter
 * recorded the final frame offset (static alloca) keyed by the MVal id,
 * reached here through the func_to_mir value->MVal side table. */
static void
dwarf_collect_vars(struct func *f, struct MFn *mf, Fn *fn)
{
	int i;

	(void)fn;
	if (!f || g_dwarf_level <= 0)
		return;
	for (i = 0; i < f->ndvars; i++) {
		struct dwarf_vrec *vr = &f->dvars[i];
		int has_loc = 0, loc_off = 0, loc_reg = -1;
		if (mf && mf->vmap && vr->value_id > 0 &&
		    (uint32_t)vr->value_id < mf->nvmap) {
			MVal *mv = mf->vmap[vr->value_id];
			if (mv)
				has_loc = dwarf_loc_get(mv->id, &loc_off,
				    &loc_reg);
		}
		dwarf_add_var(vr->name, dwarf_type_of(vr->type),
		    has_loc, loc_off, loc_reg);
	}
}

/* MIR-only switch (Phase 2): always 1 — set in main.c (MCC_USE_MIR env
 * removed).  The legacy direct-LIR path was removed in Phase 4 step 1. */
int g_use_mir;

/* P2+ MIR-native backend switch (Phase 2): x86_64 defaults to the machine
 * backend (convert to MFnM, ABI-lower, regalloc, emit) as its sole asm
 * producer.  MCC_MIR_BACKEND=0 may disable it for testing; non-x86_64
 * targets keep the bridge path. */
int g_use_mir_backend;

void
emitfunc(struct func *f, struct scope *fs, bool global)
{
	Fn *fn;
	int dwarf_idx = -1;

	/* DWARF debug info: record the function (name, declaration line). */
	if (g_dwarf_level > 0) {
		const char *fname = f->name;
		int fline = f->declloc.file ? f->declloc.line + 1 : 1;
		dwarf_loc_reset();  /* fresh per-function location table */
		dwarf_idx = dwarf_begin_func(fname, fline, 1);
	}

	/* C99 5.1.2.2.3: implicit `return 0;` from main */
	if (f->end->jump.kind == JUMP_NONE) {
		struct value *v = NULL;
		if (strcmp(f->name, "main") == 0 && f->type->base == &typeint)
			v = mkintconst(0);
		funcret(f, v);
	}

	/* MIR-only path (Phase 2): the frontend tree is always lowered to MIR
	 * (func_to_mir never fails — unmapped ops die), MIR passes run, then
	 * the x86_64 target uses the MIR-native machine backend (sole asm
	 * producer; all fallbacks closed in Phase 1) and every other target
	 * bridges to the LIR pipeline.  The legacy direct-LIR Fn construction
	 * was deleted in Phase 4 step 1 (emit.c, previously unreachable). */
	if (g_use_mir) {
		MFn *mf = func_to_mir(f, opt_level, global);
		if (debug['X']) {
			fprintf(stderr, "\n> MIR (pre-pass) %s:\n", f->name);
			mfn_dump(mf, stderr);
		}
		run_mir_passes(mf, opt_level);
		if (debug['X']) {
			fprintf(stderr, "\n> MIR (post-pass) %s:\n", f->name);
			mfn_dump(mf, stderr);
		}
		/* MIR-native backend (Phase 2/3a): x86_64 and riscv64 — the
		 * machine backend dispatches per-target (mfnm_backend_<arch>);
		 * when it reports success the MIR was emitted directly.  Other
		 * targets keep the MIR -> bridge -> LIR path.  The per-arch
		 * backend falls back (returns false) for constructs it does not
		 * cover, so the bridge path stays as the safety net. */
		if (g_use_mir_backend &&
		    ((strcmp(T.name, "x86_64") == 0 && mfnm_backend_x86_64(mf)) ||
		     (strcmp(T.name, "riscv64") == 0 && mfnm_backend_riscv64(mf)) ||
		     (strcmp(T.name, "loongarch64") == 0 &&
		      mfnm_backend_loongarch64(mf)) ||
		     (strcmp(T.name, "aarch64") == 0 && mfnm_backend_aarch64(mf)) ||
		     ((strcmp(T.name, "arm") == 0 || strcmp(T.name, "armv7") == 0) &&
		      mfnm_backend_arm(mf)))) {

			if (g_dwarf_level > 0) {
				dwarf_collect_vars(f, mf, NULL);
				dwarf_emit_func_end(stdout, dwarf_idx);
				dwarf_end_func();
			}
			freeall();
			mfn_free(mf);
			return;
		}
		fn = lir_bridge(mf);
		if (debug['X']) {
			fprintf(stderr, "\n> LIR (bridged) %s:\n", f->name);
			printfn(fn, stderr);
		}
		run_passes(fn);
		if (emit_debug)
			emitdbgloc(1, 0, stdout);
		T.emitfn(fn, stdout);
		if (g_dwarf_level > 0) {
			dwarf_collect_vars(f, mf, fn);
			dwarf_emit_func_end(stdout, dwarf_idx);
			dwarf_end_func();
		}
		freeall();
		mfn_free(mf);
		return;
	}
}

/* Return the assembly symbol name for a global value.  Static globals
 * (LINKNONE, positive id) get ".L<name>.<id>", externs use the bare name,
 * and asm-quoted names are wrapped in double quotes (emitdat/emitlnk
 * suppress T.assym for names beginning with '"'). */
static char *
globalname(struct value *v)
{
	const char *name = v->u.name ? v->u.name : "compound";

	if (v->kind & VALUE_QUOTE)
		return strf(PFn, "\"%s\"", name);
	if (v->id)
		return strf(PFn, ".L%s.%u", name, v->id);
	return (char *)name;
}

/* Build a double-quoted, C-escaped representation of data[0..size) for
 * emission as a IR `DB`/`.ascii` Dat. Printable bytes (except '"' and '\\')
 * are copied verbatim; everything else uses '\%03o' (matching the frontend's
 * dataitem escape rule, which GAS interprets in .ascii). */
static char *
escape_string(unsigned char *data, size_t size)
{
	struct array buf = {0};
	char term = '\0';

	arrayaddbuf(&buf, "\"", 1);
	for (size_t i = 0; i < size; ++i) {
		unsigned char c = data[i];
		if (isprint(c) && c != '"' && c != '\\') {
			char ch = c;
			arrayaddbuf(&buf, &ch, 1);
		} else {
			char oct[5];
			snprintf(oct, sizeof oct, "\\%03o", c);
			arrayaddbuf(&buf, oct, strlen(oct));
		}
	}
	arrayaddbuf(&buf, "\"", 1);
	arrayaddbuf(&buf, &term, 1);
	return buf.val;  /* heap-allocated via xreallocary inside arrayaddbuf */
}

/* Emit one integer-sized storage unit used by static data, selecting the
 * direct-IR data record from its byte width. */
static void
emitintegerdata(Dat *dat, unsigned long long size, unsigned long long value)
{
	dat->isstr = 0;
	dat->isref = 0;
	dat->u.num = value;
	switch (size) {
	case 1: dat->type = DB; break;
	case 2: dat->type = DH; break;
	case 4: dat->type = DW; break;
	case 8: dat->type = DL; break;
		default:
		err("unsupported bit-field storage size %llu", size);
	}
}

void
emitdata(struct decl *d, struct init *init)
{
	struct init *cur;
	struct type *t;
	unsigned long long offset = 0, start, end;
	Dat dat;
	Lnk lnk = {0};

	/* Fold all initializer expressions to constants first. */
	for (cur = init; cur; cur = cur->next)
		cur->expr = eval(cur->expr);

	/* Build linkage from decl attributes. */
	lnk.export = (d->linkage == LINKEXTERN);
	lnk.thread = (d->kind == DECLOBJECT && d->u.obj.storage == SDTHREAD);
	lnk.align = d->u.obj.align;

	/* DStart initializes emitdat's zero-accumulator; name/lnk are picked
	 * up from the first actual data record (the local `dat` is reused, so
	 * they persist across the loop). */
	dat.type = DStart;
	dat.name = globalname(d->value);
	dat.lnk = &lnk;
	emitdat(&dat, stdout);

	/* `init` is assumed sorted by start offset (parseinit emits it that
	 * way). Walk it, gap-filling with zeros between non-contiguous ranges. */
	while (init) {
		cur = init;
		init = init->next;

		/* Adjacent initializers for fields in one bit-field storage unit
		 * describe disjoint bit ranges but share the same bytes.  Pack them
		 * before emitting data, otherwise each initializer would overwrite
		 * the preceding one.  `before` is the number of lower-order bits
		 * preceding the field in the storage unit; funcbits() removes the
		 * complementary high-order (`after`) bits while reading it. */
		if (cur->bits.before || cur->bits.after) {
			unsigned long long value = 0;
			unsigned long long unit_start = cur->start;
			unsigned long long unit_end = cur->end;
			unsigned long long unit_bits = (unit_end - unit_start) * 8;

			for (;;) {
				unsigned long long width, mask;
				if (cur->expr->kind != EXPRCONST
				 || !(cur->expr->type->prop & PROPINT))
					error_code(E_DECL, &tok.loc, "bit-field initializer is not an integer constant expression");
				width = unit_bits - cur->bits.before - cur->bits.after;
				mask = width == 64 ? ~0ull : (1ull << width) - 1;
				value |= (cur->expr->u.constant.u & mask) << cur->bits.before;
				if (!init || !(init->bits.before || init->bits.after)
				 || init->start != unit_start || init->end != unit_end)
					break;
				cur = init;
				init = init->next;
			}
			/* The storage unit can begin inside a byte range already
			 * emitted by an earlier non-bit-field member that shares the
			 * unit (e.g. `char a; int b : 5;` — b's 4-byte unit starts at
			 * byte 0 and overlaps a's byte 0).  funcstore() preserves the
			 * overlapping member's bits via masked insert; emitdata must
			 * mirror that by right-shifting the packed value to the
			 * still-unemitted part of the unit instead of re-emitting the
			 * whole unit and clobbering the earlier member. */
			if (offset > unit_start) {
				value >>= (offset - unit_start) * 8;
				unit_start = offset;
			}
			if (offset < unit_start) {
				dat.type = DZ;
				dat.isstr = 0;
				dat.isref = 0;
				dat.u.num = unit_start - offset;
				emitdat(&dat, stdout);
			}
			/* emit the remaining bytes (width need not be 1/2/4/8). */
			{
				unsigned long long len = unit_end - unit_start;
				if (len >= 8) {
					emitintegerdata(&dat, 8, value & ~0ull);
					emitdat(&dat, stdout);
					len -= 8;
					value = 0;   /* the packed unit is at most 64 bits */
				}
				while (len >= 4) {
					emitintegerdata(&dat, 4, value & 0xffffffffull);
					emitdat(&dat, stdout);
					value >>= 32;
					len -= 4;
				}
				while (len >= 2) {
					emitintegerdata(&dat, 2, value & 0xffffull);
					emitdat(&dat, stdout);
					value >>= 16;
					len -= 2;
				}
				if (len == 1) {
					emitintegerdata(&dat, 1, value & 0xffull);
					emitdat(&dat, stdout);
				}
			}
			offset = unit_end;
			continue;
		}

		start = cur->start + cur->bits.before / 8;
		end = cur->end - (cur->bits.after + 7) / 8;

		/* gap fill */
		if (offset < start) {
			dat.type = DZ;
			dat.isstr = 0;
			dat.isref = 0;
			dat.u.num = start - offset;
			emitdat(&dat, stdout);
			offset = start;
		}

		/* For arrays the element type drives the data width. */
		t = cur->expr->type;
		if (t->kind == TYPEARRAY)
			t = t->base;

		dat.isstr = 0;
		dat.isref = 0;
		memset(&dat.u, 0, sizeof dat.u);

		switch (cur->expr->kind) {
		case EXPRSTRING: {
			size_t w = cur->expr->type->base->size;
			size_t slen, actual;
			if (w != 1)
				die("mcc: wide string initializer not yet supported");
			slen = cur->end - cur->start;
			actual = cur->expr->u.string.size < slen
				? cur->expr->u.string.size : slen;
			dat.type = DB;
			dat.isstr = 1;
			dat.u.str = escape_string(
				(unsigned char *)cur->expr->u.string.data, actual);
			emitdat(&dat, stdout);
			free(dat.u.str);
			/* Zero-fill the remainder of the init range when the
			 * literal is shorter than its declared slot. */
			if (actual < slen) {
				dat.isstr = 0;
				dat.type = DZ;
				dat.u.num = slen - actual;
				emitdat(&dat, stdout);
			}
			break;
		}
		case EXPRCONST:
			if (t->prop & PROPFLOAT) {
				if (t->size == 4) {
					dat.type = DW;
					dat.u.flts = (float)cur->expr->u.constant.f;
				} else {
					dat.type = DL;
					dat.u.fltd = cur->expr->u.constant.f;
				}
			} else {
				switch (irtype(t).data) {
				case 'b': dat.type = DB; break;
				case 'h': dat.type = DH; break;
				case 'w': dat.type = DW; break;
				case 'l': dat.type = DL; break;
				default:
					die("mcc: unsupported int data width '%c'",
						irtype(t).data);
				}
				dat.u.num = cur->expr->u.constant.u;
			}
			emitdat(&dat, stdout);
			break;
		case EXPRUNARY:
		case EXPRBINARY: {
			/* Address-of-identifier, optionally + constant offset:
			 *   &x      -> DL ref { name=x, off=0 }
			 *   &x + N  -> DL ref { name=x, off=N }
			 * Lowered as a single IR `DL`/`.quad name+off` Dat on
			 * LP64, or `DW`/`.int name+off` on ILP32 (i386) where
			 * pointers are 4 bytes. `.quad` is unsupported by
			 * `as --32` (BFD_RELOC_64), so the width must track
			 * the target pointer size. */
			struct expr *base;
			struct decl *refdecl;
			long long off = 0;

			if (cur->expr->kind == EXPRBINARY) {
				if (cur->expr->op != TADD
				    || cur->expr->u.binary.l->kind != EXPRUNARY
				    || cur->expr->u.binary.l->op != TBAND
				    || cur->expr->u.binary.r->kind != EXPRCONST)
					error_code(E_DECL, &tok.loc, "initializer is not a constant expression");
				base = cur->expr->u.binary.l;
				off = (long long)cur->expr->u.binary.r->u.constant.i;
			} else {
				if (cur->expr->op != TBAND)
					error_code(E_DECL, &tok.loc, "initializer is not a constant expression");
				base = cur->expr;
			}
			if (base->base->kind != EXPRIDENT)
				error_code(E_DECL, &tok.loc, "initializer is not a constant expression");
			refdecl = base->base->u.ident.decl;
			if (refdecl->kind == DECLOBJECT
			    && refdecl->u.obj.storage != SDSTATIC
			    && refdecl->linkage != LINKEXTERN
			    && refdecl->linkage != LINKINTERN)
				error_code(E_DECL, &tok.loc, "initializer is not a constant expression");
			dat.type = typelong.size == 4 ? DW : DL;
			dat.isref = 1;
			dat.u.ref.name = globalname(refdecl->value);
			dat.u.ref.off = off;
			emitdat(&dat, stdout);
			break;
		}
		default:
			error_code(E_DECL, &tok.loc, "initializer is not a constant expression");
		}
		offset = end;
	}

	/* Zero-fill up to the declared object size. */
	if (offset < d->type->size) {
		dat.isstr = 0;
		dat.isref = 0;
		dat.type = DZ;
		dat.u.num = d->type->size - offset;
		emitdat(&dat, stdout);
	}

	dat.type = DEnd;
	dat.lnk = &lnk;
	emitdat(&dat, stdout);
}
