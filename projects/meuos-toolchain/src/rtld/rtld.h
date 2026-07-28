/* rtld.h — internal declarations for the MeuOS dynamic linker.
 *
 * This ld.so runs before libc is initialized.  All syscalls go through
 * rtld_syscall(), not through libc wrappers.  No <stdio.h>, no <stdlib.h>,
 * no libc dependency. */

#ifndef RTLD_H
#define RTLD_H

#include <stddef.h>
#include <stdint.h>

/* ELF constants we need without pulling in mt/elf.h (which has API deps) */
#define ELF64_EHDR_SIZE   64
#define ELF64_PHDR_SIZE   56
#define ELF64_SYM_SIZE    24
#define ELF64_RELA_SIZE   24
#define ELF64_DYN_SIZE    16

/* x86_64 syscall numbers */
#define SYS_read    0
#define SYS_write   1
#define SYS_open    2
#define SYS_close   3
#define SYS_lseek   8
#define SYS_mmap    9
#define SYS_mprotect 10
#define SYS_munmap  11
#define SYS_brk     12
#define SYS_exit    60
#define SYS_exit_group 231
#define SYS_arch_prctl 158

/* Auxiliary vector types */
#define AT_NULL     0
#define AT_IGNORE   1
#define AT_EXECFD   2
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9
#define AT_UID      11
#define AT_EUID     12
#define AT_GID      13
#define AT_EGID     14
#define AT_SECURE   23
#define AT_RANDOM   25
#define AT_EXECFN   31
#define AT_SYSINFO_EHDR 33

/* Program header types */
#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_PHDR     6
#define PT_TLS      7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

/* Section header types */
#define SHT_NULL       0
#define SHT_PROGBITS   1
#define SHT_SYMTAB     2
#define SHT_STRTAB     3
#define SHT_RELA       4
#define SHT_HASH       5
#define SHT_DYNAMIC    6
#define SHT_DYNSYM     11
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15

/* Segment flags */
#define PF_R    4
#define PF_W    2
#define PF_X    1

/* Dynamic tags */
#define DT_NULL        0
#define DT_NEEDED      1
#define DT_PLTRELSZ    2
#define DT_PLTGOT      3
#define DT_HASH        4
#define DT_STRTAB      5
#define DT_SYMTAB      6
#define DT_RELA        7
#define DT_RELASZ      8
#define DT_RELAENT     9
#define DT_STRSZ       10
#define DT_SYMENT      11
#define DT_INIT        12
#define DT_FINI        13
#define DT_SONAME      14
#define DT_RPATH       15
#define DT_SYMBOLIC    16
#define DT_REL         17
#define DT_RELSZ       18
#define DT_RELENT      19
#define DT_PLTREL      20
#define DT_DEBUG       21
#define DT_TEXTREL     22
#define DT_JMPREL      23
#define DT_BIND_NOW    24
#define DT_INIT_ARRAY  25
#define DT_FINI_ARRAY  26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_FLAGS       30
#define DT_FLAGS_1     0x6ffffffb

/* x86_64 relocation types */
#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_PC32      2
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8
#define R_X86_64_32        10
#define R_X86_64_32S       11
#define R_X86_64_DTPMOD64  16
#define R_X86_64_DTPOFF64  17
#define R_X86_64_TPOFF64   18

/* ELF symbol info helpers */
#define ELF64_ST_BIND(i)  ((i) >> 4)
#define ELF64_ST_TYPE(i)  ((i) & 0xf)
#define ELF64_R_SYM(i)    ((uint32_t)((i) >> 32))
#define ELF64_R_TYPE(i)   ((uint32_t)((i) & 0xffffffff))

/* ON-DISK ELF64 structures.  All multi-byte values are little-endian. */
typedef struct {
	unsigned char ident[16];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint64_t entry;
	uint64_t phoff;
	uint64_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} __attribute__((packed)) Ehdr64;

typedef struct {
	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint64_t vaddr;
	uint64_t paddr;
	uint64_t filesz;
	uint64_t memsz;
	uint64_t align;
} __attribute__((packed)) Phdr64;

typedef struct {
	uint64_t d_tag;
	uint64_t d_val;
} __attribute__((packed)) Dyn64;

typedef struct {
	uint32_t name;
	unsigned char info;
	unsigned char other;
	uint16_t shndx;
	uint64_t value;
	uint64_t size;
} __attribute__((packed)) Sym64;

typedef struct {
	uint64_t r_offset;
	uint64_t r_info;
	int64_t r_addend;
} __attribute__((packed)) Rela64;

/* A loaded shared library or the main executable. */
#define RTLD_MAX_LIBS 64

struct rtld_lib {
	const char *name;     /* SONAME or path */
	uintptr_t base;       /* load base (mmap address) */
	Ehdr64 *ehdr;         /* -> ELF header in mapped memory */
	uintptr_t map_size;   /* total mmap size (file + BSS) */
	int is_main;          /* 1 = main executable, 0 = shared lib */

	/* Parsed dynamic section */
	Dyn64 *dynv;
	Sym64 *symtab;
	const char *strtab;
	size_t strsz;
	uint32_t *hash;       /* SysV hash table */
	Rela64 *rela;
	size_t relasz;
	Rela64 *jmprel;
	size_t jmprelsz;
	int pltrel;           /* 0 = DT_REL, 1 = DT_RELA */
	void (*init)(void);
	void (**init_array)(void);
	size_t init_arraysz;
	void (**fini_array)(void);
	size_t fini_arraysz;
};

/* Global state */
struct rtld_state {
	struct rtld_lib libs[RTLD_MAX_LIBS];
	int lib_count;
	uintptr_t base_addr;  /* ld.so's own base */
	size_t page_size;
};

/* syscall.c */
long rtld_syscall(long n, long a1, long a2, long a3,
                   long a4, long a5, long a6);
void *rtld_mmap(void *addr, size_t length, int prot,
                int flags, int fd, long offset);
int rtld_mprotect(void *addr, size_t length, int prot);
int rtld_munmap(void *addr, size_t length);
int rtld_open(const char *path, int flags);
int rtld_read(int fd, void *buf, size_t count);
int rtld_write(int fd, const void *buf, size_t count);
int rtld_close(int fd);
long rtld_lseek(int fd, long offset, int whence);
void *rtld_brk(void *addr);
void rtld_exit(int code);
long rtld_arch_prctl(int code, unsigned long addr);
void rtld_die(const char *msg);   /* write + exit */

/* rtld.c */
struct rtld_lib *rtld_load_lib(const char *path, struct rtld_state *st);
void rtld_apply_rela(struct rtld_lib *lib, struct rtld_state *st);
void rtld_init_lib(struct rtld_lib *lib);
Sym64 *rtld_find_sym(struct rtld_state *st, const char *name, int *out_lib);
int rtld_elf_hash(const char *name);

#endif /* RTLD_H */
