/* module_import_bad.neg.cc — C++20 modules: 非法 import 语法必须被拒绝。
 *
 * C++20 中 import 声明要求模块名是一个标识符（或标识符序列），数字
 * 字面量不可作为模块名。本测试覆盖：
 *  - import 后跟数字
 *  - import 后跟字符串字面量（非 header-unit 形式）
 *  - import 后跟符号
 *
 * 期望：编译失败。
 */
import 123;           /* must be rejected: number is not a module-name */
import "bad";         /* must be rejected: string not allowed */
import +;             /* must be rejected: operator not allowed */

int main() { return 0; }