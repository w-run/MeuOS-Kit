/* wordexp.c — POSIX.1-2008 word expansion (wordexp/wordfree).
 *
 * Shell word expansion: splits the input on unquoted whitespace, expands
 * $VAR (via getenv) , ~ (HOME), collapses adjacent pieces, and -- when a
 * field contains glob metacharacters and glob() is available -- expands it.
 * Command substitution ($(cmd) / `cmd`) is not performed: it returns
 * WRDE_CMDSUB (the caller may pass WRDE_NOCMD to suppress, but we do not
 * execute commands at all -- documented minimal subset).  Zero GNU
 * dependency; wordexp is POSIX.1-2008 in core libc. */

#include <wordexp.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>

/* A single word being assembled from possibly several quoted/unquoted
 * segments. */
struct field {
	char *data;   /* assembled bytes */
	size_t len;
	size_t cap;
};

static int
f_ensure(struct field *f, size_t extra)
{
	if (f->len + extra + 1 > f->cap) {
		size_t ncap = f->cap ? f->cap * 2 : 32;
		while (ncap < f->len + extra + 1)
			ncap *= 2;
		char *nd = realloc(f->data, ncap);
		if (!nd)
			return WRDE_NOSPACE;
		f->data = nd;
		f->cap = ncap;
	}
	return WRDE_SUCCESS;
}

static int
f_addc(struct field *f, char c)
{
	int e = f_ensure(f, 1);
	if (e)
		return e;
	f->data[f->len++] = c;
	f->data[f->len] = 0;
	return WRDE_SUCCESS;
}

static int
f_adds(struct field *f, const char *s)
{
	size_t n = strlen(s);
	int e = f_ensure(f, n);
	if (e)
		return e;
	memcpy(f->data + f->len, s, n);
	f->len += n;
	f->data[f->len] = 0;
	return WRDE_SUCCESS;
}

/* is this a glob metacharacter in shell terms? */
static int
has_globmeta(const char *s)
{
	for (; *s; s++)
		if (*s == '*' || *s == '?' || *s == '[' || *s == '\\')
			return 1;
	return 0;
}

/* Expand ${VAR}, $VAR and ~ at the start of a field; append the result.
 * Returns a WRDE_* error, or WRDE_SUCCESS. */
static int
expanse(struct field *f, const char **inp, int flags)
{
	const char *s = *inp;
	int e = WRDE_SUCCESS;

	if (*s == '~') {
		/* home dir: ~ or ~/... (~user is not supported: minimal) */
		const char *home = getenv("HOME");
		if (!home || !home[0]) {
			home = "/";
		}
		e = f_adds(f, home);
		if (e)
			return e;
		s++;
		/* expect '/' or end */
		if (*s && *s != '/') {
			/* "~user": not supported, treat literally */
		} else {
			*inp = s;
			return WRDE_SUCCESS;
		}
	}

	if (*s == '$') {
		s++;
		if (*s == '(') {
			/* command substitution $(...): not performed -> WRDE_CMDSUB */
			return WRDE_CMDSUB;
		}
		if (*s == '{') {
			/* ${VAR} or ${VAR:-default} */
			const char *end = strchr(s, '}');
			if (!end)
				return WRDE_SYNTAX;
			size_t vlen = end - (s + 1);
			char *vn = malloc(vlen + 1);
			if (!vn)
				return WRDE_NOSPACE;
			memcpy(vn, s + 1, vlen);
			vn[vlen] = 0;
			const char *val = getenv(vn);
			/* strip :default handling: only take up to ':-' or ':' */
			char *colon = strchr(vn, ':');
			if (colon) {
				/* ${VAR:-DEF} -> if VAR empty use DEF (minimal: DEF literal) */
				*colon = 0;
				val = getenv(vn);
				if (!val || !val[0]) {
					const char *def = colon + 1;
					if (*def == '-')
						def++;
					free(vn);
					e = f_adds(f, def);
					*inp = end + 1;
					return e;
				}
			}
			free(vn);
			if (!val) {
				if (flags & WRDE_UNDEF)
					return WRDE_BADVAL;
				val = "";
			}
			e = f_adds(f, val);
			*inp = end + 1;
			return e;
		} else if (isalpha((unsigned char)*s) || *s == '_') {
			/* $VAR */
			const char *vstart = s;
			while (isalnum((unsigned char)*s) || *s == '_')
				s++;
			size_t vlen = (size_t)(s - vstart);
			char *vn = malloc(vlen + 1);
			if (!vn)
				return WRDE_NOSPACE;
			memcpy(vn, vstart, vlen);
			vn[vlen] = 0;
			const char *val = getenv(vn);
			free(vn);
			if (!val) {
				if (flags & WRDE_UNDEF)
					return WRDE_BADVAL;
				val = "";
			}
			e = f_adds(f, val);
			*inp = s;
			return e;
		} else {
			/* $ followed by non-name: literal '$' */
			e = f_addc(f, '$');
			*inp = s;
			return e;
		}
	}

	*inp = s;
	return WRDE_SUCCESS;
}

/* out is the growing field pointer.  Parse one shell word. */
static int
parse_word(const char **inp, struct field *f, int flags)
{
	int e = WRDE_SUCCESS;
	const char *s = *inp;

	while (*s) {
		char c = *s;
		if (c == '\'' ) {
			/* single-quoted literal */
			s++;
			while (*s && *s != '\'') {
				e = f_addc(f, *s++);
				if (e) return e;
			}
			if (*s != '\'')
				return WRDE_SYNTAX;
			s++;
		} else if (c == '"') {
			/* double-quoted: literal except $ */
			s++;
			while (*s && *s != '"') {
				if (*s == '\\' && s[1]) {
					e = f_addc(f, s[1]); s += 2;
				} else if (*s == '$') {
					e = expanse(f, &s, flags);
					if (e) return e;
					continue;
				} else {
					e = f_addc(f, *s++);
				}
				if (e) return e;
			}
			if (*s != '"')
				return WRDE_SYNTAX;
			s++;
		} else if (c == '\\') {
			/* backslash escape (outside quotes) */
			if (s[1]) {
				e = f_addc(f, s[1]);
				s += 2;
			} else {
				e = f_addc(f, '\\');
				s++;
			}
			if (e) return e;
		} else if (c == '$') {
			e = expanse(f, &s, flags);
			if (e) return e;
		} else if (c == '`' || (c == '(' && s[1] == '(')) {
			/* command substitution: not supported */
			return WRDE_CMDSUB;
		} else if (c == '~' && f->len == 0) {
			/* ~ at the start of a word expands to the home directory */
			e = expanse(f, &s, flags);
			if (e) return e;
		} else if (isspace((unsigned char)c)) {
			/* end of word */
			*inp = s;
			return WRDE_SUCCESS;
		} else {
			e = f_addc(f, c);
			s++;
			if (e) return e;
		}
	}
	*inp = s;
	return WRDE_SUCCESS;
}

int
wordexp(const char *words, wordexp_t *pw, int flags)
{
	const char *s = words;
	int e;
	size_t count = 0;
	char **vec = NULL;
	size_t cap = 0;

	if (flags & WRDE_APPEND) {
		/* start from existing words */
		vec = pw->we_wordv;
		count = pw->we_wordc + pw->we_offs;
		cap = count;
	}

	for (;;) {
		/* skip leading whitespace */
		while (*s && isspace((unsigned char)*s))
			s++;
		if (!*s)
			break;
		/* parse one word */
		struct field f = { 0, 0, 0 };
		char *field;
		e = parse_word(&s, &f, flags);
		if (e) {
			free(f.data);
			goto fail;
		}
		/* prepare slot */
		if (count + 1 >= cap) {
			size_t ncap = cap ? cap * 2 : 8;
			char **nv = realloc(vec, ncap * sizeof *nv);
			if (!nv) {
				free(f.data);
				e = WRDE_NOSPACE;
				goto fail;
			}
			vec = nv;
			cap = ncap;
		}
		field = f.data;
		if (!field)
			field = (char *)"";
		/* glob expansion if metachars present */
		if (has_globmeta(field)) {
			glob_t g;
			memset(&g, 0, sizeof g);
			int gf = GLOB_NOCHECK; /* keep literal if no match */
			if (glob((char *)field, gf, NULL, &g) == 0) {
				for (size_t i = 0; i < g.gl_pathc; i++)
					vec[count++] = strdup(g.gl_pathv[i]);
				globfree(&g);
				free(field);
				continue;
			}
			globfree(&g);
		}
		vec[count++] = field;
	}
	vec[count] = NULL;

	/* WRDE_DOOFFS: keep leading NULL entries (we already size vec with room) */
	size_t offs = 0;
	if (!(flags & WRDE_APPEND) && (flags & WRDE_DOOFFS))
		offs = pw->we_offs;

	if (!(flags & WRDE_APPEND)) {
		pw->we_wordc = count - offs;
		pw->we_wordv = vec;
		pw->we_offs = offs;
	} else {
		pw->we_wordc = count - (flags & WRDE_DOOFFS ? pw->we_offs : 0);
	}
	return WRDE_SUCCESS;

fail:
	if (vec) {
		for (size_t i = 0; i < count; i++)
			free(vec[i]);
		free(vec);
	}
	return e;
}

void
wordfree(wordexp_t *pwordexp)
{
	if (!pwordexp || !pwordexp->we_wordv)
		return;
	for (size_t i = 0; i < pwordexp->we_wordc; i++)
		free(pwordexp->we_wordv[i]);
	free(pwordexp->we_wordv);
	pwordexp->we_wordv = NULL;
	pwordexp->we_wordc = 0;
}
