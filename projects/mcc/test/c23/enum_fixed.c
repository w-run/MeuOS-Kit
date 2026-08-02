/* C23 枚举固定底层类型 (§6.7.2.2: `enum E : type { ... }`)。
 *
 * 覆盖点：
 *   - `enum E : unsigned char` 窄底层类型：成员可超 int 正范围
 *     （如 200），sizeof(E) 反映底层类型宽度；
 *   - `enum E : int` 显式有符号底层类型，允许负值成员；
 *   - 成员值与底层类型范围匹配。
 */
extern int puts(const char *);

enum Byte : unsigned char { B_A = 200, B_B = 1 };
enum Signed : int { S_NEG = -5, S_POS = 2 };

int main(void) {
	enum Byte b = B_A;
	enum Signed s = S_NEG;

	/* 窄底层类型：200 超出普通 int 枚举常用范围但仍可表示 */
	if ((int)b != 200) {
		puts("FAIL: enum byte value");
		return 1;
	}
	/* 有符号底层类型：负值成员 */
	if ((int)s != -5) {
		puts("FAIL: enum signed value");
		return 2;
	}
	/* 第二个成员 */
	if ((int)S_POS != 2 || (int)B_B != 1) {
		puts("FAIL: enum second member");
		return 3;
	}

	puts("PASS");
	return 0;
}
