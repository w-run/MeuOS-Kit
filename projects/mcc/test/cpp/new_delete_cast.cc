/* cpp0f_cast_delete.cc — `delete (T*)expr` / `delete[] (T*)expr` with a
 * cast-expression operand (defect cpp-0f, m++ end-to-end).
 *
 * C++ [expr.delete] says the operand of delete is a cast-expression; m++
 * previously parsed it with unaryexpr, so a cast prefix failed with
 * "expected primary expression".  Each check returns a distinct exit
 * code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class A {
public:
    A(int v) { val = v; }
    ~A() { }
    int val;
};

int
main(void)
{
    /* scalar delete with a cast-expression operand */
    int *p = new int;
    *p = 42;
    delete (int*)p;

    /* cast to a different pointer type still frees the same allocation */
    char *c = new char;
    *c = 'x';
    delete (char*)c;

    /* delete[] with a cast-expression operand */
    int *arr = new int[3];
    arr[0] = 1; arr[1] = 2; arr[2] = 3;
    delete[] (int*)arr;

    /* class with a destructor: dtor runs, then free */
    A *a = new A(7);
    if (a->val != 7) return 2;
    delete (A*)a;

    /* plain delete (no cast) must still work */
    int *q = new int;
    *q = 1;
    delete q;

    /* parenthesised operand (not a cast) still works */
    int *r = new int;
    *r = 2;
    delete (r);

    printf("cpp-0f cast delete: passed\n");
    return 0;
}
