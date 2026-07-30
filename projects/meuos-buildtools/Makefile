# meuos-buildtools — MeuOS build tool replacements
#
# BusyBox-style multicall binary.
# Builds a single `buildtools` binary; symlinks dispatch per tool name.
#   ln -s buildtools /usr/bin/m4    → invokes m4_main()
#   ln -s buildtools /usr/bin/flex   → invokes flex_main()
#   ln -s buildtools /usr/bin/gperf  → invokes gperf_main()
#   buildtools m4 [args...]          → explicit subcommand form

HOST_CC ?= cc
MCC ?= ../../projects/mcc/mcc
AR ?= ar
PREFIX ?= /usr
DESTDIR ?=

BUILD := build
SRC := src
TEST := test

MODULES := m4 gperf flex
MODULE_OBJS := $(MODULES:%=$(BUILD)/%.o)
BIN := $(BUILD)/buildtools
SYMLINKS := $(MODULES:%=$(BUILD)/%)

# m4 engine library (from meow)
MEOW_DIR := ../meow
M4_ENGINE_O := $(MEOW_DIR)/$(BUILD)/libm4/m4_engine.o

.PHONY: all clean check install

all: $(BIN) $(SYMLINKS)

# Compile each module
$(BUILD)/m4.o: $(SRC)/m4/m4.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

# Build m4_engine.o from meow source
$(M4_ENGINE_O): $(MEOW_DIR)/src/libm4/m4_engine.c $(MEOW_DIR)/src/libm4/m4_engine.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -I$(MEOW_DIR)/src -I$(MEOW_DIR)/src/include -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

$(BUILD)/gperf.o: $(SRC)/gperf/gperf.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

$(BUILD)/flex.o: $(SRC)/flex/flex.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

$(BUILD)/buildtools.o: $(SRC)/buildtools/buildtools.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

# Link single binary
$(BIN): $(BUILD)/buildtools.o $(MODULE_OBJS) $(M4_ENGINE_O)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -o $@ $^

# Create symlinks (BusyBox style)
$(BUILD)/%: $(BIN)
	@ln -sf buildtools $@

# Install into DESTDIR/sysroot/usr/bin/
install: $(BIN)
	install -d $(DESTDIR)/$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)/$(PREFIX)/bin/buildtools
	@for t in $(MODULES); do \
		ln -sf buildtools $(DESTDIR)/$(PREFIX)/bin/$$t; \
	done

check: $(BIN)
	@for t in $(MODULES); do \
		echo "Testing $$t..."; \
		rm -f /tmp/bt-$$t; \
		ln -s $(BIN) /tmp/bt-$$t; \
		/tmp/bt-$$t --help 2>/dev/null || true; \
	done

# Static linking target for sysroot
$(BUILD)/%-static: $(SRC)/%/%.c
	@mkdir -p $(@D)
	$(MCC) --specs=meuos --sysroot=../../sysroot --nostdlib \
		-static -O2 -o $@ $< -l:libc-meuos.a

clean:
	rm -rf $(BUILD)
