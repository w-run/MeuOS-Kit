/* i386 i64 memory load: high-half address must be emit_addr_str-built
 * (off+4 against the real base), never the old `snprintf("%d+", ...)` that
 * produced the malformed `movl 4+, %eax`.  Regression for the emit_load i64
 * branch in i386_memit.c. */
long long g = 0x1122334455667788LL;

long long load_g(void) { return g; }

int main(void) {
    if (load_g() != 0x1122334455667788LL) return 1;
    return 0;
}
