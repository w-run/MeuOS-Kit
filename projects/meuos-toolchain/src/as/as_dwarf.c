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
dwarf_u64(struct as_file *as, struct as_section *sec, uint64_t v)
{
	unsigned char buf[8];
	buf[0] = (unsigned char)v;
	buf[1] = (unsigned char)(v >> 8);
	buf[2] = (unsigned char)(v >> 16);
	buf[3] = (unsigned char)(v >> 24);
	buf[4] = (unsigned char)(v >> 32);
	buf[5] = (unsigned char)(v >> 40);
	buf[6] = (unsigned char)(v >> 48);
	buf[7] = (unsigned char)(v >> 56);
	return as_append_bytes(as, sec, buf, 8);
}

static int
dwarf_u8(struct as_file *as, struct as_section *sec, unsigned char v)
{
	return as_append_bytes(as, sec, &v, 1);
}

static int
dwarf_sleb128(struct as_file *as, struct as_section *sec, int64_t value)
{
	unsigned char b;
	int more = 1;
	while (more) {
		b = (unsigned char)(value & 0x7f);
		value >>= 7;
		if ((value == 0 && !(b & 0x40)) || (value == -1 && (b & 0x40)))
			more = 0;
		else
			b |= 0x80;
		if (as_append_bytes(as, sec, &b, 1) != 0)
			return -1;
	}
	return 0;
}

static int
dwarf_string(struct as_file *as, struct as_section *sec, const char *s)
{
	return as_append_bytes(as, sec, s, strlen(s) + 1);
}

/* DW_EH_PE constants (repeated from ld/elfout.c for standalone use
 * in the assembler; these are not exposed in the public header). */
#define DW_EH_PE_absptr  0x00
#define DW_EH_PE_udata4  0x03
#define DW_EH_PE_sdata4  0x0b
#define DW_EH_PE_pcrel   0x10

/* Return the byte width of a DW_EH_PE value encoding (ignoring
 * application mode bits like pcrel/textrel/datarel). */
static int
dwarf_eh_pe_width(uint8_t enc)
{
	switch (enc & 0x0f) {
	case 0x00: return 0; /* DW_EH_PE_absptr: native pointer width handled below */
	case 0x01: return 0; /* uleb128 */
	case 0x02: return 2; /* udata2 */
	case 0x03: return 4; /* udata4 */
	case 0x04: return 8; /* udata8 */
	case 0x09: return 0; /* sleb128 */
	case 0x0a: return 2; /* sdata2 */
	case 0x0b: return 4; /* sdata4 */
	case 0x0c: return 8; /* sdata8 */
	default:   return 0;
	}
}

/* Emit a value using the given DW_EH_PE encoding (offset only, no
 * application-mode fixup like pcrel).  Returns the number of bytes
 * written, or -1 on error. */
static int
dwarf_eh_emit_value(struct as_file *as, struct as_section *sec,
                    uint64_t val, uint8_t enc)
{
	uint8_t fmt = enc & 0x0f;
	switch (fmt) {
	case 0x00: /* absptr — native pointer width */
		if (as->target && as->target->elf_class == 2)
			return dwarf_u64(as, sec, val);
		return dwarf_u32(as, sec, (uint32_t)val);
	case 0x03: /* udata4 */
		return dwarf_u32(as, sec, (uint32_t)val);
	case 0x0b: /* sdata4 */
		return dwarf_u32(as, sec, (uint32_t)(int32_t)(int64_t)val);
	case 0x04: /* udata8 */
	case 0x0c: /* sdata8 */
		return dwarf_u64(as, sec, val);
	case 0x02: /* udata2 */
	case 0x0a: /* sdata2 */
	{
		unsigned char buf[2];
		buf[0] = (unsigned char)val;
		buf[1] = (unsigned char)(val >> 8);
		return as_append_bytes(as, sec, buf, 2);
	}
	default:
		return as_error(as, "unsupported DW_EH_PE encoding 0x%02x", enc);
	}
}

/* Build the CIE augmentation string from the given FDE's attributes. */
static const char *
cie_augmentation_for_fde(struct as_file *as, int fde_idx,
                          char *buf, size_t bufsz)
{
	size_t pos = 0;
	(void)bufsz;
	buf[pos++] = 'z';
	buf[pos++] = 'R';
	if (fde_idx >= 0 && as->cfi_fde_personality_set &&
	    as->cfi_fde_personality_set[fde_idx])
		buf[pos++] = 'P';
	if (fde_idx >= 0 && as->cfi_lsda_pointers &&
	    as->cfi_lsda_pointers[fde_idx] &&
	    as->cfi_lsda_pointers[fde_idx][0] != '\0')
		buf[pos++] = 'L';
	if (fde_idx >= 0 && as->cfi_fde_signal_frame &&
	    as->cfi_fde_signal_frame[fde_idx])
		buf[pos++] = 'S';
	buf[pos] = '\0';
	return buf;
}

/* Emit augmentation data for a CIE.
 * fde_idx is the index of the template FDE for this CIE's attributes. */
static int
emit_cie_augmentation_data(struct as_file *as, struct as_section *eh,
                            const char *aug, int fde_idx)
{
	size_t start = eh->size;
	(void)aug;

	/* Augmentation data layout matches augmentation string order:
	 * "zR"  → [R encoding byte]
	 * "zRP" → [P encoding, personality pointer... , R encoding byte]
	 * "zRL" → [L encoding byte, R encoding byte]
	 * "zRPL" → [P encoding, personality pointer... , L encoding byte, R encoding byte]
	 * "zRS" → [R encoding byte]  (signal frame, no extra data)
	 * "zRPLS" → [P encoding, personality, L encoding, R encoding] */

	if (fde_idx >= 0 && as->cfi_fde_personality_set &&
	    as->cfi_fde_personality_set[fde_idx]) {
		dwarf_u8(as, eh, as->cfi_fde_personality_encoding[fde_idx]);
		/* Personality pointer: emit 0 placeholder + fixup */
		if (as->cfi_fde_personality_symbol &&
		    as->cfi_fde_personality_symbol[fde_idx]) {
			size_t pers_pos = eh->size;
			int pers_width = dwarf_eh_pe_width(
				as->cfi_fde_personality_encoding[fde_idx]);
			if (pers_width == 0)
				pers_width = (as->target && as->target->elf_class == 2) ? 8 : 4;
			/* Emit zero bytes for the pointer */
			unsigned char zero = 0;
			int j;
			for (j = 0; j < pers_width; j++)
				as_append_bytes(as, eh, &zero, 1);
			/* Add fixup for the personality symbol */
			unsigned reloc_type = (pers_width == 8) ?
				MT_R_X86_64_64 : MT_R_X86_64_32;
			as_add_fixup(as, eh, (size_t)pers_pos, (unsigned)pers_width,
			             reloc_type, 0,
			             as->cfi_fde_personality_symbol[fde_idx]);
		}
	}
	if (fde_idx >= 0 && as->cfi_lsda_pointers &&
	    as->cfi_lsda_pointers[fde_idx] &&
	    as->cfi_lsda_pointers[fde_idx][0] != '\0') {
		dwarf_u8(as, eh, as->cfi_lsda_encoding);
	}
	/* FDE encoding (always present with "zR") */
	dwarf_u8(as, eh, as->target->dwarf_fde_encoding);
	return (int)(eh->size - start);
}

/* Compare two FDEs' CIE-relevant attributes for equality.
 * Returns 1 if they share the same CIE signature. */
static int
fde_cie_equal(struct as_file *as, int a, int b)
{
	int pa = (as->cfi_fde_personality_set && as->cfi_fde_personality_set[a]) ? 1 : 0;
	int pb = (as->cfi_fde_personality_set && as->cfi_fde_personality_set[b]) ? 1 : 0;
	if (pa != pb) return 0;
	if (pa) {
		if (as->cfi_fde_personality_encoding[a] !=
		    as->cfi_fde_personality_encoding[b])
			return 0;
		const char *sa = as->cfi_fde_personality_symbol ?
			as->cfi_fde_personality_symbol[a] : NULL;
		const char *sb = as->cfi_fde_personality_symbol ?
			as->cfi_fde_personality_symbol[b] : NULL;
		if ((sa && !sb) || (!sa && sb)) return 0;
		if (sa && sb && strcmp(sa, sb) != 0) return 0;
	}
	int la = (as->cfi_lsda_pointers && as->cfi_lsda_pointers[a] &&
	          as->cfi_lsda_pointers[a][0] != '\0') ? 1 : 0;
	int lb = (as->cfi_lsda_pointers && as->cfi_lsda_pointers[b] &&
	          as->cfi_lsda_pointers[b][0] != '\0') ? 1 : 0;
	if (la != lb) return 0;
	/* Both have LSDA — their LSDA symbols must match exactly */
	if (la && lb) {
		uint8_t lenc = as->cfi_lsda_encoding;
		(void)lenc; /* Currently single global encoding — FDEs share */
		if (strcmp(as->cfi_lsda_pointers[a], as->cfi_lsda_pointers[b]) != 0)
			return 0;
	}
	int sa = (as->cfi_fde_signal_frame && as->cfi_fde_signal_frame[a]) ? 1 : 0;
	int sb = (as->cfi_fde_signal_frame && as->cfi_fde_signal_frame[b]) ? 1 : 0;
	if (sa != sb) return 0;
	return 1;
}

int
emit_dwarf(struct as_file *as)
{
	size_t i;
	int sec_idx;
	struct as_section *dl;
	struct as_section *eh;
	uint64_t prev_offset, prev_line, header_length, total_length;
	char augbuf[8];

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
	/* ---- .eh_frame / .debug_frame ---- */
	if (as->cfi_fde_count == 0)
		return 0;

	{
		const char *frame_sec_name = as->cfi_section_type ? ".debug_frame" : ".eh_frame";
		uint32_t cie_id_val = as->cfi_section_type ? 0xFFFFFFFFU : 0;
		int dwarf_version = as->cfi_section_type ? 3 : 1;

		sec_idx = get_section(as, frame_sec_name);
		if (sec_idx < 0)
			return -1;
		eh = &as->sections[sec_idx];

		/* ---- Multi-CIE: first pass: group FDEs by CIE signature ---- */
		/* Build cie_for_fde[] array: for each FDE, find or create a CIE index */
		size_t cie_count = 0;
		size_t cie_capacity = 0;
		uint64_t *cie_offsets = NULL; /* offset of each CIE in the output */
		int *cie_for_fde = NULL;      /* per-FDE CIE index */

		cie_for_fde = (int *)mt_malloc(as->cfi_fde_count * sizeof(int));
		if (!cie_for_fde) return as_error(as, "out of memory");

		for (i = 0; i < as->cfi_fde_count; i++) {
			/* Find existing CIE with matching signature */
			size_t c;
			int found = -1;
			for (c = 0; c < cie_count; c++) {
				/* Compare against the first FDE assigned to this CIE */
				size_t k;
				for (k = 0; k < i; k++) {
					if (cie_for_fde[k] == (int)c) {
						if (fde_cie_equal(as, (int)i, (int)k))
							found = (int)c;
						break;
					}
				}
				if (found >= 0) break;
			}
			if (found < 0) {
				/* Create new CIE */
				if (cie_count == cie_capacity) {
					size_t cap = cie_capacity ? cie_capacity * 2 : 4;
					uint64_t *no = (uint64_t *)mt_realloc(cie_offsets, cap * sizeof(*no));
					if (!no) { free(cie_for_fde); return as_error(as, "out of memory"); }
					cie_offsets = no;
					cie_capacity = cap;
				}
				found = (int)cie_count;
				cie_offsets[cie_count] = 0; /* filled after emit */
				cie_count++;
			}
			cie_for_fde[i] = found;
		}

		/* ---- Emit CIEs ---- */
		{
			unsigned char pad = 0;
			int cie_align;

			/* CIE alignment: 4 bytes for 32-bit, 8 bytes for 64-bit targets */
			cie_align = (as->target && as->target->elf_class == 2) ? 8 : 4;

			for (size_t ci = 0; ci < cie_count; ci++) {
				/* Find the first FDE assigned to this CIE to get its attributes */
				int template_fde = -1;
				for (size_t fj = 0; fj < as->cfi_fde_count; fj++) {
					if (cie_for_fde[fj] == (int)ci) {
						template_fde = (int)fj;
						break;
					}
				}
				if (template_fde < 0) continue;

				cie_augmentation_for_fde(as, template_fde, augbuf, sizeof(augbuf));
				cie_offsets[ci] = eh->size;

				uint32_t cie_len_pos = (uint32_t)eh->size;

				dwarf_u32(as, eh, 0);         /* placeholder length */
				dwarf_u32(as, eh, cie_id_val); /* CIE id */
				dwarf_u8(as, eh, (unsigned char)dwarf_version);
				dwarf_string(as, eh, augbuf); /* augmentation (dynamic) */
				dwarf_uleb128(as, eh, as->target->dwarf_code_align);
				dwarf_sleb128(as, eh, as->target->dwarf_data_align);
				dwarf_u8(as, eh, as->target->dwarf_ra_reg);
				/* augmentation data length (written after we know the size) */
				{   uint32_t aug_data_len_pos = (uint32_t)eh->size;
					dwarf_uleb128(as, eh, 0); /* placeholder */
					uint32_t aug_start = (uint32_t)eh->size;
					emit_cie_augmentation_data(as, eh, augbuf, template_fde);
					uint32_t aug_len = (uint32_t)(eh->size - aug_start);
					/* Patch augmentation data length */
					{   uint64_t tmp = aug_len;
						size_t patch_pos = aug_data_len_pos;
						do {
							unsigned char b = tmp & 0x7f;
							tmp >>= 7;
							if (tmp) b |= 0x80;
							eh->data[patch_pos++] = b;
						} while (tmp);
					}
				}
				/* Pad to CIE alignment */
				while (eh->size % (size_t)cie_align != 0)
					as_append_bytes(as, eh, &pad, 1);
				{ uint32_t cie_len = (uint32_t)(eh->size - cie_len_pos - 4);
					eh->data[cie_len_pos] = (unsigned char)cie_len;
					eh->data[cie_len_pos + 1] = (unsigned char)(cie_len >> 8);
					eh->data[cie_len_pos + 2] = (unsigned char)(cie_len >> 16);
					eh->data[cie_len_pos + 3] = (unsigned char)(cie_len >> 24);
				}
			}
		}

		/* ---- Emit FDEs ---- */
		{
			unsigned char pad = 0;
			int cie_align = (as->target && as->target->elf_class == 2) ? 8 : 4;
			int fde_addr_width;

			{
				uint8_t enc = as->target->dwarf_fde_encoding;
				int w = dwarf_eh_pe_width(enc);
				if (w > 0)
					fde_addr_width = w;
				else if (as->target && as->target->elf_class == 2)
					fde_addr_width = 8;
				else
					fde_addr_width = 4;
			}

			for (i = 0; i < as->cfi_fde_count; ++i) {
				uint32_t fde_len_pos = (uint32_t)eh->size;
				uint64_t fde_initial_loc_pos, func_size;
				const char *label = as->cfi_func_labels[i];
				int ci = cie_for_fde[i];
				uint64_t cie_offset = cie_offsets[ci];

				dwarf_u32(as, eh, 0); /* FDE length placeholder */
				/* CIE pointer: For .eh_frame, this is the signed offset
				 * from the current FDE start (where the CIE pointer field
				 * sits) BACK to the CIE start. For .debug_frame, it's
				 * the absolute offset of the CIE in the section. */
				if (as->cfi_section_type) {
					/* .debug_frame: absolute offset */
					dwarf_u32(as, eh, (uint32_t)cie_offset);
				} else {
					/* .eh_frame: relative offset from this 4-byte field's
					 * location back to the CIE start */
					uint32_t rel = (uint32_t)((int64_t)cie_offset - (int64_t)eh->size);
					dwarf_u32(as, eh, rel);
				}
				/* FDE initial_location (encoded per target) */
				fde_initial_loc_pos = eh->size;
				dwarf_eh_emit_value(as, eh, as->cfi_func_offsets[i],
				                    as->target->dwarf_fde_encoding);
				/* FDE address_range (function size) */
				func_size = as->cfi_func_end[i] - as->cfi_func_offsets[i];
				dwarf_eh_emit_value(as, eh, func_size, DW_EH_PE_udata4);
				/* LSDA pointer (if set for this FDE) */
				if (as->cfi_lsda_pointers && as->cfi_lsda_pointers[i] &&
				    as->cfi_lsda_pointers[i][0] != '\0') {
					size_t lsda_pos = eh->size;
					unsigned lsda_width = (unsigned)dwarf_eh_pe_width(as->cfi_lsda_encoding);
					if (lsda_width == 0)
						lsda_width = (as->target && as->target->elf_class == 2) ? 8 : 4;
					/* Emit zero bytes */
					unsigned j;
					for (j = 0; j < lsda_width; j++)
						as_append_bytes(as, eh, &pad, 1);
					/* Add fixup for the LSDA symbol */
					unsigned lsda_reloc = (lsda_width == 8) ?
						MT_R_X86_64_64 : MT_R_X86_64_32;
					as_add_fixup(as, eh, (size_t)lsda_pos, lsda_width,
					             lsda_reloc, 0, as->cfi_lsda_pointers[i]);
				}
				/* FDE CFI program */
				if (as->cfi_fde_sizes[i] > 0)
					as_append_bytes(as, eh, as->cfi_fde_progs[i], as->cfi_fde_sizes[i]);
				/* Terminator */
				dwarf_u8(as, eh, 0x00);
				/* FDE alignment: same as CIE */
				while (eh->size % (size_t)cie_align != 0)
					as_append_bytes(as, eh, &pad, 1);
				{ uint32_t fde_len = (uint32_t)(eh->size - fde_len_pos - 4);
					eh->data[fde_len_pos] = (unsigned char)fde_len;
					eh->data[fde_len_pos + 1] = (unsigned char)(fde_len >> 8);
					eh->data[fde_len_pos + 2] = (unsigned char)(fde_len >> 16);
					eh->data[fde_len_pos + 3] = (unsigned char)(fde_len >> 24);
				}
				/* Fixup for FDE initial_loc (PC-relative per target) */
				if (label && label[0] != '\0')
					as_add_fixup(as, eh, (size_t)fde_initial_loc_pos,
					             (unsigned)fde_addr_width,
					             as->target->dwarf_fde_reloc, 0, label);
			}
			/* terminator: length = 0 */
			dwarf_u32(as, eh, 0);
		}

		free(cie_for_fde);
		free(cie_offsets);
	}
	return 0;
}
