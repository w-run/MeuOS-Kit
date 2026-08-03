// D2 消息瑕疵回归测试：模板类方法使用不存在的成员，按需解析
// (cpp_ensure_method_defined) 时必须报错，且错误消息包含真实成员名
// （此前因错误消息用 tok.lit 而标识符文本在 tokenstr 表，报
// `struct/union has no member named '(null)'`）。
//
// 期望：编译失败，stderr 含 `no member named 'nonexistent'`。
// 复现：./m++ --specs=host -o /tmp/x test/cpp/tmpl_member_undefined.neg.cc
struct S { int x; };

template <typename T>
struct Box {
    T *p;
    Box(T *q) : p(q) {}
    void bad() { p->nonexistent(); }
};

int main() {
    S s;
    s.x = 5;
    Box<S> b(&s);
    b.bad();
    return 0;
}
