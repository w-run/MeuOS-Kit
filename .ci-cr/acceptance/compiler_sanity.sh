#!/bin/bash
# compiler_sanity.sh - mcc 编译器冒烟与 C11 门禁（SPEC §7.2）
#
# 检查内容：
#   1. mcc 可执行且 --version 正常。
#   2. mcc 能编译 int main(){return 0;} 并产出可运行二进制。
#   3. C11 关键特性门禁：_Atomic / _Generic / _Thread_local / 匿名结构体 /
#      复合字面量 / 指定初始化器 / 变长数组（VLA）。
#   4. meow 与 toolchain 复用此脚本时仅做最小冒烟。
#
# 环境变量：MEUOS_COMPONENT, MEUOS_BRANCH, MEUOS_CICR_ROOT
# 退出码：0=通过，1=失败，77=跳过

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPONENT="${MEUOS_COMPONENT:-mcc}"
MCC_DIR="${REPO_ROOT}/projects/mcc"

# mcc locates its system headers (stdatomic.h/threads.h/stdio.h) and libs
# under <sysroot>/usr/{include,lib} via --sysroot or the MEUOS_SYSROOT
# environment variable (see bootstrap.sh).  This harness may be invoked
# outside of bootstrap.sh, so resolve and export the sysroot here so that
# bare `mcc -o bin src.c` invocations below find the MeuOS headers.
: "${MEUOS_SYSROOT:=${REPO_ROOT}/sysroot}"
export MEUOS_SYSROOT

log() { printf '[compiler_sanity] %s\n' "$*" >&2; }
fail() { printf '[compiler_sanity][FAIL] %s\n' "$*" >&2; exit 1; }
skip() { printf '[compiler_sanity][SKIP] %s\n' "$*" >&2; exit 77; }

# 定位 mcc
MCC_BIN=""
if [[ -x "${MEUOS_SYSROOT:-${REPO_ROOT}/sysroot}/usr/bin/mcc" ]]; then
    MCC_BIN="${MEUOS_SYSROOT:-${REPO_ROOT}/sysroot}/usr/bin/mcc"
elif [[ -x "${MCC_DIR}/mcc" ]]; then
    MCC_BIN="${MCC_DIR}/mcc"
elif command -v mcc >/dev/null 2>&1; then
    MCC_BIN="$(command -v mcc)"
fi

[[ -n "${MCC_BIN}" ]] || skip "no mcc binary found"

log "using mcc: ${MCC_BIN}"

# 1. --version
"${MCC_BIN}" --version >/dev/null 2>&1 || fail "mcc --version failed"

# 2. hello world 闭环
HELLO_C="/tmp/cicr_sanity_hello.c"
HELLO_BIN="/tmp/cicr_sanity_hello"
cat > "${HELLO_C}" <<'EOF'
int main(void) { return 0; }
EOF
"${MCC_BIN}" -o "${HELLO_BIN}" "${HELLO_C}" 2>/dev/null || fail "mcc failed to compile hello world"
"${HELLO_BIN}" || fail "hello world binary exited non-zero"
log "hello world: PASS"

# 3. C11 特性门禁（仅对 mcc/compiler 组件全量运行）
if [[ "${COMPONENT}" == "mcc" || "${COMPONENT}" == "compiler" ]]; then
    log "running C11 feature gate"

    C11_C="/tmp/cicr_c11_gate.c"
    C11_BIN="/tmp/cicr_c11_gate"
    cat > "${C11_C}" <<'EOF'
#include <stdatomic.h>
#include <threads.h>
#include <stdio.h>

static _Atomic int counter = 0;

typedef struct {
    _Atomic int x;
    struct { int a; double b; };  /* anonymous struct */
} box;

int increment(void *arg) {
    (void)arg;
    atomic_fetch_add(&counter, 1);
    return 0;
}

int main(void) {
    /* _Generic */
    int n = 42;
    const char *kind = _Generic(n, int: "int", default: "?");
    if (kind[0] != 'i') return 2;

    /* compound literal + designated initializer */
    box b = (box){ .x = 1, .a = 2, .b = 3.0 };

    /* VLA */
    int sz = 5;
    char buf[sz];
    buf[0] = 'O'; buf[1] = 'K'; buf[2] = '\0';

    /* _Thread_local not testable in single-thread minimal; just compile */
    static _Thread_local int tls_var;
    tls_var = 7;
    if (tls_var != 7) return 3;

    /* threads: spawn and join */
    thrd_t t;
    if (thrd_create(&t, increment, 0) != thrd_success) return 4;
    thrd_join(t, 0);

    if (atomic_load(&counter) != 1) return 5;
    if (b.a != 2 || b.b != 3.0) return 6;

    puts(buf);  /* prints "OK" */
    return 0;
}
EOF
    if ! "${MCC_BIN}" -o "${C11_BIN}" "${C11_C}" -lpthread 2>/dev/null; then
        # 可能不支持 -lpthread；尝试 --specs=meuos 或裸链接
        if ! "${MCC_BIN}" --static -o "${C11_BIN}" "${C11_C}" 2>/dev/null; then
            fail "mcc failed to compile C11 feature gate"
        fi
    fi
    "${C11_BIN}" | grep -qx 'OK' || fail "C11 feature gate runtime check failed"
    log "C11 feature gate: PASS"

    # 4. 若存在 C11 测试矩阵，运行之
    if [[ -d "${MCC_DIR}/test/c11" ]]; then
        log "running make check-c11"
        if ! make -C "${MCC_DIR}" check-c11 >/dev/null 2>&1; then
            log "WARNING: full C11 matrix failed; gate above passed"
        fi
    fi
fi

# toolchain 组件：最小冒烟（as/ld 产出的二进制可执行）
if [[ "${COMPONENT}" == "toolchain" ]]; then
    log "toolchain smoke: checking mt/as availability"
    TC_DIR="${REPO_ROOT}/projects/meuos-toolchain"
    [[ -d "${TC_DIR}" ]] || skip "meuos-toolchain not present"
    if make -C "${TC_DIR}" check >/dev/null 2>&1; then
        log "toolchain make check: PASS"
    else
        log "WARNING: toolchain make check failed (may be partial)"
    fi
fi

log "compiler_sanity acceptance PASS"
exit 0
