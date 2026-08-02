/* bitfield_ops.c — C99 bit-field operations (§6.7.2.1).
 *
 * Covers:
 *  - basic unsigned bit-field read/write
 *  - signed bit-fields sign-extend on read
 *  - multiple bit-fields packed into one storage unit
 *  - bit-fields participating in arithmetic and comparisons
 *  - bit-field layout coexistence with plain members
 *  - an array of structs with bit-fields
 */
extern int puts(const char *);

struct Packed {
    unsigned lo : 4;
    unsigned mid : 8;
    unsigned hi : 4;
};

struct Sflags {
    signed neg : 4;      /* 3 value bits + sign */
    unsigned u : 4;
    int plain;
};

struct Reg {
    unsigned op : 6;
    unsigned rs : 4;
    unsigned rt : 4;
    unsigned rd : 4;
    unsigned shamt : 5;
    unsigned funct : 7;
};

int main(void) {
    /* basic unsigned bit-fields in one storage unit */
    {
        struct Packed p;
        p.lo = 15;
        p.mid = 200;
        p.hi = 9;
        if (p.lo != 15)  { puts("FAIL: lo"); return 1; }
        if (p.mid != 200){ puts("FAIL: mid"); return 2; }
        if (p.hi != 9)   { puts("FAIL: hi"); return 3; }
        p.lo = p.lo + 1;             /* wraps in 4 bits -> 0 */
        if (p.lo != 0)   { puts("FAIL: lo wrap"); return 4; }
    }

    /* signed bit-field sign extension */
    {
        struct Sflags s;
        s.neg = -3;                  /* fits in 4 bits */
        if (s.neg != -3) { puts("FAIL: signed neg"); return 5; }
        s.neg = 7;
        if (s.neg != 7)  { puts("FAIL: signed pos"); return 6; }
        s.neg = -8;                  /* min for 4-bit signed */
        if (s.neg != -8) { puts("FAIL: signed min"); return 7; }
        s.u = 15;
        if (s.u != 15)   { puts("FAIL: unsigned 15"); return 8; }
        s.u = 8;
        if (s.u != 8)    { puts("FAIL: unsigned 8"); return 9; }
        s.plain = 100;
        if (s.plain != 100) { puts("FAIL: plain mixed"); return 10; }
    }

    /* bit-fields in arithmetic and comparisons */
    {
        struct Packed p;
        p.lo = 5;
        p.mid = 10;
        if (p.lo + p.mid != 15) { puts("FAIL: add"); return 11; }
        if (p.mid * 2 != 20)    { puts("FAIL: mul"); return 12; }
        if (!(p.mid > p.lo))    { puts("FAIL: cmp"); return 13; }
        p.lo = p.mid / 2;        /* store result into another field */
        if (p.lo != 5)           { puts("FAIL: div store"); return 14; }
        if (p.mid - p.lo != 5)   { puts("FAIL: sub"); return 15; }
    }

    /* MIPS-style full register encoding across 6 bit-fields */
    {
        struct Reg r;
        r.op = 0;
        r.rs = 5;
        r.rt = 6;
        r.rd = 7;
        r.shamt = 0;
        r.funct = 32;
        if (r.rs != 5 || r.rt != 6 || r.rd != 7) { puts("FAIL: regs"); return 16; }
        if (r.funct != 32) { puts("FAIL: funct"); return 17; }
        r.funct = 63;              /* max 7-bit */
        if (r.funct != 63) { puts("FAIL: funct max"); return 18; }
    }

    /* array of structs with bit-fields */
    {
        struct Sflags arr[3];
        int i;
        for (i = 0; i < 3; i = i + 1) {
            arr[i].neg = -(i + 1);
            arr[i].u = (unsigned)(i + 2);
        }
        if (arr[0].neg != -1 || arr[2].neg != -3) { puts("FAIL: arr neg"); return 19; }
        if (arr[0].u != 2 || arr[2].u != 4)       { puts("FAIL: arr u"); return 20; }
    }

    puts("PASS");
    return 0;
}
