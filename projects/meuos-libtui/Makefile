CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar
BUILD := build
SRCS := $(sort $(wildcard src/*.c))
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
LIB := $(BUILD)/libtui.a
TEST := $(BUILD)/test_tui

.PHONY: all check clean

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

clean:
	rm -rf $(BUILD)
