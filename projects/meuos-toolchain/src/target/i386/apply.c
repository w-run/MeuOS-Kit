/* apply.c — i386 relocation application.
 *
 * Applies i386 relocations against a linked output section.
 * Called by the linker after layout to patch the final binary content. */

#include <stdint.h>
#include <string.h>

/* Apply a single i386 relocation.
 *
 * reloc_type:   R_386_* constant
 * place:        4-byte aligned location to patch (in-place)
 * S:            symbol value (final linked address)
 * A:            addend (from the relocation entry)
 * P:            position of the place (the offset being relocated)
 *
 * Returns 0 on success, -1 on unsupported relocation type. */
int
i386_apply_reloc(unsigned reloc_type, unsigned char *place,
                 uint64_t S, int64_t A, uint64_t P)
{
	uint32_t value;

	switch (reloc_type) {
	case 0:  /* R_386_NONE */
		return 0;

	case 1:  /* R_386_32 — S + A */
		value = (uint32_t)(S + (uint64_t)A);
		place[0] = (unsigned char)value;
		place[1] = (unsigned char)(value >> 8);
		place[2] = (unsigned char)(value >> 16);
		place[3] = (unsigned char)(value >> 24);
		return 0;

	case 2:  /* R_386_PC32 — S + A - P */
		value = (uint32_t)(S + (uint64_t)A - P);
		place[0] = (unsigned char)value;
		place[1] = (unsigned char)(value >> 8);
		place[2] = (unsigned char)(value >> 16);
		place[3] = (unsigned char)(value >> 24);
		return 0;

	case 11: /* R_386_32S — same as R_386_32 */
		value = (uint32_t)(S + (uint64_t)A);
		place[0] = (unsigned char)value;
		place[1] = (unsigned char)(value >> 8);
		place[2] = (unsigned char)(value >> 16);
		place[3] = (unsigned char)(value >> 24);
		return 0;

	case 10: /* R_386_GOTPC — GOT + A - P */
		/* GOT base address must be provided separately; stub */
		return -1;

	case 4:  /* R_386_PLT32 — L + A - P */
		/* PLT not yet implemented; fallback to PC32 */
		value = (uint32_t)(S + (uint64_t)A - P);
		place[0] = (unsigned char)value;
		place[1] = (unsigned char)(value >> 8);
		place[2] = (unsigned char)(value >> 16);
		place[3] = (unsigned char)(value >> 24);
		return 0;

	/* R_386_TLS_LE: Local Exec TLS — store (S + A - tp) for static exe.
	 * In a static executable, tp = 0 (thread pointer base = 0), so
	 * the value is simply S + A written as a 32-bit absolute. */
	case 12: /* R_386_TLS_LE_DEFAULT */
	case 17: /* R_386_TLS_LE — assumes tp = 0 for static link */
		value = (uint32_t)(S + (uint64_t)A);
		goto write32;
	case 13: /* R_386_TLS_LE_32 — same as TLS_LE but explicitly 32-bit */
		value = (uint32_t)(S + (uint64_t)A);
		goto write32;

	default:
		return -1; /* unsupported */
	}
write32:	/* Common 32-bit write for TLS_LE variants */
	place[0] = (unsigned char)value;
	place[1] = (unsigned char)(value >> 8);
	place[2] = (unsigned char)(value >> 16);
	place[3] = (unsigned char)(value >> 24);
	return 0;
}
