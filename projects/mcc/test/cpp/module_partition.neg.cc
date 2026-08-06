/* module_partition.neg.cc — C++20 modules: 模块分区语法必须被拒绝。
 *
 * C++20 允许 `export module M:part1` 声明模块分区，但 m++ 解析层当前
 * 不接受模块分区中的冒号（`:`）。本测试验证该诊断。
 *
 * 期望：编译失败，错误码 E0001（期望 ';' after module declaration）。
 */
export module M:part1;

int helper() { return 11; }
int main() { return helper(); }