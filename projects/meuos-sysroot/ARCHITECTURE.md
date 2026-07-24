# meuos-sysroot — .msys 单文件 sysroot 系统

> 本组件定义和维护 `.msys` 格式：一种自定义单文件 sysroot，可直接被 mcc（头文件搜索）和
> mt/as、mt/ld（库/对象读取）原生读取，无需解压到文件系统。

## 目录结构

```
meuos-sysroot/
├── Makefile               # 构建 libmsys.a + mkmsys
├── ARCHITECTURE.md        # 本文件
├── .todo/
│   └── msys.md            # 实现任务清单
├── include/mt/
│   └── msys.h             # libmsys 公共接口（mcc、mt 共享）
├── src/
│   ├── libmsys/
│   │   └── msys.c         # .msys 读取核心（mmap + 索引二分查找）
│   └── mkmsys/
│       └── main.c         # 从 sysroot 目录生成 .msys 的打包工具
└── test/
    └── msys_test.c        # 读写验收测试
```

## 设计原则

- **独立于任何组件**：meuos-sysroot 不依赖 mcc、mt/as、mt/ld，它是被它们消费的
- **格式自描述**：Magic + 排序索引，任何工具都可直接解析
- **目录兼容**：.msys 是补充而非替代，`--sysroot` 同时接受目录和 .msys
- **无压缩**：头文件是文本，.a 是 ar，seek 随机访问比解压重要

## 依赖关系

```
meuos-sysroot（本组件）
    ↑ 链接 libmsys.a
    ├── mcc           — preprocessor include 搜索
    ├── mt/ld         — -L 参数识别 .msys，读取 .a/.o
    └── meow          — make msys 调用 mkmsys 打包
```

## 使用方式

```sh
# 打包：从目录生成 .msys
mkmsys -o meuos-aarch64.msys sysroot-aarch64

# 消费（mcc 读头文件）
mcc --target=aarch64 --sysroot=meuos-aarch64.msys -c -o test.o test.c

# 消费（mcc + mt/ld 全链路）
mcc --target=aarch64 --sysroot=meuos-aarch64.msys -o test test.c
```
