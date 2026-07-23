# mcc - MeuOS C Compiler
#
# Builds the mcc binary using a host C compiler (gcc or tcc).
# mcc is a unified single-tree compiler (source-level integration of a
# frontend and an IR backend, originally derived from cproc + QBE).
#
# Layout (split for future m++ shared-backend support, see
# .todo/cpp-shared-backend.md stage A):
#   include/        shared headers
#   src/driver/     main.c unified driver                  (C frontend)
#   src/{lex,parse,sema}/  frontend (lex/parsing/semantics) (C frontend)
#   src/irgen/      AST -> IR direct construction           (C frontend)
#   src/util/       common utilities (xmalloc/utf/warn)     (shared)
#   src/{ir,opt,abi,emit}/  IR core + optimization passes   (libmcc)
#   src/target/{x86_64,aarch64,riscv64,i386,loongarch64}/  (libmcc)
#   build/          compilation output (gitignored)
#
# Stage A split (this file): backend sources are archived into
# build/libmcc.a; the mcc binary = C frontend objects + libmcc.a.
# Behavior is byte-equivalent to the previous monolithic link.

# ---- Toolchain ----
HOST_CC ?= cc
AR      ?= ar
# The source is known-clean C99/C11 but emits a lot of -Wall noise
# (unused params, sign compares, missing braces in designated initializers,
# pointer casts, etc.). Phase 1a gate is functional correctness, not warning
# hygiene. We revisit warnings once mcc can self-host.
CFLAGS  ?= -O2 -g -std=gnu11 -Iinclude -Wno-all
LDFLAGS ?=
LDLIBS  ?=

# ---- Target architecture ----
# Default target is the host architecture's canonical public name; selected
# at runtime via -target or -t by pick_target() in driver/main.c. Backend
# directory and symbol names may use historical short names, but public
# target IDs use x86_64/aarch64/riscv64/i386/loongarch64.
TARGET_ARCH ?= $(firstword $(filter $(shell uname -m),x86_64 aarch64 riscv64 loongarch64 i386 i486 i586 i686))
TARGET_ARCH := $(if $(TARGET_ARCH),$(TARGET_ARCH),x86_64)

ifeq ($(TARGET_ARCH),x86_64)
  DEFAULT_TARGET := x86_64
else ifeq ($(TARGET_ARCH),aarch64)
  DEFAULT_TARGET := aarch64
else ifeq ($(TARGET_ARCH),riscv64)
  DEFAULT_TARGET := riscv64
else ifeq ($(TARGET_ARCH),loongarch64)
  DEFAULT_TARGET := loongarch64
else ifneq ($(filter $(TARGET_ARCH),i386 i486 i586 i686),)
  DEFAULT_TARGET := i386
else
  $(error unsupported TARGET_ARCH=$(TARGET_ARCH))
endif

# Expose the default target to driver/main.c as a compile-time hint.
CFLAGS += -DMCC_DEFAULT_TARGET=\"$(DEFAULT_TARGET)\"

# ---- Sources ----
# Split into C frontend (FE, language-specific) and shared backend (BE,
# language-agnostic). Stage A of the libmcc migration: BE objects are
# archived into libmcc.a so a future m++ binary can link the same archive.
# util/ is shared (utf/warn/xmalloc are used by both FE and BE).
FE_DIRS := src/driver src/lex src/parse src/sema src/irgen
BE_DIRS := src/ir src/opt src/abi src/emit src/target src/util

FE_SRCS := $(shell find $(FE_DIRS) -name '*.c' | sort)
BE_SRCS := $(shell find $(BE_DIRS) -name '*.c' | sort)
FE_OBJS := $(patsubst %.c,build/%.o,$(FE_SRCS))
BE_OBJS := $(patsubst %.c,build/%.o,$(BE_SRCS))
DEPS := $(FE_OBJS:.o=.d) $(BE_OBJS:.o=.d)

BIN := mcc
LIB := build/libmcc.a

# ---- Rules ----
all: $(BIN)

# mcc = C frontend objects + libmcc.a (shared backend archive)
$(BIN): $(FE_OBJS) $(LIB)
	$(HOST_CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FE_OBJS) $(LIB) $(LDLIBS)

# libmcc.a = backend objects (language-agnostic, future m++ shares this)
$(LIB): $(BE_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

clean:
	rm -rf build $(BIN)

# Quick sanity check: build mcc, then compile+run hello-world.
check: $(BIN)
	@echo 'int main(void){return 0;}' > /tmp/mcc-hello.c \
	  && ./$(BIN) -o /tmp/mcc-hello /tmp/mcc-hello.c \
	  && /tmp/mcc-hello; echo "exit=$$?"

check-loongarch64: $(BIN)
	sh test/loongarch64/regress.sh ./$(BIN)

check-i386: $(BIN)
	sh test/i386/regress.sh ./$(BIN)

# i386 runtime regression: compiles and runs i386 static binaries that
# exercise Kl decomposition (push/pop EAX fix), x87 floating-point,
# time64/stat (64-bit time_t), and cross-function va_list.  Requires
# the i386 MeuOS sysroot at ../sysroot-i386; skips gracefully if absent.
check-i386-runtime: $(BIN)
	sh test/i386/runtime.sh ./$(BIN)

# i386 runtime regression inside the MeuOS Kit qemu-system-i386 VM (env/).
# Stronger gate than check-i386-runtime: runs the same binaries on a REAL
# 32-bit kernel (Alpine linux-virt 6.6.x under QEMU TCG) via the 9p share.
# Requires env/build/qemu-install/bin/qemu-system-i386 and sysroot-i386;
# skips gracefully if either is absent.
check-i386-qemu: $(BIN)
	sh test/i386/qemu-runtime.sh ./$(BIN)

# aarch64 runtime regression: compiles aarch64 static binaries (threads/
# atomics, floating-point, TLS, time64/stat, cross-function va_list) and
# runs them inside the env/bin/qvm aarch64 VM via the 9p share.  Requires
# sysroot-aarch64 (make -C ../meuos-libc ARCH=aarch64 install) and the
# aarch64 VM (qvm boot aarch64); skips gracefully if either is absent.
check-aarch64-runtime: $(BIN)
	sh test/aarch64/runtime.sh ./$(BIN)

check-targets: $(BIN)
	sh test/targets/regress.sh ./$(BIN)

# C11 atomic regressions use a small local <stdatomic.h> shim until
# meuos-libc owns the installed public header.  Both runtime cases must pass.
check-c11-atomic: $(BIN)
	@for t in test/c11/atomic_basic.c test/c11/atomic_concurrent.c; do \
		out=/tmp/mcc-$$(basename $$t .c); \
		./$(BIN) -Itest/c11 -o $$out $$t -lpthread && $$out; \
	done

# Full currently self-contained C11 matrix.  The installed libc headers are
# a Phase 2 deliverable, so the atomic tests use test/c11/stdatomic.h.
check-c11: $(BIN)
	@for t in test/c11/*.c; do \
		out=/tmp/mcc-c11-$$(basename $$t .c); \
		case $$t in \
			*atomic*|*thread_local*) ./$(BIN) -Itest/c11 -o $$out $$t -lpthread ;; \
			*varargs*)             ./$(BIN) -Itest/c11 -o $$out $$t ;; \
			*)                     ./$(BIN) -o $$out $$t ;; \
		esac || exit $$?; \
		$$out || exit $$?; \
	done

# C99 standard feature tests (§6).
check-c99: $(BIN)
	@for t in test/c99/*.c; do \
		out=/tmp/mcc-c99-$$(basename $$t .c); \
		case $$t in \
			*extern_defs*) true ;; \
			*extern*)    ./$(BIN) -Itest/c99 -o $$out $$t test/c99/extern_defs.c || exit $$?; \
			             $$out || exit $$? ;; \
			*)           ./$(BIN) -Itest/c99 -o $$out $$t || exit $$?; \
			             $$out || exit $$? ;; \
		esac; \
	done

check-c23: $(BIN)
	@for t in test/c23/*.c; do \
		out=/tmp/mcc-c23-$$(basename $$t .c); \
		./$(BIN) -o $$out $$t 2>&1 || exit $$?; \
		$$out || exit $$?; \
	done

check-driver: $(BIN)
	sh test/driver/sysroot.sh ./$(BIN)
	sh test/driver/feature-regress.sh ./$(BIN)

# P3: verify MT_AS/MT_LD/MT_AR redirect mcc to the MeuOS toolchain
# (mt/as, mt/ld) instead of the host cc.  SKIPs gracefully when
# meuos-toolchain has not been built.
check-mt-integration: $(BIN)
	@MT_DIR=$(abspath ../meuos-toolchain/build); \
	if [ -x "$$MT_DIR/bin/as" ]; then \
		sh test/driver/mt_integration.sh ./$(BIN) "$$MT_DIR"; \
	else \
		echo "check-mt-integration: SKIP (meuos-toolchain not built)"; \
	fi

# Aggregate ABI regression: verifies that adjacent bit-fields sharing a
# storage unit do not inflate the direct-IR type layout during by-value calls.
check-abi: $(BIN)
	@./$(BIN) -o /tmp/mcc-bitfield-aggregate test/abi/bitfield_aggregate.c \
		&& /tmp/mcc-bitfield-aggregate

# Rebuild mcc as a fully static program against the MeuOS sysroot, then use
# that rebuilt compiler to compile and execute a program.  The host compiler
# remains only the assembler/linker handoff selected by MCC_HOST_CC.
#
# The self-rebuild deliberately drops MT_AS/MT_LD/MT_AR from the
# environment: mt/as does not yet support every addressing mode mcc
# emits (extended SIB index, etc.), so the bootstrap self-rebuild keeps
# using the host cc as assembler/linker.  The MT integration itself is
# exercised by check-mt-integration.
check-sysroot-static: $(BIN)
	@$(MAKE) -C ../meuos-libc install DESTDIR=$(abspath ../sysroot) PREFIX=/usr
	@work=$$(mktemp -d /tmp/mcc-sysroot-static.XXXXXX); trap 'rm -rf "$$work"' EXIT HUP INT TERM; \
	cp -a . "$$work/mcc"; \
	$(MAKE) -C "$$work/mcc" clean; \
	env -u MT_AS -u MT_LD -u MT_AR MCC_HOST_CC=cc $(MAKE) -C "$$work/mcc" HOST_CC="$(abspath $(BIN))" \
		CFLAGS="-O2 -g -std=gnu11 -Iinclude -I$(abspath ../meuos-libc/include) -Wno-all --specs=meuos --sysroot=$(abspath ../sysroot) --nostdlib --static"; \
	"$$work/mcc/mcc" --version; \
	printf '%s\n' 'int main(void) { return 0; }' > "$$work/hello.c"; \
	"$$work/mcc/mcc" -o "$$work/hello" "$$work/hello.c"; \
	"$$work/hello"

# Compatibility alias for existing scripts.
check-la64: check-loongarch64

.PHONY: all clean check check-loongarch64 check-i386 check-i386-runtime check-i386-qemu check-aarch64-runtime check-targets check-la64 check-c11-atomic check-c11 check-c99 check-c23 check-driver check-mt-integration check-abi check-sysroot-static check-chibicc check-community

# chibicc community functional test suite (C11/C23 conformance).
# Runs test/community/chibicc/run.sh against the MeuOS sysroot.
# See test/community/chibicc/REPORT.md for results and classification.
check-chibicc: $(BIN)
	@sh test/community/chibicc/run.sh

check-community: check-c99 check-chibicc
