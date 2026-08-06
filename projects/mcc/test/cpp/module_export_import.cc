/* module_export_import.cc — C++20 modules: export import / 模块分区声明。
 *
 * export import 语法（re-export）允许一个模块把另一个模块的导入重新
 * 导出，让下游模块可以同时看到当前模块与被 re-export 的模块的接口。
 * 本测试覆盖：
 *  - export import OtherModule（re-export）
 *  - export 块内的 import
 *  - 模块名 + 分区声明（虽然 m++ 当前解析层尚未完全支持分区语法，但
 *    export import 解析必须正确）
 *  - 多 import 顺序不影响结果
 *  - export import 与 export 函数共存
 *
 * 期望：编译通过，运行 exit 0。
 */

export module M_ReExport;

/* 1. export import（re-export 语法） */
export import std.io;

/* 2. 普通 import（私有，不导出） */
import std.string;

/* 3. export 块内 import */
export {
    import std.vector;
    int local_helper() { return 5; }
}

/* 4. 多 import 声明 */
import std.algorithm;
import std.iterator;

/* 5. export 函数与 export import 共存 */
export int reexported_func() { return 11; }

int main() {
    if (reexported_func() != 11) return 1;
    if (local_helper() != 5) return 2;
    return 0;
}