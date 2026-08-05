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
else ifeq ($(ARCH),riscv64)
# riscv64 的 .S 也须经交叉 gcc 汇编（ecall/ll·sc 等指令 mcc 不内联）。
CC := $(MCC) --target=riscv64
AS := riscv64-linux-gnu-gcc
else ifeq ($(ARCH),loongarch64)
# loongarch64 的 .S 须经交叉 gcc 汇编（syscall 0/ll.sc/dbar 等指令 mcc 不内联）。
CC := $(MCC) --target=loongarch64
AS := loongarch64-linux-gnu-gcc
else ifeq ($(ARCH),arm)
# ARM 32-bit 通用目标。子架构通过以下变量配置（默认 v7-a + hard-float + vfpv3-d16）：
#   ARM_ARCH      架构版本（armv7-a, armv8-a 等）
#   ARM_FLOAT_ABI 浮点 ABI（hard, softfp, soft）
#   ARM_FPU       FPU 类型（vfpv3-d16, neon, vfpv4 等）
ARM_ARCH      ?= armv7-a
ARM_FLOAT_ABI ?= hard
ARM_FPU       ?= vfpv3-d16
ARM_FLAGS     := -march=$(ARM_ARCH) -mfloat-abi=$(ARM_FLOAT_ABI) -mfpu=$(ARM_FPU)
CC := arm-linux-gnu-gcc $(ARM_FLAGS)
AS := arm-linux-gnu-gcc $(ARM_FLAGS)
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

# libgcc-meuos.a — soft runtime helpers in the libgcc ABI (__divdi3, __ctzdi2,
# __floatsidf, ...).  Independent archive so these never pollute the libc
# symbol table; gcc/clang targeting this sysroot link it in place of a vendor
# libgcc.  Sources are generic C in src/libgcc_meuos/.
LIBGCC_SRCS := $(wildcard src/libgcc_meuos/*.c)
LIBGCC_OBJS := $(patsubst src/libgcc_meuos/%.c,$(BUILD)/libgcc_meuos/%.o,$(LIBGCC_SRCS))
LIBGCC_LIB  := $(BUILD)/libgcc-meuos.a

$(BUILD)/libgcc_meuos/%.o: src/libgcc_meuos/%.c
	@mkdir -p $(dir $@)
	$(CC) $(INCLUDE) -c -o $@ $<

$(LIBGCC_LIB): $(LIBGCC_OBJS)
	$(AR) rcs $@ $(LIBGCC_OBJS)

# Per-arch runtime helpers (TLS, setjmp, sigreturn, thread clone).
# load_gs.S and soft_arith.c are i386-only; set_tls.S is aarch64/armv7-only
# (variant I TLS 用 msr tpidr_el0 / mcr p15 直接设置线程指针)。
ifeq ($(ARCH),x86_64)
ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o \
                $(BUILD)/arch/$(ARCH)/setjmp.o \
                $(BUILD)/arch/$(ARCH)/sigreturn.o \
                $(BUILD)/arch/$(ARCH)/thread_clone.o \
                $(BUILD)/thread/__tls_get_addr.o
# __tls_get_addr.c for x86_64 lives in src/thread/ (host cc w/ inline asm)
$(BUILD)/thread/__tls_get_addr.o: src/thread/__tls_get_addr.c
	@mkdir -p $(dir $@)
	$(CC) -Iinclude -Isrc -c -o $@ $<
else ifeq ($(ARCH),aarch64)
ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o \
                $(BUILD)/arch/$(ARCH)/setjmp.o \
                $(BUILD)/arch/$(ARCH)/sigreturn.o \
                $(BUILD)/arch/$(ARCH)/thread_clone.o \
                $(BUILD)/arch/$(ARCH)/set_tls.o
else ifeq ($(ARCH),riscv64)
# riscv64 无需 set_tls.S：主线程 tp 在 crt1.S 用 `mv tp, a0` 设置，子线程经
# CLONE_SETTLS 由内核设置；静态 local-exec TLS 无需 __tls_get_addr。
ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o \
                $(BUILD)/arch/$(ARCH)/setjmp.o \
                $(BUILD)/arch/$(ARCH)/sigreturn.o \
                $(BUILD)/arch/$(ARCH)/thread_clone.o
else ifeq ($(ARCH),loongarch64)
# loongarch64 无需 set_tls.S：主线程 tp 在 crt1.S 用 `move $tp, $a0` 设置，子线程经
# CLONE_SETTLS 由内核设置；静态 local-exec TLS 无需 __tls_get_addr。
ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o \
                $(BUILD)/arch/$(ARCH)/setjmp.o \
                $(BUILD)/arch/$(ARCH)/sigreturn.o \
                $(BUILD)/arch/$(ARCH)/thread_clone.o
else ifeq ($(ARCH),arm)
# armv7 使用协处理器 c13 设置线程指针（mcr p15,0,rd,c13,c0,3）。
# 含 set_tls.S 用于主线程 TP 设置；aeabi.c 提供 __aeabi_* EABI 运行时
# helper（64/32 位除法、memcpy 等）。
ARCH_RT_OBJS := $(BUILD)/arch/$(ARCH)/tls.o \
                $(BUILD)/arch/$(ARCH)/setjmp.o \
                $(BUILD)/arch/$(ARCH)/sigreturn.o \
                $(BUILD)/arch/$(ARCH)/thread_clone.o \
                $(BUILD)/arch/$(ARCH)/set_tls.o \
                $(BUILD)/arch/$(ARCH)/aeabi.o \
                $(BUILD)/arch/$(ARCH)/aeabi_wrap.o
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
                  $(BUILD)/string/error.o $(BUILD)/string/token.o \
                  $(BUILD)/string/compare.o $(BUILD)/string/dirname.o
STDLIB_OBJS    := $(BUILD)/stdlib/malloc.o $(BUILD)/stdlib/convert.o \
                  $(BUILD)/stdlib/env.o $(BUILD)/stdlib/exit.o \
                  $(BUILD)/stdlib/process.o $(BUILD)/stdlib/search.o \
                  $(BUILD)/stdlib/math.o $(BUILD)/stdlib/strfmon.o
STDIO_OBJS     := $(BUILD)/stdio/file.o $(BUILD)/stdio/char_io.o \
                  $(BUILD)/stdio/block_io.o $(BUILD)/stdio/position.o \
                  $(BUILD)/stdio/fmt_out.o $(BUILD)/stdio/fmt_in.o \
                  $(BUILD)/stdio/fp_fmt.o $(BUILD)/stdio/memory_stream.o \
                  $(BUILD)/stdio/file_misc.o $(BUILD)/stdio/truncate.o \
                  $(BUILD)/stdio/popen.o $(BUILD)/stdio/getline.o
SYSCALL_OBJS   := $(addprefix $(BUILD)/syscall/, \
                  read.o write.o open.o close.o fork.o execve.o exit.o \
                  mmap.o munmap.o mprotect.o brk.o stat.o fstat.o lstat.o lseek.o \
                  getpid.o gettid.o getcwd.o chdir.o \
                  dup.o dup2.o pipe.o waitpid.o wait.o wait4.o nanosleep.o \
                  clock_gettime.o getdents64.o socketcall.o chmod.o fchmod.o \
                  access.o sched_yield.o select.o \
                  fchown.o chown.o getuid.o getgid.o geteuid.o getegid.o \
                  umask.o isatty.o fchdir.o symlink.o link.o readlink.o \
                  unlink.o mkdir.o rmdir.o rename.o mkstemp.o mkstemps.o \
                  time.o times.o utime.o gettimeofday.o sysconf.o flock.o \
                  fcntl.o uname.o time_stubs.o ioctl.o \
                  pread.o pwrite.o readv.o writev.o preadv.o pwritev.o \
                  openat.o pipe2.o dup3.o)
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
SUPPORT_OBJS   := $(BUILD)/ctype/ctype.o $(BUILD)/signal/signal.o \
                  $(BUILD)/dirent/dirent.o $(BUILD)/locale/locale.o \
                  $(BUILD)/glob/fnmatch.o $(BUILD)/glob/glob.o \
                  $(BUILD)/syslog/syslog.o \
                  $(BUILD)/socket/socket.o $(BUILD)/socket/inet.o \
                  $(BUILD)/regex/regex.o \
                  $(BUILD)/complex/complex.o \
                  $(BUILD)/wchar/wchar.o
# errno: TLS-based _Thread_local errno for all architectures
# (loongarch64 previously used non-TLS errno_la64.c as a BFD 2.41 assertion
# workaround; that is no longer needed with mt/ld linking.)
SUPPORT_OBJS   += $(BUILD)/errno/errno.o
# Exception runtime (setjmp/longjmp base): _meuos_exc_throw + try helpers.
SUPPORT_OBJS   += $(BUILD)/meuos_exc.o
# startup.c — entry contract (__progname, getauxval); called from crt1.S.
SUPPORT_OBJS   += $(BUILD)/startup.o
# P2 gap batch: config strings, signal strings, passwd/group, byte order.
SUPPORT_OBJS   += $(BUILD)/confstr.o $(BUILD)/pwd.o $(BUILD)/grp.o
SUPPORT_OBJS   += $(BUILD)/signal/strsignal.o $(BUILD)/socket/byteorder.o
SUPPORT_OBJS   += $(BUILD)/netdb/gethost.o $(BUILD)/netdb/getserv.o \
                  $(BUILD)/netdb/getproto.o $(BUILD)/netdb/getnet.o \
                  $(BUILD)/netdb/getaddrinfo.o \
                  $(BUILD)/netdb/herror.o $(BUILD)/netdb/h_errno.o
SYSCALL_GATE_OBJ := $(BUILD)/internal/arch/$(ARCH)/syscall.o
CRT1_OBJ := $(BUILD)/crt1.o
# dlfcn.c is a hardcoded ELF64/x86_64 loader (EM_X86_64=62, ELF64 structs);
# it has no meaning on 32-bit targets, where mcc's i386 backend also lacks
# the Kl extub support it needs, so exclude it there.
ifeq ($(filter $(ARCH),i386 arm),$(ARCH))
DL_OBJS :=
else
DL_OBJS := $(BUILD)/dlfcn.o
endif
TERMIOS_OBJS := $(BUILD)/termios/termios.o
TIME_OBJS    := $(BUILD)/time/time.o
LIBC_LIB := $(BUILD)/libc-meuos.a

ALL_OBJS := $(STRING_OBJS) $(STDLIB_OBJS) $(STDIO_OBJS) $(SYSCALL_OBJS) \
            $(ARCH_PRCTL_OBJ) $(THREAD_OBJS) $(SUPPORT_OBJS) $(SYSCALL_GATE_OBJ) \
            $(ARCH_RT_OBJS) $(ATOMIC_OBJ) $(DL_OBJS) $(TERMIOS_OBJS) $(TIME_OBJS)

# ============================================================
# Build rules
# ============================================================

all: $(ATOMIC_LIB) $(LIBC_LIB) $(CRT1_OBJ) $(LIBGCC_LIB) compat

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

install: $(ATOMIC_LIB) $(LIBC_LIB) $(CRT1_OBJ) $(LIBGCC_LIB)
	@mkdir -p $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include/sys
	cp -r include/. $(DESTDIR)$(PREFIX)/include/
	cp $(LIBC_LIB)    $(DESTDIR)$(PREFIX)/lib/libc-meuos.a
	cp $(ATOMIC_LIB)  $(DESTDIR)$(PREFIX)/lib/libatomic-meuos.a
	cp $(CRT1_OBJ)    $(DESTDIR)$(PREFIX)/lib/crt1.o
	cp $(LIBGCC_LIB)  $(DESTDIR)$(PREFIX)/lib/libgcc-meuos.a
	@$(MAKE) -C src/compat install DESTDIR=$(DESTDIR) PREFIX=$(PREFIX) ARCH=$(ARCH)

# ============================================================
# Tests
# ============================================================

check: $(ATOMIC_LIB) $(LIBGCC_LIB)
	$(MAKE) install DESTDIR=$(abspath $(SYSROOT)) PREFIX=/usr
	@mkdir -p /tmp/meuos-libc-check
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/atomic test/atomic.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/fence test/fence.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/string test/string.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/write test/write.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/stdio test/stdio.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/malloc test/malloc.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/stdlib test/stdlib.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/libgen test/libgen.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/ctype test/ctype.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/assert test/assert.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/c11-headers test/c11_headers.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/syscall-fs test/syscall_fs.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/select test/select.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/process test/process.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/kernel-interfaces test/kernel_interfaces.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/exc test/exc.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/getline_gate test/getline_gate.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/inet test/inet.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/strfmon_gate test/strfmon_gate.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/auxv test/auxv.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/uio test/uio.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/p2 test/p2.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-check/socket test/socket.c
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/libgcc test/libgcc.c -l:libgcc-meuos.a -l:libc-meuos.a
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
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static -o /tmp/meuos-libc-check/netdb test/netdb.c -l:libc-meuos.a
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
	/tmp/meuos-libc-check/libgen
	/tmp/meuos-libc-check/ctype
	/tmp/meuos-libc-check/assert
	/tmp/meuos-libc-check/c11-headers
	/tmp/meuos-libc-check/syscall-fs
	/tmp/meuos-libc-check/select
	/tmp/meuos-libc-check/process
	/tmp/meuos-libc-check/kernel-interfaces
	/tmp/meuos-libc-check/exc
	/tmp/meuos-libc-check/getline_gate
	/tmp/meuos-libc-check/inet
	/tmp/meuos-libc-check/strfmon_gate
	/tmp/meuos-libc-check/auxv
	/tmp/meuos-libc-check/uio
	/tmp/meuos-libc-check/p2
	/tmp/meuos-libc-check/libgcc
	/tmp/meuos-libc-check/socket
	/tmp/meuos-libc-check/threads
	@# libgcc soft helpers must live ONLY in libgcc-meuos.a, never pollute libc.
	@if nm $(LIBC_LIB) 2>/dev/null | grep -Eq ' (__divdi3|__udivdi3|__ctzdi2|__floatsidf|__multi3)$$'; then \
		echo "FAIL: libc-meuos.a must not export libgcc helpers" >&2; exit 1; fi
	@# ... but the libgcc archive must provide them.
	@for s in __divdi3 __udivdi3 __moddi3 __umoddi3 __muldi3 __clzdi2 __ctzdi2 \
	          __popcountdi2 __clzsi2 __ctzsi2 __bswapsi2 __bswapdi2 __floatsidf \
	          __floatdidf __floatdisf __fixdfdi; do \
		if nm $(LIBGCC_LIB) 2>/dev/null | grep -Eq " [Tt] $$s$$"; then :; else \
			echo "FAIL: libgcc-meuos.a missing helper $$s" >&2; exit 1; fi; \
	done
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
	/tmp/meuos-libc-check/netdb
	@$(MAKE) -C src/compat check MCC=$(abspath $(MCC)) SYSROOT=$(abspath $(SYSROOT)) HOST_CC=$(HOST_CC) ARCH=$(ARCH)

# Verify the full C library links through MeuOS's native linker (mt/ld)
# without calling the host ld.  SKIPs gracefully when meuos-toolchain
# has not been built.
check-native-linker:
	@MT_DIR=$(abspath ../meuos-toolchain/build); \
	if [ ! -x "$$MT_DIR/bin/ld" ] || [ ! -x "$$MT_DIR/bin/as" ]; then \
		echo "check-native-linker: SKIP (meuos-toolchain not built)"; \
		exit 0; \
	fi; \
	$(MAKE) install DESTDIR=$(abspath $(SYSROOT)) PREFIX=/usr; \
	export MT_AS="$$MT_DIR/bin/as"; \
	export MT_LD="$$MT_DIR/bin/ld"; \
	export LC_ALL=C; \
	rm -rf /tmp/meuos-libc-native-linker; \
	mkdir -p /tmp/meuos-libc-native-linker; \
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-native-linker/hello test/native_linker_hello.c || { echo "FAIL: native link failed"; exit 1; }; \
	out=$$(/tmp/meuos-libc-native-linker/hello); \
	echo "$$out"; \
	if [ "$$out" != "native linker ok" ]; then \
		echo "FAIL: unexpected output: $$out"; exit 1; \
	fi; \
		if command -v strace >/dev/null 2>&1; then \
			strace -f -e trace=execve $(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) -o /tmp/meuos-libc-native-linker/hello2 test/native_linker_hello.c 2>/tmp/meuos-libc-native-linker/strace.log >/dev/null || true; \
			if grep 'execve' /tmp/meuos-libc-native-linker/strace.log 2>/dev/null \
			    | grep -v 'meuos-toolchain' \
			    | grep -v '/mcc' \
			    | grep -qE '"[^"]*/(ld|gcc|cc|clang)"' ; then \
				echo "FAIL: host ld/cc was invoked for --specs=meuos link"; \
				grep 'execve' /tmp/meuos-libc-native-linker/strace.log | grep -E '/(ld|gcc|cc|clang)"' | head -5; \
				exit 1; \
			fi; \
		fi; \
	rm -rf /tmp/meuos-libc-native-linker; \
	echo "PASS: native linker check"

# Cross-line integration gate: build a real POSIX program (mini-bc) with the
# HOST compiler (gcc, not mcc) against this meuos-libc sysroot, to prove the
# "gcc + meuos-libc end-to-end" goal and guard it against regressions.  This
# is deliberately a host-C AND host-gcc path (distinct from the mcc main
# build).  SKIPs (exit 0) when no host gcc or sysroot is available.
check-integration:
	@SYSROOT_I="$(abspath $(SYSROOT))/usr"; \
	if ! command -v gcc >/dev/null 2>&1; then \
		echo "check-integration: SKIP (no host gcc)"; exit 0; fi; \
	if ! test -f "$$SYSROOT_I/lib/libc-meuos.a" || ! test -f "$$SYSROOT_I/lib/libgcc-meuos.a" \
	   || ! test -f "$$SYSROOT_I/lib/crt1.o"; then \
		echo "check-integration: SKIP (sysroot not installed; run make install)"; exit 0; fi; \
	echo "SYSROOT=$$SYSROOT_I"; \
	rm -rf /tmp/meuos-libc-check-integration; mkdir -p /tmp/meuos-libc-check-integration; \
	echo "check-integration: host gcc + meuos-libc -> mini-bc"; \
	if ! gcc -O2 -nostdlib -nostdinc -isystem "$$SYSROOT_I/include" -L"$$SYSROOT_I/lib" \
	      -o /tmp/meuos-libc-check-integration/mini-bc test/mini-bc.c \
	      "$$SYSROOT_I/lib/crt1.o" -lc-meuos -lgcc-meuos \
	      > /tmp/meuos-libc-check-integration/build.log 2>&1; then \
		cat /tmp/meuos-libc-check-integration/build.log >&2; \
		echo "FAIL: gcc could not build mini-bc (see log)" >&2; exit 1; fi; \
	out=$$(printf '1+2*3\n(1+2)*3\n100/7\nquit\n' | /tmp/meuos-libc-check-integration/mini-bc); \
	status=$$?; \
	echo "$$out"; \
	if [ $$status -ne 0 ]; then echo "FAIL: mini-bc exit $$status" >&2; exit 1; fi; \
	exp="$$(printf '7\n9\n14.285714285714\n')"; \
	if [ "$$out" != "$$exp" ]; then \
		echo "FAIL: mini-bc output mismatch" >&2; \
		echo "--- got ---"; echo "$$out"; echo "--- want ---"; echo "$$exp"; exit 1; fi; \
	rm -rf /tmp/meuos-libc-check-integration; \
	echo "PASS: integration (gcc + meuos-libc, mini-bc)"

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

# riscv64-bootstrap.sh 跨编译自检 ELF64/RISC-V 头；MEUOS_RISCV64_RUN=1
# 且 MEUOS_RISCV64_QEMU=<path> 时附加 qemu-user 运行时 gate，期望与
# aarch64 相同的语义（hello / setjmp / phase2 / bare_tls / atomic / malloc_threads）。
# riscv64 无 set_tls.S（tp 由 crt1 的 `mv tp,a0` 与 CLONE_SETTLS 设置）。
check-riscv64-bootstrap:
	@if [ ! -f build/riscv64/libc-meuos.a ] || [ ! -f build/riscv64/libatomic-meuos.a ] || [ ! -f build/riscv64/crt1.o ]; then \
		$(MAKE) ARCH=riscv64; \
	fi
	ASCC=riscv64-linux-gnu-gcc sh test/riscv64-bootstrap.sh

# loongarch64-bootstrap.sh 跨编译自检 ELF64/LoongArch 头；MEUOS_LOONGARCH64_RUN=1
# 且 MEUOS_LOONGARCH64_QEMU=<path> 时附加 qemu-user 运行时 gate。
# loongarch64 无 set_tls.S（tp 由 crt1 的 `move $tp,$a0` 与 CLONE_SETTLS 设置）。
check-loongarch64-bootstrap:
	@if [ ! -f build/loongarch64/libc-meuos.a ] || [ ! -f build/loongarch64/libatomic-meuos.a ] || [ ! -f build/loongarch64/crt1.o ]; then \
		$(MAKE) ARCH=loongarch64; \
	fi
	ASCC=loongarch64-linux-gnu-gcc sh test/loongarch64-bootstrap.sh

# arm-bootstrap.sh 跨编译自检 ELF32/ARM 头；MEUOS_ARM_RUN=1 且
# MEUOS_ARM_QEMU=<path> 时附加 qemu-user 运行时 gate。
check-arm-bootstrap:
	@if [ ! -f build/arm/libc-meuos.a ] || [ ! -f build/arm/libatomic-meuos.a ] || [ ! -f build/arm/crt1.o ]; then \
		$(MAKE) ARCH=arm; \
	fi
	ARM_ARCH=$(ARM_ARCH) ARM_FLOAT_ABI=$(ARM_FLOAT_ABI) ARM_FPU=$(ARM_FPU) \
		ASCC=arm-linux-gnu-gcc sh test/arm-bootstrap.sh

# 一步式：build + install + 全套检查（包括 atomic/tls/threads/stdio/process）
# 默认 host 是 x86_64，跨架构检查通过 check-aarch64-bootstrap / check-i386-bootstrap。
check-all: check check-integration check-aarch64-bootstrap check-i386-bootstrap check-riscv64-bootstrap check-loongarch64-bootstrap check-arm-bootstrap

clean:
	rm -rf build
	@$(MAKE) -C src/compat clean 2>/dev/null || true

.PHONY: all compat install check check-integration check-native-linker check-mcc check-i386-bootstrap \
        check-aarch64-bootstrap check-riscv64-bootstrap check-loongarch64-bootstrap check-arm-bootstrap check-all clean
