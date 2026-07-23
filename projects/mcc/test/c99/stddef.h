#ifndef MCC_TEST_STDDEF_H
#define MCC_TEST_STDDEF_H

typedef __typeof__(sizeof(int)) size_t;

#define NULL ((void *)0)

#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)

#endif
