#include <ctype.h>
#include <stdio.h>

int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isalpha(int c) { return islower(c) || isupper(c); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isascii(int c) { return c >= 0 && c <= 127; }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isprint(int c) { return c >= ' ' && c <= '~'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }

int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { if (c == EOF) return 0; return (unsigned)c < 0x20 || c == 0x7f; }
int isgraph(int c) { if (c == EOF) return 0; return c > ' ' && c < 0x7f; }
int ispunct(int c) { if (c == EOF) return 0; return isgraph(c) && !isalnum(c); }

/*
 * Ctype lookup tables.
 *
 * musl/glibc both expose a 384-entry table indexed by
 * (unsigned char)c + 128; indices 0..127 carry the EOF-class slots
 * (kept zero here, matching the historical layout), indices
 * 128..383 carry one entry per byte value 0..255.  Two flavours are
 * exposed side-by-side so foreign code can use whichever it prefers:
 *
 *   __ctype_b_loc/__ctype_tolower_loc/__ctype_toupper_loc  (function
 *       pointer flavour; glibc-style, thread-safe via a fixed pointer
 *       to a const-internal table).  Foreign code that calls the
 *       function form works without further symbol renaming.
 *
 *   __ctype_b / __ctype_tolower / __ctype_toupper  (flat array
 *       flavour; musl/BSD-style).  Foreign code that does `extern
 *       const unsigned short *__ctype_b;` or indexes the array
 *       directly links without going through the function.
 *
 * The tables are filled lazily by init_ctype_table(), which is
 * called by every accessor.  The flat arrays live in .data (not
 * .rodata) so lazy fill works; the function-pointer form keeps a
 * const pointer to the same memory so both views see identical data.
 *
 * Lazy init avoids needing a constructor (the GNU C runtime
 * .init_array machinery is only invoked when crt1 passes through
 * __libc_start_main, which already runs _init/_fini — duplicating
 * that work from a static initializer would couple libc to startup
 * order in ways the rest of the codebase does not assume).
 */

#define CTYPE_TABLE_BITS 12  /* 384 entries */
#define CTYPE_TABLE_HALF 128 /* EOF-class slot */

/* Flat (non-const) backing storage.  Foreign code reads through
 * __ctype_b/__ctype_tolower/__ctype_toupper (which alias these); the
 * function-pointer flavour exposes the same memory through a const
 * pointer so the two views never disagree.  Not declared const so the
 * linker keeps the arrays in .data and lazy init can write them. */
static unsigned short ctype_b_tab[1 << CTYPE_TABLE_BITS];
static int ctype_tolower_tab[1 << CTYPE_TABLE_BITS];
static int ctype_toupper_tab[1 << CTYPE_TABLE_BITS];

/* Public flat-array symbols (musl/BSD-style).  Indexed by
 * (unsigned char)c + 128. */
unsigned short *__ctype_b = ctype_b_tab;
int *__ctype_tolower = ctype_tolower_tab;
int *__ctype_toupper = ctype_toupper_tab;

/* Backing store for the function-pointer flavour.  The const pointer
 * returned by __ctype_*_loc always points here so the underlying data
 * is shared between the two views. */
static const unsigned short *ctype_b_loc_ptr;
static const int *ctype_tolower_loc_ptr;
static const int *ctype_toupper_loc_ptr;

static int ctype_tables_ready;

static void
init_ctype_table(void)
{
	int c;

	if (ctype_tables_ready)
		return;

	/* EOF-class slot 127 (and the rest of the lower half) stays zero;
	 * only the upper half (index 128..383 = byte 0..255) gets data. */
	for (c = 0; c < 256; ++c) {
		unsigned short flags = 0;
		if (isupper(c)) flags |= _ISupper;
		if (islower(c)) flags |= _ISlower;
		if (isalpha(c)) flags |= _ISalpha;
		if (isdigit(c)) flags |= _ISdigit;
		if (isxdigit(c)) flags |= _ISxdigit;
		if (isspace(c)) flags |= _ISspace;
		if (isprint(c)) flags |= _ISprint;
		if (isgraph(c)) flags |= _ISgraph;
		if (isblank(c)) flags |= _ISblank;
		if (iscntrl(c)) flags |= _IScntrl;
		if (ispunct(c)) flags |= _ISpunct;
		if (isalnum(c)) flags |= _ISalnum;
		ctype_b_tab[c + CTYPE_TABLE_HALF] = flags;
		ctype_tolower_tab[c + CTYPE_TABLE_HALF] = tolower(c);
		ctype_toupper_tab[c + CTYPE_TABLE_HALF] = toupper(c);
	}

	ctype_b_loc_ptr = ctype_b_tab + CTYPE_TABLE_HALF;
	ctype_tolower_loc_ptr = ctype_tolower_tab + CTYPE_TABLE_HALF;
	ctype_toupper_loc_ptr = ctype_toupper_tab + CTYPE_TABLE_HALF;
	ctype_tables_ready = 1;
}

const unsigned short **
__ctype_b_loc(void)
{
	if (!ctype_tables_ready)
		init_ctype_table();
	return (const unsigned short **)&ctype_b_loc_ptr;
}

const int **
__ctype_tolower_loc(void)
{
	if (!ctype_tables_ready)
		init_ctype_table();
	return (const int **)&ctype_tolower_loc_ptr;
}

const int **
__ctype_toupper_loc(void)
{
	if (!ctype_tables_ready)
		init_ctype_table();
	return (const int **)&ctype_toupper_loc_ptr;
}