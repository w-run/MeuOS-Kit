#ifndef MEUOS_CTYPE_H
#define MEUOS_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int);
int isalpha(int);
int isascii(int);
int isblank(int);
int iscntrl(int);
int isdigit(int);
int isgraph(int);
int islower(int);
int isprint(int);
int ispunct(int);
int isspace(int);
int isupper(int);
int isxdigit(int);
int tolower(int);
int toupper(int);

#define _ISbit(bit)	(1 << (bit))
#define _ISupper	_ISbit(0)
#define _ISlower	_ISbit(1)
#define _ISalpha	_ISbit(2)
#define _ISdigit	_ISbit(3)
#define _ISxdigit	_ISbit(4)
#define _ISspace	_ISbit(5)
#define _ISprint	_ISbit(6)
#define _ISgraph	_ISbit(7)
#define _ISblank	_ISbit(8)
#define _IScntrl	_ISbit(9)
#define _ISpunct	_ISbit(10)
#define _ISalnum	_ISbit(11)

/* Ctype lookup tables.
 *
 * Two views on the same backing storage:
 *   1) Function-pointer form (glibc flavour):  __ctype_b_loc() /
 *      __ctype_tolower_loc() / __ctype_toupper_loc() each return a
 *      pointer to a fixed const pointer into the table.  Thread-safe
 *      because the pointer is read-only after first init.
 *   2) Flat-array form (musl/BSD flavour):  __ctype_b /
 *      __ctype_tolower / __ctype_toupper point at the same 384-entry
 *      array (index 0..127 = EOF-class slot, index 128..383 = byte
 *      values 0..255).  Foreign code that wants to index the table
 *      directly links without going through the function form.
 *
 * Both views share the same backing storage (src/ctype/ctype.c) so
 * they never disagree. */
const unsigned short **__ctype_b_loc(void);
const int **__ctype_tolower_loc(void);
const int **__ctype_toupper_loc(void);

extern unsigned short *__ctype_b;
extern int *__ctype_tolower;
extern int *__ctype_toupper;

#ifdef __cplusplus
}
#endif

#endif
