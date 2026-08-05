/* got.c - mt/ld GOT and TLS-descriptor collection.
 *
 * Collects GOT entries (got_index / add_got_entry / add_got_entry_tls /
 * add_got_jumpslot), scans input relocations for GOT references
 * (collect_got_relocations), and records static-executable GD/LD TLS
 * descriptors (tls_desc_index / add_tls_desc / collect_tls_descriptors).
 * Extracted from link.c (per-stage module split).
 */
#include "ld_internal.h"


int
got_index(struct ld_context *ctx, const char *name, size_t *index)
{
	size_t i;
	for (i = 0; i < ctx->got.count; ++i)
		if (strcmp(ctx->got.items[i].name, name) == 0) {
			if (index)
				*index = i;
			return 0;
		}
	return -1;
}

static size_t
total_got_slots(struct ld_context *ctx)
{
	size_t sum = 0, i;
	for (i = 0; i < ctx->got.count; ++i)
		sum += ctx->got.items[i].slots;
	return sum;
}

static size_t
total_got_slots_before(struct ld_context *ctx, size_t idx)
{
	size_t sum = 0, i;
	for (i = 0; i < idx; ++i)
		sum += ctx->got.items[i].slots;
	return sum;
}

int
add_got_entry(struct ld_context *ctx, const char *name)
{
	struct ld_got_entry *items;
	if (!name || !*name)
		return ld_error(ctx, "GOT relocation has no symbol");
	if (find_group(ctx, ".got") < 0 &&
	    get_group(ctx, ".got", MT_SHT_PROGBITS,
	              LD_SHF_ALLOC | LD_SHF_WRITE, 8) < 0)
		return ld_error(ctx, "out of memory");
	if (got_index(ctx, name, NULL) == 0)
		return 0;
	if (ctx->got.count == ctx->got.capacity) {
		size_t capacity = ctx->got.capacity ? ctx->got.capacity * 2 : 16;
		items = (struct ld_got_entry *)ld_realloc(ctx->got.items,
		                                          capacity * sizeof(*items));
		if (!items)
			return ld_error(ctx, "out of memory");
		ctx->got.items = items;
		ctx->got.capacity = capacity;
	}
	ctx->got.items[ctx->got.count].name = ld_strdup(name);
	if (!ctx->got.items[ctx->got.count].name)
		return ld_error(ctx, "out of memory");
	ctx->got.items[ctx->got.count].offset = total_got_slots(ctx) * 8;
	ctx->got.items[ctx->got.count].reloc_type = 0; /* default RELATIVE */
	ctx->got.items[ctx->got.count].tls = 0;
	ctx->got.items[ctx->got.count].slots = 1;
	++ctx->got.count;
	return 0;
}

/* Add a TLS GOT entry.  GD/LD allocate a two-slot (DTPMOD | DTPOFF) pair;
 * IE (GOTTPOFF) allocates a single slot holding the TP offset. */
static int
add_got_entry_tls(struct ld_context *ctx, const char *name, unsigned rel_type)
{
	int slots = (rel_type == LD_R_X86_64_TLSGD ||
	             rel_type == LD_R_X86_64_TLSLD) ? 2 : 1;
	size_t idx = 0;
	if (got_index(ctx, name, &idx) == 0) {
		if (slots > ctx->got.items[idx].slots)
			ctx->got.items[idx].slots = slots;
		return 0;
	}
	if (add_got_entry(ctx, name) != 0)
		return -1;
	idx = ctx->got.count - 1;
	ctx->got.items[idx].tls = 1;
	ctx->got.items[idx].slots = slots;
	/* TLS GD/LD entries that appear after a single-slot entry must be
	 * moved to a 2-slot boundary so the DTPMOD|DTPOFF pair stays aligned.
	 * Re-apply the slot offset accounting for the promoted size. */
	ctx->got.items[idx].offset = total_got_slots_before(ctx, idx) * 8;
	return 0;
}

/* Add a GOT entry for an undefined function import (PLT32).  The slot is
 * resolved by ld.so via a JUMP_SLOT dynamic relocation so `call foo@plt`
 * in a shared library reaches the correct address at load time. */
static int
add_got_jumpslot(struct ld_context *ctx, const char *name)
{
	size_t idx = 0;
	if (got_index(ctx, name, &idx) == 0) {
		/* Existing entry (e.g. from a GOTPCREL) — promote to JUMP_SLOT. */
		ctx->got.items[idx].reloc_type = MT_R_X86_64_JUMP_SLOT;
		return 0;
	}
	if (add_got_entry(ctx, name) != 0)
		return -1;
	idx = ctx->got.count - 1;
	ctx->got.items[idx].reloc_type = MT_R_X86_64_JUMP_SLOT;
	return 0;
}

int
collect_got_relocations(struct ld_context *ctx)
{
	uint16_t i;
	size_t j;
	struct mt_elf64_section section;
	struct mt_elf64_symbol symbol;
	const char *name;
	uint64_t n;
	unsigned tls_got = 0;   /* set when collecting an x86_64 TLS GOT reloc */
	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *object = &ctx->objects.items[i];
		for (j = 0; j < object_section_count(object); ++j) {
			if (object_get_section(object, (uint16_t)j, &section) != 0)
				return -1;
			if (section.type != MT_SHT_RELA && section.type != MT_SHT_REL)
				continue;
			if (object->elf_class == 1) {
				/* ELF32 RELA: 12 bytes per entry */
				uint32_t info32;
				if (section.entry_size < 12 ||
				    section.size % section.entry_size != 0)
					continue;
				for (n = 0; n < section.size / section.entry_size; ++n) {
					const unsigned char *p = object->data + section.offset + n * section.entry_size;
					info32 = read32(p + 4);
					if ((unsigned)info32 == LD_R_X86_64_GOTPCREL ||
					    (unsigned)info32 == LD_R_X86_64_REX_GOTPCRELX ||
					    (info32 & 0xff) == 75 || (info32 & 0xff) == 76) {
						uint32_t sym_idx = info32 >> 8;
						if (get_symbol_by_index(ctx, object, sym_idx, &symbol,
						                        &name) != 0 ||
						    add_got_entry(ctx, name) != 0)
							return -1;
					}
				}
			} else {
				uint64_t info64;
				if (section.entry_size < 24 ||
				    section.size % section.entry_size != 0)
					continue;
				for (n = 0; n < section.size / section.entry_size; ++n) {
					const unsigned char *p = object->data + section.offset + n * section.entry_size;
					info64 = read64(p + 8);
					if ((unsigned)info64 != LD_R_X86_64_GOTPCREL &&
					    (unsigned)info64 != LD_R_X86_64_REX_GOTPCRELX) {
						unsigned rel_type = (unsigned)(info64 & 0xffffffff);
						if (rel_type == 75 || rel_type == 76)
							goto collect_got64;
						/* x86_64 PLT32 or PC32 undefined function imports
						 * need a JUMP_SLOT GOT entry so that in a shared
						 * lib / PIE `call foo` / `call foo@plt` resolves
						 * via ld.so.  mcc emits a plain R_X86_64_PC32 for
						 * `call foo` (no @PLT suffix); treating a PC32
						 * reloc on an UNDEF symbol as a function import
						 * mirrors gas/gcc's behaviour (PC32 to an external
						 * is normally a call).  Local/data PC32 (SHN_UNDEF
						 * != symbol) is left untouched. */
						if (strcmp(ctx->target->name, "x86_64") == 0 &&
						    (ctx->shared || ctx->pie) &&
						    (rel_type == LD_R_X86_64_PLT32 ||
						     rel_type == LD_R_X86_64_PC32)) {
							if (get_symbol_by_index(ctx, object,
							                        info64 >> 32, &symbol,
							                        &name) != 0 ||
							    symbol.section != MT_SHN_UNDEF)
								goto collect_got64_skip;
							if (add_got_jumpslot(ctx, name) != 0)
								return -1;
							continue;
						}
						/* x86_64 TLS GD/LD/IE relocations need GOT slots
						 * only when the output is a shared lib or PIE
						 * (static executables relax them to Local-Exec,
						 * which needs no GOT entry). */
						if (strcmp(ctx->target->name, "x86_64") == 0 &&
						    (ctx->shared || ctx->pie) &&
						    (rel_type == LD_R_X86_64_TLSGD ||
						     rel_type == LD_R_X86_64_TLSLD ||
						     rel_type == LD_R_X86_64_GOTTPOFF)) {
							tls_got = rel_type;
							goto collect_tls_got64;
						}
						continue;
					}
					collect_got64:;
					if (get_symbol_by_index(ctx, object, info64 >> 32, &symbol,
					                        &name) != 0 ||
					    add_got_entry(ctx, name) != 0)
						return -1;
					collect_got64_skip:;
					continue;
					collect_tls_got64:;
					if (get_symbol_by_index(ctx, object, info64 >> 32, &symbol,
					                        &name) != 0 ||
					    add_got_entry_tls(ctx, name, tls_got) != 0)
						return -1;
				}
			}
		}
	}
	if (ctx->got.count != 0) {
		struct ld_group *got = &ctx->groups[find_group(ctx, ".got")];
		if (append_group_data(ctx, got, NULL, total_got_slots(ctx) * 8, 8,
		                      &n) != 0)
			return -1;
	}
	return 0;
}

/* Find an existing static GD/LD TLS descriptor by symbol name. */
int
tls_desc_index(struct ld_context *ctx, const char *name)
{
	size_t i;
	for (i = 0; i < ctx->tls_desc_count; ++i)
		if (strcmp(ctx->tls_descs[i].name, name) == 0)
			return (int)i;
	return -1;
}

/* Allocate a 16-byte `tls_index{ti_module=1,ti_offset}` descriptor for a
 * GD/LD TLS symbol in .data, and record `{name → group,offset}`.  Called
 * during the pre-layout collect phase so .data size/address is final. */
static int
add_tls_desc(struct ld_context *ctx, const char *name)
{
	struct ld_tls_desc *desc;
	int g;
	uint64_t off;
	if (tls_desc_index(ctx, name) >= 0)
		return 0;  /* already one */
	if (ctx->tls_desc_count == ctx->tls_desc_capacity) {
		size_t cap = ctx->tls_desc_capacity ? ctx->tls_desc_capacity * 2 : 8;
		desc = (struct ld_tls_desc *)ld_realloc(
		    ctx->tls_descs, cap * sizeof(*desc));
		if (!desc)
			return ld_error(ctx, "out of memory");
		ctx->tls_descs = desc;
		ctx->tls_desc_capacity = cap;
	}
	g = get_group(ctx, ".data", MT_SHT_PROGBITS,
	              LD_SHF_ALLOC | LD_SHF_WRITE, 8);
	if (g < 0)
		return ld_error(ctx, "out of memory");
	ctx->groups[g].rank = 3;  /* allocated .data region */
	if (append_group_data(ctx, &ctx->groups[g], NULL, 16, 8, &off) != 0)
		return -1;
	desc = &ctx->tls_descs[ctx->tls_desc_count];
	desc->name = ld_strdup(name);
	if (!desc->name)
		return ld_error(ctx, "out of memory");
	desc->group = g;
	desc->offset = off;
	++ctx->tls_desc_count;
	return 0;
}

/* Pre-layout collect of static-executable GD/LD TLS descriptors.  Only
 * runs for ET_EXEC (static), matching the write_relocation path that
 * relaxes GD/LD via a static tls_index descriptor. */
int
collect_tls_descriptors(struct ld_context *ctx)
{
	uint16_t i;
	size_t j;
	uint64_t n;
	struct mt_elf64_section section;
	struct mt_elf64_symbol symbol;
	const char *name;
	if (ctx->shared || ctx->pie)
		return 0;  /* GD handled dynamically via GOT for shared/PIE */
	if (strcmp(ctx->target->name, "x86_64") != 0)
		return 0;
	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *object = &ctx->objects.items[i];
		for (j = 0; j < object_section_count(object); ++j) {
			if (object_get_section(object, (uint16_t)j, &section) != 0)
				return -1;
			if (section.type != MT_SHT_RELA && section.type != MT_SHT_REL)
				continue;
			if (object->elf_class == 1)
				continue;  /* 32-bit not handled here */
			if (section.entry_size < 24 ||
			    section.size % section.entry_size != 0)
				continue;
			for (n = 0; n < section.size / section.entry_size; ++n) {
				const unsigned char *p = object->data + section.offset +
				                          n * section.entry_size;
				uint64_t info64 = read64(p + 8);
				unsigned rel_type = (unsigned)(info64 & 0xffffffff);
				if (rel_type != LD_R_X86_64_TLSGD &&
				    rel_type != LD_R_X86_64_TLSLD)
					continue;
				if (get_symbol_by_index(ctx, object, info64 >> 32,
				                        &symbol, &name) != 0 ||
				    add_tls_desc(ctx, name) != 0)
					return -1;
			}
		}
	}
	return 0;
}
