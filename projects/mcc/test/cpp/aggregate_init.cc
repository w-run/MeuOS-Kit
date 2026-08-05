// C++11/20 聚合初始化回归测试：
// - 括号直接构造 `P p(1, 2)`（聚合无 ctor 时按成员序初始化）
// - 直接列表初始化 `P q{3, 4}`（声明符后 '{' 识别为初始化列表）
// - copy-list-init `P r = {5, 6}`（已有路径）
// 此前 `P p(1,2)` 报 "no matching constructor"、`P q{3,4}` 报
// "expected ',' or ';' after declarator, saw '{'"。

struct P {
    int a;
    int b;
};

struct Mix {
    int n;
    char c;
    int m;
};

struct HasCtor {
    int x;
    HasCtor() : x(0) {}
    HasCtor(int v) : x(v) {}
};

int main() {
    P p(1, 2);              /* 括号直接构造 */
    if (p.a != 1 || p.b != 2) return 1;

    P q{3, 4};              /* 直接列表初始化 */
    if (q.a != 3 || q.b != 4) return 2;

    P r = {5, 6};           /* copy-list-init */
    if (r.a != 5 || r.b != 6) return 3;

    Mix m{7, 'x', 9};
    if (m.n != 7 || m.c != 'x' || m.m != 9) return 4;

    Mix m2(1, 'a', 2);      /* 括号构造混合成员 */
    if (m2.n != 1 || m2.c != 'a' || m2.m != 2) return 5;

    HasCtor hc(42);         /* 有 ctor 的类走构造函数 */
    if (hc.x != 42) return 6;
    return 0;
}
