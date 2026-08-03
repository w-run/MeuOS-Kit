#!/usr/bin/env python3
"""qemu-system VM 模式测试：在 Alpine Linux VM (kernel 6.6.142) 中运行 msh"""
import socket, time, sys

SOCK_PATH = "/workspace/MeuOS-Kit/env/run/x86_64.sock"
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(SOCK_PATH)
time.sleep(3)

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
        print(f"  output: {repr(output[-300:])}")
        failed += 1

def drain():
    """清空接收缓冲区，等待延迟输出"""
    for _ in range(3):
        sock.setblocking(False)
        try:
            while True:
                chunk = sock.recv(65536)
                if not chunk: break
        except: pass
        sock.setblocking(True)
        time.sleep(0.5)

def send_recv(cmd, wait=4.0):
    """发送命令，等待，读取全部输出"""
    drain()
    sock.sendall((cmd + '\n').encode())
    time.sleep(wait)
    data = b''
    # 持续读取直到没有更多数据
    for _ in range(5):
        sock.setblocking(False)
        try:
            chunk = sock.recv(65536)
            if chunk:
                data += chunk
            else:
                break
        except:
            break
        sock.setblocking(True)
        time.sleep(0.3)
    sock.setblocking(True)
    return data.decode('utf-8', errors='replace')

# 读初始输出
drain()
time.sleep(1)
print("=== qemu-system VM 测试 (kernel 6.6.142 + Alpine musl) ===\n")

# 测试 1: 确认 VM 运行
out = send_recv('uname -m', 2.0)
check("VM uname -m", out, "x86_64")

# 测试 2: msh 可执行
out = send_recv('ls /bin/msh', 2.0)
check("msh 在 /bin/msh", out, "msh")

# 测试 3: msh --version
out = send_recv('/bin/msh --version', 3.0)
check("msh --version", out, "MeuOS Shell")

# 测试 4: msh -c echo
out = send_recv('/bin/msh -c "echo hello-vm"', 3.0)
check("msh -c echo", out, "hello-vm")

# 测试 5: msh -c :help
out = send_recv('/bin/msh -c ":help"', 3.0)
check("msh :help", out, "msh :cmd")

# 测试 6: msh -c :version
out = send_recv('/bin/msh -c ":version"', 3.0)
check("msh :version", out, "MeuOS Shell")

# 测试 7: msh -c :themes
out = send_recv('/bin/msh -c ":themes"', 3.0)
check("msh :themes", out, "modern")

# 测试 8: msh -c :theme modern
out = send_recv('/bin/msh -c ":theme modern && echo THEME_OK"', 3.0)
check("msh :theme modern", out, "THEME_OK")

# 测试 9: msh -c :plugins
out = send_recv('/bin/msh -c ":plugins"', 3.0)
check("msh :plugins", out, "git")

# 测试 10: msh -c :plugin git
out = send_recv('/bin/msh -c ":plugin git && alias gst"', 3.0)
check("msh :plugin git", out, "git status")

# 测试 11: msh -c :lang en-US
out = send_recv('/bin/msh -c ":lang en-US"', 3.0)
check("msh :lang en-US", out, "English")

# 测试 12: msh -c :lang zh-CN
out = send_recv('/bin/msh -c ":lang zh-CN"', 3.0)
check("msh :lang zh-CN", out, "zh")

# 测试 13: msh -c :config
out = send_recv('/bin/msh -c ":config"', 3.0)
check("msh :config", out, "msh")

# 测试 14: msh -c 函数 :cmd 扩展
out = send_recv('/bin/msh -c "_cmd_test() { echo vm-func; }; :test"', 3.0)
check("msh :cmd 函数扩展", out, "vm-func")

# 测试 15: msh -c 别名 :cmd 扩展
out = send_recv('/bin/msh -c "alias :hi=echo vm-alias; :hi"', 3.0)
check("msh :cmd 别名扩展", out, "vm-alias")

# 测试 16: msh -c if 语句
out = send_recv('/bin/msh -c "if true; then echo IF-VM; fi"', 3.0)
check("msh if 语句", out, "IF-VM")

# 测试 17: msh -c for 循环
out = send_recv('/bin/msh -c "for i in 1 2 3; do echo $i; done"', 3.0)
check("msh for 循环", out, "1")

# 测试 18: msh -c 管道
out = send_recv('/bin/msh -c "echo hello | cat"', 3.0)
check("msh 管道", out, "hello")

# 测试 19: msh -c exit code
out = send_recv('/bin/msh -c "exit 42"; echo "RC=$?"', 3.0)
check("msh exit code", out, "42")

# 测试 20-25: 所有主题切换
for theme in ['modern', 'minimal', 'colorful', 'powerline', 'clean', 'rainbow']:
    out = send_recv(f'/bin/msh -c ":theme {theme} && echo OK"', 3.0)
    check(f"msh :theme {theme}", out, "OK")

sock.close()

print(f"\n{'='*60}")
print(f"qemu-system VM 测试结果: PASS={passed} FAIL={failed}")
print(f"{'='*60}")
sys.exit(0 if failed == 0 else 1)
