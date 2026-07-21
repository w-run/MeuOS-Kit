# MeuOS Toolchain (mt) - x86_64-first bootstrap build.
#
# Bootstrap deliberately uses only a host C compiler and host ar to build the
# project. Once mt/ar is usable, HOST_AR can point at build/bin/ar. The source
# itself avoids host <elf.h>; the internal ELF format layer is self-contained.

HOST_CC ?= cc
HOST_AR ?= ar
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
LIBELF_LIB := $(BUILD)/lib/libmtelf.a

AR_SRCS := $(sort $(wildcard src/ar/*.c))
AR_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(AR_SRCS))
AR_BIN := $(BUILD)/bin/ar

ELF_PROBE := $(BUILD)/test/elf_probe
TEST_FIXTURE_OBJ := $(BUILD)/test/hello.o
TEST_ARCHIVE := $(BUILD)/test/libhello.a
DEPS := $(LIBELF_OBJS:.o=.d) $(AR_OBJS:.o=.d) $(ELF_PROBE).d

all: $(AR_BIN)

$(LIBELF_LIB): $(LIBELF_OBJS)
	@mkdir -p $(dir $@)
	$(HOST_AR) rcs $@ $^

$(AR_BIN): $(AR_OBJS) $(LIBELF_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(LDFLAGS) -o $@ $(AR_OBJS) $(LIBELF_LIB) $(LDLIBS)

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

check: $(AR_BIN) $(ELF_PROBE) $(TEST_ARCHIVE)
	@set -eu; \
	test "$$($(AR_BIN) --version)" = "meuos-toolchain ar 0.1.0 (x86_64 bootstrap)"; \
	test "$$($(AR_BIN) t $(TEST_ARCHIVE))" = hello.o; \
	$(ELF_PROBE) $(TEST_FIXTURE_OBJ) | grep '^type=1 machine=x86_64 sections='; \
	tmp=$$(mktemp -d /tmp/meuos-toolchain-check.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	printf 'meuos-toolchain\n' > "$$tmp/input.txt"; \
	$(AR_BIN) rcs "$$tmp/libtext.a" "$$tmp/input.txt"; \
	test "$$($(AR_BIN) t "$$tmp/libtext.a")" = input.txt; \
	mkdir "$$tmp/extract"; \
	( cd "$$tmp/extract" && "$(abspath $(AR_BIN))" x "$$tmp/libtext.a" ); \
	cmp "$$tmp/input.txt" "$$tmp/extract/input.txt"; \
	printf 'mt check: PASS\n'

check-elf: $(ELF_PROBE) $(TEST_FIXTURE_OBJ)
	$(ELF_PROBE) $(TEST_FIXTURE_OBJ)

check-ar: $(AR_BIN) $(TEST_ARCHIVE)
	test "$$($(AR_BIN) t $(TEST_ARCHIVE))" = hello.o

install: $(AR_BIN)
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(AR_BIN) $(DESTDIR)$(PREFIX)/bin/ar

clean:
	rm -rf $(BUILD)

.PHONY: all check check-elf check-ar install clean
