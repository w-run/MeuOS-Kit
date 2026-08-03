/* tmpl_nested_depth.cc — nested class-template instantiation depth (m++).
 *
 * template.cc covers a single instantiation level; this file pins the
 * recursive-instantiation boundary: `Wrap<Wrap<Wrap<Wrap<int> > > >`
 * forces four *distinct* instantiations of the same class template, each
 * one used as the template argument of the next, so the instantiation
 * cache must key on the fully-substituted argument type rather than on
 * the template name.
 *
 * Covers:
 *  - four nesting levels of the same class template, member access chained
 *    all the way down (`w4.v.v.v.v`)
 *  - a member function instantiated at each level (`sizeof(T)` differs per
 *    level only through the argument type, so the layout must be right)
 *  - a second template (Pair/SumPair) instantiated with a nested type
 *    argument; member bodies are compiled eagerly, so `Pair` carries no
 *    members that would be ill-formed for its argument type
 *  - cache reuse: the same specialization named twice yields one type
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
template <typename T> struct Wrap {
    T v;
    int tag() { return sizeof(T); }
};

template <typename T> struct Pair {
    T a;
    T b;
};

/* sum lives in a separate template: the instantiation of `Pair` must not
 * compile member bodies that are meaningless for its argument type. */
template <typename T> struct SumPair {
    T a;
    T b;
    T sum() { return a + b; }
};

int
main(void)
{
    Wrap<int> w1;                       w1.v = 5;
    Wrap<Wrap<int> > w2;                w2.v.v = 7;
    Wrap<Wrap<Wrap<int> > > w3;         w3.v.v.v = 9;
    Wrap<Wrap<Wrap<Wrap<int> > > > w4;  w4.v.v.v.v = 11;

    /* chained member access through every nesting level */
    if (w1.v != 5) return 1;
    if (w2.v.v != 7) return 2;
    if (w3.v.v.v != 9) return 3;
    if (w4.v.v.v.v != 11) return 4;

    /* member functions instantiated per level; every level wraps an int */
    if (w1.tag() != 4) return 5;
    if (w2.tag() != 4) return 6;
    if (w3.tag() != 4) return 7;
    if (w4.tag() != 4) return 8;

    /* the whole nest is still int-sized (single member per level) */
    if (sizeof(w4) != sizeof(int)) return 9;

    /* a second template, plus one instantiated over a nested type */
    SumPair<double> pd; pd.a = 1.5; pd.b = 2.25;
    if (pd.sum() != 3.75) return 10;

    Pair<Wrap<int> > pw;
    pw.a.v = 3; pw.b.v = 4;
    if (pw.a.v + pw.b.v != 7) return 11;

    /* cache reuse: naming Wrap<int> again must yield the same type */
    Wrap<int> w5; w5 = w1;
    if (w5.v != 5) return 12;

    return 0;
}
