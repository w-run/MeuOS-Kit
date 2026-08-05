/* wordexp_gate.c — wordexp/wordfree regression gate.
 * Asserts the minimal-subset expansions: $VAR / ${VAR}, single+double
 * quoting, glob expansion, '~', and the error paths WRDE_CMDSUB and
 * WRDE_BADVAL (WRDE_UNDEF). */
#include <wordexp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static int
wordp(wordexp_t *w, const char *s, int flags)
{
	return wordexp(s, w, flags);
}

static void
expect(const char *label, const char *input, int flags, int want_e,
       const char *want[], size_t nwant)
{
	wordexp_t w;
	int e = wordp(&w, input, flags);
	if (e != want_e) {
		printf("FAIL: %s wordexp('%s') ret=%d want=%d\n", label, input, e, want_e);
		fails++;
		return;
	}
	if (e != WRDE_SUCCESS) {
		wordfree(&w);
		return;
	}
	if (w.we_wordc != nwant) {
		printf("FAIL: %s wordc=%zu want %zu\n", label, w.we_wordc, nwant);
		wordfree(&w);
		fails++;
		return;
	}
	for (size_t i = 0; i < nwant; i++) {
		if (strcmp(w.we_wordv[i], want[i]) != 0) {
			printf("FAIL: %s[%zu]='%s' want '%s'\n", label, i, w.we_wordv[i], want[i]);
			fails++;
		}
	}
	wordfree(&w);
}

int
main(void)
{
	setenv("FOO", "bar", 1);
	setenv("HOME", "/home/gate", 1);

	/* $VAR and ${VAR} */
	{
		const char *w1[] = { "hello", "bar", "world" };
		expect("dollar", "hello $FOO world", 0, 0, w1, 3);
	}
	{
		const char *w1[] = { "x", "bar", "y" };
		expect("brace", "x ${FOO} y", 0, 0, w1, 3);
	}
	/* single-quoted keeps spaces in one word */
	{
		const char *w1[] = { "a", "b c", "d" };
		expect("quote", "a 'b c' d", 0, 0, w1, 3);
	}
	/* glob expands to matching files */
	{
		char tmp[64];
		strcpy(tmp, "/tmp/wx.XXXXXX");
		int tfd = mkstemp(tmp);
		if (tfd < 0) { printf("FAIL: mkstemp\n"); fails++; }
		else {
			char pattern[80];
			snprintf(pattern, sizeof pattern, "%s*", tmp);
			wordexp_t w;
			int e = wordexp(pattern, &w, 0);
			if (e != 0) { printf("FAIL: glob wordexp ret=%d\n", e); fails++; }
			else {
				if (w.we_wordc == 0) { printf("FAIL: glob matched nothing\n"); fails++; }
				else if (strcmp(tmp, w.we_wordv[0]) != 0) {
					printf("FAIL: glob[0]='%s' want '%s'\n", w.we_wordv[0], tmp);
					fails++;
				}
			}
			wordfree(&w);
			close(tfd); unlink(tmp);
		}
	}
	/* cmdsub -> WRDE_CMDSUB */
	expect("cmdsub-dollar", "$(echo x)", 0, WRDE_CMDSUB, NULL, 0);
	expect("cmdsub-btick", "`echo x`", 0, WRDE_CMDSUB, NULL, 0);
	/* undefined + WRDE_UNDEF -> WRDE_BADVAL */
	expect("undef", "$NOPE", WRDE_UNDEF, WRDE_BADVAL, NULL, 0);
	/* undefined no flag -> empty expansion, still one word "a" */
	{
		const char *w1[] = { "a" };
		expect("undef-empty", "a$NOPE", 0, 0, w1, 1);
	}
	/* '~' -> HOME */
	{
		const char *w1[] = { "/home/gate", "f" };
		expect("tilde", "~ f", 0, 0, w1, 2);
	}

	/* ---- wordfree() hardening: every wordexp_t must be wordfree-able ----
	 * A mixed input (glob + quote + $VAR) then immediate wordfree, an empty
	 * expansion (wordfree of the stored empty string must be safe), and a
	 * wordfree after a *failed* wordexp (cmdsub leaves *pwordexp in a
	 * safe/wordfree-no-op state). */
	{
		/* mixed: one glob match, one quoted, one $VAR */
		wordexp_t wx;
		memset(&wx, 0, sizeof wx);
		setenv("MF", "mid", 1);
		/* glob of a real temp file + a quoted literal + $VAR */
		char tmp[64];
		strcpy(tmp, "/tmp/we_mix.XXXXXX");
		int tf = mkstemp(tmp);
		if (tf < 0) { printf("FAIL: mkstemp mix\n"); fails++; }
		else {
			char input[140];
			snprintf(input, sizeof input, "%s* 'a b' pre$MF post", tmp);
			int e = wordexp(input, &wx, 0);
			if (e != 0) { printf("FAIL: mixed wordexp ret=%d\n", e); fails++; }
			else {
				/* expect >= 4 words: glob(+1), 'a b', pre, mid, post */
				int ok = wx.we_wordc >= 4;
				if (!ok) printf("FAIL: mixed wordc=%zu want>=4\n", wx.we_wordc);
				else {
					/* glob[0] == tmp; $MF expands to "mid" glued to "pre" -> "premid" */
					const char *mid = NULL;
					for (size_t i = 1; i < wx.we_wordc; i++)
						if (strcmp(wx.we_wordv[i], "premid") == 0) mid = wx.we_wordv[i];
					if (strcmp(tmp, wx.we_wordv[0]) != 0) {
						printf("FAIL: mixed glob[0]='%s' want '%s'\n", wx.we_wordv[0], tmp);
						fails++;
					}
					if (!mid) { printf("FAIL: mixed missing pre$MF->premid\n"); fails++; }
				}
			}
			wordfree(&wx); /* must not crash */
			close(tf); unlink(tmp);
		}
	}
	/* empty expansion -> stored empty string must be freeable */
	{
		wordexp_t we;
		memset(&we, 0, sizeof we);
		int e = wordexp("\"\"", &we, 0); /* empty double-quoted word */
		if (e != 0) { printf("FAIL: empty qword e=%d\n", e); fails++; }
		else if (we.we_wordc != 1) { printf("FAIL: empty qword wordc=%zu\n", we.we_wordc); fails++; }
		wordfree(&we); /* must not crash freeing the empty string */
	}
	/* wordfree after a failed (cmdsub) wordexp */
	{
		wordexp_t wf;
		memset(&wf, 0, sizeof wf);
		int e = wordexp("$(no)", &wf, 0);
		if (e != WRDE_CMDSUB) { printf("FAIL: cmdsub-fail e=%d\n", e); fails++; }
		wordfree(&wf); /* must be a safe no-op (pw left NULL) */
		wordfree(&wf); /* idempotent */
	}

	if (fails) {
		printf("%d wordexp FAIL\n", fails);
		return 1;
	}
	printf("PASS wordexp\n");
	return 0;
}
