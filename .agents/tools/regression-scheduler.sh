#!/usr/bin/env bash
#
# regression-scheduler.sh — 后台定时跑 verify-all + mcc 自举
#
# 设计：
# - 由 worker 在当前 worktree 内启动，日志写入 .regression-logs/
# - verify-all 每 4 小时一次，mcc 自举每 12 小时一次
# - 每次跑完后写一个时间戳标记文件 .regression-logs/last-<job>.ts
#   调度决策基于该标记文件的 mtime（不依赖 sleep 累积漂移）
# - 在 .regression-logs/ 下保留最近 14 份日志（避免膨胀）
#
# 用法（worker 内部）：
#   cd <worktree>
#   .agents/tools/regression-scheduler.sh start    # 启动后台调度
#   .agents/tools/regression-scheduler.sh status  # 查看最近结果
#   .agents/tools/regression-scheduler.sh stop     # 停止后台调度
#
# 退出码：成功=0；任何 verify-all / 自举失败 → 该 job 标记 FAILED

set -u

WT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LOG_DIR="$WT_ROOT/.regression-logs"
mkdir -p "$LOG_DIR"

VERIFY_INTERVAL=$((4 * 3600))      # 4 小时
BOOTSTRAP_INTERVAL=$((12 * 3600))  # 12 小时
KEEP_LOGS=14

PID_FILE="$LOG_DIR/.scheduler.pid"

cmd="${1:-help}"

start() {
	if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
		echo "scheduler already running (pid=$(cat "$PID_FILE"))"
		exit 0
	fi
	# 把变量以环境形式传给 setsid 内的 bash，避免引号嵌套导致变量未展开
	export LOG_DIR WT_ROOT VERIFY_INTERVAL BOOTSTRAP_INTERVAL KEEP_LOGS PID_FILE
	# setsid + & disown → 独立进程组，跟父 shell 解耦
	setsid bash -c '
		trap "echo \"[\$(date -Iseconds)] scheduler stopped\" >>\"$LOG_DIR\"/scheduler.log; rm -f \"$PID_FILE\"; exit 0" TERM INT
		while true; do
			now=$(date +%s)
			# verify-all
			if [ ! -f "$LOG_DIR/last-verify-all.ts" ] || \
			   [ $(( now - $(stat -c %Y "$LOG_DIR/last-verify-all.ts") )) -ge $VERIFY_INTERVAL ]; then
				echo "[verify-all] starting at $(date -Iseconds)" >> "$LOG_DIR/scheduler.log"
				if sh "$WT_ROOT/projects/mcc/test/verify-all.sh" --verbose > "$LOG_DIR/verify-all-latest.log" 2>&1; then
					echo "PASS" > "$LOG_DIR/last-verify-all.status"
				else
					echo "FAIL" > "$LOG_DIR/last-verify-all.status"
				fi
				touch "$LOG_DIR/last-verify-all.ts"
			fi
			# bootstrap
			if [ ! -f "$LOG_DIR/last-bootstrap.ts" ] || \
			   [ $(( now - $(stat -c %Y "$LOG_DIR/last-bootstrap.ts") )) -ge $BOOTSTRAP_INTERVAL ]; then
				echo "[bootstrap] starting at $(date -Iseconds)" >> "$LOG_DIR/scheduler.log"
				if make -C "$WT_ROOT/projects/mcc" check-sysroot-static > "$LOG_DIR/bootstrap-latest.log" 2>&1; then
					echo "PASS" > "$LOG_DIR/last-bootstrap.status"
				else
					echo "FAIL" > "$LOG_DIR/last-bootstrap.status"
				fi
				touch "$LOG_DIR/last-bootstrap.ts"
			fi
			# 滚动归档（保留最近 KEEP_LOGS 份）
			find "$LOG_DIR" -maxdepth 1 -name "verify-all-*.log" -type f | sort -r | tail -n +$((KEEP_LOGS + 1)) | xargs -r rm -f
			find "$LOG_DIR" -maxdepth 1 -name "bootstrap-*.log" -type f | sort -r | tail -n +$((KEEP_LOGS + 1)) | xargs -r rm -f
			sleep 600
		done
	' >/dev/null 2>&1 &
	disown $!
	echo $! > "$PID_FILE"
	sleep 0.3
	if kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
		echo "scheduler started (pid=$(cat "$PID_FILE"), log=$LOG_DIR/scheduler.log)"
	else
		echo "scheduler failed to start; see $LOG_DIR/scheduler.log" >&2
		exit 1
	fi
}

stop() {
	if [ -f "$PID_FILE" ]; then
		pid="$(cat "$PID_FILE")"
		if kill -0 "$pid" 2>/dev/null; then
			kill -TERM "$pid" 2>/dev/null || true
			sleep 0.5
			kill -KILL "$pid" 2>/dev/null || true
		fi
		rm -f "$PID_FILE"
		echo "scheduler stopped"
	else
		echo "no scheduler running"
	fi
}

status() {
	if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
		echo "scheduler: RUNNING (pid=$(cat "$PID_FILE"))"
	else
		echo "scheduler: STOPPED"
	fi
	echo "log dir: $LOG_DIR"
	for job in verify-all bootstrap; do
		if [ -f "$LOG_DIR/last-$job.ts" ]; then
			ts="$(stat -c %y "$LOG_DIR/last-$job.ts" | cut -d. -f1)"
			st="$(cat "$LOG_DIR/last-$job.status" 2>/dev/null || echo '?')"
			echo "  $job: $st  (last run: $ts)"
		else
			echo "  $job: never run"
		fi
	done
}

case "$cmd" in
	start) start ;;
	stop) stop ;;
	status) status ;;
	*) echo "usage: $0 {start|stop|status}" ; exit 2 ;;
esac