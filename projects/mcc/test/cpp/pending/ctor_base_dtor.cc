/* 缺陷记录：派生类析构不自动调用基类析构（继承析构链缺失）
 *
 * 当前行为（worktree-mxx-work，2026-08-02 测试矩阵扩充中发现）：
 *   继承链中派生类析构函数运行后，基类析构函数不被调用：
 *     class Base { ~Base() {...} };
 *     class Der : public Base { ~Der() {...} };
 *     main 中 Der d;  → 输出 D+ B+ D-，缺少 B-。
 *   构造链正常（Base+ → Der+，顺序正确）；析构链只跑最派生层。
 *
 * 已确认正常路径（避免误判为回归）：
 *   - 全局对象析构（global_dtor.cc，经 .fini_array）正常；
 *   - 非继承类析构正常（ctor_dtor_order.cc 的 Lone）。
 *
 * 根因：需 src/ 实现侧定位（疑似派生类析构函数尾部未合成
 *       基类析构调用）。
 *
 * 期望修复后：本文件 main 返回 0，且输出顺序为 B+ D+ B- D-。
 */
extern int printf(const char *, ...);

class Base {
public:
    Base()  { printf("B+\n"); }
    ~Base() { printf("B-\n"); }
};
class Der : public Base {
public:
    Der()  { printf("D+\n"); }
    ~Der() { printf("D-\n"); }
};

int main(void) {
    Der d;
    printf("done\n");
    return 0;
}
