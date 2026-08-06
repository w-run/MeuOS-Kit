/* module_header_unit.neg.cc — C++20 modules: header-unit import 必须被拒绝。
 *
 * `import <vector>;` 导入标准库 header unit 是 C++20 modules 的合法语法，
 * 但 m++ 当前不支持（解析层在 `<` 处报期望 ';'）。本测试覆盖：
 *  - `import <vector>;` 必须被拒绝
 *  - `import "foo.h";` 必须被拒绝（header-unit 字符串形式）
 *
 * 期望：编译失败。
 */
import <vector>;
import "myheader.h";

int main() { return 0; }