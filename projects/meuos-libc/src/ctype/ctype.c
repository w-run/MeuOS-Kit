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
int isgraph(int c) { if (c == EOF) return 0; return c > 0x20 && c < 0x7f; }
int ispunct(int c) { if (c == EOF) return 0; return isgraph(c) && !isalnum(c); }

/*
 * glibc 兼容内部符号：__ctype_b_loc() 返回指向 ctype 表指针的指针。
 * 表按 glibc 布局为 384 项，索引 (unsigned char)c + 128 处存放字符 c 的
 * _IS* 位标志；索引 127（EOF 处）为 0。懒初始化以避免构造器机制。
 */
static unsigned short ctype_b_table[384];
static const unsigned short *ctype_b_loc_value;

static void
init_ctype_table(void)
{
	int c;

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
		ctype_b_table[c + 128] = flags;
	}
}

const unsigned short **
__ctype_b_loc(void)
{
	if (!ctype_b_loc_value)
		init_ctype_table();
	ctype_b_loc_value = ctype_b_table + 128;
	return &ctype_b_loc_value;
}
