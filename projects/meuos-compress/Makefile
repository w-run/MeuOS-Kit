CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar
SO_FLAGS ?= -shared -fPIC
SO_LDFLAGS ?= -Wl,--allow-multiple-definition
MSYS_INC ?= -I../meuos-sysroot/include
BUILD := build

# Sources organized by module subdirectory (filter out CLI sources)
CODE_SRC := $(sort $(wildcard src/codec/*.c))
# NOTE: mz_fusion.c is included via CODE_SRC wildcard automatically.
CONT_SRC := $(filter-out src/container/mz_msys.c src/container/mxa_cli.c src/container/mxz_cli.c src/container/mz_cli.c,$(sort $(wildcard src/container/*.c)))
CRYPT_SRC := $(sort $(wildcard src/crypt/*.c))
SRCS := $(CODE_SRC) $(CONT_SRC) $(CRYPT_SRC)
# Flat object names (strip subdirectory prefix)
OBJS := $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o)))
MSYS_OBJ := $(BUILD)/mz_msys.o
LIB := $(BUILD)/libmz.a
SO_LIB := $(BUILD)/libmz.so

# CLI binary (single mz binary)
MZ_BIN := $(BUILD)/mz

# Test binaries
TEST_MX := $(BUILD)/test_mz
TEST_MXA := $(BUILD)/test_mxa
TEST_FUSION := $(BUILD)/test_fusion

all: $(LIB) $(MZ_BIN)

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

# Single mz CLI binary (replaces mxa_cli.c + mxz_cli.c)
$(MZ_BIN): src/container/mz_cli.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ src/container/mz_cli.c $(LIB)

check: $(TEST_MX) $(TEST_MXA) $(TEST_FUSION) $(MZ_BIN)
	@echo "=== mz codec tests ==="
	./$(TEST_MX)
	./$(TEST_MXA)
	./$(TEST_FUSION)
	@echo "=== mz CLI test ==="
	@$(MZ_BIN) --help >/dev/null 2>&1 && echo "PASS: mz --help"
	@echo "test content" > $(BUILD)/test_in.txt
	@$(MZ_BIN) c -o $(BUILD)/test_out.mz $(BUILD)/test_in.txt >/dev/null && echo "PASS: mz c -> .mz"
	@$(MZ_BIN) l $(BUILD)/test_out.mz >/dev/null && echo "PASS: mz l .mz"
	@$(MZ_BIN) t $(BUILD)/test_out.mz >/dev/null && echo "PASS: mz t .mz"
	@rm -rf $(BUILD)/xtract && mkdir $(BUILD)/xtract && \
	  cd $(BUILD)/xtract && $(CURDIR)/$(MZ_BIN) d $(CURDIR)/$(BUILD)/test_out.mz >/dev/null && \
	  cmp -s $(CURDIR)/$(BUILD)/test_in.txt $(CURDIR)/$(BUILD)/xtract/test_in.txt && \
	  echo "PASS: mz d .mz roundtrip"
	@$(MZ_BIN) c -o $(BUILD)/test_out.gz $(BUILD)/test_in.txt >/dev/null && echo "PASS: mz c -> .gz"
	@$(MZ_BIN) t $(BUILD)/test_out.gz >/dev/null && echo "PASS: mz t .gz"
	@$(MZ_BIN) d $(BUILD)/test_out.gz > $(BUILD)/test_out_decomp.txt 2>/dev/null && \
	  cmp -s $(BUILD)/test_in.txt $(BUILD)/test_out_decomp.txt && \
	  echo "PASS: mz d .gz roundtrip"
	@rm -f $(BUILD)/test_in.txt $(BUILD)/test_out.mz $(BUILD)/test_out.gz $(BUILD)/xtract/test_in.txt

$(TEST_MX): test/test.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/test.c $(LIB)
$(TEST_MXA): test/test_mxa.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/test_mxa.c $(LIB) -ldl
$(TEST_FUSION): test/test_fusion.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/test_fusion.c $(LIB)
clean:
	rm -rf $(BUILD)

bench_mxa: $(BUILD)/bench_mxa
	./$(BUILD)/bench_mxa

$(BUILD)/bench_mxa: test/bench_mxa.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/bench_mxa.c -L$(BUILD) -lmz $(LDLIBS)