/* as_dwarf.c - mt/as DWARF debug-information emission.
 *
 * Emits the .debug_line (and .eh_frame stub) sections from the location
 * records collected during assembly.  Extracted from assemble.c.
 */
#include "mt/as.h"
#include "mt/as_int.h"
#include "mt/elf.h"
#include "mt/elf32.h"
#include "mt/target.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



static int
dwarf_uleb128(struct as_file *as, struct as_section *sec, uint64_t value)
{
	unsigned char b;
	do {
		b = value & 0x7f;
		value >>= 7;
		if (value) b |= 0x80;
		if (as_append_bytes(as, sec, &b, 1) != 0)
			return -1;
	} while (value);
	return 0;
}

static int
dwarf_u32(struct as_file *as, struct as_section *sec, uint32_t v)
{
	unsigned char buf[4];
	buf[0] = (unsigned char)v;
	buf[1] = (unsigned char)(v >> 8);
	buf[2] = (unsigned char)(v >> 16);
	buf[3] = (unsigned char)(v >> 24);
	return as_append_bytes(as, sec, buf, 4);
}

static int
dwarf_u8(struct as_file *as, struct as_section *sec, unsigned char v)
{
	return as_append_bytes(as, sec, &v, 1);
}

static int
dwarf_string(struct as_file *as, struct as_section *sec, const char *s)
{
	return as_append_bytes(as, sec, s, strlen(s) + 1);
}

int
emit_dwarf(struct as_file *as)
{
	size_t i;
	int sec_idx;
	struct as_section *dl;
	struct as_section *eh;
	uint64_t prev_offset, prev_line, header_length, total_length;

	/* ---- .debug_line ---- */
	if (as->dwarf_loc_count == 0)
		goto eh_frame;

	sec_idx = get_section(as, ".debug_line");
	if (sec_idx < 0)
		return -1;
	dl = &as->sections[sec_idx];

	/* Phase 1: build the line number program into a temp buffer */
	{
		unsigned char *prog = (unsigned char *)mt_malloc(4096);
		size_t prog_cap = 4096, prog_size = 0;
		prev_offset = 0;
		prev_line = 1;
		for (i = 0; i < as->dwarf_loc_count; ++i) {
			struct as_dwarf_loc *loc = &as->dwarf_locs[i];
			uint64_t delta = loc->offset - prev_offset;
			unsigned char b;
			if (loc->offset > prev_offset) {
				/* DW_LNS_advance_pc (opcode 2) */
				b = 2;
				if (prog_size + 8 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
				prog[prog_size++] = b;
				/* ULEB128 delta */
				do {
					b = delta & 0x7f; delta >>= 7;
					if (delta) b |= 0x80;
					if (prog_size + 1 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
					prog[prog_size++] = b;
				} while (delta);
			}
			/* DW_LNS_set_file (4) */
			b = 4;
			if (prog_size + 8 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
			prog[prog_size++] = b;
			{ uint64_t v = loc->file; do { b = v & 0x7f; v >>= 7; if (v) b |= 0x80; prog[prog_size++] = b; } while (v); }
			/* DW_LNS_advance_line (3) — takes signed delta; for positive, ULEB128 works */
			if (prog_size + 8 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
			prog[prog_size++] = 3;
			{ uint64_t v = loc->line - prev_line; 
			  do { b = v & 0x7f; v >>= 7; if (v) b |= 0x80; prog[prog_size++] = b; } while (v); }
			prev_line = loc->line;
			if (loc->column != 0) {
				if (prog_size + 8 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
				prog[prog_size++] = 5; /* DW_LNS_set_column */
				{ uint64_t v = loc->column; do { b = v & 0x7f; v >>= 7; if (v) b |= 0x80; prog[prog_size++] = b; } while (v); }
			}
			/* DW_LNS_copy (1) */
			if (prog_size + 1 >= prog_cap) { prog_cap *= 2; prog = (unsigned char *)mt_realloc(prog, prog_cap); }
			prog[prog_size++] = 1;
			prev_offset = loc->offset;
		}

		/* Phase 2: compute header_length in DWARF 2/3 format */
		header_length = 1 + 1 + 1 + 1 + 1 + 12;  /* min_instr..std_op_len (no max_ops) */
		header_length += 1; /* include_directories terminator */
		for (i = 0; i < as->dwarf_file_count; ++i) {
			header_length += (uint64_t)(strlen(as->dwarf_files[i].name) + 1);
			header_length += 1 + 1 + 1; /* dir=0, time=0, size=0 */
		}
		header_length += 1; /* file_names terminator */

		/* total_length = version(2) + header_length(4) + header_length + prog_size,
		 * but note: in DWARF 2/3, total_length does NOT include unit_length(4) itself */
		total_length = 2 + 4 + header_length + prog_size;

		/* Phase 3: emit header */
		{
			unsigned char hdr[512];
			size_t hpos = 0;
			unsigned char ul[4] = { (unsigned char)total_length, (unsigned char)(total_length >> 8),
				(unsigned char)(total_length >> 16), (unsigned char)(total_length >> 24) };
			memcpy(hdr + hpos, ul, 4); hpos += 4;
			hdr[hpos++] = 2; hdr[hpos++] = 0; /* version 2 */
			{ unsigned char hl[4] = { (unsigned char)header_length, (unsigned char)(header_length >> 8),
				(unsigned char)(header_length >> 16), (unsigned char)(header_length >> 24) };
				memcpy(hdr + hpos, hl, 4); hpos += 4; }
			hdr[hpos++] = 1; /* min_inst_len */
			hdr[hpos++] = 1; /* default_is_stmt */
			hdr[hpos++] = 251; /* line_base = -5 */
			hdr[hpos++] = 14; /* line_range */
			hdr[hpos++] = 13; /* opcode_base */
			hdr[hpos++] = 0; hdr[hpos++] = 1; hdr[hpos++] = 1; hdr[hpos++] = 1;
			hdr[hpos++] = 1; hdr[hpos++] = 0; hdr[hpos++] = 0; hdr[hpos++] = 0;
			hdr[hpos++] = 1; hdr[hpos++] = 0; hdr[hpos++] = 0; hdr[hpos++] = 1;
			/* (was DWARF 4 max_ops; now DWARF 2/3 without it) */
			hdr[hpos++] = 0; /* include_directories terminator */
			for (i = 0; i < as->dwarf_file_count; ++i) {
				const char *fn = as->dwarf_files[i].name;
				size_t flen = strlen(fn);
				memcpy(hdr + hpos, fn, flen + 1); hpos += flen + 1;
				hdr[hpos++] = 0; hdr[hpos++] = 0; hdr[hpos++] = 0;
			}
			hdr[hpos++] = 0; /* file_names terminator */
			as_append_bytes(as, dl, hdr, hpos);
		}
		/* Phase 4: emit program */
		as_append_bytes(as, dl, prog, prog_size);
		free(prog);
	}

eh_frame:
	/* ---- .eh_frame ---- */
	if (as->cfi_fde_count == 0)
		return 0;

	sec_idx = get_section(as, ".eh_frame");
	if (sec_idx < 0)
		return -1;
	eh = &as->sections[sec_idx];

	/* ---- CIE ---- */
	{   uint32_t cie_offset = (uint32_t)eh->size;
		uint32_t cie_len_pos = (uint32_t)eh->size;
		unsigned char pad = 0;

		dwarf_u32(as, eh, 0);  /* placeholder length */
		dwarf_u32(as, eh, 0);  /* CIE id */
		dwarf_u8(as, eh, 1);   /* version */
		dwarf_string(as, eh, "zR"); /* augmentation */
		dwarf_uleb128(as, eh, 1);   /* code alignment */
		dwarf_uleb128(as, eh, 1);   /* data alignment */
		dwarf_u8(as, eh, 16);       /* return address reg (x86_64) */
		dwarf_uleb128(as, eh, 1);   /* augmentation data length */
		dwarf_u8(as, eh, 0x00);     /* FDE encoding: absolute */
		dwarf_u8(as, eh, 0x00);     /* DW_CFA_nop */
		while (eh->size % 4 != 0)
			as_append_bytes(as, eh, &pad, 1);
		{ uint32_t cie_len = (uint32_t)(eh->size - cie_len_pos - 4);
			eh->data[cie_len_pos] = (unsigned char)cie_len;
			eh->data[cie_len_pos + 1] = (unsigned char)(cie_len >> 8);
			eh->data[cie_len_pos + 2] = (unsigned char)(cie_len >> 16);
			eh->data[cie_len_pos + 3] = (unsigned char)(cie_len >> 24);
		}

		/* ---- FDEs ---- */
		for (i = 0; i < as->cfi_fde_count; ++i) {
			uint32_t fde_len_pos = (uint32_t)eh->size;
			uint64_t fde_initial_loc_pos, func_size;
			const char *label = as->cfi_func_labels[i];

			dwarf_u32(as, eh, 0); /* FDE length placeholder */
			dwarf_u32(as, eh, (uint32_t)((uint64_t)cie_offset)); /* CIE pointer */
			fde_initial_loc_pos = eh->size;
			dwarf_u32(as, eh, (uint32_t)as->cfi_func_offsets[i]);
			func_size = as->cfi_func_end[i] - as->cfi_func_offsets[i];
			dwarf_u32(as, eh, (uint32_t)func_size);
			if (as->cfi_fde_sizes[i] > 0)
				as_append_bytes(as, eh, as->cfi_fde_progs[i], as->cfi_fde_sizes[i]);
			dwarf_u8(as, eh, 0x00);
			while (eh->size % 4 != 0)
				as_append_bytes(as, eh, &pad, 1);
			{ uint32_t fde_len = (uint32_t)(eh->size - fde_len_pos - 4);
				eh->data[fde_len_pos] = (unsigned char)fde_len;
				eh->data[fde_len_pos + 1] = (unsigned char)(fde_len >> 8);
				eh->data[fde_len_pos + 2] = (unsigned char)(fde_len >> 16);
				eh->data[fde_len_pos + 3] = (unsigned char)(fde_len >> 24);
			}
			if (label && label[0] != '\0')
				as_add_fixup(as, eh, (size_t)fde_initial_loc_pos, 4,
				             MT_R_X86_64_32, 0, label);
		}
		/* terminator: length = 0 */
		dwarf_u32(as, eh, 0);
	}
	return 0;
}
