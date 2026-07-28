/* glob/fnmatch.c — POSIX fnmatch implementation */

#include <fnmatch.h>
#include <string.h>
#include <ctype.h>

int
fnmatch(const char *pattern, const char *string, int flags)
{
	int noescape = !!(flags & FNM_NOESCAPE);
	int pathname = !!(flags & FNM_PATHNAME);
	int period   = !!(flags & FNM_PERIOD);
	int casefold = !!(flags & FNM_CASEFOLD);

	if (period && string[0] == '.' &&
	    (pattern[0] != '.' || (pattern[0] == '.' && pattern[1] == '*')))
		return FNM_NOMATCH;

	for (;;) {
		unsigned char pc = (unsigned char)*pattern;
		unsigned char sc = (unsigned char)*string;

		if (casefold) {
			pc = (unsigned char)tolower(pc);
			sc = (unsigned char)tolower(sc);
		}

		switch (pc) {
		case '?':
			if (sc == 0) return FNM_NOMATCH;
			if (pathname && sc == '/') return FNM_NOMATCH;
			pattern++; string++;
			continue;
		case '*':
			pattern++;
			/* Collapse multiple '*' */
			while (*pattern == '*') pattern++;
			if (*pattern == 0)
				return 0; /* trailing * matches everything */
			if (pathname) {
				/* Stop at '/' */
				const char *s;
				for (s = string; *s; s++) {
					if (*s == '/') break;
					if (fnmatch(pattern, s, flags) == 0)
						return 0;
				}
				/* Also try matching with '/' as part of path */
				return fnmatch(pattern, s, flags);
			}
			for (; *string; string++) {
				if (fnmatch(pattern, string, flags) == 0)
					return 0;
			}
			return fnmatch(pattern, string, flags);
		case '\\':
			if (!noescape) {
				pattern++;
				pc = (unsigned char)*pattern;
				if (casefold)
					pc = (unsigned char)tolower(pc);
			}
			/* fall through */
		default:
			if (pc == 0 && sc == 0) return 0;
			if (pc != sc) return FNM_NOMATCH;
			/* [!] character class not implemented */
			pattern++; string++;
			continue;
		case '[':
			/* Character class: [...] or [!...] */
			if (sc == 0) return FNM_NOMATCH;
			if (pathname && sc == '/') return FNM_NOMATCH;
			pattern++; /* skip '[' */
			int negate = 0;
			if (*pattern == '!') { negate = 1; pattern++; }
			int matched = 0;
			while (*pattern && *pattern != ']') {
				if (*pattern == '\\' && !noescape) pattern++;
				unsigned char cc = (unsigned char)*pattern;
				if (casefold) cc = (unsigned char)tolower(cc);
				/* Range: a-z */
				if (pattern[1] == '-' && pattern[2] && pattern[2] != ']') {
					pattern += 2;
					unsigned char ec = (unsigned char)*pattern;
					if (casefold) ec = (unsigned char)tolower(ec);
					if ((cc <= sc && sc <= ec) || (ec <= sc && sc <= cc))
						matched = 1;
				} else {
					if (cc == sc) matched = 1;
				}
				pattern++;
			}
			if (*pattern == ']') pattern++; /* skip ']' */
			if (negate) matched = !matched;
			if (!matched) return FNM_NOMATCH;
			string++;
			continue;
		}
	}
}
