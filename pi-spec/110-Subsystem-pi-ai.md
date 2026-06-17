# 110 · 子系统 `@earendil-works/pi-ai`

> 角色：把 30+ 个 LLM Provider 抽象成统一 API。仓库最低层；只依赖外部 npm 包，不依赖任何其它 pi 包。

## 1. 入口

源码：`packages/ai/src/index.ts`

类型 box 再导出：`Type`, `Static`, `TSchema` — 让上层用同一套 schema DSL。

## 2. 导出面

| 来源 | 主要 re-export |
| --- | --- |
| `./api-registry.ts` | `registerApiProvider`, `getApiProvider`, `resetApiProviders` |
| `./env-api-keys.ts` | `getEnvApiKey` + 各 provider 的 env 变量映射 |
| `./image-models.ts` | `getImageModel`, 图像模型注册表 |
| `./images.ts` | `generateImage` (入口) |
| `./images-api-registry.ts` | `registerImageApiProvider` |
| `./models.ts` | `getModel`, `getProviders`, `getModels`, `MODELS`, `calculateCost`, `clampThinkingLevel`, `getSupportedThinkingLevels`, `modelsAreEqual` |
| `./providers/register-builtins.ts` | 内置 provider 一次性注册（`import "./providers/register-builtins.ts"` 副作用） |
| `./providers/faux.ts` | `FauxProvider` — 测试用假 provider |
| `./providers/images/register-builtins.ts` | 内置图 provider |
| `./session-resources.ts` | `cleanupSessionResources`, 资源追踪 |
| `./stream.ts` | `stream`, `streamSimple`, `complete`, `completeSimple` |
| `./types.ts` | 全部 wire / data 类型（`Model`, `Context`, `Message`, `Tool`, ...） |
| `./utils/diagnostics.ts` | `AssistantMessageDiagnostic` |
| `./utils/event-stream.ts` | `EventStream`, `AssistantMessageEventStream`, `createAssistantMessageEventStream` |
| `./utils/json-parse.ts` | 容错 JSON 解析 |
| `./utils/overflow.ts` | `isContextOverflow` |
| `./utils/typebox-helpers.ts` | TypeBox 包装 |
| `./utils/validation.ts` | `validateToolArguments`, `parseToolCallArguments` |
| `./utils/oauth/*` | `OAuthProvider`, `OAuthCredentials`, `OAuthProviderInterface` 等 |

## 3. 功能分组

### 3.1 流式调用 (LLM Streaming)

四个入口：

```ts
// packages/ai/src/stream.ts
export function stream<TApi>(model, context, options?): AssistantMessageEventStream;
export function streamSimple<TApi>(model, context, options?): AssistantMessageEventStream;
export function complete<TApi>(model, context, options?): Promise<AssistantMessage>;
export function completeSimple<TApi>(model, context, options?): Promise<AssistantMessage>;
```

- `stream` / `complete` 抛**完整事件**（含 thinking、tool_call）
- `streamSimple` / `completeSimple` 只抛**最小事件**（text_start / text_delta / text_end / done / error）

### 3.2 模型元数据

```ts
// packages/ai/src/models.ts
export const MODELS: Record<KnownProvider, Record<ModelId, Model<Api>>>;
export function getModel<TProvider, TModelId>(provider, modelId): Model<...>;
export function getProviders(): KnownProvider[];
export function getModels<TProvider>(provider): Model<...>[];
```

- `MODELS` 是**生成产物**（`models.generated.ts`），由 `packages/ai/scripts/generate-models.ts` 维护
- 包含 30+ provider × 数百 model 的完整元数据：cost、contextWindow、maxTokens、thinkingLevelMap 等

### 3.3 模型思考等级

```ts
// packages/ai/src/models.ts
export function getSupportedThinkingLevels<TApi>(model): ModelThinkingLevel[];
export function clampThinkingLevel<TApi>(model, level): ModelThinkingLevel;
export type ThinkingLevel = "minimal" | "low" | "medium" | "high" | "xhigh";
export type ModelThinkingLevel = "off" | ThinkingLevel;
```

- `clampThinkingLevel`：请求 `xhigh` 但 model 不支持时自动降级到 `high` / `medium` ...

### 3.4 Provider 注册

```ts
// packages/ai/src/api-registry.ts
type ApiProvider<TApi> = {
  stream(model, context, options?): AssistantMessageEventStream;
  streamSimple(model, context, options?): AssistantMessageEventStream;
};
export function registerApiProvider<TApi>(api: TApi, provider: ApiProvider<TApi>): void;
export function getApiProvider<TApi>(api: TApi): ApiProvider<TApi> | undefined;
export function resetApiProviders(): void;   // 测试用
```

内置 provider 文件（`packages/ai/src/providers/`）：

| 文件 | API 字符串 | 关键能力 |
| --- | --- | --- |
| `anthropic.ts` | `anthropic-messages` | thinking、cache 1h/short、extra usage 警告 |
| `openai-completions.ts` | `openai-completions` | 通用 chat completions + 兼容层 |
| `openai-responses.ts` | `openai-responses` | 新 Responses API |
| `openai-codex-responses.ts` | `openai-codex-responses` | ChatGPT subscription；WebSocket；缓存探测 |
| `azure-openai-responses.ts` | `azure-openai-responses` | Azure 端点 |
| `amazon-bedrock.ts` | `bedrock-converse-stream` | AWS SigV4；build 中间件注入 header |
| `google.ts` | `google-generative-ai` | Gemini |
| `google-vertex.ts` | `google-vertex` | Vertex AI |
| `mistral.ts` | `mistral-conversations` | Mistral |
| `cloudflare.ts` | (双 API) | Cloudflare Workers AI + AI Gateway |
| `register-builtins.ts` | — | 启动副作用：调 `registerApiProvider` 全部 |
| `faux.ts` | `faux` | 测试用 |

### 3.5 鉴权 (Auth)

#### 3.5.1 API Key (env)

```ts
// packages/ai/src/env-api-keys.ts
export function getEnvApiKey(provider: Provider): string | undefined;
```

自动把 `ANTHROPIC_API_KEY` / `OPENAI_API_KEY` / `*_API_KEY` 等映射到 provider。

#### 3.5.2 OAuth

`packages/ai/src/utils/oauth/`

- `OAuthProvider`, `OAuthProviderInterface` — 抽象
- `OAuthCredentials`, `OAuthLoginCallbacks`, `OAuthPrompt` — 数据结构
- `OAuthDeviceCodeInfo` — device code 流程
- 内置 OAuth 流：Claude Pro/Max、ChatGPT Plus/Pro、GitHub Copilot

### 3.6 工具 (Tool) 抽象

```ts
// packages/ai/src/types.ts
export interface Tool<TParameters extends TSchema = TSchema> {
  name: string;
  description: string;
  parameters: TParameters;       // TypeBox schema
}

// packages/ai/src/utils/validation.ts
export function validateToolArguments(tool: Tool, args: unknown): { ok: true; value: Static<...> } | { ok: false; errorMessage: string };
export function parseToolCallArguments(input: unknown, schema: TSchema): { ok: true; value: any } | { ok: false; errorMessage: string };
export function sanitizeToolName(name: string): string;
```

### 3.7 Cost & Token

```ts
// packages/ai/src/models.ts
export function calculateCost<TApi>(model: Model<TApi>, usage: Usage): Usage["cost"];

// packages/ai/src/types.ts
export interface Usage {
  input: number;
  output: number;
  cacheRead: number;
  cacheWrite: number;
  cacheWrite1h?: number;   // Anthropic split
  totalTokens: number;
  cost: { input; output; cacheRead; cacheWrite; total };
}
```

- Anthropic 的 1h cache write 算 2x base input
- cost 算成美元（`/ 1_000_000` × tokens）

### 3.8 错误归一化

```ts
// packages/ai/src/utils/overflow.ts
export function isContextOverflow(err: unknown): boolean;
```

识别 Anthropic / OpenAI 典型 overflow 错误。

```ts
// packages/ai/src/utils/diagnostics.ts
export interface AssistantMessageDiagnostic { code: string; message: string; ... }
```

provider 在 stopReason="error" 时填 `errorMessage` + `diagnostics`。

### 3.9 资源清理

```ts
// packages/ai/src/session-resources.ts
export function cleanupSessionResources(sessionId: string): void;
export function trackSessionResource(sessionId: string, resource: SessionResource): void;
```

配合 bash executor 的 `fullOutputPath` 临时文件做生命周期管理。

### 3.10 消息转换

```ts
// packages/ai/src/providers/transform-messages.ts
export function transformMessages(...): ...;
```

把 `Message[]` → 各 provider 的 wire payload。

### 3.11 JSON 解析

```ts
// packages/ai/src/utils/json-parse.ts
export function safeJsonParse<T>(text: string, fallback?: T): T | undefined;
```

容忍截断/控制字符，常用于 `toolCall.arguments` 解析。

### 3.12 图像生成

```ts
// packages/ai/src/images.ts
export function generateImage<TApi>(model, context, options?): Promise<AssistantImages>;

// packages/ai/src/image-models.ts
export function getImageModel(provider, modelId): ImagesModel<...>;
```

内置：`openrouter-images` (`packages/ai/src/providers/images/`)。

### 3.13 HTTP 辅助

```ts
// packages/ai/src/utils/headers.ts
export function mergeHeaders(defaults, overrides): Record<string, string>;

// packages/ai/src/utils/abort-signals.ts
export function withTimeout(signal, ms): AbortSignal;
export function combineSignals(signals): AbortSignal;
export function throwIfAborted(signal): void;

// packages/ai/src/utils/node-http-proxy.ts
export function getNodeHttpProxyAgent(): Agent | undefined;

// packages/ai/src/utils/hash.ts
export function sha256(text): string;
```

### 3.14 协议兼容层

`packages/ai/src/types.ts` 内有大量 `*Compat` 接口用于同协议不同变体：

- `OpenAICompletionsCompat` — 控制 store / developer role / reasoning_effort / max_tokens 字段等
- `OpenAIResponsesCompat`
- `AnthropicMessagesCompat`
- `OpenRouterRouting`, `VercelGatewayRouting`

## 4. 典型用法

### 4.1 最小化调用

```ts
import { streamSimple, getModel } from "@earendil-works/pi-ai";
import "@earendil-works/pi-ai/providers/register-builtins";

const model = getModel("anthropic", "claude-sonnet-4-5");
const stream = streamSimple(model, {
  systemPrompt: "You are concise.",
  messages: [{ role: "user", content: "Hi", timestamp: Date.now() }],
});
for await (const ev of stream) {
  // ev.type ∈ { start, text_start, text_delta, text_end, done, error }
}
const final = await stream.result();
```

### 4.2 注册自定义 provider

```ts
import { registerApiProvider, getModel } from "@earendil-works/pi-ai";

registerApiProvider("openai-completions", {
  stream: myStream,         // 必须返回 AssistantMessageEventStream
  streamSimple: myStreamSimple,
});
```

## 5. 内部能力（未导出但能 `import` 路径访问）

| 路径 | 用途 |
| --- | --- |
| `pi-ai/utils/oauth/types` | OAuth 内部类型（也部分从 `index` 导出） |
| `pi-ai/providers/register-builtins` | **副作用文件**：`import` 一次后所有内置 provider 注册 |
| `pi-ai/providers/faux` | 测试用假 provider |
| `pi-ai/providers/images/register-builtins` | 同上，图像 |
| `pi-ai/scripts/generate-models.ts` | `MODELS` 的生成器 |

## 6. 不变量 / 契约

1. **`stream` / `streamSimple` 永不抛** — 错误必须编码到 AssistantMessage.stopReason = "error" 或 "aborted"
2. **事件顺序**：`start` 是第一个；`done` 或 `error` 是最后一个
3. **tool_call 三元组**：`toolcall_start` → `toolcall_delta*` → `toolcall_end`，`toolcall_end.toolCall` 是完整 ToolCall
4. **cache write 拆分**：Anthropic 报 `cacheWrite1h`，其他 provider 不报
5. **MODELS 是生成产物** — 不能手改，只能改 `generate-models.ts` 后再生成
6. **`resetApiProviders` 只在测试用** — 重置整个全局注册表
7. **`withEnvApiKey` 自动从 env 补 apiKey** — 调用方传了 `apiKey` 时不覆盖
