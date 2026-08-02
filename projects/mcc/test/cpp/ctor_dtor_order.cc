/* ctor_dtor_order.cc — construction/destruction ordering (m++ end-to-end).
 *
 * Covers C.2.3 object lifetime ordering:
 *  - multi-level inheritance construction order: base → derived
 *  - local objects construct at their declaration point
 *  - objects in an inner block are destroyed when the block exits, in
 *    reverse construction order (innermost block first, reverse order)
 *  - locals destroyed before `main` returns
 *
 * Events are recorded into a global log array and asserted in one place.
 * NOTE: derived-class dtors currently do NOT invoke base-class dtors in
 * m++ (see test/cpp/pending/ctor_base_dtor.cc); this test only asserts
 * what is implemented (the derived dtor itself runs).
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
extern int printf(const char *, ...);

static int g_log[32];
static int g_n = 0;
static void mark(int id) { g_log[g_n++] = id; }

class Base {
public:
    Base()  { mark(1); }
    ~Base() { mark(100); }
};
class Mid : public Base {
public:
    Mid()  { mark(2); }
    ~Mid() { mark(200); }
};
class Leaf : public Mid {
public:
    Leaf()  { mark(3); }
    ~Leaf() { mark(300); }
};
class Lone {
public:
    int id;
    Lone(int v) { id = v; mark(id); }
    ~Lone()     { mark(1000 + id); }
};

int main(void) {
    /* multi-level construction: Base(1) Mid(2) Leaf(3) */
    Leaf l;
    if (g_n != 3) return 1;
    if (g_log[0] != 1 || g_log[1] != 2 || g_log[2] != 3) return 2;

    /* inner block: Lone(10) then Lone(11); destroyed reverse on exit */
    {
        Lone a(10);
        Lone b(11);
        if (g_n != 5) return 3;
        if (g_log[3] != 10 || g_log[4] != 11) return 4;
    }
    if (g_n != 7) return 5;
    if (g_log[5] != 1011 || g_log[6] != 1010) return 6;

    /* a second inner block reuses ids */
    {
        Lone c(12);
    }
    if (g_n != 9) return 7;
    if (g_log[7] != 12 || g_log[8] != 1012) return 8;

    /* inherited object dtor runs on block exit: mark(300) only — the
     * base/mid dtors are not invoked yet (see pending/ctor_base_dtor.cc);
     * update this assertion when the dtor chain is fixed. */
    {
        Leaf ll;
        if (g_n != 12) return 9;
        if (g_log[9] != 1 || g_log[10] != 2 || g_log[11] != 3) return 10;
    }
    if (g_n != 13) return 11;
    if (g_log[12] != 300) return 12;

    return 0;
}
