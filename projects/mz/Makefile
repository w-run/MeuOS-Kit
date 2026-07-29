CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar

BUILD := build
SRCS := $(sort $(wildcard src/*.c))
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
LIB := $(BUILD)/libmz.a
TEST := $(BUILD)/test_compress

all: $(LIB)

$(BUILD)/%.o: src/%.c include/mz.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

check: $(TEST)
	./$(TEST)

$(TEST): test/test.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/test.c $(LIB)

clean:
	rm -rf $(BUILD)

.PHONY: all check clean
