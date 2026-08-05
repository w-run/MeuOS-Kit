/* constexpr.neg.cc — negative constexpr/static_assert tests (m++).
 *
 * Each file-scope construct below must be REJECTED by the compiler;
 * check-cpp-neg compiles this expecting failure.
 */
/* 1. static_assert with a false condition (plain constant) */
static_assert(1 == 2, "one is not two");

int main(void) {
    return 0;
}
