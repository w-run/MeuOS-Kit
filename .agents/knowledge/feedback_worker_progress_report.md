# worker 15分钟定时汇报进展

分派给 worker 的长任务，须在 prompt 中要求每隔 15 分钟汇报一次进展，无论是否完成。

**Why:** 长任务 worker 可能卡住或进度缓慢，没有主动汇报机制会导致 leader 等待过久才发现问题。固定间隔汇报让 leader 可以及时发现阻塞或方向偏差。

**How to apply:** 在 spawn 或 SendMessage 分派长任务的 prompt 末尾加上：「每15分钟汇报一次进展（即使还没完成），说明当前到了哪一步、有无阻塞。」