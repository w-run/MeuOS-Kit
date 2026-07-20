---
name: "mkit-syscall-gen"
description: "Generates single-file syscall wrappers for meuos-libc (one .c per syscall). Invoke when user asks to add/implement syscall wrappers (read, write, mmap, fork, etc.) per AGENTS.md §2.1."
---

# meuos-libc Syscall Wrapper Generator

Generates per-syscall single-file C wrappers for meuos-libc following AGENTS.md §2.1 "每个系统调用一个独立源文件" (one .c file per syscall).

## When to invoke

- User asks to "add syscall X" or "implement syscall wrappers for meuos-libc"
- User asks to generate the initial 30+ syscall set from AGENTS.md §2.1
- User asks "how should meuos-libc wrap syscalls"
- During Phase 2 bootstrap when meuos-libc source is being populated

## Inputs (per syscall)

Gather before generating:

1. **Name** - e.g. `read`, `write`, `mmap`, `fork`
2. **Syscall number** - from `reference/musl/arch/<arch>/bits/syscall.h` (look up only; never copy the file)
3. **Signature** - from POSIX man page or `reference/musl/include/<header>.h`
4. **Target header** - where the prototype lives (`unistd.h`, `sys/mman.h`, `sys/stat.h`, etc.)
5. **Common errno mapping** - typical failure modes

## Output layout

For syscall `read`, generate:

### `meuos-libc/src/syscall/read.c`

```c
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>

ssize_t read(int fd, void *buf, size_t count) {
    long ret = __syscall(SYS_read, fd, buf, count);
    if (ret < 0 && ret >= -4095) {
        errno = -ret;
        return -1;
    }
    return (ssize_t)ret;
}
```

### Side-effect updates

- `meuos-libc/include/unistd.h` - add prototype `ssize_t read(int, void *, size_t);` if absent
- `meuos-libc/src/syscall/Makefile.inc` - append `read.o \` to the `SYSCALL_OBJS` list
- `meuos-libc/src/internal/syscall.h` - declares `__syscall` macro / inline asm stub (shared, generated once)

## Hard constraints (AGENTS.md §4)

- **MUST** call directly through `syscall()` or inline asm - never through a libc wrapper.
- **MUST NOT** depend on glibc headers (`<features.h>`, `<bits/*>`, `<gnu/*>`).
- **MUST** handle the Linux kernel ABI directly: negative return in range `[-4095, -1]` -> set `errno`, return `-1`. Returns outside this range are userland errors (e.g. `mmap` returns `MAP_FAILED`).
- **MUST** be a single self-contained `.c` file per syscall - no shared syscall `.c` files (shared headers in `internal/` are fine).
- **MUST** use the canonical `__syscall` stub (defined once in `meuos-libc/src/internal/syscall.h`), not raw inline asm per file.

## Initial syscall set (AGENTS.md §2.1 - "at least 30")

Process, file, memory, signal, time, directory, IPC subsets:

`read` `write` `open` `close` `fork` `execve` `exit` `mmap` `munmap` `brk`
`stat` `fstat` `lstat` `lseek` `getpid` `getcwd` `chdir` `dup` `dup2` `pipe`
`waitpid` `nanosleep` `clock_gettime` `getdents64` `unlink` `mkdir` `rmdir`
`rename` `link` `symlink` `readlink` `chmod` `access` `socket` `connect` `bind` `listen`

(Total: 37. Aim for this set as the initial milestone.)

## Workflow

1. **Lookup reference**: read `reference/musl/src/unistd/<name>.c` (or `src/fcntl/`, `src/mman/`, etc.) to understand the algorithm. Do not copy; reimplement in clean C11.
2. **Lookup signature**: read `reference/musl/include/<header>.h` (or POSIX man page).
3. **Lookup syscall number**: grep `reference/musl/arch/<arch>/bits/syscall.h` for `SYS_<name>`.
4. **Generate** `meuos-libc/src/syscall/<name>.c` using the template above, customized per signature.
5. **Update** header declaration + `Makefile.inc`.
6. **Verify**: compile with `mcc -c meuos-libc/src/syscall/<name>.c -o /tmp/<name>.o` and confirm no errors.

## Variations

- **Variadic** (`open`, `openat`): use `<stdarg.h>` and `va_arg` to extract optional mode arg.
- **Struct-returning** (`stat`, `fstat`, `getdents64`): kernel struct may differ from userspace struct; check `reference/musl/arch/<arch>/bits/` for layout.
- **mmap**: returns `MAP_FAILED` (i.e. `(void *)-1`) on failure, not `-1`. Adjust error check accordingly.
- **chdir/getcwd**: getcwd buffer must be caller-provided; copy out via `strcpy` after the syscall.

## Don't do

- Don't copy musl source verbatim - reimplement in clean C11.
- Don't use glibc-style internal `__<name>` aliasing (e.g. `read` -> `__read` -> `__libc_read`). Single symbol is fine.
- Don't add weak symbols unless required by a specific consumer.
- Don't generate combined syscall tables into a single file; one syscall per `.c`.
