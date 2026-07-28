/* regex/regex.c — POSIX Extended Regular Expression engine (NFA-based) */

#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ---- NFA fragment types ---- */
enum { CONCAT, ALTERN, STAR, PLUS, QUEST, DOT, CHAR, SET, ANCHOR_START, ANCHOR_END };

struct nfa_state {
	int type;
	int c;             /* character for CHAR/SET */
	int out;           /* transition target 1 */
	int out1;          /* transition target 2 (for ALTERN/STAR) */
	unsigned char *charset; /* 32-byte bitmap for SET */
};

struct nfa_frag {
	int start;
	int *out;  /* list of dangling arcs */
	int out_cap, out_len;
};

static struct nfa_state *states = NULL;
static int nstates = 0, state_cap = 0;

static int alloc_state(void) {
	if (nstates >= state_cap) {
		state_cap = state_cap ? state_cap * 2 : 64;
		states = realloc(states, state_cap * sizeof(*states));
	}
	memset(&states[nstates], 0, sizeof(states[nstates]));
	return nstates++;
}

static void patch(int *list, int len, int s) {
	for (int i = 0; i < len; i++) states[list[i]].out = s;
}

static int list_append(struct nfa_frag *f, int s) {
	if (f->out_len >= f->out_cap) {
		f->out_cap = f->out_cap ? f->out_cap * 2 : 8;
		f->out = realloc(f->out, f->out_cap * sizeof(int));
	}
	f->out[f->out_len++] = s;
	return 0;
}

static struct nfa_frag frag(int start, int out_s) {
	struct nfa_frag f = { start, NULL, 0, 0 };
	list_append(&f, out_s);
	return f;
}

/* Regex parser — simple recursive descent for ERE */
static const char *rp; /* regex pointer */
static int rflags;
static int errcode;

static struct nfa_frag parse_branch(void);
static struct nfa_frag parse_piece(void);
static struct nfa_frag parse_atom(void);

static struct nfa_frag parse_branch(void) {
	struct nfa_frag f = parse_piece();
	struct nfa_frag g;
	while (*rp && *rp != '|' && *rp != ')') {
		g = parse_piece();
		/* concat: chain f's out to g's start */
		int s = alloc_state();
		states[s].type = CONCAT;
		states[s].out = f.start;
		states[f.out[0]].out = g.start;
		if (f.out_len > 1) patch(f.out + 1, f.out_len - 1, g.start);
		f.start = s;
		free(f.out);
		f.out = g.out;
		f.out_len = g.out_len;
		f.out_cap = g.out_cap;
	}
	return f;
}

static struct nfa_frag parse_piece(void) {
	struct nfa_frag f = parse_atom();
	if (!errcode && (*rp == '*' || *rp == '+' || *rp == '?')) {
		int op = *rp++;
		int s = alloc_state();
		if (op == '*') {
			states[s].type = STAR;
			states[s].out = f.start;
			patch(f.out, f.out_len, s);
			free(f.out);
			f.out = malloc(sizeof(int));
			f.out[0] = s; f.out_len = 1; f.out_cap = 1;
			f.start = s;
		} else if (op == '+') {
			states[s].type = STAR;
			states[s].out = f.start;
			patch(f.out, f.out_len, s);
			free(f.out);
			f.start = f.start;
			f.out = malloc(sizeof(int));
			f.out[0] = s; f.out_len = 1; f.out_cap = 1;
		} else { /* '?' */
			int s2 = alloc_state();
			states[s2].type = ALTERN;
			states[s2].out = f.start;
			states[s2].out1 = s; /* epsilon to end */
			patch(f.out, f.out_len, s);
			free(f.out);
			f.out = malloc(sizeof(int));
			f.out[0] = s2; f.out_len = 1; f.out_cap = 1;
			f.start = s2;
		}
	}
	return f;
}

static int hexval(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static struct nfa_frag parse_atom(void) {
	struct nfa_frag f = {0};
	if (!*rp) { errcode = REG_BADPAT; return f; }

	if (*rp == '(') {
		rp++;
		f = parse_branch();
		if (*rp == ')') rp++;
		else { errcode = REG_EPAREN; return f; }
		return f;
	}
	if (*rp == '|') {
		/* empty branch in alternation */
		int s = alloc_state();
		states[s].type = CHAR;
		states[s].c = -1; /* epsilon */
		f = frag(s, s);
		return f;
	}
	if (*rp == '.') {
		rp++;
		int s = alloc_state();
		states[s].type = DOT;
		f = frag(s, s);
		return f;
	}
	if (*rp == '[') {
		rp++;
		unsigned char charset[32] = {0};
		int neg = 0;
		if (*rp == '^') { neg = 1; rp++; }
		if (*rp == ']') { charset[0] |= 1; rp++; } /* literal ] */
		while (*rp && *rp != ']') {
			if (*rp == '\\') rp++;
			int c1 = (unsigned char)*rp++;
			if (*rp == '-' && rp[1] && rp[1] != ']') {
				rp++;
				int c2 = (unsigned char)*rp++;
				if (c1 > c2) { int t = c1; c1 = c2; c2 = t; }
				for (int i = c1; i <= c2; i++)
					charset[i >> 3] |= (1 << (i & 7));
			} else {
				charset[c1 >> 3] |= (1 << (c1 & 7));
			}
		}
		if (!*rp) { errcode = REG_EBRACK; return f; }
		rp++; /* skip ] */
		int s = alloc_state();
		states[s].type = SET;
		if (neg) {
			for (int i = 0; i < 32; i++) charset[i] = ~charset[i];
			charset[0] &= ~1; /* exclude NUL */
		}
		states[s].charset = malloc(32);
		memcpy(states[s].charset, charset, 32);
		f = frag(s, s);
		return f;
	}
	if (*rp == '\\') {
		rp++;
		if (!*rp) { errcode = REG_EESCAPE; return f; }
		int c = (unsigned char)*rp++;
		/* Backreferences and other escapes: treat as literal */
		int s = alloc_state();
		states[s].type = CHAR;
		states[s].c = c;
		f = frag(s, s);
		return f;
	}
	/* Literal character */
	int c = (unsigned char)*rp++;
	if (c == '*' || c == '+' || c == '?' || c == '|' || c == ')' || c == '^' || c == '$') {
		if (c == '^') {
			int s = alloc_state(); states[s].type = ANCHOR_START;
			f = frag(s, s); return f;
		}
		if (c == '$') {
			int s = alloc_state(); states[s].type = ANCHOR_END;
			f = frag(s, s); return f;
		}
		/* Unary operator without operand — error */
		errcode = REG_BADRPT;
		return f;
	}
	int s = alloc_state();
	states[s].type = CHAR;
	states[s].c = c;
	f = frag(s, s);
	return f;
}

/* ---- NFA simulation (Thompson NFA with state sets) ---- */

/* Compute epsilon closure (all states reachable without consuming input) */
static int closure(int *set, int *set_len, int max_states) {
	int added;
	do {
		added = 0;
		for (int i = 0; i < *set_len; i++) {
			int s = set[i];
			if (s >= max_states) continue;
			int next[3]; int nnext = 0;
			switch (states[s].type) {
			case CONCAT: case STAR: case PLUS: next[nnext++] = states[s].out; break;
			case ALTERN: next[nnext++] = states[s].out; next[nnext++] = states[s].out1; break;
			case CHAR: if (states[s].c < 0) next[nnext++] = states[s].out; break;
			default: break;
			}
			for (int ni = 0; ni < nnext; ni++) {
				int ns = next[ni];
				int found = 0;
				for (int j = 0; j < *set_len; j++)
					if (set[j] == ns) { found = 1; break; }
				if (!found && ns < max_states) {
					set[(*set_len)++] = ns;
					added = 1;
				}
			}
		}
	} while (added);
	return 0;
}

static int
matched(const regex_t *re, const char *str, regmatch_t *pm, int eflags)
{
	int start = ((int *)re->opaque)[0];
	int accept = ((int *)re->opaque)[1];
	int max_states = nstates; /* from last regcomp */
	int slen = strlen(str);
	(void)eflags;

	/* For each start position */
	for (int pos = 0; pos <= slen; pos++) {
		if (pos > 0 && (eflags & REG_NOTBOL)) break;

		/* State set for current position */
		int cur[512], cur_len = 0;
		cur[cur_len++] = start;
		closure(cur, &cur_len, max_states);

		/* Check if we're already at accept */
		for (int i = 0; i < cur_len; i++)
			if (cur[i] == accept) {
				if (pm) { pm[0].rm_so = pos; pm[0].rm_eo = pos; }
				return 1;
			}

		/* Step through remaining characters */
		for (int p = pos; p < slen; p++) {
			int next[512], next_len = 0;
			int c = (unsigned char)str[p];
			if (rflags & REG_ICASE) c = tolower(c);

			for (int i = 0; i < cur_len; i++) {
				int s = cur[i];
				int match_char = 0;
				int target = -1;

				switch (states[s].type) {
				case CHAR:
					if (states[s].c == c || 
					    ((rflags & REG_ICASE) && tolower(states[s].c) == c))
						{ match_char = 1; target = states[s].out; }
					break;
				case DOT:
					if (c != '\n') { match_char = 1; target = states[s].out; }
					break;
				case SET:
					if (states[s].charset[c >> 3] & (1 << (c & 7)))
						{ match_char = 1; target = states[s].out; }
					break;
				case STAR:
					/* STAR: try both body and skip */
					/* The star's body is at states[s].out */
					/* Check if char matches body's first char */
					/* For now, handle simple star by checking:
					 * STAR state: try matching body then looping, AND skipping */
					/* Add body's start to next set */
					if (states[s].out < max_states) {
						int found = 0;
						for (int j = 0; j < next_len; j++)
							if (next[j] == states[s].out) { found = 1; break; }
						if (!found) next[next_len++] = states[s].out;
					}
					/* Also add the star itself (loop) to continuation */
					if (next_len < 510) next[next_len++] = s;
					break;
				default:
					break;
				}

				if (match_char && target >= 0) {
					int found = 0;
					for (int j = 0; j < next_len; j++)
						if (next[j] == target) { found = 1; break; }
					if (!found && next_len < 510) next[next_len++] = target;
				}
			}

			/* Epsilon closure on next set */
			closure(next, &next_len, max_states);

			/* Check for accept */
			for (int i = 0; i < next_len; i++)
				if (next[i] == accept) {
					if (pm) { pm[0].rm_so = pos; pm[0].rm_eo = p + 1; }
					return 1;
				}

			memcpy(cur, next, next_len * sizeof(int));
			cur_len = next_len;
		}
	}
	return 0;
}

int
regcomp(regex_t *restrict preg, const char *restrict pattern, int cflags)
{
	/* Reset */
	if (states) { free(states); states = NULL; }
	nstates = 0; state_cap = 0;
	rp = pattern;
	rflags = cflags;
	errcode = 0;

	/* ERE grammar: branch ( '|' branch )* */
	struct nfa_frag f = {0};
	
	if (*rp) {
		f = parse_branch();
		while (*rp == '|') {
			rp++;
			struct nfa_frag g = parse_branch();
			int s = alloc_state();
			states[s].type = ALTERN;
			states[s].out = f.start;
			states[s].out1 = g.start;
			patch(f.out, f.out_len, s);
			patch(g.out, g.out_len, s);
			free(f.out); free(g.out);
			f.out = malloc(sizeof(int));
			f.out[0] = s; f.out_len = 1; f.out_cap = 1;
			f.start = s;
		}
	} else {
		/* Empty pattern matches empty string */
		int s = alloc_state();
		states[s].type = CHAR; states[s].c = -1;
		f = frag(s, s);
	}

	if (errcode) {
		regfree(preg);
		if (states) { free(states); states = NULL; }
		nstates = 0;
		return errcode;
	}

	/* Create accept state */
	int accept = alloc_state();
	states[accept].type = CHAR; states[accept].c = -1;

	/* Connect final fragment's out to accept */
	patch(f.out, f.out_len, accept);
	int start = f.start;

	/* Store start and accept in opaque */
	preg->re_nsub = 0;
	int *opaque = malloc(2 * sizeof(int));
	opaque[0] = start;
	opaque[1] = accept;
	if (preg->opaque) free(preg->opaque);
	preg->opaque = opaque;
	preg->opaque_len = 2 * sizeof(int);

	free(f.out);

	return 0;
}

int
regexec(const regex_t *restrict preg, const char *restrict str,
        size_t nmatch, regmatch_t pmatch[restrict], int eflags)
{
	(void)nmatch;
	return matched(preg, str, pmatch, eflags) ? 0 : REG_NOMATCH;
}

size_t
regerror(int errcode, const regex_t *restrict preg,
         char *restrict buf, size_t bufsize)
{
	(void)preg;
	const char *msg;
	switch (errcode) {
	case REG_NOERROR: msg = "Success"; break;
	case REG_NOMATCH: msg = "No match"; break;
	case REG_BADPAT:  msg = "Invalid regex pattern"; break;
	case REG_ECOLLATE: msg = "Invalid collation"; break;
	case REG_ECTYPE:  msg = "Invalid character class"; break;
	case REG_EESCAPE: msg = "Trailing backslash"; break;
	case REG_ESUBREG: msg = "Invalid back reference"; break;
	case REG_EBRACK:  msg = "Unmatched bracket"; break;
	case REG_EPAREN:  msg = "Unmatched parenthesis"; break;
	case REG_EBRACE:  msg = "Unmatched brace"; break;
	case REG_BADBR:   msg = "Invalid repetition count"; break;
	case REG_ERANGE:  msg = "Invalid character range"; break;
	case REG_ESPACE:  msg = "Out of memory"; break;
	case REG_BADRPT:  msg = "Invalid use of repetition operator"; break;
	default:          msg = "Unknown error"; break;
	}
	size_t n = strlen(msg) + 1;
	if (buf && bufsize > 0) {
		strncpy(buf, msg, bufsize - 1);
		buf[bufsize - 1] = '\0';
	}
	return n;
}

void
regfree(regex_t *preg)
{
	if (preg->opaque) {
		free(preg->opaque);
		preg->opaque = NULL;
	}
	preg->opaque_len = 0;
}
