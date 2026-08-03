/* irgen.h — internal shared header for the AST → IR construction module.
 *
 * Split out of the former monolithic irgen.c. The public API (mkfunc /
 * funcexpr / funcinit / emitfunc / emitdata / ...) is declared in cc.h
 * and consumed by the frontend (decl.c / stmt.c / expr.c). This header
 * only exposes the data structures and helper functions shared between
 * the split translation units inside irgen/. */
#ifndef MCC_IRGEN_H
#define MCC_IRGEN_H

#include <stdbool.h>
#include <stdio.h>
#include "util.h"
#include "mcc.h"
#include "ir.h"

/* --- Value / block / instruction data model -----------------------------
 *
 * These structures mirror the frontend/backend contract: the frontend
 * builds `struct func` trees whose leaves are `struct value *`, and the
 * IR-construction pass at emit time translates each value into a IR Ref
 * (temporary / constant / global address). */

struct value {
	enum {
		VALUE_NONE,
		VALUE_GLOBAL,
		VALUE_INTCONST,
		VALUE_FLTCONST,
		VALUE_DBLCONST,
		VALUE_TEMP,
		VALUE_TYPE,
		VALUE_LABEL,

		VALUE_EXTERN = 1<<4,
		VALUE_THREAD = 1<<5,
		VALUE_QUOTE = 1<<6,
	} kind;
	unsigned id;
	union {
		char *name;
		unsigned long long i;
		double f;
	} u;
};

struct lvalue {
	struct value *addr;
	struct bitfield bits;
};

enum instkind {
	INONE,

#define OP(op, name) op,
#include "ops.h"
#undef OP

	IARG,
	IVARARG,
};

struct irtype {
	char base, data;
	enum instkind load, store;
};

struct inst {
	enum instkind kind;
	int class;
	struct value res, *arg[2];
};

struct jump {
	enum {
		JUMP_NONE,
		JUMP_JMP,
		JUMP_JNZ,
		JUMP_RET,
		JUMP_HLT,
	} kind;
	struct value *arg;
	struct block *blk[2];
};

struct block {
	struct value label;
	struct array insts;
	struct {
		int class;
		struct block *blk[2];
		struct value *val[2];
		struct value res;
	} phi;
	struct jump jump;

	struct block *next;
	Blk *ir;  /* corresponding IR basic block, assigned in emitfunc pass 1 */
};

struct switchcase {
	struct treenode node;
	struct block *body;
};

/* DWARF variable record: a local/parameter captured at funcalloc() time,
 * mapped to its MIR value (via value id) for the final stack location. */
struct dwarf_vrec {
	const char *name;
	struct type *type;
	int value_id;
};

struct func {
	struct decl *decl, *namedecl;
	char *name;
	struct value *paramtemps;
	struct type *type;
	struct block *start, *end;
	struct map gotos;
	unsigned lastid;
	/* source location of the closing brace of the (outermost) compound
	 * statement, recorded by stmt() so diagnostics can point at the
	 * function body's end rather than the following token */
	struct location bodyend;
	/* source location of the function name (declaration), recorded by
	 * mkfunc() for DWARF subprogram DIEs */
	struct location declloc;
	/* local variables / parameters recorded by funcalloc() for DWARF
	 * variable DIEs (name, type, frontend value id -> MIR slot) */
	struct dwarf_vrec *dvars;
	int ndvars, capdvars;
};

/* Pointer width follows the selected ABI: i386 is ILP32, the remaining
 * current targets are LP64.  Do not encode pointers as Kl unconditionally;
 * doing so corrupts argument lowering on i386. */
static inline int
ptrclass(void)
{
	/* ILP32 → 'w' (4-byte word), LP64 → 'l' (8-byte quad) */
	return typelong.size == 4 ? 'w' : 'l';
}

/* --- No-op bookkeeping hooks ---------------------------------------------
 *
 * The legacy text-IL emitter used emitname/emittype to print global symbol
 * and type declarations into the IL stream. In the direct-IR model emittype
 * now builds a real IR `Typ` entry (see emittype.c); emitname stays a no-op
 * because global symbols are tracked inside Fn/Dat at emit time.
 */
static inline void emitname(struct value *v) { (void)v; }
void emittype(struct type *t);
/* reverse map: VALUE_TYPE value id (LIR typ[] index) -> frontend type
 * (registered by emittype; used by func_to_mir's fe_to_mtd) */
struct type *typeforvalue(unsigned id);

/* --- Internal helpers shared across irgen/*.c --------------------------- */

/* value.c */
struct value *mkfltconst(int kind, double n);
struct irtype irtype(struct type *t);

/* inst.c */
void functemp(struct func *f, struct value *v);
struct value *funcinst(struct func *f, int op, int class,
                       struct value *arg0, struct value *arg1);

/* convert.c */
struct value *funcbits(struct func *f, struct type *t, struct value *v,
                       struct bitfield b);
struct value *convert(struct func *f, struct type *dst, struct type *src,
                      struct value *l);

/* funcmem.c */
void calcvla(struct func *f, struct type *t);
void funcalloc(struct func *f, struct decl *d);
struct value *funcstore(struct func *f, struct type *t, enum typequal tq,
                       struct lvalue lval, struct value *v);
struct value *funcload(struct func *f, struct type *t, struct lvalue lval);

/* branch.c */
struct lvalue funclval(struct func *f, struct expr *e);

/* func_to_mir.c — translate the frontend struct func tree into MIR (MFn).
 * Part of the B.4.2 MIR-first migration: emitfunc lowers to MIR, runs MIR
 * passes, then bridges to the LIR Fn. */
struct MFn;
struct MFn *func_to_mir(struct func *f, int optlevel, bool export);

#endif /* MCC_IRGEN_H */
