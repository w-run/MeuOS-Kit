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
	@./$(BUILD)/ls --version > /tmp/utils-ls-ver.txt; \
	 grep -q '(meuos-utils)' /tmp/utils-ls-ver.txt && echo "ls --version: PASS" || (echo "ls --version: FAIL"; exit 1)
	@out=$$(./$(BUILD)/ls --classic /tmp 2>&1 | head -1); \
	 [ -n "$$out" ] && echo "ls classic: PASS" || (echo "ls classic: FAIL"; exit 1)
	@./$(BUILD)/tree --version > /tmp/utils-tree-ver.txt; \
	 grep -q '(meuos-utils)' /tmp/utils-tree-ver.txt && echo "tree --version: PASS" || (echo "tree --version: FAIL"; exit 1)
	@out=$$(./$(BUILD)/tree --classic /tmp 2>&1 | wc -l); \
	 [ "$$out" -gt 5 ] && echo "tree classic: PASS" || (echo "tree classic: FAIL (lines=$$out)"; exit 1)
	@./$(BUILD)/find --help 2>&1 | grep -q 'Usage' && echo "find --help: PASS (现代版)" || (echo "find --help: FAIL"; exit 1) || true
	@./$(BUILD)/grep --help 2>&1 | grep -q 'Usage' && echo "grep --help: PASS (现代版)" || (echo "grep --help: FAIL"; exit 1) || true
	@./$(BUILD)/diff --help 2>&1 | grep -q 'Usage' && echo "diff --help: PASS (现代版)" || (echo "diff --help: FAIL"; exit 1) || true
	@./$(BUILD)/cp --help 2>&1 | grep -q 'Usage' && echo "cp --help: PASS (现代版)" || (echo "cp --help: FAIL"; exit 1) || true
	@./$(BUILD)/mv --help 2>&1 | grep -q 'Usage' && echo "mv --help: PASS (现代版)" || (echo "mv --help: FAIL"; exit 1) || true
	@./$(BUILD)/rm --help 2>&1 | grep -q 'Usage' && echo "rm --help: PASS (现代版)" || (echo "rm --help: FAIL"; exit 1) || true
	@./$(BUILD)/wc --help 2>&1 | grep -q 'Usage' && echo "wc --help: PASS" || (echo "wc --help: FAIL"; exit 1) || true
	@./$(BUILD)/head --help 2>&1 | grep -q 'Usage' && echo "head --help: PASS" || (echo "head --help: FAIL"; exit 1) || true
	@./$(BUILD)/tail --help 2>&1 | grep -q 'Usage' && echo "tail --help: PASS" || (echo "tail --help: FAIL"; exit 1) || true
	@rm -f /tmp/utils-cat.in /tmp/utils-ver.txt /tmp/utils-ls-ver.txt /tmp/utils-tree-ver.txt
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
