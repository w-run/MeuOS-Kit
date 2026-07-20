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

ifeq ($(CC),../mcc/mcc)
  MEOW_CFLAGS := --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib
  MEOW_LDLIBS := -l:libc-meuos.a
else
  MEOW_CFLAGS := -D_GNU_SOURCE
  MEOW_LDLIBS :=
endif

all: $(BUILD)/meow

$(BUILD)/meow: $(OBJS)
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
	cd test/make-compat && MEUOS_SYSROOT=./test-sysroot ../../$(BUILD)/meow build .
	cd .. && CC=cc ./meow/$(BUILD)/meow --bootstrap
	cd .. && $(MAKE) -C meow clean all

# Build a standalone meow against the MeuOS sysroot and exercise the native
# YAML target graph.  The shell used by recipes is intentionally the host's
# /bin/sh until the MeuOS userspace supplies its own shell.
check-sysroot-static:
	@$(MAKE) -C ../meuos-libc install DESTDIR=$(abspath ../sysroot) PREFIX=/usr
	@work=$$(mktemp -d /tmp/meow-sysroot-static.XXXXXX); trap 'rm -rf "$$work"' EXIT HUP INT TERM; \
	../mcc/mcc --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static \
		-o "$$work/meow" $(SRCS) -l:libc-meuos.a; \
	cd .. && { \
		"$$work/meow" build meow-smoke && "$$work/meow" clean meow-smoke && \
		"$$work/meow" build meow-env && "$$work/meow" clean meow-env && \
		"$$work/meow" list | grep -qx meow-smoke; \
	}

install: $(BUILD)/meow
	@mkdir -p $(DESTDIR)/usr/bin
	cp $(BUILD)/meow $(DESTDIR)/usr/bin/meow

clean:
	rm -rf $(BUILD)

.PHONY: all check check-sysroot-static install clean
