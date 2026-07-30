CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar
SO_FLAGS ?= -shared -fPIC
SO_LDFLAGS ?= -Wl,--allow-multiple-definition
MSYS_INC ?= -I../meuos-sysroot/include
BUILD := build

# Library sources (exclude CLI and msys plugin)
SRCS := $(filter-out src/mz_msys.c src/mxa_cli.c,$(sort $(wildcard src/*.c)))
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
MSYS_OBJ := $(BUILD)/mz_msys.o
LIB := $(BUILD)/libmz.a
SO_LIB := $(BUILD)/libmz.so

# CLI binary
MZ_BIN := $(BUILD)/mz

# Test binaries
TEST_MX := $(BUILD)/test_mz
TEST_MXA := $(BUILD)/test_mxa

all: $(LIB) $(MZ_BIN)

$(BUILD)/%.o: src/%.c include/mz.h include/mxa.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<
$(MSYS_OBJ): src/mz_msys.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -Iinclude $(MSYS_INC) -c -o $@ $<
$(LIB): $(OBJS)
	$(AR) rcs $@ $^
libmz.so: CFLAGS += -fPIC $(MSYS_INC)
libmz.so: $(OBJS) $(MSYS_OBJ)
	$(CC) $(SO_FLAGS) -o $(SO_LIB) $(OBJS) $(MSYS_OBJ) $(SO_LDFLAGS)

$(MZ_BIN): src/mxa_cli.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ src/mxa_cli.c $(LIB)

check: $(TEST_MX) $(TEST_MXA) $(MZ_BIN)
	./$(TEST_MX)
	./$(TEST_MXA)
	@echo "=== mz CLI --help ==="
	@./$(MZ_BIN) --help >/dev/null 2>&1 && echo "PASS: mz --help"

$(TEST_MX): test/test.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/test.c $(LIB)
$(TEST_MXA): test/test_mxa.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/test_mxa.c $(LIB) -ldl
clean:
	rm -rf $(BUILD)

bench_mxa: $(BUILD)/bench_mxa
	./$(BUILD)/bench_mxa

$(BUILD)/bench_mxa: test/bench_mxa.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/bench_mxa.c -L$(BUILD) -lmz $(LDLIBS)
