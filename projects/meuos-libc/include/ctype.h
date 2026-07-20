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

#ifdef __cplusplus
}
#endif

#endif
