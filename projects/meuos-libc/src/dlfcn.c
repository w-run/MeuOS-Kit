/* dlfcn.c — dlopen/dlsym/dlclose/dlerror implementation.
 *
 * Phase 2: full .so loading via mmap-based ELF loader.
 * Uses the same approach as ld.so but adapted for libc. */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Optional rtld dlopen/dlsym ABI (P0.3 stage B) ----
 *
 * In a dynamically-linked process ld.so (rtld) exports DTV-aware
 * dlopen/dlsym and resolves them via rtld_self_sym, so the dl* wrappers
 * prefer them for correct run-time loading + TLS.  These are weak so a
 * plain static link (no ld.so) resolves them to NULL and falls back to the
 * in-libc ELF loader below. */
__attribute__((weak)) void *rtld_dlopen(const char *name);
__attribute__((weak)) void *rtld_dlsym(void *handle, const char *name);

/* ---- Error state (static buffer, non-thread-safe) ---- */
#define ERR_BUF_SIZE 256
static char dl_err[ERR_BUF_SIZE];

static void
set_error(const char *msg)
{
	size_t n = strlen(msg);
	if (n >= ERR_BUF_SIZE) n = ERR_BUF_SIZE - 1;
	memcpy(dl_err, msg, n);
	dl_err[n] = '\0';
}

/* ---- Minimal ELF64 structures ---- */
#define ELF64_EHDR_SIZE 64
#define ELF64_PHDR_SIZE 56
#define ELF64_SYM_SIZE  24
#define ELF64_RELA_SIZE 24

typedef struct {
	uint32_t type, flags;
	uint64_t offset, vaddr, paddr, filesz, memsz, align;
} Phdr64;

typedef struct {
	uint32_t name;
	uint8_t  info, other;
	uint16_t shndx;
	uint64_t value, size;
} Sym64;

typedef struct {
	uint64_t r_offset;
	uint64_t r_info;
	int64_t  r_addend;
} Rela64;

#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_TLS     7
#define DT_NULL    0
#define DT_NEEDED  1
#define DT_SYMTAB  6
#define DT_STRTAB  5
#define DT_STRSZ   10
#define DT_HASH    4
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9
#define DT_SONAME  14
#define DT_INIT    12
#define DT_FINI    13
#define DT_INIT_ARRAY 25
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAY  26
#define DT_FINI_ARRAYSZ 28

#define R_X86_64_RELATIVE  8
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_64        1
#define R_X86_64_JUMP_SLOT 7
#define ELF64_R_TYPE(i) ((int)(i) & 0xffffffff)
#define ELF64_R_SYM(i)  ((int)((i) >> 32))
#define ELF64_ST_BIND(i) ((i) >> 4)

/* ---- Loaded library bookkeeping ---- */
#define MAX_LOADED_LIBS 64

struct loaded_lib {
	char  *path;
	char  *soname;
	void  *base;       /* mmap base address */
	size_t map_size;
	void  *dynv;       /* DT_DYNAMIC address */
	Sym64 *symtab;
	const char *strtab;
	size_t  strsz;
	uint32_t *hash;
	Rela64 *rela;
	size_t  relasz;
	Rela64 *jmprel;
	size_t  jmprelsz;
	void (*init)(void);
	void (**init_array)(void);
	size_t  init_arraysz;
};

static struct {
	struct loaded_lib libs[MAX_LOADED_LIBS];
	int count;
} dl_state;

/* ---- ELF parsing helpers ---- */

static uint64_t
rd64(const unsigned char *p)
{
	return (uint64_t)p[0] | (uint64_t)p[1]<<8 | (uint64_t)p[2]<<16 |
	       (uint64_t)p[3]<<24 | (uint64_t)p[4]<<32 | (uint64_t)p[5]<<40 |
	       (uint64_t)p[6]<<48 | (uint64_t)p[7]<<56;
}

static uint32_t
rd32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
	       (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

static uint16_t
rd16(const unsigned char *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1]<<8;
}

static size_t
page_align_up(size_t v)
{
	size_t ps = 4096;
	return (v + ps - 1) & ~(ps - 1);
}

/* ELF hash (SysV) */
static uint32_t
elf_hash(const char *name)
{
	uint32_t h = 0, g;
	while (*name) {
		h = (h << 4) + (unsigned char)*name++;
		g = h & 0xf0000000;
		if (g) h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

/* ---- Library loading ---- */

static struct loaded_lib *
find_lib(const char *path)
{
	for (int i = 0; i < dl_state.count; i++) {
		struct loaded_lib *l = &dl_state.libs[i];
		if (l->path && strcmp(l->path, path) == 0) return l;
		if (l->soname && strcmp(l->soname, path) == 0) return l;
	}
	return NULL;
}

static struct loaded_lib *
load_lib(const char *path)
{
	if (dl_state.count >= MAX_LOADED_LIBS) {
		set_error("too many loaded libraries");
		return NULL;
	}

	/* Check if already loaded */
	struct loaded_lib *existing = find_lib(path);
	if (existing) return existing;

	int fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;

	/* Get file size */
	struct stat st;
	if (fstat(fd, &st) < 0) { close(fd); return NULL; }
	size_t fsize = (size_t)st.st_size;
	if (fsize < ELF64_EHDR_SIZE) { close(fd); return NULL; }

	/* Map the file */
	void *file_map = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (file_map == MAP_FAILED) return NULL;

	unsigned char *data = (unsigned char *)file_map;

	/* Parse ELF header */
	if (data[0] != 0x7f || data[1]!='E' || data[2]!='L' || data[3]!='F' ||
	    data[4] != 2 || data[5] != 1) /* ELF64, LE */
		goto fail;
	uint16_t e_type = rd16(data + 16);
	uint16_t e_machine = rd16(data + 18);
	if (e_type != 3 || e_machine != 62) /* ET_DYN, EM_X86_64 */
		goto fail;
	uint64_t e_phoff = rd64(data + 32);
	uint16_t e_phnum = rd16(data + 56);
	uint16_t e_phentsize = rd16(data + 54);
	if (e_phoff == 0 || e_phentsize < ELF64_PHDR_SIZE || e_phnum == 0)
		goto fail;

	/* Determine load range */
	uintptr_t min_vaddr = (uintptr_t)-1, max_end = 0;
	for (int i = 0; i < e_phnum; i++) {
		const unsigned char *ph = data + e_phoff + i * e_phentsize;
		if (rd32(ph) != PT_LOAD) continue;
		uint64_t vaddr = rd64(ph + 16);
		uint64_t memsz = rd64(ph + 40);
		if (vaddr < min_vaddr) min_vaddr = (uintptr_t)vaddr;
		uint64_t end = vaddr + memsz;
		if (end > max_end) max_end = end;
	}
	if (max_end == 0) goto fail;

	/* Allocate memory */
	size_t map_size = page_align_up(max_end) - (min_vaddr & ~(size_t)0xFFF);
	void *map_base = mmap(NULL, map_size, PROT_READ|PROT_WRITE,
	                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (map_base == MAP_FAILED) goto fail;

	/* Map each PT_LOAD segment */
	for (int i = 0; i < e_phnum; i++) {
		const unsigned char *ph = data + e_phoff + i * e_phentsize;
		if (rd32(ph) != PT_LOAD) continue;
		uint64_t offset = rd64(ph + 8);
		uint64_t vaddr  = rd64(ph + 16);
		uint64_t filesz = rd64(ph + 32);
		uint64_t memsz  = rd64(ph + 40);
		uint32_t flags  = rd32(ph + 4);
		if (filesz == 0 && memsz == 0) continue;

		uintptr_t dest = (uintptr_t)map_base + (vaddr - min_vaddr);
		if (filesz > 0)
			memcpy((void *)dest, data + offset, (size_t)filesz);

		/* Set memory protection per PT_LOAD flags */
		int prot = 0;
		if (flags & 4) prot |= PROT_READ;
		if (flags & 2) prot |= PROT_WRITE;
		if (flags & 1) prot |= PROT_EXEC;
		size_t seg_size = page_align_up((size_t)(vaddr + memsz)) -
		                  (size_t)(vaddr & ~(size_t)0xFFF);
		uintptr_t seg_start = (uintptr_t)map_base +
		                      (vaddr & ~(size_t)0xFFF) - min_vaddr;
		mprotect((void *)seg_start, seg_size, prot);
	}

	uintptr_t load_base = (uintptr_t)map_base - min_vaddr;

	/* Allocate and fill library struct */
	struct loaded_lib *lib = &dl_state.libs[dl_state.count++];
	memset(lib, 0, sizeof(*lib));
	lib->base = (void *)load_base;
	lib->map_size = map_size;

	/* Parse dynamic section from PT_DYNAMIC */
	for (int i = 0; i < e_phnum; i++) {
		const unsigned char *ph = data + e_phoff + i * e_phentsize;
		if (rd32(ph) != PT_DYNAMIC) continue;
		lib->dynv = (void *)(load_base + rd64(ph + 16));
		break;
	}
	if (!lib->dynv) { dl_state.count--; goto fail_free; }

	/* Parse dynamic entries */
	{
		uint64_t *d = (uint64_t *)lib->dynv;
		for (; d[0] != DT_NULL; d += 2) {
			switch (d[0]) {
			case DT_SYMTAB:  lib->symtab = (Sym64 *)(load_base + d[1]); break;
			case DT_STRTAB:  lib->strtab = (const char *)(load_base + d[1]); break;
			case DT_STRSZ:   lib->strsz  = (size_t)d[1]; break;
			case DT_HASH:    lib->hash   = (uint32_t *)(load_base + d[1]); break;
			case DT_RELA:    lib->rela   = (Rela64 *)(load_base + d[1]); break;
			case DT_RELASZ:  lib->relasz = (size_t)d[1]; break;
			case DT_INIT:    lib->init   = (void (*)(void))(load_base + d[1]); break;
			case DT_INIT_ARRAY:   lib->init_array   = (void (**)(void))(load_base + d[1]); break;
			case DT_INIT_ARRAYSZ: lib->init_arraysz = (size_t)d[1]; break;
			}
		}
	}

	/* Apply dynamic relocations */
	if (lib->rela && lib->relasz > 0) {
		size_t n = lib->relasz / sizeof(Rela64);
		for (size_t ri = 0; ri < n; ri++) {
			Rela64 *r = &lib->rela[ri];
			uintptr_t *loc = (uintptr_t *)(load_base + r->r_offset);
			int rtype = ELF64_R_TYPE(r->r_info);
			uintptr_t val;

			switch (rtype) {
			case R_X86_64_RELATIVE:
				*loc = load_base + (uintptr_t)r->r_addend;
				break;
			case R_X86_64_GLOB_DAT:
			case R_X86_64_64:
			case R_X86_64_JUMP_SLOT:
				/* Eager resolve: look up in already-loaded libs */
				{
					int rsym = ELF64_R_SYM(r->r_info);
					if (rsym > 0 && lib->symtab && lib->strtab) {
						Sym64 *sym = &lib->symtab[rsym];
						if (sym->name > 0 && sym->name < lib->strsz) {
							const char *sname = lib->strtab + sym->name;
							/* Search all loaded libs for this symbol */
							for (int li = 0; li < dl_state.count; li++) {
								struct loaded_lib *sl = &dl_state.libs[li];
								if (!sl->symtab || !sl->hash) continue;
								uint32_t nb = sl->hash[0], nc = sl->hash[1];
								uint32_t *bk = sl->hash + 2;
								uint32_t *ch = bk + nb;
								uint32_t si = bk[elf_hash(sname) % nb];
								int found = 0;
								while (si && si < nc) {
									Sym64 *ss = &sl->symtab[si];
									if (ss->name > 0 && ss->name < sl->strsz &&
									    ELF64_ST_BIND(ss->info) &&
									    ss->shndx &&
									    strcmp(sl->strtab + ss->name, sname) == 0) {
										val = (uintptr_t)sl->base + ss->value;
										*loc = val;
										found = 1;
										break;
									}
									si = ch[si];
								}
								if (found) break;
							}
						}
					}
				}
				break;
			}
		}
	}

	/* Determine soname */
	{
		uint64_t *d = (uint64_t *)lib->dynv;
		for (; d[0] != DT_NULL; d += 2) {
			if (d[0] == DT_SONAME && lib->strtab && d[1] < lib->strsz) {
				lib->soname = strdup(lib->strtab + d[1]);
				break;
			}
		}
	}
	lib->path = strdup(path);

	/* Call init functions */
	if (lib->init) lib->init();
	if (lib->init_array) {
		size_t n = lib->init_arraysz / sizeof(void (*)(void));
		for (size_t i = 0; i < n; i++) lib->init_array[i]();
	}

	munmap(file_map, fsize);
	return lib;

fail_free:
	munmap(map_base, map_size);
fail:
	munmap(file_map, fsize);
	return NULL;
}

/* ---- dl* entry points ---- */

void *
dlopen(const char *file, int mode)
{
	(void)mode;
	if (!file || !*file) {
		set_error("filename is NULL");
		return NULL;
	}
	/* Prefer the dynamic linker's DTV-aware loader when linked against
	 * ld.so; fall back to the in-libc loader for static-only builds. */
	if (rtld_dlopen) {
		void *h = rtld_dlopen(file);
		if (!h) {
			char buf[ERR_BUF_SIZE];
			snprintf(buf, sizeof(buf), "dlopen: cannot load '%s'", file);
			set_error(buf);
		}
		return h;
	}
	struct loaded_lib *lib = load_lib(file);
	if (!lib) {
		char buf[ERR_BUF_SIZE];
		snprintf(buf, sizeof(buf), "dlopen: cannot load '%s'", file);
		set_error(buf);
		return NULL;
	}
	return (void *)lib;
}

void *
dlsym(void *handle, const char *name)
{
	if (!name || !*name) {
		set_error("dlsym: symbol name is NULL");
		return NULL;
	}
	/* Dynamic scenario: let ld.so resolve (it owns the loaded-lib list
	 * and DTV).  Falls back to the in-libc table for static builds. */
	if (rtld_dlsym) {
		void *sym = rtld_dlsym(handle, name);
		if (!sym)
			set_error("dlsym: symbol not found");
		return sym;
	}
	uint32_t h = elf_hash(name);

	/* If handle == RTLD_DEFAULT, search all loaded libs */
	/* If handle == RTLD_NEXT, search all except the first that matches (skip) */
	/* Otherwise search the specific library */

	int start = 0;
	if (handle == ((void *)0)) { /* RTLD_DEFAULT */
		start = 0;
	} else if (handle == ((void *)-1)) { /* RTLD_NEXT - not supported, use default */
		start = 0;
	} else {
		start = dl_state.count - 1;
		/* Search only the given handle's library */
		for (int i = 0; i < dl_state.count; i++) {
			if (&dl_state.libs[i] == (struct loaded_lib *)handle) {
				start = i;
				break;
			}
		}
	}

	for (int li = start; li < dl_state.count; li++) {
		struct loaded_lib *lib = &dl_state.libs[li];
		if (!lib->symtab || !lib->hash) continue;
		uint32_t nb = lib->hash[0], nc = lib->hash[1];
		uint32_t *bk = lib->hash + 2;
		uint32_t *ch = bk + nb;
		uint32_t si = bk[h % nb];
		while (si && si < nc) {
			Sym64 *sym = &lib->symtab[si];
			if (sym->name > 0 && sym->name < lib->strsz &&
			    ELF64_ST_BIND(sym->info) &&
			    sym->shndx &&
			    strcmp(lib->strtab + sym->name, name) == 0) {
				return (void *)((uintptr_t)lib->base + sym->value);
			}
			si = ch[si];
		}
	}
	set_error("dlsym: symbol not found");
	return NULL;
}

int
dlclose(void *handle)
{
	if (!handle)
		return -1;
	/* Find and mark as removable (for now, just no-op) */
	(void)handle;
	return 0;
}

char *
dlerror(void)
{
	char *e = dl_err;
	dl_err[0] = '\0';
	return *e ? e : NULL;
}
