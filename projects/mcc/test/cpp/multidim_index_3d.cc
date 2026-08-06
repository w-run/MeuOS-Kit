/* multidim_index_3d.cc — C++23 多维下标：三维及混合维。
 *
 * multidim_index.cc 已覆盖 2 参数 operator[]。本测试扩展到：
 *  - 3 参数 operator[](int, int, int)（三维）
 *  - 4 参数 operator[](int, int, int, int)（四维）
 *  - 混合：operator[](int, int) 和 operator[](int, int, int) 重载
 *  - 通过下标写回（write-through）三维
 *
 * 每个 check 返回不同退出码；exit 0 = 全部通过。
 */

struct Cube {
    int data[8];
    int &operator[](int i, int j, int k) {
        return data[i * 4 + j * 2 + k];
    }
};

struct HyperCube {
    int data[16];
    int &operator[](int a, int b, int c, int d) {
        return data[(a * 8) + (b * 4) + (c * 2) + d];
    }
};

struct Mixed {
    int data[12];
    int operator[](int i, int j) { return data[i * 2 + j]; }
    int operator[](int i, int j, int k) { return data[i * 6 + j * 2 + k]; }
};

int
main(void)
{
    /* 1. 三维下标读取 */
    {
        Cube c;
        int val = 0;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                for (int k = 0; k < 2; ++k)
                    c.data[val++] = i * 100 + j * 10 + k;
        if (c[1, 0, 1] != 101) return 1;
        if (c[0, 1, 0] != 10) return 1;
    }
    /* 2. 三维下标写回 */
    {
        Cube c;
        for (int i = 0; i < 8; ++i) c.data[i] = i;
        c[1, 1, 1] = 99;
        if (c.data[7] != 99) return 2;
    }
    /* 3. 四维下标 */
    {
        HyperCube h;
        for (int i = 0; i < 16; ++i) h.data[i] = i * 10;
        if (h[1, 0, 1, 0] != 100) return 3;
        if (h[0, 1, 1, 0] != 60) return 3;
    }
    /* 4. 混合重载：2 参数 vs 3 参数 */
    {
        Mixed m;
        for (int i = 0; i < 12; ++i) m.data[i] = i + 1;
        if (m[0, 1] != 2) return 4;  /* 2-arg path */
        if (m[1, 0, 1] != 8) return 4;  /* 3-arg path */
    }
    return 0;
}