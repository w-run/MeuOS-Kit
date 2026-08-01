# mxx — MeuOS C++ Compiler Makefile
MCC_DIR := ../mcc
BUILD := build
BIN := $(BUILD)/mxx
LIB := $(MCC_DIR)/build/libmcc.a

CFLAGS ?= -O2 -g -std=gnu11
CFLAGS += -Iinclude -I$(MCC_DIR)/include -I$(MCC_DIR)/src
LDFLAGS ?= -L$(MCC_DIR)/build -lmcc
LDLIBS ?= -lmsys

SRCS := $(sort $(wildcard src/*/*.c))
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN): $(OBJS) $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

$(BUILD): ; mkdir -p $(BUILD)
$(LIB): ; $(MAKE) -C $(MCC_DIR) libmcc.a

all: $(BIN)
clean: ; rm -rf $(BUILD)
.PHONY: all clean
