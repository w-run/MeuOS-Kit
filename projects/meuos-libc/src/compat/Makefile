# meuos-libc/compat - GNU extension compatibility layer
#
# Built as a separate archive (libc-meuos-compat.a) so applications can
# opt-in by linking it explicitly. Provides error / error_at_line /
# argp / obstack / funopen / fopencookie / asprintf /
# vasprintf / weak __malloc_hook / weak __free_hook / strdupa /
# strndupa -- symbols that glibc ships but POSIX does not require.
#
# Layout:
#   include/    public compat headers (error.h, obstack.h, argp.h)
#   src/        implementation (.c files, one per symbol group)
#   ../test/compat.c  regression test (driven from the parent Makefile)

HOST_CC ?= cc
AR ?= ar
PREFIX ?= /usr
DESTDIR ?=
ARCH ?= x86_64
MCC ?= ../../mcc/mcc
SYSROOT ?= ../../sysroot

# 防止 HOST_CC 泄漏到 mcc 的环境变量中：mcc 的 pick_host_cc() 会优先读取
# MCC_HOST_CC / HOST_CC 环境变量，而非基于 --target 自动选择交叉编译器。
# unexport 确保 HOST_CC 仅作为 Make 变量使用，不进入子进程环境。
unexport HOST_CC

ifeq ($(ARCH),i386)
CC := $(MCC) --target=i386
AS := $(HOST_CC) -m32
else ifeq ($(ARCH),aarch64)
CC := $(MCC) --target=aarch64
else
CC ?= $(HOST_CC)
endif

BUILD := build
COMPAT_LIB := $(BUILD)/libc-meuos-compat.a

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

CFLAGS := -Iinclude -I../../include

all: $(COMPAT_LIB)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(COMPAT_LIB): $(OBJS)
	$(AR) rcs $@ $^

install: $(COMPAT_LIB)
	@mkdir -p $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include
	cp -r include/. $(DESTDIR)$(PREFIX)/include/
	cp $(COMPAT_LIB) $(DESTDIR)$(PREFIX)/lib/libc-meuos-compat.a

check: $(COMPAT_LIB)
	@$(MAKE) -C ../.. install DESTDIR=$(abspath $(SYSROOT)) PREFIX=/usr
	@mkdir -p $(BUILD)
	$(MCC) --specs=meuos --sysroot=$(abspath $(SYSROOT)) --nostdlib --static \
		-o $(BUILD)/test-compat ../../test/compat.c -l:libc-meuos-compat.a -l:libc-meuos.a
	$(BUILD)/test-compat

clean:
	rm -rf $(BUILD)

.PHONY: all install check clean
