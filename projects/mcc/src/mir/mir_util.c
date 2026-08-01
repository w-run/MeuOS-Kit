/* mir_util.c — MIR core utilities: allocation, type info, constant pool.
 *
 * MIR values/constants/types are allocated from a per-function arena and
 * freed together with the function (no individual frees, mirroring the
 * existing backend arena model in ir_util.c).
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

/* ---- per-function arena ------------------------------------------------ */

typedef struct MArena MArena;
struct MArena {
	MArena *next;
	size_t used, cap;
	char data[];
};

static MArena *arena_new(void)
{
	MArena *a = malloc(sizeof *a + 65536);
	if (!a) abort();
	a->next = 0;
	a->used = 0;
	a->cap = 65536;
	return a;
}

static void *arena_alloc(MArena **head, size_t size)
{
	MArena *a = *head;
	size = (size + 15u) & ~15u;
	if (!a || a->used + size > a->cap) {
		MArena *na = arena_new();
		na->next = a;
		*head = na;
		a = na;
	}
	void *p = a->data + a->used;
	a->used += size;
	memset(p, 0, size);
	return p;
}

/* Per-function arena storage. We keep a small struct that mirrors the
 * public handle: each MFn gets one private arena pointer stored in the
 * first allocation block. To keep mir.h self-contained (no private
 * fields), the arena head lives in a side table. */
struct MArenaHead {
	MFn *fn;
	MArena *head;
	MArena *dummy;
};

static struct MArenaHead *g_arenas;
static size_t g_narenas, g_carenas;

static MArena **arena_of(MFn *fn)
{
	for (size_t i = 0; i < g_narenas; i++)
		if (g_arenas[i].fn == fn)
			return &g_arenas[i].head;
	/* grow side table */
	if (g_narenas == g_carenas) {
		g_carenas = g_carenas ? g_carenas * 2 : 16;
		g_arenas = realloc(g_arenas, g_carenas * sizeof *g_arenas);
		if (!g_arenas) abort();
	}
	g_arenas[g_narenas].fn = fn;
	g_arenas[g_narenas].head = 0;
	g_arenas[g_narenas].dummy = 0;
	return &g_arenas[g_narenas++].head;
}

static void arena_free_all(MFn *fn)
{
	MArena *a = *arena_of(fn);
	while (a) {
		MArena *next = a->next;
		free(a);
		a = next;
	}
	/* remove from side table */
	for (size_t i = 0; i < g_narenas; i++) {
		if (g_arenas[i].fn == fn) {
			g_arenas[i] = g_arenas[--g_narenas];
			return;
		}
	}
}

void *m_alloc(MFn *fn, size_t size)
{
	return arena_alloc(arena_of(fn), size);
}

void m_arena_free(MFn *fn)
{
	arena_free_all(fn);
}

/* Standard-heap string copy (mcc's libc may not export strdup during
 * bootstrap, so roll our own). */
char *
mx_strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (!p)
		abort();
	memcpy(p, s, n);
	return p;
}

/* ---- type info table ---------------------------------------------------- */

const struct MTypeInfo mtype_info[MT_NTYPE] = {
	[MT_NONE] = { .size = 0, .align = 0 },
	[MT_VOID] = { .size = 1, .align = 1 },
	[MT_I8]   = { .size = 1, .align = 1, .isint = 1 },
	[MT_I16]  = { .size = 2, .align = 2, .isint = 1 },
	[MT_I32]  = { .size = 4, .align = 4, .isint = 1 },
	[MT_I64]  = { .size = 8, .align = 8, .isint = 1 },
	[MT_F32]  = { .size = 4, .align = 4, .isfloat = 1 },
	[MT_F64]  = { .size = 8, .align = 8, .isfloat = 1 },
	[MT_PTR]  = { .size = 8, .align = 8, .isint = 1, .isptr = 1 },
	[MT_AGG]  = { .size = -1, .align = -1, .isagg = 1 },
};

int mtypesize(MType t)
{
	if ((unsigned)t >= MT_NTYPE)
		return 0;
	return mtype_info[t].size;
}

int mtypealign(MType t)
{
	if ((unsigned)t >= MT_NTYPE)
		return 0;
	return mtype_info[t].align;
}

bool mtypeisint(MType t)
{
	return (unsigned)t < MT_NTYPE && mtype_info[t].isint;
}

bool mtypeisfloat(MType t)
{
	return (unsigned)t < MT_NTYPE && mtype_info[t].isfloat;
}

bool mtypeisptr(MType t)
{
	return (unsigned)t < MT_NTYPE && mtype_info[t].isptr;
}

/* ---- value table -------------------------------------------------------- */

static uint32_t hash_bytes(const void *p, size_t n)
{
	uint32_t h = 2166136261u;
	const unsigned char *b = p;
	for (size_t i = 0; i < n; i++) {
		h ^= b[i];
		h *= 16777619u;
	}
	return h;
}

/* ---- constant pool ------------------------------------------------------ */

static MConst *con_pool_find(MFn *fn, MConst *key)
{
	for (uint32_t i = 0; i < fn->ncon; i++) {
		MConst *c = fn->con[i];
		if (c->hash != key->hash)
			continue;
		if (c->kind != key->kind || c->type != key->type)
			continue;
		switch (c->kind) {
		case MC_INT:
			if (c->u.i == key->u.i)
				return c;
			break;
		case MC_FLT:
			if (c->type == MT_F32) {
				if (c->u.s == key->u.s)
					return c;
			} else {
				if (c->u.d == key->u.d)
					return c;
			}
			break;
		case MC_ADDR:
			if (c->u.addr.sym == key->u.addr.sym &&
			    c->u.addr.off == key->u.addr.off &&
			    c->u.addr.tls == key->u.addr.tls &&
			    c->u.addr.isext == key->u.addr.isext)
				return c;
			break;
		default:
			return c;
		}
	}
	return 0;
}

MConst *mconst_int(MFn *fn, MType t, int64_t v)
{
	MConst key = { .kind = MC_INT, .type = t };
	key.u.i = v;
	key.hash = hash_bytes(&key.u.i, sizeof key.u.i) ^ (uint32_t)(t << 16);
	MConst *c = con_pool_find(fn, &key);
	if (c)
		return c;
	c = m_alloc(fn, sizeof *c);
	*c = key;
	c->id = fn->ncon;
	fn->con = realloc(fn->con, (fn->ncon + 1) * sizeof *fn->con);
	fn->con[fn->ncon++] = c;
	return c;
}

MConst *mconst_flt(MFn *fn, MType t, double v)
{
	MConst key = { .kind = MC_FLT, .type = t };
	if (t == MT_F32) {
		key.u.s = (float)v;
		key.hash = hash_bytes(&key.u.s, sizeof key.u.s) ^ (uint32_t)(t << 16);
	} else {
		key.u.d = v;
		key.hash = hash_bytes(&key.u.d, sizeof key.u.d) ^ (uint32_t)(t << 16);
	}
	MConst *c = con_pool_find(fn, &key);
	if (c)
		return c;
	c = m_alloc(fn, sizeof *c);
	*c = key;
	c->id = fn->ncon;
	fn->con = realloc(fn->con, (fn->ncon + 1) * sizeof *fn->con);
	fn->con[fn->ncon++] = c;
	return c;
}

MConst *mconst_addr(MFn *fn, const char *sym, int64_t off,
                    bool tls, bool isext)
{
	MConst key = { .kind = MC_ADDR, .type = MT_PTR };
	key.u.addr.sym = sym;
	key.u.addr.off = off;
	key.u.addr.tls = tls;
	key.u.addr.isext = isext;
	key.hash = hash_bytes(sym, strlen(sym)) ^ (uint32_t)off ^
	           ((uint32_t)tls << 8) ^ ((uint32_t)isext << 9);
	MConst *c = con_pool_find(fn, &key);
	if (c)
		return c;
	c = m_alloc(fn, sizeof *c);
	*c = key;
	c->id = fn->ncon;
	fn->con = realloc(fn->con, (fn->ncon + 1) * sizeof *fn->con);
	fn->con[fn->ncon++] = c;
	return c;
}

/* ---- aggregate types ---------------------------------------------------- */

MTypeDesc *mtd_new(const char *name, bool is_union)
{
	MTypeDesc *td = calloc(1, sizeof *td);
	td->name = name ? mx_strdup(name) : 0;
	td->is_union = is_union;
	return td;
}

MTypeDesc *mtd_array(MTypeDesc *elem, uint64_t nelem)
{
	MTypeDesc *td = calloc(1, sizeof *td);
	td->is_array = true;
	td->elem_desc = elem;
	td->nelem = nelem;
	if (elem) {
		td->align = elem->align;
		td->size = elem->size * nelem;
	} else {
		td->size = 0;
	}
	td->name = 0;
	return td;
}

void mtd_add_field(MTypeDesc *td, const char *name, MType t,
                   MTypeDesc *sub, int64_t off,
                   int16_t bitoff, int16_t bits)
{
	td->field = realloc(td->field, (td->nfield + 1) * sizeof *td->field);
	MField *f = &td->field[td->nfield++];
	f->type = t;
	f->sub = sub;
	f->name = name ? mx_strdup(name) : 0;
	f->offset = off;
	f->bitoff = bitoff;
	f->bits = bits;
	if (t == MT_AGG && sub) {
		if (off + (int64_t)sub->size > (int64_t)td->size)
			td->size = off + sub->size;
		if (sub->align > td->align)
			td->align = sub->align;
	} else if (t != MT_AGG && t != MT_NONE) {
		int sz = mtypesize(t);
		int al = mtypealign(t);
		if (sz > 0 && off + sz > (int64_t)td->size)
			td->size = off + sz;
		if (al > td->align)
			td->align = al;
	}
}

void mtd_finalize(MTypeDesc *td)
{
	if (td->is_array) {
		if (td->elem_desc) {
			td->size = td->elem_desc->size * td->nelem;
			td->align = td->elem_desc->align;
		}
		return;
	}
	/* align whole aggregate up to member alignment (empty aggregates keep
	 * size 0 / align 0 and are classified as in-memory by the ABI) */
	if (td->align > 0)
		td->size = (td->size + td->align - 1) / td->align * td->align;
}
