#ifndef MT_LD_H
#define MT_LD_H

#include <stddef.h>

/* Linker options structure for extended output control.
 * Default (zero-initialized) provides the original ET_EXEC static link. */
struct mt_ld_options {
	const char *output;    /* output file path */
	const char *entry;     /* entry symbol (default "_start") */
	const char *soname;    /* DT_SONAME for shared libraries (may be NULL) */
	const char *dynamic_linker; /* PT_INTERP path for PIE executables (may be NULL) */
	int         shared;    /* 1 = ET_DYN (shared library), 0 = ET_EXEC */
	int         pie;       /* 1 = PIE (ET_DYN + PT_INTERP), 0 = 普通 */
	int         build_id;   /* 1 = generate .note.gnu.build-id (FNV-1a hash) */
	int         eh_frame_hdr; /* 1 = generate .eh_frame_hdr (exception index) */
	int         as_needed;    /* 0 = --no-as-needed, 1 = --as-needed, -1 = default */
	int         whole_archive; /* 1 = --whole-archive, 0 = --no-whole-archive */
	int         no_undefined;  /* 1 = error on undefined symbols */
	int         gc_sections;   /* 1 = garbage-collect unused sections */
	int         print_map;     /* 1 = output link map to stderr */
	int         cref;          /* 1 = output cross-reference table */
	const char *link_script;   /* path to section layout script (NULL = none) */
	const char *const *defsym; /* --defsym=SYM=VAL list (NULL = none).
	                           Each entry is a SYM=VAL pair; VAL may use a 0x
	                           prefix for hexadecimal. Multiple --defsym flags
	                           are collected into this array. */
	size_t      defsym_count;  /* number of entries in defsym[] */
	const char *const *wrap;   /* --wrap=SYM list (NULL = none).
	                           Each entry is a symbol name; references to SYM
	                           are redirected to __wrap_SYM, and __real_SYM
	                           points at the original SYM. */
	size_t      wrap_count;    /* number of entries in wrap[] */
	const char *const *add_needed; /* --add-needed=<soname> list (NULL = none).
	                               Each entry is a DT_NEEDED soname to add. */
	size_t      add_needed_count;  /* number of entries in add_needed[] */
	const char *version_script;    /* --version-script=<path> (NULL = none).
	                               Path to a symbol version script file. */
};

/* Static linker entry point (original API, shared=0).
 *
 * target  is the architecture name ("x86_64", …) or NULL to default to x86_64.
 * All other architectures now parse correctly to set ELF header fields. */
int mt_ld_link(const char *output, const char *entry,
               const char *const *inputs, size_t input_count,
               const char *target,
               const char **error_message);

/* Extended linker entry point with mt_ld_options.
 * When opts is NULL, behaves identically to mt_ld_link() with
 * output=output, entry=entry, shared=0. */
int mt_ld_link_opts(const struct mt_ld_options *opts,
                    const char *const *inputs, size_t input_count,
                    const char *target,
                    const char **error_message);

#endif
