# 0170 每次同步传输调用带身份

- **状态**：accepted
- **背景**：GitHub 与 WebDAV 两个传输各自维护队列和 `inFlight`，按请求 `Kind` 认领客户端信号。后端与传输共用同一个客户端：WebDAV 的可达性探测、GitHub 登录后的 `getUser` 与轮次调用走同一批信号。一次 round 的 List 与一次探测重叠时，先到的 `found` 会被两边同时认领；同一 Kind 的迟到应答也可能落进新一轮请求。
- **决定**：新增纯队列基类 `TransportQueue`；`send()` 入队，`dispatch()` 出队并调用客户端。三个客户端（`GistClient`、`WebDavClient`、`GithubAuth::refresh`）各自为每次调用分配自增 id 并从调用返回，信号把 id 带回；传输记录当前请求的客户端 id，只接受命中的应答，其余丢弃，不再按 Kind 匹配。后端自己的探测与取名按客户端返回的 id 过滤。刷新失败改用 `refreshFailed(id, why)`，与登录流程的 `failed` 分开。
- **后果**：客户端方法返回 id、信号携带 id；直接使用客户端的代码要保存这个返回值。同步传输的重复队列实现被删除；共享客户端的串线由 id 消除，WebDAV 后端有专门的串线用例。客户端在返回 id 前同步拒绝时，传输把尚无 id 的队首当作该次调用，不会挂起。
