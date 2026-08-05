/* layout.c - mt/ld section address layout.
 *
 * Assigns output-group virtual addresses and file offsets after
 * collecting all input sections (layout_output), honoring a linker
 * script (apply_link_script) for section ordering.  Extracted from
 * link.c (per-stage module split; shared state in ld_context).
 */
#include "ld_internal.h"


int
apply_link_script(struct ld_context *ctx, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return ld_errorf(ctx, "cannot open link script", path);

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == '\n' || *p == '\0') continue;

		/* Parse: section_name = rank_number */
		char name[128];
		int n = 0;
		while (*p && *p != '=' && *p != ' ' && *p != '\t' && n < 126)
			name[n++] = *p++;
		name[n] = '\0';
		while (*p && (*p == ' ' || *p == '\t' || *p == '=')) p++;
		if (*p) {
			int rank = (int)strtol(p, NULL, 10);
			int g = find_group(ctx, name);
			if (g >= 0)
				ctx->groups[g].rank = rank;
		}
	}
	fclose(f);
	return 0;
}

int
layout_output(struct ld_context *ctx)
{
	uint64_t offset = LD_PAGE;
	/* For shared libraries (ET_DYN) the base load address is 0; the
	 * dynamic loader chooses the final address at load time. */
	uint64_t base = ctx->shared ? 0 : LD_BASE;
	int have_rw = 0;
	int rank;
	size_t i;

	ctx->tls_tdata_group = find_group(ctx, ".tdata");
	ctx->tls_tbss_group = find_group(ctx, ".tbss");
	ctx->tls_tdata_size = 0;
	ctx->tls_size = 0;
	ctx->tls_align = 1;

	/* Calculate TLS block size (variant II: tdata then tbss). */
	if (ctx->tls_tdata_group >= 0 || ctx->tls_tbss_group >= 0) {
		uint64_t tdata = 0, tbss = 0, talign = 1, tbss_align = 1;
		if (ctx->tls_tdata_group >= 0) {
			tdata = ctx->groups[ctx->tls_tdata_group].size;
			talign = ctx->groups[ctx->tls_tdata_group].align;
		}
		if (ctx->tls_tbss_group >= 0) {
			tbss = ctx->groups[ctx->tls_tbss_group].size;
			tbss_align = ctx->groups[ctx->tls_tbss_group].align;
			if (tbss_align > talign)
				talign = tbss_align;
		}
		ctx->tls_tdata_size = tdata;
		ctx->tls_align = talign;
		/* variant II: .tbss starts at the .tbss-aligned boundary right
		 * after .tdata (NOT rounded up to talign — that would over-round
		 * a small .tdata and inflate memsz, e.g. 4-byte .tdata align 8 +
		 * 4-byte .tbss would yield 16 instead of 8).  Only the final
		 * memsz is rounded up to the max TLS alignment. */
		ctx->tls_size = align_up(align_up(tdata, tbss_align) + tbss, talign);
	}

	/* Pass 1: PROGBITS sections (skip TLS .tdata).
	 * rank 0-4 are loaded (ALLOC) sections; .debug/.comment/.note and
	 * other non-allocatable sections (rank 5+) are laid out in Pass 3 so
	 * that .bss (NOBITS, rank 4) ends up before them. */
	for (rank = 0; rank <= 4; ++rank) {
		if (rank >= 2 && !have_rw) {
			offset = align_up(offset, LD_PAGE);
			have_rw = 1;
		}
		for (i = 0; i < ctx->group_count; ++i) {
			struct ld_group *group = &ctx->groups[i];
			if (group->rank != rank || group->type == MT_SHT_NOBITS)
				continue;
			if (is_tls_group(ctx, (int)i))
				continue;
			offset = align_up(offset, group->align ? group->align : 1);
			group->file_offset = offset;
			group->address = base + offset;
			offset += group->size;
		}
	}

	/* Pass 2: NOBITS sections (skip TLS .tbss) - after PROGBITS rank 0-4.
	 * .bss (rank 4) must come before non-allocatable sections (rank 5+),
	 * so only ranks 2-4 are handled here. */
	for (rank = 2; rank <= 4; ++rank) {
		for (i = 0; i < ctx->group_count; ++i) {
			struct ld_group *group = &ctx->groups[i];
			if (group->rank != rank || group->type != MT_SHT_NOBITS)
				continue;
			if (is_tls_group(ctx, (int)i))
				continue;
			offset = align_up(offset, group->align ? group->align : 1);
			group->file_offset = offset;
			group->address = base + offset;
			/* NOBITS: does not advance file offset */
		}
	}

	/* Advance offset past non-TLS sections so TLS does not overlap .bss */
	for (i = 0; i < ctx->group_count; ++i) {
		struct ld_group *g = &ctx->groups[i];
		if (is_tls_group(ctx, (int)i)) continue;
		uint64_t e = g->file_offset + g->size;
		if (e > offset) offset = e;
	}

	/* TLS sections: lay out .tdata (needs file space) then .tbss (NOBITS).
	 * TLS must be laid out BEFORE the non-allocatable Pass 3 below, so that
	 * .comment/.debug/.note land at higher file offsets than the allocatable
	 * (.tdata) file region.  Otherwise a non-alloc section sharing the LOAD
	 * file span (e.g. .comment at the same offset as .bss) gets its file
	 * bytes loaded over .bss, zeroing/clobbering runtime state (ctor arrays,
	 * counters) → static TLS programs segfault. */
	if (ctx->tls_tdata_group >= 0) {
		struct ld_group *g = &ctx->groups[ctx->tls_tdata_group];
		offset = align_up(offset, g->align ? g->align : 1);
		g->file_offset = offset;
		/* .tdata gets its real vaddr (base + offset) for BOTH shared and
		 * PIE.  Previously a shared library forced .tdata vaddr = 0,
		 * which made PT_TLS p_vaddr = 0 while the .tdata bytes actually
		 * lived at file offset p_offset (> 0).  The dynamic loader copies
		 * the TLS template from load_base + p_vaddr, so a 0 vaddr pointed
		 * it at the ELF header instead of the .tdata data, corrupting
		 * every lazy-allocated TLS block. */
		g->address = base + offset;
		offset += g->size;
	}
	if (ctx->tls_tbss_group >= 0) {
		struct ld_group *g = &ctx->groups[ctx->tls_tbss_group];
		if (ctx->tls_tdata_group >= 0) {
			struct ld_group *td = &ctx->groups[ctx->tls_tdata_group];
			/* .tbss starts at the .tbss-aligned boundary after .tdata.
			 * (.tbss is NOBITS, so file space isn't needed here.) */
			uint64_t tdata_end = td->file_offset + td->size;
			uint64_t tbss_off = align_up(tdata_end, g->align ? g->align : 1);
			g->file_offset = tbss_off;
			g->address = base + tbss_off;
		} else {
			/* No .tdata: allocate beyond non-TLS sections */
			offset = align_up(offset, g->align ? g->align : 1);
			g->file_offset = offset;
			g->address = base + offset;
			if (g->type != MT_SHT_NOBITS)
				offset += g->size;
			}
		}

	/* Pass 3: non-allocatable sections (.debug*, .comment, .note, ...).
	 * These carry no loadable content, so place them after every loaded
	 * section (PROGBITS, NOBITS and TLS alike) — beyond any PT_LOAD file
	 * span, so their bytes are never mapped into an allocatable segment. */
	for (rank = 5; rank <= 6; ++rank) {
		for (i = 0; i < ctx->group_count; ++i) {
			struct ld_group *group = &ctx->groups[i];
			if (group->rank != rank)
				continue;
			if (is_tls_group(ctx, (int)i))
				continue;
			offset = align_up(offset, group->align ? group->align : 1);
			group->file_offset = offset;
			group->address = base + offset;
			if (group->type != MT_SHT_NOBITS)
				offset += group->size;
		}
	}
	return 0;
}
