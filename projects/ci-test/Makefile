# ci-test - ci-driver 自动化能力测试项目
#
# 故意简单:用宿主 cc,不依赖 mcc/meuos-libc。目的是让 ci-driver 的
# main session 能在短时间内完成代码修改 + 验收,验证 round 循环 /
# CLAIM_DONE 协议 / fake-DONE 防御。
#
# todo 设计:
#   01-happy  → 创建 calc.c 实现 add(), 验收 cc -c 通过
#   02-partial → 加 mul() + div(), 验收只测 mul (partial 场景)
#   03-fail   → 验收命令故意 false (accept-fail 场景)

CC ?= cc
CFLAGS ?= -Wall -Werror -std=c11

check: src/calc.c
	$(CC) $(CFLAGS) -fsyntax-only src/calc.c
	@echo "ci-test check: PASS"

clean:
	rm -f src/calc.c src/fail.c

.PHONY: check clean
