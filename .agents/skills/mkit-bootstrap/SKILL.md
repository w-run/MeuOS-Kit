---
name: "mkit-bootstrap"
description: "Orchestrates MeuOS Kit Phase 0-4 self-bootstrap with per-phase validation. Invoke when user asks to bootstrap, self-rebuild, run bootstrap.sh, verify a phase, or generate bootstrap-report.md."
---

# MeuOS Bootstrap Orchestrator

Drives the staged self-bootstrap of MeuOS Kit per AGENTS.md §3 and §6 Task 4. Each phase must validate before the next begins; on failure, halt and capture diagnostics.

## When to invoke

- User says "bootstrap", "self-rebuild", "build the kit", "run bootstrap.sh"
- User asks to verify Phase N (0-4) is complete
- User asks to generate / update `bootstrap-report.md` (run log) or `STATE.md` (curated status)
- User asks "what's the next step in the bootstrap chain"

## Prerequisites

- Working directory: `/workspace/MeuOS-Kit`
- Host CC on PATH: `gcc` preferred, `tcc` fallback
- `MEUOS_SYSROOT` env var set (default: `${PWD}/sysroot`)
- Reference trees present: `reference/cproc/`, `reference/qbe/`, `reference/musl/`
- `bootstrap.sh` exists at repo root (created by this skill if missing)

## Phases

### Phase 0 — Preparation

1. Detect `${HOST_CC}`: probe `gcc`, then `tcc`. Abort if neither found.
2. If `MEUOS_SYSROOT` unset, set to `${PWD}/sysroot`.
3. `mkdir -p ${MEUOS_SYSROOT}/{bin,lib,include,usr/bin,usr/lib}`.
4. **Validate**: `${HOST_CC} -v` exits 0; `${MEUOS_SYSROOT}` is writable.

### Phase 1 — Birth of mcc (host CC builds mcc)

1. `${HOST_CC}` builds `mcc/` source → produces `mcc` binary.
2. Install to `${MEUOS_SYSROOT}/usr/bin/mcc`.
3. **Validate**: `echo 'int main(){return 0;}' > /tmp/hello.c && mcc -o /tmp/hello /tmp/hello.c && /tmp/hello; echo $?` outputs `0`. (May link host libc for the host-side bootstrap binary only — this is the only exception.)

### Phase 2 - Birth of meuos-libc (mcc builds meuos-libc)

1. `mcc` builds `meuos-libc/` + `meuos-libc-compat/` source.
2. Install `.a`/`.so` to `${MEUOS_SYSROOT}/lib`, headers to `${MEUOS_SYSROOT}/include`.
3. Install `crt1.o` (and `crti.o`/`crtn.o` if present) to `${MEUOS_SYSROOT}/lib`.
4. **Validate**: Compile the AGENTS.md §6 Task 1 test program (`_Atomic int counter` + 2 threads × 1000 increments) with `mcc -specs=meuos -static -o /tmp/counter counter.c`, run on host, expect stdout `counter = 2000` and exit code 0.

### Phase 3 - Birth of meow (mcc + meuos-libc build meow)

1. `mcc -specs=meuos` builds `meow/meow.c` -> `meow` binary.
2. Install to `${MEUOS_SYSROOT}/usr/bin/meow`.
3. **Validate**: `meow build dash` reads `pkgs/dash/meow.yaml` and dispatches `fetch` + `unpack` steps. `configure`/`make` may fail (acceptable if host lacks deps), but YAML parsing and step dispatch must succeed.

### Phase 4 — Bootstrap Verification (chroot self-rebuild)

1. Enter `${MEUOS_SYSROOT}` via `chroot` (use `proot` or `bwrap` if chroot unavailable).
2. Inside chroot: rebuild `mcc`, `meuos-libc`, `meow` using sysroot's own `mcc` + `meow`.
3. Compare rebuilt binaries behaviorally against Phase 1-3 outputs. **Functional equivalence only - bit-identical is not required** (AGENTS.md §3 Phase 4).
4. **Validate**: All three rebuilds succeed; the §6 Task 1 counter test still produces `counter = 2000` with the rebuilt `mcc` + `meuos-libc`.

## bootstrap-report.md output

After each run (full or partial), write `bootstrap-report.md` at repo root (auto run log). The curated single source of truth is `STATE.md` — update its 「最近变更」 section.

```markdown
# MeuOS Kit Bootstrap Progress

Generated: <ISO timestamp>
Host CC: <path>
MEUOS_SYSROOT: <path>

## Phase 0 — Preparation [PASS|FAIL|SKIP]

...

## Phase 1 — Birth of mcc [PASS|FAIL|SKIP]

...

## Phase 2 - Birth of meuos-libc [PASS|FAIL|SKIP]

...

## Phase 3 — Birth of meow [PASS|FAIL|SKIP]

...

## Phase 4 — Bootstrap Verification [PASS|FAIL|SKIP]

...

## Next Steps

<concrete next action>
```

Each phase section must include: timestamp, exit status, validation command(s) and their output, any errors captured from stderr.

## Failure handling

- Halt immediately on first phase failure. Do not proceed.
- Capture: failing command, exit code, full stderr, last 50 lines of stdout.
- Write failure info to bootstrap-report.md and surface in chat; update STATE.md if status changed.
- Suggest remediation based on which sub-step failed (e.g. "Phase 2 link failure -> likely missing symbol in meuos-libc, check crt1.o exports \_start").

## Hard constraints (AGENTS.md §4) — never violate

- Never use `autotools`, `cmake`, or `meson` to build Kit components (only simple Makefile or shell scripts).
- Never link Kit binaries against glibc. Exception: Phase 1 host-bootstrap `mcc` may link host libc to bootstrap itself.
- All syscall wrappers in meuos-libc go through `syscall()` or inline asm, never through libc wrappers.
- No GPL/LGPL/LLVM copyrighted code in the tree. MIT only.
- No precompiled binaries committed (host bootstrap binary is the only exception).
