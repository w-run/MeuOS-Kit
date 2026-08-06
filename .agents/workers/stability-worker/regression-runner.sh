#!/usr/bin/env bash
#
# stability-worker regression runner
#
# Runs the same gates as .github/workflows/stability-regression.yml:
#   - verify-all (24/25-test gate)
#   - self-host  (mcc compiles mcc, runs hello)
#
# Records every run to .agents/workers/stability-worker/regression-logs/
# with timestamp + exit code + summary. Designed for use under cron,
# systemd timer, or manual `bash regression-runner.sh all` invocation.
#
# Schedule (recommended):
#   0 */4 * * *  cd <repo> && bash .agents/workers/stability-worker/regression-runner.sh verify-all
#   30 */12 * * * cd <repo> && bash .agents/workers/stability-worker/regression-runner.sh self-host
#
# Usage:
#   bash regression-runner.sh verify-all     # 24/25-test gate
#   bash regression-runner.sh self-host      # check-sysroot-static
#   bash regression-runner.sh all            # both, sequential
#
# Why: GitHub Actions runs in the cloud; this gives local worktree a
# parallel capture path during agent working sessions and offline runs.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT" || exit 1

MODE="${1:-all}"
LOGDIR="$ROOT/.agents/workers/stability-worker/regression-logs"
mkdir -p "$LOGDIR"

if [ -z "${MEUOS_SYSROOT:-}" ]; then
    export MEUOS_SYSROOT="$ROOT/sysroot"
fi
export MEUOS_SYSROOT

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
SHORT_HEAD="$(git rev-parse --short HEAD)"
SUMMARY="$LOGDIR/summary.log"

fail_count=0

run_one() {
    local label="$1"
    local logfile="$LOGDIR/${label}-${STAMP}.log"
    echo "[$STAMP] $label starting (HEAD=$SHORT_HEAD, mode=$MODE)"
    local t0 t1
    t0=$(date +%s)
    case "$label" in
        verify-all)
            if sh projects/mcc/test/verify-all.sh > "$logfile" 2>&1; then
                local pass_fail_skip
                pass_fail_skip=$(grep -E "^汇总:" "$logfile" | tail -1 || echo "(no summary line)")
                echo "[$STAMP] $label PASS $pass_fail_skip"
                echo "[$STAMP] $label PASS $pass_fail_skip  log=$logfile" >> "$SUMMARY"
            else
                local rc=$?
                local pass_fail_skip
                pass_fail_skip=$(grep -E "^汇总:" "$logfile" | tail -1 || echo "(no summary line)")
                echo "[$STAMP] $label FAIL ($rc) $pass_fail_skip"
                echo "[$STAMP] $label FAIL ($rc) $pass_fail_skip  log=$logfile" >> "$SUMMARY"
                fail_count=$((fail_count + 1))
            fi
            ;;
        self-host)
            if make -C projects/mcc check-sysroot-static > "$logfile" 2>&1; then
                echo "[$STAMP] $label PASS"
                echo "[$STAMP] $label PASS  log=$logfile" >> "$SUMMARY"
            else
                local rc=$?
                echo "[$STAMP] $label FAIL ($rc)"
                echo "[$STAMP] $label FAIL ($rc)  log=$logfile" >> "$SUMMARY"
                fail_count=$((fail_count + 1))
            fi
            ;;
    esac
    t1=$(date +%s)
    echo "[$STAMP] $label done in $((t1 - t0))s, fail_count=$fail_count"
}

case "$MODE" in
    verify-all) run_one verify-all ;;
    self-host)  run_one self-host  ;;
    all)
        run_one verify-all
        run_one self-host
        ;;
    *)
        echo "Usage: $0 {verify-all|self-host|all}" >&2
        exit 2
        ;;
esac

exit $fail_count
