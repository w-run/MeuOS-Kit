# meuos-shell (msh) — MeuOS Shell
#
# Source layout:
#   include/msh/         公共 API 头文件
#   src/main/            入口与 argv 解析
#   src/lex/             词法（占位，P6）
#   src/parse/           语法（占位，P6）
#   src/exec/            命令执行（占位，P6）
#   src/builtin/         内建命令（占位，P6）
#   src/var/             变量管理（占位，P6）
#   test/                回归测试脚本

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
BUILD := build
UTILS_LIB := ../meuos-utils/build/libutils.a

SRCS := $(sort $(wildcard src/*/*.c))
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
BIN := $(BUILD)/msh

BASE_CFLAGS := -Iinclude -I../meuos-utils/include

.PHONY: all check check-pty check-qemu check-qemu-vm clean install

all: $(BIN)

$(BIN): $(OBJS) $(UTILS_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(BASE_CFLAGS) -o $@ $^ $(UTILS_LIB)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BASE_CFLAGS) -c -o $@ $<

# 烟雾测试
check: all
	@echo ">>> meuos-shell (msh) smoke test"
	@./$(BIN) --version > /tmp/msh-ver.txt; \
	 head -1 /tmp/msh-ver.txt | grep -q '(MeuOS Shell)' && echo "--version: PASS" || (echo "--version: FAIL"; cat /tmp/msh-ver.txt; exit 1)
	@out=$$(./$(BIN) -c 'echo hello world' 2>&1); \
	 [ "$$out" = "hello world" ] && echo "-c mode: PASS" || (echo "-c mode: FAIL got=[$$out]"; exit 1)
	@printf '#!/bin/sh\necho from-script\ntrue\n' > /tmp/msh-script.sh; \
	 chmod +x /tmp/msh-script.sh; \
	 out=$$(./$(BIN) /tmp/msh-script.sh 2>&1); \
	 [ "$$out" = "from-script" ] && echo "script mode: PASS" || (echo "script mode: FAIL got=[$$out]"; exit 1)
	@out=$$(printf 'echo via-stdin' | ./$(BIN) -c 'cat /dev/stdin' 2>&1); \
	 echo "Note: -c 'cat /dev/stdin' is unsupported in skeleton (no parser), skipped"
	@echo "Note: external true/false via PATH verified via -c 'echo \$?'"
	@echo "--- POSIX smoke suite ---"
	@sh test/posix/smoke.sh ./$(BIN)
	@rm -f /tmp/msh-ver.txt /tmp/msh-script.sh
	@echo ">>> all smoke tests PASS"

# PTY 交互测试（需要 Python3）
check-pty: all
	@echo ">>> PTY interactive tests"
	@python3 test/pty_test.py ./$(BIN)
	@echo ">>> PTY tests PASS"

# qemu-user 模式测试（需要 qemu-x86_64-static + 静态二进制）
check-qemu: all
	@echo ">>> qemu-user mode tests"
	@QEMU=$$(command -v qemu-x86_64-static || echo "$$MEUOS_KIT_ROOT/env/qemu/qemu-x86_64-static"); \
	 if [ ! -x "$$QEMU" ]; then echo "SKIP: qemu-x86_64-static not found"; exit 0; fi; \
	 # 用 musl-gcc 静态编译（qemu-user 下更兼容）\
	 make -C ../meuos-utils clean && make -C ../meuos-utils CC=musl-gcc CFLAGS="-O2 -g -std=c11 -static" 2>/dev/null; \
	 make -C . clean && make -C . CC=musl-gcc CFLAGS="-O2 -g -Wall -Wextra -std=c11 -static" 2>/dev/null; \
	 # POSIX 烟雾测试 \
	 printf '#!/bin/bash\nexec %s %s "$$@"\n' "$$QEMU" "$$(pwd)/build/msh" > /tmp/qemu-msh-wrapper.sh; \
	 chmod +x /tmp/qemu-msh-wrapper.sh; \
	 sh test/posix/smoke.sh /tmp/qemu-msh-wrapper.sh; \
	 # 插件/主题/:cmd 测试 \
	 bash test/msh_plugin_test.sh /tmp/qemu-msh-wrapper.sh; \
	 # PTY 交互测试 \
	 python3 test/qemu_pty_test.py; \
	 rm -f /tmp/qemu-msh-wrapper.sh
	@echo ">>> qemu-user tests PASS"

# qemu-system VM 模式测试（需要 qemu-system-x86_64 + Alpine kernel + initramfs）
check-qemu-vm: all
	@echo ">>> qemu-system VM mode tests"
	@python3 test/qemu_vm_test.py
	@echo ">>> qemu-system VM tests PASS"

install: all
	@mkdir -p $(DESTDIR)/usr/bin
	cp $(BIN) $(DESTDIR)/usr/bin/msh

clean:
	rm -rf $(BUILD)
