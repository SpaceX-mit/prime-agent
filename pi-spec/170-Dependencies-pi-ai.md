# 170 · `pi-ai` 的依赖

> 源码 `packages/ai/package.json`：10 个直接依赖 + 1 bin + devDeps。

## 1. 直接依赖（10 个）

### 1.1 LLM Provider SDK

| 包 | 版本 | 字节 | 用途 | 调用点 | C/C++ 替代 |
| --- | --- | ---: | --- | --- | --- |
| `@anthropic-ai/sdk` | 0.91.1 | ~5 MB | Anthropic Messages API 客户端（含 streaming、cache、tools） | `providers/anthropic.ts` | **自实现**（HTTP+SSE，~500 行） |
| `openai` | 6.26.0 | ~10 MB | OpenAI Chat Completions + Responses + Code mode | `providers/openai-completions.ts`<br>`providers/openai-responses.ts`<br>`providers/openai-codex-responses.ts` | **自实现**（HTTP+SSE+WS，~1500 行） |
| `@google/genai` | 1.52.0 | ~3 MB | Google Gemini + Vertex AI | `providers/google.ts`<br>`providers/google-vertex.ts` | **自实现**（HTTP+SSE，~500 行） |
| `@mistralai/mistralai` | 2.2.1 | ~1 MB | Mistral Conversations | `providers/mistralai.ts` | **自实现**（HTTP+SSE，~300 行） |
| `@aws-sdk/client-bedrock-runtime` | 3.1048.0 | **~50 MB** | AWS Bedrock Converse + SigV4 签名 | `providers/amazon-bedrock.ts` | **自实现 SigV4**（HTTP+SSE，~600 行） |

> **AWS SDK 依赖膨胀**：完整 `@aws-sdk/*` 包树有上百个间接依赖。pi 只用了一个 `ConverseStreamCommand`。C/C++ 端口**不要**用任何 AWS SDK，直接实现 SigV4 签名。

### 1.2 Proxy / HTTP 工具

| 包 | 版本 | 用途 | 调用点 | C/C++ 替代 |
| --- | --- | ---: | --- | --- |
| `http-proxy-agent` | 7.0.2 | HTTP/HTTPS 代理（Anthropic / OpenAI / Google） | `providers/*.ts` 通过 `node-http-proxy.ts` | libcurl `CURLOPT_PROXY` |
| `https-proxy-agent` | 7.0.6 | 同上（HTTPS 优先） | 同上 | 同上 |
| `@smithy/node-http-handler` | 4.7.3 | AWS SDK 自定义 HTTP handler（注入 header + proxy） | `providers/amazon-bedrock.ts` | 不需要（自实现走 libcurl） |

### 1.3 解析 / 工具

| 包 | 版本 | 用途 | 调用点 | C/C++ 替代 |
| --- | --- | ---: | --- | --- |
| `typebox` | 1.1.38 | JSON Schema + TS 类型推导 | 全部 provider + types | C++ 用 nlohmann/json + schema；C 不用 schema |
| `partial-json` | 0.1.7 | 流式部分 JSON 解析（tool_call 拼接） | `utils/json-parse.ts` | 自实现（~50 行） |

## 2. devDependencies

| 包 | 用途 | 是否进 dist |
| --- | --- | --- |
| `@types/node` 24.12.4 | TS 类型 | ❌ |
| `canvas` 3.2.3 | node-canvas（测试用，模拟图像） | ❌ |
| `vitest` 3.2.4 | 测试 | ❌ |

## 3. Provider 实现内部细节

### 3.1 `anthropic.ts` 调用面

```ts
import Anthropic from "@anthropic-ai/sdk";
import type { MessageCreateParamsStreaming, MessageParam, RawMessageStreamEvent, ... } from "@anthropic-ai/sdk/resources/messages.js";

// 主要 API
client.messages.stream({...})                    // → AsyncIterable<RawMessageStreamEvent>
client.messages.create({...})                     // → Message
client.beta.messages.create({...})                // beta (1h cache)
```

**实际只用 ~6 个方法**。C/C++ 端口可裸写：
- `POST /v1/messages` (HTTP, Content-Type: application/json, x-api-key, anthropic-version: 2023-06-01)
- `POST /v1/messages?beta=true` (cache_control: ephemeral + ttl: 1h)
- 读 SSE 流，转 `AssistantMessageEvent`

### 3.2 `openai-completions.ts` 调用面

```ts
import OpenAI from "openai";
client.chat.completions.create({ stream: true, ... })   // → AsyncIterable<ChatCompletionChunk>
client.responses.create({ stream: true, ... })           // → AsyncIterable<ResponseStreamEvent>
```

C/C++ 端口可裸写：OpenAI 协议非常标准。

### 3.3 `openai-codex-responses.ts` 调用面

```ts
// 走 OpenAI 兼容 HTTP + WebSocket
// 走 "responses" API 但后端是 ChatGPT 订阅
// URL: wss://chatgpt.com/backend-api/codex/responses
// 通过自定义 WebSocket headers (ChatGPT-Account-Id, session_id, ...)
```

**复杂点**：WebSocket 协议、私有 header、cache probe、cached continuation state。约 1500 行 C++。

### 3.4 `amazon-bedrock.ts` 调用面

```ts
import { BedrockRuntimeClient, ConverseStreamCommand } from "@aws-sdk/client-bedrock-runtime";
import { NodeHttpHandler } from "@smithy/node-http-handler";

client.send(new ConverseStreamCommand({...}));
```

**关键点**：自己实现 SigV4 签名（HMAC-SHA256 × 4 次 + AWS date format）。已有 libcurl + OpenSSL 就能做。

### 3.5 `google.ts` / `google-vertex.ts` 调用面

```ts
import { GoogleGenAI } from "@google/genai";
const genai = new GoogleGenAI({ apiKey, httpOptions: { baseUrl, headers, ... } });
genai.models.generateContentStream({ model, contents, config });
```

C/C++ 端口：标准 HTTP + SSE。Vertex 还需 OAuth token 交换（先 `POST /v1/token`，再 `Authorization: Bearer`）。

## 4. OAuth 模块内部依赖

`packages/ai/src/utils/oauth/`（8 文件）：

| 文件 | 依赖 |
| --- | --- |
| `anthropic.ts` | 裸 HTTP + `pkce.ts` |
| `github-copilot.ts` | 裸 HTTP |
| `openai-codex.ts` | 裸 HTTP + `pkce.ts` + `device-code.ts` + `oauth-page.ts` |
| `device-code.ts` | 轮询逻辑 |
| `pkce.ts` | **Web Crypto API**（SHA-256, base64url） |
| `oauth-page.ts` | 本地 HTTP server + browser launch |
| `types.ts` | 类型 |
| `index.ts` | 注册 + 导出 |

> **Web Crypto API**：用 `crypto.subtle.digest("SHA-256", ...)` + `btoa()`。C/C++ 端口用 OpenSSL `EVP_Digest` + `Base64URL`。

## 5. 间接依赖（锁文件里出现但不直接 import）

通过 `@anthropic-ai/sdk`：
- `@anthropic-ai/sdk` 内部：`@anthropic-ai/tokenizer`, `p-retry`, `headers-polyfill`, `@types/node`...

通过 `openai`：
- `openai` 内部基本不引第三方；自带 fetch 包装

通过 `@aws-sdk/*`（**重要**：~50 MB 树）：
- `@aws-crypto/*`（加密）
- `@aws-sdk/util-*`, `@aws-sdk/middleware-*`
- `@smithy/*`（几十个）

**C/C++ 端口策略**：全部跳过，不需要。

## 6. 总结：pi-ai 的 C/C++ 端口方案

| 类型 | 自实现 | 用现成库 |
| --- | --- | --- |
| Provider HTTP+SSE 协议 | ✅ ~5000 行（5 大 provider） | — |
| WebSocket（Codex） | ✅ ~1500 行 | 可选 libwebsockets |
| OAuth 流程 | ✅ ~600 行 | — |
| SigV4 签名 | ✅ ~200 行 | 可选 aws-cpp-sdk |
| HTTP 客户端 | — | **libcurl**（推荐） |
| JSON | — | **nlohmann/json** 或 simdjson |
| JSON Schema (TypeBox) | ✅ 简化版（不引 schema lib） | — |
| 部分 JSON 解析 | ✅ ~80 行 | — |
| HTTP proxy | — | libcurl 自带 |
| TLS | — | OpenSSL（libcurl 用） |

详见 [230-CPP-Port-Mapping.md](./230-CPP-Port-Mapping.md)。
