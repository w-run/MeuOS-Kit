# meow - MeuOS build system
#
# Originally a single meow.c; now split by function under src/:
#   meow.h       shared declarations & global state
#   state.c      global state definitions
#   exec.c       run() command executor + list_packages()
#   recipe.c     load_recipe() + root-level include + text helpers
#   parse.c      parse_recipe() + target-table/list helpers
#   graph.c      run_target() + out-of-date + command expansion
#   main.c       main() entry point, arg dispatch, Makefile/bootstrap compat
#
# Two compile paths are preserved:
#   * host CC (default for `make check`): -D_GNU_SOURCE for environ.
#   * mcc + MeuOS sysroot: --specs=meuos --nostdlib, link libc-meuos.a.

ifeq ($(origin CC),default)
CC := ../mcc/mcc
endif
SYSROOT ?= ../sysroot
BUILD ?= build

SRCS := $(sort $(wildcard src/*.c))
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

# color.c and env.c are picked up automatically via the wildcard above.

ifeq ($(CC),../mcc/mcc)
  MEOW_CFLAGS := --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib -I../meuos-sysroot/include
  MEOW_LDLIBS := -l:libc-meuos.a -L$(abspath ../meuos-sysroot/build) -lmsys
else
  MEOW_CFLAGS := -D_GNU_SOURCE -I../meuos-sysroot/include
  MEOW_LDLIBS := -L$(abspath ../meuos-sysroot/build) -lmsys
endif

# libmsys.a — .msys single-file sysroot reader (dependency for msys/check-msys)
MSYS_LIB := $(abspath ../meuos-sysroot/build/libmsys.a)
MSYS_DIR := $(abspath ../meuos-sysroot)

$(MSYS_LIB):
	$(MAKE) -C $(MSYS_DIR)

all: $(BUILD)/meow

$(BUILD)/meow: $(OBJS) $(MSYS_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(MEOW_CFLAGS) -o $@ $^ $(MEOW_LDLIBS)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(MEOW_CFLAGS) -c -o $@ $<

check: $(BUILD)/meow
	cd .. && ./meow/$(BUILD)/meow build meow-smoke
	cd .. && ./meow/$(BUILD)/meow clean meow-smoke
	cd .. && ./meow/$(BUILD)/meow build meow-env
	cd .. && ./meow/$(BUILD)/meow clean meow-env
	@rm -f /tmp/meow-incremental.input /tmp/meow-incremental.output /tmp/meow-incremental.log
	@printf '%s\n' first > /tmp/meow-incremental.input
	cd .. && ./meow/$(BUILD)/meow build meow-incremental
	@test "$$(cat /tmp/meow-incremental.log)" = hit
	cd .. && ./meow/$(BUILD)/meow build meow-incremental
	@test "$$(cat /tmp/meow-incremental.log)" = hit
	@printf '%s\n' second > /tmp/meow-incremental.input
	cd .. && ./meow/$(BUILD)/meow build meow-incremental
	@test "$$(cat /tmp/meow-incremental.log)" = hithit
	cd .. && ./meow/$(BUILD)/meow clean meow-incremental
	@rm -f /tmp/meow-auto.input /tmp/meow-auto.output
	@printf '%s\n' input > /tmp/meow-auto.input
	cd .. && ./meow/$(BUILD)/meow build meow-auto
	@test "$$(cat /tmp/meow-auto.output)" = "report:/tmp/meow-auto.input:/tmp/meow-auto.input"
	cd .. && ./meow/$(BUILD)/meow clean meow-auto
	@rm -f alpha.out /tmp/meow-pattern-alpha.in
	@printf '%s\n' input > /tmp/meow-pattern-alpha.in
	cd .. && ./meow/$(BUILD)/meow build meow-pattern alpha.out
	@test "$$(cat ../alpha.out)" = "alpha:/tmp/meow-pattern-alpha.in"
	cd .. && ./meow/$(BUILD)/meow build meow-pattern alpha.out
	@test "$$(cat ../alpha.out)" = "alpha:/tmp/meow-pattern-alpha.in"
	cd .. && ./meow/$(BUILD)/meow clean meow-pattern
	@rm -f /tmp/meow-include.result
	cd .. && ./meow/$(BUILD)/meow build meow-include
	@test "$$(cat /tmp/meow-include.result)" = included
	cd .. && ./meow/$(BUILD)/meow clean meow-include
	@rm -f /tmp/meow-parallel-left /tmp/meow-parallel-right
	cd .. && ./meow/$(BUILD)/meow -j2 build meow-parallel
	@test -f /tmp/meow-parallel-left -a -f /tmp/meow-parallel-right
	cd .. && ./meow/$(BUILD)/meow -j 2 clean meow-parallel
	@rm -f /tmp/meow-parallel-fail-downstream /tmp/meow-parallel-fail-other
	@cd .. && ! ./meow/$(BUILD)/meow -j2 build meow-parallel-fail
	@test ! -e /tmp/meow-parallel-fail-downstream
	@test -f /tmp/meow-parallel-fail-other
	cd .. && ./meow/$(BUILD)/meow clean meow-parallel-fail
	cd .. && ./meow/$(BUILD)/meow list | grep -qx meow-smoke
	cd .. && ./meow/$(BUILD)/meow list | grep -qx dash
	cd test/make-compat && env -u MAKEFLAGS -u MFLAGS MEUOS_SYSROOT=./test-sysroot ../../$(BUILD)/meow build .
	@rm -f /tmp/meow-conditional.result
	cd .. && ./meow/$(BUILD)/meow build meow-conditional
	@test "$$(cat /tmp/meow-conditional.result)" = "native"
	cd .. && ./meow/$(BUILD)/meow clean meow-conditional
	cd .. && CC=cc ./meow/$(BUILD)/meow --bootstrap
	sh test/pkg_config.sh "$(abspath $(BUILD)/meow)"
	cd .. && $(MAKE) -C meow clean all

# Verify --arch flag injection by creating a minimal recipe that echoes
# ARCH, then checking the output contains the expected value.
check-meow-multiarch: $(BUILD)/meow
	@set -eu; \
	tmp=$$(mktemp -d /tmp/meow-arch-check.XXXXXX); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	mkdir -p "$$tmp/pkgs/arch-test"; \
	output="$$tmp/output"; \
	printf '%s\n' \
		'variables:' \
		'  CC: mcc' \
		'targets:' \
		'  build:' \
		'    commands:' \
		"      - echo \"ARCH=\$${ARCH}\" > \"$$output\"" \
		> "$$tmp/pkgs/arch-test/meow.yaml"; \
	cd "$$tmp" && $(abspath $(BUILD))/meow --arch=aarch64 build arch-test; \
	grep -q 'ARCH=aarch64' "$$output" && echo "multi-arch: PASS"

# Build a standalone meow against the MeuOS sysroot and exercise the native
# YAML target graph.  The shell used by recipes is intentionally the host's
# /bin/sh until the MeuOS userspace supplies its own shell.
check-sysroot-static:
	@$(MAKE) -C ../meuos-libc install DESTDIR=$(abspath ../sysroot) PREFIX=/usr
	@$(MAKE) -C ../meuos-sysroot
	@work=$$(mktemp -d /tmp/meow-sysroot-static.XXXXXX); trap 'rm -rf "$$work"' EXIT HUP INT TERM; \
	../mcc/mcc --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static \
		-o "$$work/meow" $(SRCS) -l:libc-meuos.a -L$(abspath ../meuos-sysroot/build) -lmsys; \
	cd .. && { \
		"$$work/meow" build meow-smoke && "$$work/meow" clean meow-smoke && \
		"$$work/meow" build meow-env && "$$work/meow" clean meow-env && \
		"$$work/meow" list | grep -qx meow-smoke; \
	}

# msys — Build .msys from MEUOS_SYSROOT using mkmsys
MSYS_SYSROOT ?= $(MEUOS_SYSROOT)
MSYS_OUTPUT  ?= $(MSYS_SYSROOT).msys
MKMSYS       := $(abspath ../meuos-sysroot/build/mkmsys)

msys: $(MKMSYS)
	@if [ -z "$(MSYS_SYSROOT)" ]; then \
		echo "MEUOS_SYSROOT not set — use MSYS_SYSROOT=<dir>"; exit 1; fi
	@echo "Packing $(MSYS_SYSROOT) -> $(MSYS_OUTPUT)"
	"$(MKMSYS)" --format v2 --dedup --arch=$$(basename "$$MSYS_SYSROOT" | sed 's/sysroot-//; s/\.msys//') \
	  -o "$(MSYS_OUTPUT)" "$(MSYS_SYSROOT)"

$(MKMSYS):
	$(MAKE) -C $(MSYS_DIR)

# check-msys — Validate .msys is readable
check-msys: $(MSYS_LIB)
	@ok=0; \
	for f in /tmp/*.msys; do \
		if [ -f "$$f" ]; then \
			echo "Checking $$f ..."; \
			$(MKMSYS) --list "$$f" > /dev/null 2>&1; \
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

install: $(BUILD)/meow
	@mkdir -p $(DESTDIR)/usr/bin
	cp $(BUILD)/meow $(DESTDIR)/usr/bin/meow

clean:
	rm -rf $(BUILD)

.PHONY: all check check-sysroot-static check-meow-multiarch msys check-msys install clean
