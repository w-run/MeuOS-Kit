/* fold_expr.cc — C++17 fold expressions.
 *
 * A fold expression reduces a parameter pack with an operator:
 *   - unary left:   (... op pack)  -> ((a0 op a1) op a2) ...
 *   - unary right:  (pack op ...)  -> a0 op (a1 op (a2 ...))
 *   - binary left:  (pack op ... op init)
 * Works with arithmetic (`+`), logical (`&&`, `||`), comparison, etc.
 *
 * Returns 0 on success.
 */

template <typename... A>
auto
suml(A... a)
{
	return (... + a);          /* unary left  : (1+2)+3 */
}

template <typename... A>
auto
sumr(A... a)
{
	return (a + ...);          /* unary right : 1+(2+3) */
}

template <typename... A>
auto
all_true(A... a)
{
	return (... && a);         /* unary left logical AND */
}

template <typename... A>
auto
any_true(A... a)
{
	return (... || a);         /* unary left logical OR */
}

template <typename... A>
auto
bin_add(A... a)
{
	return (a + ... + 10);     /* binary left : (a0+a1+a2)+10 */
}

template <typename... A>
auto
fsum(A... a)
{
	return (... + a);          /* double elements */
}

int
main(void)
{
	if (suml(1, 2, 3) != 6) return 1;       /* ((1+2)+3) */
	if (sumr(1, 2, 3) != 6) return 2;       /* 1+(2+3) */
	if (!all_true(true, true, true)) return 3;
	if (all_true(true, false, true)) return 4;
	if (any_true(false, false, true) != true) return 5;
	if (bin_add(1, 2, 3) != 16) return 6;   /* (1+2+3)+10 */
	if (suml(7) != 7) return 7;             /* single element */
	if (fsum(1.5, 2.5, 0.0) != 4.0) return 8;

	return 0;
}
