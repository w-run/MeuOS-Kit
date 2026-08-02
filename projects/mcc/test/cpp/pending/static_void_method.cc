/* 缺陷记录：static void 方法通过 Class::method() 调用被误判为构造
 *
 * 当前行为（worktree-mxx-work，2026-08-02 测试矩阵扩充中发现）：
 *   返回类型为 void 的静态方法，以 Class::method(...) 形式调用时报
 *   编译错误：
 *     class Bank {
 *     public:
 *         static int balance;
 *         static void deposit(int v) { balance = balance + v; }
 *     };
 *     Bank::deposit(50);   → error: no matching constructor for object 'deposit'
 *   「deposit」被当作对象构造。
 *
 * 已确认正常路径（避免误判为回归）：
 *   - static 方法返回 int/double 等非 void → Class::method() 调用正常
 *     （static_member.cc 的 Counter::next / Bank::deposit(int 返回)）；
 *   - static 数据成员读写正常。
 *
 * 根因：需 src/ 实现侧定位（疑似「返回 void 的 static 方法」在
 *       Class:: 调用点走错分支：非 void 时正常，void 时被当作
 *       构造/对象创建解析）。
 *
 * 期望修复后：本文件 main 返回 0。
 */
extern int puts(const char *);

class Bank {
public:
    static int balance;
    static void deposit(int v) { balance = balance + v; }
    static int balance_of() { return balance; }
};
int Bank::balance = 100;

int main(void) {
    Bank::deposit(50);            /* 复现点：应执行而非报构造错误 */
    if (Bank::balance_of() != 150) return 1;
    return 0;
}
