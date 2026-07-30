CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar
BUILD := build
SRCS := $(sort $(wildcard src/*/*.c))
OBJS := $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o)))
LIB := $(BUILD)/libtui.a
TEST := $(BUILD)/test_tui

EXAMPLES := demo demo_dialog demo_list
EXE := $(addprefix $(BUILD)/,$(EXAMPLES))

.PHONY: all check clean examples $(EXAMPLES)

all: $(LIB)

vpath %.c src/core:src/widget

$(BUILD)/%.o: %.c include/meuos/libtui.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

$(BUILD):
	mkdir -p $(BUILD)

check: $(TEST)
	./$(TEST)

$(TEST): test/test.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/test.c $(LIB)

# ── 示例程序 ────────────────────────────────────────

examples: $(EXE)

$(BUILD)/demo: examples/demo.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -o $@ examples/demo.c $(LIB)

$(BUILD)/demo_dialog: examples/demo_dialog.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -o $@ examples/demo_dialog.c $(LIB)

$(BUILD)/demo_list: examples/demo_list.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -o $@ examples/demo_list.c $(LIB)

# 单个示例快捷构建
demo: $(BUILD)/demo
demo_dialog: $(BUILD)/demo_dialog
demo_list: $(BUILD)/demo_list

clean:
	rm -rf $(BUILD)
