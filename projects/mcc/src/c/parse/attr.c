#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include "util.h"
#include "mcc.h"

enum attrprefix {
	PREFIXNONE = 1,  /* standard attribute */
	PREFIXGNU,
};

static const char *
strip(const char *name, char *buf, size_t buflen)
{
	size_t len;

	len = strlen(name);
	if (len >= 4 && name[0] == '_' && name[1] == '_' && name[len - 2] == '_' && name[len - 1] == '_') {
		if (len - 4 >= buflen)
			return "";
		memcpy(buf, name + 2, len - 4);
		buf[len - 4] = '\0';
		return buf;
	}
	return name;
}

static bool
parseattr(struct attr *a, enum attrkind allowed, enum attrprefix prefix)
{
	const char *name, *prefixname = "";
	char namebuf[32], *section;
	enum attrkind kind;
	int paren;

	/* `_Noreturn` lexes as the reserved T_NORETURN token, not TIDENT;
	 * accept it as an attribute name (C23 makes `_Noreturn` equivalent
	 * to `[[noreturn]]`). */
	if (tok.kind < TIDENT && tok.kind != T_NORETURN)
		return false;
	name = strip(tokenstr(tok.kind), namebuf, sizeof namebuf);
	next();
	if (!prefix) {
		if (consume(TCOLONCOLON)) {
			if (strcmp(name, "gnu") == 0)
				prefix = PREFIXGNU;
			name = strip(expect(TIDENT, "after attribute prefix"), namebuf, sizeof namebuf);
		} else {
			prefix = PREFIXNONE;
		}
	}
	kind = 0;
	switch (prefix) {
	case PREFIXNONE:
		if (strcmp(name, "noreturn") == 0 || strcmp(name, "_Noreturn") == 0)
			kind = ATTRNORETURN;
		else if (strcmp(name, "fallthrough") == 0)
			kind = ATTRFALLTHROUGH;
		else if (strcmp(name, "nodiscard") == 0) {
			kind = ATTRNODISCARD;
			if (consume(TLPAREN))
				expect(TRPAREN, "after nodiscard");
		} else if (strcmp(name, "maybe_unused") == 0)
			kind = ATTRMAYBEUNUSED;
		else if (strcmp(name, "assume") == 0) {
			/* P1774: `[[assume(expr)]]` — the expression is unevaluated
			 * and the attribute is a no-op here, but the parenthesized
			 * argument form is required (and its balanced token list is
			 * consumed by the generic skip below). */
			if (tok.kind != TLPAREN)
				error_code(E_DECL, &tok.loc,
				    "'assume' attribute requires a parenthesized expression");
		} else if (strcmp(name, "deprecated") == 0) {
			kind = ATTRDEPRECATED;
			if (consume(TLPAREN)) {
				if (tok.kind == TSTRINGLIT)
					next();
				expect(TRPAREN, "after deprecated message");
			}
		}
		break;
	case PREFIXGNU:
		prefixname = "GNU ";
		if (strcmp(name, "aligned") == 0) {
			kind = ATTRALIGNED;
			if (consume(TLPAREN)) {
				unsigned long long i;

				i = intconstexpr(&filescope, false);
				if (!i || i & i - 1 || i > INT_MAX)
					error_code(E_DECL, &tok.loc, "invalid alignment %llu", i);
				if (a)
					a->align = i;
				expect(TRPAREN, "after alignment");
			} else {
				if (a)
					a->align = 16;
			}
		} else if (strcmp(name, "constructor") == 0) {
			kind = ATTRCONSTRUCTOR;
		} else if (strcmp(name, "destructor") == 0) {
			kind = ATTRDESTRUCTOR;
		} else if (strcmp(name, "packed") == 0) {
			kind = ATTRPACKED;
		} else if (strcmp(name, "section") == 0) {
			kind = ATTRSECTION;
			expect(TLPAREN, "after 'section'");
			section = expect(TSTRINGLIT, "for section name");
			if (a)
				a->section = section;
			expect(TRPAREN, "after section name");
		} else if (strcmp(name, "weak") == 0) {
			kind = ATTRWEAK;
		} else if (strcmp(name, "used") == 0) {
			kind = ATTRUSED;
		} else if (strcmp(name, "unused") == 0) {
			/* silently accept unused */
		} else if (strcmp(name, "noinline") == 0) {
			kind = ATTRNOINLINE;
		} else if (strcmp(name, "always_inline") == 0) {
			kind = ATTRALWAYSINLINE;
		} else if (strcmp(name, "noreturn") == 0) {
			/* GNU `__attribute__((noreturn))` / `((__noreturn__))`
			 * (the double-underscore form is stripped to `noreturn`
			 * by strip() above).  Mirrors the PREFIXNONE handling. */
			kind = ATTRNORETURN;
		} else if (strcmp(name, "visibility") == 0) {
			/* silently accept visibility("...") */
			if (consume(TLPAREN)) {
				if (tok.kind == TSTRINGLIT) next();
				expect(TRPAREN, "after visibility argument");
			}
		}
		break;
	}
	if (kind) {
		if (!(kind & allowed))
			error_code(E_DECL, &tok.loc, "%sattribute '%s' is not supported here", prefixname, name);
		if (a)
			a->kind |= kind;
	} else if (consume(TLPAREN)) {
		/* skip arguments */
		for (paren = 1; paren > 0; next()) {
			switch (tok.kind) {
			case TLPAREN: ++paren; break;
			case TRPAREN: --paren; break;
			}
		}
	}
	return true;
}

static bool
attrspec(struct attr *a, enum attrkind allowed)
{
	if (tok.kind != TLBRACK || !peek(TLBRACK))
		return false;
	while (parseattr(a, allowed, 0) || consume(TCOMMA))
		;
	expect(TRBRACK, "to end attribute specifier");
	expect(TRBRACK, "to end attribute specifier");
	return true;
}

bool
attr(struct attr *a, enum attrkind allowed)
{
	if (!attrspec(a, allowed))
		return false;
	while (attrspec(a, allowed))
		;
	return true;
}

static bool
gnuattrspec(struct attr *a, enum attrkind allowed)
{
	if (!consume(T__ATTRIBUTE__))
		return false;
	while (parseattr(a, allowed, PREFIXGNU) || consume(TCOMMA))
		;
	expect(TLPAREN, "after '__attribute__' to begin attribute specifier");
	expect(TLPAREN, "after '__attribute__' to begin attribute specifier");
	while (parseattr(a, allowed, PREFIXGNU) || consume(TCOMMA))
		;
	expect(TRPAREN, "to end attribute specifier");
	expect(TRPAREN, "to end attribute specifier");
	return true;
}

bool
gnuattr(struct attr *a, enum attrkind allowed)
{
	if (!gnuattrspec(a, allowed))
		return false;
	while (gnuattrspec(a, allowed))
		;
	return true;
}
