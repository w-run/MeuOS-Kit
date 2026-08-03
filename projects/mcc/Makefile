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
#   src/c/{lex,parse,sema,irgen}/  C frontend              (C frontend)
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
CFLAGS  ?= -O2 -g -std=gnu11 -Iinclude -I../meuos-sysroot/include -I../meuos-toolchain/include -Wno-all
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
FE_DIRS := src/driver src/cpp src/c
BE_DIRS := src/mir src/lir src/ir src/opt src/abi src/emit src/target src/util

# mpp_main.c provides the C++-mode main() (g_lang=1) and is linked only
# into m++; mcc links c_main.c (C mode, g_lang=0).
FE_SRCS := $(shell find $(FE_DIRS) -name '*.c' ! -name 'mpp_main.c' | sort)
BE_SRCS := $(shell find $(BE_DIRS) -name '*.c' | sort)
FE_OBJS := $(patsubst %.c,build/%.o,$(FE_SRCS))
BE_OBJS := $(patsubst %.c,build/%.o,$(BE_SRCS))
DEPS := $(FE_OBJS:.o=.d) $(BE_OBJS:.o=.d)

BIN := mcc
LIB := build/libmcc.a

# ---- Rules ----
all: $(BIN) $(MSYS_LIB)

# libmsys.a — .msys single-file sysroot reader (shared dependency)
MSYS_LIB := $(abspath ../meuos-sysroot/build/libmsys.a)
MSYS_DIR  := $(abspath ../meuos-sysroot)

# Build libmsys if not already present (used by mcc at link time)
$(MSYS_LIB):
	$(MAKE) -C $(MSYS_DIR)

# mcc = C frontend objects + libmcc.a (shared backend archive).
# mcc links c_main.o (C mode, g_lang=0); m++ links mpp_main.o (g_lang=1).
# share the driver; the m++ binary (target `mpp`) will link the C++
# frontend objects instead of the C frontend objects (dual-binary
# shared-backend design, see C.1).
$(BIN): $(FE_OBJS) $(LIB)
	$(HOST_CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FE_OBJS) $(LIB) $(LDLIBS) -L../meuos-sysroot/build -lmsys

# m++ (C++ compiler) — stage C.1: same driver, C++ frontend under
# construction (falls back to the shared C driver for now).
MPP_BIN := m++
$(MPP_BIN): $(FE_OBJS) $(LIB) build/src/driver/mpp_main.o
	$(HOST_CC) $(CFLAGS) $(LDFLAGS) -o $@ \
	    $(filter-out build/src/driver/c_main.o,$(FE_OBJS)) \
	    build/src/driver/mpp_main.o $(LIB) $(LDLIBS) -L../meuos-sysroot/build -lmsys

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
	  && ./$(BIN) --specs=host -o /tmp/mcc-hello /tmp/mcc-hello.c \
	  && /tmp/mcc-hello; echo "exit=$$?"

check-loongarch64: $(BIN)
	sh test/loongarch64/regress.sh ./$(BIN)

check-i386: $(BIN)
	sh test/i386/regress.sh ./$(BIN)

# arm runtime regression: compiles and runs arm static binaries through
# qemu-arm (user-mode).  Exercises AAPCS base-standard vararg
# marshalling — 8-byte stack alignment and 64-bit Kl packing.  Requires
# sysroot/arm, mt/as + mt/ld, and env/qemu/qemu-arm-static; skips
# gracefully if any is absent.
check-arm: $(BIN)
	sh test/arm/regress.sh ./$(BIN)

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

check-pic-verify: $(BIN)
	sh test/pic_verify.sh ./$(BIN)

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
	@export MEUOS_SYSROOT="$(abspath ../sysroot)"; \
	for t in test/c11/*.c; do \
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
	@export MEUOS_SYSROOT="$(abspath ../sysroot)"; \
	for t in test/c99/*.c; do \
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
	@export MEUOS_SYSROOT="$(abspath ../sysroot)"; \
	for t in test/c23/*.c; do \
		out=/tmp/mcc-c23-$$(basename $$t .c); \
		./$(BIN) -o $$out $$t 2>&1 || exit $$?; \
		$$out || exit $$?; \
	done

# C23 negative tests: test/c23/neg/*.neg.c must be REJECTED by mcc.
# (A compile that succeeds is a regression: the invalid construct was not
# diagnosed.)
check-c23-neg: $(BIN)
	@export MEUOS_SYSROOT="$(abspath ../sysroot)"; \
	for t in test/c23/neg/*.neg.c; do \
		if ./$(BIN) -o /tmp/mcc-c23-neg-$$(basename $$t .neg.c) $$t 2>/dev/null; then \
			echo "REGRESSION: $$(basename $$t) compiled (expected rejection)"; exit 1; \
		else \
			echo "OK: $$(basename $$t) rejected"; \
		fi; \
	done

# C 功能回归 × MIR/LIR 双路径矩阵：把 c99/c11/c23 三套正向用例分别在
# MCC_USE_MIR=1（MIR 默认路径）与 MCC_USE_MIR=0（legacy 直接 LIR 路径）
# 下编译运行，要求两路径均通过且 stdout 一致。守护双路径不回归、不分化。
check-c-mir: $(BIN)
	@sh test/mir_matrix.sh ./$(BIN)

check-driver: $(BIN) $(MPP_BIN)
	sh test/driver/sysroot.sh ./$(BIN)
	sh test/driver/feature-regress.sh ./$(BIN)
	sh test/driver/cli-args.sh ./$(MPP_BIN) ./$(BIN)

# mcc-mt-integrate: verify MT_AS/MT_LD/MT_AR redirect mcc to the MeuOS toolchain
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
	@$(MAKE) -C $(MSYS_DIR)
	@work=$$(mktemp -d /tmp/mcc-sysroot-static.XXXXXX); trap 'rm -rf "$$work"' EXIT HUP INT TERM; \
	cp -a . "$$work/mcc"; \
	$(MAKE) -C "$$work/mcc" clean; \
	env -u MT_AS -u MT_LD -u MT_AR MCC_HOST_CC=cc $(MAKE) -C "$$work/mcc" HOST_CC="$(abspath $(BIN))" \
		CFLAGS="-O2 -g -std=gnu11 -Iinclude -I$(abspath ../meuos-libc/include) -I$(abspath ../meuos-sysroot/include) -I$(abspath ../meuos-toolchain/include) -Wno-all --sysroot=$(abspath ../sysroot) --nostdlib --static" \
		LDLIBS="-L$(abspath ../meuos-sysroot/build) -lmsys -ldl"; \
	"$$work/mcc/mcc" --version; \
	printf '%s\n' 'int main(void) { return 0; }' > "$$work/hello.c"; \
	MEUOS_SYSROOT="$(abspath ../sysroot)" "$$work/mcc/mcc" -o "$$work/hello" "$$work/hello.c"; \
	"$$work/hello"

# Compatibility alias for existing scripts.
check-la64: check-loongarch64

# MIR core unit tests (B.1: types/aggregates/values/instructions/consts).
# The mir/ sources are backend-side and belong to the libmcc BE_DIRS, but
# they compile standalone here so the MIR core can be validated before it
# is wired into the C frontend (see the MIR-first task line in .issues/).
MIR_SRCS := $(wildcard src/mir/*.c)
# machine.c references mtarget_x86_64.abi (mfnm_abi_x86_64) and
# mtarget_riscv64.abi (mfnm_abi_riscv64), so every standalone link of
# the mir/ sources also needs both machine ABIs.
MABI_SRC := src/target/x86_64/x86_64_mabi.c src/target/riscv64/riscv64_mabi.c src/target/loongarch64/loongarch64_mabi.c src/target/aarch64/aarch64_mabi.c
check-mir-types: $(MIR_SRCS) $(MABI_SRC)
	@mkdir -p build
	$(HOST_CC) $(CFLAGS) -Iinclude -o build/mir_test $^ test/mir/mir_test.c
	./build/mir_test

check-mir-passes: $(MIR_SRCS) $(MABI_SRC)
	@mkdir -p build
	$(HOST_CC) $(CFLAGS) -Iinclude -o build/mir_pass_test $^ test/mir/pass_test.c
	./build/mir_pass_test

# P4a register allocator: live-interval construction on machine IR.
check-mir-regalloc: $(MIR_SRCS) $(MABI_SRC)
	@mkdir -p build
	$(HOST_CC) $(CFLAGS) -Iinclude -o build/regalloc_test $^ test/mir/regalloc_test.c
	./build/regalloc_test

# P1 machine layer: physical registers as MVal, addressing modes, machine
# opcodes (MMOP), machine functions/blocks/instructions (MFnM/MBlkM/MInsM).
# Independent of the LIR/QBE pipeline — links mir/ sources only (plus the
# x86_64 machine ABI, which mtarget_x86_64.abi points at).
check-mir-machine: $(MIR_SRCS) src/target/x86_64/x86_64_mabi.c
	@mkdir -p build
	$(HOST_CC) $(CFLAGS) -Iinclude -o build/machine_test \
		test/mir/machine_test.c src/mir/build.c src/mir/mir_util.c \
		src/mir/machine.c src/mir/print.c src/target/x86_64/x86_64_mabi.c src/target/riscv64/riscv64_mabi.c src/target/loongarch64/loongarch64_mabi.c src/target/aarch64/aarch64_mabi.c
	./build/machine_test

# P2 x86-64 SysV ABI lowering on MIR-native data: eightbyte classification
# (mabi_typclass/mabi_argsclass) + selpar/selcall/selret lowering.
check-mir-abi: $(MIR_SRCS) src/target/x86_64/x86_64_mabi.c
	@mkdir -p build
	$(HOST_CC) $(CFLAGS) -Iinclude -o build/mabi_test \
		test/mir/mabi_test.c src/mir/build.c src/mir/mir_util.c \
		src/mir/machine.c src/mir/print.c src/target/x86_64/x86_64_mabi.c src/target/riscv64/riscv64_mabi.c src/target/loongarch64/loongarch64_mabi.c src/target/aarch64/aarch64_mabi.c
	./build/mabi_test

# MIR → LIR bridge end-to-end: build a fib MFn, run MIR passes, bridge to
# the LIR Fn, run the LIR pipeline and emit x86-64 asm.  Assemble+link+run
# the output to verify the emitted code actually works.
check-mir-bridge: $(BIN)
	@mkdir -p build
	$(HOST_CC) $(CFLAGS) -Iinclude -o build/bridge_test \
		test/mir/bridge_test.c src/mir/build.c src/mir/mir_util.c \
		src/mir/passes.c src/mir/print.c src/lir/bridge.c \
		build/libmcc.a -L../meuos-sysroot/build -lmsys
	@./build/bridge_test > /tmp/bridge_fib.s 2>&1
	@echo "=== 生成的 fib 汇编 ==="
	@head -40 /tmp/bridge_fib.s
	@# assemble a driver + the emitted asm, run it
	@printf 'extern int fib(int); int main(void){ return fib(5)==5 ? 0 : 1; }\n' \
		> /tmp/bridge_main.c
	@$(HOST_CC) -c -o /tmp/bridge_main.o /tmp/bridge_main.c
	@$(HOST_CC) -o /tmp/bridge_fib /tmp/bridge_main.o /tmp/bridge_fib.s
	@/tmp/bridge_fib; echo "bridge exit=$$? (0 = fib(5)==5)"

check-mir: check-mir-types check-mir-passes check-mir-machine check-mir-abi check-mir-regalloc check-mir-bridge

# m++ C++ lexer unit tests (C.1.2: keyword classification).
CPP_SRCS := $(wildcard src/cpp/lex/*.c)
check-cpp-lex: $(CPP_SRCS)
	@mkdir -p build
	$(HOST_CC) $(CFLAGS) -Iinclude -o build/cpp_lex_test $^ test/cpp/lex_test.c
	./build/cpp_lex_test

# C.2.5 virtual functions & vtables — end-to-end: m++ compiles
# test/cpp/virtual.cc against the MeuOS sysroot and the binary asserts the
# dispatch behavior (exit 0 = all checks passed).
check-cpp-virtual: $(MPP_BIN)
	@mkdir -p build
	MEUOS_SYSROOT=$(abspath ../sysroot) ./m++ --specs=meuos \
	    -o build/cpp_virtual test/cpp/virtual.cc
	./build/cpp_virtual

# m++ C++ functional tests: each test/cpp/*.cc is compiled with m++ and
# run; non-zero exit (or compile failure) fails the target.  These are
# freestanding user programs (no meuos-libc dependency) so they link with
# the host specs, keeping them runnable in any environment.  virtual.cc
# needs the meuos sysroot and is exercised by check-cpp-virtual instead.
check-cpp-func: $(MPP_BIN)
	@for t in $$(ls test/cpp/*.cc | grep -v '\.neg\.cc' | grep -v '/virtual\.cc'); do \
		out=/tmp/mpp-$$(basename $$t .cc); \
		./$(MPP_BIN) --specs=host -o $$out $$t || exit $$?; \
		$$out || exit $$?; \
		if [ "$$(basename $$t)" = "global_dtor.cc" ]; then \
			$$out | grep -q "^dtor 3$$" || { echo "FAIL: global_dtor order"; exit 1; }; \
		fi; \
	done

# m++ negative tests: test/cpp/*.neg.cc must be REJECTED by the compiler.
check-cpp-neg: $(MPP_BIN)
	@for t in test/cpp/*.neg.cc; do \
		if ./$(MPP_BIN) --specs=host -o /tmp/mpp-neg-$$(basename $$t .neg.cc) $$t 2>/dev/null; then \
			echo "FAIL: $$t should not compile"; exit 1; \
		fi; \
	done

check-cpp: check-cpp-lex check-cpp-virtual check-cpp-func check-cpp-neg

# mcc/m++ -O 优化级别语义分级回归（test/olevel/run.sh）：
# -O0..-O3/-Os/-Oz/-Og 运行时正确性 + 级别差异（if 转换/强度削减/
# 帧指针）+ 非法级别（-O9 钳制、-Ox 拒绝）。
check-olevel: $(BIN)
	@sh test/olevel/run.sh ./$(BIN)

# mcc/m++ i18n 双语消息目录回归（test/i18n/run.sh）：
# --lang=en/zh 错误文本、--error-json message、--explain 修复建议、
# --help 双语、LANG 环境推断、未收录消息降级。
check-i18n: $(BIN) $(MPP_BIN)
	@sh test/i18n/run.sh ./$(BIN) ./$(MPP_BIN)

.PHONY: all clean check check-all check-arm check-loongarch64 check-i386 check-i386-runtime check-i386-qemu check-aarch64-runtime check-targets check-pic-verify check-la64 check-c11-atomic check-c11 check-c99 check-c23 check-c23-neg check-c-mir check-driver check-mt-integration check-abi check-sysroot-static check-chibicc check-community check-mir-types check-mir-passes check-mir-machine check-mir-abi check-mir-bridge check-mir check-cpp-lex check-cpp-virtual check-cpp-func check-cpp-neg check-cpp check-olevel check-i18n

# chibicc community functional test suite (C11/C23 conformance).
# Runs test/community/chibicc/run.sh against the MeuOS sysroot.
# See test/community/chibicc/REPORT.md for results and classification.
check-chibicc: $(BIN)
	@sh test/community/chibicc/run.sh

check-community: check-c99 check-chibicc

# 一键回归验收门禁：聚合所有 check 目标（test/verify-all.sh），
# 任一检查失败即以非零状态退出。只新增本目标，不改动已有 check 目标。
check-all:
	sh test/verify-all.sh
