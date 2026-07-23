/* C99 flexible array member (§6.7.2.1) */
#include <stddef.h>
extern int puts(const char *);

struct flex {
    int len;
    int data[];    /* flexible array member — no size */
};

int main(void) {
    /* sizeof must not include the flexible array */
    if (sizeof(struct flex) != sizeof(int)) {
        puts("FAIL: sizeof flex");
        return 1;
    }

    /* offsetof must work for the fixed member */
    if (offsetof(struct flex, len) != 0) {
        puts("FAIL: offsetof flex.len");
        return 1;
    }

    /* 'data' should start right after len */
    if (offsetof(struct flex, data) != sizeof(int)) {
        puts("FAIL: offsetof flex.data");
        return 1;
    }

    /* Pointer arithmetic on flexible array members */
    struct flex *f = (struct flex *)0;
    if ((char *)&f->data[0] != (char *)f + sizeof(int)) {
        puts("FAIL: flex pointer");
        return 1;
    }

    puts("PASS");
    return 0;
}
