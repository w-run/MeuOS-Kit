# meuos-libc - MeuOS C Library
#
# Builds libc-meuos.a (libc core) and libatomic-meuos.a (atomic shim),
# plus the crt1.o entry stub. The compat layer (formerly meuos-libc-compat)
# is built by a recursive make into src/compat/Makefile and produces
# libc-meuos-compat.a separately so applications can opt-in.
#
# Layout:
#   include/          public libc headers
#   crt/<arch>/       crt1.S per arch
#   src/
#     arch/<arch>/    arch-specific runtime (TLS, atomic, setjmp, sigreturn)
#     internal/       internal headers + syscall gate
#     stdio/          split stdio.c (file/char_io/block_io/position/...)
#     stdlib/         malloc / exit / env / convert / ...
#     string/         memcpy / strerror / strdup / ...
#     thread/         C11 threads split into c11_threads/mutex/condvar/tss/...
#     signal/         signal/sigaction
#     ctype/          ctype.h
#     errno/          errno.h
#     dirent/         opendir/readdir/...
#     syscall/        one .c per syscall wrapper
#     compat/         GNU-extension compat (separate archive)
#   test/             regression tests
#   build/<arch>/     object files (gitignored)

HOST_CC ?= cc
AR     ?= ar
PREFIX ?= /usr
DESTDIR ?=
ARCH   ?= x86_64
MCC    ?= ../mcc/mcc
SYSROOT ?= ../sysroot

# C sources are compiled by mcc (or the host CC for x86_64); assembly
# sources always go through the host assembler. For i386, mcc handles
# C compilation via --target=i386 while the host assembler uses -m32.
ifeq ($(ARCH),i386)
CC := $(MCC) --target=i386
AS := $(HOST_CC) -m32
else ifeq ($(ARCH),aarch64)
# mcc 不支持 msr/mrs/svc/brk 等系统指令，所有 .S 必须经交叉 gcc 汇编。
CC := $(MCC) --target=aarch64
AS := aarch64-linux-gnu-gcc
else
CC := $(HOST_CC)
AS := $(HOST_CC)
endif

BUILD := build/$(ARCH)
INCLUDE := -Iinclude -Isrc

# ============================================================
# libc-meuos.a objects
# ============================================================

# Per-arch atomic shim (assembly).
ATOMIC_OBJ := $(BUILD)/arch/$(ARCH)/atomic.o
ATOMIC_LIB := $(BUILD)/libatomic-meuos.a

# Per-arch runtime helpers (TLS, setjmp, sigreturn, thread clone).
# load_gs.S and soft_arith.c are i386-only; set_tls.S is aarch64-only
# (variant I TLS 用 msr tpidr_el0 直接设置线程指针，没有 arch_prctl 等价 syscall)。
ifeq ($(ARCH),x86_64)
ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o \
                $(BUILD)/arch/$(ARCH)/setjmp.o \
                $(BUILD)/arch/$(ARCH)/sigreturn.o \
                $(BUILD)/arch/$(ARCH)/thread_clone.o \
                $(BUILD)/thread/__tls_get_addr.o
# __tls_get_addr.c for x86_64 lives in src/thread/ (host cc w/ inline asm)
$(BUILD)/thread/__tls_get_addr.o: src/thread/__tls_get_addr.c
	$(CC) -Iinclude -Isrc -c -o $@ $<
else ifeq ($(ARCH),aarch64)
ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o \
                $(BUILD)/arch/$(ARCH)/setjmp.o \
                $(BUILD)/arch/$(ARCH)/sigreturn.o \
                $(BUILD)/arch/$(ARCH)/thread_clone.o \
                $(BUILD)/arch/$(ARCH)/set_tls.o
else
# i386 and other archs: __tls_get_addr is assembly (no inline asm in mcc)
ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o \
                $(BUILD)/arch/$(ARCH)/setjmp.o \
                $(BUILD)/arch/$(ARCH)/sigreturn.o \
                $(BUILD)/arch/$(ARCH)/thread_clone.o \
                $(BUILD)/arch/$(ARCH)/load_gs.o \
                $(BUILD)/arch/$(ARCH)/soft_arith.o \
                $(BUILD)/arch/$(ARCH)/__tls_get_addr.o
endif

# Per-feature C objects.
STRING_OBJS    := $(BUILD)/string/memory.o $(BUILD)/string/duplicate.o \
                  $(BUILD)/string/error.o $(BUILD)/string/token.o
STDLIB_OBJS    := $(BUILD)/stdlib/malloc.o $(BUILD)/stdlib/convert.o \
                  $(BUILD)/stdlib/env.o $(BUILD)/stdlib/exit.o \
                  $(BUILD)/stdlib/process.o $(BUILD)/stdlib/search.o \
                  $(BUILD)/stdlib/math.o
STDIO_OBJS     := $(BUILD)/stdio/file.o $(BUILD)/stdio/char_io.o \
                  $(BUILD)/stdio/block_io.o $(BUILD)/stdio/position.o \
                  $(BUILD)/stdio/fmt_out.o $(BUILD)/stdio/fmt_in.o \
                  $(BUILD)/stdio/fp_fmt.o $(BUILD)/stdio/memory_stream.o \
                  $(BUILD)/stdio/file_misc.o $(BUILD)/stdio/truncate.o
SYSCALL_OBJS   := $(addprefix $(BUILD)/syscall/, \
                  read.o write.o open.o close.o fork.o execve.o exit.o \
                  mmap.o munmap.o brk.o stat.o fstat.o lstat.o lseek.o \
                  getpid.o gettid.o getcwd.o chdir.o \
                  dup.o dup2.o pipe.o waitpid.o wait.o wait4.o nanosleep.o \
                  clock_gettime.o getdents64.o socketcall.o chmod.o fchmod.o \
                  access.o sched_yield.o \
                  fchown.o chown.o getuid.o getgid.o geteuid.o getegid.o \
                  umask.o isatty.o fchdir.o symlink.o link.o readlink.o \
                  unlink.o mkdir.o rmdir.o rename.o mkstemp.o mkstemps.o \
                  time.o times.o utime.o gettimeofday.o sysconf.o flock.o \
                  fcntl.o)
# arch_prctl(2) 只存在于 x86_64；i386/aarch64 的 tls.c 都不引用它。
# 链接器按需从 .a 抽取，未引用时不会进入最终二进制；这里仍按 arch
# 过滤，避免给非 x86_64 build 列表里塞一条无意义的目标。
ifeq ($(ARCH),x86_64)
ARCH_PRCTL_OBJ := $(BUILD)/syscall/arch_prctl.o
else
ARCH_PRCTL_OBJ :=
endif
THREAD_OBJS    := $(BUILD)/thread/state.o $(BUILD)/thread/c11_threads.o \
                  $(BUILD)/thread/tss.o $(BUILD)/thread/mutex.o \
                  $(BUILD)/thread/condvar.o $(BUILD)/thread/call_once.o \
                  $(BUILD)/thread/pthread.o
SUPPORT_OBJS   := $(BUILD)/ctype/ctype.o $(BUILD)/errno/errno.o \
                  $(BUILD)/signal/signal.o $(BUILD)/dirent/dirent.o
SYSCALL_GATE_OBJ := $(BUILD)/internal/arch/$(ARCH)/syscall.o
CRT1_OBJ := $(BUILD)/crt1.o
LIBC_LIB := $(BUILD)/libc-meuos.a

ALL_OBJS := $(STRING_OBJS) $(STDLIB_OBJS) $(STDIO_OBJS) $(SYSCALL_OBJS) \
            $(ARCH_PRCTL_OBJ) $(THREAD_OBJS) $(SUPPORT_OBJS) $(SYSCALL_GATE_OBJ) \
            $(ARCH_RT_OBJS) $(ATOMIC_OBJ)

# ============================================================
# Build rules
# ============================================================

all: $(ATOMIC_LIB) $(LIBC_LIB) $(CRT1_OBJ) compat

# Atomic shim: assembly only.
$(ATOMIC_OBJ): src/arch/$(ARCH)/atomic.S
	@mkdir -p $(dir $@)
	$(AS) -c -o $@ $<

$(ATOMIC_LIB): $(ATOMIC_OBJ)
	$(AR) rcs $@ $<

# Syscall gate: arch-specific syscall.S.
$(SYSCALL_GATE_OBJ): src/internal/arch/$(ARCH)/syscall.S
	@mkdir -p $(dir $@)
	$(AS) -c -o $@ $<

# crt1.o: per-arch entry stub.
$(CRT1_OBJ): crt/$(ARCH)/crt1.S
	@mkdir -p $(dir $@)
	$(AS) -c -o $@ $<

# Per-feature C sources.
$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(INCLUDE) -c -o $@ $<

# Per-arch runtime C and assembly (src/arch/<arch>/*.{c,S}).
$(BUILD)/arch/$(ARCH)/%.o: src/arch/$(ARCH)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(INCLUDE) -c -o $@ $<

$(BUILD)/arch/$(ARCH)/%.o: src/arch/$(ARCH)/%.S
	@mkdir -p $(dir $@)
	$(AS) -c -o $@ $<

# Internal syscall gate (architecture-specific).
$(BUILD)/internal/arch/$(ARCH)/%.o: src/internal/arch/$(ARCH)/%.S
	@mkdir -p $(dir $@)
	$(AS) -c -o $@ $<

# libc-meuos.a archive.
$(LIBC_LIB): $(ALL_OBJS)
	$(AR) rcs $@ $(ALL_OBJS)

# ============================================================
# Compat layer (separate archive, recursive make)
# ============================================================

# 不向子 make 传递 HOST_CC：compat 在 aarch64 下不使用 HOST_CC（无 .S），
# 而将 HOST_CC 作为命令行变量传给子 make 会被 GNU Make 自动导出为环境变量，
# 使 mcc 的 pick_host_cc() 读取到 HOST_CC=cc 而不选择 aarch64-linux-gnu-gcc。
# compat Makefile 内的 HOST_CC ?= cc 仍可作为普通 Make 变量供 i386/x86_64 使用。
compat:
	@$(MAKE) -C src/compat \
		MCC=$(abspath $(MCC)) AR=$(AR) \
		PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) \
		SYSROOT=$(SYSROOT) ARCH=$(ARCH) all

# ============================================================
# Install
# ============================================================

install: $(ATOMIC_LIB) $(LIBC_LIB) $(CRT1_OBJ)
	@mkdir -p $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include/sys
	cp -r include/. $(DESTDIR)$(PREFIX)/include/
	cp $(LIBC_LIB)    $(DESTDIR)$(PREFIX)/lib/libc-meuos.a
	cp $(ATOMIC_LIB)  $(DESTDIR)$(PREFIX)/lib/libatomic-meuos.a
	cp $(CRT1_OBJ)    $(DESTDIR)$(PREFIX)/lib/crt1.o
	@$(MAKE) -C src/compat install DESTDIR=$(DESTDIR) PREFIX=$(PREFIX) ARCH=$(ARCH)

# ============================================================
# Tests
# ============================================================

check: $(ATOMIC_LIB)
	$(MAKE) install DESTDIR=$(abspath $(SYSROOT)) PREFIX=/usr
	@mkdir -p /tmp/meuos-libc-check
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/atomic test/atomic.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/fence test/fence.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/string test/string.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/write test/write.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/stdio test/stdio.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/malloc test/malloc.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/stdlib test/stdlib.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/ctype test/ctype.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/assert test/assert.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/c11-headers test/c11_headers.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/syscall-fs test/syscall_fs.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/process test/process.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/kernel-interfaces test/kernel_interfaces.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/socket test/socket.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/threads test/threads.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib -o /tmp/meuos-libc-check/crt test/crt.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib -o /tmp/meuos-libc-check/env test/env.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib -o /tmp/meuos-libc-check/bare-threads test/threads.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/phase2-counter test/phase2_counter.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/bare-tls test/bare_tls.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/malloc-threads test/malloc_threads.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/bare-stdio test/stdio.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/signal test/signal.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/setjmp test/setjmp.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/pthread test/pthread.c -l:libc-meuos.a
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/bare-process test/process.c -l:libc-meuos.a
	@for bin in atomic fence; do \
		if nm -u /tmp/meuos-libc-check/$$bin | grep -Eq '__atomic_|atomic_(thread|signal)_fence'; then \
			echo "unexpected unresolved atomic runtime symbol in $$bin" >&2; exit 1; \
		fi; \
	done
	@for bin in atomic threads phase2-counter bare-tls malloc-threads bare-stdio bare-process; do \
		if LC_ALL=C readelf -l /tmp/meuos-libc-check/$$bin | grep -q 'Requesting program interpreter'; then \
			echo "MeuOS specs unexpectedly produced a dynamically linked $$bin" >&2; exit 1; \
		fi; \
	done
	/tmp/meuos-libc-check/atomic
	/tmp/meuos-libc-check/fence
	/tmp/meuos-libc-check/string
	/tmp/meuos-libc-check/write
	/tmp/meuos-libc-check/stdio
	/tmp/meuos-libc-check/malloc
	/tmp/meuos-libc-check/stdlib
	/tmp/meuos-libc-check/ctype
	/tmp/meuos-libc-check/assert
	/tmp/meuos-libc-check/c11-headers
	/tmp/meuos-libc-check/syscall-fs
	/tmp/meuos-libc-check/process
	/tmp/meuos-libc-check/kernel-interfaces
	/tmp/meuos-libc-check/socket
	/tmp/meuos-libc-check/threads
	@/tmp/meuos-libc-check/crt; status=$$?; test $$status -eq 42
	/tmp/meuos-libc-check/env
	/tmp/meuos-libc-check/bare-threads
	/tmp/meuos-libc-check/phase2-counter
	/tmp/meuos-libc-check/bare-tls
	/tmp/meuos-libc-check/malloc-threads
	/tmp/meuos-libc-check/bare-stdio
	/tmp/meuos-libc-check/signal
	/tmp/meuos-libc-check/setjmp
	/tmp/meuos-libc-check/pthread
	/tmp/meuos-libc-check/bare-process
	@$(MAKE) -C src/compat check MCC=$(abspath $(MCC)) SYSROOT=$(abspath $(SYSROOT)) HOST_CC=$(HOST_CC) ARCH=$(ARCH)

check-mcc:
	$(MAKE) BUILD=build/mcc CC=$(MCC) check

check-i386-bootstrap:
	MCC=$(abspath $(MCC)) CC=$(HOST_CC) sh test/i386-bootstrap.sh

# aarch64-bootstrap.sh 跨编译自检 ELF64/AArch64 头；MEUOS_AARCH64_RUN=1
# 且 MEUOS_AARCH64_QEMU=<path> 时附加 qemu-user 运行时 gate，期望 hello=
# "aarch64 MeuOS libc" / setjmp="setjmp ok" / phase2="counter = 2000" /
# bare_tls="tls main=5 child=9 errno=31/47" / atomic-test 与 malloc-threads
# exit 0。产物缺失时自动触发 ARCH=aarch64 build。
check-aarch64-bootstrap:
	@if [ ! -f build/aarch64/libc-meuos.a ] || [ ! -f build/aarch64/libatomic-meuos.a ] || [ ! -f build/aarch64/crt1.o ]; then \
		$(MAKE) ARCH=aarch64; \
	fi
	ASCC=aarch64-linux-gnu-gcc sh test/aarch64-bootstrap.sh

# 一步式：build + install + 全套检查（包括 atomic/tls/threads/stdio/process）
# 默认 host 是 x86_64，跨架构检查通过 check-aarch64-bootstrap / check-i386-bootstrap。
check-all: check check-aarch64-bootstrap check-i386-bootstrap

clean:
	rm -rf build
	@$(MAKE) -C src/compat clean 2>/dev/null || true

.PHONY: all compat install check check-mcc check-i386-bootstrap \
        check-aarch64-bootstrap check-all clean
