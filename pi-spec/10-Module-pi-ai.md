# 10 · 模块 `@earendil-works/pi-ai`

> 角色：把 30+ 个 LLM Provider（OpenAI / Anthropic / Google / Bedrock / Mistral / OpenRouter / Ollama 兼容…）抽象成一套统一 API，是整个仓库的最低层。

## 1. 入口与导出面

源码：`packages/ai/src/index.ts`

| 类别 | 关键符号 |
| --- | --- |
| 流式入口 | `stream`, `streamSimple`, `complete`, `completeSimple` |
| 模型 | `getModel`, `getProviders`, `getModels`, `MODELS`, `models.generated.ts` |
| Provider 注册 | `registerApiProvider`, `registerOAuthProvider`, `getApiProvider` |
| 工具/类型 | `Tool`, `Message`, `Context`, `AssistantMessageEventStream`, `EventStream`, `validateToolArguments` |
| 工具结果 | `ToolResultMessage`, `ImageContent`, `TextContent` |
| 鉴权 | `getEnvApiKey` (`env-api-keys.ts`), `OAuthProvider`, `OAuthCredentials`, `OAuthProviderInterface` |
| 诊断 | `isContextOverflow`, `AssistantMessageDiagnostic`, `clampThinkingLevel`, `cleanupSessionResources` |
| 校验 | `validateToolArguments`, `parseToolCallArguments`, `sanitizeToolName` |
| 重置 | `resetApiProviders` (测试用) |
| 类型 | `Type`, `Static`, `TSchema` (从 typebox 再导出) |

## 2. 核心抽象

### 2.1 三个核心类型

```ts
// packages/ai/src/types.ts
type Api = KnownApi | string;
type Provider = KnownProvider | string;
type Model<Api> = { id; provider; api; cost; reasoning; thinkingLevelMap?; ... };
type StreamOptions = { apiKey?; signal?; transport?; cacheRetention?; headers?; timeoutMs?; maxRetries?; ... };
```

- **`Api`** = 协议层身份（`openai-completions` / `anthropic-messages` / `bedrock-converse-stream` / `google-generative-ai` …）
- **`Provider`** = 上游服务身份（`anthropic` / `openai` / `github-copilot` / `vercel-ai-gateway` / `minimax` / `kimi-coding` …）
- **`Model<Api>`** = 端到端静态元数据 + cost 表 + reasoning 配置

### 2.2 流式协议

```ts
// packages/ai/src/utils/event-stream.ts
class EventStream<T, R> extends EventEmitter implements AsyncIterable<T> {
  push(event: T): void;
  end(result: R): void;
  result(): Promise<R>;
}
```

每个 Provider 都要返回 `AssistantMessageEventStream` 形式的 `AsyncIterable`，把协议事件流转换成统一的 `AssistantMessageEvent`（`start` / `text_start` / `text_delta` / `text_end` / `thinking_*` / `tool_call_start` / `tool_call_delta` / `tool_call_end` / `done` / `error`）。`streamSimple` 是 `stream` 的子集，只抛更少事件（无 thinking 等）。

### 2.3 流式调用约定

> **Provider 实现必须不抛、不 reject**。所有错误必须编码到 `AssistantMessage` 的 `stopReason: "error" | "aborted"` 与 `errorMessage` 字段中。

调用方拿到 stream 后 `.result()` 拿最终 AssistantMessage，事件消费是副作用。

## 3. Provider 实现目录

`packages/ai/src/providers/`

| Provider 文件 | 对应 API | 关键能力 |
| --- | --- | --- |
| `anthropic.ts` | `anthropic-messages` | thinking、prompt cache (`1h` / `short`)、extra usage warning |
| `openai-completions.ts` | `openai-completions` | 通用 OpenAI chat completions |
| `openai-responses.ts` | `openai-responses` | 新 Responses API |
| `openai-codex-responses.ts` | `openai-codex-responses` | ChatGPT subscription；WebSocket transport 支持；缓存探测 |
| `azure-openai-responses.ts` | `azure-openai-responses` | Azure 端点适配 |
| `openai-responses-shared.ts` | (共享) | Responses 公共逻辑 |
| `openai-prompt-cache.ts` | (共享) | OpenAI 风格 prompt cache key 拼接 |
| `amazon-bedrock.ts` | `bedrock-converse-stream` | AWS SigV4 签名；`build` 中间件注入自定义 header |
| `google.ts` | `google-generative-ai` | Gemini |
| `google-vertex.ts` | `google-vertex` | Vertex AI 端点 |
| `google-shared.ts` | (共享) | Google 通用 thinking level 映射 |
| `mistral.ts` | `mistral-conversations` | Mistral |
| `cloudflare.ts` | (双 API) | Cloudflare Workers AI + AI Gateway |
| `register-builtins.ts` | — | 启动时把所有内置 provider 一次性 `registerApiProvider` |
| `faux.ts` | — | 测试用伪 provider（`test/suite/harness.ts` 用） |
| `transform-messages.ts` | — | 把 `Message[]` → 各 provider 的 wire payload |
| `github-copilot-headers.ts` | — | Copilot IDE 风格特殊 header |
| `simple-options.ts` | — | `SimpleStreamOptions` 标准化 |
| `images/register-builtins.ts` | — | 注册 image generation providers |
| `images/*.ts` | — | 图生成 provider（默认支持 `openrouter-images`） |

### 3.1 内置 Provider 清单（`KnownProvider`）

```
amazon-bedrock, ant-ling, anthropic, google, google-vertex,
openai, azure-openai-responses, openai-codex,
nvidia, deepseek, github-copilot, xai, groq, cerebras,
openrouter, vercel-ai-gateway, zai, zai-coding-cn, mistral,
minimax, minimax-cn, moonshotai, moonshotai-cn, huggingface,
fireworks, together, opencode, opencode-go, kimi-coding,
cloudflare-workers-ai, cloudflare-ai-gateway,
xiaomi, xiaomi-token-plan-cn, xiaomi-token-plan-ams, xiaomi-token-plan-sgp
```

## 4. 鉴权与 OAuth

`packages/ai/src/utils/oauth/`

- `OAuthProvider`, `OAuthProviderInterface` — provider 实现接口
- `OAuthCredentials` — 凭证数据结构
- `OAuthLoginCallbacks`, `OAuthPrompt` — 登录回调与提示
- `OAuthDeviceCodeInfo` — 设备码流程
- `OAuthProviderInfo`, `OAuthProviderId` — provider 元数据
- 内置 OAuth 流支持：Claude Pro/Max、ChatGPT Plus/Pro（Codex）、GitHub Copilot

`env-api-keys.ts` 负责把 `ANTHROPIC_API_KEY` / `OPENAI_API_KEY` / `*_API_KEY` 等环境变量映射到 `getEnvApiKey(provider)`。

## 5. 工具（Tool）抽象

- `Tool` = 工具元数据（name / description / parameters schema (TypeBox) / execute）
- `validateToolArguments(tool, args)` 在 Agent 循环里**每次都跑**
- `parseToolCallArguments` 把模型返回的 JSON 字符串安全 parse（默认包 `try/catch` + 错误回写 tool result）
- `sanitizeToolName` 统一清洗 tool name

> 工具 schema 用 **TypeBox** (`Type.Object({...})`)，不是 Zod。再导出 `Type` / `Static` / `TSchema` 是为了让上层统一使用。

## 6. 会话资源管理

`session-resources.ts`

- 暴露 `cleanupSessionResources(...)` 让上层在结束会话时清理临时文件、关闭打开的句柄
- 配合 `core/bash-executor.ts` 的 `fullOutputPath` 使用（bash 输出超限写入 tmp 文件，会话结束清理）

## 7. 实用工具（`utils/`）

| 文件 | 作用 |
| --- | --- |
| `event-stream.ts` | `EventStream` 异步可迭代 |
| `diagnostics.ts` | `AssistantMessageDiagnostic`、context overflow 检测 |
| `json-parse.ts` | 安全 JSON 解析（容忍截断/控制字符） |
| `validation.ts` | `validateToolArguments`、schema 校验 |
| `typebox-helpers.ts` | `Type.Object` 包装、`Static` 类型派生 |
| `overflow.ts` | context overflow 错误归一化 |
| `sanitize-unicode.ts` | LLM 不可见 unicode 字符清理 |
| `headers.ts` | provider header 合并规则 |
| `hash.ts` | 哈希工具（用于 cache key、request id） |
| `abort-signals.ts` | AbortSignal 组合 / 超时包装 |
| `node-http-proxy.ts` | Node `http`/`https` proxy 注入 |

## 8. 图像生成（`images-api-registry.ts` / `images.ts`）

- `ImagesModel<ImagesApi>` — 图生成模型元数据
- `ImagesProvider` — 图生成 provider
- `registerApiProvider` 给 image API 用
- 当前默认实现：OpenRouter Images（`openrouter-images`）
- `images-api-registry.ts` 是 image 侧的 provider 注册表

## 9. CLI 工具

`cli.ts` 暴露 `pi-ai` 的命令行工具（仅模型元数据查询），**不**是 `pi` CLI。

## 10. 模型注册表生成

- 源数据：`packages/ai/scripts/generate-models.ts` 维护
- 产物：`packages/ai/src/models.generated.ts`（`MODELS` 常量）
- **禁止直接改** generated 文件，只能改 generator 后重跑
- `MODELS` 是 `Record<KnownProvider, Record<ModelId, Model<Api>>>`

## 11. 被谁使用

```
pi-agent-core      → streamSimple / completeSimple / getModel / getProviders
pi-coding-agent    → 同上 + OAuth + env api keys + images
pi-coding-agent/test → faux provider
```

## 12. 不变量（实现 provider 时必须遵守）

1. **绝不抛 / 绝不 reject** provider 自身的 `stream` / `streamSimple` 函数
2. 所有错误必须落到 AssistantMessage.stopReason = "error" 或 "aborted"，并填 `errorMessage`
3. 事件推送顺序必须满足：`start` 是第一个，`done`/`error` 是最后一个
4. `tool_call_*` 事件必须完整配对（`start` → `delta*` → `end`），agent 才能 parse
5. `cacheRead` / `cacheWrite` 必须如实回写，否则 cost 计算会失真
6. `onPayload` / `onResponse` 是**观察/可选改写**回调，不可被业务逻辑依赖
