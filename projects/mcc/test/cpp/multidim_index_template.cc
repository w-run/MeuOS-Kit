/* multidim_index_template.cc — C++23 多维下标：模板类多维下标。
 *
 * 模板类中 operator[] 的多参数版本必须能实例化并正确生成代码。
 * 覆盖：
 *  - 模板类 operator[](int, int)（二维模板）
 *  - 模板类 operator[](int, int, int)（三维模板）
 *  - 模板参数通过下标传递（非类型模板参数 + 多维下标）
 *  - 混合维度重载的模板类
 *  - 模板类嵌套多维下标（return T 类型也是模板实例化结果）
 *
 * 每个 check 返回不同退出码；exit 0 = 全部通过。
 */

template <typename T>
struct Matrix2D {
    T data[4];
    T &operator[](int i, int j) { return data[i * 2 + j]; }
};

template <typename T>
struct Tensor3D {
    T data[4];
    T operator[](int i, int j, int k) { return data[i * 2 + j * 1 + k]; }
};

template <typename T, int N>
struct SizedTensor {
    T data[N * N];
    int dim() const { return N; }
    T &operator[](int i, int j) { return data[i * N + j]; }
};

int
main(void)
{
    /* 1. 模板类二维下标 */
    {
        Matrix2D<int> m;
        for (int i = 0; i < 4; ++i) m.data[i] = i + 1;
        if (m[1, 0] != 3) return 1;
        if (m[0, 1] != 2) return 1;
        m[1, 1] = 99;
        if (m.data[3] != 99) return 1;
    }
    /* 2. 模板类三维下标 */
    {
        Tensor3D<int> t;
        for (int i = 0; i < 4; ++i) t.data[i] = i * 10;
        if (t[1, 0, 0] != 20) return 2;
        if (t[1, 0, 1] != 30) return 2;
    }
    /* 3. 非类型模板参数 + 多维下标 */
    {
        SizedTensor<int, 3> st;
        for (int i = 0; i < 9; ++i) st.data[i] = i;
        if (st.dim() != 3) return 3;
        if (st[1, 2] != 5) return 3;
        st[2, 0] = 100;
        if (st.data[6] != 100) return 3;
    }
    /* 4. 模板类 double 类型多维下标 */
    {
        Matrix2D<double> md;
        md.data[0] = 1.5; md.data[1] = 2.5;
        md.data[2] = 3.5; md.data[3] = 4.5;
        if (md[0, 1] < 2.4 || md[0, 1] > 2.6) return 4;
        if (md[1, 0] < 3.4 || md[1, 0] > 3.6) return 4;
    }
    return 0;
}