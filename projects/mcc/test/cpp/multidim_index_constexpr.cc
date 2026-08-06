/* multidim_index_constexpr.cc — C++23 多维下标：const / 运行时混合。
 *
 * 覆盖：
 *  - const 对象的多维下标（const 重载）
 *  - 多维下标写回（非 const）
 *  - 多维下标链式
 *  - const 对象多维下标值的聚合使用
 *
 * 每个 check 返回不同退出码；exit 0 = 全部通过。
 */

struct Matrix {
    int data[4];
    int operator[](int i, int j) const { return data[i * 2 + j]; }
    int &operator[](int i, int j) { return data[i * 2 + j]; }
};

int
main(void)
{
    /* 1. const 对象的 const 重载 */
    {
        const Matrix m = {10, 20, 30, 40};
        if (m[1, 0] != 30) return 1;
        if (m[0, 1] != 20) return 1;
    }
    /* 2. 非 const 对象的写回 */
    {
        Matrix m = {0, 0, 0, 0};
        m[0, 0] = 5;
        m[1, 1] = 6;
        if (m[0, 0] != 5) return 2;
        if (m[1, 1] != 6) return 2;
    }
    /* 3. 多维下标链式 */
    {
        Matrix m = {1, 2, 3, 4};
        int s = m[0, 0] + m[0, 1] + m[1, 0] + m[1, 1];
        if (s != 10) return 3;
    }
    /* 4. const 重载与非 const 重载的类型匹配 */
    {
        const Matrix cm = {100, 200, 300, 400};
        Matrix m = {100, 200, 300, 400};
        if (cm[0, 0] != m[0, 0]) return 4;
        if (cm[1, 1] != m[1, 1]) return 4;
    }
    return 0;
}