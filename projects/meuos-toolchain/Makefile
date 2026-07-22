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

CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror
CFLAGS += -MMD -MP
LDFLAGS ?=
LDLIBS ?=

LIBELF_SRCS := $(sort $(wildcard src/libelf/*.c))
LIBELF_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LIBELF_SRCS))
LIBELF_LIB := $(BUILD)/lib/libelf.a

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

ELF_PROBE := $(BUILD)/test/elf_probe
TEST_FIXTURE_OBJ := $(BUILD)/test/hello.o
TEST_ARCHIVE := $(BUILD)/test/libhello.a
DEPS := $(LIBELF_OBJS:.o=.d) $(AR_OBJS:.o=.d) $(AS_OBJS:.o=.d) $(LD_OBJS:.o=.d) $(RANLIB_OBJS:.o=.d) $(ELF_PROBE).d

all: $(AR_BIN) $(AS_BIN) $(LD_BIN) $(RANLIB_BIN)

$(LIBELF_LIB): $(LIBELF_OBJS)
	@mkdir -p $(dir $@)
	$(HOST_AR) rcs $@ $^

$(AR_BIN): $(AR_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(AR_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(AS_BIN): $(AS_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(AS_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(LD_BIN): $(LD_OBJS) $(AR_CORE_OBJ) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(LD_OBJS) $(AR_CORE_OBJ) $(LIBELF_LIB) $(LDLIBS)

$(RANLIB_BIN): $(RANLIB_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(RANLIB_OBJS) $(LIBELF_LIB) $(LDLIBS)

$(ELF_PROBE): test/elf_probe.c $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIBELF_LIB) $(LDFLAGS) $(LDLIBS)

$(TEST_FIXTURE_OBJ): test/fixtures/hello.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(TEST_ARCHIVE): $(AR_BIN) $(TEST_FIXTURE_OBJ)
	@mkdir -p $(dir $@)
	$(AR_BIN) rcs $@ $(TEST_FIXTURE_OBJ)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

-include $(DEPS)

check: check-as-x86_64 check-as-sse-x86_64 check-as-libc-x86_64 check-ld-x86_64 check-ar-bsd check-ld-libpath check-ranlib check-libelf $(AR_BIN) $(AS_BIN) $(LD_BIN) $(RANLIB_BIN) $(ELF_PROBE) $(TEST_ARCHIVE)
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
	readelf -h "$$tmp/main.o" | grep -Eq 'Type:[[:space:]]+REL'; \
	readelf -s "$$tmp/main.o" | grep -Eq '[[:space:]]main$$'; \
	printf 'mt as x86_64 check: PASS\n'


check-as-sse-x86_64: $(AS_BIN)
	sh test/as_sse_x86_64.sh "$(abspath $(AS_BIN))"

check-ld-x86_64: $(AS_BIN) $(AR_BIN) $(LD_BIN)
	sh test/ld_smoke.sh "$(abspath $(AS_BIN))" "$(abspath $(AR_BIN))" "$(abspath $(LD_BIN))"
	sh test/ld_sysroot.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))"
	sh test/ld_counter_e2e.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(MCC))" "$(SYSROOT)"


check-ar-bsd: $(AS_BIN) $(AR_BIN) $(LD_BIN)
	sh test/ar_bsd_format.sh "$(abspath $(AS_BIN))" "$(abspath $(AR_BIN))" "$(abspath $(LD_BIN))"

check-ld-libpath: $(AS_BIN) $(LD_BIN) $(MCC)
	sh test/ld_libpath.sh "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(MCC))" "$(SYSROOT)"

check-ranlib: $(AR_BIN) $(RANLIB_BIN)
	sh test/ranlib_basic.sh "$(abspath $(AR_BIN))" "$(abspath $(RANLIB_BIN))"

check-elf: $(ELF_PROBE) $(TEST_FIXTURE_OBJ)
	$(ELF_PROBE) $(TEST_FIXTURE_OBJ)

check-ar: $(AR_BIN) $(TEST_ARCHIVE)
	test "$$($(AR_BIN) t $(TEST_ARCHIVE))" = hello.o

check-libelf: $(ELF_PROBE) $(AS_BIN) $(LD_BIN) $(AR_BIN)
	sh test/libelf_roundtrip.sh "$(abspath $(ELF_PROBE))" "$(abspath $(AS_BIN))" "$(abspath $(LD_BIN))" "$(abspath $(AR_BIN))"

install: $(AR_BIN) $(AS_BIN) $(LD_BIN) $(RANLIB_BIN)
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(AR_BIN) $(DESTDIR)$(PREFIX)/bin/ar
	cp $(AS_BIN) $(DESTDIR)$(PREFIX)/bin/as
	cp $(LD_BIN) $(DESTDIR)$(PREFIX)/bin/ld

clean:
	rm -rf $(BUILD)

.PHONY: all check check-elf check-ar check-as-x86_64 check-as-sse-x86_64 check-as-libc-x86_64 check-ld-x86_64 check-ar-bsd check-ld-libpath check-ranlib check-libelf install clean
