/* rtld.c — MeuOS dynamic linker main logic.
 *
 * This file implements the core loading, symbol resolution, relocation,
 * and initialization transfer for ELF shared libraries on x86_64.
 *
 * Run-time environment: no libc available.  All I/O uses raw syscalls. */

#include "rtld.h"

/* ---- utility helpers ---- */

static uint64_t
read64(const unsigned char *p)
{
	return (uint64_t)p[0] | (uint64_t)p[1] << 8 |
	       (uint64_t)p[2] << 16 | (uint64_t)p[3] << 24 |
	       (uint64_t)p[4] << 32 | (uint64_t)p[5] << 40 |
	       (uint64_t)p[6] << 48 | (uint64_t)p[7] << 56;
}

static uint32_t
read32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	       (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t
read16(const unsigned char *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static size_t
page_align(size_t addr, size_t page_size)
{
	return (addr + page_size - 1) & ~(page_size - 1);
}

/* Bootstrap memory allocator: bump pointer in data segment.
 * We keep track of brk end so the actual brk value stays. */
static uintptr_t heap_start;
static uintptr_t heap_cur;

static void
rtld_heap_init(void)
{
	heap_start = (uintptr_t)rtld_brk(0);
	heap_cur = heap_start;
}

static void *
rtld_alloc(size_t size)
{
	uintptr_t old = heap_cur;
	heap_cur += (size + 7) & ~7;  /* align to 8 bytes */
	if (heap_cur > (uintptr_t)rtld_brk(0)) {
		/* Extend the brk */
		uintptr_t new_brk = (heap_cur + 4095) & ~4095;
		if (rtld_brk((void *)new_brk) != (void *)new_brk)
			return 0;
	}
	return (void *)old;
}

static size_t
rtld_strlen(const char *s)
{
	size_t n = 0;
	while (s[n]) n++;
	return n;
}

static char *
rtld_strdup(const char *s)
{
	size_t n = rtld_strlen(s);
	char *copy = (char *)rtld_alloc(n + 1);
	if (!copy) return 0;
	for (size_t i = 0; i <= n; i++)
		copy[i] = s[i];
	return copy;
}

static int
rtld_strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return (unsigned char)*a - (unsigned char)*b;
}

/* ---- auxv parsing ---- */

static uintptr_t
rtld_get_aux(size_t *sp, int type)
{
	/* sp[0] = argc */
	size_t argc = sp[0];
	/* argv = sp + 1, NULL terminated */
	size_t *argv = sp + 1;
	size_t *envp = argv + argc + 1;
	/* envp is NULL terminated */
	size_t *ep = envp;
	while (*ep) ep++;
	/* auxv follows the NULL terminator */
	size_t *auxv = ep + 1;

	for (size_t *a = auxv; a[0] != AT_NULL; a += 2) {
		if ((int)a[0] == type)
			return a[1];
	}
	return 0;
}

/* ---- ELF hash (SysV) ---- */

int
rtld_elf_hash(const char *name)
{
	unsigned h = 0, g;
	while (*name) {
		h = (h << 4) + (unsigned char)*name++;
		g = h & 0xf0000000;
		if (g) h ^= g >> 24;
		h &= ~g;
	}
	return (int)h;
}

/* ---- reading on-disk ELF structures ---- */

static int
parse_ehdr(const unsigned char *data, size_t size, Ehdr64 *ehdr)
{
	if (size < ELF64_EHDR_SIZE)
		return -1;
	if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F')
		return -1;
	if (data[4] != 2)  /* ELFCLASS64 */                     return -1;
	if (data[5] != 1)  /* ELFDATA2LSB */                     return -1;

	ehdr->type     = read16(data + 16);
	ehdr->machine  = read16(data + 18);
	ehdr->version  = read32(data + 20);
	ehdr->entry    = read64(data + 24);
	ehdr->phoff    = read64(data + 32);
	ehdr->shoff    = read64(data + 40);
	ehdr->flags    = read32(data + 48);
	ehdr->ehsize   = read16(data + 52);
	ehdr->phentsize = read16(data + 54);
	ehdr->phnum    = read16(data + 56);
	ehdr->shentsize = read16(data + 58);
	ehdr->shnum    = read16(data + 60);
	ehdr->shstrndx = read16(data + 62);
	return 0;
}

static void
parse_phdr(const unsigned char *data, size_t offset, Phdr64 *phdr)
{
	const unsigned char *p = data + offset;
	phdr->type   = read32(p + 0);
	phdr->flags  = read32(p + 4);
	phdr->offset = read64(p + 8);
	phdr->vaddr  = read64(p + 16);
	phdr->paddr  = read64(p + 24);
	phdr->filesz = read64(p + 32);
	phdr->memsz  = read64(p + 40);
	phdr->align  = read64(p + 48);
}

/* ---- library loading ---- */

/* Load a shared library from a path.  Returns 0 on failure.
 * On success, fills in lib->base and the dynamic parsing fields. */
struct rtld_lib *
rtld_load_lib(const char *path, struct rtld_state *st)
{
	if (st->lib_count >= RTLD_MAX_LIBS)
		return 0;

	/* Open and mmap the file */
	int fd = rtld_open(path, 0);
	if (fd < 0)
		return 0;

	long size = rtld_lseek(fd, 0, 2); /* SEEK_END */
	if (size < (long)ELF64_EHDR_SIZE) {
		rtld_close(fd);
		return 0;
	}
	rtld_lseek(fd, 0, 0); /* SEEK_SET */

	/* Map the entire file read-only first for header parsing */
	unsigned char *file_map = (unsigned char *)rtld_mmap(
		0, (size_t)size, 3 /* PROT_READ|PROT_WRITE */,
		0x22 /* MAP_PRIVATE|MAP_ANONYMOUS */, -1, 0);
	if ((long)file_map < 0) {
		rtld_close(fd);
		return 0;
	}
	rtld_read(fd, file_map, (size_t)size);
	rtld_close(fd);

	/* Parse ELF header */
	Ehdr64 ehdr;
	if (parse_ehdr(file_map, (size_t)size, &ehdr) != 0)
		goto fail;
	if (ehdr.type != 3 || ehdr.machine != 62) /* ET_DYN, EM_X86_64 */
		goto fail;
	if (ehdr.phoff == 0 || ehdr.phentsize < ELF64_PHDR_SIZE)
		goto fail;

	/* Find program headers to determine load segments */
	size_t phoff = (size_t)ehdr.phoff;
	unsigned phnum = ehdr.phnum;
	if (phoff > (size_t)size || phnum > 64)
		goto fail;
	if (phoff + phnum * ELF64_PHDR_SIZE > (size_t)size)
		goto fail;

	/* Determine load range and base address.
	 * For a shared library (ET_DYN), the load base is chosen by us. */
	uintptr_t min_vaddr = (uintptr_t)-1;
	uintptr_t max_end = 0;
	uintptr_t max_file_end = 0;
	Phdr64 phdr;
	for (unsigned i = 0; i < phnum; i++) {
		parse_phdr(file_map, phoff + i * ELF64_PHDR_SIZE, &phdr);
		if (phdr.type == PT_LOAD) {
			if (phdr.vaddr < min_vaddr)
				min_vaddr = phdr.vaddr;
			uintptr_t end = (uintptr_t)(phdr.vaddr + phdr.memsz);
			if (end > max_end)
				max_end = end;
			uintptr_t file_end = (uintptr_t)(phdr.offset + phdr.filesz);
			if (file_end > max_file_end)
				max_file_end = file_end;
		}
	}

	/* Allocate a contiguous block for all load segments.
	 * We use mmap at a hint address near 0x700000000 (high enough to
	 * avoid collisions). */
	size_t map_size = page_align(max_end, 4096) - page_align(min_vaddr, 4096);
	uintptr_t base_hint = 0x700000000ULL;
	unsigned char *map_base = (unsigned char *)rtld_mmap(
		(void *)page_align(base_hint - page_align(min_vaddr, 4096), 4096),
		map_size, 3 /* PROT_READ|PROT_WRITE */,
		0x22 /* MAP_PRIVATE|MAP_ANONYMOUS */, -1, 0);
	if ((long)map_base < 0)
		goto fail;

	/* Now map each PT_LOAD segment at the right offset within our block */
	for (unsigned i = 0; i < phnum; i++) {
		parse_phdr(file_map, phoff + i * ELF64_PHDR_SIZE, &phdr);
		if (phdr.type != PT_LOAD)
			continue;
		uintptr_t load_addr = (uintptr_t)map_base
		                      + page_align(phdr.vaddr, 4096)
		                      - page_align(min_vaddr, 4096);
		size_t seg_file_size = (size_t)phdr.filesz;
		if (seg_file_size > 0) {
			/* Copy file data into the anonymous mapping */
			/* Copy file data into the anonymous mapping */
			unsigned char *src = file_map + (size_t)phdr.offset;
			unsigned char *dst = (unsigned char *)load_addr;
			for (size_t j = 0; j < seg_file_size; j++)
				dst[j] = src[j];
		}
		/* The rest (BSS) is already zero-filled from anonymous mmap */
	}

	/* Calculate the actual load base: the base address of this library.
	 * For shared libraries, vaddr starts at 0, so load_base = map_base.
	 * The page alignment adjustment handles cases where vaddr != 0. */
	uintptr_t load_base = (uintptr_t)map_base
	                      - (page_align(min_vaddr, 4096) & ~4095);

	/* Fill in the struct */
	struct rtld_lib *lib = &st->libs[st->lib_count++];
	lib->name = 0;   /* filled by caller */
	lib->base = load_base;
	lib->ehdr = 0;
	lib->map_size = map_size;
	lib->is_main = 0;
	lib->dynv = 0;
	lib->symtab = 0;
	lib->strtab = 0;
	lib->strsz = 0;
	lib->hash = 0;
	lib->rela = 0;
	lib->relasz = 0;
	lib->jmprel = 0;
	lib->jmprelsz = 0;
	lib->pltrel = 0;
	lib->init = 0;
	lib->init_array = 0;
	lib->init_arraysz = 0;
	lib->fini_array = 0;
	lib->fini_arraysz = 0;
	lib->tls_modid = 0;
	lib->tls_vaddr = 0;
	lib->tls_filesz = 0;
	lib->tls_memsz = 0;
	lib->tls_align = 0;
	lib->tls_image = 0;

	/* Parse dynamic section from program headers */
	for (unsigned i = 0; i < phnum; i++) {
		parse_phdr(file_map, phoff + i * ELF64_PHDR_SIZE, &phdr);
		if (phdr.type == PT_DYNAMIC) {
			lib->dynv = (Dyn64 *)(load_base + phdr.vaddr);
			break;
		}
	}
	if (!lib->dynv)
		goto fail_free;

	/* Parse dynamic entries */
	for (Dyn64 *d = lib->dynv; d->d_tag != DT_NULL; d++) {
		switch (d->d_tag) {
		case DT_SYMTAB:
			lib->symtab = (Sym64 *)(load_base + d->d_val);
			break;
		case DT_STRTAB:
			lib->strtab = (const char *)(load_base + d->d_val);
			break;
		case DT_STRSZ:
			lib->strsz = (size_t)d->d_val;
			break;
		case DT_HASH:
			lib->hash = (uint32_t *)(load_base + d->d_val);
			break;
		case DT_RELA:
			lib->rela = (Rela64 *)(load_base + d->d_val);
			break;
		case DT_RELASZ:
			lib->relasz = (size_t)d->d_val;
			break;
		case DT_PLTREL:
			lib->pltrel = (int)d->d_val;
			break;
		case DT_JMPREL:
			lib->jmprel = (Rela64 *)(load_base + d->d_val);
			break;
		case DT_PLTRELSZ:
			lib->jmprelsz = (size_t)d->d_val;
			break;
		case DT_INIT:
			lib->init = (void (*)(void))(load_base + d->d_val);
			break;
		case DT_INIT_ARRAY:
			lib->init_array = (void (**)(void))(load_base + d->d_val);
			break;
		case DT_INIT_ARRAYSZ:
			lib->init_arraysz = (size_t)d->d_val;
			break;
		case DT_FINI_ARRAY:
			lib->fini_array = (void (**)(void))(load_base + d->d_val);
			break;
		case DT_FINI_ARRAYSZ:
			lib->fini_arraysz = (size_t)d->d_val;
			break;
		}
	}

	/* Scan PT_TLS to record this module's TLS block layout. */
	for (unsigned i = 0; i < phnum; i++) {
		parse_phdr(file_map, phoff + i * ELF64_PHDR_SIZE, &phdr);
		if (phdr.type == PT_TLS) {
			lib->tls_vaddr  = load_base + phdr.vaddr;
			lib->tls_filesz = (size_t)phdr.filesz;
			lib->tls_memsz  = (size_t)phdr.memsz;
			lib->tls_align  = phdr.align ? (size_t)phdr.align : 1;
			break;
		}
	}

	/* Set page protections for loaded segments */
	for (unsigned i = 0; i < phnum; i++) {
		parse_phdr(file_map, phoff + i * ELF64_PHDR_SIZE, &phdr);
		if (phdr.type == PT_LOAD) {
			uintptr_t seg_start = page_align(load_base + phdr.vaddr, 4096);
			uintptr_t seg_end = page_align(load_base + phdr.vaddr + phdr.memsz, 4096);
			size_t seg_size = seg_end - seg_start;
			int prot = 0;
			if (phdr.flags & PF_R) prot |= 1;  /* PROT_READ */
			if (phdr.flags & PF_W) prot |= 2;  /* PROT_WRITE */
			if (phdr.flags & PF_X) prot |= 4;  /* PROT_EXEC */
			if (seg_size > 0)
				rtld_mprotect((void *)seg_start, seg_size, prot);
		}
	}

	/* Free the file map (no longer needed) */
	rtld_munmap(file_map, (size_t)size);

	/* Set name from SONAME if available, else use path */
	/* Check DT_SONAME in dynamic */
	for (Dyn64 *d = lib->dynv; d->d_tag != DT_NULL; d++) {
		if (d->d_tag == DT_SONAME && lib->strtab && d->d_val < lib->strsz) {
			lib->name = rtld_strdup(lib->strtab + d->d_val);
			break;
		}
	}
	if (!lib->name)
		lib->name = rtld_strdup(path);
	lib->ehdr = (Ehdr64 *)(load_base);  /* ehdr is at base; may need adjust */

	return lib;

fail_free:
	st->lib_count--;
	rtld_munmap(map_base, map_size);
fail:
	rtld_munmap(file_map, (size_t)size);
	return 0;
}

/* ---- symbol lookup ---- */

Sym64 *
rtld_find_sym(struct rtld_state *st, const char *name, int *out_lib)
{
	unsigned h = (unsigned)rtld_elf_hash(name);
	/* Search all loaded libraries in order (main executable first,
	 * then its dependencies in load order) */
	for (int l = 0; l < st->lib_count; l++) {
		struct rtld_lib *lib = &st->libs[l];
		if (!lib->symtab || !lib->hash) continue;
		uint32_t nbucket = lib->hash[0];
		uint32_t nchain  = lib->hash[1];
		uint32_t *bucket = lib->hash + 2;
		uint32_t *chain  = bucket + nbucket;

		uint32_t si = bucket[h % nbucket];
		while (si != 0 && si < nchain) {
			Sym64 *sym = &lib->symtab[si];
			if (sym->name > 0 && sym->name < lib->strsz) {
				const char *sym_name = lib->strtab + sym->name;
				if (ELF64_ST_BIND(sym->info) != 0 && /* STB_LOCAL */
				    sym->shndx != 0 &&              /* SHN_UNDEF */
				    rtld_strcmp(sym_name, name) == 0) {
					if (out_lib) *out_lib = l;
					return sym;
				}
			}
			si = chain[si];
		}
	}
	return 0;
}

/* ---- relocation application ---- */

static uint64_t
resolve_sym_value(struct rtld_lib *lib, Sym64 *sym, uintptr_t lib_base)
{
	(void)lib;
	return lib_base + sym->value;
}

void
rtld_apply_rela(struct rtld_lib *lib, struct rtld_state *st)
{
	(void)st;
	uintptr_t base = lib->base;
	/* Apply DT_RELA relocations (RELATIVE + symbol lookups) */
	uint64_t n = lib->relasz / sizeof(Rela64);
	for (uint64_t i = 0; i < n; i++) {
		Rela64 *r = &lib->rela[i];
		uintptr_t *loc = (uintptr_t *)(base + r->r_offset);
		int rtype = (int)ELF64_R_TYPE(r->r_info);
		int rsym  = (int)ELF64_R_SYM(r->r_info);

		switch (rtype) {
		case R_X86_64_RELATIVE:
			*loc = base + (uintptr_t)r->r_addend;
			break;
		case R_X86_64_GLOB_DAT:
		case R_X86_64_64: {
			Sym64 *sym = &lib->symtab[rsym];
			*loc = resolve_sym_value(lib, sym, base) + (uintptr_t)r->r_addend;
			break;
		}
		case R_X86_64_JUMP_SLOT: {
			Sym64 *sym = &lib->symtab[rsym];
			*loc = resolve_sym_value(lib, sym, base);
			break;
		}
		case R_X86_64_DTPMOD64: {
			/* Module id for the library that defines this TLS
			 * symbol.  Resolve globally, then use the owning
			 * library's assigned module id. */
			const char *sym_name = 0;
			if (lib->symtab && lib->strtab && lib->strsz) {
				Sym64 *s = &lib->symtab[rsym];
				if (s->name > 0 && s->name < lib->strsz)
					sym_name = lib->strtab + s->name;
			}
			int def_lib = -1;
			if (sym_name)
				rtld_find_sym(st, sym_name, &def_lib);
			if (def_lib >= 0 && st->libs[def_lib].tls_modid > 0)
				*(uint64_t *)loc = (uint64_t)st->libs[def_lib].tls_modid;
			else
				*(uint64_t *)loc = (uint64_t)lib->tls_modid;
			break;
		}
		case R_X86_64_DTPOFF64: {
			/* TP-relative offset of the symbol.  The compiler's
			 * __tls_get_addr resolves `tp + ti_offset`, so we store
			 *   (module_block_base + sym_offset) - tp. */
			const char *sym_name = 0;
			if (lib->symtab && lib->strtab && lib->strsz) {
				Sym64 *s = &lib->symtab[rsym];
				if (s->name > 0 && s->name < lib->strsz)
					sym_name = lib->strtab + s->name;
			}
			int def_lib = -1;
			Sym64 *def_sym = 0;
			if (sym_name)
				def_sym = rtld_find_sym(st, sym_name, &def_lib);
			uintptr_t block_base = lib->tls_image;
			uint64_t sym_off = (uint64_t)r->r_addend;
			if (def_sym) {
				sym_off = def_sym->value + (uint64_t)r->r_addend;
				if (def_lib >= 0)
					block_base = st->libs[def_lib].tls_image;
			}
			*(uint64_t *)loc =
				(uint64_t)((int64_t)(block_base + sym_off)
				           - (int64_t)st->tls_tp);
			break;
		}
		case R_X86_64_NONE:
			break;
		default:
			/* Unsupported relocation — skip */
			break;
		}
	}

	/* Apply JMPREL relocations if they're separate from DT_RELA */
	if (lib->jmprel && lib->jmprelsz > 0) {
		/* Only process JMPREL if it's not the same range as DT_RELA */
		uintptr_t rela_start = (uintptr_t)lib->rela;
		uintptr_t rela_end = rela_start + lib->relasz;
		uintptr_t jmp_start = (uintptr_t)lib->jmprel;
		if (jmp_start < rela_start || jmp_start >= rela_end) {
			n = lib->jmprelsz / sizeof(Rela64);
			for (uint64_t i = 0; i < n; i++) {
				Rela64 *r = &lib->jmprel[i];
				uintptr_t *loc = (uintptr_t *)(base + r->r_offset);
				int rtype = (int)ELF64_R_TYPE(r->r_info);
				int rsym  = (int)ELF64_R_SYM(r->r_info);

				switch (rtype) {
				case R_X86_64_JUMP_SLOT: {
					Sym64 *sym = &lib->symtab[rsym];
					*loc = resolve_sym_value(lib, sym, base);
					break;
				}
				case R_X86_64_RELATIVE:
					*loc = base + (uintptr_t)r->r_addend;
					break;
				case R_X86_64_GLOB_DAT: {
					Sym64 *sym = &lib->symtab[rsym];
					*loc = resolve_sym_value(lib, sym, base);
					break;
				}
				default:
					break;
				}
			}
		}
	}
}

/* ---- init/fini ---- */

void
rtld_init_lib(struct rtld_lib *lib)
{
	if (lib->init)
		lib->init();
	if (lib->init_array) {
		size_t n = lib->init_arraysz / sizeof(void (*)(void));
		for (size_t i = 0; i < n; i++)
			lib->init_array[i]();
	}
}

/* Forward declaration */
static void rtld_load_needed(struct rtld_state *st, struct rtld_lib *lib);

/* Load all DT_NEEDED dependencies of a library (transitive). */
static void
rtld_load_needed(struct rtld_state *st, struct rtld_lib *lib)
{
	if (!lib->dynv || !lib->strtab || !lib->strsz)
		return;
	for (Dyn64 *d = lib->dynv; d->d_tag != DT_NULL; d++) {
		if (d->d_tag != DT_NEEDED) continue;
		if (d->d_val >= lib->strsz) continue;
		const char *soname = lib->strtab + d->d_val;
		int already = 0;
		for (int i = 0; i < st->lib_count; i++) {
			if (st->libs[i].name &&
			    rtld_strcmp(st->libs[i].name, soname) == 0) {
				already = 1; break;
			}
		}
		if (already) continue;
		struct rtld_lib *loaded = rtld_load_lib(soname, st);
		if (!loaded) {
			char libpath[512];
			int k = 0;
			const char *p = "/lib/";
			while (*p) libpath[k++] = *p++;
			p = soname;
			while (*p) libpath[k++] = *p++;
			libpath[k] = 0;
			loaded = rtld_load_lib(libpath, st);
		}
		if (loaded) rtld_load_needed(st, loaded);
	}
}

/* ---- TLS module setup ---- */

/* Build the initial TLS layout (x86_64 Variant II).
 *
 * All modules that have a PT_TLS are laid out back-to-back in a single
 * contiguous region.  The thread pointer (%fs base) points at the END of
 * this region (Variant II).  Each variable's address is therefore
 *   block_base[symbol] + offset - tls_tp  (TP-relative)
 * which the compiler's __tls_get_addr resolves as `tp + ti_offset`.
 *
 * Called once after every library is loaded and before relocations, so
 * that R_X86_64_DTPMOD64/DTPOFF64 can be filled in. */
static void
rtld_tls_setup(struct rtld_state *st)
{
	st->tls_mod_count = 0;
	st->tls_tp = 0;

	/* Count total TLS bytes and reserve a 16-byte TCB at the top. */
	size_t total = 16; /* TCB (holds DTV slot / pthread at tp-8) */
	for (int l = 0; l < st->lib_count; l++) {
		struct rtld_lib *lib = &st->libs[l];
		if (lib->tls_memsz == 0)
			continue;
		size_t align = lib->tls_align > 16 ? lib->tls_align : 16;
		total += (lib->tls_memsz + align - 1) & ~(align - 1);
	}
	if (total == 16)
		return; /* no TLS at all */

	/* Allocate the contiguous TLS area. */
	uintptr_t area = (uintptr_t)rtld_alloc(total);
	if (!area)
		return;
	/* The thread pointer is the END of the area. */
	uintptr_t tp = area + total;
	st->tls_tp = tp;

	/* Lay out modules from the top down (module with the highest
	 * modid sits just below the TCB).  Assign modids 1..N. */
	uintptr_t cursor = tp;
	/* Main executable first (modid 1). */
	if (st->libs[0].tls_memsz > 0) {
		struct rtld_lib *lib = &st->libs[0];
		size_t align = lib->tls_align > 16 ? lib->tls_align : 16;
		size_t sz = (lib->tls_memsz + align - 1) & ~(align - 1);
		cursor -= sz;
		lib->tls_modid = ++st->tls_mod_count;
		lib->tls_image = cursor;
		for (size_t i = 0; i < lib->tls_memsz; i++)
			((unsigned char *)cursor)[i] = 0;
		for (size_t i = 0; i < lib->tls_filesz; i++)
			((unsigned char *)cursor)[i] =
				((unsigned char *)lib->tls_vaddr)[i];
	}
	for (int l = 1; l < st->lib_count; l++) {
		struct rtld_lib *lib = &st->libs[l];
		if (lib->tls_memsz == 0)
			continue;
		size_t align = lib->tls_align > 16 ? lib->tls_align : 16;
		size_t sz = (lib->tls_memsz + align - 1) & ~(align - 1);
		cursor -= sz;
		lib->tls_modid = ++st->tls_mod_count;
		lib->tls_image = cursor;
		for (size_t i = 0; i < lib->tls_memsz; i++)
			((unsigned char *)cursor)[i] = 0;
		for (size_t i = 0; i < lib->tls_filesz; i++)
			((unsigned char *)cursor)[i] =
				((unsigned char *)lib->tls_vaddr)[i];
	}

	/* Point %fs at the thread pointer. */
	rtld_arch_prctl(0x1002 /* ARCH_SET_FS */, (unsigned long)tp);
}

/* ---- main entry (called from dlstart.S) ---- */

uintptr_t
rtld_main(size_t *sp)
{
	struct rtld_state st;
	uintptr_t entry = 0;

	/* Parse auxv */
	uintptr_t at_phdr  = rtld_get_aux(sp, AT_PHDR);
	uintptr_t at_phnum = rtld_get_aux(sp, AT_PHNUM);
	uintptr_t at_phent = rtld_get_aux(sp, AT_PHENT);
	uintptr_t at_entry = rtld_get_aux(sp, AT_ENTRY);
	uintptr_t at_pagesz = rtld_get_aux(sp, AT_PAGESZ);

	if (!at_phdr || !at_phnum || !at_phent || !at_entry)
		rtld_die("missing auxv entries");

	st.page_size = at_pagesz ? at_pagesz : 4096;

	/* Initialize the basic allocator */
	rtld_heap_init();

	/* Register the main executable as the first "library" */
	struct rtld_lib *main_lib = &st.libs[0];
	st.lib_count = 1;
	main_lib->name = "(main executable)";
	main_lib->base = 0;  /* PIE executed from AT_PHDR derived base */
	main_lib->is_main = 1;
	main_lib->dynv = 0;
	main_lib->symtab = 0;
	main_lib->strtab = 0;
	main_lib->hash = 0;
	main_lib->rela = 0;
	main_lib->relasz = 0;
	main_lib->jmprel = 0;
	main_lib->jmprelsz = 0;
	main_lib->pltrel = 0;
	main_lib->tls_modid = 0;
	main_lib->tls_vaddr = 0;
	main_lib->tls_filesz = 0;
	main_lib->tls_memsz = 0;
	main_lib->tls_align = 0;
	main_lib->tls_image = 0;

	/* Get the main executable's base from AT_PHDR.
	 * For PIE, the program headers are at a vaddr relative to the
	 * load base.  We compute the base by subtracting the first PHDR's
	 * vaddr from the AT_PHDR pointer.  For shared libraries with
	 * vaddr_start = 0, at_phdr equals main_base + 0x40 (ELF header). */
	Phdr64 ph0;
	parse_phdr((const unsigned char *)at_phdr, 0, &ph0);
	uintptr_t phdr_vaddr = ph0.vaddr;
	uintptr_t main_base = at_phdr - phdr_vaddr;
	/* Walk the phdrs to find PT_DYNAMIC */
	for (unsigned i = 0; i < at_phnum; i++) {
		Phdr64 ph;
		parse_phdr((const unsigned char *)at_phdr,
		            i * at_phent, &ph);
		if (ph.type == PT_DYNAMIC) {
			main_lib->dynv = (Dyn64 *)(at_phdr + ph.vaddr - phdr_vaddr);
		} else if (ph.type == PT_TLS) {
			main_lib->tls_vaddr  = main_base + ph.vaddr;
			main_lib->tls_filesz = (size_t)ph.filesz;
			main_lib->tls_memsz  = (size_t)ph.memsz;
			main_lib->tls_align  = ph.align ? (size_t)ph.align : 1;
		}
	}

	if (!main_lib->dynv)
		rtld_die("main executable has no .dynamic section");

	/* Parse main binary's dynamic entries */
	for (Dyn64 *d = main_lib->dynv; d->d_tag != DT_NULL; d++) {
		switch (d->d_tag) {
		case DT_STRTAB:
			main_lib->strtab = (const char *)(main_base + d->d_val);
			break;
		case DT_STRSZ:
			main_lib->strsz = (size_t)d->d_val;
			break;
		case DT_SYMTAB:
			main_lib->symtab = (Sym64 *)(main_base + d->d_val);
			break;
		case DT_HASH:
			main_lib->hash = (uint32_t *)(main_base + d->d_val);
			break;
		case DT_RELA:
			main_lib->rela = (Rela64 *)(main_base + d->d_val);
			break;
		case DT_RELASZ:
			main_lib->relasz = (size_t)d->d_val;
			break;
		case DT_PLTREL:
			main_lib->pltrel = (int)d->d_val;
			break;
		case DT_JMPREL:
			main_lib->jmprel = (Rela64 *)(main_base + d->d_val);
			break;
		case DT_INIT:
			main_lib->init = (void (*)(void))(main_base + d->d_val);
			break;
		case DT_INIT_ARRAY:
			main_lib->init_array = (void (**)(void))(main_base + d->d_val);
			break;
		case DT_INIT_ARRAYSZ:
			main_lib->init_arraysz = (size_t)d->d_val;
			break;
	/* Load shared libraries via DT_NEEDED (transitive) */
	rtld_load_needed(&st, main_lib);
		}
	}

	/* Register TLS modules, assign module IDs, lay out the contiguous
	 * TLS area, and set %fs before resolving DTPMOD64/DTPOFF64
	 * relocations. */
	rtld_tls_setup(&st);

	/* Apply main executable relocations first */
	rtld_apply_rela(main_lib, &st);

	/* Then apply relocations for each loaded shared library */
	for (int l = 1; l < st.lib_count; l++)
		rtld_apply_rela(&st.libs[l], &st);

	/* Call init functions: main binary first, then libraries */
	rtld_init_lib(main_lib);
	for (int l = 1; l < st.lib_count; l++)
		rtld_init_lib(&st.libs[l]);

	/* Return the entry point address */
	entry = at_entry;
	if (main_lib->symtab) {
		/* Try to find _start symbol for exact entry */
		/* For PIE, the entry is relative to the base, so
		 * we compute it from the binary's phdrs layout.
		 * The entry from auxv is already absolute. */
	}
	return entry;
}
