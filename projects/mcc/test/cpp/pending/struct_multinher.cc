/* 限制项 #7：struct 多继承顶层解析失败
 *   `struct D : A, B { ... }`（class 正常，struct 失败）
 *
 * 当前行为（worktree-mxx-work）：编译报错
 *   error: expected '(' or identifier
 *   位置：src/cpp/parse/cpp_parse.c cpp_parse_translation_unit
 *         （顶层声明循环，只对 CPP_TCLASS 分派到 cpp_class_decl）
 *
 * 根因：cpp_parse_translation_unit 仅处理
 *   - CPP_TCLASS  -> cpp_class_decl(&filescope)  ← 支持基类列表
 *   - CPP_TNAMESPACE / CPP_TUSING
 *   `struct`/`union` 落到 C 路径 decl() -> declspecs -> tagspec，
 *   tagspec（src/parse/specs.c:94）解析完 `struct D` 后遇到 `:`
 *   直接返回（tagspec 不认识基类列表），declarator 见到 `:` 报
 *   "expected '(' or identifier"。即多继承基类列表语法只在
 *   cpp_class_decl 里实现，而 struct/union 走不到那里。
 *   注意：struct 单继承 `struct D : A` 同样失败。
 *
 * 修复思路（参考 cpp_class_decl 已实现的 is_class=false 分支）：
 *   - cpp_parse_translation_unit 增加对 CPP_TSTRUCT/CPP_TUNION 的
 *     分派：若下个 token 是 `tag :`（基类列表），转交 cpp_class_decl；
 *     否则继续走 C 路径（兼容 C 风格的 struct 声明）；
 *   - cpp_class_decl 已支持 is_class=false（struct 默认 public access、
 *     基类匿名成员注册、成员函数降级），只需解决分派入口与
 *     `struct D : A` 单继承场景的 token 状态一致性。
 *
 * 期望：编译通过，main 返回 0。
 */
struct A { int x; };
struct B { int y; };
struct D : A, B {
    void set(int v) { x = v; }
};

int main(void) {
    D d;
    d.set(5);
    return d.x == 5 ? 0 : 1;
}
