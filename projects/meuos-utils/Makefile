# meuos-utils — MeuOS 核心工具集
#
# Source layout:
#   include/          public API (utils.h + tool-specific headers)
#   src/libutils/     静态库 libutils.a（xmalloc/getopt/progname/version/...）
#   src/utils/        各工具源文件，每文件一个工具
#   test/             各工具的回归脚本
#
# Two compile paths:
#   * Host CC (default for `make`/`make check`): cc -D_GNU_SOURCE
#   * mcc + MeuOS sysroot: --specs=meuos --nostdlib + libc-meuos.a

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar

BUILD := build

# 公共编译选项
BASE_CFLAGS := -Iinclude -Isrc

# libutils 源文件
LIBUTILS_SRCS := $(sort $(wildcard src/libutils/*.c))
LIBUTILS_OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(LIBUTILS_SRCS))
LIBUTILS_LIB := $(BUILD)/libutils.a

# 各工具：src/utils/<toolname>.c -> build/<toolname>
TOOL_SRCS := $(sort $(wildcard src/utils/*.c))
TOOL_BINS := $(patsubst src/utils/%.c,$(BUILD)/%,$(TOOL_SRCS))

.PHONY: all check clean install

all: $(LIBUTILS_LIB) $(TOOL_BINS)

# libutils.a
$(LIBUTILS_LIB): $(LIBUTILS_OBJS)
	@mkdir -p $(BUILD)
	$(AR) rcs $@ $^

$(BUILD)/libutils/%.o: src/libutils/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BASE_CFLAGS) -c -o $@ $<

# 通用工具编译规则
$(BUILD)/%: src/utils/%.c $(LIBUTILS_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BASE_CFLAGS) -o $@ $< $(LIBUTILS_LIB)

# 检查
check: all
	@echo ">>> meuos-utils smoke test"
	@./$(BUILD)/true; \
	 [ $$? -eq 0 ] && echo "true: PASS" || (echo "true: FAIL"; exit 1)
	@./$(BUILD)/false; \
	 [ $$? -ne 0 ] && echo "false: PASS (exit != 0)" || (echo "false: FAIL"; exit 1)
	@out=$$(./$(BUILD)/echo hello world); \
	 [ "$$out" = "hello world" ] && echo "echo: PASS" || (echo "echo: FAIL got=[$$out]"; exit 1)
	@out=$$(./$(BUILD)/echo -n "no newline"); \
	 [ "$$out" = "no newline" ] && echo "echo -n: PASS" || (echo "echo -n: FAIL got=[$$out]"; exit 1)
	@printf 'line1\nline2\n' > /tmp/utils-cat.in; \
	 out=$$(./$(BUILD)/cat /tmp/utils-cat.in); \
	 [ "$$out" = $$'line1\nline2' ] && echo "cat: PASS" || (echo "cat: FAIL got=[$$out]"; exit 1)
	@out=$$(./$(BUILD)/yes a | head -3); \
	 [ "$$out" = $$'a\na\na' ] && echo "yes: PASS" || (echo "yes: FAIL got=[$$out]"; exit 1)
	@./$(BUILD)/echo --version | head -1 > /tmp/utils-ver.txt; \
	 grep -q '(meuos-utils)' /tmp/utils-ver.txt && echo "--version flag: PASS" || (echo "--version: FAIL"; exit 1)
	@./$(BUILD)/echo --help | grep -q 'STRING' && echo "--help flag: PASS" || (echo "--help: FAIL"; exit 1)
	@rm -f /tmp/utils-cat.in /tmp/utils-ver.txt
	@echo ">>> all smoke tests PASS"

# 安装
install: all
	@mkdir -p $(DESTDIR)/usr/bin $(DESTDIR)/usr/lib
	@for bin in $(TOOL_BINS); do \
		cp $$bin $(DESTDIR)/usr/bin/; \
	done
	@cp $(LIBUTILS_LIB) $(DESTDIR)/usr/lib/

clean:
	rm -rf $(BUILD)
