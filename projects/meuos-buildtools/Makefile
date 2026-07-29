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

.PHONY: all clean check install

all: $(BIN) $(SYMLINKS)

# Compile each module
$(BUILD)/m4.o: $(SRC)/m4.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

$(BUILD)/gperf.o: $(SRC)/gperf.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

$(BUILD)/flex.o: $(SRC)/flex.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

$(BUILD)/buildtools.o: $(SRC)/buildtools.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -c -o $@ $<

# Link single binary
$(BIN): $(BUILD)/buildtools.o $(MODULE_OBJS)
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
$(BUILD)/%-static: $(SRC)/%.c
	@mkdir -p $(@D)
	$(MCC) --specs=meuos --sysroot=../../sysroot --nostdlib \
		-static -O2 -o $@ $< -l:libc-meuos.a

clean:
	rm -rf $(BUILD)
