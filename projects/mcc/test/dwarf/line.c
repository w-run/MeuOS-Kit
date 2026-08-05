/* DWARF .debug_line regression source.  Each function begins on a distinct
 * line so the debugger can map function entry addresses to declaration lines.
 * Used by test/dwarf/line.sh to verify mcc emits a valid, parseable line
 * program (the advance_line opcode bug corrupted the stream). */
int add(int a, int b) { int s = a + b; return s; }
int sub(int a, int b) { return a - b; }
int main(void) { int x = 40; int y = 2; int r = add(x, y); return r + sub(x, y); }
