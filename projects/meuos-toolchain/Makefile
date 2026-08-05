# MeuOS Toolchain (mt) - x86_64-first bootstrap build.
#
# Bootstrap deliberately uses only a host C compiler and host ar to build the
# project. Once mt/ar is usable, HOST_AR can point at build/bin/ar. The source
# itself avoids host <elf.h>; the internal ELF format layer is self-contained.

HOST_CC ?= cc
HOST_AR ?= ar
MCC ?= ../mcc/mcc
SYSROOT ?= /workspace/MeuOS-Kit/sysroot
PREFIX ?= /usr
DESTDIR ?=
BUILD ?= build

CPPFLAGS += -Iinclude -I../meuos-sysroot/include
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L
CFLAGS += -MMD -MP
LDFLAGS ?=
LDLIBS ?=

LIBELF_SRCS := $(sort $(wildcard src/libelf/*.c))
LIBELF_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LIBELF_SRCS))
LIBELF_LIB := $(BUILD)/lib/libelf.a

LIBDISASM_SRCS := $(sort $(wildcard src/libdisasm/*.c))
LIBDISASM_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LIBDISASM_SRCS))
LIBDISASM_LIB := $(BUILD)/lib/libdisasm.a

TARGET_SRCS := $(sort $(wildcard src/target/*.c) $(wildcard src/target/x86_64/*.c) $(wildcard src/target/i386/*.c) $(wildcard src/target/aarch64/*.c) $(wildcard src/target/riscv64/*.c) $(wildcard src/target/loongarch64/*.c) $(wildcard src/target/arm/*.c))
TARGET_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(TARGET_SRCS))

AR_SRCS := $(sort $(wildcard src/ar/*.c))
AR_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(AR_SRCS))
AR_BIN := $(BUILD)/bin/ar

AS_SRCS := $(sort $(wildcard src/as/*.c))
AS_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(AS_SRCS))
AS_BIN := $(BUILD)/bin/as

LD_SRCS := $(sort $(wildcard src/ld/*.c))
LD_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LD_SRCS))
LD_BIN := $(BUILD)/bin/ld
AR_CORE_OBJ := $(BUILD)/src/ar/archive.o
RANLIB_OBJS := $(BUILD)/src/ranlib/main.o $(AR_CORE_OBJ)
RANLIB_BIN := $(BUILD)/bin/ranlib

NM_SRCS := $(sort $(wildcard src/nm/*.c))
NM_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(NM_SRCS))
NM_BIN := $(BUILD)/bin/nm

READELF_SRCS := $(sort $(wildcard src/readelf/*.c))
READELF_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(READELF_SRCS))
READELF_BIN := $(BUILD)/bin/readelf

STRIP_SRCS := $(sort $(wildcard src/strip/*.c))
STRIP_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(STRIP_SRCS))
STRIP_BIN := $(BUILD)/bin/strip

OBJCOPY_SRCS := $(sort $(wildcard src/objcopy/*.c))
OBJCOPY_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(OBJCOPY_SRCS))
OBJCOPY_BIN := $(BUILD)/bin/objcopy

OBJDUMP_SRCS := $(sort $(wildcard src/objdump/*.c))
OBJDUMP_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(OBJDUMP_SRCS))
OBJDUMP_BIN := $(BUILD)/bin/objdump

MTINFO_SRCS := $(sort $(wildcard src/mt-info/*.c))
MTINFO_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(MTINFO_SRCS))
MTINFO_BIN := $(BUILD)/bin/mt-info

RTLD_SRCS := $(sort $(wildcard src/rtld/*.c))
RTLD_ASM_SRCS := $(sort $(wildcard src/rtld/x86_64/*.S))
RTLD_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(RTLD_SRCS)) $(patsubst %.S,$(BUILD)/%.o,$(RTLD_ASM_SRCS))
RTLD_BIN := $(BUILD)/bin/ld.so

ELF_PROBE := $(BUILD)/test/elf_probe
TEST_FIXTURE_OBJ := $(BUILD)/test/hello.o
TEST_ARCHIVE := $(BUILD)/test/libhello.a
TEST_EXEC := $(BUILD)/test/hello.exec
DEPS := $(LIBELF_OBJS:.o=.d) $(LIBDISASM_OBJS:.o=.d) $(AR_OBJS:.o=.d) $(AS_OBJS:.o=.d) $(LD_OBJS:.o=.d) $(RANLIB_OBJS:.o=.d) $(NM_OBJS:.o=.d) $(READELF_OBJS:.o=.d) $(STRIP_OBJS:.o=.d) $(OBJCOPY_OBJS:.o=.d) $(OBJDUMP_OBJS:.o=.d) $(MTINFO_OBJS:.o=.d) $(TARGET_OBJS:.o=.d) $(RTLD_OBJS:.o=.d) $(ELF_PROBE).d

all: $(AR_BIN) $(AS_BIN) $(LD_BIN) $(RANLIB_BIN) $(NM_BIN) $(READELF_BIN) $(STRIP_BIN) $(OBJCOPY_BIN) $(OBJDUMP_BIN) $(MTINFO_BIN) $(RTLD_BIN)

$(LIBELF_LIB): $(LIBELF_OBJS)
	@mkdir -p $(dir $@)
	$(HOST_AR) rcs $@ $^

$(LIBDISASM_LIB): $(LIBDISASM_OBJS)
	@mkdir -p $(dir $@)
	$(HOST_AR) rcs $@ $^

$(AR_BIN): $(AR_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(AR_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(AS_BIN): $(AS_OBJS) $(TARGET_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(AS_OBJS) $(TARGET_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(LD_BIN): $(LD_OBJS) $(AR_CORE_OBJ) $(TARGET_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(LD_OBJS) $(AR_CORE_OBJ) $(TARGET_OBJS) $(LIBELF_LIB) -L../meuos-sysroot/build -lmsys $(LDLIBS)

$(RANLIB_BIN): $(RANLIB_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(RANLIB_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(NM_BIN): $(NM_OBJS) $(BUILD)/src/ar/archive.o $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(NM_OBJS) $(BUILD)/src/ar/archive.o $(LIBELF_LIB) $(LDLIBS)

$(READELF_BIN): $(READELF_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(READELF_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(STRIP_BIN): $(STRIP_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(STRIP_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(OBJCOPY_BIN): $(OBJCOPY_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(OBJCOPY_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(OBJDUMP_BIN): $(OBJDUMP_OBJS) $(LIBELF_LIB) $(LIBDISASM_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(OBJDUMP_OBJS) $(LIBELF_LIB) $(LIBDISASM_LIB) $(LDLIBS)

$(MTINFO_BIN): $(MTINFO_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(MTINFO_OBJS) $(LIBELF_LIB) $(LDLIBS)

# ld.so: dynamic linker — no libc, static build with custom entry
RTLD_CFLAGS := -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -ffreestanding -fno-builtin -D_POSIX_C_SOURCE=200809L -MMD -MP
$(BUILD)/src/rtld/%.o: CPPFLAGS += -Iinclude -I../meuos-sysroot/include
$(BUILD)/src/rtld/%.o: CFLAGS := $(RTLD_CFLAGS)

# .S -> .o (assembly for arch-specific startup)
$(BUILD)/src/rtld/x86_64/dlstart.o: src/rtld/x86_64/dlstart.S
	@mkdir -p $(dir $@)
	$(HOST_CC) -c -o $@ $<

$(RTLD_BIN): $(RTLD_OBJS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -nostdlib -static -fno-pie -no-pie -e _dlstart $(LDFLAGS) -o $@ $(RTLD_OBJS) $(LDLIBS)

$(ELF_PROBE): test/elf_probe.c $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIBELF_LIB) $(LDFLAGS) $(LDLIBS)

$(TEST_FIXTURE_OBJ): test/fixtures/hello.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(TEST_EXEC): test/fixtures/exec_main.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(TEST_ARCHIVE): $(AR_BIN) $(TEST_FIXTURE_OBJ)
	@mkdir -p $(dir $@)
	$(AR_BIN) rcs $@ $(TEST_FIXTURE_OBJ)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

-include $(DEPS)

check: check-as-x86_64 check-as-tls-x86_64 check-as-sse-x86_64 check-as-libc-x86_64 check-as-arm-neg check-ld-x86_64 check-ld-cref check-ar-bsd check-ld-libpath check-ranlib check-libelf check-nm check-readelf check-strip check-objcopy check-objdump check-rtld $(AR_BIN) $(AS_BIN) $(LD_BIN) $(RANLIB_BIN) $(NM_BIN) $(READELF_BIN) $(STRIP_BIN) $(OBJCOPY_BIN) $(OBJDUMP_BIN) $(ELF_PROBE) $(TEST_ARCHIVE)
	@set -eu; \
	test "$$($(AR_BIN) --version)" = "meuos-toolchain ar 0.2.0 (x86_64 bootstrap)"; \
	test "$$($(AR_BIN) t $(TEST_ARCHIVE))" = hello.o; \
	$(AR_BIN) s $(TEST_ARCHIVE); \
	$(ELF_PROBE) $(TEST_FIXTURE_OBJ) | grep '^type=1 machine=x86_64 sections=.* symbols=1$$'; \
	tmp=$$(mktemp -d /tmp/meuos-toolchain-check.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	printf 'meuos-toolchain\n' > "$$tmp/input.txt"; \
	$(AR_BIN) rcs "$$tmp/libtext.a" "$$tmp/input.txt"; \
	test "$$($(AR_BIN) t "$$tmp/libtext.a")" = input.txt; \
	mkdir "$$tmp/extract"; \
	( cd "$$tmp/extract" && "$(abspath $(AR_BIN))" x "$$tmp/libtext.a" ); \
	cmp "$$tmp/input.txt" "$$tmp/extract/input.txt"; \
	printf '%s\n' 'int toolchain_fixture_value(void);' 'int main(void) { return toolchain_fixture_value() == 42 ? 0 : 1; }' > "$$tmp/main.c"; \
	$(HOST_CC) -c "$$tmp/main.c" -o "$$tmp/main.o"; \
	$(HOST_CC) "$$tmp/main.o" "$(TEST_ARCHIVE)" -o "$$tmp/main"; \
	"$$tmp/main"; \
	printf alpha > "$$tmp/this_is_a_long_member_name.o"; \
	printf beta > "$$tmp/second_long_member_name.o"; \
	$(AR_BIN) rcs "$$tmp/long.a" "$$tmp/this_is_a_long_member_name.o"; \
	$(AR_BIN) q "$$tmp/long.a" "$$tmp/second_long_member_name.o"; \
	test "$$($(AR_BIN) t "$$tmp/long.a" | tr '\n' ' ')" = 'this_is_a_long_member_name.o second_long_member_name.o '; \
	printf gamma > "$$tmp/this_is_a_long_member_name.o"; \
	$(AR_BIN) r "$$tmp/long.a" "$$tmp/this_is_a_long_member_name.o"; \
	mkdir "$$tmp/long-extract"; \
	( cd "$$tmp/long-extract" && "$(abspath $(AR_BIN))" x "$$tmp/long.a" ); \
	test "$$(cat "$$tmp/long-extract/this_is_a_long_member_name.o")" = gamma; \
	test "$$(cat "$$tmp/long-extract/second_long_member_name.o")" = beta; \
	printf 'mt check: PASS\n'

check-as-x86_64: $(AS_BIN)
	@set -eu; \
	tmp=$$(mktemp -d /tmp/meuos-toolchain-as-check.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	printf '%s\n' '.text' '.globl main' 'main:' 'endbr64' 'movl $$0, %eax' 'ret' > "$$tmp/main.s"; \
	$(AS_BIN) -o "$$tmp/main.o" "$$tmp/main.s"; \
	LANG=C readelf -h "$$tmp/main.o" | grep -Eq 'Type:[[:space:]]+REL'; \
	LANG=C readelf -s "$$tmp/main.o" | grep -Eq '[[:space:]]main$$'; \
	printf 'mt as x86_64 check: PASS\n'
	sh test/as_rept.sh "$(abspath $(AS_BIN))"
	sh test/as_check_equ.sh "$(abspath $(AS_BIN))"

check-as-tls-x86_64: $(AS_BIN) $(LD_BIN) $(READELF_BIN)
	sh test/as_tls_x86_64.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(READELF_BIN))"

check-target-dispatch: $(AS_BIN) $(LD_BIN)
	@set -eu; \
	tmp=$$(mktemp -d /tmp/meuos-toolchain-dispatch.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	printf '%s\n' '.text' '.globl main' 'main:' 'ret' > "$$tmp/min.s"; \
	$(AS_BIN) --target=x86_64 -o "$$tmp/main.o" "$$tmp/min.s"; \
	printf '%s' '--target=x86_64 as: '; LANG=C readelf -h "$$tmp/main.o" | grep 'Machine:'; \
	$(AS_BIN) --target=aarch64 -o "$$tmp/main.o" "$$tmp/min.s"; \
	printf '%s' '--target=aarch64 as: '; LANG=C readelf -h "$$tmp/main.o" | grep 'Machine:'; \
	$(AS_BIN) --target=i386 -o "$$tmp/main.o" "$$tmp/min.s"; \
	printf '%s' '--target=i386 as: '; LANG=C readelf -h "$$tmp/main.o" | grep 'Machine:'; \
	$(AS_BIN) --target=riscv64 -o "$$tmp/main.o" "$$tmp/min.s"; \
	printf '%s' '--target=riscv64 as: '; LANG=C readelf -h "$$tmp/main.o" | grep 'Machine:'; \
	$(AS_BIN) --target=loongarch64 -o "$$tmp/main.o" "$$tmp/min.s"; \
	printf '%s' '--target=loongarch64 as: '; LANG=C readelf -h "$$tmp/main.o" | grep 'Machine:'; \
	$(AS_BIN) --target=arm "$$tmp/min.s" 2>&1 && exit 1 || printf 'unsupported target rejected: PASS\n'; \
	$(LD_BIN) --target=x86_64 --version | grep -q 'x86_64' && printf 'ld --target=x86_64 --version: PASS\n'; \
	$(LD_BIN) --target=i386 --version | grep -q 'i386' && printf 'ld --target=i386 --version: PASS\n'; \
	printf 'check-target-dispatch: PASS\n'

# aarch64 e2e: 最小汇编端到端（无需 CRT/libc）
QEMU_AARCH64 ?= ../../env/qemu/qemu-aarch64
check-aarch64-e2e: $(AS_BIN) $(LD_BIN)
	sh test/aarch64_e2e.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(QEMU_AARCH64))"

# riscv64 e2e: 最小汇编端到端
QEMU_RISCV64 ?= ../../env/qemu/qemu-riscv64
check-riscv64-e2e: $(AS_BIN) $(LD_BIN)
	sh test/riscv64_e2e.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(QEMU_RISCV64))"

# loongarch64 e2e: 最小汇编端到端
QEMU_LOONGARCH64 ?= ../../env/qemu/qemu-loongarch64
check-loongarch64-e2e: $(AS_BIN) $(LD_BIN)
	sh test/loongarch64_e2e.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(QEMU_LOONGARCH64))"

# i386 e2e: as 汇编验证（ld 待 ELF32 输入支持 P0-1a）
check-i386-e2e: $(AS_BIN) $(LD_BIN)
	sh test/i386_e2e.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))"

# -------------------------------------------------------------------
# QEMU 集成测试：mcc → as → ld → qemu-user 全自动管线验证
# 测试 C 语言程序在交叉架构上的编译、汇编、链接和运行。
# 如果链接或 QEMU 运行失败，输出 ⚠️ 但整体不阻塞。
# -------------------------------------------------------------------
QEMU_C_HELLO := test/qemu_c_hello.sh

# check-qemu-* : per-arch QEMU C hello pipeline.  True environment SKIP
# only when the user-mode qemu or per-arch sysroot is absent; any pipeline
# failure (mcc -S / as / ld / wrong ELF arch / wrong exit) propagates a
# nonzero rc so `make check-qemu-all` fails loudly instead of masking it.
define RUN_QEMU_ARCH
	@arch=$(1); qemu="$(abspath ../../env/qemu/qemu-$$arch-static)"; \
	sysroot="$(abspath ../../sysroot/$$arch)"; \
	if [ ! -x "$$qemu" ]; then echo "check-qemu-$$arch: SKIP (no QEMU)"; exit 0; fi; \
	if [ ! -d "$$sysroot" ]; then echo "check-qemu-$$arch: SKIP (no sysroot)"; exit 0; fi; \
	sh $(QEMU_C_HELLO) "$(abspath $(MCC))" "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" \
		"$$qemu" "$$sysroot" "$$arch"
endef

check-qemu-aarch64: $(AS_BIN) $(LD_BIN)
	$(call RUN_QEMU_ARCH,aarch64)
	@echo "check-qemu-aarch64: PASS"

check-qemu-riscv64: $(AS_BIN) $(LD_BIN)
	$(call RUN_QEMU_ARCH,riscv64)
	@echo "check-qemu-riscv64: PASS"

check-qemu-loongarch64: $(AS_BIN) $(LD_BIN)
	$(call RUN_QEMU_ARCH,loongarch64)
	@echo "check-qemu-loongarch64: PASS"

check-qemu-arm: $(AS_BIN) $(LD_BIN)
	$(call RUN_QEMU_ARCH,arm)
	@echo "check-qemu-arm: PASS"

check-qemu-all: check-qemu-x86_64 check-qemu-i386 check-qemu-aarch64 check-qemu-riscv64 check-qemu-loongarch64 check-qemu-arm
	@echo "check-qemu-all: DONE"

check-qemu-x86_64: $(AS_BIN) $(LD_BIN)
	$(call RUN_QEMU_ARCH,x86_64)
	@echo "check-qemu-x86_64: PASS"

check-qemu-i386: $(AS_BIN) $(LD_BIN)
	$(call RUN_QEMU_ARCH,i386)
	@echo "check-qemu-i386: PASS"

check-as-sse-x86_64: $(AS_BIN)
	sh test/as_sse_x86_64.sh "$(abspath $(AS_BIN))"

check-as-libc-x86_64: $(AS_BIN)
	sh test/as_libc_x86_64.sh "$(abspath $(AS_BIN))"

check-as-arm-neg: $(AS_BIN)
	sh test/as_arm_neg.sh "$(abspath $(AS_BIN))"

check-ld-x86_64: $(AS_BIN) $(AR_BIN) $(LD_BIN) $(MCC)
	sh test/ld_smoke.sh "$(abspath $(AS_BIN))" "$(abspath $(AR_BIN))" "$(abspath $(LD_BIN))"
	sh test/ld_sysroot.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))"
	sh test/ld_counter_e2e.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(MCC))" "$(SYSROOT)"
	sh test/ld_defsym.sh "$(abspath $(LD_BIN))"
	sh test/ld_wrap.sh "$(abspath $(LD_BIN))"

check-ld-cref: $(AS_BIN) $(LD_BIN)
	sh test/ld_cref.sh "$(abspath $(LD_BIN))" "$(abspath $(AS_BIN))"


check-ar-bsd: $(AS_BIN) $(AR_BIN) $(LD_BIN)
	sh test/ar_bsd_format.sh "$(abspath $(AS_BIN))" "$(abspath $(AR_BIN))" "$(abspath $(LD_BIN))"

check-ld-libpath: $(AS_BIN) $(LD_BIN) $(MCC)
	sh test/ld_libpath.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(MCC))" "$(SYSROOT)"

check-rtld: $(AS_BIN) $(LD_BIN) $(RTLD_BIN)
	sh test/rtld_e2e.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(RTLD_BIN))"

check-rtld-dtv: $(RTLD_BIN)
	sh test/rtld_dtv.sh "$(abspath ../..)"

check-ranlib: $(AR_BIN) $(RANLIB_BIN)
	sh test/ranlib_basic.sh "$(abspath $(AR_BIN))" "$(abspath $(RANLIB_BIN))"

check-elf: $(ELF_PROBE) $(TEST_FIXTURE_OBJ)
	$(ELF_PROBE) $(TEST_FIXTURE_OBJ)

check-ar: $(AR_BIN) $(TEST_ARCHIVE)
	test "$$($(AR_BIN) t $(TEST_ARCHIVE))" = hello.o

check-libelf: $(ELF_PROBE) $(AS_BIN) $(LD_BIN) $(AR_BIN)
	sh test/libelf_roundtrip.sh "$(abspath $(ELF_PROBE))" "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(AR_BIN))"

check-nm: $(NM_BIN) $(TEST_FIXTURE_OBJ)
	sh test/nm_basic.sh "$(abspath $(NM_BIN))" "$(abspath $(TEST_FIXTURE_OBJ))"

check-readelf: $(READELF_BIN) $(TEST_FIXTURE_OBJ) $(TEST_EXEC)
	sh test/readelf_basic.sh "$(abspath $(READELF_BIN))" "$(abspath $(TEST_FIXTURE_OBJ))" "$(abspath $(TEST_EXEC))"

check-strip: $(STRIP_BIN) $(TEST_FIXTURE_OBJ) $(TEST_EXEC)
	sh test/strip_basic.sh "$(abspath $(STRIP_BIN))" "$(abspath $(TEST_FIXTURE_OBJ))" "$(abspath $(TEST_EXEC))"

check-objcopy: $(OBJCOPY_BIN) $(TEST_FIXTURE_OBJ) $(TEST_EXEC)
	sh test/objcopy_basic.sh "$(abspath $(OBJCOPY_BIN))" "$(abspath $(TEST_FIXTURE_OBJ))" "$(abspath $(TEST_EXEC))"

check-objdump: $(OBJDUMP_BIN) $(TEST_EXEC)
	sh test/objdump_basic.sh "$(abspath $(OBJDUMP_BIN))" "$(abspath $(TEST_EXEC))"

install: $(AR_BIN) $(AS_BIN) $(LD_BIN) $(RANLIB_BIN) $(NM_BIN) $(READELF_BIN) $(STRIP_BIN) $(OBJCOPY_BIN) $(OBJDUMP_BIN) $(RTLD_BIN)
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(AR_BIN) $(DESTDIR)$(PREFIX)/bin/ar
	cp $(AS_BIN) $(DESTDIR)$(PREFIX)/bin/as
	cp $(LD_BIN) $(DESTDIR)$(PREFIX)/bin/ld
	cp $(RANLIB_BIN) $(DESTDIR)$(PREFIX)/bin/ranlib
	cp $(NM_BIN) $(DESTDIR)$(PREFIX)/bin/nm
	cp $(READELF_BIN) $(DESTDIR)$(PREFIX)/bin/readelf
	cp $(STRIP_BIN) $(DESTDIR)$(PREFIX)/bin/strip
	cp $(OBJCOPY_BIN) $(DESTDIR)$(PREFIX)/bin/objcopy
	cp $(OBJDUMP_BIN) $(DESTDIR)$(PREFIX)/bin/objdump
	@mkdir -p $(DESTDIR)$(PREFIX)/lib
	cp $(RTLD_BIN) $(DESTDIR)$(PREFIX)/lib/ld-meuos.so.1

clean:
	rm -rf $(BUILD)

.PHONY: all check check-elf check-ar check-as-x86_64 check-as-tls-x86_64 check-target-dispatch check-as-sse-x86_64 check-as-libc-x86_64 check-as-arm-neg check-ld-x86_64 check-ar-bsd check-ld-libpath check-rtld check-rtld-dtv check-ranlib check-libelf check-nm check-readelf check-strip check-objcopy check-objdump check-qemu-x86_64 check-qemu-i386 check-qemu-aarch64 check-qemu-riscv64 check-qemu-loongarch64 check-qemu-arm check-qemu-all install clean
