/* C++23 P1774: `[[assume]]` requires the parenthesized expression form. */
int f(int x) {
    [[assume]];      /* ill-formed: no argument */
    return x;
}

int main(void) { return 0; }
