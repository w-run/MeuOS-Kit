# meow - MeuOS build system
#
# Source layout:
#   src/include/    shared declarations (meow.h)
#   src/cli/        entry point & command dispatch (main.c)
#   src/recipe/     recipe parsing & loading (parse.c, parse_meow.c, recipe.c, lint.c)
#   src/engine/     build engine (graph.c, exec.c, probe.c)
#   src/pkg/        package management (pkg_config.c, pkglib.c)
#   src/info/       information commands (show.c, init.c, env.c)
#   src/util/       utilities (triple.c, color.c, state.c)
#
# Two compile paths are preserved:
#   * host CC (default for `make check`): -D_GNU_SOURCE for environ.
#   * mcc + MeuOS sysroot: --specs=meuos --nostdlib, link libc-meuos.a.

ifeq ($(origin CC),default)
CC := ../mcc/mcc
endif
SYSROOT ?= ../sysroot
BUILD ?= build
REPO_ROOT := $(abspath $(CURDIR)/../..)

SRCS := $(sort $(shell find src -name '*.c'))
# Filter out libm4 objects — they use host cc, not mcc
OBJS := $(filter-out $(BUILD)/libm4/m4_engine.o,$(patsubst src/%.c,$(BUILD)/%.o,$(SRCS)))
LIBM4_OBJS := $(BUILD)/libm4/m4_engine.o

# color.c and env.c are picked up automatically via the wildcard above.

ifeq ($(CC),../mcc/mcc)
  MEOW_CFLAGS := --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib -I../meuos-sysroot/include -Isrc/include -Isrc -Isrc/util
  MEOW_LDLIBS := -l:libc-meuos.a -L$(abspath ../meuos-sysroot/build) -lmsys
else
  MEOW_CFLAGS := -D_GNU_SOURCE -I../meuos-sysroot/include -Isrc/include -Isrc -Isrc/util
  MEOW_LDLIBS := -L$(abspath ../meuos-sysroot/build) -lmsys
endif

# libmsys.a — .msys single-file sysroot reader (dependency for msys/check-msys)
MSYS_LIB := $(abspath ../meuos-sysroot/build/libmsys.a)
MSYS_DIR := $(abspath ../meuos-sysroot)

$(MSYS_LIB):
	$(MAKE) -C $(MSYS_DIR)

all: $(BUILD)/meow

$(BUILD)/meow: $(OBJS) $(LIBM4_OBJS) $(MSYS_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(MEOW_CFLAGS) -o $@ $^ $(MEOW_LDLIBS)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(MEOW_CFLAGS) -c -o $@ $<

# m4_engine.o: must be compiled with host cc (not mcc) for C library access
$(BUILD)/libm4/m4_engine.o: src/libm4/m4_engine.c
	@mkdir -p $(dir $@)
	cc -I../meuos-sysroot/include -Isrc/include -Isrc -Isrc/util -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

check: $(BUILD)/meow
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-smoke
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow clean meow-smoke
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-env
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow clean meow-env
	@rm -f /tmp/meow-incremental.input /tmp/meow-incremental.output /tmp/meow-incremental.log
	@printf '%s\n' first > /tmp/meow-incremental.input
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-incremental
	@test "$$(cat /tmp/meow-incremental.log)" = hit
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-incremental
	@test "$$(cat /tmp/meow-incremental.log)" = hit
	@printf '%s\n' second > /tmp/meow-incremental.input
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-incremental
	@test "$$(cat /tmp/meow-incremental.log)" = hithit
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow clean meow-incremental
	@rm -f /tmp/meow-auto.input /tmp/meow-auto.output
	@printf '%s\n' input > /tmp/meow-auto.input
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-auto
	@test "$$(cat /tmp/meow-auto.output)" = "report:/tmp/meow-auto.input:/tmp/meow-auto.input"
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow clean meow-auto
	@rm -f /tmp/meow-pattern-alpha.in
	@printf '%s\n' input > /tmp/meow-pattern-alpha.in
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-pattern alpha.out
	@test "$$(cat $(REPO_ROOT)/alpha.out)" = "alpha:/tmp/meow-pattern-alpha.in"
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-pattern alpha.out
	@test "$$(cat $(REPO_ROOT)/alpha.out)" = "alpha:/tmp/meow-pattern-alpha.in"
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow clean meow-pattern
	@rm -f /tmp/meow-include.result
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-include
	@test "$$(cat /tmp/meow-include.result)" = included
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow clean meow-include
	@rm -f /tmp/meow-parallel-left /tmp/meow-parallel-right
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow -j2 build meow-parallel
	@test -f /tmp/meow-parallel-left -a -f /tmp/meow-parallel-right
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow -j 2 clean meow-parallel
	@rm -f /tmp/meow-parallel-fail-downstream /tmp/meow-parallel-fail-other
	cd $(REPO_ROOT) && ! ./projects/meow/$(BUILD)/meow -j2 build meow-parallel-fail
	@test ! -e /tmp/meow-parallel-fail-downstream
	@test -f /tmp/meow-parallel-fail-other
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow clean meow-parallel-fail
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow list | grep -qx meow-smoke
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow list | grep -qx dash
	cd test/make-compat && env -u MAKEFLAGS -u MFLAGS MEUOS_SYSROOT=./test-sysroot ../../$(BUILD)/meow build .
	@rm -f /tmp/meow-conditional.result
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow build meow-conditional
	@test "$$(cat /tmp/meow-conditional.result)" = "native"
	cd $(REPO_ROOT) && ./projects/meow/$(BUILD)/meow clean meow-conditional
	cd $(REPO_ROOT) && CC=cc ./projects/meow/$(BUILD)/meow --bootstrap
	sh test/pkg_config.sh "$(abspath $(BUILD)/meow)"
	sh test/pkg_config_integration.sh "$(abspath $(BUILD)/meow)"
	sh test/macros_test.sh "$(abspath $(BUILD)/meow)"
	sh test/macros_v3_test.sh "$(abspath $(BUILD)/meow)"
	cd $(REPO_ROOT) && $(MAKE) -C projects/meow clean all

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
	ln -sf meow $(DESTDIR)/usr/bin/pkg-config

clean:
	rm -rf $(BUILD)

.PHONY: all check check-sysroot-static check-meow-multiarch msys check-msys install clean
