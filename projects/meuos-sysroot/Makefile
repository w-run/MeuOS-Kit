# meuos-sysroot — .msys single-file sysroot tooling
#
# Layout:
#   include/mt/msys.h     libmsys public API
#   src/libmsys/msys.c    .msys reader (libmsys.a)
#   src/mkmsys/main.c     .msys packer (mkmsys binary)
#   build/                build artifacts (gitignored)

HOST_CC ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror
BUILD   ?= build

MSYS_SRCS := src/libmsys/msys.c
MSYS_OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(MSYS_SRCS))
MSYS_LIB  := $(BUILD)/libmsys.a

MKMSYS_SRCS := src/mkmsys/main.c
MKMSYS_OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(MKMSYS_SRCS))
MKMSYS_BIN  := $(BUILD)/mkmsys

TEST_SRCS := test/msys_test.c
TEST_BIN  := $(BUILD)/test-msys

.PHONY: all check clean

all: $(MSYS_LIB) $(MKMSYS_BIN)

# libmsys.a — shared .msys reader (consumed by mcc, mt/ld)
$(MSYS_LIB): $(MSYS_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BUILD)/%.o: src/%.c include/mt/msys.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -Iinclude -c -o $@ $<

# mkmsys — .msys packer (standalone binary)
$(MKMSYS_BIN): $(MKMSYS_OBJS) $(MSYS_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -o $@ $(MKMSYS_OBJS) $(MSYS_LIB)

$(BUILD)/mkmsys/main.o: src/mkmsys/main.c include/mt/msys.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -Iinclude -c -o $@ $<

# test
$(TEST_BIN): $(TEST_SRCS) $(MSYS_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -Iinclude -o $@ $< $(MSYS_LIB)

check: $(MKMSYS_BIN) $(TEST_BIN)
	@# 1) Build a .msys from a real sysroot (if available)
	@for d in ../sysroot ../sysroot-aarch64 ../sysroot-riscv64; do \
		if [ -d "$$d" ] && [ -f "$$d/usr/lib/libc-meuos.a" ]; then \
			arch=$$(basename $$d | sed 's/sysroot-//'); \
			[ "$$arch" = "sysroot" ] && arch=x86_64; \
			$(MKMSYS_BIN) -o /tmp/$$arch.msys $$d; \
			$(MKMSYS_BIN) --list /tmp/$$arch.msys | head -5; \
			echo "  $$arch.msys: $$(wc -c < /tmp/$$arch.msys) bytes"; \
		fi; \
	done
	@# 2) Unit test
	$(TEST_BIN)
	@echo "meuos-sysroot: check PASS"

clean:
	rm -rf $(BUILD)
