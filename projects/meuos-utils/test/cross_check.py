#!/usr/bin/env python3
"""
test/cross_check.py — meuos-utils 与 GNU 工具行为交叉验证

对每个工具生成多组测试输入，分别用 meuos-utils 和 GNU 工具执行，
对比 stdout 输出和退出码，发现行为差异。

用法：python3 test/cross_check.py [build_dir]
默认 build_dir = ../build
"""

import os
import subprocess
import sys
import tempfile
import unittest

BUILD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build")
if len(sys.argv) > 1:
    BUILD_DIR = os.path.abspath(sys.argv[1])
    sys.argv = [sys.argv[0]] + sys.argv[2:]


def run_tool(tool, args, stdin_data=None, use_gnu=False):
    """运行工具，返回 (stdout, stderr, returncode)"""
    if use_gnu:
        # 使用系统 GNU 工具
        cmd = [tool]
    else:
        cmd = [os.path.join(BUILD_DIR, tool)]
        # 只有部分工具有 --classic 模式（现代 UX 工具），其余默认即为 POSIX 行为
        CLASSIC_TOOLS = {"ls", "cat", "cp", "mv", "rm", "find", "grep",
                         "diff", "tree", "stat", "chmod"}
        if tool in CLASSIC_TOOLS:
            cmd.append("--classic")

    cmd.extend(args)

    try:
        result = subprocess.run(
            cmd,
            input=stdin_data,
            capture_output=True,
            text=True,
            timeout=10
        )
        return result.stdout, result.stderr, result.returncode
    except FileNotFoundError:
        return None, None, -1
    except subprocess.TimeoutExpired:
        return None, None, -2


def cross_check(test_case, tool, args, stdin_data=None, ignore_rc=False):
    """交叉对比：我们的工具 vs GNU 工具"""
    our_out, our_err, our_rc = run_tool(tool, args, stdin_data)
    gnu_out, gnu_err, gnu_rc = run_tool(tool, args, stdin_data, use_gnu=True)

    if gnu_out is None:
        test_case.skipTest(f"GNU {tool} not available")

    if not ignore_rc:
        test_case.assertEqual(
            our_rc, gnu_rc,
            f"{tool} {args}: rc mismatch (ours={our_rc}, gnu={gnu_rc})\n"
            f"  our stderr: {our_err}\n"
            f"  gnu stderr: {gnu_err}"
        )

    test_case.assertEqual(
        our_out, gnu_out,
        f"{tool} {args}: stdout mismatch\n"
        f"  ours: {repr(our_out)}\n"
        f"  gnu:  {repr(gnu_out)}"
    )


class TestEcho(unittest.TestCase):
    def test_echo_basic(self):
        cross_check(self, "echo", ["hello", "world"])

    def test_echo_n(self):
        cross_check(self, "echo", ["-n", "no", "newline"])

    def test_echo_e(self):
        cross_check(self, "echo", ["-e", "a\\tb"])

    def test_echo_empty(self):
        cross_check(self, "echo", [])

    def test_echo_special(self):
        cross_check(self, "echo", ["hello!", "world?"])


class TestCat(unittest.TestCase):
    def test_cat_stdin(self):
        cross_check(self, "cat", [], stdin_data="hello\nworld\n")

    def test_cat_empty(self):
        cross_check(self, "cat", [], stdin_data="")

    def test_cat_file(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
            f.write("line1\nline2\nline3\n")
            f.flush()
            cross_check(self, "cat", [f.name])
            os.unlink(f.name)

    def test_cat_multiple(self):
        f1 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f2 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f1.write("aaa\n"); f1.flush(); f1.close()
        f2.write("bbb\n"); f2.flush(); f2.close()
        cross_check(self, "cat", [f1.name, f2.name])
        os.unlink(f1.name); os.unlink(f2.name)


class TestHead(unittest.TestCase):
    def test_head_default(self):
        data = "\n".join(f"line{i}" for i in range(20)) + "\n"
        cross_check(self, "head", [], stdin_data=data)

    def test_head_n5(self):
        data = "\n".join(f"line{i}" for i in range(20)) + "\n"
        cross_check(self, "head", ["-n", "5"], stdin_data=data)

    def test_head_n1(self):
        data = "only line\n"
        cross_check(self, "head", ["-n", "1"], stdin_data=data)

    def test_head_c10(self):
        data = "abcdefghij" * 10
        cross_check(self, "head", ["-c", "10"], stdin_data=data)


class TestTail(unittest.TestCase):
    def test_tail_default(self):
        data = "\n".join(f"line{i}" for i in range(20)) + "\n"
        cross_check(self, "tail", [], stdin_data=data)

    def test_tail_n5(self):
        data = "\n".join(f"line{i}" for i in range(20)) + "\n"
        cross_check(self, "tail", ["-n", "5"], stdin_data=data)

    def test_tail_n0(self):
        data = "line1\nline2\n"
        cross_check(self, "tail", ["-n", "+1"], stdin_data=data)


class TestWc(unittest.TestCase):
    """wc 测试：GNU wc 使用右对齐填充，我们的 wc 不填充。
    这是已知的行为差异，测试时做格式归一化。"""

    def _normalize_wc(self, s):
        """归一化 wc 输出：去除数字间的多余空格"""
        return ' '.join(s.split())

    def test_wc_lines(self):
        data = "a\nb\nc\n"
        our_out, _, our_rc = run_tool("wc", ["-l"], stdin_data=data)
        gnu_out, _, gnu_rc = run_tool("wc", ["-l"], stdin_data=data, use_gnu=True)
        if gnu_out is None: self.skipTest("GNU wc not available")
        self.assertEqual(our_rc, gnu_rc)
        self.assertEqual(self._normalize_wc(our_out), self._normalize_wc(gnu_out))

    def test_wc_words(self):
        data = "hello world foo bar\n"
        our_out, _, our_rc = run_tool("wc", ["-w"], stdin_data=data)
        gnu_out, _, gnu_rc = run_tool("wc", ["-w"], stdin_data=data, use_gnu=True)
        if gnu_out is None: self.skipTest("GNU wc not available")
        self.assertEqual(our_rc, gnu_rc)
        self.assertEqual(self._normalize_wc(our_out), self._normalize_wc(gnu_out))

    def test_wc_chars(self):
        data = "hello\n"
        our_out, _, our_rc = run_tool("wc", ["-c"], stdin_data=data)
        gnu_out, _, gnu_rc = run_tool("wc", ["-c"], stdin_data=data, use_gnu=True)
        if gnu_out is None: self.skipTest("GNU wc not available")
        self.assertEqual(our_rc, gnu_rc)
        self.assertEqual(self._normalize_wc(our_out), self._normalize_wc(gnu_out))

    def test_wc_all(self):
        data = "hello world\nfoo bar baz\n"
        our_out, _, our_rc = run_tool("wc", [], stdin_data=data)
        gnu_out, _, gnu_rc = run_tool("wc", [], stdin_data=data, use_gnu=True)
        if gnu_out is None: self.skipTest("GNU wc not available")
        self.assertEqual(our_rc, gnu_rc)
        self.assertEqual(self._normalize_wc(our_out), self._normalize_wc(gnu_out))

    def test_wc_empty(self):
        our_out, _, our_rc = run_tool("wc", [], stdin_data="")
        gnu_out, _, gnu_rc = run_tool("wc", [], stdin_data="", use_gnu=True)
        if gnu_out is None: self.skipTest("GNU wc not available")
        self.assertEqual(our_rc, gnu_rc)
        self.assertEqual(self._normalize_wc(our_out), self._normalize_wc(gnu_out))


class TestSort(unittest.TestCase):
    def test_sort_basic(self):
        data = "banana\napple\ncherry\n"
        cross_check(self, "sort", [], stdin_data=data)

    def test_sort_reverse(self):
        data = "apple\nbanana\ncherry\n"
        cross_check(self, "sort", ["-r"], stdin_data=data)

    def test_sort_numeric(self):
        data = "10\n2\n1\n20\n"
        cross_check(self, "sort", ["-n"], stdin_data=data)

    def test_sort_unique(self):
        data = "a\nb\na\nc\nb\n"
        cross_check(self, "sort", ["-u"], stdin_data=data)

    def test_sort_empty(self):
        cross_check(self, "sort", [], stdin_data="")


class TestUniq(unittest.TestCase):
    def test_uniq_basic(self):
        data = "a\na\nb\nc\nc\nc\n"
        cross_check(self, "uniq", [], stdin_data=data)

    def test_uniq_count(self):
        data = "a\na\nb\n"
        cross_check(self, "uniq", ["-c"], stdin_data=data)

    def test_uniq_dups(self):
        data = "a\na\nb\nc\nc\n"
        cross_check(self, "uniq", ["-d"], stdin_data=data)

    def test_uniq_unique(self):
        data = "a\na\nb\nc\nc\n"
        cross_check(self, "uniq", ["-u"], stdin_data=data)


class TestCut(unittest.TestCase):
    def test_cut_f1(self):
        data = "a:b:c\n1:2:3\n"
        cross_check(self, "cut", ["-d", ":", "-f", "1"], stdin_data=data)

    def test_cut_f1_3(self):
        data = "a:b:c\n1:2:3\n"
        cross_check(self, "cut", ["-d", ":", "-f", "1,3"], stdin_data=data)

    def test_cut_c1_3(self):
        data = "abcdef\nghijkl\n"
        cross_check(self, "cut", ["-c", "1-3"], stdin_data=data)


class TestTr(unittest.TestCase):
    def test_tr_upper(self):
        cross_check(self, "tr", ["a-z", "A-Z"], stdin_data="hello")

    def test_tr_delete(self):
        cross_check(self, "tr", ["-d", "aeiou"], stdin_data="hello world")

    def test_tr_squeeze(self):
        cross_check(self, "tr", ["-s", " "], stdin_data="a   b    c")


class TestTee(unittest.TestCase):
    def test_tee_basic(self):
        with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
            fname = f.name
        try:
            cross_check(self, "tee", [fname], stdin_data="hello\n")
        finally:
            os.unlink(fname) if os.path.exists(fname) else None


class TestTrueFalse(unittest.TestCase):
    def test_true(self):
        out, err, rc = run_tool("true", [])
        self.assertEqual(rc, 0)

    def test_false(self):
        out, err, rc = run_tool("false", [])
        self.assertNotEqual(rc, 0)

    def test_yes(self):
        # yes outputs infinitely — run with timeout, check first line
        try:
            result = subprocess.run(
                [os.path.join(BUILD_DIR, "yes"), "a"],
                capture_output=True, text=True, timeout=0.5
            )
        except subprocess.TimeoutExpired as e:
            out = e.output.decode() if e.output else ""
            self.assertTrue(out.startswith("a\n"), f"yes should output 'a\n' repeatedly: {repr(out[:20])}")
            return
        self.assertTrue(result.stdout.startswith("a\n") or result.stdout == "a")


class TestPrintf(unittest.TestCase):
    def test_printf_string(self):
        cross_check(self, "printf", ["%s", "hello"])

    def test_printf_int(self):
        cross_check(self, "printf", ["%d", "42"])

    def test_printf_escape(self):
        cross_check(self, "printf", ["%s\\n", "hello"])

    def test_printf_multi(self):
        cross_check(self, "printf", ["%s=%d\\n", "a", "1", "b", "2"])


class TestEnv(unittest.TestCase):
    def test_env_basic(self):
        out, err, rc = run_tool("env", [])
        # env output varies, just check rc
        self.assertEqual(rc, 0)


class TestSeq(unittest.TestCase):
    def test_seq_5(self):
        cross_check(self, "seq", ["5"])

    def test_seq_range(self):
        cross_check(self, "seq", ["2", "6"])

    def test_seq_step(self):
        cross_check(self, "seq", ["2", "2", "10"])

    def test_seq_neg_step(self):
        cross_check(self, "seq", ["5", "-1", "1"])


class TestBasics(unittest.TestCase):
    """测试基础工具的退出码"""
    def test_test_eq(self):
        _, _, rc = run_tool("test", ["1", "-eq", "1"])
        self.assertEqual(rc, 0)

    def test_test_ne(self):
        _, _, rc = run_tool("test", ["1", "-eq", "2"])
        self.assertNotEqual(rc, 0)

    def test_test_file(self):
        _, _, rc = run_tool("test", ["-d", "/tmp"])
        self.assertEqual(rc, 0)

    def test_which(self):
        out, _, rc = run_tool("which", ["ls"], use_gnu=True)
        if rc == 0:
            _, _, our_rc = run_tool("which", ["ls"])
            self.assertEqual(our_rc, 0, "our which should find ls")


class TestDd(unittest.TestCase):
    def test_dd_basic(self):
        data = "hello world\n"
        cross_check(self, "dd", ["bs=1", "count=5"], stdin_data=data, ignore_rc=True)

    def test_dd_full(self):
        data = "abcdefghij" * 100
        cross_check(self, "dd", [], stdin_data=data, ignore_rc=True)


class TestDiff(unittest.TestCase):
    def test_diff_same(self):
        f1 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f2 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f1.write("same\n"); f1.flush(); f1.close()
        f2.write("same\n"); f2.flush(); f2.close()
        try:
            _, _, rc = run_tool("diff", [f1.name, f2.name])
            gnu_out, _, gnu_rc = run_tool("diff", [f1.name, f2.name], use_gnu=True)
            self.assertEqual(rc, gnu_rc, "diff same files should have same rc")
        finally:
            os.unlink(f1.name); os.unlink(f2.name)

    def test_diff_different(self):
        f1 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f2 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f1.write("line1\nline2\n"); f1.flush(); f1.close()
        f2.write("line1\nchanged\n"); f2.flush(); f2.close()
        try:
            _, _, rc = run_tool("diff", [f1.name, f2.name])
            self.assertNotEqual(rc, 0, "diff different files should have non-zero rc")
        finally:
            os.unlink(f1.name); os.unlink(f2.name)


class TestCmp(unittest.TestCase):
    def test_cmp_same(self):
        f1 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f2 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f1.write("same\n"); f1.flush(); f1.close()
        f2.write("same\n"); f2.flush(); f2.close()
        try:
            our_out, _, our_rc = run_tool("cmp", [f1.name, f2.name])
            gnu_out, _, gnu_rc = run_tool("cmp", [f1.name, f2.name], use_gnu=True)
            if gnu_out is not None:
                self.assertEqual(our_rc, gnu_rc, f"cmp same: our_rc={our_rc} gnu_rc={gnu_rc}")
            else:
                self.assertEqual(our_rc, 0, f"cmp same should return 0, got {our_rc}")
        finally:
            os.unlink(f1.name); os.unlink(f2.name)

    def test_cmp_different(self):
        f1 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f2 = tempfile.NamedTemporaryFile(mode='w', delete=False)
        f1.write("aaa\n"); f1.flush(); f1.close()
        f2.write("bbb\n"); f2.flush(); f2.close()
        try:
            _, _, rc = run_tool("cmp", [f1.name, f2.name])
            self.assertNotEqual(rc, 0)
        finally:
            os.unlink(f1.name); os.unlink(f2.name)


class TestMd5sum(unittest.TestCase):
    def test_md5_empty(self):
        cross_check(self, "md5sum", [], stdin_data="")

    def test_md5_text(self):
        cross_check(self, "md5sum", [], stdin_data="hello world\n")

    def test_md5_large(self):
        data = "abcdefghij" * 1000 + "\n"
        cross_check(self, "md5sum", [], stdin_data=data)

    def test_md5_special(self):
        cross_check(self, "md5sum", [], stdin_data="\x00\x01\x02\x03")


class TestSha256sum(unittest.TestCase):
    def test_sha256_empty(self):
        cross_check(self, "sha256sum", [], stdin_data="")

    def test_sha256_text(self):
        cross_check(self, "sha256sum", [], stdin_data="hello world\n")

    def test_sha256_large(self):
        data = "abcdefghij" * 1000 + "\n"
        cross_check(self, "sha256sum", [], stdin_data=data)


class TestBase64(unittest.TestCase):
    def test_b64_encode(self):
        cross_check(self, "base64", [], stdin_data="hello world\n")

    def test_b64_empty(self):
        cross_check(self, "base64", [], stdin_data="")

    def test_b64_binary(self):
        cross_check(self, "base64", [], stdin_data="\x00\x01\xff\xfe")


class TestSleep(unittest.TestCase):
    def test_sleep_zero(self):
        _, _, rc = run_tool("sleep", ["0.01"])
        self.assertEqual(rc, 0)

    def test_sleep_integer(self):
        _, _, rc = run_tool("sleep", ["1"])
        self.assertEqual(rc, 0)


class TestKill(unittest.TestCase):
    def test_kill_list(self):
        _, _, rc = run_tool("kill", ["-l"])
        self.assertEqual(rc, 0)


class TestUname(unittest.TestCase):
    def test_uname(self):
        out, _, rc = run_tool("uname", [])
        self.assertEqual(rc, 0)
        self.assertTrue(len(out) > 0)

    def test_uname_a(self):
        out, _, rc = run_tool("uname", ["-a"])
        self.assertEqual(rc, 0)


class TestHostname(unittest.TestCase):
    def test_hostname(self):
        out, _, rc = run_tool("hostname", [])
        self.assertEqual(rc, 0)
        self.assertTrue(len(out.strip()) > 0)


class TestWhoami(unittest.TestCase):
    def test_whoami(self):
        out, _, rc = run_tool("whoami", [])
        self.assertEqual(rc, 0)
        self.assertTrue(len(out.strip()) > 0)


class TestId(unittest.TestCase):
    def test_id(self):
        out, _, rc = run_tool("id", [])
        self.assertEqual(rc, 0)
        self.assertIn("uid", out)


class TestDate(unittest.TestCase):
    def test_date_format(self):
        out, _, rc = run_tool("date", ["+%Y"])
        self.assertEqual(rc, 0)
        self.assertTrue(len(out.strip()) >= 4)

    def test_date_utc(self):
        out, _, rc = run_tool("date", ["-u", "+%H:%M"])
        self.assertEqual(rc, 0)


if __name__ == "__main__":
    if not os.path.isdir(BUILD_DIR):
        print(f"FATAL: build dir not found: {BUILD_DIR}")
        print("Run 'make -C projects/meuos-utils' first.")
        sys.exit(1)

    print(f"Testing utils: {os.path.abspath(BUILD_DIR)}")
    print(f"GNU tools: system PATH")
    print()

    unittest.main(verbosity=2)
