# meuos-sysroot — .msys single-file sysroot tooling
#
# Layout:
#   include/mt/msys.h     libmsys public API
#   src/libmsys/msys.c    .msys reader (libmsys.a)
#   src/mkmsys/main.c     .msys packer (mkmsys binary)
#   build/                build artifacts (gitignored)

HOST_CC ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L
BUILD   ?= build
OBJDIR  ?= $(BUILD)/obj

MSYS_SRCS := src/libmsys/msys.c src/libmsys/sha256.c src/libmsys/overlay.c src/libmsys/ed25519.c
MSYS_OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(MSYS_SRCS))
MSYS_LIB  := $(BUILD)/libmsys.a

MKMSYS_SRCS := src/mkmsys/main.c
MKMSYS_OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(MKMSYS_SRCS))
MKMSYS_BIN  := $(BUILD)/mkmsys

TEST_SRCS := test/msys_test.c
TEST_BIN  := $(BUILD)/test-msys

.PHONY: all check clean msys msys-compressed check-msys

all: $(MSYS_LIB) $(MKMSYS_BIN)

# libmsys.a — shared .msys reader (consumed by mcc, mt/ld)
$(MSYS_LIB): $(MSYS_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(OBJDIR)/%.o: src/%.c include/mt/msys.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -Iinclude -c -o $@ $<

# mkmsys — .msys packer (standalone binary)
$(MKMSYS_BIN): $(MKMSYS_OBJS) $(MSYS_LIB)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -o $@ $(MKMSYS_OBJS) $(MSYS_LIB) -ldl

$(OBJDIR)/mkmsys/main.o: src/mkmsys/main.c include/mt/msys.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -Iinclude -Isrc/libmsys -c -o $@ $<

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
	@# 2) check-msys: validate generated .msys
	$(MAKE) check-msys
	@# 3) Unit test
	$(TEST_BIN)
	@echo "meuos-sysroot: check PASS"

# msys — Build .msys from sysroot directory
MSYS_SYSROOT ?= $(MEUOS_SYSROOT)
MSYS_OUTPUT  ?= $(MSYS_SYSROOT).msys

msys: $(MKMSYS_BIN)
	@if [ -z "$(MSYS_SYSROOT)" ]; then \
		echo "MEUOS_SYSROOT not set — use MSYS_SYSROOT=<dir>"; exit 1; fi
	@echo "Packing $(MSYS_SYSROOT) -> $(MSYS_OUTPUT)"
	$(MKMSYS_BIN) -o "$(MSYS_OUTPUT)" "$(MSYS_SYSROOT)"

# msys-compressed — Build compressed .msys from sysroot directory
msys-compressed: $(MKMSYS_BIN)
	@if [ -z "$(MSYS_SYSROOT)" ]; then \
		echo "MEUOS_SYSROOT not set — use MSYS_SYSROOT=<dir>"; exit 1; fi
	@echo "Packing $(MSYS_SYSROOT) -> $(MSYS_OUTPUT) (zlib)"
	$(MKMSYS_BIN) --compress=zlib -o "$(MSYS_OUTPUT)" "$(MSYS_SYSROOT)"

# check-msys — Validate .msys is readable (detect existing .msys files)
check-msys: $(MKMSYS_BIN)
	@ok=0; \
	for f in /tmp/*.msys; do \
		if [ -f "$$f" ]; then \
			echo "Checking $$f ..."; \
			$(MKMSYS_BIN) --list "$$f" > /dev/null 2>&1; \
			if [ $$? -eq 0 ]; then \
				echo "  OK: $$(wc -c < "$$f") bytes"; \
				ok=1; \
			else \
				echo "  FAIL: $$f"; exit 1; \
			fi; \
		fi; \
	done; \
	if [ "$$ok" -eq 0 ]; then \
		echo "check-msys: no .msys files found — build one first with 'make msys'"; \
	fi

clean:
	rm -rf $(BUILD)
