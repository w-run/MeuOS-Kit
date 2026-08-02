/* empty_class_value.cc — 空类（无数据成员）按值传参/返回（缺陷 U 回归）。
 *
 * 缺陷 U（2026-08-03 修复，双根因）：
 *  A. 运行期 SEGV：`int pass(Empty e)` 被调用时，caller 对空类局部变量
 *     的 alloca 槽在 coalesce 里被判"死槽"（槽无数据位、s->m==0，
 *     Oargc 的整对象读不扩展活跃区间），被 kill 后 Oargc 源指针折叠成
 *     CON_Z（地址 0），`movl 0, %edi` 读地址 0 崩溃。
 *  B. 编译期 SEGV：`Empty make() { ... return e; }` 被调用时，空类聚合
 *     返回的 AClass.cls[] 停留在 Kx（无字段可分类），retr() 用
 *     KBASE(Kx)=-1 索引 retreg[-1]（越界写坏内存）崩溃。
 *
 * 修复：
 *  - src/opt/mem.c load()：槽只要被读（Oargc/Jretc 整对象读）即扩展
 *    活跃区间，避免被 coalesce 误杀；
 *  - src/target/x86_64/x86_64_sysv.c typclass()：残留 Kx 的 eightbyte
 *    归一为 INTEGER（Kl），符合 SysV psABI。
 *
 * 对照语义：空类 sizeof==1，传参/返回的副本内容是未定义字节，但拷贝
 * 本身必须安全（地址有效），后续实参不得错位，返回值可正常丢弃或再传。
 *
 * 每个检查返回不同退出码；退出 0 = 全部通过。
 */
class Empty { };
class FuncOnly { int f(int x) { return x + 1; } };
class StaticOnly { static int s; };

int pass(Empty a, FuncOnly b, StaticOnly c, int n) { return n; }
int pass2(int n, Empty a) { return n * 2; }

Empty make(void) { Empty e; return e; }
FuncOnly make2(void) { FuncOnly f; return f; }

/* 空类参数 + 空类返回值复合：取回后原样传回 */
Empty id(Empty e) { return e; }

int main(void) {
    Empty e;
    FuncOnly fo;
    StaticOnly so;

    /* 空类打头 + 实参在后的寄存器排布（回归面 A） */
    if (pass(e, fo, so, 7) != 7) return 1;
    /* 实参打头 + 空类在后（回归面 A 的错位防护） */
    if (pass2(21, e) != 42) return 2;
    /* 空类返回（回归面 B：编译期曾崩） */
    Empty r = make();
    (void)r;
    /* 仅成员函数/仅静态成员类同样触发缺陷 U（无数据成员即触发） */
    FuncOnly r2 = make2();
    (void)r2;
    /* 空类入参原样返回再传 */
    Empty r3 = id(e);
    if (pass2(5, r3) != 10) return 3;

    return 0;
}
