# libutils 共享代码重构（P10）

> **状态**: ✅ 完成（2026-08-03）
> **提交**: 6e09a43 → 90abdf7 → 3272c4f → 2339bbb → bfa80fb → 98003d2

## 背景

在实现 ip/nslookup/telnet 网络工具后，发现 meuos-utils 的 45+ 工具间存在大量重复代码。
逐个工具各自实现了 version 字符串、program_name 提取、--version/--help 检查、
网络信息解析、信号名表、时长解析、十六进制转换等公共逻辑。

## 重构内容

### 1. netinfo 共享模块（90abdf7）

**问题**: ip/ifconfig/route/netstat 4 个工具各自实现 /proc/net/dev 解析、
ioctl 调用、MAC 地址格式化、路由表解析、hex IP 转换。

**方案**: 创建 `libutils/netinfo.c` + `include/meuos/netinfo.h`，
提供 7 个共享函数和 3 个结构体。

| 函数 | 用途 |
|------|------|
| `netinfo_get_if_stats()` | 解析 /proc/net/dev 获取接口统计 |
| `netinfo_get_if_info()` | ioctl 获取接口地址/MAC/MTU/flags |
| `netinfo_format_mac()` | MAC 地址 → "xx:xx:xx:xx:xx:xx" |
| `netinfo_get_routes()` | 解析 /proc/net/route |
| `netinfo_hex_to_ip()` | 十六进制 IP → 点分十进制 |
| `netinfo_cidr_prefix()` | 子网掩码 → CIDR 前缀长度 |
| `netinfo_format_ip()` | sockaddr → 可读 IP 字符串 |

**影响工具**: ip, ifconfig, route, netstat（4 个）

### 2. utils_init 一站式初始化（3272c4f）

**问题**: 35+ 工具各自手写 `const char *version = "..."`、
`set_program_name(argv[0])`、`--version`/`--help` 检查。

**方案**: 在 `libutils/version.c` 中实现 `utils_init()`：

```c
int utils_init(int argc, char **argv);
// 返回值 = 第一个非选项参数的索引（argi）
// 自动处理：
//   --version  → 打印版本信息，exit(0)
//   --help     → 返回 argi，由调用方处理 usage
//   program_name = argv[0] 的 basename
```

同时提供 `utils_usage()` 和 `utils_die_usage()` 辅助函数。

**影响工具**: 35+ 个（sys/ + file/ + text/ + arch/ + core/ 目录全部工具）

**重构模式**:
```c
// 旧代码（每个工具各写一遍）
const char *version = "xxx (meuos-utils) v0.1";
set_program_name(argv[0]);
int argi = 1;
if (argi < argc && !strcmp(argv[argi], "--version")) {
    printf("%s\n", version);
    return 0;
}

// 新代码（一行搞定）
int argi = utils_init(argc, argv);
```

### 3. parse_duration 时长解析（2339bbb）

**问题**: sleep 和 timeout 各自实现 `parse_duration()`，
仅支持简单秒数。

**方案**: 创建 `libutils/duration.c`，增强为：

| 格式 | 示例 | 说明 |
|------|------|------|
| 纯数字 | `90` | 90 秒 |
| 秒后缀 | `90s` | 90 秒 |
| 复合 | `1h30m` | 1 小时 30 分 |
| 冒号 | `1:30:00` | 1 时 30 分 0 秒 |
| 冒号 | `90:00` | 90 分 0 秒 |

提供 `parse_duration()`（返回 double 秒数）和 `parse_duration_ts()`
（填充 `struct timespec`，用于 `nanosleep`）。

**影响工具**: sleep, timeout（2 个）

### 4. signame 信号名表（bfa80fb）

**问题**: kill 和 timeout 各自维护信号名表（仅 20 个信号），
`sig_name()` / `sig_from_name()` 重复实现。

**方案**: 创建 `libutils/signame.c`，扩展到 31 个信号，
提供 3 个函数：

| 函数 | 用途 |
|------|------|
| `sig_from_name(s)` | "TERM"/"SIGTERM"/"15" → 15 |
| `sig_to_name(sig)` | 15 → "TERM"（不含 SIG 前缀） |
| `sig_list_all()` | 打印所有已知信号（kill -l 用） |

**影响工具**: kill, timeout（2 个）

### 5. hex 十六进制转换（98003d2）

**问题**: md5sum 和 sha256sum 各自实现 `md5_hex()` / `sha256_hex()`，
逻辑完全相同。

**方案**: 创建 `libutils/hex.c`，提供：

| 函数 | 用途 |
|------|------|
| `bytes_to_hex(data, len, out)` | 二进制 → 小写十六进制字符串 |
| `hex_to_bytes(hex, out, max)` | 十六进制 → 二进制（支持冒号/连字符/空格分隔） |

**影响工具**: md5sum, sha256sum（2 个）

### 6. md5sum/sha256sum 哈希算法 bug 修复（98003d2）

在重构 hex 转换时发现两个工具的哈希值与 GNU 不一致，
排查发现 3 个预存 bug：

| Bug | 影响 | 修复 |
|-----|------|------|
| MD5 输出字节序交错 | md5sum | `out[i*4]` → `out[i]`/`out[4+i]`/`out[8+i]`/`out[12+i]` |
| bits padding 污染 | md5sum + sha256sum | 在 padding 前保存 `saved_bits` |
| check 模式 sscanf | md5sum + sha256sum | `%*2s` → `%32s %255s` + `*` 前缀处理 |

**验证**: 修复后与 GNU md5sum/sha256sum 完全一致
（空文件、100KB 随机文件、stdin、check 模式交叉验证）。

## 后续（暂缓）

- **digest 哈希框架抽象** — 当前仅 MD5 + SHA-256 两个算法，
  抽象为通用 `digest_ctx` 接口的收益有限。
  等 SHA-1/SHA-512 等更多算法加入后再做。
