/* new_delete_array.cc — array forms of new/delete (m++).
 *
 * Covers:
 *  - `new T[n]` / `delete[] p` for a builtin element type (malloc only)
 *  - `new T[n]` / `delete[] p` for a class with a constructor/destructor:
 *    each element is default-constructed on allocation and destructed on
 *    deallocation (the array length is kept in a cookie before the block)
 *  - edge case `new T[0]`
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);
int dtor_count = 0;

class Item {
public:
    Item() { val = 0; }
    ~Item() { dtor_count++; }
    int val;
};

int
main(void)
{
    /* builtin element type: write/read through the array */
    int *a = new int[5];
    for (int i = 0; i < 5; i++)
        a[i] = i * 2;
    if (a[3] != 6) return 1;
    delete[] a;

    /* class element type: default construction + per-element dtor */
    Item *arr = new Item[3];
    if (arr[0].val != 0 || arr[2].val != 0) return 2;
    arr[1].val = 42;
    if (arr[1].val != 42) return 3;
    delete[] arr;
    if (dtor_count != 3) return 4;

    /* zero-length arrays are valid allocations */
    int *z = new int[0];
    delete[] z;
    Item *zc = new Item[0];
    delete[] zc;

    return 0;
}
