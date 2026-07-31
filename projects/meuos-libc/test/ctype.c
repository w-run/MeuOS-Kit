#include <ctype.h>
#include <stdio.h>

/* glibc 内部符号：不在公共头文件中声明 */
const unsigned short **__ctype_b_loc(void);

int
main(void)
{
	const unsigned short **table;
	if (!isalpha('Z') || !isdigit('7') || !isxdigit('f') || !isspace('\n') ||
	    !isprint(' ') || tolower('Q') != 'q' || toupper('m') != 'M')
		return 1;
	table = __ctype_b_loc();
	if (!((*table)['a'] & _ISlower) || !((*table)['A'] & _ISupper) ||
	    !((*table)['5'] & _ISdigit) || (*table)[EOF] != 0)
		return 1;
	puts("PASS ctype");
	return 0;
}
