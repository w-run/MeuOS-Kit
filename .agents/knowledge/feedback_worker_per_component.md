# 按组件分派专职 worker

团队成员尽量按组件分派专职，一个 worker 专职做 mcc 部分，另一个专职做 libc 部分，以此类推。

**Why:** 每个组件的代码量大、上下文复杂（架构差异、接口契约、history），频繁切换 worker 或让同一个 worker 跨组件会导致每次都要重读大段代码，效率极低。连续专职的 worker 能利用模型缓存保持上下文持续。

**How to apply:** 拉团队时按组件分配队员：mcc-worker（专职 mcc/m++）、libc-worker（专职 meuos-libc）、toolchain-worker（专职 meuos-toolchain）、meow-worker（专职 meow）等。除非组件间有阻塞依赖，否则不跨组件分派同一 worker。leader 负责跨组件协调和裁决。