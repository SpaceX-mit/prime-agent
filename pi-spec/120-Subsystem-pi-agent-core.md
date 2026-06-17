# 120 · 子系统 `@earendil-works/pi-agent-core`

> 角色：把"模型对话"封装成"Agent 循环"。提供状态机、消息转换、工具调度、上下文压缩、Session 抽象。无 CLI、无 UI。

## 1. 入口

源码：`packages/agent/src/index.ts`

```ts
// 全部 re-export
export * from "./agent.ts";
export * from "./agent-loop.ts";
export * from "./harness/agent-harness.ts";
export { ... } from "./harness/compaction/branch-summarization.ts";
export { ... } from "./harness/compaction/compaction.ts";
export * from "./harness/messages.ts";
export * from "./harness/prompt-templates.ts";
export * from "./harness/session/jsonl-repo.ts";
export * from "./harness/session/memory-repo.ts";
export * from "./harness/session/repo-utils.ts";
export * from "./harness/session/session.ts";
export { uuidv7 } from "./harness/session/uuid.ts";
export * from "./harness/skills.ts";
export * from "./harness/system-prompt.ts";
export * from "./harness/types.ts";
export * from "./harness/utils/shell-output.ts";
export * from "./harness/utils/truncate.ts";
export * from "./proxy.ts";
export * from "./types.ts";
```

## 2. 功能分组

### 2.1 Agent 循环

```ts
// packages/agent/src/agent-loop.ts
export function agentLoop(prompts, context, config, signal?, streamFn?): EventStream<AgentEvent, AgentMessage[]>;
export function agentLoopContinue(context, config, signal?, streamFn?): EventStream<AgentEvent, AgentMessage[]>;
export async function runAgentLoop(...): Promise<AgentMessage[]>;
export async function runAgentLoopContinue(...): Promise<AgentMessage[]>;
```

`agentLoop` / `agentLoopContinue` 是**工厂**，返回 `EventStream` 让你边消费边执行。

`runAgentLoop` / `runAgentLoopContinue` 是**等终态**版本，返回 `Promise<AgentMessage[]>`。

### 2.2 Agent 类

```ts
// packages/agent/src/agent.ts
export class Agent {
  state: AgentState;
  streamFn: StreamFn;
  beforeToolCall?(ctx): Promise<...>;
  afterToolCall?(ctx): Promise<...>;
  subscribe(listener: (e: AgentEvent) => void): () => void;
  // ...
}
```

`Agent` 是状态机；构造时绑定 `streamFn` 与可选钩子。

### 2.3 钩子 (Hooks)

8 个钩子（`AgentLoopConfig`）：

| 钩子 | 时机 | 不变量 |
| --- | --- | --- |
| `convertToLlm` | 每次 LLM 调用前 | **不抛**；返回 `Message[]` |
| `transformContext` | 每次 LLM 调用前 | 返回新 context |
| `hasToolCall` | 决定是否进入工具执行 | — |
| `executeToolCall` | 真执行 | 返回 `AgentToolResult` |
| `beforeToolCall` | 工具前 | `{ block?, reason? }` — block 注入 error tool result |
| `afterToolCall` | 工具后 | 字段级覆盖 `content/details/isError/terminate` |
| `shouldStopAfterTurn` | 轮末 | `boolean | AgentLoopTurnUpdate` |
| `prepareNextTurn` | 轮末 | 改 context/model/thinkingLevel |

### 2.4 事件类型

```ts
// packages/agent/src/types.ts
export type AgentEvent =
  | { type: "agent_start" }
  | { type: "agent_end"; messages: AgentMessage[] }
  | { type: "turn_start" }
  | { type: "turn_end"; message: AssistantMessage; toolResults: ToolResultMessage[] }
  | { type: "message_start"; message: AgentMessage }
  | { type: "message_update"; message: AgentMessage; assistantMessageEvent: AssistantMessageEvent }
  | { type: "message_end"; message: AgentMessage }
  | { type: "tool_execution_start"; toolCallId; toolName; args }
  | { type: "tool_execution_update"; toolCallId; toolName; args; partialResult: AgentToolResult }
  | { type: "tool_execution_end"; toolCallId; toolName; result: AgentToolResult; isError: boolean };
```

### 2.5 状态

```ts
// packages/agent/src/types.ts
export interface AgentState {
  systemPrompt: string;
  model: Model<any>;
  thinkingLevel: ThinkingLevel;
  messages: AgentMessage[];
  isStreaming: boolean;
  streamingMessage: AssistantMessage | undefined;
  pendingToolCalls: AgentToolCall[] | undefined;
  errorMessage: string | undefined;
}

export interface AgentContext {
  systemPrompt: string;
  messages: AgentMessage[];
  tools: AgentTool[];
}
```

### 2.6 执行模式

```ts
export type ToolExecutionMode = "sequential" | "parallel";
export type QueueMode = "all" | "one-at-a-time";
```

- `sequential` — 一个 tool 完整结束后再下一个
- `parallel` — tool call 准备顺序进行，**allowed tool 并发执行**；`tool_execution_end` 事件按完成顺序发
- 队列模式决定 steer/follow-up 在 queue drain 时如何注入

### 2.7 工具抽象

```ts
export interface AgentTool<TParams = any, TDetails = unknown> {
  name: string;
  description: string;
  parameters: TSchema;             // TypeBox
  label: string;
  execute(
    toolCallId: string,
    params: TParams,
    signal: AbortSignal,
    onUpdate: AgentToolUpdateCallback,
  ): Promise<AgentToolResult<TDetails>>;
}

export type AgentToolResult<TDetails = unknown> = {
  content: (TextContent | ImageContent)[];
  details?: TDetails;
};

export type AgentToolUpdateCallback = (partial: AgentToolResult) => void;
```

`AgentToolCall` = `Extract<AssistantMessage["content"][number], { type: "toolCall" }>`。

### 2.8 消息

```ts
// packages/agent/src/types.ts
export type AgentMessage =
  | { role: "user"; content: string | (TextContent | ImageContent)[]; timestamp: number }
  | AssistantMessage
  | ToolResultMessage
  | { role: "custom"; customType: string; content?: ...; display?: boolean; details?: unknown; timestamp: number }
  | { role: "bashExecution"; command: string; output: string; exitCode: number | undefined; cancelled: boolean; truncated: boolean; fullOutputPath?: string; timestamp: number };
```

`bashExecution` 来自交互模式的 `!command` 注入，不进 LLM 但进 session。

### 2.9 Compaction (上下文压缩)

`packages/agent/src/harness/compaction/compaction.ts`

```ts
export const DEFAULT_COMPACTION_SETTINGS: CompactionSettings;

export function shouldCompact(ctx, settings): boolean;
export function findCutPoint(messages, settings): number;
export function findTurnStartIndex(messages, fromIndex): number;
export function prepareCompaction(...): CompactionPreparation;
export function generateSummary(...): Promise<{ summary: string; usage?: Usage }>;
export function compact(...): Promise<CompactionResult>;
export function calculateContextTokens(messages): number;
export function estimateContextTokens(...): number;
export function estimateTokens(text): number;          // 字符 / 4 粗估
export function serializeConversation(messages): string;
export function getLastAssistantUsage(messages): Usage | undefined;
```

**Settings 默认值**：
- `enabled: true`
- `reserveTokens: 16384`
- `keepRecentTokens: 20000`

**CompactionEntry 详情**：跟踪 read/modified 文件列表。

### 2.10 分支摘要 (Branch Summary)

`packages/agent/src/harness/compaction/branch-summarization.ts`

```ts
export function generateBranchSummary(...): Promise<BranchSummaryResult>;
export function prepareBranchEntries(entries, targetId, options): BranchPreparation;
export function collectEntriesForBranchSummary(...): CollectEntriesResult;
```

用于 `/tree` / `/fork` 前生成"切出会丢什么"预览。

### 2.11 Session 抽象

`packages/agent/src/harness/session/`

```ts
// session.ts — 内存中的当前 session 视图
export interface SessionState { id; parentSession?; entries: SessionEntry[]; }
// jsonl-repo.ts — JSONL 磁盘实现
// memory-repo.ts — 内存实现（测试用）
// repo-utils.ts — 读写公共
// uuid.ts
export function uuidv7(): string;          // 时间排序 UUID
```

> 注：`pi-coding-agent` 的 `core/session-manager.ts` 是更上层的封装（含 v3 schema、tree 等）。

### 2.12 Skills 加载与格式化

`packages/agent/src/harness/skills.ts`

```ts
export interface Skill { name: string; description: string; content: string; filePath: string; disableModelInvocation?: boolean; }
export async function loadSkills(env, dirs, opts?): Promise<{ skills: Skill[]; diagnostics: SkillDiagnostic[] }>;
export function formatSkillInvocation(skill, additional?): string;  // 包成 <skill> 块
```

`disableModelInvocation: true` 表示技能不进入 system prompt 列表（仅供显式调用）。

### 2.13 System Prompt

`packages/agent/src/harness/system-prompt.ts`

```ts
export function formatSkillsForSystemPrompt(skills): string;   // XML 化技能清单
```

完整的 `buildSystemPrompt` 在 `pi-coding-agent`（带 cwd、context files、tools 等）。

### 2.14 Prompt Templates

`packages/agent/src/harness/prompt-templates.ts`

```ts
export function expandPromptTemplate(template: string, vars: Record<string, string>): string;
export function parsePromptTemplate(name: string, content: string): { name: string; description?: string; body: string };
```

### 2.15 Proxy

`packages/agent/src/proxy.ts`

```ts
export function getProxyForUrl(url: string): ProxyAgent | undefined;
export class ProxyAgent { ... }
```

走 Node `http`/`https` 代理，给 LLM 调用链用。

### 2.16 工具辅助

| 文件 | 用途 |
| --- | --- |
| `harness/utils/shell-output.ts` | shell 输出截断、ANSI 剥离 |
| `harness/utils/truncate.ts` | 文本截断（head / tail / line） |
| `harness/messages.ts` | `createCompactionSummaryMessage`, `createBranchSummaryMessage`, `createCustomMessage` |
| `harness/agent-harness.ts` | 通用 Agent 工厂（不依赖 TUI） |
| `harness/env/` | 工具运行时环境 |

### 2.17 类型

```ts
// packages/agent/src/types.ts 集中所有类型
export type StreamFn = (...args: Parameters<typeof streamSimple>) => ReturnType<typeof streamSimple> | Promise<...>;
export type ToolExecutionMode = "sequential" | "parallel";
export type QueueMode = "all" | "one-at-a-time";
export type AgentToolCall = ...;
export interface BeforeToolCallResult { block?; reason? }
export interface AfterToolCallResult { content?; details?; isError?; terminate? }
export interface BeforeToolCallContext { assistantMessage; toolCall; args; context; }
export interface AfterToolCallContext { assistantMessage; toolCall; args; result; isError; context; }
export interface ShouldStopAfterTurnContext { message; toolResults; context; newMessages; }
export interface AgentLoopTurnUpdate { context?; model?; thinkingLevel?; }
export interface PrepareNextTurnContext extends ShouldStopAfterTurnContext {}
```

## 3. 典型用法

### 3.1 跑一个最小 Agent

```ts
import {
  Agent, agentLoop, streamSimple,
  type AgentLoopConfig, type AgentMessage,
} from "@earendil-works/pi-agent-core";
import { getModel } from "@earendil-works/pi-ai";

const model = getModel("anthropic", "claude-sonnet-4-5");
const config: AgentLoopConfig = {
  model,
  // convertToLlm 默认实现：过滤 user/assistant/toolResult
  executeToolCall: async (call, args, onUpdate, signal) => {
    return { content: [{ type: "text", text: "ok" }] };
  },
};

const agent = new Agent({ streamFn: streamSimple, systemPrompt: "...", model });
const prompts: AgentMessage[] = [
  { role: "user", content: "hi", timestamp: Date.now() },
];
for await (const ev of agentLoop(prompts, agent.state, config)) {
  console.log(ev.type);
}
```

### 3.2 触发 Compaction

```ts
import { shouldCompact, compact, DEFAULT_COMPACTION_SETTINGS } from "@earendil-works/pi-agent-core";

if (shouldCompact(ctx, DEFAULT_COMPACTION_SETTINGS)) {
  const result = await compact(ctx, model, settings, deps);
  // result.newMessages 替换 ctx.messages
}
```

### 3.3 加载 Skills

```ts
import { loadSkills, formatSkillInvocation } from "@earendil-works/pi-agent-core";

const { skills } = await loadSkills(env, [".pi/skills", "~/.pi/agent/skills"]);
const invocation = formatSkillInvocation(skills[0]);
// → "<skill name=\"...\" location=\"...\">\nReferences are relative to ...\n\n<body>\n</skill>"
```

## 4. 内部能力

- `harness/env/` — 工具运行时的环境变量/路径
- `harness/session/jsonl-repo.ts` — Session 持久化的 JSONL 抽象（`pi-coding-agent` 内有更上层封装）

## 5. 不变量 / 契约

1. **`streamFn` 永不抛 / 永不 reject** — 所有失败落到 `AssistantMessage.stopReason = "error" | "aborted"`
2. **`convertToLlm` 永不抛** — 不能转换的消息必须**过滤**
3. **`agentLoopContinue` 要求 context 末条 ≠ assistant**，否则抛错
4. **`beforeToolCall` block 不中断循环** — 注入 error tool result 后继续
5. **`terminate: true` 在并行模式下需 batch 内全员同意**
6. **`uuidv7` 始终时间排序** — session ID 用它，便于前缀搜索
7. **Compaction 触发后 session 必须 reload**（`pi-coding-agent` 在 `compact` 内部做）
8. **`shouldCompact` 不修改 context** — 是纯判定函数
