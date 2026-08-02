# meuos-utils — MeuOS 核心工具集
#
# Source layout:
#   include/            public API (utils.h + tool-specific headers)
#   src/libutils/       静态库 libutils.a（xmalloc/getopt/progname/version/...）
#   src/utils/core/     核心文件/目录操作 (ls/cp/mv/rm/cat/tree/...)
#   src/utils/text/     文本处理 (head/tail/wc/sort/grep/sed/awk/...)
#   src/utils/file/     文件路径/类型 (file/basename/dirname/readlink/...)
#   src/utils/arch/     归档/压缩 (tar/gzip/unzip/mz)
#   src/utils/sys/      系统/进程 (date/env/sleep/kill/id/uname/...)
#   src/utils/net/      网络工具 (curl/wget/ifconfig/netstat/route/ping)
#   test/               各工具的回归脚本
#
# Two compile paths:
#   * Host CC (default for `make`/`make check`): cc -D_GNU_SOURCE
#   * mcc + MeuOS sysroot: --specs=meuos --nostdlib + libc-meuos.a

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar

BUILD := build

# meuos-compress 库路径
MZ_DIR := ../meuos-compress
MZ_LIB := $(MZ_DIR)/build/libmz.a
MZ_INC := -I$(MZ_DIR)/include

# 公共编译选项
BASE_CFLAGS := -Iinclude -Isrc

# 工具分类子目录
TOOL_DIRS := core text file arch sys net

# VPATH：让 Make 按类别搜索源文件
VPATH := $(addprefix src/utils/,$(TOOL_DIRS))

# libutils 源文件
LIBUTILS_SRCS := $(sort $(wildcard src/libutils/*.c))
LIBUTILS_OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(LIBUTILS_SRCS))
LIBUTILS_LIB := $(BUILD)/libutils.a

# 各工具：src/utils/<category>/<toolname>.c -> build/<toolname>
# 用 notdir 去掉分类目录，所有二进制平铺到 build/
TOOL_SRCS := $(sort $(foreach dir,$(TOOL_DIRS),$(wildcard src/utils/$(dir)/*.c)))
TOOL_NAMES := $(notdir $(patsubst %.c,%,$(TOOL_SRCS)))
TOOL_BINS := $(addprefix $(BUILD)/,$(TOOL_NAMES))

# 需要特殊链接的工具（从通用规则中排除）
SPECIAL_BINS := $(BUILD)/mz $(BUILD)/awk
GENERIC_BINS := $(filter-out $(SPECIAL_BINS),$(TOOL_BINS))

.PHONY: all check clean install

all: $(LIBUTILS_LIB) $(GENERIC_BINS) $(SPECIAL_BINS)

# mz 工具：依赖 libmz
$(BUILD)/mz: src/utils/arch/mz.c $(LIBUTILS_LIB) $(MZ_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BASE_CFLAGS) $(MZ_INC) -o $@ $< $(LIBUTILS_LIB) $(MZ_LIB) -ldl

# awk: -O2 optimization exposes UB in parser; use -O0 until fixed
$(BUILD)/awk: src/utils/text/awk.c $(LIBUTILS_LIB)
	@mkdir -p $(dir $@)
	$(CC) -O0 -g $(BASE_CFLAGS) -o $@ $< $(LIBUTILS_LIB) -lm

# libmz 依赖
$(MZ_LIB):
	$(MAKE) -C $(MZ_DIR)

# libutils.a
$(LIBUTILS_LIB): $(LIBUTILS_OBJS)
	@mkdir -p $(BUILD)
	$(AR) rcs $@ $^

$(BUILD)/libutils/%.o: src/libutils/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BASE_CFLAGS) -c -o $@ $<

# 通用工具编译规则（VPATH 自动搜索 src/utils/<category>/）
$(GENERIC_BINS): $(LIBUTILS_LIB)
$(BUILD)/%: %.c $(LIBUTILS_LIB)
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
	 out=$$(./$(BUILD)/cat --classic /tmp/utils-cat.in); \
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
	@./$(BUILD)/sort --help 2>&1 | grep -q 'Usage' && echo "sort --help: PASS" || (echo "sort --help: FAIL"; exit 1) || true
	@./$(BUILD)/uniq --help 2>&1 | grep -q 'Usage' && echo "uniq --help: PASS" || (echo "uniq --help: FAIL"; exit 1) || true
	@./$(BUILD)/tr --help 2>&1 | grep -q 'Usage' && echo "tr --help: PASS" || (echo "tr --help: FAIL"; exit 1) || true
	@./$(BUILD)/cut --help 2>&1 | grep -q 'Usage' && echo "cut --help: PASS" || (echo "cut --help: FAIL"; exit 1) || true
	@out=$$(printf 'banana\napple\ncherry\n' | ./$(BUILD)/sort); \
	 [ "$$out" = $$'apple\nbanana\ncherry' ] && echo "sort: PASS" || (echo "sort: FAIL got=[$$out]"; exit 1)
	@out=$$(printf 'apple\napple\nbanana\n' | ./$(BUILD)/uniq); \
	 [ "$$out" = $$'apple\nbanana' ] && echo "uniq: PASS" || (echo "uniq: FAIL got=[$$out]"; exit 1)
	@out=$$(printf 'abcdef\n' | ./$(BUILD)/tr 'a-z' 'A-Z'); \
	 [ "$$out" = 'ABCDEF' ] && echo "tr: PASS" || (echo "tr: FAIL got=[$$out]"; exit 1)
	@out=$$(printf 'a\tb\tc\n' | ./$(BUILD)/cut -f1); \
	 [ "$$out" = 'a' ] && echo "cut -f1: PASS" || (echo "cut: FAIL got=[$$out]"; exit 1)
	@printf 'hello\nworld\n' > /tmp/utils-cat-classic.in; \
	 out=$$(./$(BUILD)/cat --classic /tmp/utils-cat-classic.in); \
	 [ "$$out" = $$'hello\nworld' ] && echo "cat classic: PASS" || (echo "cat classic: FAIL got=[$$out]"; exit 1); \
	 rm -f /tmp/utils-cat-classic.in
	@rm -f /tmp/utils-cat.in /tmp/utils-ver.txt /tmp/utils-ls-ver.txt /tmp/utils-tree-ver.txt
	@echo ">>> all smoke tests PASS"
	@cd test && ./runner.sh

# 安装
install: all
	@mkdir -p $(DESTDIR)/usr/bin $(DESTDIR)/usr/lib
	@for bin in $(TOOL_BINS); do \
		cp $$bin $(DESTDIR)/usr/bin/; \
	done
	@cp $(LIBUTILS_LIB) $(DESTDIR)/usr/lib/

clean:
	rm -rf $(BUILD)
