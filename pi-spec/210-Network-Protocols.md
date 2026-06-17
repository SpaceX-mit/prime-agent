# 210 · 网络协议 (Network Protocols)

> C/C++ 端口必须自己实现的网络协议。涵盖 LLM provider 协议、OAuth 协议、终端协议、文件协议。

## 1. LLM Provider 协议

### 1.1 OpenAI Chat Completions（最广泛）

```
POST {baseUrl}/chat/completions
Authorization: Bearer {apiKey}
Content-Type: application/json
Stream: true

{
  "model": "...",
  "messages": [{ "role": "user|assistant|tool|system", "content": ... }],
  "tools": [...],
  "temperature": ...,
  "max_tokens": ...,
  "stream_options": { "include_usage": true }
}
```

**响应（SSE）**：
```
data: {"id":"chatcmpl-...","choices":[{"delta":{"role":"assistant"},"index":0}]}
data: {"id":"chatcmpl-...","choices":[{"delta":{"content":"Hello"},"index":0}]}
data: {"id":"chatcmpl-...","choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_xxx","function":{"name":"bash","arguments":""}}]},"index":0}]}
data: {"id":"chatcmpl-...","choices":[{"finish_reason":"tool_calls","delta":{}}]}
data: {"id":"chatcmpl-...","choices":[],"usage":{"prompt_tokens":...}}
data: [DONE]
```

**关键解析点**：
- `data: ` 前缀 + 一行一个 JSON
- `[DONE]` 结束
- tool_call.arguments 是**部分 JSON 字符串**，要 `partial-json` 解析
- thinking 模型（DeepSeek / o1）有 `reasoning_content` 字段
- prompt cache：`prompt_tokens_details.cached_tokens`、`cache_creation_input_tokens`

### 1.2 OpenAI Responses

```
POST {baseUrl}/responses
{
  "model": "...",
  "input": [...],
  "tools": [...],
  "instructions": "...",   // 替代 system
  "stream": true,
  "store": false,
  "reasoning": { "effort": "low|medium|high" }
}
```

**响应**：
```
event: response.created
data: {"type":"response.created","response":{...}}

event: response.output_text.delta
data: {"type":"response.output_text.delta","delta":"Hello"}

event: response.function_call_arguments.delta
data: {"type":"response.function_call_arguments.delta","delta":"{\""}

event: response.completed
data: {"type":"response.completed","response":{"usage":{...}}}
```

### 1.3 OpenAI Codex Responses（**特殊**）

```
WebSocket: wss://chatgpt.com/backend-api/codex/responses
Headers: ChatGPT-Account-Id, session_id, OpenAI-Organization, OpenAI-Beta
Protocol: 自定义 JSON 事件（不是标准 OpenAI）
```

**消息流**：
```json
// client → server
{"type":"response.create","response":{...}}

// server → client
{"type":"response.created", ...}
{"type":"response.output_text.delta", ...}
{"type":"response.completed", ...}
```

> **关键差别**：Codex 走 WSS（WebSocket Secure），不是 HTTP+SSE。约 1500 行 C++ 实现。

### 1.4 Anthropic Messages

```
POST {baseUrl}/v1/messages
x-api-key: {apiKey}
anthropic-version: 2023-06-01
Content-Type: application/json

{
  "model": "claude-...",
  "system": "...",
  "messages": [...],
  "tools": [...],
  "max_tokens": ...,
  "stream": true,
  "thinking": { "type": "enabled", "budget_tokens": ... }
}
```

**响应（SSE）**：
```
event: message_start
data: {"type":"message_start","message":{...,"usage":{...}}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: ping

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":...}}

event: message_stop
data: {"type":"message_stop"}
```

**关键点**：
- `event:` 标记事件类型
- `ping` 事件要忽略
- thinking 块是独立 `content_block`
- cache：`cache_creation_input_tokens` / `cache_read_input_tokens`
- 1h cache：headers 加 `anthropic-beta: prompt-caching-2024-07-31` + `cache_control: {type: "ephemeral", ttl: "1h"}`

### 1.5 Google Gemini

```
POST {baseUrl}/v1beta/models/{model}:streamGenerateContent?key={apiKey}
Content-Type: application/json
{
  "contents": [{ "role": "user|model", "parts": [{"text": "..."}] }],
  "systemInstruction": { "parts": [{"text": "..."}] },
  "tools": [...],
  "generationConfig": { "temperature": ..., "maxOutputTokens": ..., "thinkingConfig": {"thinkingBudget": ...} }
}
```

**响应（JSON array 流）**：
```
[{"candidates":[{"content":{"parts":[{"text":"Hello"}],"role":"model"}}], "usageMetadata": {...}}, ...]
```

> **注意**：Gemini 把整个 SSE 流**包成一个 JSON 数组**（每行一个完整 JSON），不是单对象。

### 1.6 Google Vertex AI

类似 Gemini，但端点是 `{region}-aiplatform.googleapis.com`，auth 用 OAuth Bearer。

### 1.7 Mistral Conversations

```
POST {baseUrl}/v1/chat/completions
```
完全 OpenAI 兼容。

### 1.8 AWS Bedrock Converse

```
POST https://bedrock-runtime.{region}.amazonaws.com/model/{modelId}/converse-stream
Authorization: AWS4-HMAC-SHA256 ...   // SigV4
Content-Type: application/json
X-Amz-Date: ...
{
  "messages": [...],
  "system": [...],
  "tools": [...],
  "inferenceConfig": { ... }
}
```

**响应（AWS event stream binary）**：
- 不像 Anthropic 用 SSE，**AWS event stream 是二进制 protocol**
- 每条消息有 prelude (12 字节 total length) + headers + payload + crc32
- 需自实现解析（或改用 Anthropic provider 走 Bedrock 上的 Claude，避免 event stream 复杂度）

> **C/C++ 端口建议**：用 Bedrock 上的 Claude 走 `providers/anthropic.ts` 的逻辑，不实现 event stream。

### 1.9 SSE 通用规范

```
field: value\n
field: value\n
\n                // 空行 = 事件结束
```

- `event:` 类型（可选，默认 `message`）
- `data:` 一行或多行
- `id:` 事件 ID
- `retry:` 重连间隔
- 多行 `data:` 用 `\n` 拼接

## 2. OAuth 协议

### 2.1 Authorization Code + PKCE（Anthropic / OpenAI Codex）

```
1. 生成 code_verifier (43-128 chars random)
2. 计算 code_challenge = base64url(sha256(verifier))
3. GET {auth_url}?client_id=...&response_type=code&code_challenge=...&code_challenge_method=S256&redirect_uri=http://localhost:{port}/callback
4. 用户授权 → browser redirect → http://localhost:{port}/callback?code=...
5. POST {token_url} { grant_type:"authorization_code", code, redirect_uri, client_id, code_verifier }
6. 拿 {access_token, refresh_token, expires_in}
```

### 2.2 Device Code（GitHub Copilot / OpenAI Codex 备选）

```
1. POST {device_url} {client_id, scope} → {device_code, user_code, verification_uri, interval, expires_in}
2. 显示 user_code，让用户到 verification_uri 输入
3. 轮询 POST {token_url} {grant_type:"urn:ietf:params:oauth:grant-type:device_code", device_code, client_id}
4. 响应：authorization_pending / slow_down / access_denied / {access_token, refresh_token, ...}
```

### 2.3 Token Refresh

```
POST {token_url} {
  grant_type: "refresh_token",
  refresh_token: "...",
  client_id: "...",
}
```

### 2.4 本地 HTTP Server（OAuth callback）

```c
// 流程
1. socket(); bind(127.0.0.1:0); listen();
2. accept() 等连接
3. recv() 读 HTTP GET / 第一个
4. 解析 query string 拿 code
5. send() 返回 200 + HTML "you can close this tab"
6. close()
```

**关键**：bind 端口 = 0（OS 分配），启动时把 port 写到 URL redirect_uri。

## 3. AWS SigV4 签名

```c
// 算法
1. canonical_request = METHOD\nURI\nQUERY\nCANONICAL_HEADERS\nSIGNED_HEADERS\nPAYLOAD_HASH
2. string_to_sign = "AWS4-HMAC-SHA256\n" ISO8601_BASIC "\n" SCOPE "\n" sha256(canonical_request)
3. signing_key = HMAC("AWS4" + SECRET, DATE) → HMAC(..., REGION) → HMAC(..., SERVICE) → HMAC(..., "aws4_request")
4. signature = hex(hmac(signing_key, string_to_sign))
5. Authorization: AWS4-HMAC-SHA256 Credential=ACCESS/SCOPE, SignedHeaders=..., Signature=...
```

~200 行 C 代码 + OpenSSL HMAC。

## 4. 终端协议

### 4.1 Kitty 键盘协议

```
// 启用（写终端）
\x1b[>7u   // push 7 = report_events + report_alternate + report_all
\x1b[?u    // push current
\x1b[c     // DA1 query

// 响应（读终端）
\x1b[?7u   // flags = 7

// 解析按键（启用后）
\x1b[97;5u    // Ctrl+A（5 = ctrl modifier, 97 = 'a'）
\x1b[97;2u    // Shift+A
\x1b[97;3u    // Alt+A
```

### 4.2 Kitty image protocol

```
\x1b_Ga=T,f=100,t=f,s=1,v=1;DATA\x1b\\   // 传图（DATA = base64）
\x1b_Ga=T,f=100,t=f,s=1,v=1,m=1;DATA\x1b\\ // 续传
\x1b_Ga=Q\x1b\\                            // query
\x1b_Ga=d,i=ID\x1b\\                       // delete
```

### 4.3 iTerm2 image

```
\x1b]1337;File=size=NUM;width=W;height=H;inline=1;base64=DATA\x07
```

### 4.4 OSC 9;4 进度

```
\x1b]9;4;0\x07    // remove
\x1b]9;4;1;50\x07 // set 50%
\x1b]9;4;3\x07    // indeterminate
```

### 4.5 OSC 8 hyperlink

```
\x1b]8;;URL\x07TEXT\x1b]8;;\x07
```

### 4.6 OSC 11 背景色

```
\x1b]11;rgb:RRRR/GGGG/BBBB\x07
```

### 4.7 DA1（Primary Device Attributes）

```
write: \x1b[c
read:  \x1b[?1;2c     // VT220
       \x1b[?63;1;2c  // xterm 256 color
       \x1b[?1;0c     // VT100
```

## 5. JSONL over stdio（RPC 模式）

```
// 输入
{"type":"prompt","id":"1","text":"hi"}\n
{"type":"set_model","id":"2","provider":"anthropic","modelId":"claude-sonnet-4-5"}\n

// 输出（每行一个 JSON）
{"type":"event","event_type":"message_update",...}\n
{"type":"response","id":"1","command":"prompt","success":true}\n
```

**关键**：每行**必须**独立解析；不要跨越多行。

## 6. Git 协议（pi package install）

C/C++ 端口不直接实现 git 协议；**调用系统 git 命令**：
```bash
git clone <url>
git fetch
git checkout <sha>
```

但需要：
- 解析 git URL：SSH (`git@github.com:foo/bar`) / HTTPS (`https://...`) / shorthand (`foo/bar`) / tarball
- 调用 SSH 时不弹密码（用已配置的 key）

## 7. 协议总结表

| 协议 | 自实现 C/C++ | 现成库 | 行数估算 |
| --- | --- | --- | ---: |
| OpenAI Chat Completions | ✅ | — | ~500 |
| OpenAI Responses | ✅ | — | ~400 |
| OpenAI Codex WSS | ✅ | libwebsockets（可选） | ~1500 |
| Anthropic Messages | ✅ | — | ~500 |
| Google Gemini | ✅ | — | ~400 |
| Google Vertex | ✅ | — | ~300（+ OAuth） |
| Mistral | ✅ | — | ~50（复用 OpenAI） |
| AWS Bedrock | 建议跳过 | — | ~800 |
| AWS SigV4 | ✅ | OpenSSL HMAC | ~200 |
| OAuth 2.0 PKCE | ✅ | — | ~200 |
| OAuth 2.0 Device Code | ✅ | — | ~100 |
| OAuth 本地 callback server | ✅ | libmicrohttpd（可选） | ~150 |
| HTTP 客户端 | — | **libcurl** | — |
| TLS | — | OpenSSL (libcurl 用) | — |
| SSE 解析 | ✅ | — | ~150 |
| WebSocket | ✅ | libwebsockets | ~400 |
| JSONL 解析 | ✅ | — | ~50 |
| 终端协议 | ✅ | — | ~800 |
| Git 协议 | ❌ 调外部 git | — | 0 |
