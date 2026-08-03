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

/* Machine layer (P1+, x86-64 first).  Independent structures so the MIR
 * backend is MIR-native: no QBE Fn/Ins/Ref representation. */
typedef struct MInsM MInsM;
typedef struct MBlkM MBlkM;
typedef struct MFnM MFnM;

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
	MV_REG,        /* physical register (MVal.reg), machine layer */
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
	/* MV_REG: physical register id (MREG_*), MREG_NONE otherwise */
	int32_t reg;
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
	MOP_UI2F,                             /* unsigned int -> float */
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
	MVal **reg;          /* physical register table (indexed by MReg), NULL until first mfn_reg() */
	uint32_t nreg;       /* capacity of reg[] (MREG_NREG once allocated) */
	MTypeDesc *rettyd;   /* aggregate return MTypeDesc (func_to_mir), NULL if scalar */
	void **tdkey;        /* frontend struct type* -> MTypeDesc cache (fe_to_mtd) */
	MTypeDesc **tdcache;
	uint32_t ntdc;
	int32_t slot;        /* total stack slot bytes */
	int32_t salign;      /* stack alignment */
	bool vararg;
	bool export;         /* external linkage (true) or local/static (false) */
	int optlevel;        /* 0=O0, 1=O1, 2=O2 (default), 3=O3 */
	bool emitted;        /* codegen already run */
	/* Frontend value-id -> MVal side table (func_to_mir).  Indexed by
	 * frontend `struct value` ids (VALUE_TEMP; ids start at 1); NULL for
	 * ids that do not correspond to a MIR value.  Used to map frontend
	 * locals back to their MIR values after the machine backend assigns
	 * final stack slots / registers (DWARF variable locations). */
	MVal **vmap;
	uint32_t nvmap;
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
	MIR_PASS_COPY,   /* copy propagation + phi copy propagation */
	MIR_PASS_GVN,    /* global value numbering */
	MIR_PASS_DCE,    /* dead code elimination */
	MIR_PASS_SSA,    /* SSA consistency check (mssa_check) */
	MIR_PASS_N,
};

uint32_t run_mir_pass(MFn *fn, enum MIRPass pass);
void run_mir_passes(MFn *fn, int optlevel);
int mssa_check(MFn *fn);
void build_uses(MFn *fn);
uint32_t mcopy(MFn *fn);
uint32_t mgvn(MFn *fn);
void mref_replace(MFn *fn, MVal *old, MRef nr);

/* ---- MIR → LIR bridge (src/lir/bridge.c) ------------------------------ */

struct Fn;
struct Fn *lir_bridge(MFn *mfn);

/* arena helpers (mir_util.c) */
void *m_alloc(MFn *fn, size_t size);
void m_arena_free(MFn *fn);
char *mx_strdup(const char *s);

/* =====================================================================
 * Machine layer (P1+).  MIR-native backend structures: physical
 * registers as MVal, addressing modes, machine opcodes, machine
 * functions/blocks/instructions.  No QBE Fn/Ins/Ref anywhere.
 * =================================================================== */

/* ---- Physical registers (target-parameterized) ------------------------ */

typedef enum MRegCls {
	MRC_GPR,         /* general purpose register file */
	MRC_FPR,         /* floating point / SIMD register file */
} MRegCls;

typedef struct MRegInfo {
	const char *name;      /* assembler name: "rax", "xmm0", "r0", ... */
	MRegCls cls;
	bool caller_saved;
	bool callee_saved;
	bool arg;              /* argument register under the target ABI */
} MRegInfo;

/* Register id is target-defined: 0..mt->nreg-1.  Register counts differ
 * per architecture (i386 8 GPRs, arm 16 GPRs + 32 D regs, aarch64/
 * riscv64/loongarch64 31+ GPRs + 32 FPRs, x86_64 16 GPRs + 16 XMM). */
typedef int32_t MReg;

/* Per-architecture machine target description.  The MIR backend is
 * target-parameterized through this structure (mirrors the existing
 * struct Target abstraction, but MIR-native). */
typedef struct MTargetM {
	const char *name;          /* "x86_64", "aarch64", ... */
	/* register file */
	uint16_t nreg;             /* total register count */
	const MRegInfo *regs;      /* per-register descriptions, nreg entries */
	uint16_t gpr0, ngpr;       /* GPR range [gpr0, gpr0+ngpr) */
	uint16_t fpr0, nfpr;       /* FPR range (may be empty, e.g. i386 x87) */
	uint64_t rglob;            /* registers never allocated (SP/FP/...), bitmask */
	uint64_t reserved;         /* implicit scratch registers, bitmask */
	uint64_t scratch;          /* emitter temporaries excluded from regalloc, bitmask */
	/* ABI */
	const int *argreg;         /* argument registers in ABI order, -1 end */
	const int *rsave;          /* caller-saved registers, -1 end */
	const int *rclob;          /* callee-saved registers, -1 end */
	int ptrsize;               /* 8 (LP64) or 4 (ILP32: i386/arm) */
	int stackalign;            /* stack alignment in bytes */
	bool kl_in_reg;            /* 64-bit ints may live in GPRs (0 for i386/arm) */
	uint32_t feat;             /* MTF_* feature bits */
	int sret_reg;              /* register holding the hidden sret buffer
	                            * (x86_64: RDI, riscv64/arm64: A0), or -1 */
	/* machine backend entry points (per-arch, filled progressively) */
	void (*abi)(MFnM *);       /* ABI lowering: selpar/selcall/selret (P2) */
} MTargetM;

/* machine target feature bits (isel/emit guidance) */
#define MTF_SCALE_INDEX (1u<<0)  /* base+index*scale addressing (x86/i386) */
#define MTF_COND_EXEC   (1u<<1)  /* every instruction predicated (arm) */
#define MTF_CMOV        (1u<<2)  /* conditional-move if-conversion (x86_64) */

extern const MTargetM mtarget_x86_64;   /* x86_64 first; others follow per-arch */
extern const MTargetM mtarget_riscv64;  /* P3a: riscv64 MIR-native (LP64D) */
extern const MTargetM mtarget_loongarch64; /* P3b: loongarch64 MIR-native (LP64D) */
extern const MTargetM mtarget_aarch64;  /* P3b: aarch64 MIR-native (AAPCS64) */

const char *mreg_name(const MTargetM *mt, MReg r);
/* Look up a register by assembler name; returns -1 if not found. */
int mreg_id(const MTargetM *mt, const char *name);

/* Return the MVal for a physical register of `mt`, creating it on first
 * use.  Register values live in fn->reg[] (outside the SSA val table). */
MVal *mfn_reg(MFn *fn, const MTargetM *mt, MReg r);

/* ---- Addressing modes ------------------------------------------------ */

typedef struct MAddr {
	MVal *base;          /* base register, may be NULL */
	MVal *index;         /* index register, may be NULL */
	uint8_t scale;       /* index scale: 1, 2, 4 or 8 */
	int64_t off;         /* displacement */
	MConst *offcon;      /* symbol-relative displacement (relocation), else NULL */
} MAddr;

MAddr maddr(MVal *base, MVal *index, uint8_t scale, int64_t off);
MAddr maddr_sym(MVal *base, MConst *offcon, int64_t off);

/* ---- Machine opcodes ------------------------------------------------- */

typedef enum MMOP {
	MMOP_NONE,
	/* data movement */
	MMOP_MOV,              /* dst = src (reg / imm / mem via addr) */
	MMOP_MOVSX,            /* sign-extending move, src width in dtype/src */
	MMOP_MOVZX,            /* zero-extending move */
	MMOP_LEA,              /* dst = &addr */
	MMOP_PUSH, MMOP_POP,
	/* integer arithmetic */
	MMOP_ADD, MMOP_SUB, MMOP_MUL, MMOP_AND, MMOP_OR, MMOP_XOR,
	MMOP_SHL, MMOP_SHR, MMOP_SAR,
	MMOP_NEG, MMOP_NOT,
	MMOP_DIV, MMOP_UDIV, MMOP_REM, MMOP_UREM,  /* RAX/RDX implicit pair */
	/* floating point arithmetic (dtype distinguishes f32/f64) */
	MMOP_FADD, MMOP_FSUB, MMOP_FMUL, MMOP_FDIV,
	MMOP_FNEG, MMOP_FSQRT,
	/* conversions */
	MMOP_CVTSI2SS, MMOP_CVTSI2SD, MMOP_CVTSS2SD, MMOP_CVTSD2SS,
	MMOP_CVTTSS2SI, MMOP_CVTTSD2SI,
	MMOP_CVTSI2SD_U,                       /* unsigned int -> double (64-bit trick) */
	MMOP_CVTSI2SS_U,                       /* unsigned int -> float */
	/* memory */
	MMOP_LOAD,             /* width from dtype; operand = addr */
	MMOP_LOAD_S8, MMOP_LOAD_S16, MMOP_LOAD_S32,   /* sign-extending loads */
	MMOP_LOAD_Z8, MMOP_LOAD_Z16, MMOP_LOAD_Z32,   /* zero-extending loads */
	MMOP_STORE,            /* src[0] = value; operand = addr */
	MMOP_BLIT,             /* aggregate copy: dst ptr, src ptr, cst = size */
	MMOP_ALLOCA4, MMOP_ALLOCA8, MMOP_ALLOCA16,
	MMOP_SALLOC,           /* dynamic stack allocation */
	MMOP_VASTART,          /* initialize va_list (src[0] = ap ptr) */
	MMOP_VAARG,            /* dst = va_arg(src[0] = ap ptr, dtype) */
	/* flags */
	MMOP_CMP,              /* compare src[0] vs src[1], set flags */
	MMOP_TEST,             /* and src[0], src[1], set flags */
	MMOP_SETCC,            /* dst = flags.cc ? 1 : 0 */
	MMOP_CMOV,             /* dst = flags.cc ? src[0] : dst (if-conversion) */
	MMOP_SETCCR,           /* dst = (src[0] cc src[1]) ? 1 : 0 — register
	                        * comparison for flag-less ISAs (riscv64 slt/...).
	                        * cc carries the condition, operands src[0]/src[1]. */
	/* parameter passing (pre-ABI markers consumed by the ABI lowering) */
	MMOP_PARM,             /* function parameter (entry block); td = agg type */
	MMOP_ARG,              /* call argument; td = agg type for aggregates */
	/* control flow */
	MMOP_JMP,              /* terminator; target via MBlkM.s1 */
	MMOP_JCC,              /* terminator; cc + s1(taken)/s2(fallthrough) */
	MMOP_CALL,             /* src[0] = callee; td = agg return type */
	MMOP_RET,              /* src[0] = return value, or none; td = agg ret type */
	MMOP_NOP,
} MMOP;

const char *mmop_name(MMOP op);

/* ---- Condition codes -------------------------------------------------- */

/* Semantic condition codes shared by all targets (x86 setcc/jcc, arm/
 * aarch64 predicates, riscv64/loongarch64 branch+set mappings):
 *   EQ/NE  ==/!=   CS/CC  unsigned >=/< (carry set/clear)
 *   MI/PL  negative/zero-or-positive   VS/VC  overflow set/clear
 *   HI/LS  unsigned >/<=   GE/LT/GT/LE  signed   AL  always
 * The emitter maps each MCC to the target's mnemonic (x86: b=CC, be=LS,
 * a=HI, ae=CS; arm/aarch64: identical names). */
typedef enum MCC {
	MCC_NONE,
	MCC_EQ,  MCC_NE,
	MCC_CS,  MCC_CC,
	MCC_MI,  MCC_PL,
	MCC_VS,  MCC_VC,
	MCC_HI,  MCC_LS,
	MCC_GE,  MCC_LT,  MCC_GT,  MCC_LE,
	MCC_AL,
	MCC_NCC,
} MCC;

const char *mcc_name(MCC cc);

/* Inverse condition (cmov complement / branch negation). */
static inline MCC
mcc_neg(MCC cc)
{
	switch (cc) {
	case MCC_EQ:  return MCC_NE;
	case MCC_NE:  return MCC_EQ;
	case MCC_CS:  return MCC_CC;
	case MCC_CC:  return MCC_CS;
	case MCC_MI:  return MCC_PL;
	case MCC_PL:  return MCC_MI;
	case MCC_VS:  return MCC_VC;
	case MCC_VC:  return MCC_VS;
	case MCC_HI:  return MCC_LS;
	case MCC_LS:  return MCC_HI;
	case MCC_GE:  return MCC_LT;
	case MCC_LT:  return MCC_GE;
	case MCC_GT:  return MCC_LE;
	case MCC_LE:  return MCC_GT;
	default:      return MCC_AL;   /* AL/NONE: no inverse */
	}
}

/* ---- Machine functions / blocks / instructions ------------------------ */

struct MInsM {
	uint32_t id;
	uint32_t pos;            /* global linear-scan position (regalloc) */
	MMOP op;
	MType dtype;           /* result / memory width */
	MVal *dst;             /* result value (register or temp) */
	MVal *src[3];          /* source values (registers/temps) */
	MConst *cst;           /* immediate / symbol constant, else NULL */
	MAddr addr;            /* addressing mode (LOAD/STORE/LEA/BLIT) */
	MCC cc;                /* condition code (SETCC / JCC terminator) */
	MTypeDesc *td;         /* aggregate type (PARM/ARG/CALL/RET), else NULL */
	uint64_t extra;        /* extension word (C++ frontend) */
	struct MBlkM *blk;     /* owning machine block */
};

struct MBlkM {
	uint32_t id;
	char *name;
	MInsM *ins;            /* instruction array (terminator excluded) */
	uint32_t nins, cins;
	MInsM term;            /* terminator: MMOP_JMP/JCC/RET/CALL */
	MBlkM *s1;             /* JMP: target; JCC: taken target */
	MBlkM *s2;             /* JCC: fallthrough target */
	MBlkM *link;           /* function block list */
};

struct MFnM {
	const char *name;
	const MTargetM *mt;      /* target this machine function is lowered for */
	MFn *host;               /* owning MIR function: const pool, arena */
	MBlkM *start;            /* entry block */
	MBlkM *link;             /* block list head */
	uint32_t nblk;
	MTypeDesc *retty;        /* aggregate return type, NULL for scalar/void */
	bool has_sret;           /* sret: the hidden buffer register (mt->sret_reg)
	                          * is pinned for the whole function */
	MVal *sret_pad;          /* sret: alloca holding the buffer ptr across calls */
	uint32_t vafa;           /* varargs: packed arg-register usage (selpar) */
	int32_t slot;            /* stack frame size (bytes); filled by regalloc/spill */
	int32_t salign;          /* frame alignment */
	uint64_t regsused;       /* bitmask of used physical registers (bit = MReg) */
	uint32_t nspill;         /* number of stack spill slots */
	bool dynalloc;           /* function performs a runtime (VLA) alloca */
};

/* ---- Machine construction API (src/mir/machine.c) --------------------- */

MFnM *mfnm_new(MFn *host, const MTargetM *mt, const char *name);
MBlkM *mblkm_new(MFnM *fm, const char *name);
void mfnm_addblk(MFnM *fm, MBlkM *b);

/* generic instruction builders: 0/1/2/3 source operands */
MInsM *maddm(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
             MVal *s0, MVal *s1);
MInsM *maddm3(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
              MVal *s0, MVal *s1, MVal *s2);
MInsM *maddm_addr(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
                  MAddr a, MVal *s0);
MInsM *maddm_cst(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
                 MVal *s0, MConst *cst);
MInsM *maddm_cc(MFnM *fm, MBlkM *b, MMOP op, MType dtype, MVal *dst,
                MVal *s0, MVal *s1, MCC cc);
MInsM *maddm_blit(MFnM *fm, MBlkM *b, MVal *dstptr, MVal *srcptr,
                  MConst *size);

/* block terminator */
void mfnm_term(MFnM *fm, MBlkM *b, MMOP op, MVal *s0, MBlkM *s1, MBlkM *s2,
               MCC cc);

void mfnm_dump(MFnM *fm, FILE *out);
void mfnm_free(MFnM *fm);

/* ---- Register allocation (P4) -------------------------------------------- */

/* Spill-slot packing (P4b): slot4/slot8 double cursor. */
typedef struct MRegSlots {
	int32_t slot4;   /* 4-byte units cursor */
	int32_t slot8;   /* 4-byte units cursor */
} MRegSlots;

/* Allocate a stack slot; returns the negative rbp offset. */
int32_t mreg_slot_alloc(MRegSlots *s, MType t);
/* Total frame bytes for the slots, 16-aligned. */
int32_t mreg_slot_total(const MRegSlots *s);

/* Linear-scan register allocation for a lowered MFnM (post-ABI, pre-emit).
 * Fills MVal.reg (physical register) or MVal.slot (spill), plus fm->slot /
 * fm->regsused for frame emission. */
void mfnm_regalloc(MFnM *fm);

/* If-conversion: replace simple branch diamonds (ternary / short if-else
 * assignments) with MMOP_CMOV conditional moves.  Runs after isel, before
 * the ABI lowering. */
void mfnm_ifconv(MFnM *fm);

#endif /* MCC_MIR_H */
