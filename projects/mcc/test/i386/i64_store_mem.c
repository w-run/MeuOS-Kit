/* i386 i64 memory store: the high-half (addr+4) must be built via
 * emit_addr_str so any base/index/offset carries over.  The old code did
 * snprintf("%lld", a.off+4) for a non-register base -> a bare `movl %eax, 4`
 * (no base register), corrupting the high half.  Struct field forces a
 * non-REG base at emit time, exercising that path. */
struct S { int a; long long b; int c; };
struct S gs;

void setb(long long v) { gs.b = v; }

int main(void) {
    setb(0x1122334455667788LL);
    return (gs.b == 0x1122334455667788LL) ? 0 : 1;
}
