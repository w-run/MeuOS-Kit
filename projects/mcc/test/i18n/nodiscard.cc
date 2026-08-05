/* nodiscard.cc — i18n --explain 测试用警告：忽略 nodiscard 返回值。
 * check-i18n 用 m++ --explain --lang=en/zh 编译，断言修复建议语言一致。 */
[[nodiscard]] int
f(void)
{
	return 1;
}

int
main(void)
{
	f();
	return 0;
}
