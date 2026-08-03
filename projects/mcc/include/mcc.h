#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>

struct func;

enum tokenkind {
#define TOKEN(t, s) t,
#include "tokens.h"
#undef TOKEN

	TPPIDENT = TALIGNAS,
	TIDENT = TDEFINE
};

struct location {
	const char *file;
	size_t line, col;
};

struct token {
	enum tokenkind kind;
	/* whether or not the token is ineligible for expansion */
	bool hide;
	/* whether or not the token was preceeded by a space */
	bool space;
	struct location loc;
	char *lit;
};

enum typequal {
	QUALNONE,

	QUALCONST    = 1<<1,
	QUALRESTRICT = 1<<2,
	QUALVOLATILE = 1<<3,
	QUALATOMIC   = 1<<4,
	QUALCONSTEXPR = 1<<5
};

enum typekind {
	TYPENONE,

	TYPEVOID,
	TYPEBOOL,
	TYPECHAR,
	TYPESHORT,
	TYPEINT,
	TYPEENUM,
	TYPELONG,
	TYPELLONG,
	TYPEFLOAT,
	TYPEDOUBLE,
	TYPELDOUBLE,
	TYPEPOINTER,
	TYPEARRAY,
	TYPEFUNC,
	TYPESTRUCT,
	TYPEUNION,
	TYPENULLPTR,
	TYPEBITINT,
	TYPEATOMIC,
	TYPEDECIMAL32,
	TYPEDECIMAL64,
	TYPEDECIMAL128,
};

enum typeprop {
	PROPNONE,

	PROPCHAR    = 1<<0,
	PROPINT     = 1<<1,
	PROPREAL    = 1<<2,
	PROPARITH   = 1<<3,
	PROPSCALAR  = 1<<4,
	PROPFLOAT   = 1<<5,
	PROPVM      = 1<<6, /* variably-modified type */
	PROPFLEX    = 1<<7, /* struct with flexible array member */
	PROPCOMPLEX = 1<<8, /* complex type */
};

struct bitfield {
	short before;  /* number of bits in the storage unit before the bit-field */
	short after;   /* number of bits in the storage unit after the bit-field */
};

/* C++ member access control.  Stored in struct member.access (and the
 * struct builder's current level); C aggregates are ACC_PUBLIC. */
enum member_access {
	ACC_PUBLIC = 0,
	ACC_PRIVATE,
	ACC_PROTECTED,
};

struct member {
	char *name;
	struct type *type;
	enum typequal qual;
	unsigned long long offset;
	struct bitfield bits;
	/* C++ access control (ACC_PUBLIC for C aggregates): enforced on
	 * `obj.member` / `obj->member` access outside the member's class. */
	unsigned char access;
	/* C++ mutable member: writable even through a const this pointer. */
	bool is_mutable;
	/* C++ virtual member function: dispatched via the object's vtable. */
	bool is_virtual;
	/* C++ const member function (trailing `const` qualifier). */
	bool is_const;
	/* C++ vtable slot index (valid once the class layout is finalized). */
	int vslot;
	struct member *next;
};

/* C++ vtable slot (full definition in cpp/cpp.h). */
struct cpp_vslot;

struct type {
	enum typekind kind;
	enum typeprop prop;
	int align;
	unsigned long long size;
	struct value *value;  /* used by the backend */
	struct type *base;
	struct list link;  /* used only during construction of type */
	/* C++: the scope the class was declared in (namespace scope for
	 * `namespace n { class C {}; }`); member symbols live there. */
	struct scope *scope;
	/* qualifiers of the base type */
	enum typequal qual;
	bool incomplete;
	/* C++ reference type: a pointer that auto-dereferences in
	 * expressions and binds to the address of its initializer. */
	bool isref;
	/* C++ rvalue reference (`T &&`): like a reference but only binds to
	 * temporary (rvalue) arguments; distinguishes the move-constructor /
	 * move-assignment overloads from the by-value and lvalue-ref ones. */
	bool isrref;
	union {
		struct {
			bool issigned, iscomplex;
			int width;
		} arith;
		struct {
			struct expr *length;
			enum typequal ptrqual;
			struct value *size;
		} array;
		struct {
			bool isvararg;
			struct decl *params;
			size_t nparam;
		} func;
		struct {
			char *tag;
			struct member *members;
			/* C++ virtual dispatch (m++).  `own_poly` is set when the
			 * class itself declares a virtual function; `poly` means the
			 * object needs vptr initialization (own or inherited). */
			bool poly;
			bool own_poly;
			struct cpp_vslot *own_virtuals; /* virtual fns declared here */
			struct cpp_vslot *vslots;       /* finalized vtable layout */
			int nvslots;
			struct type *primary_base;      /* first polymorphic base */
		} structunion;
		struct {
			enum typequal basequal;
		} atomic;
	} u;
};

enum declkind {
	DECLTYPE,
	DECLOBJECT,
	DECLFUNC,
	DECLCONST,
	DECLBUILTIN,
	DECLNAMESPACE,
};

enum linkage {
	LINKNONE,
	LINKINTERN,
	LINKEXTERN,
};

enum storageduration {
	SDSTATIC,
	SDTHREAD,
	SDAUTO,
};

enum builtinkind {
	BUILTINALLOCA,
	BUILTINCONSTANTP,
	BUILTINEXPECT,
	BUILTININFF,
	BUILTINNANF,
	BUILTINOFFSETOF,
	BUILTINTYPESCOMPATIBLEP,
	BUILTINUNREACHABLE,
	BUILTINVAARG,
	BUILTINVACOPY,
	BUILTINVAEND,
	BUILTINVALIST,
	BUILTINVASTART,
	/* Lowered by IR generation to the compiler atomic ABI.  Keeping these
	 * as builtins lets <stdatomic.h> be a small, target-neutral wrapper. */
	BUILTINATOMICFETCHADD,
	BUILTINATOMICFETCHSUB,
	BUILTINATOMICADDASSIGN,
	BUILTINATOMICSUBASSIGN,
	BUILTINATOMICFETCHAND,
	BUILTINATOMICFETCHOR,
	BUILTINATOMICFETCHXOR,
	BUILTINATOMICEXCHANGE,
	BUILTINATOMICCOMPAREEXCHANGE,
};

struct decl {
	char *name;
	enum declkind kind;
	enum linkage linkage;
	struct type *type;
	enum typequal qual;
	struct value *value;
	char *asmname;
	bool defined;
	bool tentative;
	/* C++: destructor already emitted for this local object (so a
	 * return-statement run and the block-exit run don't double-destroy). */
	bool dtor_done;
	struct decl *next;

	union {
		struct {
			/* alignment of object storage (may be stricter than type requires) */
			int align;
			enum storageduration storage;
			/* C++ constexpr variable: the constant value of its (folded)
			 * initializer, usable in later constant expressions. */
			unsigned long long constval;
			bool has_constval;
		} obj;
		struct {
			/* the function might have an "inline definition" (C11 6.7.4p7) */
			bool inlinedefn;
			bool isnoreturn;
			bool isconstexpr;   /* C++ constexpr function */
			/* C99 6.7.4p6: an inline definition is not itself an external
			 * definition.  The body is deferred here (with its function
			 * scope held open) until a later `extern`/non-inline
			 * declaration promotes it to an external definition, or the
			 * translation unit ends and it is dropped (defect c-01). */
			struct func *deferfn;
			struct scope *deferscope;
		} func;
		unsigned long long enumconst;
		enum builtinkind builtin;
		struct scope *ns; /* DECLNAMESPACE: the namespace's scope */
	} u;
};

struct scope {
	struct map tags;
	struct map decls;
	struct block *breaklabel;
	struct block *continuelabel;
	struct switchcases *switchcases;
	struct scope *parent;
	/* C++ namespace name (NULL for ordinary scopes). */
	const char *name;
	/* Class-typed objects declared in this scope, in declaration order
	 * (C++ destroys them in reverse order at scope exit). */
	struct decl *objects;
};

enum exprkind {
	/* primary expression */
	EXPRIDENT,
	EXPRCONST,
	EXPRSTRING,

	/* postfix expression */
	EXPRCALL,
	/* member E.M gets transformed to *(typeof(E.M) *)((char *)E + offsetof(typeof(E), M)) */
	EXPRBITFIELD,
	EXPRINCDEC,
	EXPRCOMPOUND,
	/* subscript E1[E2] gets transformed to *((E1)+(E2)) */

	EXPRUNARY,
	EXPRCAST,
	EXPRBINARY,
	EXPRCOND,
	EXPRASSIGN,
	EXPRCOMMA,

	EXPRBUILTIN,
	EXPRTEMP,
	EXPRSIZEOF,
	EXPRSTMTEXPR,
};

struct stringlit {
	size_t size;
	void *data;
};

struct expr {
	enum exprkind kind;
	/* whether this expression is an lvalue */
	bool lvalue;
	/* whether this expression is a pointer decayed from an array or function designator */
	bool decayed;
	/* the unqualified type of the expression */
	struct type *type;
	/* the type qualifiers of the object this expression refers to (ignored for non-lvalues) */
	enum typequal qual;
	enum tokenkind op;
	struct expr *base;
	struct expr *next;
	struct expr *toeval;
	union {
		struct {
			struct decl *decl;
		} ident;
		union {
			unsigned long long u;
			long long i;
			double f;
		} constant;
		struct stringlit string;
		struct {
			struct expr *args;
			size_t nargs;
		} call;
		struct {
			struct bitfield bits;
		} bitfield;
		struct {
			struct decl *decl;
			struct init *init;
		} compound;
		struct {
			bool post;
		} incdec;
		struct {
			struct expr *l, *r;
		} binary;
		struct {
			struct expr *t, *f;
		} cond;
		struct {
			struct expr *l, *r;
		} assign;
		struct {
			enum builtinkind kind;
		} builtin;
		struct {
			struct type *type;
		} szof;
		struct value *temp;
		struct {
			struct stmt_expr_item *items;
			struct expr *last_expr;
		} stmt_expr;
	} u;
};

struct init {
	unsigned long long start, end;
	struct expr *expr;
	struct bitfield bits;
	struct init *next;
};

/* Item in a GNU statement-expression ({...}) block: a declaration with
 * optional initialiser, or an expression statement.  The list is built
 * during parsing and consumed during IR generation (funcexpr). */
struct stmt_expr_item {
	enum { STMTEXPR_DECL, STMTEXPR_EXPR } kind;
	union {
		struct {
			struct decl *decl;
			struct init *init;
			bool hasinit;
		} decl_item;
		struct expr *expr;
	} u;
	struct stmt_expr_item *next;
};

/* token */

extern struct token tok;

void tokeninit(void);
int tokenget(const void *, size_t);
void tokenprint(const struct token *, FILE *);
char *tokenstr(enum tokenkind);
char *tokencheck(const struct token *, enum tokenkind, const char *);
void diagloc(const struct location *);
noreturn void error(const struct location *, const char *, ...);
void cc_warn(const struct location *, int, const char *, ...) __attribute__((format(printf, 3, 4)));
extern int warn_level;
extern bool warn_as_error;

/* Diagnostic output mode (p9-ui): --error-json emits diagnostics as
 * structured JSON lines for tooling; --explain adds a fix-hint suffix. */
extern int g_error_json;
extern int g_error_explain;

/* Target features bitmask (MT_FEATURE_*), set by -march=native or
 * -march=x86-64-vN. 0 = baseline only.
 *
 * 位布局与 mt/as (include/mt/target.h) 保持一致，确保跨工具链（mcc / mt/as）
 * 对相同特性的位定义一致：
 *   x86:  SSE SSE2 SSE3 SSSE3 SSE4_1 SSE4_2 AVX AVX2 POPCNT ... (0..15)
 *   riscv: RV_F RV_D RV_C RV_V  (32..35) */
extern uint64_t g_target_features;

/* ARM architecture version (from -march=armvN), 0 = default (7).
 * Used by arm_emit.c to gate movw/movt (ARMv6T2+) vs literal pool loads. */
extern int g_arm_arch_ver;

/* RISC-V extension feature bits (mirrors include/mt/target.h bit layout). */
#define MT_FEATURE_RV_F  (1ULL << 32)  /* riscv F (single-precision float) */
#define MT_FEATURE_RV_D  (1ULL << 33)  /* riscv D (double-precision float) */
#define MT_FEATURE_RV_C  (1ULL << 34)  /* riscv C (compressed) */
#define MT_FEATURE_RV_V  (1ULL << 35)  /* riscv V (vector) */

/* i386 variant feature bits */
#define MT_FEATURE_I386_CMPXCHG   (1ULL << 40)  /* i486+: CMPXCHG */
#define MT_FEATURE_I386_FPU       (1ULL << 41)  /* i586+: FPU */
#define MT_FEATURE_I386_CMPXCHG8B (1ULL << 42)  /* i686+: CMPXCHG8B */

/* aarch64 extension feature bits */
#define MT_FEATURE_AARCH64_FP16   (1ULL << 48)  /* FEAT_FP16 */
#define MT_FEATURE_AARCH64_RDM    (1ULL << 49)  /* FEAT_RDM */
#define MT_FEATURE_AARCH64_JSCVT  (1ULL << 50)  /* FEAT_JSCVT */

/* scan */

void scanfrom(const char *, FILE *);
void scanopen(void);
void scansetloc(struct location loc);
void scan(struct token *);

/* current input file name, or NULL */
const char *scanfile(void);
/* tokenize a synthetic fragment (e.g. command-line -D) in isolation */
void *scanpushisolated(const char *name, FILE *f);
void scanpopisolated(void *cookie);

/* preprocessor */

enum ppflags {
	/* preserve newlines in preprocessor output */
	PPNEWLINE   = 1 << 0,
};

extern enum ppflags ppflags;

void ppinit(void);

/* command-line macro / include-path control (used by the driver) */
void ppdefine(const char *name, const char *value);
void ppundef(const char *name);
void ppincludepath(const char *dir);

/* emit a Makefile dependency rule for -M/-MM/-MD/-MMD */
void ppdumpdeps(FILE *f, const char *target, const char *input);

void next(void);
bool peek(enum tokenkind);
void tokpush(struct token *, size_t);
size_t tokctx_depth(void);
void tokctx_rewind(size_t);
char *expect(enum tokenkind, const char *);
bool consume(enum tokenkind);

/* type */

struct type *mktype(enum typekind, enum typeprop);
struct type *mkpointertype(struct type *, enum typequal);
struct type *mkarraytype(struct type *, enum typequal, unsigned long long);
struct type *mkbitinttype(int width, bool sign);
struct type *mkatomictype(struct type *, enum typequal);
struct type *mkcomplextype(struct type *);
struct type *mkimaginarytype(struct type *);
struct type *mkdecimaltype(int);

bool typecompatible(struct type *, struct type *);
bool typesame(struct type *, struct type *);
struct type *typecomposite(struct type *, struct type *);
struct type *typeunqual(struct type *, enum typequal *);
struct type *typecommonreal(struct type *, unsigned, struct type *, unsigned);
struct type *typepromote(struct type *, unsigned);
struct type *typeadjust(struct type *, enum typequal *);
enum typeprop typeprop(struct type *);
struct member *typemember(struct type *, const char *, unsigned long long *);
bool typehasint(struct type *, unsigned long long, bool);

extern struct type typevoid;
extern struct type typebool;
extern struct type typechar, typeschar, typeuchar;
extern struct type typeshort, typeushort;
extern struct type typeint, typeuint;
extern struct type typelong, typeulong;
extern struct type typellong, typeullong;
extern struct type typefloat, typedouble, typeldouble;
extern struct type typenullptr;
extern struct type *typeadjvalist;
/* C++11 `auto` placeholder type: `auto x = expr;` / `auto f() {...}`
 * deduce the real type from the initializer / return statement. */
extern struct type typeauto;

/* targ */

struct target {
	const char *name;
	struct type *typevalist;
	struct type *typewchar;
	int signedchar;
};

extern const struct target *targ;

void targinit(const char *);

/* attr */

enum attrkind {
	ATTRNORETURN    = 1<<0,
	ATTRFALLTHROUGH = 1<<1,

	/* GNU attributes */
	ATTRALIGNED     = 1<<2,
	ATTRCONSTRUCTOR = 1<<3,
	ATTRDESTRUCTOR  = 1<<4,
	ATTRPACKED      = 1<<5,
	ATTRSECTION     = 1<<6,
	ATTRNODISCARD   = 1<<7,
	ATTRMAYBEUNUSED = 1<<8,
	ATTRDEPRECATED  = 1<<9,
	ATTRWEAK        = 1<<10,
	ATTRUSED        = 1<<11,
	ATTRNOINLINE    = 1<<12,
	ATTRALWAYSINLINE = 1<<13,
};

struct attr {
	enum attrkind kind;
	int align;
	char *section;
};

bool attr(struct attr *, enum attrkind);
bool gnuattr(struct attr *, enum attrkind);

/* decl */

struct decl *mkdecl(char *name, enum declkind, struct type *, enum typequal, enum linkage);
bool decl(struct scope *, struct func *);
struct type *typename(struct scope *, enum typequal *, struct expr **);

struct decl *stringdecl(struct expr *);

void emittentativedefns(void);

/* scope */

void scopeinit(void);
struct scope *mkscope(struct scope *);
struct scope *delscope(struct scope *);

void scopeputdecl(struct scope *, struct decl *);
struct decl *scopegetdecl(struct scope *, const char *, bool);

void scopeputtag(struct scope *, const char *, struct type *);
struct type *scopegettag(struct scope *, const char *, bool);

extern struct scope filescope;

/* Trial-parse support (SFINAE-style well-formedness checks, used by the
 * C++ requires-expression evaluator): error() longjmps to the innermost
 * active trial instead of aborting.  cpp_trial_begin/end save/restore the
 * enclosing trial's jump buffer so nested trials rethrow on error.
 * cpp_trial_guard rewinds the token context to `depth` and pushes a
 * bounded guard buffer so a caller resuming after a trial never falls
 * through to source scanning. */
void cpp_trial_begin(jmp_buf env);
void cpp_trial_end(jmp_buf env);
void cpp_trial_rethrow(void);
int cpp_trial_depth(void);
void cpp_trial_guard(size_t depth);

/* expr */

struct type *stringconcat(struct stringlit *, bool);

struct expr *expr(struct scope *);
struct expr *assignexpr(struct scope *);
struct expr *condexpr(struct scope *);
unsigned long long intconstexpr(struct scope *, bool);
void delexpr(struct expr *);
struct expr *expr_dup(struct expr *);

struct expr *exprassign(struct expr *, struct type *);
struct expr *exprpromote(struct expr *);

/* eval */

struct expr *eval(struct expr *);

/* init */

struct init *mkinit(unsigned long long, unsigned long long, struct bitfield, struct expr *);
struct init *parseinit(struct scope *, struct type *);

/* statement-expression parsing — returns an EXPRSTMTEXPR node */
struct expr *parse_stmt_expr_body(struct scope *);

/* stmt */

void stmt(struct func *, struct scope *);

/* current function context, set by stmt() for statement expression
 * ({...}) parsing — enables parse_stmt_expr_body to call stmt() for
 * control-flow constructs (if/while/for/...) inside the body. */
extern struct func *curfunc;

/* backend */

struct gotolabel {
	struct block *label;
	bool defined;
};

struct switchcases {
	void *root;
	struct type *type;
	struct block *defaultlabel;
};

void switchcase(struct switchcases *, unsigned long long, struct block *);

struct block *mkblock(char *);

struct value *mkglobal(struct decl *);

struct value *mkintconst(unsigned long long);

struct func *mkfunc(struct decl *, char *, struct type *, struct scope *);
void delfunc(struct func *);
struct type *functype(struct func *);
void funclabel(struct func *, struct block *);
struct value *funcbranch(struct func *, struct expr *, struct block *, struct block *);
struct value *funcexpr(struct func *, struct expr *);
void funcjmp(struct func *, struct block *);
void funcjnz(struct func *, struct value *, struct type *, struct block *, struct block *);
void funcret(struct func *, struct value *);
void funchlt(struct func *);
struct gotolabel *funcgoto(struct func *, char *);
bool func_falls_off_end(struct func *);
void funcswitch(struct func *, struct value *, struct switchcases *, struct block *);
void funcinit(struct func *, struct decl *, struct init *, bool);

void emitfunc(struct func *, bool);
void emitdata(struct decl *, struct init *);
