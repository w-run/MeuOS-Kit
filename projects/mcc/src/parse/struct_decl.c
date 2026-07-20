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
	if (mt.type->kind == TYPEFUNC)
		error(&tok.loc, "struct member '%s' has function type", name);
	if (mt.type->prop & PROPVM)
		error(&tok.loc, "struct member '%s' has variably modified type", name);
	assert(mt.type->align > 0);
	if (name || width == -1) {
		m = xmalloc(sizeof(*m));
		m->type = mt.type;
		m->qual = mt.qual;
		m->name = name;
		m->next = NULL;
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

	if (staticassert(s))
		return;
	attr(NULL, 0);
	base = declspecs(s, NULL, NULL, &align);
	if (!base.type)
		error(&tok.loc, "no type in struct member declaration");
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
			addmember(b, mt, name, align, width);
		}
		if (tok.kind == TSEMICOLON)
			break;
		expect(TCOMMA, "or ';' after declarator");
	}
	next();
}
