#include <stdio.h>
struct Empty {};
struct S {
    int x;
    [[no_unique_address]] Empty e;
};
int main(void) {
    printf("sizeof(Empty)=%zu\n", sizeof(struct Empty));
    printf("sizeof(S)=%zu\n", sizeof(struct S));
    struct S s;
    printf("&s=%p &s.x=%p &s.e=%p\n", (void*)&s, (void*)&s.x, (void*)&s.e);
    printf("offset e=%zu\n", (char*)&s.e - (char*)&s);
    return 0;
}