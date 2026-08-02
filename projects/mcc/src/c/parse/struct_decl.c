/* parse/struct_decl.c -- struct/union/enum member declarations.
 *
 * Implements the struct/union member grammar:
 *   addmember()      -> register one member in a struct/union being built
 *   staticassert()   -> _Static_assert declarations
 *   structdecl()     -> struct-or-union-specifier parser
 *
 * addmember() is also used during enums (anonymous-bit-field handling).
 * structdecl() is called from declspecs() (in specs.c) when a tag is
 * parsed and no prior definition exists in the scope. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "decl_internal.h"
#include "cpp/cpp_tokens.h"

void
addmember(struct structbuilder *b, struct qualtype mt, char *name, int align, unsigned long long width)
{
	struct type *t = b->type;
	struct member *m;
	size_t end;

	if (t->kind == TYPESTRUCT && t->prop & PROPFLEX)
		error(&tok.loc, "struct has member '%s' after flexible array member", name);
	if (mt.type->incomplete) {
		if (mt.type->kind != TYPEARRAY)
			error(&tok.loc, "struct member '%s' has incomplete type", name);
		t->prop |= PROPFLEX;
	}
	if (mt.type->prop & PROPFLEX) {
		/*
		ISO C doesn't allow structures with members containing
		FAMs, but allow it as an extension
		*/
		t->prop |= PROPFLEX;
	}
	if (mt.type->kind == TYPEFUNC) {
		/* C++ member function: register it in the member list (so member
		 * calls can be lowered to `Class_name(&obj, ...)`), then consume
		 * the body (lowered to an out-of-line function by the C++ frontend
		 * via structdecl's TYPEFUNC path).  In C++ mode we add the member
		 * and return; the body itself is consumed by structdecl. */
		extern int g_lang;
		if (g_lang == 1) {
			/* register the function-typed member for call lowering */
			if (name) {
				struct member *mf = xmalloc(sizeof(*mf));
				mf->type = mt.type;
				mf->qual = mt.qual;
				mf->name = name;
				mf->next = NULL;
				mf->offset = 0;
				mf->bits.before = mf->bits.after = 0;
				mf->access = b->access;
				mf->is_mutable = false;
				mf->is_virtual = b->member_virtual;
				mf->is_const = b->member_const;
				mf->vslot = -1;
				if (b->last)
					*b->last = mf;
				else
					b->type->u.structunion.members = mf;
				b->last = &mf->next;
			}
			/* the body is consumed by structdecl's TYPEFUNC branch */
			return;
		}
		error(&tok.loc, "struct member '%s' has function type", name);
	}
	if (mt.type->prop & PROPVM)
		error(&tok.loc, "struct member '%s' has variably modified type", name);
	assert(mt.type->align > 0);
	if (name || width == -1) {
		m = xmalloc(sizeof(*m));
		m->type = mt.type;
		m->qual = mt.qual;
		m->name = name;
		m->next = NULL;
		m->access = b->access;
		m->is_mutable = b->member_mutable;
		m->is_virtual = b->member_virtual;
		m->is_const = b->member_const;
		m->vslot = -1;
		*b->last = m;
		b->last = &m->next;
	} else {
		m = NULL;
	}
	if (width == -1) {
		m->bits.before = 0;
		m->bits.after = 0;
		if (align < mt.type->align) {
			if (align)
				error(&tok.loc, "specified alignment of struct member '%s' is less strict than is required by type", name);
			align = b->pack ? 1 : mt.type->align;
		}
		if (t->kind == TYPESTRUCT) {
			m->offset = ALIGNUP(t->size, align);
			t->size = m->offset + mt.type->size;
		} else {
			m->offset = 0;
			if (t->size < mt.type->size)
				t->size = mt.type->size;
		}
		b->bits = 0;
	} else {  /* bit-field */
		if (!(mt.type->prop & PROPINT))
			error(&tok.loc, "bit-field '%s' has invalid type", name);
		if (align)
			error(&tok.loc, "alignment specified for bit-field '%s'", name);
		if (b->pack)
			error(&tok.loc, "bit-field '%s' in packed struct is not supported", name);
		if (!width && name)
			error(&tok.loc, "bit-field '%s' with zero width must not have declarator", name);
		if (width > mt.type->u.arith.width)
			error(&tok.loc, "bit-field '%s' exceeds width of underlying type", name);
		align = mt.type->align;
		if (t->kind == TYPESTRUCT) {
			/* calculate end of the storage-unit for this bit-field */
			end = ALIGNUP(t->size, mt.type->size);
			if (!width || width > (end - t->size) * 8 + b->bits) {
				/* no room, allocate a new storage-unit */
				t->size = end;
				b->bits = 0;
			}
			if (m) {
				m->offset = ALIGNDOWN(t->size - !!b->bits, mt.type->size);
				m->bits.before = (t->size - m->offset) * 8 - b->bits;
				m->bits.after = mt.type->size * 8 - width - m->bits.before;
			}
			t->size += (width - b->bits + 7) / 8;
			b->bits = (b->bits - width) % 8;
		} else if (m) {
			m->offset = 0;
			m->bits.before = 0;
			m->bits.after = mt.type->size * 8 - width;
			if (t->size < mt.type->size)
				t->size = mt.type->size;
		}
	}
	if (m && t->align < align)
		t->align = align;
}
bool
staticassert(struct scope *s)
{
	struct stringlit msg;
	unsigned long long c;

	if (!consume(TSTATIC_ASSERT))
		return false;
	expect(TLPAREN, "after static_assert");
	c = intconstexpr(s, true);
	if (consume(TCOMMA)) {
		tokencheck(&tok, TSTRINGLIT, "after static assertion expression");
		stringconcat(&msg, true);
		if (!c)
			error(&tok.loc, "static assertion failed: %.*s", (int)(msg.size - 1), (char *)msg.data);
	} else if (!c) {
		error(&tok.loc, "static assertion failed");
	}
	expect(TRPAREN, "after static assertion");
	expect(TSEMICOLON, "after static assertion");
	return true;
}
void
structdecl(struct scope *s, struct structbuilder *b)
{
	struct qualtype base, mt;
	char *name;
	unsigned long long width;
	int align;
	enum storageclass sc;
	enum funcspec fs;

	extern enum cpp_tokenkind cpp_tok_kind(void);
	if (staticassert(s))
		return;
	attr(NULL, 0);
	b->member_mutable = false;
	b->member_virtual = false;
	b->member_const = false;
	/* C++ `mutable` storage: writable via const this. */
	extern int g_lang;
	if (g_lang == 1 && cpp_tok_kind() == CPP_TMUTABLE) {
		b->member_mutable = true;
		next();
	}
	/* C++ `virtual` member function: dispatched via the object's vtable. */
	if (g_lang == 1 && cpp_tok_kind() == CPP_TVIRTUAL) {
		b->member_virtual = true;
		next();
	}
	/* C++ destructor: `~Class() { ... }`.  Detect it before declspecs
	 * (which does not understand '~').  Lowered to `Class_dtor` via
	 * cpp_define_method; the member is registered under a `~Class` marker
	 * name so cpp_has_dtor can find it without colliding with a user
	 * method literally named `dtor`. */
	extern int g_lang;
	if (g_lang == 1 && tok.kind == TBNOT) {
		const char *tag = b->type->u.structunion.tag;
		next(); /* consume '~' */
		if (tok.kind >= TIDENT && tag &&
		    strcmp(tokenstr(tok.kind), tag) == 0) {
			struct type *ct;
			char *marker;
			next(); /* consume the class name */
			expect(TLPAREN, "after destructor name");
			ct = mktype(TYPEFUNC, 0);
			ct->qual = QUALNONE;
			ct->base = &typevoid;
			ct->u.func.isvararg = false;
			ct->u.func.params = NULL;
			ct->u.func.nparam = 0;
			expect(TRPAREN, "after destructor parameters");
			marker = xmalloc(strlen(tag) + 2);
			sprintf(marker, "~%s", tag);
			mt.type = ct;
			mt.qual = QUALNONE;
			mt.expr = NULL;
			name = marker;
			width = -1;
			{
				extern void cpp_define_method(struct scope *,
				    struct type *, const char *, const char *, bool, bool, bool);
				cpp_define_method(s, ct, "dtor", tag, false, false, false);
			}
			addmember(b, mt, name, align, width);
			return;
		}
		error(&tok.loc, "expected class name after '~'");
	}
	base = declspecs(s, &sc, &fs, &align);
	if (!base.type)
		error(&tok.loc, "no type in struct member declaration");
	/* C++ constructor: `ClassName(...) { ... }`.  declspecs parsed the
	 * class tag as a bare type name; a following '(' means it was really
	 * the constructor name (which has no return type).  Parse the
	 * parameter list by hand — declarator() cannot because the name was
	 * consumed — and define it as `Class_Class` like any other method. */
	extern int g_lang;
	if (g_lang == 1 && tok.kind == TLPAREN &&
	    base.type->kind == TYPESTRUCT && base.type->u.structunion.tag &&
	    b->type->u.structunion.tag &&
	    strcmp(base.type->u.structunion.tag, b->type->u.structunion.tag) == 0) {
		struct type *ct;
		struct decl *pd, **pend;
		const char *tag = base.type->u.structunion.tag;

		ct = mktype(TYPEFUNC, 0);
		ct->qual = QUALNONE;
		ct->base = &typevoid; /* constructor has no return type */
		ct->u.func.isvararg = false;
		ct->u.func.params = NULL;
		ct->u.func.nparam = 0;
		pend = &ct->u.func.params;
		next(); /* consume '(' */
		while (tok.kind != TRPAREN) {
			pd = parameter(s);
			*pend = pd;
			pend = &pd->next;
			++ct->u.func.nparam;
			if (tok.kind == TRPAREN)
				break;
			expect(TCOMMA, "or ')' after constructor parameter");
		}
		next(); /* consume ')' */
		mt.type = ct;
		mt.qual = QUALNONE;
		mt.expr = NULL;
		name = (char *)tag;
		width = -1;
		{
			extern void cpp_define_method(struct scope *,
			    struct type *, const char *, const char *, bool, bool, bool);
			cpp_define_method(s, ct, tag, tag, false, false, false);
		}
		addmember(b, mt, name, align, width);
		return;
	}
	/* C++ operator overload: `Vec operator+(const Vec &o) {...}`.  The
	 * return type was parsed by declspecs; parse the operator token and
	 * parameter list by hand and lower it to `Class_operator_pl`. */
	extern int g_lang;
	if (g_lang == 1 && cpp_tok_kind() == CPP_TOPERATOR) {
		extern const char *cpp_op_mangle(enum tokenkind);
		extern void cpp_define_method(struct scope *, struct type *,
		    const char *, const char *, bool, bool, bool);
		const char *opcode;
		struct type *ft;
		struct decl *pd, **pend;
		char *mname;
		bool is_const = false;

		next(); /* consume 'operator' */
		opcode = cpp_op_mangle(tok.kind);
		if (!opcode)
			error(&tok.loc, "unsupported operator for overloading");
		next(); /* consume the operator token */
		/* operator(): the closing ')' of the operator token follows
		 * (`operator()`, not `operator ( ...`); the next '(' is the
		 * parameter list. */
		if (strcmp(opcode, "cl") == 0)
			expect(TRPAREN, "after 'operator()'");

		ft = mktype(TYPEFUNC, 0);
		ft->qual = QUALNONE;
		ft->base = base.type; /* return type */
		ft->u.func.isvararg = false;
		ft->u.func.params = NULL;
		ft->u.func.nparam = 0;
		pend = &ft->u.func.params;
		if (tok.kind == TLPAREN) {
			next();
			while (tok.kind != TRPAREN) {
				pd = parameter(s);
				*pend = pd;
				pend = &pd->next;
				++ft->u.func.nparam;
				if (tok.kind == TRPAREN)
					break;
				expect(TCOMMA, "or ')' after operator parameter");
			}
			next(); /* consume ')' */
		}
		if (tok.kind == TCONST) {
			is_const = true;
			b->member_const = true;
			next();
		}
		mname = xmalloc(strlen(opcode) + 10);
		sprintf(mname, "operator_%s", opcode);
		cpp_define_method(s, ft, mname, b->type->u.structunion.tag,
		                  is_const, false, false);
		addmember(b, (struct qualtype){ft, QUALNONE, NULL}, mname, 0, -1);
		return;
	}
	if (tok.kind == TSEMICOLON) {
		if ((base.type->kind != TYPESTRUCT && base.type->kind != TYPEUNION) || base.type->u.structunion.tag)
			error(&tok.loc, "struct declaration must declare at least one member");
		next();
		addmember(b, base, NULL, align, -1);
		return;
	}
	for (;;) {
		if (consume(TCOLON)) {
			width = intconstexpr(s, false);
			addmember(b, base, NULL, 0, width);
		} else {
			mt = declarator(s, base, &name, &align, NULL, false);
			width = consume(TCOLON) ? intconstexpr(s, false) : -1;
			/* C++ member function: define it as an out-of-line free
			 * function `ClassName_method` (this-pointer lowering and
			 * in-body member access are handled by the C++ frontend).
			 * In C++ mode we consume the body via cpp_define_method and
			 * end the member. */
			extern int g_lang;
			if (g_lang == 1 && mt.type->kind == TYPEFUNC) {
				extern void cpp_define_method(struct scope *,
				    struct type *, const char *, const char *, bool, bool, bool);
				/* const member function: `void get() const {...}` */
				bool is_const = false;
				if (tok.kind == TCONST) {
					is_const = true;
					b->member_const = true;
					next();
				}
				bool is_static = (sc & SCSTATIC) != 0;
				/* class tag for name mangling (Class_method) */
				extern bool g_cpp_define_virtual;
				g_cpp_define_virtual = false;
				cpp_define_method(s, mt.type, name,
				    b->type->u.structunion.tag, is_const, is_static,
				    b->member_virtual);
				/* an override of a base virtual is virtual even without
				 * the keyword: propagate so addmember flags the member */
				if (g_cpp_define_virtual)
					b->member_virtual = true;
				/* register the function member for call lowering */
				addmember(b, mt, name, align, width);
				/* leave the following token (class-body '}' or next
				 * member) unconsumed for the caller's loop */
				return;
			}
			/* C++ static data member: declare `Class_member` as a file
			 * symbol; it occupies no per-object storage. */
			extern int g_lang;
			if (g_lang == 1 && (sc & SCSTATIC) && name) {
				struct decl *sd;
				char mangled[256];
				char *pm;
				struct scope *cs = b->type->scope ? b->type->scope : s;
				snprintf(mangled, sizeof mangled, "%s_%s",
				    b->type->u.structunion.tag, name);
				pm = xmalloc(strlen(mangled) + 1);
				strcpy(pm, mangled);
				sd = mkdecl(pm, DECLOBJECT, mt.type, mt.qual, LINKEXTERN);
				sd->u.obj.storage = SDSTATIC;
				sd->value = mkglobal(sd); /* symbol slot exists before
				                             the out-of-line definition */
				scopeputdecl(cs, sd);
				/* in-class initializer: `static int count = 5;` */
				if (tok.kind == TASSIGN) {
					struct init *init;
					next();
					init = parseinit(s, mt.type);
					emitdata(sd, init);
					sd->defined = true;
					expect(TSEMICOLON, "after static member initializer");
					return;
				}
				/* C++17 inline static member without an initializer is a
				 * definition: emit a zero-initialized object (unlike a
				 * plain `static` member, which is only a declaration). */
				if ((fs & FUNCINLINE) && mt.type->kind != TYPEFUNC) {
					emitdata(sd, NULL);
					sd->defined = true;
					expect(TSEMICOLON, "after inline static member");
					return;
				}
				/* skip addmember: no layout space */
			} else {
				addmember(b, mt, name, align, width);
			}
		}
		if (tok.kind == TSEMICOLON)
			break;
		expect(TCOMMA, "or ';' after declarator");
	}
	next();
}
