# 员工版网络出口审计设计

> **状态**：草案，待用户审查
> **作者**：Claude（基于 2026-05-11 brainstorming session）
> **目的**：在 tdesktop 员工版客户端的最底层网络出口植入审计监控，记录员工的所有"收发信息 / 增删拉黑联系人 / 单向双向清空删除聊天记录"等行为，确保即使员工事后双向删除好友和聊天记录，公司仍能查到完整记录。
> **威胁模型**：充分信任客户端（员工无能力反编译/旁路 hook），重点防"通过正常 UI 操作销毁证据"。

---

## 0. 决策汇总（来自 brainstorming）

| 维度 | 决策 |
|---|---|
| 数据去向 | 实时上报公司后端 |
| 故障策略 | 本地临时队列 + 后台重试，不阻断员工动作 |
| 记录粒度 | 元数据 + 文本全文；文件只记 file_id + 缩略图；通话只记元数据 |
| 覆盖动作 | 消息（发/转/编/删/收快照）+ 联系人（增删拉黑改备注）+ 清空对话 |
| 威胁模型 | 充分信任客户端，单点 hook |
| 后端协议 | 客户端定义契约（HTTPS REST + JSON），公司团队实现服务 |

## 1. 架构与组件

### 1.1 文件布局

新增 5 个文件，全部在 `Telegram/SourceFiles/intro/employee/`：

| 文件 | 职责 | 估行 |
|---|---|---|
| `employee_audit.cpp/h` | 协调器 + hook 安装入口；session 启动初始化、关闭 flush | ~250 |
| `employee_audit_event.cpp/h` | `AuditEvent` 结构体 + JSON 序列化 | ~200 |
| `employee_audit_decoder.cpp/h` | TL RPC / Update → AuditEvent 解码（按 RPC ID 白名单分发） | ~400 |
| `employee_audit_queue.cpp/h` | 本地 SQLite 队列（持久化、重启续传、容量上限） | ~250 |
| `employee_audit_uploader.cpp/h` | 上传线程：批量打包、HTTPS POST、指数退避、ack/nack | ~250 |

总计 ~1350 行。每文件职责单一可独立测试。

### 1.2 Hook 注入点（用 `#ifdef TDESKTOP_EMPLOYEE_MODE`）

1. **出站**：`Telegram/SourceFiles/mtproto/mtp_instance.cpp::Instance::Private::sendRequest` —— 拦截所有 RPC
2. **入站**：`Telegram/SourceFiles/api/api_updates.cpp::Updates::feedUpdate` —— 捕获新到消息快照

### 1.3 数据流

```
[Api::SendMessage] → [Instance::sendRequest]
                       ↓ HOOK 1 (outbound)
                       Audit::onRpc(rpcId, params)
                            ↓ if rpcId in whitelist
                            AuditDecoder::decodeRpc → AuditEvent
                                       ↓
                                       AuditQueue::push  (SQLite)
                       ↓ continue normal RPC flow
                       [Telegram Server]
                            ↓
[Updates::feedUpdate] ← updates
       ↓ HOOK 2 (inbound)
       Audit::onUpdate(updateNewMessage / updateNewChannelMessage / editMessage)
            ↓ ALL incoming new/edited messages（v1 不做 peer 过滤，见 §7 Q4）
            AuditDecoder::decodeUpdate → AuditEvent
                       ↓
                       AuditQueue::push (SQLite)

[Background uploader thread, started by Audit::init]
   loop:
     batch = AuditQueue::peek_unsent(N=50)
     if empty: wait 5s; continue
     POST /v1/audit/events  → backend
     on 2xx: AuditQueue::mark_sent(batch_ids)
     on 5xx/timeout: exponential backoff (5s → 30s → 5min cap)
     on 4xx (non-auth): log + drop those batch_ids
     on 401: refresh employee token, retry; if still 401 stop, write critical error
```

### 1.4 关键不变量

- 拦截 hook **绝不**抛异常（出错就 swallow + log），保证 RPC 主流程不受影响
- 拦截 hook **永不阻塞**（push 到 queue 后立即返回，I/O 在后台线程）
- queue 用 SQLite 而非内存：客户端崩溃/异常退出/断电不丢未上报记录
- queue 容量上限 100MB：超过则丢最老的事件 + 写一条 `meta.dropped` 元事件供后端告警

## 2. JSON Schema（后端接收契约）

### 2.1 Envelope（所有事件统一外壳）

```json
{
  "event_id": "01HQQX5...",         // 客户端生成 ULID，用于后端幂等去重
  "client_seq": 12345,              // 单调自增，本次客户端启动以来第 N 个事件
  "timestamp_ms": 1747845600123,    // 事件发生时间（hook 触发时刻）
  "employee_id": "emp_42",          // 来自 employee_auth token
  "tg_user_id": 953271174,          // 员工的 Telegram 账号 ID
  "client_version": "6.8.1",
  "platform": "windows",
  "event_type": "msg.send",         // 见 2.2
  "direction": "out",               // out=员工发起 | in=收到
  "payload": { ... }                // 类型特定内容，见 2.3
}
```

### 2.2 事件类型清单

| event_type | 触发 RPC / Update | 上下文场景 |
|---|---|---|
| `msg.send` | `messages.sendMessage` / `sendMedia` / `sendMultiMedia` | 员工发文本/图/文件/语音/贴纸 |
| `msg.forward` | `messages.forwardMessages` | 员工转发 |
| `msg.edit` | `messages.editMessage` | 员工编辑（"编完掩盖"场景） |
| `msg.delete` | `messages.deleteMessages` | 员工删消息（含 revoke=true 双向删） |
| `msg.recv` | `updateNewMessage` 入站 | 收到对方消息立刻快照（防对方双向删后无据可查） |
| `history.clear` | `messages.deleteHistory` / `deleteUserHistory` | 员工清空整段对话 |
| `contact.add` | `contacts.addContact` | 加联系人或改备注（同一 RPC） |
| `contact.delete` | `contacts.deleteContacts` | 删联系人 |
| `contact.block` | `contacts.block` / `blockFromReplies` | 拉黑 |
| `contact.unblock` | `contacts.unblock` | 解除拉黑 |
| `meta.dropped` | 内部触发 | 队列爆仓丢事件时上报 |

### 2.3 Payload 详情

#### 复用结构 `peer`

```json
{
  "type": "user|chat|channel",
  "id": 123456789,
  "access_hash": "0xabc..." or null,
  "username": "alice" or null,
  "first_name": "张三" or null,
  "last_name": "李四" or null,
  "phone": "+8613800000000" or null
}
```

#### 复用结构 `media`

```json
{
  "type": "photo|video|document|voice|video_note|sticker|gif|webpage|geo|contact|poll",
  "file_id": "AgADbgADxoZAVQ" or null,
  "size_bytes": 1234567,
  "mime": "image/jpeg",
  "name": "IMG_001.jpg" or null,
  "duration_sec": 12 or null,
  "thumb_b64": "data:image/jpeg;base64,..." or null,
  "caption": "..." or null
}
```

#### `msg.send`

```json
{
  "rpc": "messages.sendMessage",
  "to_peer": { ...peer... },
  "client_msg_id": 17472841,
  "reply_to_msg_id": null or 9999,
  "text": "你好，这是消息内容",
  "entities": [{"type":"bold","offset":0,"length":2}],
  "media": null or { ...media... },
  "silent": false,
  "schedule_date": null or 1747900000,
  "ttl_period_sec": null or 86400
}
```

#### `msg.forward`

```json
{
  "from_peer": { ...peer... },
  "to_peer": { ...peer... },
  "from_msg_ids": [12345, 12346],
  "drop_author": false,
  "drop_media_captions": false,
  "with_my_score": false
}
```

#### `msg.edit`

```json
{
  "peer": { ...peer... },
  "msg_id": 12345,
  "new_text": "改后的内容",
  "new_entities": [...],
  "new_media": null or { ...media... }
}
```

#### `msg.delete`

```json
{
  "peer": { ...peer... },
  "msg_ids": [12345, 12346],
  "revoke": true
}
```

#### `msg.recv`

```json
{
  "peer": { ...peer... },
  "msg_id": 67890,
  "from_user_id": 999888,
  "text": "对方发的内容",
  "entities": [...],
  "media": null or { ...media... },
  "ttl_period_sec": null,
  "fwd_from": null or { ...peer... },
  "via_bot_id": null
}
```

#### `history.clear`

```json
{
  "peer": { ...peer... },
  "max_id": 99999,
  "just_clear": true,
  "revoke": true
}
```

#### `contact.add`（包含编辑备注）

```json
{
  "tg_user_id": 999888,
  "first_name": "VIP-北京-订单1234",
  "last_name": "",
  "phone": "+8613900000000",
  "add_phone_privacy_exception": false
}
```

#### `contact.delete`

```json
{
  "tg_user_ids": [999888, 999889]
}
```

#### `contact.block` / `contact.unblock`

```json
{
  "peer": { ...peer... },
  "block_from_stories_only": false
}
```

#### `meta.dropped`

```json
{
  "dropped_count": 50,
  "reason": "queue_overflow" | "local_disk_full" | "decode_failed" | "decode_crashed",
  "first_dropped_ts_ms": 1747844000000,
  "last_dropped_ts_ms": 1747845000000
}
```

## 3. HTTPS 端点契约

### 3.1 端点 URL

在 `employee_config.h` 新增一个独立常量（与 `BackendType` 解耦，便于运维分团队）：

```cpp
namespace Intro::Employee {
constexpr auto kAuditEndpoint = "https://audit.your-company.com/v1/audit/events";
}
```

### 3.2 请求

```http
POST /v1/audit/events HTTP/1.1
Host: audit.your-company.com
Content-Type: application/json
Content-Encoding: gzip
Authorization: Bearer <employee_token>
X-Client-Version: 6.8.1
X-Client-Platform: windows
X-Batch-Id: 01HQQX5RNX...

{
  "batch_id": "01HQQX5RNX...",
  "events": [
    { ...envelope+payload... },
    { ...envelope+payload... }
  ]
}
```

**约束**：
- 每批 ≤ 50 events 或 body ≤ 1MB（先达者为准）
- 必须支持 `Content-Encoding: gzip`（后端要 decode）
- 连接超时 10s，读超时 30s

### 3.3 响应（200 OK）

```json
{
  "batch_id": "01HQQX5RNX...",
  "accepted": ["01HQQX5RA01...", "01HQQX5RA02..."],
  "rejected": [
    {
      "event_id": "01HQQX5RA03...",
      "reason": "duplicate" | "schema_invalid" | "too_old" | "ratelimited",
      "detail": "human-readable optional"
    }
  ]
}
```

**客户端语义**：
- `accepted` + `rejected` 里所有 event_id → 从 queue 删除
- `rejected.reason == "schema_invalid"` → 同时本地 ERROR log
- 不在响应里的 event_id → 留队列重试

### 3.4 状态码

| Status | 含义 | 客户端行为 |
|---|---|---|
| `200` | 处理完成 | 按 accepted/rejected 删队列 |
| `400` | 整批 JSON 格式错 | log ERROR + 删整批（重试不会修） |
| `401` | token 失效 | 刷新 token，重试 1 次；2 次失败转入 5xx 退避 |
| `403` | 该员工被禁审计上报 | 写 critical log + 暂停上传 1 小时再试 |
| `408` | timeout | 标准退避 |
| `413` | 批太大 | 批切半重试；单条 > 1MB 则丢该条 |
| `429` | rate limit | 读 `Retry-After` 头，等待对应秒数后再试 |
| `5xx` | 后端故障 | 指数退避重试（5s → 30s → 5min cap） |
| 网络超时 / DNS / TLS | 同 5xx | 同 5xx |

### 3.5 幂等性

- event_id 是后端的去重主键
- 见过的同样 event_id → 放进 `rejected[].reason="duplicate"`，**不视为错**
- 客户端可放心重试

### 3.6 TLS

- 必须 `https://`（http 拒绝）
- 系统 CA 校验即可，不做 cert pinning

### 3.7 不做心跳

后端**无法**主动判断客户端是否在线（仅靠"最近事件上报时间"推断）。v2 再加。

## 4. 本地队列（SQLite）

### 4.1 文件位置

```
<workdir>/tdata/audit/queue.sqlite
<workdir>/tdata/audit/queue.sqlite-wal
<workdir>/tdata/audit/queue.sqlite-shm
```

独立目录（不塞进 tdesktop 自己加密管理的 tdata 主库），便于纯 SQLite 工具救数据。

### 4.2 Schema

```sql
CREATE TABLE events (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id        TEXT    NOT NULL UNIQUE,
    event_type      TEXT    NOT NULL,
    timestamp_ms    INTEGER NOT NULL,
    json_blob       BLOB    NOT NULL,           -- 完整 envelope+payload，gzip 压缩
    blob_size       INTEGER NOT NULL,
    state           INTEGER NOT NULL DEFAULT 0, -- 0=pending, 1=in_flight, 2=sent待清理
    attempt_count   INTEGER NOT NULL DEFAULT 0,
    next_retry_ms   INTEGER NOT NULL DEFAULT 0,
    last_error      TEXT
);

CREATE INDEX idx_state_retry ON events(state, next_retry_ms);
CREATE INDEX idx_timestamp ON events(timestamp_ms);

CREATE TABLE meta (
    key TEXT PRIMARY KEY,
    value TEXT
);
-- 存放：client_seq, last_dropped_count, queue_total_bytes（缓存）
```

```sql
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA wal_autocheckpoint = 1000;
PRAGMA mmap_size = 67108864;
```

### 4.3 容量管理

- **软上限 50MB**：超过预警 + 本地 WARN log
- **硬上限 100MB**：超过删 1000 条最老的 pending + 入队一条 `meta.dropped`
- 节流：每 100 条 push 才检查一次 SUM；用内存 `cachedTotalBytes` 估算

### 4.4 上传线程读队列

```cpp
std::vector<EventRow> peekBatch(int maxCount = 50, int maxBytes = 1024*1024) {
    auto now = nowMs();
    return sqlite(R"(
        SELECT id, event_id, json_blob FROM events
        WHERE state = 0 AND next_retry_ms <= ?
        ORDER BY id LIMIT ?
    )", now, maxCount);
}

void markInFlight(const std::vector<int64>& rowIds) {
    sqlite("UPDATE events SET state=1, attempt_count=attempt_count+1 WHERE id IN (...)");
}

void markSent(const std::vector<std::string>& eventIds) {
    sqlite("DELETE FROM events WHERE event_id IN (...)");
}

void markFailed(const std::vector<int64>& rowIds, const std::string& err, int retryDelayMs) {
    sqlite("UPDATE events SET state=0, next_retry_ms=?, last_error=? WHERE id IN (...)",
           nowMs() + retryDelayMs, err);
}
```

### 4.5 启动恢复

```cpp
void recoverFromStaleInFlight() {
    // 客户端 crash 时 state=1 的事件没回归 → 重置为 0 重试
    // 后端用 event_id 幂等去重
    sqlite("UPDATE events SET state = 0 WHERE state = 1");
}
```

### 4.6 `client_seq` 持久化

每次 hook 入队时拿一个 SQLite 事务原子递增的 `client_seq`。重启不重置——后端拿到序列就知道是同一员工的连续行为流。

### 4.7 不加密

客户端被 trusted，磁盘加密由 Windows BitLocker / 公司 IT 政策保证。如果合规要求再加（SQLCipher 或 tdesktop 自家 tdata 加密 API）。

## 5. 错误处理

### 5.1 重试退避

```cpp
constexpr int64_t kInitialBackoffMs  = 5'000;
constexpr int64_t kMaxBackoffMs      = 5 * 60'000;  // 5min cap
constexpr double  kBackoffMultiplier = 2.0;
constexpr double  kJitterFraction    = 0.2;         // ±20%

int64_t computeBackoff(int attemptCount) {
    auto base = std::min(
        kInitialBackoffMs * std::pow(kBackoffMultiplier, attemptCount - 1),
        double(kMaxBackoffMs));
    auto jitter = base * kJitterFraction * (rand01() * 2 - 1);
    return int64_t(base + jitter);
}
```

序列：5s → 10s → 20s → 40s → 80s → 160s → 300s → 300s → ...

±20% jitter 防群发雪崩。

### 5.2 单条事件重试上限

100 次仍失败 → 写 ERROR log + 把这条 `state=2`（不再重试），运维事后人工排查。

### 5.3 Token 刷新（401 处理）

```cpp
void handle401(const Batch& batch) {
    if (auto newToken = employee_auth::tryRefreshToken(); newToken) {
        retry(batch, *newToken);  // 1 次
    } else {
        pauseUntil(now() + 3600 * 1000);  // 暂停 1 小时
        criticalLog("audit token refresh failed, paused 1h");
    }
}
```

**依赖**：`employee_auth::tryRefreshToken()` 接口存在。如果当前不存在，需要先实现，或 token 长效 → 401 直接进 1 小时暂停。**待用户确认。**

### 5.4 Hook 内部错误

Hook 永远不抛异常：

```cpp
void onOutboundRpc(mtpRequestId rpcId, const TLObject& params) {
    try {
        if (!isWhitelisted(rpcId)) return;
        auto event = AuditDecoder::decode(rpcId, params);
        AuditQueue::push(std::move(event));
    } catch (const std::exception& e) {
        LOG(("Audit: decode failed for rpc %1: %2").arg(rpcId).arg(e.what()));
        AuditQueue::pushMetaDropped(1, "decode_failed", nowMs(), nowMs());
    } catch (...) {
        LOG(("Audit: decode crashed for rpc %1").arg(rpcId));
        AuditQueue::pushMetaDropped(1, "decode_crashed", nowMs(), nowMs());
    }
}
```

### 5.5 优雅关闭

```cpp
void onShutdown() {
    auto timeout = QDeadlineTimer(5'000);
    while (!timeout.hasExpired() && AuditQueue::pendingCount() > 0) {
        AuditUploader::triggerFlush();
        std::this_thread::sleep_for(100ms);
    }
    AuditQueue::close();
}
```

5 秒上限避免员工关窗卡住，剩余下次启动续传。

### 5.6 本地日志

用 tdesktop `LOG(())` 宏，前缀 `Audit:`：
```cpp
LOG(("Audit: queue size %1 events / %2 bytes").arg(count).arg(bytes));
LOG(("Audit: upload batch %1 events to %2, status %3").arg(n).arg(url).arg(httpCode));
LOG(("Audit: token refresh failed, paused 1h"));
```

落 `tdata/log.txt`。

## 6. 测试策略

### 6.1 Layer 1：Decoder 单元测试

`tests/employee/test_audit_decoder.cpp`，每个 whitelist RPC 类型至少 2 个用例（典型场景 + 一个边界），预估 ~25 cases。

### 6.2 Layer 2：Queue 持久化测试

`tests/employee/test_audit_queue.cpp`：
- 重启恢复（in_flight 重置）
- 容量爆仓（丢老 + 入 meta.dropped）
- event_id 幂等

### 6.3 Layer 3：Uploader 测试（mock HTTP server）

`tests/employee/test_audit_uploader.cpp`：
- 5xx 退避重试
- 413 切半重试
- 401 刷新 token
- 429 respect Retry-After

### 6.4 Layer 4：端到端

**自动 e2e（CI 跑）**：mock backend + tdesktop 测试构建 + 脚本驱动 + 断言。

**手工 QA checklist（发版必跑）**：

| # | 操作 | 后端应该收到 |
|---|---|---|
| 1 | 发文本消息 | 1× `msg.send` |
| 2 | 发图片 + caption | 1× `msg.send`（含 media + caption） |
| 3 | 转发消息 | 1× `msg.forward` |
| 4 | 编辑消息 | 1× `msg.edit` |
| 5 | 单向删自己消息 | 1× `msg.delete` (revoke=false) |
| 6 | 双向删自己消息 | 1× `msg.delete` (revoke=true) |
| 7 | 收对方消息 | 1× `msg.recv` |
| 8 | 清空整个对话 | 1× `history.clear` |
| 9 | 加新联系人 | 1× `contact.add` |
| 10 | 修改联系人备注 | 1× `contact.add` |
| 11 | 删联系人 | 1× `contact.delete` |
| 12 | 拉黑 | 1× `contact.block` |
| 13 | 解除拉黑 | 1× `contact.unblock` |
| 14 | 断网 → 发 5 条 → 恢复网络 | 5× event 在 ~10s 内补上传 |
| 15 | 关客户端时队列非空 → 重启 | 上次未发的事件被发出 |
| 16 | 任务管理器强杀 → 重启 | in-flight 事件重发，event_id 去重 |

### 6.5 防"hook 漏拦"两道闸

1. **CI lint 扫 `api.tl`**：自动列出名字含 `delete|block|send|forward|history|contact` 的 RPC，跟 `audit_decoder.cpp` whitelist 对比；新增没在 whitelist 里的 RPC CI fail，强迫显式决策（加入 whitelist 或加 `// AUDIT_NO_TRACK:` 注释）
2. **运行时 sampling**：调试构建，每 1000 个 RPC 抽 1 个不在 whitelist 的写 LOG，运维 grep 找异常增长

### 6.6 性能 SLA

- p50 < 100µs/event（含 decode + push 队列）
- p99 < 500µs/event
- 内存占用 < 5MB（队列在 100MB 上限内）

如超出，先优化 SQLite 写（合并 transaction、prepared statement 缓存）。

## 7. 待用户确认的开放问题

1. **`employee_auth::tryRefreshToken()` 是否存在？** 决定 401 处理走"刷新一次"还是"直接 1h 暂停"两条路。
2. **审计后端 URL** 由你最终定（spec 写的是占位 `https://audit.your-company.com/v1/audit/events`）。
3. **缩略图 `thumb_b64` 嵌入还是只存 file_id？** spec 当前是嵌入（≤32KB），若想瘦身可改为后端按需拉。
4. **`msg.recv` 入站快照范围**：v1 提议捕获**所有**入站新消息（包括员工的私人聊天、群组、频道、bot 等）。这是最保险但数据量最大。可选收窄方式：
   - 只捕获员工"已加入联系人"的私聊（漏频道/群组/未加好友的客户）
   - 只捕获服务端下发的"客户白名单"peer（需后端有此列表 + 客户端有拉取接口）
   - 排除明显非工作的（频道订阅、bot 通知）— 但靠规则判断不可靠

   **你选哪种？** 默认 v1 全捕获。
5. **Uploader 线程崩溃后是否自动重启？** spec 当前没明说。建议：用 `std::thread` + 顶层 `try/catch`，捕获到任何异常 → 5s 后重启线程；连续重启 5 次失败则永久停止 + 写 critical log。
6. **Log 文件路径**：spec 说 `tdata/log.txt`，但 tdesktop 实际日志路径可能不同（待实施时按现有 LOG 宏的实际目标地址确认）。

## 8. 不做的事（明确划界）

- ❌ TLS cert pinning（信任客户端 + 增加运维成本）
- ❌ 加密本地 SQLite（依赖系统盘加密）
- ❌ 客户端→后端心跳（v2 再加）
- ❌ 文件二进制上传（只存 file_id + 缩略图）
- ❌ 通话录音（只存元数据 + 时长）
- ❌ 防员工反编译/旁路（信任客户端）
- ❌ 群组 / 频道 / 文件上传等其他动作的审计（用户明确未选）
- ❌ 阻断式 hook（失败时拦截员工动作）—— 选了"不阻断"

## 9. 后续可能扩展

- v2：增加心跳机制，后端可主动检测客户端在线
- v2：增加内部事件总线，支持多消费者（本地审计快查工具、实时预警）
- v2：合规模式可选启用本地 SQLite 加密
- v2：拍员工屏 / 上传文件二进制（如果合规要求）
- v3：扩到其他平台（macOS / Linux）
