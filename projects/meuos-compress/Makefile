CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
AR ?= ar
SO_FLAGS ?= -shared -fPIC
SO_LDFLAGS ?= -Wl,--allow-multiple-definition
MSYS_INC ?= -I../meuos-sysroot/include
BUILD := build
SRCS := $(filter-out src/mz_msys.c,$(sort $(wildcard src/*.c)))
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
MSYS_OBJ := $(BUILD)/mz_msys.o
LIB := $(BUILD)/libmz.a
SO_LIB := $(BUILD)/libmz.so
TEST := $(BUILD)/test_mz
all: $(LIB)
$(BUILD)/%.o: src/%.c include/mz.h
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
check: $(TEST)
	./$(TEST)
$(TEST): test/test.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o $@ test/test.c $(LIB)
clean:
	rm -rf $(BUILD)
