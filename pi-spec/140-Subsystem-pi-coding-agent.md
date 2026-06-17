# 140 · 子系统 `@earendil-works/pi-coding-agent`

> 角色：把"Agent 运行时 + LLM API + TUI 库"装配成可发布的 `pi` CLI。**最大、模块数最多的子系统**。

## 1. 入口

源码：`packages/coding-agent/src/index.ts`

CLI 入口：`packages/coding-agent/src/main.ts`（被 `bin.pi` 指向 `dist/cli.js` 包裹）

## 2. 导出面（`index.ts`）

| 来源 | 关键符号 |
| --- | --- |
| `./cli/args.ts` | `parseArgs`, `Args`, `Mode` |
| `./config.ts` | `getAgentDir`, `getDocsPath`, `getExamplesPath`, `getPackageDir`, `getReadmePath`, `VERSION` |
| `./core/agent-session.ts` | `AgentSession`, `AgentSessionConfig`, `AgentSessionEvent`, `parseSkillBlock` |
| `./core/agent-session-runtime.ts` | `CreateAgentSessionRuntimeFactory`, `createAgentSessionRuntime`, `SessionImportFileNotFoundError` |
| `./core/agent-session-services.ts` | `createAgentSessionFromServices`, `createAgentSessionServices`, `AgentSessionServices`, `AgentSessionRuntimeDiagnostic` |
| `./core/sdk.ts` | `createAgentSession`, `CreateAgentSessionOptions`, `CreateAgentSessionResult` |
| `./core/auth-storage.ts` | `AuthStorage`, `FileAuthStorageBackend`, `InMemoryAuthStorageBackend`, `ApiKeyCredential`, `OAuthCredential`, `AuthStatus` |
| `./core/compaction/index.ts` | `compact`, `shouldCompact`, `generateBranchSummary`, `prepareCompaction`, `DEFAULT_COMPACTION_SETTINGS` |
| `./core/event-bus.ts` | `createEventBus`, `EventBus`, `EventBusController` |
| `./core/extensions/index.ts` | `ExtensionAPI`, `ExtensionContext`, `ExtensionUIContext`, `ExtensionRunner`, `loadExtensions` 等 100+ 类型 |
| `./core/export-html/index.ts` | `exportSessionToHtml`, `ToolHtmlRenderer` |
| `./core/model-registry.ts` | `ModelRegistry` |
| `./core/model-resolver.ts` | `resolveCliModel`, `resolveModelScope`, `ScopedModel`, `defaultModelPerProvider` |
| `./core/prompt-templates.ts` | `expandPromptTemplate`, `PromptTemplate` |
| `./core/resource-loader.ts` | `ResourceLoader`, `DefaultResourceLoader` |
| `./core/session-manager.ts` | `SessionManager`, `SessionInfo`, `SessionContext`, `SessionEntry`, `CURRENT_SESSION_VERSION` |
| `./core/settings-manager.ts` | `SettingsManager`, `Settings` |
| `./core/skills.ts` | `Skill`, `loadSkills`, `formatSkillsForPrompt` |
| `./core/slash-commands.ts` | `SlashCommandInfo` |
| `./core/system-prompt.ts` | `buildSystemPrompt`, `BuildSystemPromptOptions` |
| `./core/telemetry.ts` | telemetry 开关 |
| `./core/tools/index.ts` | `createBashTool`, `createReadTool`, `createWriteTool`, `createEditTool`, `createGrepTool`, `createFindTool`, `createLsTool`, `ToolName` |
| `./core/extensions/types.ts` | 全部 Extension 类型 |
| `./modes/index.ts` | `InteractiveMode`, `runPrintMode`, `runRpcMode` |
| `./utils/*` | 路径、shell、image、ansi、frontmatter、json、sleep |

## 3. 功能分组

### 3.1 CLI 解析 (`cli/args.ts`)

```ts
export type Mode = "text" | "json" | "rpc";
export interface Args { provider?; model?; apiKey?; systemPrompt?; appendSystemPrompt?; thinking?; continue?; resume?; ... messages: string[]; fileArgs: string[]; unknownFlags: Map<string, boolean | string>; diagnostics: ... }
export function parseArgs(args: string[]): Args;
export function printHelp(): void;
export function isValidThinkingLevel(level): level is ThinkingLevel;
```

完整 flag 表见 [40-Module-pi-coding-agent §3](./40-Module-pi-coding-agent.md)。

### 3.2 CLI 启动器 (`cli/`)

| 文件 | 用途 |
| --- | --- |
| `cli/args.ts` | parseArgs |
| `cli/file-processor.ts` | `processFileArguments` — `@file` 内联 |
| `cli/initial-message.ts` | `buildInitialMessage` — 处理 stdin / file / 位置参数 |
| `cli/list-models.ts` | `--list-models` |
| `cli/project-trust.ts` | `createProjectTrustContext` |
| `cli/session-picker.ts` | `--resume` picker |
| `cli/startup-ui.ts` | `shouldRunFirstTimeSetup`, `showFirstTimeSetup`, `showStartupSelector` |
| `main.ts` | 主流程 |

### 3.3 运行时

```ts
// sdk.ts — SDK 入口
export interface CreateAgentSessionOptions {
  cwd?: string;
  agentDir?: string;
  authStorage?: AuthStorage;
  modelRegistry?: ModelRegistry;
  model?: Model<any>;
  thinkingLevel?: ThinkingLevel;
  scopedModels?: Array<{ model: Model<any>; thinkingLevel?: ThinkingLevel }>;
  noTools?: "all" | "builtin";
  resourceLoader?: ResourceLoader;
  sessionManager?: SessionManager;
  settingsManager?: SettingsManager;
  customTools?: ToolDefinition[];
  baseToolsOverride?: Record<string, AgentTool>;
  initialActiveToolNames?: string[];
  allowedToolNames?: string[];
  excludedToolNames?: string[];
}
export function createAgentSession(opts): Promise<CreateAgentSessionResult>;

// agent-session-runtime.ts — 应用级 factory（含 trust）
export function createAgentSessionRuntime(opts): Promise<CreateAgentSessionRuntimeResult>;

// agent-session-services.ts — 跨 cwd 复用的服务
export function createAgentSessionServices(opts): Promise<{ services; diagnostics }>;
export function createAgentSessionFromServices(opts): Promise<{ session; diagnostics }>;
```

### 3.4 `AgentSession` 公共面

```ts
export class AgentSession {
  readonly agent: Agent;
  readonly sessionManager: SessionManager;
  readonly settingsManager: SettingsManager;
  readonly modelRegistry: ModelRegistry;

  prompt(text, options?): Promise<void>;
  abort(): void;
  abortBash(): void;
  subscribe(listener: AgentSessionEventListener): () => void;
  on(event, handler): () => void;
  setModel(model): Promise<boolean>;
  cycleModel(direction: 1 | -1): Promise<boolean>;
  cycleScopedModel(direction: 1 | -1): Promise<boolean>;
  cycleThinkingLevel(): void;
  newSession(options?): Promise<void>;
  switchSession(path): Promise<void>;
  fork(entryId): Promise<void>;
  getSessionName(): string | undefined;
  setSessionName(name): void;
  getActiveTools(): string[];
  setActiveTools(names): void;
  executeBash(command, options?): Promise<BashResult>;
  compact(options?): Promise<void>;
  steer(messages): void;
  followUp(messages): void;
  bindExtensions(opts): void;
  // ...
}
```

事件类型：`AgentSessionEvent`（见 [40-Module-pi-coding-agent §3.3](./40-Module-pi-coding-agent.md)）。

### 3.5 内置工具 (`core/tools/`)

| 工具 | Schema | Operations 接口 |
| --- | --- | --- |
| `Bash` | `command, timeout?` | `BashOperations.exec` |
| `Read` | `path, offset?, limit?` | `ReadOperations.readFile / access / detectImageMimeType?` |
| `Write` | `path, content` | `WriteOperations.writeFile` |
| `Edit` | `path, oldString, newString, globalReplace?` | `EditOperations.readFile / writeFile` |
| `Grep` | `pattern, path?, include?, ignoreCase?, ...` | `GrepOperations.search?` |
| `Find` | `pattern, cwd?, type?, limit?` | `FindOperations.search?` |
| `Ls` | `path, hidden?, ignore?` | `LsOperations.list?` |

工具工厂：

```ts
export function createBashTool(opts?): ToolDefinition;
export function createReadTool(opts?): ToolDefinition;
export function createWriteTool(opts?): ToolDefinition;
export function createEditTool(opts?): ToolDefinition;
export function createGrepTool(opts?): ToolDefinition;
export function createFindTool(opts?): ToolDefinition;
export function createLsTool(opts?): ToolDefinition;
export function createReadOnlyTools(): ToolDefinition[];      // read + grep + find + ls
export function createCodingTools(): ToolDefinition[];        // read + write + edit + bash
export function withFileMutationQueue(...): ...;              // 并发写保护
```

### 3.6 扩展系统 (`core/extensions/`)

#### 3.6.1 类型 (`types.ts`)

- `Extension`, `ExtensionFactory`, `ExtensionAPI`
- `ExtensionContext`, `ExtensionCommandContext`, `ExtensionUIContext`
- `ExtensionMode = "tui" | "rpc" | "json" | "print"`
- `ToolDefinition<TParams, TDetails, TState>` + `defineTool`
- 30+ 事件类型（`SessionStartEvent`, `AgentStartEvent`, `TurnStartEvent`, `MessageStartEvent`, `ToolExecution*Event`, `BashToolCallEvent`, ..., `InputEvent`, `UserBashEvent`, `ModelSelectEvent`, `ThinkingLevelSelectEvent`, `SessionBeforeCompactEvent`, `SessionBeforeForkEvent`, `SessionBeforeSwitchEvent`, `SessionBeforeTreeEvent`, `SessionCompactEvent`, `SessionShutdownEvent`, `SessionTreeEvent`, `ResourcesDiscoverEvent`, `ProjectTrustEvent`）
- `RegisteredCommand`, `ResolvedCommand`
- `ProviderConfig`, `ProviderModelConfig`
- `Keybinding`, `KeybindingsConfig`, `KeybindingsManager`
- `TerminalInputHandler`, `WorkingIndicatorOptions`
- `AutocompleteProviderFactory`, `EditorFactory`
- `MessageRenderer`, `MessageRenderOptions`
- `BuildSystemPromptOptions`, `ExecOptions`, `ExecResult`
- `ExtensionFlag`, `ExtensionShortcut`
- 类型守卫：`isBashToolResult`, `isReadToolResult`, ..., `isToolCallEventType`

#### 3.6.2 Loader (`loader.ts`)

```ts
export function loadExtensions(opts): Promise<LoadExtensionsResult>;
export function loadExtensionFromFactory(factory, ...): ExtensionRuntime;
export function createExtensionRuntime(...): ExtensionRuntime;
```

`LoadExtensionsResult = { extensions: ExtensionRuntime[]; diagnostics: ResourceDiagnostic[]; context: ExtensionContext }`

#### 3.6.3 Runner (`runner.ts`)

- `ExtensionRunner` 持有所有 loaded extension
- 派发事件给 handler
- 在扩展 UI 等待时阻塞
- 错误隔离：单扩展抛错不影响其他
- 安装 tool 钩子到 Agent

#### 3.6.4 Wrapper (`wrapper.ts`)

- `wrapRegisteredTools` — 把 `ToolDefinition` 包成 `AgentTool`（处理 ctx / signal / onUpdate）

#### 3.6.5 注入

- 全局：`~/.pi/agent/extensions/*.ts`
- 项目：`.pi/extensions/*.ts`
- CLI：`pi -e ./foo.ts`
- Package：`pi package install <git-url>`

### 3.7 资源加载 (`core/resource-loader.ts`)

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
  reload(opts?): Promise<void>;
  dispose(): void;
}

export class DefaultResourceLoader implements ResourceLoader { ... }
```

加载顺序：项目 (`.pi/`) 覆盖全局 (`~/.pi/agent/`) 覆盖 package（`pi package install`）。

### 3.8 鉴权存储 (`core/auth-storage.ts`)

```ts
export type ApiKeyCredential = { type: "api_key"; key: string };
export type OAuthCredential = { type: "oauth"; ...; refresh: string; expires: number; ... };
export type AuthCredential = ApiKeyCredential | OAuthCredential;
export type AuthStorageData = Record<string, AuthCredential>;
export type AuthStatus = { status: "ok" | "missing" | "expired"; ... };

export interface AuthStorageBackend { read(): Promise<AuthStorageData>; write(data): Promise<void>; }
export class FileAuthStorageBackend implements AuthStorageBackend { ... }
export class InMemoryAuthStorageBackend implements AuthStorageBackend { ... }
export class AuthStorage {
  get(provider): Promise<AuthStatus>;
  set(provider, credential): Promise<void>;
  remove(provider): Promise<void>;
  list(): Promise<string[]>;
}
```

`proper-lockfile` 保护并发写。

### 3.9 模型注册表 (`core/model-registry.ts`)

- 内置 `MODELS`（来自 `pi-ai`）
- 自定义 provider（用户 JSON / `pi.registerProvider`）
- API key 解析：env → auth.json → config value
- OAuth provider 缓存
- OpenRouter routing preferences
- Anthropic "extra usage" 警告
- `getApiKeyAndHeaders(model) → { ok, apiKey?, headers? } | { ok: false, error }`

### 3.10 模型解析 (`core/model-resolver.ts`)

```ts
export function resolveCliModel(opts): Promise<Model | undefined>;
export function resolveModelScope(opts): Promise<ScopedModel[]>;
export function defaultModelPerProvider(provider): Model | undefined;
export function findInitialModel(opts): Promise<Model | undefined>;
```

`ScopedModel = { model: Model<any>; thinkingLevel?: ThinkingLevel }`（Ctrl+P 循环）。

### 3.11 设置 (`core/settings-manager.ts`)

```ts
export interface Settings {
  enabled?: boolean;
  scopedModels?: Array<{ provider; model; thinkingLevel? }>;
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
}

export class SettingsManager {
  read(): Settings;
  write(patch: DeepPartial<Settings>): void;
  on(event: "change", handler: (s: Settings) => void): () => void;
  drainErrors(): Array<{ scope: "global" | "project"; error: Error }>;
}
```

### 3.12 Session 持久化 (`core/session-manager.ts`)

```ts
export const CURRENT_SESSION_VERSION = 3;

export type SessionEntry =
  | SessionMessageEntry | ThinkingLevelChangeEntry | ModelChangeEntry
  | CompactionEntry | BranchSummaryEntry | LabelEntry
  | SessionInfoEntry | CustomMessageEntry | CustomEntry;

export class SessionManager {
  readHeader(): SessionHeader | undefined;
  writeHeader(header): void;
  appendEntry(entry): void;
  readEntries(): SessionEntry[];
  getEntry(id): SessionEntry | undefined;
  buildSessionContext(opts?): SessionContext;
  static list(): SessionInfo[];
  static getInfo(path): SessionInfo;
  static resolveIdPrefix(prefix): string | undefined;
  assertValidSessionId(id): void;
}
```

### 3.13 Compaction (`core/compaction/`)

`re-export` from `pi-agent-core`，加：

- `DEFAULT_COMPACTION_SETTINGS`
- 触发器（auto、manual、overflow）
- 写回 `SessionManager` + reload
- 与 `SessionBeforeCompactEvent` / `SessionCompactEvent` 钩子

### 3.14 HTML 导出 (`core/export-html/`)

```ts
export interface ExportOptions { outputPath?; themeName?; toolRenderer?: ToolHtmlRenderer; }
export function exportSessionToHtml(sessionPath: string, options: ExportOptions): Promise<void>;
export function exportFromFile(input: string, opts: ExportOptions): Promise<void>;
```

子模块：
- `index.ts` — 入口
- `template.html` / `template.css` / `template.js` — 模板
- `ansi-to-html.ts` — ANSI → HTML
- `tool-renderer.ts` — 工具调用/结果 HTML 渲染
- `vendor/` — 第三方静态资源

### 3.15 Bash 执行 (`core/bash-executor.ts`)

```ts
export interface BashExecutorOptions { onChunk?: (chunk: string) => void; signal?: AbortSignal; }
export interface BashResult { output: string; exitCode: number | undefined; cancelled: boolean; truncated: boolean; fullOutputPath?: string; }
export function executeBashWithOperations(command, cwd, ops, options?): Promise<BashResult>;
```

默认 ops = `createLocalBashOperations()`（可换 SSH / 容器实现）。

### 3.16 HTTP / 网络 (`core/http-dispatcher.ts`)

```ts
export const DEFAULT_HTTP_IDLE_TIMEOUT_MS = 300_000;
export const HTTP_IDLE_TIMEOUT_CHOICES: Array<{ label; timeoutMs }>;
export function parseHttpIdleTimeoutMs(value: unknown): number | undefined;
export function formatHttpIdleTimeoutMs(timeoutMs: number): string;
export function configureHttpDispatcher(timeoutMs?: number): void;
```

包装 `undici.setGlobalDispatcher` (EnvHttpProxyAgent)，给所有 HTTP 调用一个 idle timeout。

### 3.17 信任 (`core/trust-manager.ts` + `core/project-trust.ts`)

- `ProjectTrustStore` — 持久化
- `resolveProjectTrusted(opts): Promise<AppMode>` — 弹确认对话框
- `hasTrustRequiringProjectResources(extensions, cwd): boolean`
- `--project-trust-override` 跳过

### 3.18 包管理 (`core/package-manager.ts` + `package-manager-cli.ts`)

- `DefaultPackageManager`
- `pi package install <git-url>`
- `pi package list / remove / update`
- SSH / HTTPS 都支持
- `handlePackageCommand(argv): Promise<number>`
- `handleConfigCommand(argv): Promise<number>`

### 3.19 Telemetry (`core/telemetry.ts`)

- 匿名事件埋点
- 默认关

### 3.20 System Prompt 构建 (`core/system-prompt.ts`)

```ts
export function buildSystemPrompt(opts: BuildSystemPromptOptions): string;
```

构造完整 system prompt：tools + skills + AGENTS.md + 项目路径 + 日期 + 自定义追加。

### 3.21 Skills (`core/skills.ts`)

```ts
export interface Skill { name; description; content; filePath; disableModelInvocation? }
export function loadSkills(env, dirs, opts?): Promise<{ skills: Skill[]; diagnostics: ResourceDiagnostic[] }>;
export function formatSkillsForPrompt(skills): string;
```

### 3.22 Prompt 模板 (`core/prompt-templates.ts`)

```ts
export interface PromptTemplate { name; description?; body; sourceInfo: SourceInfo }
export function loadPromptTemplates(env, dirs, opts?): Promise<{ prompts: PromptTemplate[]; diagnostics: ResourceDiagnostic[] }>;
export function expandPromptTemplate(template, vars): string;
```

### 3.23 主题 (`modes/interactive/theme/theme.ts`)

- 内置 default + 自定义
- `getThemeByName(name)`
- `getResolvedThemeColors(themeName)`
- `getThemeExportColors(themeName)` — 导出 HTML 用
- `loadThemeFromPath(path)`
- `initTheme()` / `stopThemeWatcher()`
- OSC 11 背景色探测驱动主题适配

### 3.24 三种模式 (`modes/`)

#### 3.24.1 Interactive (`modes/interactive/interactive-mode.ts`)

- TUI 主循环
- editor / autocomplete / selectors / footer / dialog
- 与 `AgentSession` 双向事件
- 子组件：`components/` (40+ 文件)
- 子主题：`theme/`

#### 3.24.2 Print (`modes/print-mode.ts`)

```ts
export interface PrintModeOptions { mode: "text" | "json"; messages?: string[]; initialMessage?: string; initialImages?: ImageContent[]; }
export function runPrintMode(runtime, opts): Promise<number>;
```

#### 3.24.3 RPC (`modes/rpc/`)

- `rpc-mode.ts` — 协议实现
- `rpc-types.ts` — `RpcCommand`, `RpcResponse`, `RpcExtensionUIRequest`, `RpcExtensionUIResponse`
- `jsonl.ts` — `attachJsonlLineReader`, `serializeJsonLine`
- `rpc-client.ts` — 客户端 SDK

### 3.25 输出保护 (`core/output-guard.ts`)

- `takeOverStdout()` — RPC 模式独占 stdout
- `restoreStdout()` — 恢复
- `writeRawStdout` / `flushRawStdout` / `waitForRawStdoutBackpressure`

### 3.26 Event Bus (`core/event-bus.ts`)

```ts
export interface EventBus { emit(channel, data): void; on(channel, handler): () => void; }
export interface EventBusController extends EventBus { clear(): void; }
export function createEventBus(): EventBusController;
```

供扩展间通信；handler 抛错被吞掉。

### 3.27 Source Info (`core/source-info.ts`)

- 标记资源来源：`user` / `project` / `package` / `synthetic`
- 用于 UI 显示与冲突检测

### 3.28 Footer Data (`core/footer-data-provider.ts`)

- 把 AgentSession 状态投影成 footer 可消费数据
- `ReadonlyFooterDataProvider` interface

### 3.29 Slash Commands (`core/slash-commands.ts`)

- `SlashCommandInfo` — 列表
- `/help` / `/model` / `/theme` / `/resume` / `/new` / `/tree` / `/fork` / `/compact` / `/login` 等
- 扩展可注册自定义 `/command`

### 3.30 工具辅助

| 文件 | 用途 |
| --- | --- |
| `core/tools/file-mutation-queue.ts` | 并发写保护 |
| `core/tools/truncate.ts` | 输出截断 |
| `core/tools/output-accumulator.ts` | bash 输出流累积 |
| `core/tools/render-utils.ts` | 工具结果渲染工具 |
| `core/tools/path-utils.ts` | 路径白名单/安全检查 |
| `core/tools/tool-definition-wrapper.ts` | `wrapToolDefinition` |

### 3.31 时间统计 (`core/timings.ts`)

- `time(label, fn)` — 性能埋点
- `printTimings()` / `resetTimings()` — `--verbose` 输出

### 3.32 实验特性 (`core/experimental.ts`)

- 一些在 lab 阶段的 feature flag

### 3.33 工具函数 (`utils/`)

| 文件 | 用途 |
| --- | --- |
| `utils/paths.ts` | 路径归一化、相对化、tild 展开 |
| `utils/shell.ts` | shell 配置、env、killProcessTree、trackDetachedChildPid |
| `utils/image-resize.ts` | 图像缩放 (photon-node) |
| `utils/mime.ts` | MIME 类型探测 |
| `utils/ansi.ts` | ANSI 剥离 |
| `utils/frontmatter.ts` | YAML frontmatter 解析 |
| `utils/json.ts` | 注释剥离 / 解析 |
| `utils/child-process.ts` | `waitForChildProcess` |
| `utils/sleep.ts` | `sleep(ms)` |
| `utils/windows-self-update.ts` | Windows 自更新清理 |
| `utils/clipboard-image*.ts` | 剪贴板图像支持 |

### 3.34 Migrations (`migrations.ts`)

- 旧 session JSONL → v3 自动迁移
- `runMigrations(...)`
- `showDeprecationWarnings()`

### 3.35 Auth Guidance (`core/auth-guidance.ts`)

- `formatNoApiKeyFoundMessage(provider)`
- `formatNoModelSelectedMessage()`
- `formatNoModelsAvailableMessage()`

### 3.36 Provider 元信息

- `core/provider-display-names.ts` — `BUILT_IN_PROVIDER_DISPLAY_NAMES`
- `core/provider-attribution.ts` — `mergeProviderAttributionHeaders`
- `core/resolve-config-value.ts` — `resolveConfigValueOrThrow` 等

### 3.37 Diagnostics (`core/diagnostics.ts`)

- `ResourceDiagnostic` — 加载资源时的非致命错误

## 4. 典型用法

### 4.1 SDK：嵌入到外部 Node 应用

```ts
import {
  createAgentSession,
  FileAuthStorageBackend,
  AuthStorage,
} from "@earendil-works/pi-coding-agent";

const session = await createAgentSession({
  cwd: "/my/project",
  authStorage: new AuthStorage(new FileAuthStorageBackend({ filePath: "/etc/pi/auth.json" })),
  // 可选：customTools, baseToolsOverride, allowedToolNames, ...
});

session.subscribe((e) => {
  if (e.type === "message_update") {
    // 流式消费
  }
});
await session.prompt("Summarize this repo");
```

### 4.2 CLI：交互模式

```bash
pi
```

### 4.3 CLI：单次打印

```bash
pi -p "Say exactly: ok"
```

### 4.4 CLI：JSON 事件流

```bash
pi --mode json "explain this code" | jq
```

### 4.5 CLI：RPC（嵌入式 IDE 集成）

```bash
echo '{"type":"prompt","id":"1","text":"hi"}' | pi --mode rpc
```

### 4.6 自定义工具（扩展）

```ts
// ~/.pi/agent/extensions/greet.ts
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";

export default function (pi: ExtensionAPI) {
  pi.registerTool({
    name: "greet",
    label: "Greet",
    description: "Greet someone",
    parameters: Type.Object({ name: Type.String() }),
    execute: async (_id, params) => ({
      content: [{ type: "text", text: `Hello, ${params.name}!` }],
      details: {},
    }),
  });
}
```

## 5. 内部能力

- `core/experimental.ts` — lab 阶段功能
- `core/output-guard.ts` — RPC 独占 stdout
- `core/footer-data-provider.ts` — footer 状态投影
- `core/agent-session-services.ts` — services 工厂
- `core/agent-session-runtime.ts` — runtime 工厂
- `core/sdk.ts` — SDK 工厂

## 6. 不变量 / 契约

1. **`AgentSession.prompt()` 单线程入口** — 队列在内部管
2. **Compaction 自动触发**：context > `reserveTokens` + `keepRecentTokens`
3. **扩展抛错不杀主循环** — 单扩展隔离
4. **`SessionManager` 写盘失败必须冒泡** — 不静默丢消息
5. **`output-guard.takeOverStdout()` 后必须 `restoreStdout()`**
6. **`package-manager-cli` 只对子命令 `pi package` / `pi config` 生效**
7. **JSONL schema 固定 v3** — 旧版本走 `migrations.ts` 自动迁移
8. **`auth.json` 写并发安全** — `proper-lockfile` 保护
9. **未知 CLI flag 进 `unknownFlags` map**，不报错（供扩展消费）
10. **OAuth 凭证存 `auth.json`** — `expires` 到期时 `AuthStatus = "expired"`
