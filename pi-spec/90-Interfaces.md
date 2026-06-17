# 90 · 接口详细定义 (Detailed Interface Definitions)

> 跨模块的关键接口完整定义。来源：`packages/*/src/`。所有 TypeScript 代码片段都是仓库源码的真实签名（节选与注释）。

## 1. pi-ai · LLM 流式协议

### 1.1 `AssistantMessageEventStream`

```ts
// packages/ai/src/utils/event-stream.ts
export class EventStream<T, R = T> implements AsyncIterable<T> {
  push(event: T): void;
  end(result?: R): void;
  [Symbol.asyncIterator](): AsyncIterator<T>;
  result(): Promise<R>;
}

export class AssistantMessageEventStream extends EventStream<AssistantMessageEvent, AssistantMessage> {
  constructor();
}

export function createAssistantMessageEventStream(): AssistantMessageEventStream;
```

**契约**：

- `push` 是非阻塞；超过消费速度的事件进 `queue`
- `end(result?)` 关闭流；如不传 `result` 则 `result()` 永挂
- 实现为 `AsyncIterable`，可用 `for await` 消费
- 末尾事件必须 `isComplete(event) === true`（即 `done` 或 `error`）

### 1.2 `AssistantMessageEvent`

```ts
// packages/ai/src/types.ts
export type AssistantMessageEvent =
  | { type: "start"; partial: AssistantMessage }
  | { type: "text_start"; contentIndex: number; partial: AssistantMessage }
  | { type: "text_delta"; contentIndex: number; delta: string; partial: AssistantMessage }
  | { type: "text_end"; contentIndex: number; content: string; partial: AssistantMessage }
  | { type: "thinking_start"; contentIndex: number; partial: AssistantMessage }
  | { type: "thinking_delta"; contentIndex: number; delta: string; partial: AssistantMessage }
  | { type: "thinking_end"; contentIndex: number; content: string; partial: AssistantMessage }
  | { type: "toolcall_start"; contentIndex: number; partial: AssistantMessage }
  | { type: "toolcall_delta"; contentIndex: number; delta: string; partial: AssistantMessage }
  | { type: "toolcall_end"; contentIndex: number; toolCall: ToolCall; partial: AssistantMessage }
  | { type: "done"; reason: Extract<StopReason, "stop" | "length" | "toolUse">; message: AssistantMessage }
  | { type: "error"; reason: Extract<StopReason, "aborted" | "error">; error: AssistantMessage };
```

**规则**：
- 第一个事件必须是 `start`
- 最后一个事件必须是 `done` 或 `error`
- `text_*` / `thinking_*` / `toolcall_*` 各自成三元组
- `partial` 是"到目前为止的"完整消息快照

### 1.3 `Model<TApi>`

```ts
// packages/ai/src/types.ts
export interface Model<TApi extends Api> {
  id: string;                          // 模型 ID（provider 内唯一）
  name: string;                        // 显示名
  api: TApi;                           // 协议身份
  provider: Provider;                  // 上游身份
  baseUrl: string;
  reasoning: boolean;                  // 是否支持 reasoning/thinking
  thinkingLevelMap?: ThinkingLevelMap; // pi level → provider value
  input: ("text" | "image")[];
  cost: {
    input: number;      // $/M tokens
    output: number;
    cacheRead: number;
    cacheWrite: number;
  };
  contextWindow: number;   // tokens
  maxTokens: number;       // max output tokens
  headers?: Record<string, string>;
  compat?: TApi extends "openai-completions" ? OpenAICompletionsCompat
         : TApi extends "openai-responses"   ? OpenAIResponsesCompat
         : TApi extends "anthropic-messages" ? AnthropicMessagesCompat
         : never;
}
```

### 1.4 `StreamOptions`

```ts
// packages/ai/src/types.ts
export interface StreamOptions {
  temperature?: number;
  maxTokens?: number;
  signal?: AbortSignal;
  apiKey?: string;
  transport?: "sse" | "websocket" | "websocket-cached" | "auto";
  cacheRetention?: "none" | "short" | "long";
  sessionId?: string;             // session-based caching / routing
  onPayload?: (payload: unknown, model: Model<Api>) => unknown | undefined | Promise<...>;
  onResponse?: (response: ProviderResponse, model: Model<Api>) => void | Promise<void>;
  headers?: Record<string, string>;
  timeoutMs?: number;
  websocketConnectTimeoutMs?: number;
  maxRetries?: number;
  maxRetryDelayMs?: number;       // default 60000
  metadata?: Record<string, unknown>;
}

export interface SimpleStreamOptions extends StreamOptions {
  reasoning?: ThinkingLevel;
  thinkingBudgets?: ThinkingBudgets;
}
```

### 1.5 `AssistantMessage`

```ts
export interface AssistantMessage {
  role: "assistant";
  content: (TextContent | ThinkingContent | ToolCall)[];
  api: Api;
  provider: Provider;
  model: string;
  responseModel?: string;   // OpenRouter auto 解析后真实模型
  responseId?: string;
  diagnostics?: AssistantMessageDiagnostic[];
  usage: Usage;
  stopReason: StopReason;   // "stop" | "length" | "toolUse" | "error" | "aborted"
  errorMessage?: string;
  timestamp: number;        // ms
}

export interface ToolResultMessage<TDetails = any> {
  role: "toolResult";
  toolCallId: string;
  toolName: string;
  content: (TextContent | ImageContent)[];
  details?: TDetails;
  isError: boolean;
  timestamp: number;
}

export type Message = UserMessage | AssistantMessage | ToolResultMessage;
```

### 1.6 Provider 注册

```ts
// packages/ai/src/api-registry.ts (类型推断)
type ApiProvider<TApi extends Api> = {
  stream(model: Model<TApi>, context: Context, options?: StreamOptions): AssistantMessageEventStream;
  streamSimple(model: Model<TApi>, context: Context, options?: SimpleStreamOptions): AssistantMessageEventStream;
};

function registerApiProvider<TApi extends Api>(api: TApi, provider: ApiProvider<TApi>): void;
function getApiProvider<TApi extends Api>(api: TApi): ApiProvider<TApi> | undefined;
```

**不变量**：注册时 type 必须携带 `TApi`，调用时 `model.api` 必须匹配。`streamSimple` 是 `stream` 的"窄事件"版本，**不**抛 thinking 事件等。

## 2. pi-agent-core · Agent 循环

### 2.1 `agentLoop`

```ts
// packages/agent/src/agent-loop.ts
export function agentLoop(
  prompts: AgentMessage[],
  context: AgentContext,
  config: AgentLoopConfig,
  signal?: AbortSignal,
  streamFn?: StreamFn,
): EventStream<AgentEvent, AgentMessage[]>;

export function agentLoopContinue(
  context: AgentContext,
  config: AgentLoopConfig,
  signal?: AbortSignal,
  streamFn?: StreamFn,
): EventStream<AgentEvent, AgentMessage[]>;
```

### 2.2 `AgentLoopConfig`

```ts
// packages/agent/src/types.ts
export interface AgentLoopConfig extends SimpleStreamOptions {
  model: Model<any>;
  convertToLlm: (messages: AgentMessage[]) => Message[];          // 不抛
  transformContext?: (ctx: AgentContext) => AgentContext | Promise<AgentContext>;
  hasToolCall?: (message: AssistantMessage) => boolean;
  executeToolCall: (
    toolCall: AgentToolCall,
    args: unknown,
    onUpdate: AgentToolUpdateCallback,
    signal: AbortSignal,
  ) => Promise<AgentToolResult>;
  beforeToolCall?: (ctx: BeforeToolCallContext) => Promise<BeforeToolCallResult | undefined> | BeforeToolCallResult | undefined;
  afterToolCall?: (ctx: AfterToolCallContext) => Promise<AfterToolCallResult | undefined> | AfterToolCallResult | undefined;
  shouldStopAfterTurn?: (ctx: ShouldStopAfterTurnContext) => boolean | AgentLoopTurnUpdate | Promise<boolean | AgentLoopTurnUpdate>;
  prepareNextTurn?: (ctx: PrepareNextTurnContext) => AgentLoopTurnUpdate | Promise<AgentLoopTurnUpdate>;
  streamFn?: StreamFn;
}
```

### 2.3 `AgentEvent`

```ts
export type AgentEvent =
  | { type: "agent_start" }
  | { type: "agent_end"; messages: AgentMessage[] }
  | { type: "turn_start" }
  | { type: "turn_end"; message: AssistantMessage; toolResults: ToolResultMessage[] }
  | { type: "message_start"; message: AgentMessage }
  | { type: "message_update"; message: AgentMessage; assistantMessageEvent: AssistantMessageEvent }
  | { type: "message_end"; message: AgentMessage }
  | { type: "tool_execution_start"; toolCallId: string; toolName: string; args: unknown }
  | { type: "tool_execution_update"; toolCallId: string; toolName: string; args: unknown; partialResult: AgentToolResult }
  | { type: "tool_execution_end"; toolCallId: string; toolName: string; result: AgentToolResult; isError: boolean };
```

### 2.4 `BeforeToolCallResult` / `AfterToolCallResult`

```ts
export interface BeforeToolCallResult {
  block?: boolean;   // true → 注入 error tool result，循环不中断
  reason?: string;
}

export interface AfterToolCallResult {
  content?: (TextContent | ImageContent)[];  // 整体替换，不深合并
  details?: unknown;                          // 整体替换
  isError?: boolean;
  terminate?: boolean;                        // batch 内全员同意才提前停
}
```

### 2.5 Compaction 公开 API

```ts
export interface CompactionSettings {
  enabled?: boolean;          // default true
  reserveTokens?: number;     // default 16384
  keepRecentTokens?: number;  // default 20000
}

export const DEFAULT_COMPACTION_SETTINGS: CompactionSettings;

export function shouldCompact(ctx: AgentContext, settings: CompactionSettings): boolean;
export function findCutPoint(messages: AgentMessage[], settings: CompactionSettings): number;
export function prepareCompaction(...): CompactionPreparation;
export function compact(...): Promise<CompactionResult>;
export function generateSummary(...): Promise<{ summary: string; usage?: Usage }>;
export function calculateContextTokens(messages: AgentMessage[]): number;
export function estimateContextTokens(...): number;
export function serializeConversation(messages: AgentMessage[]): string;
```

## 3. pi-coding-agent · AgentSession

### 3.1 `AgentSession` 公共面

```ts
// packages/coding-agent/src/core/agent-session.ts
export class AgentSession {
  readonly agent: Agent;
  readonly sessionManager: SessionManager;
  readonly settingsManager: SettingsManager;
  readonly modelRegistry: ModelRegistry;

  constructor(config: AgentSessionConfig);

  // 订阅事件（所有 mode 都共享）
  subscribe(listener: AgentSessionEventListener): () => void;
  on(event: AgentSessionEvent["type"], handler: (e: AgentSessionEvent) => void): () => void;

  // 核心动作
  prompt(text: string | (TextContent | ImageContent)[], options?: PromptOptions): Promise<void>;
  promptWithImages?(...): Promise<void>;
  abort(): void;
  abortBash(): void;

  // 模型 / 思考
  setModel(model: Model<any>): Promise<boolean>;
  cycleModel(direction: 1 | -1): Promise<boolean>;
  cycleScopedModel(direction: 1 | -1): Promise<boolean>;
  cycleThinkingLevel(): void;

  // Session
  newSession(options?: NewSessionOptions): Promise<void>;
  switchSession(path: string): Promise<void>;
  fork(entryId: string): Promise<void>;
  getSessionName(): string | undefined;
  setSessionName(name: string | undefined): void;

  // 工具
  getActiveTools(): string[];
  setActiveTools(names: string[]): void;

  // Bash
  executeBash(command: string, options?: { cwd?: string; timeout?: number }): Promise<BashResult>;

  // Compaction
  compact(options?: { signal?: AbortSignal }): Promise<void>;

  // 队列
  steer(messages: string[]): void;
  followUp(messages: string[]): void;
  getSteeringQueue(): readonly string[];
  getFollowUpQueue(): readonly string[];

  // 扩展
  bindExtensions(opts: ExtensionBindings): void;
}
```

### 3.2 `AgentSessionConfig`

```ts
export interface AgentSessionConfig {
  agent: Agent;
  sessionManager: SessionManager;
  settingsManager: SettingsManager;
  cwd: string;
  scopedModels?: Array<{ model: Model<any>; thinkingLevel?: ThinkingLevel }>;
  resourceLoader: ResourceLoader;
  customTools?: ToolDefinition[];
  modelRegistry: ModelRegistry;
  initialActiveToolNames?: string[];   // default [read, bash, edit, write]
  allowedToolNames?: string[];
  excludedToolNames?: string[];
  baseToolsOverride?: Record<string, AgentTool>;
  extensionRunnerRef?: { current?: ExtensionRunner };
  sessionStartEvent?: SessionStartEvent;
}
```

### 3.3 `AgentSessionEvent`

```ts
export type AgentSessionEvent =
  | Exclude<AgentEvent, { type: "agent_end" }>
  | { type: "agent_end"; messages: AgentMessage[]; willRetry: boolean }
  | { type: "queue_update"; steering: readonly string[]; followUp: readonly string[] }
  | { type: "compaction_start"; reason: "manual" | "threshold" | "overflow" }
  | { type: "compaction_end"; reason: ...; result: CompactionResult | undefined; aborted: boolean; willRetry: boolean; errorMessage?: string }
  | { type: "auto_retry_start"; attempt: number; maxAttempts: number; delayMs: number; errorMessage: string }
  | { type: "auto_retry_end"; success: boolean; attempt: number; finalError?: string }
  | { type: "session_info_changed"; name: string | undefined }
  | { type: "thinking_level_changed"; level: ThinkingLevel };
```

## 4. pi-coding-agent · 扩展 API

### 4.1 `ExtensionAPI`

```ts
// packages/coding-agent/src/core/extensions/types.ts
export interface ExtensionAPI {
  // 事件订阅
  on<E extends keyof ExtensionEventMap>(event: E, handler: ExtensionHandler<ExtensionEventMap[E]>): void;

  // 工具 / 命令 / 快捷键 / flag
  registerTool<TParams, TDetails, TState>(tool: ToolDefinition<TParams, TDetails, TState>): void;
  registerCommand(name: string, options: Omit<RegisteredCommand, "name" | "sourceInfo">): void;
  registerShortcut(shortcut: KeyId, options: { description?: string; handler: (ctx: ExtensionContext) => Promise<void> | void }): void;
  registerFlag(name: string, options: { description?: string; type: "boolean" | "string"; default?: boolean | string }): void;
  getFlag(name: string): boolean | string | undefined;

  // 消息 / 会话
  sendMessage<T>(message: Pick<CustomMessage<T>, "customType" | "content" | "display" | "details">, options?: { triggerTurn?: boolean; deliverAs?: "steer" | "followUp" | "nextTurn" }): void;
  sendUserMessage(content: string | (TextContent | ImageContent)[], options?: { deliverAs?: "steer" | "followUp" }): void;
  appendEntry<T>(customType: string, data?: T): void;
  setSessionName(name: string): void;
  getSessionName(): string | undefined;
  setLabel(entryId: string, label: string | undefined): void;

  // 工具查询
  getActiveTools(): string[];
  getAllTools(): ToolInfo[];
  setActiveTools(toolNames: string[]): void;
  getCommands(): SlashCommandInfo[];

  // 模型
  setModel(model: Model<any>): Promise<boolean>;
  getThinkingLevel(): ThinkingLevel;
  setThinkingLevel(level: ThinkingLevel): void;

  // 自定义 Provider
  registerProvider(name: string, config: ProviderConfig): void;
  unregisterProvider(name: string): void;

  // 执行
  exec(command: string, args: string[], options?: ExecOptions): Promise<ExecResult>;

  // 消息渲染器
  registerMessageRenderer<T>(customType: string, renderer: MessageRenderer<T>): void;

  // 共享事件总线
  events: EventBus;
}

export type ExtensionFactory = (pi: ExtensionAPI) => void | Promise<void>;
```

### 4.2 `ExtensionContext` / `ExtensionUIContext`

```ts
export interface ExtensionContext {
  cwd: string;
  ui: ExtensionUIContext;
  hasPermission: (permission: string) => boolean;
  abortController: AbortController;
  shutdown: ShutdownHandler;
}

export interface ExtensionCommandContext extends ExtensionContext {
  input: string;     // 命令的尾部参数
  // ...继承所有 ExtensionContext 字段
}

export interface ExtensionUIContext {
  notify(message: string, level?: "info" | "warn" | "error"): void;
  confirm(title: string, question: string, options?: ExtensionUIDialogOptions): Promise<boolean>;
  input(prompt: string, options?: ExtensionUIDialogOptions & { placeholder?: string; defaultValue?: string }): Promise<string | undefined>;
  select<T>(title: string, options: Array<{ label: string; value: T; description?: string }>, opts?: ExtensionUIDialogOptions): Promise<T | undefined>;
  custom<T>(factory: (tui: TUI, theme: ..., keybindings: ...) => Component, opts?: ExtensionUIDialogOptions): Promise<T | undefined>;
  setWidget(component: Component, options?: ExtensionWidgetOptions): void;
  // ...
}
```

### 4.3 `ToolDefinition`

```ts
export interface ToolDefinition<TParams extends TSchema = TSchema, TDetails = unknown, TState = any> {
  name: string;
  label: string;
  description: string;
  parameters: TParams;        // TypeBox schema
  execute(
    toolCallId: string,
    params: Static<TParams>,
    signal: AbortSignal,
    onUpdate: AgentToolUpdateCallback,
    ctx: ExtensionContext,
  ): Promise<AgentToolResult<TDetails>>;
  renderCall?: MessageRenderer;
  renderResult?: MessageRenderer;
}

export type AgentToolResult<TDetails = unknown> = {
  content: (TextContent | ImageContent)[];
  details?: TDetails;
};

export function defineTool<TParams, TDetails, TState>(def: ToolDefinition<TParams, TDetails, TState>): ToolDefinition<TParams, TDetails, TState>;
```

### 4.4 `ProviderConfig`（扩展注册 provider）

```ts
export interface ProviderConfig {
  name?: string;
  baseUrl?: string;
  apiKey?: string;
  api?: Api;
  models?: ProviderModelConfig[];
  streamSimple?: (model: Model<any>, context: Context, options?: SimpleStreamOptions) => AssistantMessageEventStream;
  oauth?: {
    name: string;
    login(callbacks: OAuthLoginCallbacks): Promise<OAuthCredentials>;
    refreshToken(credentials: OAuthCredentials): Promise<OAuthCredentials>;
    getApiKey(credentials: OAuthCredentials): string;
  };
}

export interface ProviderModelConfig {
  id: string;
  name: string;
  reasoning: boolean;
  input: ("text" | "image")[];
  cost: { input: number; output: number; cacheRead: number; cacheWrite: number };
  contextWindow: number;
  maxTokens: number;
  // ...
}
```

## 5. pi-coding-agent · 工具可替换后端

### 5.1 `BashOperations`

```ts
export interface BashOperations {
  exec(
    command: string,
    cwd: string,
    options: {
      onData: (data: Buffer) => void;
      signal?: AbortSignal;
      timeout?: number;
      env?: NodeJS.ProcessEnv;
    },
  ): Promise<{ exitCode: number | null }>;
}

export function createLocalBashOperations(options?: { shellPath?: string }): BashOperations;
```

### 5.2 `ReadOperations` / `EditOperations` / `GrepOperations` / `FindOperations` / `LsOperations`

```ts
export interface ReadOperations {
  readFile(absolutePath: string): Promise<Buffer>;
  access(absolutePath: string): Promise<void>;
  detectImageMimeType?(absolutePath: string): Promise<string | null | undefined>;
}

export interface EditOperations {
  readFile(absolutePath: string): Promise<string>;
  writeFile(absolutePath: string, content: string): Promise<void>;
  // ...
}

export interface GrepOperations {
  // 默认走 ripgrep，可替换为内置实现
  search?(cwd: string, args: string[], signal: AbortSignal): Promise<{ matches: GrepMatch[]; truncated: boolean }>;
}

export interface FindOperations {
  search?(cwd: string, pattern: string, options: { type?: "file" | "directory"; limit?: number }): Promise<string[]>;
}

export interface LsOperations {
  list?(cwd: string, path: string, options: { hidden?: boolean; ignore?: string[] }): Promise<{ entries: Array<{ name: string; type: "file" | "directory" | "other" }> }>;
}
```

> 注入方式：通过 `AgentSessionConfig.baseToolsOverride` 替换；或在 Extension 中 `pi.registerTool`。

## 6. pi-coding-agent · 鉴权存储

```ts
// packages/coding-agent/src/core/auth-storage.ts
export type ApiKeyCredential = { type: "api_key"; key: string };
export type OAuthCredential = { type: "oauth"; ...; refresh: string; expires: number; ... };
export type AuthCredential = ApiKeyCredential | OAuthCredential;
export type AuthStorageData = Record<string, AuthCredential>;

export type AuthStatus = { status: "ok"; credential: AuthCredential } | { status: "missing" } | { status: "expired"; reason: string };

export interface AuthStorageBackend {
  read(): Promise<AuthStorageData>;
  write(data: AuthStorageData): Promise<void>;
}

export class FileAuthStorageBackend implements AuthStorageBackend { ... }
export class InMemoryAuthStorageBackend implements AuthStorageBackend { ... }

export class AuthStorage {
  constructor(backend: AuthStorageBackend);
  get(provider: string): Promise<AuthStatus>;
  set(provider: string, credential: AuthCredential): Promise<void>;
  remove(provider: string): Promise<void>;
  list(): Promise<string[]>;
}
```

## 7. pi-coding-agent · Session 持久化

### 7.1 `SessionManager`

```ts
// packages/coding-agent/src/core/session-manager.ts
export const CURRENT_SESSION_VERSION = 3;

export class SessionManager {
  constructor(filePath: string);

  // Header
  readHeader(): SessionHeader | undefined;
  writeHeader(header: SessionHeader): void;

  // Entries
  appendEntry(entry: SessionEntry): void;
  readEntries(): SessionEntry[];
  getEntry(id: string): SessionEntry | undefined;

  // Context 构建
  buildSessionContext(options?: { excludeSystemReminder?: boolean }): SessionContext;

  // 列表
  static list(): SessionInfo[];       // 扫整个 sessions 目录
  static getInfo(path: string): SessionInfo;

  // 工具
  static resolveIdPrefix(prefix: string): string | undefined;

  // 锁定（proper-lockfile）
  private _lock: () => Promise<() => void>;
}
```

### 7.2 `SessionEntry` 联合类型

```ts
export type SessionEntry =
  | SessionMessageEntry             // { type: "message", message: AgentMessage }
  | ThinkingLevelChangeEntry        // { type: "thinking_level_change", thinkingLevel: string }
  | ModelChangeEntry                // { type: "model_change", model: Model, provider: Provider }
  | CompactionEntry                 // { type: "compaction", summary: string, ...details }
  | BranchSummaryEntry              // { type: "branch_summary", summary, cancelled? }
  | LabelEntry                      // { type: "label", targetId, label }
  | SessionInfoEntry                // { type: "session_info", name? }
  | CustomMessageEntry              // { type: "custom_message", customType, content, display }
  | CustomEntry;                    // { type: "custom", customType, data? }
```

### 7.3 `SessionInfo` / `SessionContext`

```ts
export interface SessionInfo {
  path: string;
  id: string;
  name?: string;
  timestamp: string;
  cwd: string;
  parentSession?: string;
  messageCount: number;
  fileSize: number;
}

export interface SessionContext {
  header: SessionHeader;
  messages: Message[];            // 已过滤、已转 LLM 形式
  lastCompactionIndex: number;
  lastBranchSummaryIndex: number;
}
```

## 8. pi-coding-agent · ResourceLoader

```ts
export interface ResourceLoader {
  getExtensions(): LoadExtensionsResult;
  getSkills(): { skills: Skill[]; diagnostics: ResourceDiagnostic[] };
  getPrompts(): { prompts: PromptTemplate[]; diagnostics: ResourceDiagnostic[] };
  getThemes(): { themes: Theme[]; diagnostics: ResourceDiagnostic[] };
  getAgentsFiles(): { agentsFiles: Array<{ path: string; content: string }> };
  getSystemPrompt(): string | undefined;
  getAppendSystemPrompt(): string[];
  extendResources(paths: ResourceExtensionPaths): void;
  reload(opts?: ResourceLoaderReloadOptions): Promise<void>;
  dispose(): void;
}

export interface ResourceExtensionPaths {
  skillPaths?: Array<{ path: string; metadata: PathMetadata }>;
  promptPaths?: Array<{ path: string; metadata: PathMetadata }>;
  themePaths?: Array<{ path: string; metadata: PathMetadata }>;
}
```

## 9. pi-coding-agent · SettingsManager

```ts
export interface Settings {
  enabled?: boolean;
  scopedModels?: Array<{ provider: string; model: string; thinkingLevel?: ThinkingLevel }>;
  compaction?: CompactionSettings;
  branchSummary?: BranchSummarySettings;
  retry?: RetrySettings;
  providerRetry?: ProviderRetrySettings;
  terminal?: TerminalSettings;
  image?: ImageSettings;
  thinkingBudgets?: ThinkingBudgetsSettings;
  markdown?: MarkdownSettings;
  warning?: WarningSettings;
  defaultProjectTrust?: "ask" | "always" | "never";
  // ...
}

export class SettingsManager {
  constructor(opts: { globalPath: string; projectPath?: string });
  read(): Settings;
  write(patch: DeepPartial<Settings>): void;
  on(event: "change", handler: (s: Settings) => void): () => void;
  drainErrors(): Array<{ scope: "global" | "project"; error: Error }>;
}
```

## 10. pi-coding-agent · RPC 协议

```ts
// packages/coding-agent/src/modes/rpc/rpc-types.ts
export type RpcCommand =
  | { type: "prompt"; id?: string; text: string; images?: ImageContent[] }
  | { type: "abort"; id?: string }
  | { type: "set_model"; id?: string; provider: string; modelId: string }
  | { type: "set_thinking_level"; id?: string; level: ThinkingLevel }
  | { type: "cycle_model"; id?: string; direction: 1 | -1 }
  | { type: "cycle_scoped_model"; id?: string; direction: 1 | -1 }
  | { type: "get_state"; id?: string }
  | { type: "compact"; id?: string }
  | { type: "new_session"; id?: string; options?: { name?: string; parentSession?: string } }
  | { type: "switch_session"; id?: string; path: string }
  | { type: "fork"; id?: string; entryId: string }
  | { type: "list_sessions"; id?: string; cwd?: string }
  | { type: "get_session"; id?: string; path: string }
  | { type: "get_commands"; id?: string }
  | { type: "set_session_name"; id?: string; name?: string }
  | { type: "set_label"; id?: string; entryId: string; label?: string }
  | { type: "execute_bash"; id?: string; command: string; cwd?: string; timeout?: number }
  | { type: "extension_ui_response"; id: string; value: unknown }
  // ... 由扩展注入额外 command 类型
  ;

export type RpcResponse =
  | { type: "response"; id?: string; command: string; success: true; data?: object | null }
  | { type: "response"; id?: string; command: string; success: false; error: string };

export type RpcExtensionUIRequest = {
  type: "extension_ui_request";
  id: string;        // 用于匹配 response
  method: "confirm" | "input" | "select" | "notify" | "custom" | "setWidget" | ...;
  payload: unknown;
};

export type RpcExtensionUIResponse = {
  type: "extension_ui_response";
  id: string;
  value?: unknown;
  cancelled?: boolean;
};
```

**事件流**：`AgentSessionEvent` 直接序列化为 JSON 写 stdout。客户端按 `type` 路由。
