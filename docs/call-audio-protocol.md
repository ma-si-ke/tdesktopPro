# 通话录音上传协议 v1

本文档定义**企业客户端**与**服务端**之间,用于通话录音上传的接口协议。

- 客户端在每次语音/视频通话时,把**混音后的单声道音频**通过一条连接上传给服务端。
- 客户端**只上传一条流**。服务端如何使用该音频流,不在本协议范围内。

> 视频通话按语音处理:**只上传音频,不上传视频**。是否为视频通话仅在元数据中标记。

---

## 1. 名词约定

| 名词 | 含义 |
|---|---|
| `callId` | 一次通话的全局唯一标识,由客户端生成,整个通话生命周期不变。 |
| 帧 (frame) | 一个音频编码包(默认一个 Opus 包)。 |
| `seq` | 帧序号,从 `0` 起,每帧 `+1`,单调递增,唯一。 |
| 媒体时钟 `tsMs` | 从本通话第一个采样点起算的毫秒数,单调递增。 |
| 会话 (session) | 一次 WebSocket 连接。一个 `callId` 可能跨多个会话(断线重连)。 |

所有时间戳(`startedAt`/`endedAt` 等)为 **UTC 毫秒(epoch ms)**;`tsMs` 为相对媒体时钟毫秒。
所有多字节整数在二进制帧中一律 **大端序 (big-endian)**。JSON 一律 **UTF-8**。

---

## 2. 传输与鉴权

本服务为**独立后端**,自有域名与部署。下文以 `<call-audio-host>` 表示其地址。

### 2.1 端点与协议

- 协议:**WebSocket over TLS (`wss://`)**。
- 端点:`wss://<call-audio-host>/stream`
- 通话进行中,音频通过此连接边采边传。
- 一个 `callId` 对应一条 WebSocket 连接。**禁止**同一 `callId` 同时开多条连接上传。

### 2.2 鉴权:固定 API Key

- WebSocket 握手请求头携带:
  `X-API-Key: <apiKey>`
- `<apiKey>` 为服务端与客户端约定的**固定密钥**,用于认证「这是合法的企业客户端」。鉴权失败服务端以关闭码 `4001` 拒绝。
- 身份(工号、账号、对端 TgId)由 [`hello`](#51-hello客户端--服务端会话第一条消息) 消息携带,与 API Key 无关:API Key 只认「客户端合法」,`hello` 才标明「哪个员工、和谁通话」。

---

## 3. 消息类型总览

WebSocket 原生区分**文本帧**与**二进制帧**,本协议据此复用,无需自定义多路复用:

| WS 帧类型 | 用途 |
|---|---|
| **文本帧 (Text)** | 控制消息,JSON。见 [§5](#5-控制消息文本json)。 |
| **二进制帧 (Binary)** | 音频数据。见 [§6](#6-音频数据二进制)。 |

控制消息共 4 种:`hello` / `ready` / `bye`(客户端与服务端交互)、`error`(服务端下发)。

---

## 4. 会话流程

### 4.1 正常流程

```
客户端                                 服务端
  │  WS 握手 (X-API-Key: <apiKey>)        │
  │ ───────────────────────────────────▶ │
  │  Text: hello (元数据 + 音频参数)      │
  │ ───────────────────────────────────▶ │
  │              Text: ready (resumeFrom) │
  │ ◀─────────────────────────────────── │
  │  Binary: 音频帧 seq=0,1,2,...         │
  │ ───────────────────────────────────▶ │
  │  ...                                  │
  │  Text: bye (endedAt, lastSeq)         │
  │ ───────────────────────────────────▶ │
  │                WS Close 1000 (正常)   │
  │ ◀─────────────────────────────────── │
```

### 4.2 断线重连流程

```
  (传输过程中连接断开)
  │  WS 重新握手                          │
  │ ───────────────────────────────────▶ │
  │  Text: hello (相同 callId, resume=true)│
  │ ───────────────────────────────────▶ │
  │        Text: ready (resumeFrom = 已收到的下一个期望 seq)
  │ ◀─────────────────────────────────── │
  │  Binary: 从 resumeFrom 起继续发送     │
  │ ───────────────────────────────────▶ │
```

`hello` 对同一 `callId` **幂等**,既用于首次建立,也用于重连续传。服务端以 `callId` 为准。

---

## 5. 控制消息(文本 / JSON)

### 5.1 `hello`(客户端 → 服务端,会话第一条消息)

WebSocket 建立后,客户端**必须**先发送一条 `hello`,之后才能发送音频帧。

```json
{
  "type": "hello",
  "protocol": 1,
  "callId": "9f2c1a7e-6b40-4e0a-8b1c-2f3d4e5a6b7c",
  "resume": false,

  "employeeNo": "E10086",
  "accountName": "zhangsan",
  "peerTgId": 123456789,
  "direction": "out",
  "isVideo": true,
  "startedAt": 1733650000000,
  "clientVersion": "7.0.8",

  "audio": {
    "codec": "opus",
    "sampleRate": 48000,
    "channels": 1,
    "frameMs": 60,
    "bitrate": 24000
  }
}
```

字段说明:

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `type` | string | 是 | 固定 `"hello"`。 |
| `protocol` | int | 是 | 协议版本,当前 `1`。 |
| `callId` | string | 是 | 全局唯一(UUID),通话全程不变。 |
| `resume` | bool | 否 | `true` 表示这是断线重连(便于服务端记日志);默认 `false`。行为以 `callId` 为准。 |
| `employeeNo` | string | 是 | **工号**。 |
| `accountName` | string | 是 | **后台账号名**。 |
| `peerTgId` | int64 | 是 | **对端 Telegram ID**(带符号):私聊为顾客用户 ID(正数),群组为群/频道 ID(负数)。服务端按不透明整数存储即可。 |
| `direction` | string | 是 | `"out"`(员工主叫)/ `"in"`(顾客主叫)。群组通话无主被叫之分,固定 `"out"`。 |
| `isVideo` | bool | 是 | 原始通话是否为视频通话(仅标记,音频照录)。 |
| `startedAt` | int64 | 是 | 通话开始时间,UTC 毫秒。 |
| `clientVersion` | string | 是 | 客户端版本号。 |
| `audio.codec` | string | 是 | `"opus"`(默认)或 `"pcm_s16le"`(兜底,不推荐)。 |
| `audio.sampleRate` | int | 是 | 采样率,默认 `48000`。 |
| `audio.channels` | int | 是 | 声道数,固定 `1`(单声道混音)。 |
| `audio.frameMs` | int | 是 | 单帧时长毫秒,默认 `60`。用于估算缺帧应补的静音长度。 |
| `audio.bitrate` | int | 否 | 目标码率(bps),仅 Opus 有意义。 |

### 5.2 `ready`(服务端 → 客户端)

服务端接受 `hello` 后回复。客户端收到 `ready` 才开始发送音频帧。

```json
{
  "type": "ready",
  "callId": "9f2c1a7e-6b40-4e0a-8b1c-2f3d4e5a6b7c",
  "sessionId": "srv-8827",
  "resumeFrom": 0
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `sessionId` | string | 服务端为本次连接分配的标识,用于日志排查。 |
| `resumeFrom` | uint32 | 服务端期望收到的**下一个** `seq`。新会话为 `0`;重连时为服务端已持久化的下一个序号,客户端从该序号起继续发送(见 [§7](#7-重连去重与缺帧))。 |

### 5.3 `bye`(客户端 → 服务端,通话结束)

音频发送完毕后发送,随后客户端可关闭连接。

```json
{
  "type": "bye",
  "callId": "9f2c1a7e-6b40-4e0a-8b1c-2f3d4e5a6b7c",
  "endedAt": 1733650123000,
  "lastSeq": 2050,
  "frameCount": 2051,
  "reason": "hangup"
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `endedAt` | int64 | 通话结束时间,UTC 毫秒。 |
| `lastSeq` | uint32 | 最后一帧的 `seq`。 |
| `frameCount` | uint32 | 本通话应有的总帧数(用于服务端校验完整性)。 |
| `reason` | string | `"hangup"`(正常挂断)/ `"failed"`(通话失败)/ `"error"`(客户端异常)。 |

服务端收到 `bye` 后以关闭码 `1000` 关闭连接。

### 5.4 `error`(服务端 → 客户端)

```json
{
  "type": "error",
  "code": "BAD_HELLO",
  "message": "missing field: employeeNo",
  "fatal": true
}
```

`fatal=true` 时服务端会随后关闭连接;客户端不应就该 `callId` 立即重连(除非是网络类错误)。`code` 取值见 [§8 关闭码](#8-关闭码) 对应表。

---

## 6. 音频数据(二进制)

### 6.1 帧头布局

每个 WebSocket **二进制帧**由 1 个或多个音频帧顺序拼接而成。每个音频帧结构如下(大端序):

```
 偏移  长度  字段
  0     1    version    固定 0x01
  1     1    flags      位标志(见下)
  2     4    seq        uint32,帧序号
  6     8    tsMs       uint64,媒体时钟毫秒
 14     2    payloadLen uint16,payload 字节数
 16     N    payload    音频编码数据(一个 Opus 包)
```

- 头部固定 16 字节,`payload` 紧随其后。一个 WS 二进制帧内可连续拼接多个「头+payload」。
- `flags` 位定义:
  - bit0 `GAP_BEFORE`:置位表示此帧与上一帧之间存在已知缺口(有帧永久丢失),服务端可据此补静音。
  - bit1–7:保留,置 0。

### 6.2 音频参数

- 默认编码:**Opus**,单声道,48000 Hz,每帧 `60ms`(约每秒 16~17 帧)。
- 编码具体参数以 `hello.audio` 为准;服务端**必须**按 `hello` 声明的参数解码,不得假设固定值。
- `payload` 为**裸编码包**(不含容器)。

---

## 7. 重连、去重与缺帧

- **唯一键**:服务端以 `(callId, seq)` 为唯一键。收到重复 `seq` 直接丢弃(幂等)。
- **重连**:连接断开后,客户端用**相同 `callId`** 重新握手并发送 `hello`(`resume=true`)。服务端在 `ready.resumeFrom` 中返回下一个期望 `seq`,客户端据此从该 `seq` 起继续发送。
- **缺帧**:当某些 `seq` 永久丢失时,后续第一帧会置 `GAP_BEFORE`,`tsMs` 保持真实媒体时钟,据此可算出缺口时长。
- **完整性**:`bye.frameCount` 给出本通话应有的总帧数,供服务端与实际收到帧数比对。

---

## 8. 关闭码

WebSocket 关闭帧使用以下状态码:

| 关闭码 | `error.code` | 含义 |
|---|---|---|
| `1000` | — | 正常关闭(`bye` 之后)。 |
| `1011` | `SERVER_ERROR` | 服务端内部错误。客户端可重连续传。 |
| `4001` | `AUTH_FAILED` | 鉴权失败。不重连。 |
| `4002` | `BAD_HELLO` | `hello` 缺字段或格式错误。修正后方可重连。 |
| `4003` | `CALL_ID_CONFLICT` | 该 `callId` 已被终结(已收到 `bye`),不接受续传。 |
| `4004` | `UNSUPPORTED_CODEC` | 不支持 `hello.audio.codec`。 |

网络类中断(非上述业务码)客户端应按退避策略重连续传。

---

## 9. 传输安全

- 全程 TLS(`wss://`)。
- 鉴权即固定 `X-API-Key`(见 [§2.2](#22-鉴权固定-api-key))。

---

## 10. 版本

- `v1`(本文档):单条 WebSocket 上传混音单声道 Opus。
- 后续变更通过 `hello.protocol` 递增协商。
