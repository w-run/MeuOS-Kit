/* locale/locale.c — C/POSIX locale support */

#include <locale.h>
#include <string.h>
#include <stdlib.h>

static char current_locale[64] = "C";

char *
setlocale(int category, const char *locale)
{
	(void)category;
	if (!locale) {
		/* Query: return current locale */
		return current_locale;
	}
	if (locale[0] == '\0') {
		/* "" → use environment (default to "C") */
		const char *env = getenv("LANG");
		if (env && env[0]) {
			size_t n = strlen(env);
			if (n >= sizeof(current_locale))
				n = sizeof(current_locale) - 1;
			memcpy(current_locale, env, n);
			current_locale[n] = '\0';
		} else {
			strcpy(current_locale, "C");
		}
		return current_locale;
	}
	/* "C" or "POSIX" — the only locales we truly support */
	if (strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0) {
		strcpy(current_locale, "C");
		return current_locale;
	}
	return NULL; /* unsupported locale */
}

struct lconv *
localeconv(void)
{
	/* C locale: "." decimal point, no grouping, empty currency */
	static struct lconv c_locale = {
		.decimal_point = ".",
		.thousands_sep = "",
		.grouping = "",
		.mon_decimal_point = "",
		.mon_thousands_sep = "",
		.mon_grouping = "",
		.positive_sign = "",
		.negative_sign = "",
		.int_frac_digits = -1,
		.frac_digits = -1,
		.p_cs_precedes = -1,
		.p_sep_by_space = -1,
		.n_cs_precedes = -1,
		.n_sep_by_space = -1,
		.p_sign_posn = -1,
		.n_sign_posn = -1,
		.int_p_cs_precedes = -1,
		.int_p_sep_by_space = -1,
		.int_n_cs_precedes = -1,
		.int_n_sep_by_space = -1,
		.int_curr_symbol = "",
		.currency_symbol = "",
	};
	return &c_locale;
}
