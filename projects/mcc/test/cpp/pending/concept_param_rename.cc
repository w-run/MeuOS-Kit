/* 缺陷记录：concept 体内形参名 ≠ `T` 时被误判 undeclared（defect R）
 *
 * 触发条件（worktree-mxx-work，2026-08-03 测试矩阵扩充中发现）：
 *   概念定义的模板形参若不叫 `T`，概念体里对它的引用被判 undeclared：
 *     template <typename X> concept Four = sizeof(X) == 4;
 *     → error: undeclared identifier: X
 *   对照：template <typename T> concept Four = sizeof(T) == 4;  正常
 *   （见 test/cpp/concepts.cc 的 FourByte）。
 *
 * 连带影响：多模板参数约束函数无法用第二个形参名，例如
 *     template <typename A, typename B> requires Four<A> int mix(A, B);
 *   要求概念形参也叫 A 才能通过（形参名耦合）。
 *
 * 疑似根因：概念体折叠器用硬编码 `T`（或固定映射）替代形参名，未按
 *       `concept C = body` 自身的模板参数表查实际名。
 *
 * 期望修复后：本文件 main 返回 0（两种形参命名均通过）。
 *
 * 注意：本文件在 pending/ 目录，不接入 check-cpp-* glob（避免 CI 红）。
 *       修复后把断言移入 concepts_combo_boundary.cc（去掉其中的 #if 0）
 *       并删除本文件。
 */
template <typename X> concept FourX = sizeof(X) == 4;
template <typename T> requires FourX<T> int viaX(T a) { return (int)a + 2; }

template <typename A, typename B> requires FourX<A> int mix(A a, B b) {
    return (int)a + (int)b;
}

int
main(void)
{
    if (viaX(40) != 42) return 1;
    if (mix(40, 2) != 42) return 2;
    return 0;
}
