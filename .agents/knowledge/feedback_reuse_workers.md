# 复用已有 worker 而非每次新建

队员完成分派任务后，直接通过 SendMessage 复用，而不是每次都 spawn 新队员。

**Why:** 队员的模型缓存里已经有组件上下文（架构、文件结构、近期改动），新建队员要从头加载大段代码，浪费 token 和时间。

**How to apply:**
- worker 完成任务后，通过 SendMessage 发新的任务指令继续用同一队员
- 仅当队员的上下文完全无关（比如之前做 libc 现在让做 meow）或队员表现不佳时才 spawn 新的
- 这同样适用于 research→implementation 的延续：research worker 刚读完相关文件，直接续接实现