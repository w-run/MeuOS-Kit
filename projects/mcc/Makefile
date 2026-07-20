# mcc - MeuOS C Compiler
#
# Builds the mcc binary using a host C compiler (gcc or tcc).
# mcc is a unified single-tree compiler (source-level integration of a
# frontend and an IR backend, originally derived from cproc + QBE).
#
# Layout:
#   include/        shared headers
#   src/driver/     main.c unified driver
#   src/{lex,parse,sema,util}/   frontend (lexing, parsing, semantics)
#   src/irgen/      AST -> IR direct construction (no textual IR step)
#   src/{ir,opt,abi,emit}/       IR core + optimization passes
#   src/target/{amd64,arm64,rv64}/   per-arch backends
#   build/          compilation output (gitignored)

# ---- Toolchain ----
HOST_CC ?= cc
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
# Compile every supported backend so one mcc binary can target any supported
# architecture.
SRC_DIRS := src
SRCS := $(shell find $(SRC_DIRS) -name '*.c' | sort)
OBJS := $(patsubst %.c,build/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

BIN := mcc

# ---- Rules ----
all: $(BIN)

$(BIN): $(OBJS)
	$(HOST_CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

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

check-driver: $(BIN)
	sh test/driver/sysroot.sh ./$(BIN)
	sh test/driver/feature-regress.sh ./$(BIN)

# Aggregate ABI regression: verifies that adjacent bit-fields sharing a
# storage unit do not inflate the direct-IR type layout during by-value calls.
check-abi: $(BIN)
	@./$(BIN) -o /tmp/mcc-bitfield-aggregate test/abi/bitfield_aggregate.c \
		&& /tmp/mcc-bitfield-aggregate

# Rebuild mcc as a fully static program against the MeuOS sysroot, then use
# that rebuilt compiler to compile and execute a program.  The host compiler
# remains only the assembler/linker handoff selected by MCC_HOST_CC.
check-sysroot-static: $(BIN)
	@$(MAKE) -C ../meuos-libc install DESTDIR=$(abspath ../sysroot) PREFIX=/usr
	@work=$$(mktemp -d /tmp/mcc-sysroot-static.XXXXXX); trap 'rm -rf "$$work"' EXIT HUP INT TERM; \
	cp -a . "$$work/mcc"; \
	$(MAKE) -C "$$work/mcc" clean; \
	MCC_HOST_CC=cc $(MAKE) -C "$$work/mcc" HOST_CC="$(abspath $(BIN))" \
		CFLAGS="-O2 -g -std=gnu11 -Iinclude -I$(abspath ../meuos-libc/include) -Wno-all --specs=meuos --sysroot=$(abspath ../sysroot) --nostdlib --static"; \
	"$$work/mcc/mcc" --version; \
	printf '%s\n' 'int main(void) { return 0; }' > "$$work/hello.c"; \
	"$$work/mcc/mcc" -o "$$work/hello" "$$work/hello.c"; \
	"$$work/hello"

# Compatibility alias for existing scripts.
check-la64: check-loongarch64

.PHONY: all clean check check-loongarch64 check-i386 check-targets check-la64 check-c11-atomic check-c11 check-driver check-abi check-sysroot-static
