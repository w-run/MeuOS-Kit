CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar
BUILD := build
SRCS := $(sort $(wildcard src/*.c))
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
LIB := $(BUILD)/libtui.a
TEST := $(BUILD)/test_tui

EXAMPLES := demo_dialog demo_list demo_dashboard demo_showcase
EXE := $(addprefix $(BUILD)/,$(EXAMPLES))

.PHONY: all check clean examples $(EXAMPLES)

all: $(LIB)

$(BUILD)/%.o: src/%.c include/meuos/libtui.h | $(BUILD)
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

$(BUILD)/demo_dialog: examples/demo_dialog.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -o $@ examples/demo_dialog.c $(LIB)

$(BUILD)/demo_list: examples/demo_list.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -o $@ examples/demo_list.c $(LIB)

$(BUILD)/demo_dashboard: examples/demo_dashboard.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -o $@ examples/demo_dashboard.c $(LIB)

$(BUILD)/demo_showcase: examples/demo_showcase.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -o $@ examples/demo_showcase.c $(LIB)

# 单个示例快捷构建
demo_dialog: $(BUILD)/demo_dialog
demo_list: $(BUILD)/demo_list
demo_dashboard: $(BUILD)/demo_dashboard
demo_showcase: $(BUILD)/demo_showcase

clean:
	rm -rf $(BUILD)
