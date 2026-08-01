/* mir_test.c — MIR core unit tests.
 *
 * Verifies:
 *   1. mtype_info size/align/is* tables
 *   2. aggregate construction (struct/union/array)
 *   3. function/block/value/instruction construction
 *   4. constant pool deduplication
 *   5. -dmir style dump
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"

static int failures;

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
		failures++; \
	} \
} while (0)

static void test_types(void)
{
	CHECK(mtypesize(MT_I8) == 1, "i8 size");
	CHECK(mtypesize(MT_I16) == 2, "i16 size");
	CHECK(mtypesize(MT_I32) == 4, "i32 size");
	CHECK(mtypesize(MT_I64) == 8, "i64 size");
	CHECK(mtypesize(MT_F32) == 4, "f32 size");
	CHECK(mtypesize(MT_F64) == 8, "f64 size");
	CHECK(mtypesize(MT_PTR) == 8, "ptr size");
	CHECK(mtypesize(MT_AGG) == -1, "agg size is -1");
	CHECK(mtypealign(MT_I64) == 8, "i64 align");
	CHECK(mtypealign(MT_PTR) == 8, "ptr align");
	CHECK(mtypeisint(MT_I32), "i32 is int");
	CHECK(mtypeisint(MT_PTR), "ptr is int");
	CHECK(mtypeisfloat(MT_F64), "f64 is float");
	CHECK(!mtypeisfloat(MT_I32), "i32 not float");
	CHECK(mtypeisptr(MT_PTR), "ptr is ptr");
	CHECK(!mtypeisptr(MT_I64), "i64 not ptr");
}

static void test_aggregates(void)
{
	/* struct Point { int x; double y; } */
	MTypeDesc *pt = mtd_new("Point", false);
	mtd_add_field(pt, "x", MT_I32, 0, 0, -1, 0);
	mtd_add_field(pt, "y", MT_F64, 0, 4, -1, 0);
	mtd_finalize(pt);
	CHECK(pt->nfield == 2, "Point has 2 fields");
	CHECK(pt->size == 16, "Point size 16");
	CHECK(pt->align == 8, "Point align 8");
	CHECK(pt->field[0].type == MT_I32, "field0 is i32");
	CHECK(pt->field[1].type == MT_F64, "field1 is f64");
	CHECK(pt->field[1].offset == 4, "field1 offset 4");

	/* union U { int i; char c; } */
	MTypeDesc *un = mtd_new("U", true);
	mtd_add_field(un, "i", MT_I32, 0, 0, -1, 0);
	mtd_add_field(un, "c", MT_I8, 0, 0, -1, 0);
	mtd_finalize(un);
	CHECK(un->is_union, "U is union");
	CHECK(un->size == 4, "union size 4");
	CHECK(un->field[1].offset == 0, "union field offset 0");

	/* array of Point[3] */
	MTypeDesc *arr = mtd_array(pt, 3);
	mtd_finalize(arr);
	CHECK(arr->is_array, "arr is array");
	CHECK(arr->size == 48, "array size 48");
	CHECK(arr->align == 8, "array align 8");

	free(arr); free(un); free(pt);
}

static void test_type_register(void)
{
	MFn *fn = mfn_new("types", 2);
	MTypeDesc *pt = mtd_new("Point", false);
	mtd_add_field(pt, "x", MT_I32, 0, 0, -1, 0);
	mtd_add_field(pt, "y", MT_F64, 0, 4, -1, 0);
	mtd_finalize(pt);
	uint32_t id = mfn_addtype(fn, pt);
	CHECK(id == 0, "first registered type id 0");
	CHECK(fn->ntyp == 1, "type table has 1 entry");
	CHECK(fn->typ[0] == pt, "type table entry points at td");
	MVal *tv = mval_type(fn, pt);
	CHECK(tv->td == pt, "mval_type carries td");
	mfn_dump(fn, stdout);
	mfn_free(fn);
}

static void test_build(void)
{
	MFn *fn = mfn_new("test_add", 2);
	MBlk *entry = mblk_new(fn, "entry");
	mfn_addblk(fn, entry);

	MVal *a = mval_new(fn, MV_TEMP, MT_I32, 0, "a");
	MVal *b = mval_new(fn, MV_TEMP, MT_I32, 0, "b");
	MVal *r = mval_new(fn, MV_TEMP, MT_I32, 0, "r");
	CHECK(a->id == 0 && b->id == 1 && r->id == 2, "value ids sequential");

	madd1(fn, entry, MOP_PAR, MT_I32, a, MREF_CON(0));
	madd1(fn, entry, MOP_PAR, MT_I32, b, MREF_CON(0));
	madd(fn, entry, MOP_ADD, MT_I32, r, MREF_VAL(a), MREF_VAL(b));
	mret(fn, entry, MREF_VAL(r));

	CHECK(entry->nins == 3, "entry has 3 instructions");
	CHECK(entry->term.op == MOP_RET, "terminator is ret");
	CHECK(r->def == &entry->ins[2], "r defined by last ins");

	mfn_dump(fn, stdout);
	mfn_free(fn);
}

static void test_const_pool(void)
{
	MFn *fn = mfn_new("consts", 2);
	MConst *c1 = mconst_int(fn, MT_I32, 42);
	MConst *c2 = mconst_int(fn, MT_I32, 42);
	MConst *c3 = mconst_int(fn, MT_I32, 43);
	CHECK(c1 == c2, "const dedup: same value shares pool entry");
	CHECK(c1 != c3, "const distinct value new entry");

	MConst *f1 = mconst_flt(fn, MT_F64, 3.14);
	MConst *f2 = mconst_flt(fn, MT_F64, 3.14);
	MConst *f3 = mconst_flt(fn, MT_F32, 3.14);
	CHECK(f1 == f2, "flt const dedup");
	CHECK(f1 != f3, "f64 vs f32 distinct");

	MConst *s1 = mconst_addr(fn, "glob", 8, false, true);
	MConst *s2 = mconst_addr(fn, "glob", 8, false, true);
	MConst *s3 = mconst_addr(fn, "glob", 8, true, true);
	CHECK(s1 == s2, "addr const dedup");
	CHECK(s1 != s3, "tls flag distinct");

	CHECK(fn->ncon == 6, "6 distinct constants");
	mfn_free(fn);
}

int main(void)
{
	test_types();
	test_aggregates();
	test_type_register();
	test_const_pool();
	test_build();

	if (failures) {
		fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	printf("mir_test: all checks passed\n");
	return 0;
}
