/* 负测试：隐式 int 声明应被 mcc 拒绝 */
/* C90 允许隐式 int，但 mcc 作为 C99 编译器拒绝 */

static x = 42;

int main()
{
    return x;
}