/* reloc.c - mt/ld relocation application.
 *
 * Applies per-architecture relocations across all output sections
 * (write_relocation over each .rel* input), fills the GOT (fill_got) and
 * drives apply_relocations.  Extracted from link.c (per-stage module
 * split; shared state in struct ld_context via ld_internal.h).
 */
#include "ld_internal.h"


static int
symbol_tls_offset(struct ld_context *ctx, struct ld_object *object,
                  uint64_t symbol_index, uint64_t *tls_offset)
{
	struct mt_elf64_symbol symbol;
	const char *name;
	struct ld_global *global;
	struct ld_section_map *map;
	/* Base offset of .tbss within the TLS block: the .tbss-aligned
	 * boundary right after .tdata (matches tls_size / PT_TLS memsz). */
	uint64_t tbss_base = ctx->tls_tdata_size;
	if (ctx->tls_tbss_group >= 0) {
		uint64_t a = ctx->groups[ctx->tls_tbss_group].align;
		tbss_base = align_up(tbss_base, a ? a : 1);
	}
	if (get_symbol_by_index(ctx, object, symbol_index, &symbol, &name) != 0)
		return -1;
	if (symbol.section == MT_SHN_UNDEF) {
		global = find_global(ctx, name);
		if (!global)
			return ld_errorf(ctx, "undefined symbol", name);
		/* Follow alias chain (from --wrap) */
		while (global->alias)
			global = global->alias;
		if (!global->defined)
			return ld_errorf(ctx, "undefined symbol", name);
		if (global->group == ctx->tls_tdata_group)
			*tls_offset = global->offset + symbol.value;
		else if (global->group == ctx->tls_tbss_group)
			*tls_offset = tbss_base + global->offset + symbol.value;
		else
			return ld_errorf(ctx, "TPOFF32 relocation against non-TLS symbol", name);
		return 0;
	}
	if (symbol.section >= object_section_count(object) ||
	    object->maps[symbol.section].group < 0)
		return ld_errorf(ctx, "symbol section was discarded", name);
	map = &object->maps[symbol.section];
	if (map->group == ctx->tls_tdata_group)
		*tls_offset = map->offset + symbol.value;
	else if (map->group == ctx->tls_tbss_group)
		*tls_offset = tbss_base + map->offset + symbol.value;
	else
		return ld_errorf(ctx, "TPOFF32 relocation against non-TLS symbol", name);
	return 0;
}

/* Width in bytes of the relocated field for the given architecture and
 * relocation type.  64-bit absolute data relocations (R_X86_64_64,
 * R_AARCH64_ABS64, R_RISCV_64, R_LARCH_64) write 8 bytes; every other
 * type emitted by this toolchain patches a 4-byte field.  The check must
 * be architecture-aware because type numbers overlap across targets
 * (e.g. R_LARCH_64 == R_RISCV_64 == 2 == LD_R_X86_64_PC32). */
static unsigned
reloc_field_width(const struct ld_context *ctx, int type)
{
	const char *t = ctx->target->name;
	if (strcmp(t, "x86_64") == 0 && type == LD_R_X86_64_64)
		return 8;
	if (strcmp(t, "aarch64") == 0 && type == 257)    /* R_AARCH64_ABS64 */
		return 8;
	if (strcmp(t, "riscv64") == 0 && type == 2)      /* R_RISCV_64 */
		return 8;
	if (strcmp(t, "loongarch64") == 0 && type == 2)  /* R_LARCH_64 */
		return 8;
	return 4;
}

int
write_relocation(struct ld_context *ctx, struct ld_object *object,
                 const struct mt_elf64_section *reloc_section,
                 struct ld_group *target, uint64_t reloc_index)
{
	const unsigned char *p = object->data + reloc_section->offset +
	                         reloc_index * reloc_section->entry_size;
	uint64_t offset, info, symbol_index;
	int64_t addend;
	int type;
	if (object->elf_class == 1) {
		if (reloc_section->type == MT_SHT_RELA) {
			/* ELF32 RELA: 12-byte entry */
			offset = read32(p + 0);
			info = read32(p + 4);
			type = (int)(info & 0xff);
			symbol_index = info >> 8;
			addend = (int32_t)read32(p + 8);
		} else {
			/* ELF32 REL: 8-byte entry (ARM, i386).
			 * Addend is implicit in the place being relocated.
			 * Set addend=0; arch-specific apply function extracts
			 * the value from the instruction/data location. */
			offset = read32(p + 0);
			info = read32(p + 4);
			type = (int)(info & 0xff);
			symbol_index = info >> 8;
			addend = 0;
		}
	} else {
		offset = read64(p + 0);
		info = read64(p + 8);
		type = (int)(info & 0xffffffffu);
		symbol_index = info >> 32;
		addend = (int64_t)read64(p + 16);
	}
	uint64_t resolved_value = 0;
	const char *name;
	uint64_t place;
	uint64_t value;
	size_t got;
	struct ld_group *got_group;
	uint64_t target_offset;
	unsigned width;

	/* Skip relocations targeting GC'd (--gc-sections) sections */
	if (target->size == 0) return 0;
	if (offset > target->size ||
	    reloc_field_width(ctx, type) > target->size - offset)
		return ld_error(ctx, "relocation offset is outside output section");
	if (symbol_value(ctx, object, symbol_index, &resolved_value, &name) != 0)
		return -1;
	target_offset = (uint64_t)object->maps[reloc_section->info].offset + offset;
	place = target->address + target_offset;

	/* Per-architecture relocation dispatch.
	 *
	 * WARNING: Architecture-specific relocation type numbers may overlap
	 * with x86_64's LD_R_X86_64_* constants (e.g. R_LARCH_64 == 2 ==
	 * LD_R_X86_64_PC32).  The per-arch check MUST come before the x86_64
	 * type-number comparisons so that the correct formula is used. */
	if (strcmp(ctx->target->name, "loongarch64") == 0) {
		uint64_t la64_got_addr;
		size_t la64_got_idx;

		/* TLS LE relocations need TP-relative offset, not full VA */
		if (type == 83 || type == 84 || type == 85 || type == 86) {
			uint64_t tls_off;
			if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
				return ld_errorf(ctx, "unsupported TLS relocation", name);
			resolved_value = tls_off;
		}

		/* GOT-based relocations: route via GOT entry (fill_got handles the data) */
		if (type == 75 || type == 76) { /* R_LARCH_GOT_PC_HI20 / GOT_PC_LO12 */
			if (got_index(ctx, name, &la64_got_idx) != 0)
				return ld_errorf(ctx, "missing GOT entry", name);
			la64_got_addr = ctx->groups[ctx->got.group].address +
			                ctx->got.items[la64_got_idx].offset;
			if (la64_apply_reloc(type, target->data + target_offset,
			                     la64_got_addr, addend, place) == 0)
				return 0;
		} else if (la64_apply_reloc(type, target->data + target_offset,
		                            resolved_value, addend, place) == 0) {
			return 0;
		}
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (strcmp(ctx->target->name, "aarch64") == 0) {
		/* TLSLE relocations need TP-relative offset, not full VA:
		 *   R_AARCH64_TLSLE_ADD_TPREL_HI12     = 549
		 *   R_AARCH64_TLSLE_ADD_TPREL_LO12_NC  = 551
		 * mcc emits these for _Thread_local access; the linker must
		 * resolve the symbol to its offset within the PT_TLS image,
		 * not its linked virtual address.  aarch64 uses GAP_ABOVE_TP
		 * = 16 (musl ABI: TPIDR_EL0 addresses the mmap base; .tdata
		 * is copied at TP+16), so the TP-relative offset must include
		 * the 16-byte TCB reservation. */
		if (type == 549 || type == 551) {
			uint64_t tls_off;
			if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
				return ld_errorf(ctx, "unsupported TLS relocation", name);
			resolved_value = tls_off + 16; /* GAP_ABOVE_TP */
		}
		if (mt_apply_aarch64_reloc(type, target->data + target_offset,
		                           resolved_value, addend, place) == 0)
			return 0;
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (strcmp(ctx->target->name, "arm") == 0) {
		/* TLS local-exec relocations need TP-relative offset, not the
		 * linked VA.  arm uses GAP_ABOVE_TP = 16 (musl ABI, same as
		 * aarch64: the thread pointer addresses the mmap base; .tdata
		 * is copied at TP+16), so the offset includes the 16-byte TCB
		 * reservation.  R_ARM_TLS_LE32 = 107/108, R_ARM_TLS_LE12 =
		 * 110/111 (mcc's #:tprel_hi12:/#:tprel_lo12: adds). */
		if (type == 107 || type == 108 || type == 110 || type == 111) {
			uint64_t tls_off;
			if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
				return ld_errorf(ctx, "unsupported TLS relocation", name);
			resolved_value = tls_off + 16; /* GAP_ABOVE_TP */
		}
		if (mt_apply_arm_reloc(type, target->data + target_offset,
		                       resolved_value, addend, place) == 0)
			return 0;
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (strcmp(ctx->target->name, "riscv64") == 0) {
		/* TLS LE relocations need TP-relative offset, not full VA.
		 * Two numbering schemes appear in practice:
		 *   - standard psABI: TPREL_ADD=38, TPREL_LO12_I=39,
		 *     TPREL_LO12_S=40, TPREL_HI20=41 (mt/as emits these)
		 *   - GNU as/binutils compatibility numbers: TPREL_HI20=29,
		 *     TPREL_LO12_I=30, TPREL_ADD=32 (libc objects built with
		 *     riscv64-linux-gnu-gcc emit these)
		 * Both must resolve the symbol to its TP-relative offset. */
		if (type == 38 || type == 39 || type == 40 || type == 41 ||
		    type == 29 || type == 30 || type == 32) {
			uint64_t tls_off;
			if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
				return ld_errorf(ctx, "unsupported TLS relocation", name);
			resolved_value = tls_off;
		}
		/* R_RISCV_PCREL_LO12_I/S (24/25): the relocation references a
		 * local label (.L0) placed on the paired auipc instruction, not
		 * the final symbol.  The low 12 bits must be computed from the
		 * paired R_RISCV_PCREL_HI20 (23) relocation:
		 *     lo12 = (S_hi + A_hi - P_hi) & 0xFFF
		 * where P_hi is the auipc instruction address.  Without this,
		 * the LO12 operand is wrong whenever the auipc position's low
		 * 12 bits differ from the target symbol's. */
		if (type == 24 || type == 25) {
			struct mt_elf64_symbol lo_sym;
			const char *lo_name;
			uint64_t lo_sym_value = 0;
			uint64_t p_hi = resolved_value;  /* .L0 解析地址 = auipc 位置 */
			uint64_t p_off;
			uint64_t n;
			uint64_t hi_sym = 0;
			int64_t hi_addend = 0;
			int found = 0;
			unsigned char *q;
			/* The LO12 relocation's symbol is a local label (.L0)
			 * placed on the paired auipc instruction; its value is the
			 * auipc's offset within the source section.  Find the
			 * R_RISCV_PCREL_HI20 (23) relocation at that same offset. */
			if (get_symbol_by_index(ctx, object, symbol_index, &lo_sym,
			                        &lo_name) == 0)
				lo_sym_value = lo_sym.value;
			p_off = lo_sym_value;
			/* Scan the same relocation section for a PCREL_HI20 (23)
			 * whose offset matches the auipc instruction. */
			for (n = 0; n < reloc_section->size / reloc_section->entry_size; ++n) {
				uint64_t roff, rinfo, rtype, rsym;
				int64_t raddend;
				if (object->elf_class == 1) {
					q = (unsigned char *)(object->data + reloc_section->offset +
					                      n * reloc_section->entry_size);
					if (reloc_section->type == MT_SHT_RELA) {
						roff = read32(q + 0);
						rinfo = read32(q + 4);
						raddend = (int32_t)read32(q + 8);
					} else {
						roff = read32(q + 0);
						rinfo = read32(q + 4);
						raddend = 0;
					}
					rtype = rinfo & 0xff;
					rsym = rinfo >> 8;
				} else {
					q = (unsigned char *)(object->data + reloc_section->offset +
					                      n * reloc_section->entry_size);
					roff = read64(q + 0);
					rinfo = read64(q + 8);
					rtype = rinfo & 0xffffffffu;
					rsym = rinfo >> 32;
					raddend = (int64_t)read64(q + 16);
				}
				if (rtype == 23 && roff == p_off) {
					hi_sym = rsym;
					hi_addend = raddend;
					found = 1;
					break;
				}
			}
			if (!found)
				return ld_errorf(ctx, "PCREL_LO12 without paired HI20", name);
			if (symbol_value(ctx, object, hi_sym, &value, &name) != 0)
				return -1;
			/* lo12 = (S_hi + A_hi - P_hi) & 0xFFF, where P_hi is the
			 * resolved address of the .L0 label (the auipc position). */
			resolved_value = value + (uint64_t)hi_addend - p_hi;
		}
		if (riscv64_apply_reloc(type, target->data + target_offset,
		                         resolved_value, addend, place) == 0)
			return 0;
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (strcmp(ctx->target->name, "i386") == 0) {
		if (type == LD_R_386_TLS_GD || type == LD_R_386_TLS_LDM) {
			/* Dynamic TLS model (GD/LD).  For a static executable
			 * the linker knows the final TP-relative offset.  For a
			 * shared library the relocation is preserved for ld.so. */
			if (!ctx->shared && !ctx->pie) {
				uint64_t tls_off;
				if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
					return -1;
				/* Relax to Local-Exec TPOFF32: value = sym_tpoff */
				uint32_t val = (uint32_t)((int64_t)tls_off - (int64_t)ctx->tls_size);
				target->data[target_offset + 0] = (unsigned char)val;
				target->data[target_offset + 1] = (unsigned char)(val >> 8);
				target->data[target_offset + 2] = (unsigned char)(val >> 16);
				target->data[target_offset + 3] = (unsigned char)(val >> 24);
				return 0;
			}
			/* Shared library or PIE: leave for ld.so via GOT */
			return 0;
		}
		if (type == 17 /* R_386_TLS_LE */) {
			/* Local-Exec TLS (variant II): the symbol resolves to a
			 * negative offset from the %gs thread pointer, which the
			 * TCB places after the TLS image (tls_size).  mcc emits
			 * `movl %gs:sym@ntpoff, %reg`; the disp32 holds
			 * (tls_off - tls_size), not the symbol's linked VA. */
			uint64_t tls_off;
			if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
				return -1;
			uint32_t val = (uint32_t)((int64_t)tls_off - (int64_t)ctx->tls_size)
			               + (uint32_t)addend;
			target->data[target_offset + 0] = (unsigned char)val;
			target->data[target_offset + 1] = (unsigned char)(val >> 8);
			target->data[target_offset + 2] = (unsigned char)(val >> 16);
			target->data[target_offset + 3] = (unsigned char)(val >> 24);
			return 0;
		}
		if (i386_apply_reloc(type, target->data + target_offset,
		                     resolved_value, addend, place) == 0)
			return 0;
		return ld_errorf(ctx, "unsupported relocation type", name);
	}

	/* x86_64-specific type dispatch.  These type numbers are ONLY valid
	 * for x86_64 — arch-specific dispatch above handles all others. */
	if (strcmp(ctx->target->name, "x86_64") != 0)
		return ld_errorf(ctx, "unsupported relocation type", name);
	if (type == LD_R_X86_64_GOTPCREL || type == LD_R_X86_64_REX_GOTPCRELX) {
		if (got_index(ctx, name, &got) != 0)
			return ld_errorf(ctx, "missing GOT entry", name);
		got_group = &ctx->groups[ctx->got.group];
		value = got_group->address + ctx->got.items[got].offset + addend - place;
		width = 4;
	} else if (type == LD_R_X86_64_PLT32) {
		/* PLT32: call/jmp to a function.  In shared-library OR PIE
		 * mode, undefined external symbols must go through the GOT so
		 * ld.so can resolve them at load time via a JUMP_SLOT dynamic
		 * relocation.  (A PIE's `call foo@PLT` otherwise gets a static
		 * self-relative call to address 0 and crashes.) */
		if ((ctx->shared || ctx->pie) && resolved_value == 0) {
			size_t got_idx = 0;
			if (add_got_entry(ctx, name) != 0)
				return ld_errorf(ctx, "cannot create GOT entry for %s", name);
			got_index(ctx, name, &got_idx); /* succeeds after add */
			got = got_idx;
			ctx->got.items[got].reloc_type = MT_R_X86_64_JUMP_SLOT;
			/* The call targets this symbol's PLT stub (`jmp *GOT(%rip)`),
			 * whose address is known after layout.  The stub then jumps
			 * through the GOT slot that ld.so fills with the resolved
			 * function address. */
			int pgl = find_group(ctx, ".plt");
			if (pgl >= 0) {
				value = ctx->groups[pgl].address +
				        ctx->got.items[got].plt_offset +
				        addend - place;
			} else {
				got_group = &ctx->groups[ctx->got.group];
				value = got_group->address + ctx->got.items[got].offset +
				        addend - place;
			}
		} else {
			value = resolved_value + addend - place;
		}
		width = 4;
	} else if (type == LD_R_X86_64_PC32) {
		value = resolved_value + addend - place;
		width = 4;
	} else if (type == LD_R_X86_64_TLSGD || type == LD_R_X86_64_TLSLD ||
	           type == LD_R_X86_64_DTPOFF || type == LD_R_X86_64_DTPMOD ||
	           type == LD_R_X86_64_GOTTPOFF) {
		/* Dynamic TLS model (GD/LD/IE).  For a static executable the
		 * linker knows the final TP-relative offset, so we relax to
		 * Local-Exec (TPOFF32).  For a shared library the %rip-relative
		 * field is patched to point at the symbol's GOT slot, which
		 * ld.so fills at load time. */
		if (!ctx->shared && !ctx->pie) {
			/* Static executable: GD/LD relaxes via a static tls_index
			 * descriptor (方案B).  The compiler emitted
			 *   lea sym@tlsgd(%rip),%rdi; call __tls_get_addr
			 * which __tls_get_addr resolves to tp + ti->ti_offset.  We
			 * allocate a `{ti_module=1, ti_offset}` descriptor in .data
			 * (collect_tls_descriptors) and point the lea at it, keeping
			 * the call.  This needs no instruction rewrite / no size
			 * growth; DTPOFF (absolute) resolves to the raw offset. */
			if (type == LD_R_X86_64_DTPOFF) {
				value = resolved_value + addend;
				width = 4;
			} else if (type == LD_R_X86_64_TLSGD ||
			           type == LD_R_X86_64_TLSLD) {
				int d = tls_desc_index(ctx, name);
				struct ld_group *dg;
				uint64_t tls_off, ti_offset;
				uint64_t desc_addr;
				if (d < 0)
					return ld_errorf(ctx, "missing static TLS descriptor",
					                 name);
				dg = &ctx->groups[ctx->tls_descs[d].group];
				if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
					return -1;
				ti_offset = (uint64_t)((int64_t)tls_off -
				                       (int64_t)ctx->tls_size);
				desc_addr = dg->address + ctx->tls_descs[d].offset;
				/* Write {ti_module=1, ti_offset} into the descriptor. */
				if (dg->type != MT_SHT_NOBITS &&
				    ctx->tls_descs[d].offset + 16 <= dg->size) {
					write64(dg->data + ctx->tls_descs[d].offset, 1);
					write64(dg->data + ctx->tls_descs[d].offset + 8, ti_offset);
				}
				/* Point the lea @tlsgd(%rip) reloc field at the desc. */
				value = desc_addr + addend - place;
				width = 4;
			} else {
				uint64_t tls_off;
				if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
					return -1;
				value = (uint64_t)((int64_t)tls_off - (int64_t)ctx->tls_size) + addend;
				width = 4;
			}
		} else {
			/* Shared library or PIE: patch the %rip-relative field
			 * (GLD/LD `lea ...(%rip)` and IE `movq ...(%rip)`) to the
			 * symbol's GOT slot; ld.so resolves the DTPMOD|DTPOFF /
			 * TPOFF contents via .rela.dyn. */
			if (type == LD_R_X86_64_DTPOFF || type == LD_R_X86_64_DTPMOD) {
				/* DTPOFF/DTPMOD alone are not %rip-relative; they are
				 * consumed as GOT-pair contents, left for ld.so. */
				return 0;
			}
			size_t got;
			if (got_index(ctx, name, &got) != 0)
				return ld_errorf(ctx, "missing TLS GOT entry", name);
			got_group = &ctx->groups[ctx->got.group];
			value = got_group->address + ctx->got.items[got].offset + addend - place;
			width = 4;
		}
	} else if (type == LD_R_X86_64_64) {
		value = resolved_value + addend;
		width = 8;
	} else if (type == LD_R_X86_64_32 || type == LD_R_X86_64_32S) {
		value = resolved_value + addend;
		width = 4;
	} else if (type == LD_R_X86_64_TPOFF32) {
		/* Local-Exec TLS: resolve to negative TP offset.
		 * Used by static _Thread_local variables like errno_value. */
		uint64_t tls_off;
		if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
			return -1;
		value = (uint64_t)((int64_t)tls_off - (int64_t)ctx->tls_size) + addend;
		width = 4;
	} else {
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (target->type == MT_SHT_NOBITS || target_offset > target->size ||
	    width > target->size - target_offset)
		return ld_error(ctx, "relocation targets non-file section");
	if (width == 8)
		write64(target->data + target_offset, value);
	else
		write32(target->data + target_offset, (uint32_t)value);
	return 0;
}

int
apply_relocations(struct ld_context *ctx)
{
	size_t i;
	uint16_t j;
	uint64_t n;
	struct mt_elf64_section section;
	int group;
	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *object = &ctx->objects.items[i];
		/* A shared-library (.so) input carries its own dynamic
		 * relocations (DT_RELA etc.) that ld.so applies at load time;
		 * the static linker must not process them.  Skipping avoids the
		 * bogus "relocation target was discarded" on the .so's
		 * .rela.dyn (whose sh_info points at section 0, which has no
		 * output-section group). */
		if (object->is_shared)
			continue;
		for (j = 0; j < object_section_count(object); ++j) {
			if (object_get_section(object, j, &section) != 0)
				return ld_errorf(ctx, "invalid relocation section", object->name);
			if (section.type != MT_SHT_RELA && section.type != MT_SHT_REL)
				continue;
			if (section.info >= object_section_count(object) ||
			    object->maps[section.info].group < 0)
				return ld_error(ctx, "relocation target was discarded");
			group = object->maps[section.info].group;
			/* Skip relocations targeting GC'd sections */
			if (ctx->groups[group].size == 0) continue;
			/* Reject malformed relocation tables: sh_entsize==0 (with
			 * a non-empty table) would divide by zero in the loop below. */
			if (section.size != 0 &&
			    (section.entry_size == 0 ||
			     section.size % section.entry_size != 0))
				return ld_errorf(ctx, "invalid relocation table", object->name);
			for (n = 0; n < section.size / section.entry_size; ++n)
				if (write_relocation(ctx, object, &section,
				                    &ctx->groups[group], n) != 0)
					return -1;
		}
	}
	return 0;
}

int
fill_got(struct ld_context *ctx)
{
	size_t i;
	struct ld_group *got;
	struct ld_global *global;
	uint64_t value;
	if (ctx->got.count == 0)
		return 0;
	got = &ctx->groups[ctx->got.group];
	for (i = 0; i < ctx->got.count; ++i) {
		if (ctx->got.items[i].tls ||
		    ctx->got.items[i].reloc_type == MT_R_X86_64_JUMP_SLOT)
			continue;  /* TLS slots + PLT imports filled by ld.so */
		global = find_global(ctx, ctx->got.items[i].name);
		if (global && global->alias)
			global = global->alias;
		if (!global || !global->defined)
			return ld_errorf(ctx, "undefined GOT symbol", ctx->got.items[i].name);
		value = ctx->groups[global->group].address + global->offset;
		write64(got->data + ctx->got.items[i].offset, value);
	}
	/* Fill the .plt stub disp32 fields: each `jmp *GOT_slot(%rip)` stub
	 * needs `disp32 = GOT_slot_addr - stub_addr - 4`.  Both addresses are
	 * known only after layout, which has now run. */
	if (ctx->shared || ctx->pie) {
		int pg = find_group(ctx, ".plt");
		if (pg >= 0) {
			uint64_t plt_addr = ctx->groups[pg].address;
			for (i = 0; i < ctx->got.count; ++i) {
				if (ctx->got.items[i].reloc_type != MT_R_X86_64_JUMP_SLOT)
					continue;
				uint64_t got_slot = ctx->groups[ctx->got.group].address +
				                    ctx->got.items[i].offset;
				uint64_t stub_addr = plt_addr + ctx->got.items[i].plt_offset;
				/* The jmp is 6 bytes (ff 25 disp32); the effective
				 * address is (stub_addr + 6) + disp, so disp = target
				 * - (stub_addr + 6). */
				int64_t disp = (int64_t)got_slot - (int64_t)stub_addr - 6;
				unsigned char *stub = ctx->groups[pg].data +
				                      ctx->got.items[i].plt_offset;
				/* disp32 lives at stub+2 (after `ff 25`). */
				write32(stub + 2, (uint32_t)(uint64_t)disp);
			}
		}
	}
	return 0;
}
