/* placement_new_arity.neg.cc — placement new must respect the target
 * class's constructor overload set.
 *
 * `new (ptr) T(args)` runs ordinary overload resolution on T's ctors;
 * supplying the wrong number of arguments is a compile error
 * ("no matching constructor for 'new (ptr) P'"), exactly like a stack
 * declaration would be.  This pins that placement new does not bypass
 * ctor checking just because no allocation happens.
 */
class P {
public:
    P(int a, int b) { v = a + b; }
    int v;
};

int
main(void)
{
    char buf[sizeof(P)];
    P *p = new (buf) P(1);   /* one argument for a two-argument ctor */
    return p->v;
}
