/* auto_nttp.cc — C++17 `auto` non-type template parameters.
 *
 * `template <auto V>` deduces the parameter's type from the argument
 * value at instantiation (an integer literal gives an int NTTP, a char
 * literal a char NTTP).  Returns 0 on success.
 */
template <auto V>
int
get(void)
{
	return (int)V;
}

int
main(void)
{
	if (get<7>() != 7) return 1;
	if (get<'A'>() != 65) return 2;
	return 0;
}
