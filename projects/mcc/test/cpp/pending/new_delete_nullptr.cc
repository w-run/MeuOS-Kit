/* 缺陷记录：delete nullptr / delete[] nullptr → 运行时段错误（defect Q）
 *
 * 触发条件（worktree-mxx-work，2026-08-03 测试矩阵扩充中发现）：
 *   对空指针执行 delete / delete[] 立即段错误（exit 139）：
 *     int *p = 0; delete p;    // rc=139
 *     int *p = 0; delete[] p;  // rc=139
 *   按 C++ 标准 delete nullptr 应为 no-op（无操作）。
 *
 * 对照：new int[0]; delete[] z; 正常（q3 probe 通过）。
 *
 * 疑似根因：codegen 在 delete(ptr) 时无条件走 dtor+free，未先判空；
 *       需 src/ 实现侧确认（delete 语句的 IR 生成路径）。
 *
 * 期望修复后：本文件 main 返回 0（两个用例均通过）。
 *
 * 注意：本文件在 pending/ 目录，不接入 check-cpp-* glob（避免 CI 红）。
 *       修复后把 nullptr 断言移入 new_delete_runtime_boundary.cc 并删除本文件。
 */
int
main(void)
{
    int *p = 0;
    delete p;    /* 必须为 no-op */
    delete[] p;  /* 必须为 no-op */
    return 0;
}
