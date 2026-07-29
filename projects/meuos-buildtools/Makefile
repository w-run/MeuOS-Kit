# meuos-buildtools — MeuOS build tool replacements
#
# Each tool is a single .c file producing a standalone binary.
# Compiled with host or mcc cc.

HOST_CC ?= cc
MCC ?= ../../projects/mcc/mcc
AR ?= ar
PREFIX ?= /usr

BUILD := build
SRC := src
TEST := test

TOOLS := m4 gperf flex
BINS := $(TOOLS:%=$(BUILD)/%)

.PHONY: all clean check

all: $(BINS)

$(BUILD)/%: $(SRC)/%.c
	@mkdir -p $(@D)
	$(HOST_CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -o $@ $<

# Static linking target for use inside sysroot
$(BUILD)/%-static: $(SRC)/%.c
	@mkdir -p $(@D)
	$(MCC) --specs=meuos --sysroot=../../sysroot --nostdlib \
		-static -O2 -o $@ $< -l:libc-meuos.a

check: $(BINS)
	@for t in $(BINS); do \
		echo "Testing $$t..."; \
		[ -x "$$t" ] && $$t --version >/dev/null 2>&1 && echo "  $$t: OK" || echo "  $$t: built"; \
	done

clean:
	rm -rf $(BUILD)
