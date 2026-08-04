#ifndef MEUOS_INTERNAL_TLS_H
#define MEUOS_INTERNAL_TLS_H

/* src/internal/tls.h -- TLS runtime internals shared across libc files
 * (__tls_get_addr.c, arch/<arch>/tls.c, dlfcn.c) for the DTV model.
 *
 * DTV (Dynamic Thread Vector) model: each DSO with a PT_TLS is assigned a
 * 1-based module id by ld.so (rtld).  A per-thread vector maps module id to
 * that thread's TLS block.  __tls_get_addr(ti) resolves `DTV[ti_module] +
 * ti_offset`, where ti_offset is the offset within the module's TLS block
 * (NOT a TP-relative delta).
 *
 * The static (no-ld.so) path keeps module 1 only and resolves
 * `tp + ti_offset`; libc's __tls_get_addr is weak so the dynamic (DTV)
 * strong symbol exported by ld.so wins at run time.
 *
 * tls_index must stay binary-identical to rtld's own copy (rtld.c) and to
 * the compiler-issued GD descriptor. */

#include <stddef.h>

/* x86_64 TLS descriptor.  Matches rtld (src/rtld/rtld.c) and mcc's GD
 * `leaq sym@tlsgd(%rip); call __tls_get_addr` ABI. */
typedef struct {
	unsigned long ti_module;
	unsigned long ti_offset;
} tls_index;

/* TCB layout (x86_64 Variant II).  TP points at the end of the static TLS
 * block; the two machine words immediately above TP form the TCB:
 *   TP[0] (+0x00) = TP itself            (self / pthread anchor)
 *   TP[1] (+0x08) = DTV pointer          (%fs+8)
 * The DTV pointer lives at a positive offset from TP so it never overlaps
 * the module's TLS data block (which lies at [TP-tls_memsz, TP)).  rtld's
 * stage-A temporary `*(tp-8)` MUST be aligned to MEUOS_TCB_DTV_OFF.
 */
#define MEUOS_TCB_SELF_OFF 0
#define MEUOS_TCB_DTV_OFF  8
#define MEUOS_TCB_SIZE     16

/* Module TLS registration entry.  Mirrors rtld's rtld_tls_mod. */
struct tls_module {
	long modid;           /* 1-based, assigned by rtld */
	const void *tpl;      /* PT_TLS template start (in DSO image) */
	size_t filesz;        /* initialized size (.tdata) */
	size_t memsz;         /* total size (.tdata + .tbss) */
	size_t align;         /* TLS alignment */
};

/* Module TLS registry (libc storage).  rtld fills it via
 * __meuos_tls_add_module when loading a DSO with a PT_TLS; allocate_tls /
 * __meuos_tls_alloc read it to size a new thread's TLS area + DTV.  In a
 * plain static process the registry stays empty (module 1 is implicit). */
#define MEUOS_MAX_TLS_MODULES 64
extern struct tls_module __meuos_tls_modules[MEUOS_MAX_TLS_MODULES];
extern int __meuos_tls_module_count;

/* Registry interface, declared here for rtld (via its own extern) and used
 * internally by allocate_tls. */
void __meuos_tls_add_module(long modid, const void *tpl, size_t filesz,
                            size_t memsz, size_t align);
struct tls_module *__meuos_tls_lookup(long modid);

#endif /* MEUOS_INTERNAL_TLS_H */
