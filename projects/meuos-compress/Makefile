CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar
SO_FLAGS ?= -shared -fPIC
SO_LDFLAGS ?= -Wl,--allow-multiple-definition
MSYS_INC ?= -I../meuos-sysroot/include
BUILD := build

# Sources organized by module subdirectory (filter out special-compile files)
CODE_SRC := $(sort $(wildcard src/codec/*.c))
CONT_SRC := $(filter-out src/container/mz_msys.c src/container/mxa_cli.c src/container/mxz_cli.c,$(sort $(wildcard src/container/*.c)))
CRYPT_SRC := $(sort $(wildcard src/crypt/*.c))
SRCS := $(CODE_SRC) $(CONT_SRC) $(CRYPT_SRC)
# Flat object names (strip subdirectory prefix)
OBJS := $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o)))
MSYS_OBJ := $(BUILD)/mz_msys.o
LIB := $(BUILD)/libmz.a
SO_LIB := $(BUILD)/libmz.so

# CLI binaries
MZ_BIN := $(BUILD)/mz
MXZ_BIN := $(BUILD)/mxz

# Test binaries
TEST_MX := $(BUILD)/test_mz
TEST_MXA := $(BUILD)/test_mxa

all: $(LIB) $(MZ_BIN) $(MXZ_BIN)

# vpath enables pattern rule to find .c in subdirectories
vpath %.c src/codec:src/container:src/crypt

$(BUILD)/%.o: %.c include/mz.h include/mxa.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<
$(MSYS_OBJ): src/container/mz_msys.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -Iinclude $(MSYS_INC) -c -o $@ $<
$(LIB): $(OBJS)
	$(AR) rcs $@ $^
libmz.so: CFLAGS += -fPIC $(MSYS_INC)
libmz.so: $(OBJS) $(MSYS_OBJ)
	$(CC) $(SO_FLAGS) -o $(SO_LIB) $(OBJS) $(MSYS_OBJ) $(SO_LDFLAGS)

$(MZ_BIN): src/container/mxa_cli.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ src/container/mxa_cli.c $(LIB) -ldl

$(MXZ_BIN): src/container/mxz_cli.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ src/container/mxz_cli.c $(LIB)

check: $(TEST_MX) $(TEST_MXA) $(MZ_BIN) $(MXZ_BIN)
	./$(TEST_MX)
	./$(TEST_MXA)
	@echo "=== mz CLI --help ==="
	@./$(MZ_BIN) --help >/dev/null 2>&1 && echo "PASS: mz --help"
	@echo "=== mxz CLI test ==="
	@$(MXZ_BIN) --help >/dev/null 2>&1 && echo "PASS: mxz --help"
	@echo "Hello World" > /tmp/mxz_test.txt
	@$(MXZ_BIN) c -o /tmp/mxz_test.mz /tmp/mxz_test.txt && echo "PASS: mxz compress mz"
	@$(MXZ_BIN) l /tmp/mxz_test.mz >/dev/null && echo "PASS: mxz list mz"
	@$(MXZ_BIN) t /tmp/mxz_test.mz >/dev/null && echo "PASS: mxz test mz"
	$(MXZ_BIN) c --format=gz -o /tmp/mxz_test.gz /tmp/mxz_test.txt && echo "PASS: mxz compress gz"
	$(MXZ_BIN) t /tmp/mxz_test.gz >/dev/null && echo "PASS: mxz test gz"

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
