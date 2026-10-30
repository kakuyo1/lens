# 0143 在线 OCR 的五个 Adapter 共用一个骨架

- **状态**：accepted（2026-10-30）
- **背景**：`failed()` 结果工厂、严格 JSON readers、预检阶梯与 HTTP 状态分类在五家 adapter（OCR.space、百度、腾讯云、阿里云、Google Vision）各抄一份，并已抄出分歧：百度对 401／403／429 返回裸 `Failed`，其余四家返回 `Authentication` 或 `Quota`，与 ADR 0136 承诺的 “失败分类随结果返回” 相悖。`OcrFailureReason` 之外还留着一个无人产也无人读的 `OcrFallbackReason`。架构评审 2026-10-30 候选 03。
- **决定**：新增 `src/ocr/ocr_adapter.{h,cpp}`，收下五家共享的部分：一份预检（已确认来源、配置可用、图片可读、服务边界、未取消、未截止；配置判断由各家以 `configurationUsable` 自报，共享的只有它的结局）、传输层与 HTTP 状态的同一读法（3xx → `Failed`，401／403 → `Authentication`，429 → `Quota`，其余非 2xx → `TransportFailed`）、共享上限常量，以及 `finishOcrResult`——EmptyText、大小上限与 `geometryLevel` 在一处决定，几何级别从行自带的框推导，adapter 无法再虚报。签名与鉴权、请求构造、自家错误码表与 payload 解析留在各家；阿里云的 401／403 先读 body、Google 的 400 算密钥问题，这两处作为协议差异保留在各自文件里。`OcrFallbackReason` 与 `OcrTiming` 里从未产出的两个字段删除。
- **后果**：额度与鉴权分类只修一处，五份预检阶梯一份；新增一家服务仍是 adapter 文件、表行、枚举值与工厂分支，骨架不重抄。`OcrAdapter` 的 10 例与五家 adapter 的 44 例全部通过。被排除的方案：只加一条跨服务分类用例而不收拢实现——分歧仍活在五份副本里，下一次改动还会漂移；为共享部分另立抽象基类——共享的是无状态函数与数值，类没有可持有的状态。
- **ADR 关系**：0136 的服务表、凭据条款与失败分类承诺不变，本 ADR 是让那条承诺真正成立；0130 的本地与在线边界不动。
