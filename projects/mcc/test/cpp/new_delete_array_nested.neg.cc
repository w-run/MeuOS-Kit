/* Negative test: multi-dimensional `new T[n][m]` is not supported — the
 * array form only handles a single dimension, so `new int[3][4]` fails to
 * typecheck.  Keep this pinned so a future multi-dim implementation can
 * flip it to a positive test.
 */
int
main(void)
{
    int (*g)[4] = new int[3][4];
    g[1][2] = 5;
    delete[] g;
    return 0;
}
