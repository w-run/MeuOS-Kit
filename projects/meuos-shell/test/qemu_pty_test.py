#!/usr/bin/env python3
"""qemu-user PTY 交互测试：在 qemu-user 下运行 msh 交互模式"""
import pty, os, sys, time

QEMU = "/workspace/MeuOS-Kit/env/qemu/qemu-x86_64-static"
MSH = "/workspace/MeuOS-Kit/.agents/worktrees/shell-utils/projects/meuos-shell/build/msh"

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
        print(f"  output: {repr(output[:300])}")
        failed += 1

def run_qemu_pty(commands, wait=0.8):
    """在 qemu-user 下用 PTY 运行 msh 交互模式"""
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(QEMU, [QEMU, MSH])
    
    results = []
    time.sleep(1.0)  # qemu-user 启动稍慢
    
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

# 测试 1: 基本交互
print("=== 1. qemu-user PTY 基本交互 ===")
r = run_qemu_pty(['echo hello-pty', 'echo DONE'])
out = ''.join(r)
check("qemu PTY 基本输出", out, "hello-pty")
check("qemu PTY 完成", out, "DONE")

# 测试 2: :version
print("\n=== 2. qemu-user PTY :version ===")
r = run_qemu_pty([':version', 'echo DONE'])
out = ''.join(r)
check("qemu PTY :version", out, "MeuOS Shell")

# 测试 3: :theme modern 不段错误
print("\n=== 3. qemu-user PTY :theme modern ===")
r = run_qemu_pty([':theme modern', 'echo DONE'])
out = ''.join(r)
check("qemu PTY :theme modern 不段错误", out, "DONE", "Segmentation")

# 测试 4: 各主题切换
print("\n=== 4. qemu-user PTY 主题切换 ===")
for theme in ['modern', 'minimal', 'colorful', 'powerline', 'clean', 'rainbow']:
    r = run_qemu_pty([f':theme {theme}', 'echo OK'])
    out = ''.join(r)
    check(f"qemu PTY :theme {theme}", out, "OK", "Segmentation")

# 测试 5: :help
print("\n=== 5. qemu-user PTY :help ===")
r = run_qemu_pty([':help', 'echo DONE'])
out = ''.join(r)
check("qemu PTY :help", out, "msh :cmd")

# 测试 6: :config
print("\n=== 6. qemu-user PTY :config ===")
r = run_qemu_pty([':config', 'echo DONE'])
out = ''.join(r)
check("qemu PTY :config", out, "msh")

# 测试 7: :plugin git
print("\n=== 7. qemu-user PTY :plugin git ===")
r = run_qemu_pty([':plugin git', 'alias gst', 'echo DONE'])
out = ''.join(r)
check("qemu PTY :plugin git", out, "git status")

# 测试 8: :lang 切换
print("\n=== 8. qemu-user PTY :lang ===")
r = run_qemu_pty([':lang en-US', 'echo DONE'])
out = ''.join(r)
check("qemu PTY :lang en-US", out, "English")

# 测试 9: :cmd 函数扩展
print("\n=== 9. qemu-user PTY :cmd 函数扩展 ===")
r = run_qemu_pty([
    '_cmd_test() { echo qemu-func-result; }',
    ':test',
    'echo DONE'
])
out = ''.join(r)
check("qemu PTY :cmd 函数扩展", out, "qemu-func-result")

# 测试 10: :cmd 别名扩展
print("\n=== 10. qemu-user PTY :cmd 别名扩展 ===")
r = run_qemu_pty([
    'alias :hi="echo qemu-alias-result"',
    ':hi',
    'echo DONE'
])
out = ''.join(r)
check("qemu PTY :cmd 别名扩展", out, "qemu-alias-result")

# 测试 11: 管道
print("\n=== 11. qemu-user PTY 管道 ===")
r = run_qemu_pty(['echo hello | cat', 'echo DONE'])
out = ''.join(r)
check("qemu PTY 管道", out, "hello")

# 测试 12: 变量
print("\n=== 12. qemu-user PTY 变量 ===")
r = run_qemu_pty(['X=42', 'echo X=$X', 'echo DONE'])
out = ''.join(r)
check("qemu PTY 变量赋值", out, "X=42")

# 测试 13: if 语句
print("\n=== 13. qemu-user PTY if 语句 ===")
r = run_qemu_pty(['if true; then echo IF-OK; fi', 'echo DONE'])
out = ''.join(r)
check("qemu PTY if 语句", out, "IF-OK")

# 测试 14: for 循环
print("\n=== 14. qemu-user PTY for 循环 ===")
r = run_qemu_pty(['for i in 1 2 3; do echo $i; done', 'echo DONE'])
out = ''.join(r)
check("qemu PTY for 循环", out, "1")
check("qemu PTY for 循环 3", out, "3")

# 测试 15: 连续主题切换不崩溃
print("\n=== 15. qemu-user PTY 连续主题切换 ===")
cmds = []
for t in ['modern', 'minimal', 'colorful', 'modern', 'rainbow', 'clean', 'powerline', 'modern']:
    cmds.append(f':theme {t}')
cmds.append('echo ALLDONE')
r = run_qemu_pty(cmds, wait=0.5)
out = ''.join(r)
check("qemu PTY 连续 8 次主题切换", out, "ALLDONE", "Segmentation")

print(f"\n{'='*60}")
print(f"qemu-user PTY 交互测试结果: PASS={passed} FAIL={failed}")
print(f"{'='*60}")
sys.exit(0 if failed == 0 else 1)
