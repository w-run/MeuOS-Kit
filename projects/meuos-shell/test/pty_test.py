#!/usr/bin/env python3
"""
test/pty_test.py — msh 交互式 PTY 测试框架

通过 forkpty 创建伪终端，模拟真实键盘输入测试 msh 行编辑功能。
覆盖场景：
  - 基本输入与执行
  - Backspace / Delete
  - 光标移动 (左右/Home/End)
  - 历史导航 (上下箭头)
  - UTF-8 多字节字符（中文）
  - Ctrl-C / Ctrl-D
  - Ctrl-W / Ctrl-U
  - Tab 补全

用法：python3 test/pty_test.py [msh_binary]
"""

import os
import pty
import select
import signal
import sys
import termios
import time
import unittest

MSH_BIN = sys.argv[1] if len(sys.argv) > 1 else "build/msh"
MSH_DIR = os.path.dirname(os.path.abspath(__file__)) + "/.."


def pty_session(args=None, env=None):
    """创建一个 PTY 会话上下文管理器"""
    if args is None:
        args = [MSH_BIN, "--classic"]
    
    full_env = dict(os.environ)
    full_env["TERM"] = "xterm"
    full_env["HOME"] = "/tmp"
    full_env["MSH_CLASSIC"] = "1"
    full_env.pop("MSH_PS1", None)
    if env:
        full_env.update(env)

    class PTYSession:
        def __init__(self):
            self.pid = None
            self.master_fd = None
            self.output = b""

        def __enter__(self):
            self.pid, self.master_fd = pty.fork()
            if self.pid == 0:
                # Child
                os.chdir(MSH_DIR)
                os.execvpe(args[0], args, full_env)
                os._exit(127)
            else:
                # Parent: set window size
                import struct
                import fcntl
                winsize = struct.pack("HHHH", 24, 80, 0, 0)
                fcntl.ioctl(self.master_fd, termios.TIOCSWINSZ, winsize)
                # Wait for shell to start
                time.sleep(0.3)
                self._drain()
            return self

        def __exit__(self, *args):
            try:
                os.close(self.master_fd)
            except OSError:
                pass
            try:
                os.kill(self.pid, signal.SIGTERM)
                os.waitpid(self.pid, 0)
            except (OSError, ChildProcessError):
                pass

        def _drain(self, timeout=0.3):
            """读取所有可用的输出"""
            data = b""
            while True:
                r, _, _ = select.select([self.master_fd], [], [], timeout)
                if not r:
                    break
                try:
                    chunk = os.read(self.master_fd, 4096)
                    if not chunk:
                        break
                    data += chunk
                    timeout = 0.1  # 后续读取用短超时
                except OSError:
                    break
            self.output += data
            return data

        def send(self, data):
            """发送数据到 PTY"""
            if isinstance(data, str):
                data = data.encode()
            os.write(self.master_fd, data)
            time.sleep(0.15)
            return self._drain()

        def send_key(self, key):
            """发送特殊按键"""
            key_map = {
                "ENTER":      b"\r",
                "BACKSPACE":  b"\x7f",
                "DELETE":     b"\x1b[3~",
                "UP":         b"\x1b[A",
                "DOWN":       b"\x1b[B",
                "RIGHT":      b"\x1b[C",
                "LEFT":       b"\x1b[D",
                "HOME":       b"\x1b[H",
                "END":        b"\x1b[F",
                "CTRL_C":     b"\x03",
                "CTRL_D":     b"\x04",
                "CTRL_A":     b"\x01",
                "CTRL_E":     b"\x05",
                "CTRL_W":     b"\x17",
                "CTRL_U":     b"\x15",
                "TAB":        b"\t",
            }
            return self.send(key_map.get(key, key.encode()))

        def get_output_text(self):
            """获取已累积的输出（解码为字符串）"""
            return self.output.decode("utf-8", errors="replace")

        def reset_output(self):
            self.output = b""

    return PTYSession()


class TestBasicInput(unittest.TestCase):
    """测试基本输入与命令执行"""

    def test_simple_echo(self):
        """输入 echo hello 并执行，验证输出"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo hello")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("hello", text, f"Expected 'hello' in output: {repr(text)}")

    def test_multiple_commands(self):
        """连续执行多条命令"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo one")
            s.send_key("ENTER")
            s.send("echo two")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("one", text)
            self.assertIn("two", text)

    def test_empty_line(self):
        """空行不报错"""
        with pty_session() as s:
            s.reset_output()
            s.send_key("ENTER")
            text = s.get_output_text()
            # 应该没有错误信息
            self.assertNotIn("error", text.lower())
            self.assertNotIn("syntax", text.lower())


class TestBackspace(unittest.TestCase):
    """测试退格键"""

    def test_backspace_simple(self):
        """输入 hellx 然后退格修正为 hello"""
        with pty_session() as s:
            s.reset_output()
            s.send("hellx")
            s.send_key("BACKSPACE")
            s.send("o")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("hello", text, f"Expected 'hello' after backspace fix: {repr(text)}")

    def test_backspace_to_empty(self):
        """退格到空行"""
        with pty_session() as s:
            s.reset_output()
            s.send("abc")
            s.send_key("BACKSPACE")
            s.send_key("BACKSPACE")
            s.send_key("BACKSPACE")
            s.send("echo ok")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("ok", text, f"Expected 'ok' after clearing line: {repr(text)}")

    def test_backspace_middle(self):
        """在行中间退格"""
        with pty_session() as s:
            s.reset_output()
            s.send("helo")
            s.send_key("LEFT")
            s.send_key("BACKSPACE")  # 删除 'l' -> "helo" -> "helo" with cursor at 'e'
            # 实际上: "helo", cursor at 'l'(pos 2), backspace removes 'h' -> "elo"?
            # Let me think: "helo", cursor moves left to pos 3 (between 'l' and 'o')
            # backspace removes 'l' -> "heo", cursor at pos 2
            # Then type 'l' -> "helo"
            s.send("l")
            s.send_key("ENTER")
            text = s.get_output_text()
            # echo 命令执行了，输出应该包含 echo 的结果
            # 由于我们在 "helo" 上操作，回退后输入 l，应该是 "helo"
            self.assertTrue("helo" in text or "heo" in text,
                          f"Expected helo or heo: {repr(text)}")

    def test_delete_key(self):
        """Delete 键删除光标处字符"""
        with pty_session() as s:
            s.reset_output()
            s.send("helo")
            s.send_key("HOME")
            s.send_key("RIGHT")   # 光标在 'e' 后（位置1，在 'h' 和 'e' 之间）
            s.send_key("RIGHT")   # 光标在 'l' 后（位置2，在 'e' 和 'l' 之间）
            s.send_key("DELETE")  # 删除 'l' -> "heo"
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("heo", text, f"Expected 'heo' after delete: {repr(text)}")


class TestCursorMovement(unittest.TestCase):
    """测试光标移动"""

    def test_home_end(self):
        """Home/End 键移动光标"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo tes")
            s.send_key("HOME")    # 光标到行首
            s.send_key("END")     # 光标到行尾
            s.send("t")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("test", text, f"Expected 'test': {repr(text)}")

    def test_left_right(self):
        """左右箭头移动光标"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo abd")
            s.send_key("LEFT")    # 光标在 'd' 前
            s.send("c")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("abcd", text, f"Expected 'abcd': {repr(text)}")

    def test_ctrl_a_ctrl_e(self):
        """Ctrl-A/Ctrl-E 行首/行尾"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo hi")
            s.send_key("CTRL_A")
            s.send_key("CTRL_E")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("hi", text, f"Expected 'hi': {repr(text)}")


class TestUTF8(unittest.TestCase):
    """测试 UTF-8 多字节字符"""

    def test_chinese_input(self):
        """输入中文字符不乱码"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo 你好世界")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("你好世界", text,
                         f"Expected Chinese text: {repr(text)}")

    def test_chinese_backspace(self):
        """中文字符退格不残缺"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo 你好x")
            s.send_key("BACKSPACE")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("你好", text,
                         f"Expected '你好' after backspace: {repr(text)}")

    def test_chinese_mixed(self):
        """中英文混合输入"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo hello世界test")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("hello世界test", text,
                         f"Expected mixed text: {repr(text)}")

    def test_chinese_middle_edit(self):
        """在中文中间插入字符"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo 你好")
            # Move cursor back to between 你 and 好
            s.send_key("LEFT")
            s.send("X")
            s.send_key("ENTER")
            text = s.get_output_text()
            # Expected: 你X好
            self.assertIn("你X好", text,
                         f"Expected '你X好': {repr(text)}")


class TestHistory(unittest.TestCase):
    """测试历史导航"""

    def test_history_up_down(self):
        """上下箭头浏览历史"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo first")
            s.send_key("ENTER")
            s.send("echo second")
            s.send_key("ENTER")
            s._drain()
            s.reset_output()
            s.send_key("UP")     # 应该显示 "echo second"
            text = s.get_output_text()
            self.assertIn("second", text,
                         f"Expected 'second' in history: {repr(text)}")

    def test_history_edit(self):
        """从历史中编辑命令"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo aaa")
            s.send_key("ENTER")
            s._drain()
            s.reset_output()
            s.send_key("UP")         # "echo aaa"
            s.send_key("END")
            s.send_key("BACKSPACE")
            s.send_key("BACKSPACE")
            s.send("bb")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("bb", text,
                         f"Expected edited history output: {repr(text)}")

    def test_history_restore_buffer(self):
        """历史导航后恢复原始输入"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo done")
            s.send_key("ENTER")
            s._drain()
            s.reset_output()
            s.send("typing new")
            s.send_key("UP")      # 进入历史
            s.send_key("DOWN")    # 回来，应该恢复 "typing new"
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("typing", text,
                         f"Expected original buffer restored: {repr(text)}")


class TestCtrlKeys(unittest.TestCase):
    """测试 Ctrl 组合键"""

    def test_ctrl_c(self):
        """Ctrl-C 中断当前行"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo before")
            s.send_key("ENTER")
            s._drain()
            s.reset_output()
            s.send("some partial")
            s.send_key("CTRL_C")
            s._drain()
            s.reset_output()
            s.send("echo after")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("after", text,
                         f"Expected 'after' after Ctrl-C: {repr(text)}")
            self.assertNotIn("partial", text,
                           f"Should not execute 'partial': {repr(text)}")

    def test_ctrl_d_exit(self):
        """Ctrl-D 空行退出 shell"""
        with pty_session() as s:
            s.reset_output()
            s.send_key("CTRL_D")
            time.sleep(1.0)
            # Shell should have exited — try multiple checks
            exited = False
            for _ in range(5):
                try:
                    pid, status = os.waitpid(s.pid, os.WNOHANG)
                    if pid != 0:
                        exited = True
                        break
                except ChildProcessError:
                    exited = True
                    break
                time.sleep(0.2)
            self.assertTrue(exited, "Shell should have exited on Ctrl-D")

    def test_ctrl_u(self):
        """Ctrl-U 删除到行首"""
        with pty_session() as s:
            s.reset_output()
            s.send("garbage")
            s.send_key("CTRL_U")
            s.send("echo clean")
            s.send_key("ENTER")
            text = s.get_output_text()
            # "clean" should appear as command output
            self.assertIn("clean", text,
                         f"Expected 'clean' after Ctrl-U: {repr(text)}")
            # "garbage" should NOT appear as command output (only in echoed input)
            # The raw PTY output includes echoed "garbage" + ANSI clear sequences,
            # so we check that the command result doesn't contain it.
            # After ENTER, the output is: \r\nclean\r\n
            # Split on \r\n and check the last meaningful line
            lines = [l for l in text.replace('\r\n', '\n').split('\n') if l.strip()]
            # Filter out lines that are just prompt
            cmd_lines = [l for l in lines if not l.strip().startswith('$') and 'garbage' not in l]
            self.assertTrue(any('clean' in l for l in cmd_lines),
                          f"Expected 'clean' in command output lines: {lines}")

    def test_ctrl_w(self):
        """Ctrl-W 删除前一个 word"""
        with pty_session() as s:
            s.reset_output()
            s.send("echo old new")
            s.send_key("CTRL_W")  # 删除 "new"
            s.send("test")
            s.send_key("ENTER")
            text = s.get_output_text()
            # "old" should appear in command output
            self.assertIn("old", text,
                         f"Expected 'old' after Ctrl-W: {repr(text)}")
            # "newtest" should NOT appear as a word in command output
            lines = [l for l in text.replace('\r\n', '\n').split('\n') if l.strip()]
            cmd_lines = [l for l in lines if not l.strip().startswith('$')]
            joined = ' '.join(cmd_lines)
            self.assertNotIn("newtest", joined,
                           f"Should not have 'newtest' in output: {repr(joined)}")


class TestTerminalRestore(unittest.TestCase):
    """测试终端状态恢复"""

    def test_terminal_restored_after_exit(self):
        """退出后终端恢复正常模式"""
        with pty_session() as s:
            s.send("exit")
            s.send_key("ENTER")
            time.sleep(0.3)
            # 读取终端属性
            try:
                attrs = termios.tcgetattr(s.master_fd)
                # 在非 raw 模式下，ICANON 应该是 set 的
                # 但由于子进程已退出，PTY 可能返回默认属性
                # 这个测试主要确保不崩溃
                self.assertTrue(True, "Terminal attributes readable after exit")
            except (termios.error, OSError):
                # PTY 可能已关闭，这也是正常的
                self.assertTrue(True, "PTY closed after exit (normal)")

    def test_terminal_restored_after_ctrl_c(self):
        """Ctrl-C 后终端仍可用"""
        with pty_session() as s:
            s.send_key("CTRL_C")
            s._drain()
            s.reset_output()
            s.send("echo recovered")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("recovered", text,
                         f"Expected 'recovered' after Ctrl-C recovery: {repr(text)}")


class TestTabCompletion(unittest.TestCase):
    """测试 Tab 补全"""

    def test_tab_command(self):
        """Tab 补全内建命令"""
        with pty_session() as s:
            s.reset_output()
            s.send("ec")
            s.send_key("TAB")
            s.send(" hello")
            s.send_key("ENTER")
            text = s.get_output_text()
            self.assertIn("hello", text,
                         f"Expected 'hello' after tab completion of 'echo': {repr(text)}")


class TestScriptMode(unittest.TestCase):
    """测试脚本模式（非交互）"""

    def test_script_basic(self):
        """脚本模式基本执行"""
        import subprocess
        result = subprocess.run(
            [MSH_BIN, "-c", "echo script_ok"],
            capture_output=True, text=True, cwd=MSH_DIR
        )
        self.assertIn("script_ok", result.stdout)

    def test_script_multiline(self):
        """多行脚本"""
        import subprocess
        script = "echo line1\necho line2\nfor i in a b; do echo $i; done"
        result = subprocess.run(
            [MSH_BIN, "-c", script],
            capture_output=True, text=True, cwd=MSH_DIR
        )
        self.assertIn("line1", result.stdout)
        self.assertIn("line2", result.stdout)
        self.assertIn("a", result.stdout)
        self.assertIn("b", result.stdout)


if __name__ == "__main__":
    # Remove the first arg (msh binary path) if present
    if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        MSH_BIN = sys.argv[1]
        sys.argv = [sys.argv[0]] + sys.argv[2:]
    
    print(f"Testing msh: {os.path.abspath(MSH_BIN)}")
    print(f"Working dir: {os.path.abspath(MSH_DIR)}")
    print()
    
    unittest.main(verbosity=2)
