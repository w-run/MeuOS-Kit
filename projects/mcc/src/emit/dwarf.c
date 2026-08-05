/* Minimal DWARF 4 debug-info emission.
 *
 * The collector records one entry per emitted function (name, declaration
 * line, source file, and local variables with their frame offsets) during
 * code generation; dwarf_finalize() then writes .debug_line /
 * .debug_abbrev / .debug_info as GNU as directives at the end of the
 * compilation.  The line table is emitted explicitly so it works whether
 * or not the target backend emits .loc directives (emitdbgloc is
 * suppressed while a DWARF level is active).
 *
 * Variable locations use DW_OP_fbreg against the frame base (DW_OP_reg6
 * = rbp on x86_64, where mcc knows the frame layout; other targets fall
 * back to DW_OP_call_frame_cfa).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "util.h"
#include "mcc.h"
#include "ir.h"

extern int g_dwarf_level;

/* --- DWARF v4 constants -------------------------------------------- */
enum {
	DW_TAG_compile_unit = 0x11,
	DW_TAG_base_type    = 0x24,
	DW_TAG_subprogram   = 0x2e,
	DW_TAG_variable     = 0x34,

	DW_AT_location    = 0x02,
	DW_AT_name        = 0x03,
	DW_AT_byte_size   = 0x0b,
	DW_AT_stmt_list   = 0x10,
	DW_AT_low_pc      = 0x11,
	DW_AT_high_pc     = 0x12,
	DW_AT_language    = 0x13,
	DW_AT_comp_dir    = 0x1b,
	DW_AT_producer    = 0x25,
	DW_AT_decl_file   = 0x3a,
	DW_AT_decl_line   = 0x3b,
	DW_AT_encoding    = 0x3e,
	DW_AT_frame_base  = 0x40,
	DW_AT_type        = 0x49,

	DW_FORM_addr      = 0x01,
	DW_FORM_data2     = 0x05,
	DW_FORM_data4     = 0x06,
	DW_FORM_string    = 0x08,
	DW_FORM_data1     = 0x0b,
	DW_FORM_ref4      = 0x13,
	DW_FORM_exprloc   = 0x18,

	DW_ATE_address    = 0x01,
	DW_ATE_signed     = 0x05,
	DW_ATE_unsigned   = 0x07,

	DW_LANG_C99 = 0x000c,

	DW_OP_fbreg          = 0x91,
	DW_OP_call_frame_cfa = 0x9c,
	DW_OP_reg0           = 0x50, /* rbp is reg 6 on x86_64 */
};

/* Size of the signed LEB128 encoding of v (for exprloc length bytes). */
static int
sleb_size(long long v)
{
	int n = 0;
	for (;;) {
		unsigned char byte = v & 0x7f;
		v >>= 7;
		n++;
		if ((v == 0 && !(byte & 0x40)) || (v == -1 && (byte & 0x40)))
			return n;
	}
}

/* --- collector ------------------------------------------------------ */

/* DWARF type kind used by variable DIEs (maps to a base-type DIE). */
enum dwarf_type_kind {
	DT_INT = 0,   /* signed 32-bit */
	DT_CHAR,      /* signed 8-bit */
	DT_UINT,      /* unsigned 32-bit */
	DT_PTR,       /* address-sized pointer */
	DT_LAST,
};

struct dwarf_var {
	const char *name;
	int type;       /* enum dwarf_type_kind */
	int has_loc;    /* nonzero: DW_AT_location present */
	int loc_off;    /* DW_OP_fbreg operand (frame-relative offset) */
	int loc_reg;    /* DW_OP_regN register number, -1 if stack */
};

struct dwarf_func {
	const char *name;
	int line;       /* 1-based declaration line */
	int file;       /* 1-based file index */
	int framebase;  /* 0 = rbp (DW_OP_reg6), 1 = rsp (DW_OP_reg7) */
	struct dwarf_var *vars;
	int nvars, cap;
};

static struct dwarf_func *g_funcs;
static int g_nfuncs, g_capfuncs;
static const char *g_srcfile;
static int g_curfunc = -1;   /* index of the open record, -1 if none */

/* --- machine-layer location feedback --------------------------------
 * The x86_64 machine emitter (memit) records each static alloca's final
 * frame offset here, keyed by the MVal id (which the frontend reaches via
 * the func_to_mir value->MVal side table).  dwarf_collect_vars() looks a
 * frontend local up after the backend runs and writes the DW_AT_location. */

struct dwarf_loc {
	uint32_t mval_id;
	int32_t off;    /* frame offset for DW_OP_fbreg */
	int32_t reg;    /* DWARF register number, -1 = stack */
};

static struct dwarf_loc *g_locs;
static int g_nlocs, g_caplocs;

/* Per-function frame base set by the emitter: 0 = rbp, 1 = rsp. */
static int g_framebase;

void
dwarf_loc_reset(void)
{
	g_nlocs = 0;
	g_framebase = 0;
}

void
dwarf_set_framebase(int base)
{
	g_framebase = base;
}

void
dwarf_loc_set_stack(uint32_t mval_id, int32_t off)
{
	struct dwarf_loc *l;

	if (g_nlocs >= g_caplocs) {
		g_caplocs = g_caplocs ? g_caplocs * 2 : 32;
		g_locs = xreallocarray(g_locs, g_caplocs, sizeof *g_locs);
	}
	l = &g_locs[g_nlocs++];
	l->mval_id = mval_id;
	l->off = off;
	l->reg = -1;
}

void
dwarf_loc_set_reg(uint32_t mval_id, int32_t dreg)
{
	struct dwarf_loc *l;

	if (g_nlocs >= g_caplocs) {
		g_caplocs = g_caplocs ? g_caplocs * 2 : 32;
		g_locs = xreallocarray(g_locs, g_caplocs, sizeof *g_locs);
	}
	l = &g_locs[g_nlocs++];
	l->mval_id = mval_id;
	l->off = 0;
	l->reg = dreg;
}

/* Return 1 and the location if recorded for this MVal id. */
int
dwarf_loc_get(uint32_t mval_id, int32_t *off, int32_t *reg)
{
	int i;

	for (i = 0; i < g_nlocs; i++)
		if (g_locs[i].mval_id == mval_id) {
			*off = g_locs[i].off;
			*reg = g_locs[i].reg;
			return 1;
		}
	return 0;
}

void
dwarf_set_file(const char *path)
{
	g_srcfile = path;
}

/* Open a function record and return its index (for the .Ldwarf<N>_end
 * label emitted after the function's assembly). */
int
dwarf_begin_func(const char *name, int line, int file)
{
	struct dwarf_func *fu;

	if (g_nfuncs >= g_capfuncs) {
		g_capfuncs = g_capfuncs ? g_capfuncs * 2 : 16;
		g_funcs = xreallocarray(g_funcs, g_capfuncs, sizeof *g_funcs);
	}
	fu = &g_funcs[g_nfuncs];
	memset(fu, 0, sizeof *fu);
	fu->name = name;
	fu->line = line ? line : 1;
	fu->file = file ? file : 1;
	g_curfunc = g_nfuncs++;
	return g_curfunc;
}

void
dwarf_add_var(const char *name, int type, int has_loc, int loc_off,
    int loc_reg)
{
	struct dwarf_func *fu;
	struct dwarf_var *v;

	if (g_curfunc < 0)
		return;
	fu = &g_funcs[g_curfunc];
	if (fu->nvars >= fu->cap) {
		fu->cap = fu->cap ? fu->cap * 2 : 8;
		fu->vars = xreallocarray(fu->vars, fu->cap, sizeof *fu->vars);
	}
	v = &fu->vars[fu->nvars++];
	v->name = name;
	v->type = type;
	v->has_loc = has_loc;
	v->loc_off = loc_off;
	v->loc_reg = loc_reg;
}

void
dwarf_end_func(void)
{
	/* frame base is set by the emitter during codegen (memit may use rsp
	 * for leaf functions that omit the frame pointer), so capture it when
	 * the function record closes, after the backend has run */
	if (g_curfunc >= 0)
		g_funcs[g_curfunc].framebase = g_framebase;
	g_curfunc = -1;
}

/* Emit the `.Ldwarf<N>_end:` label right after the function's assembly so
 * the subprogram's high_pc (size) is resolvable.  The function emission may
 * finish inside a `.section .rodata` block (float literals), so force
 * .text to keep the label in the same section as the function symbol
 * (cross-section symbol subtraction is not resolvable by gas). */
void
dwarf_emit_func_end(FILE *f, int idx)
{
	fprintf(f, ".text\n.Ldwarf%d_end:\n", idx);
}

/* --- finalize ------------------------------------------------------- */

static const char *
type_label(int type)
{
	switch (type) {
	case DT_CHAR: return ".Ldwarf_type_char";
	case DT_UINT: return ".Ldwarf_type_uint";
	case DT_PTR:  return ".Ldwarf_type_ptr";
	default:      return ".Ldwarf_type_int";
	}
}

void
dwarf_finalize(FILE *f)
{
	char dirbuf[4096];
	const char *dir;
	int ptrsize = typelong.size;
	int i, j;

	if (!g_dwarf_level || g_nfuncs == 0)
		return;
	if (!g_srcfile)
		g_srcfile = "stdin";
	dir = getcwd(dirbuf, sizeof dirbuf);
	if (!dir)
		dir = ".";
#define ASZ (ptrsize == 8 ? "8" : "4")

	/* ---------- .debug_line (DWARF 4) ---------- */
	fprintf(f, "\n.section .debug_line,\"\",@progbits\n");
	fprintf(f, ".Ldwarf_line0:\n");
	fprintf(f, "\t.4byte .Ldwarf_line_end - .Ldwarf_line0 - 4\n");
	fprintf(f, "\t.2byte 4\n");                       /* version */
	fprintf(f, "\t.4byte .Ldwarf_line_hdr_end - .Ldwarf_line_hdr\n");
	fprintf(f, ".Ldwarf_line_hdr:\n");
	fprintf(f, "\t.byte 1\n");                        /* min instr length */
	fprintf(f, "\t.byte 1\n");                        /* max ops / instr */
	fprintf(f, "\t.byte 1\n");                        /* default_is_stmt */
	fprintf(f, "\t.byte 0xfb\n");                     /* line_base = -5 */
	fprintf(f, "\t.byte 14\n");                       /* line_range */
	fprintf(f, "\t.byte 13\n");                       /* opcode_base */
	fprintf(f, "\t.byte 0,1,1,1,1,0,0,0,1,0,0,1\n");  /* std opcode lengths */
	fprintf(f, "\t.byte 0\n");                        /* include dirs */
	fprintf(f, "\t.byte 1\n");                        /* file count */
	fprintf(f, "\t.asciz \"%s\"\n", g_srcfile);
	fprintf(f, "\t.uleb128 0\n\t.uleb128 0\n\t.uleb128 0\n"); /* dir, mtime, size */
	fprintf(f, "\t.byte 0\n");                        /* end of files */
	fprintf(f, ".Ldwarf_line_hdr_end:\n");
	for (i = 0; i < g_nfuncs; i++) {
		struct dwarf_func *fu = &g_funcs[i];
		fprintf(f, "\t.byte 0,9,2\n");                /* DW_LNE_set_address */
		fprintf(f, "\t.%sbyte %s\n", ASZ, fu->name);
		fprintf(f, "\t.sleb128 %d\n", fu->line - 1);  /* advance_line */
		fprintf(f, "\t.byte 1\n");                    /* DW_LNS_copy */
		fprintf(f, "\t.byte 0,1,1\n");                /* DW_LNE_end_sequence */
	}
	fprintf(f, ".Ldwarf_line_end:\n");

	/* ---------- .debug_abbrev (DWARF 4) ---------- */
	fprintf(f, "\n.section .debug_abbrev,\"\",@progbits\n");
	fprintf(f, ".Ldwarf_abbrev0:\n");
	/* 1: compile_unit, children yes */
	fprintf(f, "\t.uleb128 1\n\t.uleb128 0x11\n\t.byte 1\n");
	fprintf(f, "\t.uleb128 0x03, 0x08\n");  /* name, string */
	fprintf(f, "\t.uleb128 0x25, 0x08\n");  /* producer, string */
	fprintf(f, "\t.uleb128 0x13, 0x05\n");  /* language, data2 */
	fprintf(f, "\t.uleb128 0x11, 0x01\n");  /* low_pc, addr */
	fprintf(f, "\t.uleb128 0x10, 0x06\n");  /* stmt_list, data4 */
	fprintf(f, "\t.uleb128 0x1b, 0x08\n");  /* comp_dir, string */
	fprintf(f, "\t.byte 0,0\n");
	/* 2: subprogram, children yes */
	fprintf(f, "\t.uleb128 2\n\t.uleb128 0x2e\n\t.byte 1\n");
	fprintf(f, "\t.uleb128 0x03, 0x08\n");  /* name */
	fprintf(f, "\t.uleb128 0x11, 0x01\n");  /* low_pc */
	fprintf(f, "\t.uleb128 0x12, 0x06\n");  /* high_pc, data4 */
	fprintf(f, "\t.uleb128 0x3a, 0x0b\n");  /* decl_file, data1 */
	fprintf(f, "\t.uleb128 0x3b, 0x05\n");  /* decl_line, data2 */
	fprintf(f, "\t.uleb128 0x40, 0x18\n");  /* frame_base, exprloc */
	fprintf(f, "\t.byte 0,0\n");
	/* 3: variable, no children */
	fprintf(f, "\t.uleb128 3\n\t.uleb128 0x34\n\t.byte 0\n");
	fprintf(f, "\t.uleb128 0x03, 0x08\n");  /* name */
	fprintf(f, "\t.uleb128 0x49, 0x13\n");  /* type, ref4 */
	fprintf(f, "\t.uleb128 0x3a, 0x0b\n");  /* decl_file, data1 */
	fprintf(f, "\t.uleb128 0x3b, 0x05\n");  /* decl_line, data2 */
	fprintf(f, "\t.uleb128 0x02, 0x18\n");  /* location, exprloc */
	fprintf(f, "\t.byte 0,0\n");
	/* 4: base_type, no children */
	fprintf(f, "\t.uleb128 4\n\t.uleb128 0x24\n\t.byte 0\n");
	fprintf(f, "\t.uleb128 0x03, 0x08\n");  /* name */
	fprintf(f, "\t.uleb128 0x3e, 0x0b\n");  /* encoding, data1 */
	fprintf(f, "\t.uleb128 0x0b, 0x0b\n");  /* byte_size, data1 */
	fprintf(f, "\t.byte 0,0\n");
	fprintf(f, "\t.byte 0\n");              /* end of abbrev table */

	/* ---------- .debug_info (DWARF 4) ---------- */
	fprintf(f, "\n.section .debug_info,\"\",@progbits\n");
	fprintf(f, ".Ldwarf_info0:\n");
	fprintf(f, "\t.4byte .Ldwarf_info_end - .Ldwarf_info0 - 4\n");
	fprintf(f, "\t.2byte 4\n");            /* version */
	fprintf(f, "\t.4byte 0\n");            /* abbrev offset */
	fprintf(f, "\t.byte %d\n", ptrsize);  /* address size */

	/* compile_unit */
	fprintf(f, "\t.uleb128 1\n");
	fprintf(f, "\t.asciz \"%s\"\n", g_srcfile);
	fprintf(f, "\t.asciz \"mcc (MeuOS C compiler)\"\n");
	fprintf(f, "\t.2byte 0x%x\n", DW_LANG_C99);
	fprintf(f, "\t.%sbyte 0\n", ASZ);     /* low_pc 0 */
	fprintf(f, "\t.4byte 0\n");           /* stmt_list */
	fprintf(f, "\t.asciz \"%s\"\n", dir);

	/* base types (referenced by variable DIEs) */
	fprintf(f, ".Ldwarf_type_int:\n\t.uleb128 4\n\t.asciz \"int\"\n"
	    "\t.byte 0x%x\n\t.byte 4\n", DW_ATE_signed);
	fprintf(f, ".Ldwarf_type_char:\n\t.uleb128 4\n\t.asciz \"char\"\n"
	    "\t.byte 0x%x\n\t.byte 1\n", DW_ATE_signed);
	fprintf(f, ".Ldwarf_type_uint:\n\t.uleb128 4\n\t.asciz \"unsigned int\"\n"
	    "\t.byte 0x%x\n\t.byte 4\n", DW_ATE_unsigned);
	fprintf(f, ".Ldwarf_type_ptr:\n\t.uleb128 4\n\t.asciz \"pointer\"\n"
	    "\t.byte 0x%x\n\t.byte %d\n", DW_ATE_address, ptrsize);

	/* subprograms (+ variables) */
	for (i = 0; i < g_nfuncs; i++) {
		struct dwarf_func *fu = &g_funcs[i];
		fprintf(f, "\t.uleb128 2\n");
		fprintf(f, "\t.asciz \"%s\"\n", fu->name);
		fprintf(f, "\t.%sbyte %s\n", ASZ, fu->name);        /* low_pc */
		fprintf(f, "\t.4byte .Ldwarf%d_end - %s\n", i, fu->name); /* size */
		fprintf(f, "\t.byte %d\n", fu->file);
		fprintf(f, "\t.2byte %d\n", fu->line);
		if (strcmp(T.name, "x86_64") == 0)
			/* frame base matches the variable fbreg offsets: rbp
			 * (DW_OP_reg6) unless the function omitted the frame
			 * pointer (rsp, DW_OP_reg7) */
			fprintf(f, "\t.byte 1, 0x%x\n",
			    DW_OP_reg0 + (fu->framebase ? 7 : 6));
		else
			fprintf(f, "\t.byte 1, 0x%x\n", DW_OP_call_frame_cfa);
		for (j = 0; j < fu->nvars; j++) {
			struct dwarf_var *v = &fu->vars[j];
			int loclen = 0;
			fprintf(f, "\t.uleb128 3\n");
			fprintf(f, "\t.asciz \"%s\"\n", v->name);
			fprintf(f, "\t.4byte %s - .Ldwarf_info0\n", type_label(v->type));
			fprintf(f, "\t.byte %d\n", fu->file);
			fprintf(f, "\t.2byte %d\n", fu->line);
			/* DW_AT_location exprloc: DW_OP_fbreg <off> or
			 * DW_OP_regN (a register number is a single-byte
			 * zero-operand opcode, so its exprloc length is 1) */
			if (v->has_loc && v->loc_reg >= 0)
				fprintf(f, "\t.byte 1, 0x%x\n",
				    DW_OP_reg0 + v->loc_reg);
			else if (v->has_loc) {
				loclen = 1 + sleb_size(v->loc_off);
				fprintf(f, "\t.byte %d\n", loclen);
				fprintf(f, "\t.byte 0x%x\n", DW_OP_fbreg);
				fprintf(f, "\t.sleb128 %d\n", v->loc_off);
			} else
				fprintf(f, "\t.byte 0\n"); /* empty exprloc */
		}
		fprintf(f, "\t.byte 0\n");  /* end of subprogram children */
	}
	fprintf(f, "\t.byte 0\n");  /* end of CU children */
	fprintf(f, ".Ldwarf_info_end:\n");
#undef ASZ
}
