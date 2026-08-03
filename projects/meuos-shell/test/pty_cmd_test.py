#!/usr/bin/env python3
"""PTY 交互模式 :cmd + YAML 主题详细测试"""
import pty, os, sys, time, re

def run_pty_session(commands, wait=0.5):
    """在 PTY 中运行一组命令，返回输出"""
    pid, fd = pty.fork()
    if pid == 0:
        os.execv('./build/msh', ['./msh'])
    
    results = []
    time.sleep(0.5)
    
    for cmd in commands:
        os.write(fd, (cmd + '\n').encode())
        time.sleep(wait)
        try:
            data = os.read(fd, 65536)
            results.append(data.decode('utf-8', errors='replace'))
        except:
            results.append('')
    
    try:
        os.kill(pid, 9)
    except:
        pass
    os.waitpid(pid, 0)
    return results

MSH = './build/msh'
passed = 0
failed = 0

def check(name, output, expected, unexpected=None):
    global passed, failed
    ok = expected in output if expected else True
    if unexpected and unexpected in output:
        ok = False
    if ok:
        print(f"PASS: {name}")
        passed += 1
    else:
        print(f"FAIL: {name} (expected '{expected}')")
        print(f"  output: {repr(output[:200])}")
        failed += 1

# 测试 1: :theme modern 不段错误，PS1 有 git 分支
print("=== 1. 交互模式 :theme modern 段错误修复 ===")
r = run_pty_session([':theme modern', 'echo DONE'], wait=0.8)
out = ''.join(r)
check(":theme modern 不段错误", out, "DONE")
check(":theme modern 显示 git 分支", out, "worktree-shell-utils")

# 测试 2: 各主题在交互模式下切换
print("\n=== 2. 交互模式主题切换 ===")
for theme in ['modern', 'minimal', 'colorful', 'powerline', 'clean', 'rainbow']:
    r = run_pty_session([f':theme {theme}', 'echo OK'], wait=0.5)
    out = ''.join(r)
    check(f":theme {theme} 交互模式", out, "OK", "Segmentation fault")

# 测试 3: :help 在交互模式下显示
print("\n=== 3. 交互模式 :help ===")
r = run_pty_session([':help', 'echo DONE'], wait=0.5)
out = ''.join(r)
check(":help 交互模式", out, "msh :cmd")
check(":help 显示 :theme", out, ":theme")

# 测试 4: :config 在交互模式下显示
print("\n=== 4. 交互模式 :config ===")
r = run_pty_session([':config', 'echo DONE'], wait=0.5)
out = ''.join(r)
check(":config 交互模式", out, "msh")
check(":config 显示主题", out, "主题")

# 测试 5: :lang 切换在交互模式下
print("\n=== 5. 交互模式 :lang ===")
r = run_pty_session([':lang en-US', 'echo DONE'], wait=0.5)
out = ''.join(r)
check(":lang en-US 交互模式", out, "English")

# 测试 6: :plugin git 在交互模式下
print("\n=== 6. 交互模式 :plugin git ===")
r = run_pty_session([':plugin git', 'echo DONE'], wait=0.5)
out = ''.join(r)
check(":plugin git 交互模式", out, "DONE")

# 测试 7: :cmd 函数扩展在交互模式下
print("\n=== 7. 交互模式 :cmd 函数扩展 ===")
r = run_pty_session([
    '_cmd_test() { echo func-result; }',
    ':test',
    'echo DONE'
], wait=0.5)
out = ''.join(r)
check(":cmd 函数扩展交互模式", out, "func-result")

# 测试 8: :cmd 别名扩展在交互模式下
print("\n=== 8. 交互模式 :cmd 别名扩展 ===")
r = run_pty_session([
    'alias :hi="echo alias-result"',
    ':hi',
    'echo DONE'
], wait=0.5)
out = ''.join(r)
check(":cmd 别名扩展交互模式", out, "alias-result")

# 测试 9: 未知 :cmd 报错
print("\n=== 9. 交互模式未知 :cmd ===")
r = run_pty_session([':nonexistent', 'echo DONE'], wait=0.5)
out = ''.join(r)
check("未知 :cmd 报错", out, "未知指令")

# 测试 10: : 单独不触发 :cmd
print("\n=== 10. 交互模式 : no-op ===")
r = run_pty_session([':', 'echo DONE'], wait=0.5)
out = ''.join(r)
check(": no-op 不触发", out, "DONE")

# 测试 11: msh theme modern 在交互模式下工作
print("\n=== 11. 交互模式 msh theme modern ===")
r = run_pty_session(['msh theme modern', 'echo DONE'], wait=0.5)
out = ''.join(r)
check("msh theme modern 交互模式", out, "DONE", "cannot open")

# 测试 12: msh plugin list 在交互模式下工作
print("\n=== 12. 交互模式 msh plugin list ===")
r = run_pty_session(['msh plugin list', 'echo DONE'], wait=0.5)
out = ''.join(r)
check("msh plugin list 交互模式", out, "git")

# 测试 13: 连续多次主题切换不崩溃
print("\n=== 13. 连续多次主题切换 ===")
cmds = []
for t in ['modern', 'minimal', 'colorful', 'modern', 'rainbow', 'clean', 'powerline', 'modern']:
    cmds.append(f':theme {t}')
cmds.append('echo ALLDONE')
r = run_pty_session(cmds, wait=0.3)
out = ''.join(r)
check("连续 8 次主题切换", out, "ALLDONE", "Segmentation")

# 测试 14: 提示符在非 git 目录下正常工作
print("\n=== 14. 非 git 目录提示符 ===")
r = run_pty_session([
    'cd /tmp',
    ':theme modern',
    'echo DONE'
], wait=0.5)
out = ''.join(r)
check("非 git 目录主题切换", out, "DONE", "Segmentation")

print(f"\n{'='*50}")
print(f"PTY 交互模式测试结果: PASS={passed} FAIL={failed}")
print(f"{'='*50}")
sys.exit(0 if failed == 0 else 1)
