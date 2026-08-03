#!/usr/bin/env python3
"""
capture-pty.py — 用 Python PTY 运行 TUI 程序并捕获输出

替代 `script` 命令，在非交互环境（CI、bash 脚本）下可靠工作。

用法:
  capture-pty.py --program PATH [--output RAW] [--width N] [--height N] [--timeout N]

示例:
  capture-pty.py --program ./build/demo --output build/demo.raw --width 80 --height 30
"""

import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import termios
import time
import tty


def main():
    parser = argparse.ArgumentParser(description="Run TUI program in PTY and capture output")
    parser.add_argument("--program", required=True, help="TUI program to run")
    parser.add_argument("--output", required=True, help="Raw ANSI output file")
    parser.add_argument("--width", type=int, default=80)
    parser.add_argument("--height", type=int, default=30)
    parser.add_argument("--timeout", type=int, default=10, help="Max seconds to wait")
    parser.add_argument("--args", nargs="*", default=[], help="Additional arguments for the program")
    args = parser.parse_args()

    # 创建 PTY
    master_fd, slave_fd = pty.openpty()

    # 设置终端尺寸
    winsize = struct.pack("HHHH", args.height, args.width, 0, 0)
    fcntl.ioctl(slave_fd, termios.TIOCSWINSZ, winsize)

    # Fork 子进程
    pid = os.fork()
    if pid == 0:
        # 子进程
        os.close(master_fd)
        os.setsid()

        # 设置 slave 为控制终端
        tty.setraw(slave_fd)

        # 复制 stdio 到 slave
        os.dup2(slave_fd, 0)
        os.dup2(slave_fd, 1)
        os.dup2(slave_fd, 2)

        if slave_fd > 2:
            os.close(slave_fd)

        # 设置环境变量
        env = os.environ.copy()
        env["TERM"] = "xterm-256color"
        env["LINES"] = str(args.height)
        env["COLUMNS"] = str(args.width)
        env["TERMINAL_LINES"] = str(args.height)
        env["TERMINAL_COLS"] = str(args.width)

        # 执行程序
        try:
            os.execve(args.program, [args.program] + args.args, env)
        except OSError as e:
            print(f"exec failed: {e}", file=sys.stderr)
            sys.exit(1)

    # 父进程
    os.close(slave_fd)

    output = bytearray()
    start_time = time.time()

    try:
        while True:
            # 检查超时
            elapsed = time.time() - start_time
            if elapsed > args.timeout:
                break

            # 等待数据（最多 0.1s）
            try:
                r, _, _ = select.select([master_fd], [], [], 0.1)
            except (OSError, ValueError):
                break

            if master_fd in r:
                try:
                    data = os.read(master_fd, 4096)
                    if not data:
                        break
                    output.extend(data)
                except OSError:
                    break

            # 检查子进程是否退出
            try:
                wpid, status = os.waitpid(pid, os.WNOHANG)
                if wpid == pid:
                    # 子进程已退出，再读取一次残留数据
                    time.sleep(0.05)
                    try:
                        r, _, _ = select.select([master_fd], [], [], 0.1)
                        if master_fd in r:
                            data = os.read(master_fd, 4096)
                            if data:
                                output.extend(data)
                    except OSError:
                        pass
                    break
            except ChildProcessError:
                break

    finally:
        # 清理
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            os.close(master_fd)
        except OSError:
            pass

    # 写入文件
    with open(args.output, "wb") as f:
        f.write(output)

    if not output:
        print(f"⚠️  No output captured", file=sys.stderr)
        sys.exit(1)
    else:
        print(f"Captured {len(output)} bytes from {args.program}")


if __name__ == "__main__":
    main()
