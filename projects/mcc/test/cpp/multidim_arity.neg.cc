/* multidim_arity.neg.cc — C++23 多维下标参数数量不匹配负向。
 *
 * 如果 operator[] 声明接收 N 个参数，但调用时给了 M ≠ N 个参数，
 * 编译器必须拒绝并给出诊断（而非尝试退化到单参数或内置数组）。
 *
 * 覆盖：
 *  - operator[](int, int) 被以 1 个参数调用
 *  - operator[](int) 被以 2 个参数调用
 *  - operator[](int) 被以 0 个参数调用
 *  - operator[](int, int, int) 被以 2 个参数调用
 *
 * 期望：编译失败。
 */
struct M2 {
    int data[4];
    int operator[](int i, int j) { return data[i * 2 + j]; }
};

struct M1 {
    int data[2];
    int operator[](int i) { return data[i]; }
};

struct M3 {
    int data[8];
    int operator[](int i, int j, int k) { return data[i * 4 + j * 2 + k]; }
};

int main() {
    M2 m2;
    m2[1];            /* 1 arg, expected 2 → must reject */
    M1 m1;
    m1[1, 2];         /* 2 args, expected 1 → must reject */
    m1[];             /* 0 args, expected 1 → must reject */
    M3 m3;
    m3[1, 2];         /* 2 args, expected 3 → must reject */
    return 0;
}