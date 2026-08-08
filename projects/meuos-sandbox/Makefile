# meuos-sandbox (mbox) — MeuOS Box: Unified Development Sandbox
#
# Source layout:
#   include/mbox/        公共 API 头文件
#   src/                 源文件（main.c, conf.c, ns.c）
#   test/                测试目录
#
# Two compile paths:
#   * Host CC (default): cc -D_GNU_SOURCE
#   * mcc + MeuOS sysroot: CC=mcc CFLAGS='--specs=meuos --nostdlib -static'

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=gnu11 -D_GNU_SOURCE
LDFLAGS ?=
BUILD := build

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
BIN := $(BUILD)/mbox

BASE_CFLAGS := -Iinclude

.PHONY: all check clean install

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(BASE_CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.c include/mbox/*.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BASE_CFLAGS) -c -o $@ $<

# Smoke test: compile + basic exec
check: $(BIN)
	@printf '>>> mbox smoke test\\n'
	@./$(BIN) --help 2>&1 | head -3 > /dev/null && printf '  --help: PASS\\n' || (printf '  --help: FAIL\\n'; exit 1)
	@# Basic version/help with no rootfs should exit 1
	@! ./$(BIN) 2>/dev/null && printf '  no-args: PASS (expected exit 1)\\n' || (printf '  no-args: FAIL (expected exit 1)\\n'; exit 1)
	@printf '>>> mbox smoke: ALL PASS\\n'

# Build the rootfs (uses mbox-mkrootfs shell script)
rootfs: $(BIN)
	@sh mbox-mkrootfs --output=./rootfs

clean:
	rm -rf $(BUILD)

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/mbox