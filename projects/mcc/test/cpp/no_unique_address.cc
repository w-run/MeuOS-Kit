#include <stdio.h>
struct Empty {};
struct S {
    int x;
    [[no_unique_address]] Empty e;
};
int main(void) {
    /* sizeof(S) should be 4 (int alone) since Empty is [[no_unique_address]] */
    if (sizeof(struct S) != sizeof(int))
        return 1;
    /* The empty member should be at offset 0 (overlapping with x) */
    struct S s;
    size_t off = (char*)&s.e - (char*)&s;
    if (off != 0)
        return 2;
    return 0;
}