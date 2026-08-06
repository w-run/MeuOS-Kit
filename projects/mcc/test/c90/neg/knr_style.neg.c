/* 负测试：K&R 风格函数声明应被 mcc 拒绝（mcc 是 C99 编译器） */
/* mcc 不支持旧式参数声明列表 */

int add(a, b)
int a;
int b;
{
    return a + b;
}

int main()
{
    return add(1, 2);
}