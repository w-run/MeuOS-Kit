#ifndef MCC_PARSE_DECL_INTERNAL_H
#define MCC_PARSE_DECL_INTERNAL_H

/* Internal definitions shared by the split src/parse/decl/*.c files.
 *
 * The original src/parse/decl.c contained the entire C declaration
 * grammar plus the small enum and struct types used internally by the
 * spec/qual/struct parsers. To keep each split file self-contained we
 * lift those type definitions into this header; the implementations
 * themselves are split by responsibility:
 *
 *   specs.c        storageclass, typequal, funcspec, tagspec, declspecs,
 *                  typename, istypename
 *   declarator.c   declarator, declaratortypes, parameter
 *   struct_decl.c  addmember, staticassert, structdecl
 *   decl.c         decl(), declcommon, defineobj, mkdecl, stringdecl,
 *                  emittentativedefns (the entry points)
 *
 * The cross-file forward decls let specs.c/declarator.c/struct_decl.c
 * call each other's functions without exposing them globally to mcc.c. */

#include <stdbool.h>

/* Internal types shared by decl.c, specs.c, declarator.c, struct_decl.c. */
struct qualtype {
	struct type *type;
	enum typequal qual;
	struct expr *expr;
	enum attrkind kind;   /* attributes gathered by declspecs()/declarator() */
};

enum storageclass {
	SCNONE,
	SCTYPEDEF     = 1<<1,
	SCEXTERN      = 1<<2,
	SCSTATIC      = 1<<3,
	SCAUTO        = 1<<4,
	SCREGISTER    = 1<<5,
	SCTHREADLOCAL = 1<<6,
};

enum typespec {
	SPECNONE,
	SPECVOID     = 1<<1,
	SPECCHAR     = 1<<2,
	SPECBOOL     = 1<<3,
	SPECINT      = 1<<4,
	SPECFLOAT    = 1<<5,
	SPECDOUBLE   = 1<<6,
	SPECSHORT    = 1<<7,
	SPECLONG     = 1<<8,
	SPECLONG2    = 1<<9,
	SPECSIGNED   = 1<<10,
	SPECUNSIGNED = 1<<11,
	SPECCOMPLEX  = 1<<12,
	SPECBITINT   = 1<<13,
	SPECIMAGINARY = 1<<14,
	SPECLONGLONG = SPECLONG|SPECLONG2,
};

enum funcspec {
	FUNCNONE,
	FUNCINLINE   = 1<<1,
	FUNCNORETURN = 1<<2,
	FUNCCONSTEXPR = 1<<3,   /* C++ constexpr function */
	FUNCCONSTEVAL = 1<<4,   /* C++20 consteval (immediate) function */
};

struct structbuilder {
	struct type *type;
	struct member **last;
	unsigned bits;
	bool pack;
	/* current C++ access level for members added from here on
	 * (ACC_PUBLIC in C mode; class defaults to ACC_PRIVATE) */
	int access;
	/* current member is C++ `mutable` (writable via const this) */
	bool member_mutable;
	/* current member is C++ `virtual` (dispatched via vtable) */
	bool member_virtual;
	/* current member is a C++ const member function */
	bool member_const;
};

/* Tentative-definition list: tracked in decl.c but drained by
 * emittentativedefns(). */
extern struct decl *tentativedefns;
extern struct decl **tentativedefnsend;

/* Forward declarations for functions defined across the split files. */
struct qualtype declspecs(struct scope *, enum storageclass *,
    enum funcspec *, int *);
struct qualtype declarator(struct scope *, struct qualtype,
    char **, int *, struct scope **, bool, struct attr *);
struct type *typename(struct scope *, enum typequal *, struct expr **);
struct type *tagspec(struct scope *);
int typequal(enum typequal *);
int storageclass(enum storageclass *);
int funcspec(enum funcspec *);
bool istypename(struct scope *, const char *);
bool staticassert(struct scope *);
void structdecl(struct scope *, struct structbuilder *);
void addmember(struct structbuilder *, struct qualtype, char *,
    int, unsigned long long);
struct decl *parameter(struct scope *);

#endif
