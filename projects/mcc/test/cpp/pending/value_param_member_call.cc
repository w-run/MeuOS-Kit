/* 缺陷记录：size-0 类（无数据成员）按值传参 → 运行时段错误
 *
 * 触发条件（worktree-mxx-work，2026-08-02 测试矩阵扩充中发现）：
 *   任何「没有数据成员」的类（空类、只含成员函数的类、只含重载成员
 *   函数的类）作为值参数传入函数，调用点即段错误（exit 139）：
 *     class Empty { };                 // 空类
 *     int pass(Empty e) { return 1; }
 *     int run(Calc obj, int v) { return obj.f(v); }  // Calc 无数据成员
 *   - 只要类里有 ≥1 个数据成员（如 int m），值传正常。
 *   - 引用传参（Calc &obj）不受影响。
 *
 * 疑似根因：size-0 类的按值传参 ABI/拷贝路径缺陷（栈上无实际载荷，
 *       参拷贝/this 地址计算异常）。需 src/ 实现侧确认（C 前端
 *       结构体按值传参逻辑或 C++ 参数传递降级路径）。
 *
 * 期望修复后：本文件 main 返回 0（三个用例均通过）。
 */
class Empty { };                        /* 空类 */
class FuncOnly {                        /* 只有非重载成员函数 */
public:
    int f(int a) { return a + 1; }
};
class OvldOnly {                        /* 只有重载成员函数 */
public:
    int f(int a) { return a + 1; }
    int f(int a, int b) { return a + b; }
};

int pass(Empty e) { return 1; }
int runF(FuncOnly obj, int v) { return obj.f(v); }
int runO(OvldOnly obj) { return obj.f(5); }

int main(void) {
    Empty e;
    if (pass(e) != 1) return 1;         /* 空类值传 */
    FuncOnly fc;
    if (runF(fc, 5) != 6) return 2;     /* 无字段类 + 非重载成员 */
    OvldOnly oc;
    if (runO(oc) != 6) return 3;        /* 无字段类 + 重载成员 */
    return 0;
}
