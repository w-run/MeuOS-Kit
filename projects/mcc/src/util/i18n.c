/* i18n.c — 轻量双语消息目录（en/zh）。
 *
 * 设计：不做 gettext 依赖。目录是一张"英文源串 -> 中文译文"的静态
 * 表，key 与编译器源码中的 printf 格式串完全一致（占位符 %s/%d 两侧
 * 必须对应）。msg_tr() 在诊断输出点翻译格式串，未收录条目原样返回
 * 英文——410 条诊断串无需全部覆盖，未翻译的优雅降级为 en。
 *
 * 结构词（error:/warning:/note:）与 --explain 修复建议也走同一套
 * 语言选择，保证 --lang=en 时 hint 也为英文、--lang=zh 时正文为中文。
 */
#include <string.h>

#include "i18n.h"
#include "util.h"

int g_msg_lang;   /* 0=en（默认），1=zh */

/* ---- 诊断正文格式串目录（en -> zh） ------------------------------- */
/* 覆盖常见编程错误；新增翻译直接在这里加一行。占位符必须一致。 */
static const struct entry {
	const char *en;
	const char *zh;
} catalog[] = {
	{ "undeclared identifier: %s", "未声明的标识符：%s" },
	{ "expected %s %s, saw %s", "期望 %s %s，但看到 %s" },
	{ "expected declaration or function definition",
	  "期望声明或函数定义" },
	{ "unexpected ';' at top-level", "顶层出现意外的 ';'" },
	{ "expected ';' after expression", "表达式后缺少 ';'" },
	{ "expected ';' after declaration", "声明后缺少 ';'" },
	{ "expected ')' after expression", "表达式后缺少 ')'" },
	{ "expected ')'", "缺少 ')'" },
	{ "expected '}'", "缺少 '}'" },
	{ "expected identifier", "期望标识符" },
	{ "assignment to arithmetic type must be from arithmetic type",
	  "赋值给算术类型时，右值必须是算术类型" },
	{ "assignment to struct type must be from compatible type",
	  "赋值给结构体类型时，右值必须是兼容类型" },
	{ "cannot store to 'const' object", "不能写入 'const' 对象" },
	{ "invalid operands to '%s' operator", "'%s' 运算符的操作数无效" },
	{ "invalid operands to '+' operator", "'+' 运算符的操作数无效" },
	{ "invalid operands to '-' operator", "'-' 运算符的操作数无效" },
	{ "function '%s' redeclared with incompatible return type",
	  "函数 '%s' 重声明时返回类型不兼容" },
	{ "'%s' redeclared with incompatible type",
	  "'%s' 重声明时类型不兼容" },
	{ "%s '%s' redeclared with incompatible type",
	  "%s '%s' 重声明时类型不兼容" },
	{ "'%s' redeclared with different kind", "'%s' 以不同种类重声明" },
	{ "redeclaration of tag '%s' with different kind",
	  "标签 '%s' 以不同种类重声明" },
	{ "not enough arguments for function call", "函数调用实参不足" },
	{ "too many arguments for function call", "函数调用实参过多" },
	{ "warning treated as error", "警告已被视为错误" },
	{ "redefinition of '%s'", "'%s' 重复定义" },
	{ "redefinition of class '%s'", "类 '%s' 重复定义" },
	{ "redefinition of member function '%s'", "成员函数 '%s' 重复定义" },
	{ "redefinition of member '%s'", "成员 '%s' 重复定义" },
	{ "redefinition of tag '%s'", "标签 '%s' 重复定义" },
	{ "'%s' is not accessible from this context (member is private/protected)",
	  "'%s' 在此上下文中不可访问（成员为 private/protected）" },
	{ "no member named '%s' in namespace '%s'",
	  "命名空间 '%s' 中没有名为 '%s' 的成员" },
	{ "struct/union has no member named '%s'",
	  "struct/union 没有名为 '%s' 的成员" },
	{ "%s has no member named '%s'", "%s 没有名为 '%s' 的成员" },
	{ "'%s' is not a class type", "'%s' 不是类类型" },
	{ "'%s' is not a namespace", "'%s' 不是命名空间" },
	{ "'%s' is not a class or namespace", "'%s' 不是类或命名空间" },
	{ "template '%s' has too many parameters", "模板 '%s' 的参数过多" },
	{ "template member '%s' has too many parameters",
	  "模板成员 '%s' 的参数过多" },
	{ "too many arguments for template '%s'", "模板 '%s' 的实参过多" },
	{ "too many template arguments for class template '%s'",
	  "类模板 '%s' 的模板实参过多" },
	{ "too few template arguments for class template '%s'",
	  "类模板 '%s' 的模板实参不足" },
	{ "too many template arguments", "模板实参过多" },
	{ "too many explicit template arguments", "显式模板实参过多" },
	{ "expected template argument", "期望模板实参" },
	{ "template argument too long", "模板实参过长" },
	{ "template argument list too long", "模板实参列表过长" },
	{ "initializer is not a constant expression", "初始化器不是常量表达式" },
	{ "initializer specified for incomplete type",
	  "对不完整类型指定了初始化器" },
	{ "controlling expression of loop must have scalar type",
	  "循环控制表达式必须是标量类型" },
	{ "controlling expression of if statement must have scalar type",
	  "if 语句控制表达式必须是标量类型" },
	{ "expression is not an object", "表达式不是对象" },
	{ "expected primary expression", "期望主表达式" },
	{ "expected expression in #if directive", "#if 指令中期望表达式" },
	{ "unexpected end of file in statement", "语句中意外到达文件末尾" },
	{ "expected '::' after namespace name", "命名空间名称后期望 '::'" },
	{ "expected name after '::'", "'::' 后期望名称" },
	{ "expected member name after '::'", "'::' 后期望成员名称" },
	{ "expected type name after '::'", "'::' 后期望类型名称" },
	{ "cast operand must have scalar type", "转换操作数必须是标量类型" },
	{ "array element has incomplete type", "数组元素类型不完整" },
	{ "object '%s' has incomplete type", "对象 '%s' 类型不完整" },
	{ "identifier '%s' is not an object or function",
	  "标识符 '%s' 不是对象或函数" },
	{ "operand of unary '+' operator must have arithmetic type",
	  "一元 '+' 运算符的操作数必须是算术类型" },
	{ "operand of unary '-' operator must have arithmetic type",
	  "一元 '-' 运算符的操作数必须是算术类型" },
	{ "operand of '~' operator must have integer type",
	  "'~' 运算符的操作数必须是整数类型" },
	{ "operands of conditional operator must have compatible types",
	  "条件运算符的两个操作数必须是兼容类型" },
	{ "operands to '%s' operator must be arithmetic",
	  "'%s' 运算符的操作数必须是算术类型" },
	{ "operands to '%s' operator must be integer",
	  "'%s' 运算符的操作数必须是整数类型" },
	{ "operands to '%%' operator must be integer",
	  "'%%' 运算符的操作数必须是整数" },
	{ "left operand of '%s' operator must be scalar",
	  "'%s' 运算符的左操作数必须是标量" },
	{ "right operand of '%s' operator must be scalar",
	  "'%s' 运算符的右操作数必须是标量" },
	{ "operand of '%s' operator must be an lvalue",
	  "'%s' 运算符的操作数必须是左值" },
	{ "operand of '%s' operator is const qualified",
	  "'%s' 运算符的操作数带有 const 限定" },
	{ "'%s' operator must be applied to pointer to struct/union",
	  "'%s' 运算符必须应用于 struct/union 指针" },
	{ "class type has no matching operator[]",
	  "类类型没有匹配的 operator[]" },
	{ "no matching constructor for 'new %s'", "'new %s' 没有匹配的构造函数" },
	{ "no matching constructor for object '%s'",
	  "对象 '%s' 没有匹配的构造函数" },
	{ "no matching constructor for '%s' in initializer list",
	  "初始化列表中的 '%s' 没有匹配的构造函数" },
	{ "if constexpr condition is not a constant expression",
	  "if constexpr 条件不是常量表达式" },
	{ "expected '(' or identifier", "期望 '(' 或标识符" },
	{ "expected ',' or '}' after initializer", "初始化器后缺少 ',' 或 '}'" },
	{ "expected ')' in #if expression", "#if 表达式缺少 ')'" },
	{ "expected ')' after 'alignof'", "'alignof' 后缺少 ')'" },
	{ "expected identifier after '%s' operator",
	  "'%s' 运算符后缺少标识符" },
	{ "expected declaration in namespace body", "命名空间体中期望声明" },
	{ "'new' outside of a function body is not supported",
	  "不支持在函数体外使用 'new'" },
	{ "va_arg with non-scalar type is not yet supported",
	  "暂不支持非标量类型的 va_arg" },
	{ "atomic load currently requires a scalar type up to 64 bits",
	  "atomic load 当前要求不超过 64 位的标量类型" },
	{ "atomic store currently requires a scalar type up to 64 bits",
	  "atomic store 当前要求不超过 64 位的标量类型" },
	{ "bit-field initializer is not an integer constant expression",
	  "位域初始化器不是整型常量表达式" },
	{ "base class '%s' has incomplete type", "基类 '%s' 类型不完整" },
	{ "struct member '%s' has incomplete type", "结构体成员 '%s' 类型不完整" },
	{ "pointer operands to '%s' operator are to incompatible types",
	  "'%s' 运算符的指针操作数指向不兼容类型" },
	{ "ignoring return value of nodiscard function '%s'",
	  "忽略 nodiscard 函数 '%s' 的返回值" },
};

static const char *
tr(const char *s)
{
	if (g_msg_lang != 1 || !s)
		return s;
	for (size_t i = 0; i < countof(catalog); i++)
		if (strcmp(catalog[i].en, s) == 0)
			return catalog[i].zh;
	return s;
}

const char *
msg_tr(const char *fmt)
{
	return tr(fmt);
}

/* ---- fix-it 提示目录 ------------------------------------------------ */
static const struct entry fix_catalog[] = {
	{ "add ';' here", "在此处添加 ';'" },
};

const char *
msg_tr_fix(const char *fix)
{
	if (g_msg_lang != 1 || !fix)
		return fix;
	for (size_t i = 0; i < countof(fix_catalog); i++)
		if (strcmp(fix_catalog[i].en, fix) == 0)
			return fix_catalog[i].zh;
	return fix;
}

/* ---- 诊断结构词 ----------------------------------------------------- */
const char *
msg_word_error(void)
{
	return g_msg_lang == 1 ? "错误:" : "error:";
}

const char *
msg_word_warning(void)
{
	return g_msg_lang == 1 ? "警告:" : "warning:";
}

const char *
msg_word_note(void)
{
	return g_msg_lang == 1 ? "提示:" : "note:";
}

/* ---- --explain 修复建议（分类匹配 + 双语） -------------------------- */
struct explain_entry {
	const char *key;   /* strstr 分类 key（匹配 en 源串） */
	const char *en;    /* en 提示 */
	const char *zh;    /* zh 提示 */
};

static const struct explain_entry explain_catalog[] = {
	{ "implicit declaration",
	  "  (hint: include the header declaring this function, or provide a prototype)",
	  "  (建议: 包含声明该函数的头文件，或提供原型)" },
	{ "unused",
	  "  (hint: to keep it deliberately, add (void)var; or __attribute__((unused)))",
	  "  (建议: 若有意保留，可加 (void)var; 或 __attribute__((unused)))" },
	{ "type",
	  "  (hint: check the types match, or add an explicit cast)",
	  "  (建议: 检查类型是否匹配，或显式转换)" },
	{ "nodiscard",
	  "  (hint: use the return value, or discard it with (void))",
	  "  (建议: 使用返回值，或用 (void) 显式丢弃)" },
};

const char *
msg_explain(const char *fmt)
{
	if (!fmt)
		return NULL;
	for (size_t i = 0; i < countof(explain_catalog); i++)
		if (strstr(fmt, explain_catalog[i].key))
			return (g_msg_lang == 1) ? explain_catalog[i].zh
			                     : explain_catalog[i].en;
	return NULL;
}
