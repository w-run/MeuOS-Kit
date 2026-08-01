/* mir.h — mcc MIR (Medium IR) definition.
 *
 * MIR is the language-independent middle-end of mcc, shared by the C
 * frontend (mcc) and the C++ frontend (m++).  It is the platform-neutral
 * IR on which all target-independent optimization passes operate, and is
 * deliberately decoupled from the existing QBE-derived `Fn`/`Ins` (which
 * becomes the LIR layer after the MIR/LIR split).
 *
 * Design goals (see .issues/IR-DESIGN.md):
 *   - complete scalar type system (i8..i64, f32/f64, pointer) plus
 *     aggregates, decoupled from any particular target word size
 *   - explicit SSA with explicit Phi nodes
 *   - three-address instructions, at most 2 sources and 1 destination
 *   - a per-instruction `extra` extension word and a per-aggregate `ext`
 *     slot so the C++ frontend can add new instructions/types without
 *     changing the core structures
 *   - separate value table and constant pool
 */
#ifndef MCC_MIR_H
#define MCC_MIR_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Forward typedefs so construction APIs can use the public names before
 * the full struct definitions below. */
typedef struct MFn MFn;
typedef struct MBlk MBlk;
typedef struct MVal MVal;
typedef struct MConst MConst;
typedef struct MTypeDesc MTypeDesc;
typedef struct MIns MIns;
typedef struct MPhi MPhi;
typedef struct MRef MRef;

/* ---- Scalar types -------------------------------------------------------
 * MT_AGG is the marker type for aggregates; the actual layout lives in a
 * MTypeDesc.  Signedness is carried by the opcode (MOP_DIV vs MOP_UDIV),
 * not by the type, mirroring the existing IR.
 */
typedef enum MType {
	MT_NONE = 0,
	MT_VOID,
	MT_I8,
	MT_I16,
	MT_I32,
	MT_I64,
	MT_F32,
	MT_F64,
	MT_PTR,
	MT_AGG,          /* struct / union / array, see MTypeDesc */
	MT_NTYPE,
} MType;

/* Per-type static properties (size/align for scalars, -1 for MT_AGG).
 * ABI-dependent layout is applied later; these are the machine-neutral
 * minimums that LIR then refines per target. */
struct MTypeInfo {
	int16_t size;      /* bytes, -1 for MT_AGG */
	int16_t align;     /* bytes */
	uint8_t isint;     /* integer (incl. pointer) */
	uint8_t isfloat;
	uint8_t isptr;
	uint8_t isagg;
};

extern const struct MTypeInfo mtype_info[MT_NTYPE];
int mtypesize(MType t);     /* -1 for MT_AGG */
int mtypealign(MType t);
bool mtypeisint(MType t);
bool mtypeisfloat(MType t);
bool mtypeisptr(MType t);

/* ---- Aggregates --------------------------------------------------------- */

typedef struct MField {
	MType type;              /* scalar field type */
	MTypeDesc *sub;          /* nested aggregate field (type==MT_AGG) */
	const char *name;
	int64_t offset;          /* byte offset (union: 0 for all) */
	/* bitfield */
	int16_t bitoff;          /* bit offset within storage unit, -1 if not a bf */
	int16_t bits;            /* bit width, 0 if not a bitfield */
} MField;

typedef struct MTypeDesc {
	uint32_t id;
	const char *name;
	bool is_union;
	bool is_array;
	bool is_incomplete;
	uint64_t size;           /* bytes */
	int32_t align;           /* bytes */
	uint32_t nfield;
	MField *field;
	/* array dimensions (is_array) */
	MType elem_type;         /* scalar elem type, or MT_AGG with elem_desc */
	MTypeDesc *elem_desc;
	uint64_t nelem;
	/* extension slot (C++ frontend: vtbl pointer, RTTI, etc.) */
	void *ext;
	uint32_t refs;           /* number of active references */
} MTypeDesc;

MTypeDesc *mtd_new(const char *name, bool is_union);
MTypeDesc *mtd_array(MTypeDesc *elem, uint64_t nelem);
void mtd_add_field(MTypeDesc *td, const char *name, MType t,
                   MTypeDesc *sub, int64_t off,
                   int16_t bitoff, int16_t bits);
void mtd_finalize(MTypeDesc *td);

/* Register an aggregate in the function type table; returns its id. */
uint32_t mfn_addtype(MFn *fn, MTypeDesc *td);

/* ---- Values and constants ---------------------------------------------- */

typedef enum MValKind {
	MV_NONE,
	MV_TEMP,       /* SSA value, defined by an MIns or MPhi */
	MV_GLOBAL,     /* reference to a global symbol */
	MV_CONST,      /* constant, see MConst */
	MV_TYPE,       /* reference to an aggregate MTypeDesc */
	MV_LABEL,      /* block label */
} MValKind;

typedef struct MUse {
	struct MIns *ins;    /* using instruction */
	struct MPhi *phi;    /* using phi (one of the two is set) */
	int argn;            /* operand index (-1 for phi) */
} MUse;

typedef struct MVal {
	uint32_t id;
	MValKind kind;
	MType type;
	struct MTypeDesc *td;      /* aggregate type when type==MT_AGG */
	/* definition */
	struct MIns *def;          /* MV_TEMP defined by an instruction */
	struct MPhi *defphi;       /* MV_TEMP defined by a phi */
	struct MBlk *defblk;       /* block of definition */
	/* uses */
	uint32_t nuse, cuse;
	MUse *use;
	/* stack slot for spilled values, -1 unset */
	int32_t slot;
	/* lowering hint (LIR): preferred register class/reg, -1 none */
	int32_t hint;
	const char *name;
	/* global symbol payload */
	const char *sym;
	int32_t symref;            /* LIR sym id, -1 unset */
	bool tls;                  /* MV_GLOBAL: thread-local symbol */
	bool isext;                /* MV_GLOBAL: external symbol */
	/* constant payload (MV_CONST) */
	struct MConst *con;
	/* LIR temp index (bridge): -1 until lir_bridge assigns it */
	int32_t lirtmp;
} MVal;

typedef enum MConstKind {
	MC_UNDEF,
	MC_INT,
	MC_FLT,
	MC_ADDR,       /* symbol address (+ offset), optional TLS */
} MConstKind;

typedef struct MConst {
	uint32_t id;
	MConstKind kind;
	MType type;
	uint64_t hash;
	union {
		int64_t i;
		double d;
		float s;
		struct {
			const char *sym;
			int64_t off;
			bool tls;        /* TLS access */
			bool isext;      /* external symbol */
		} addr;
	} u;
} MConst;

/* ---- Instructions ------------------------------------------------------ */

typedef enum MOP {
	MOP_NONE,

	/* arithmetic */
	MOP_ADD, MOP_SUB, MOP_MUL,
	MOP_DIV, MOP_UDIV, MOP_REM, MOP_UREM,
	MOP_NEG, MOP_AND, MOP_OR, MOP_XOR,
	MOP_SHL, MOP_SHR, MOP_SAR,

	/* comparisons (src[0] op src[1] -> 0/1 in dst) */
	MOP_CEQ, MOP_CNE,
	MOP_CSLT, MOP_CSLE, MOP_CSGT, MOP_CSGE,
	MOP_CULT, MOP_CULE, MOP_CUGT, MOP_CUGE,
	MOP_CFEQ, MOP_CFNE, MOP_CFLT, MOP_CFLE, MOP_CFGT, MOP_CFGE,

	/* memory */
	MOP_LOAD,        /* dst = load(src[0])              */
	MOP_STORE,       /* store(src[1]) -> src[0]         */
	MOP_ALLOCA,      /* dst = alloca(src[0])            */

	/* conversions */
	MOP_SEXT, MOP_ZEXT, MOP_TRUNC,       /* integer widening/narrowing */
	MOP_CAST,                              /* pointer/float bit-preserving */
	MOP_F2I, MOP_I2F,                     /* float<->int with rounding */
	MOP_FEXT, MOP_FTRUNC,                 /* f32<->f64 */

	/* control (terminators) */
	MOP_JMP,         /* unconditional jump             */
	MOP_JNZ,         /* cond = src[0]; jump s1 if nonzero else s2 */
	MOP_RET,         /* return src[0] (or void)        */
	MOP_CALL,        /* function call, args via MOP_ARG */

	/* parameter passing */
	MOP_ARG,         /* argument value for following MOP_CALL */
	MOP_PAR,         /* function parameter (entry block) */
	MOP_VARARG,      /* variadic marker */

	/* misc */
	MOP_PHI,
	MOP_COPY,
	MOP_VASTART,
	MOP_VAARG,
	MOP_SALLOC,      /* dynamic stack allocation (VLA) */

	/* extension point for the C++ frontend */
	MOP_EXTRA,       /* first language-specific op; MIns.extra selects */

	MOP_NOP,
} MOP;

/* MIns operand addressing: a value reference that may be an MVal (SSA) or
 * an MConst (constant folded into the instruction). */
typedef struct MRef {
	MVal *val;       /* non-NULL: SSA value */
	MConst *con;     /* non-NULL: constant */
} MRef;

#define MREF_VAL(v)  ((MRef){ .val = (v), .con = 0 })
#define MREF_CON(c)  ((MRef){ .val = 0,  .con = (c) })

struct MIns {
	uint32_t id;
	MOP op;
	MType dtype;       /* destination type (for MOP_LOAD: loaded type) */
	MVal *dst;         /* result, may be NULL for stores/jumps */
	MRef src[2];       /* at most 2 sources */
	MConst *cst;       /* optional const operand (MOP_LOAD offset etc.) */
	uint64_t extra;    /* extension word (C++ frontend) */
	struct MBlk *blk;  /* owning block */
};

/* ---- Phi nodes (explicit SSA) ------------------------------------------ */

struct MPhi {
	uint32_t id;
	MVal *dst;
	MType dtype;
	uint32_t narg, carg;
	MVal **arg;
	struct MBlk **blk;   /* predecessor block for each arg */
	int32_t visit;
	struct MPhi *link;   /* block phi list */
};

/* ---- Blocks and functions ---------------------------------------------- */

struct MBlk {
	uint32_t id;
	MPhi *phi;           /* entry phis */
	MIns *ins;           /* instruction array */
	uint32_t nins, cins;
	MIns term;           /* terminator (MOP_JMP/JNZ/RET/CALL), src in .src */
	/* control flow */
	struct MBlk *s1, *s2;
	struct MBlk **pred;  /* predecessor blocks */
	uint32_t npred;
	/* dominator info */
	struct MBlk *idom, *dom, *dlink;
	struct MBlk **fron;
	uint32_t nfron;
	int32_t depth;
	int32_t loop;
	struct MBlk *link;   /* function block list */
	char *name;
	uint32_t visit;
};

struct MFn {
	const char *name;
	MBlk *start;
	MBlk *link;          /* block list head (linked via ->link) */
	MVal **val;          /* value table */
	uint32_t nval, cval;
	MConst **con;        /* constant pool */
	uint32_t ncon, ccon;
	MTypeDesc **typ;     /* aggregate type table */
	uint32_t ntyp, ctyp;
	uint32_t nblk;
	int retty;           /* return aggregate type id, -1 if scalar */
	MType rettype;       /* scalar return type */
	MVal **param;        /* parameter values (entry block MOP_PARs) */
	uint32_t nparam;
	int *paramty;        /* per-parameter aggregate typ[] index, -1 if scalar */
	int32_t slot;        /* total stack slot bytes */
	int32_t salign;      /* stack alignment */
	bool vararg;
	bool export;         /* external linkage (true) or local/static (false) */
	int optlevel;        /* 0=O0, 1=O1, 2=O2 (default), 3=O3 */
	bool emitted;        /* codegen already run */
};

/* ---- Construction API (src/mir/build.c) -------------------------------- */

MFn *mfn_new(const char *name, int optlevel);
MBlk *mblk_new(MFn *fn, const char *name);
void mfn_addblk(MFn *fn, MBlk *b);

MVal *mval_new(MFn *fn, MValKind kind, MType t, struct MTypeDesc *td,
               const char *name);
MVal *mval_const(MFn *fn, MType t, MConst *c);
MVal *mval_global(MFn *fn, const char *sym, bool isext, bool tls);
MVal *mval_type(MFn *fn, MTypeDesc *td);
MVal *mval_label(MFn *fn, MBlk *b);

MConst *mconst_int(MFn *fn, MType t, int64_t v);
MConst *mconst_flt(MFn *fn, MType t, double v);
MConst *mconst_addr(MFn *fn, const char *sym, int64_t off,
                    bool tls, bool isext);

MIns *madd(MFn *fn, MBlk *b, MOP op, MType dtype, MVal *dst,
           MRef a0, MRef a1);
MIns *madd0(MFn *fn, MBlk *b, MOP op, MType dtype, MVal *dst);
MIns *madd1(MFn *fn, MBlk *b, MOP op, MType dtype, MVal *dst, MRef a0);
void mterm(MFn *fn, MBlk *b, MOP op, MRef a0, struct MBlk *s1,
           struct MBlk *s2);
void mret(MFn *fn, MBlk *b, MRef a0);
void mretvoid(MFn *fn, MBlk *b);

MVal *mphi_add(MFn *fn, MBlk *b, MType dtype, MVal *dst);

void mfn_dump(MFn *fn, FILE *out);
void mfn_free(MFn *fn);

/* ---- optimization passes (src/mir/passes.c) ---------------------------- */

enum MIRPass {
	MIR_PASS_USES,   /* build use chains */
	MIR_PASS_FOLD,   /* constant folding + algebraic simplification */
	MIR_PASS_DCE,    /* dead code elimination */
	MIR_PASS_N,
};

uint32_t run_mir_pass(MFn *fn, enum MIRPass pass);
void run_mir_passes(MFn *fn, int optlevel);
void build_uses(MFn *fn);

/* ---- MIR → LIR bridge (src/lir/bridge.c) ------------------------------ */

struct Fn;
struct Fn *lir_bridge(MFn *mfn);

/* arena helpers (mir_util.c) */
void *m_alloc(MFn *fn, size_t size);
void m_arena_free(MFn *fn);
char *mx_strdup(const char *s);

#endif /* MCC_MIR_H */
