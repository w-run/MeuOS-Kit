/* Test C23 standard attributes */
[[nodiscard]] int must_use(void) { return 42; }

int main(void) {
    (void)must_use();
    return 0;
}
