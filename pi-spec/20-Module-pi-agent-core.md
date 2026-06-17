# 20 · 模块 `@earendil-works/pi-agent-core`

> 角色：把"模型对话"封装成"Agent 循环"。负责状态、消息转换、工具调用编排、上下文压缩。无 CLI、无 UI。`pi-coding-agent` 把它包装成可运行产品。

## 1. 入口与导出面

源码：`packages/agent/src/index.ts`

| 类别 | 关键符号 |
| --- | --- |
| 核心循环 | `agentLoop`, `agentLoopContinue`, `runAgentLoop` |
| Agent 类 | `Agent`（agent.ts） |
| 消息 | `AgentMessage`, `AgentState`, `AgentEvent`, `AgentContext` |
| 工具 | `AgentTool`, `AgentToolResult`, `AgentToolUpdateCallback`, `AgentToolCall` |
| 流函数 | `StreamFn`（`packages/ai/streamSimple` 的薄包装） |
| 执行模式 | `ToolExecutionMode = "sequential" \| "parallel"` |
| 队列模式 | `QueueMode = "all" \| "one-at-a-time"` |
| 钩子结果 | `BeforeToolCallResult`, `AfterToolCallResult` |
| 钩子上下文 | `BeforeToolCallContext`, `AfterToolCallContext`, `ShouldStopAfterTurnContext` |
| 钩子配置 | `AgentLoopConfig` (含 `convertToLlm`, `transformContext`, `hasToolCall`, `executeToolCall`, `beforeToolCall`, `afterToolCall`, `shouldStopAfterTurn`, `prepareNextTurn`) |
| Harness 子模块 | `harness/agent-harness.ts`, `harness/messages.ts`, `harness/system-prompt.ts`, `harness/prompt-templates.ts`, `harness/skills.ts` |
| Compaction | `compact`, `shouldCompact`, `findCutPoint`, `prepareCompaction`, `generateSummary`, `serializeConversation`, `calculateContextTokens`, `estimateTokens`（同 `pi-coding-agent` 重新导出） |
| 分支摘要 | `generateBranchSummary`, `prepareBranchEntries`, `collectEntriesForBranchSummary` |
| 会话存储 | `harness/session/jsonl-repo.ts`, `harness/session/memory-repo.ts`, `harness/session/session.ts`, `harness/session/uuid.ts` |
| 工具 | `uuidv7` |
| Proxy | `proxy.ts` (含 `ProxyAgent` 等 HTTP 代理) |
| Shell 工具 | `harness/utils/shell-output.ts` |
| 截断 | `harness/utils/truncate.ts` |
| 类型 | `types.ts`（一切类型集中） |

## 2. 状态机：Agent 循环

`agentLoop` 与 `agentLoopContinue`：

```
                ┌───────────────────────────────────────┐
prompts ──▶    │  1. 把 prompts 追加到 context         │
                │  2. 调 streamFn(model, ctx)           │
context ──▶    │  3. 拿到 AssistantMessageEventStream  │
config  ──▶    │  4. 顺序处理 tool_call 事件           │
signal  ──▶    │     ├─ beforeToolCall  (可 block)     │
streamFn ──▶   │     ├─ executeToolCall                │
                │     └─ afterToolCall   (可改 result)  │
                │  5. 决定是否进入下一轮：              │
                │     ├─ shouldStopAfterTurn  (yes)     │
                │     ├─ prepareNextTurn       (改 ctx) │
                │     └─ 否则继续到 step 2              │
                └───────────────────────────────────────┘
```

事件类型（`AgentEvent`）：

```
turn_start
turn_end
message_start
message_update
message_end
tool_execution_start
tool_execution_update
tool_execution_end
agent_start
agent_end
```

并行工具执行模式（`ToolExecutionMode = "parallel"`）：所有 tool call 准备完毕后，allowed 的工具**并发**执行；`tool_execution_end` 事件按完成顺序发出；tool-result 消息按 assistant 源顺序写入。

## 3. 钩子契约

| 钩子 | 时机 | 返回值 |
| --- | --- | --- |
| `convertToLlm` | 每次 LLM 调用前 | `Message[]`，**不抛**（抛了会绕过正常事件序列） |
| `transformContext` | 每次 LLM 调用前 | 改写后的 `AgentContext` |
| `hasToolCall` | 决定是否进入工具执行 | `boolean` |
| `executeToolCall` | 真执行工具 | `AgentToolResult` |
| `beforeToolCall` | 工具调用前 | `{ block?: boolean; reason?: string }`（block 注入 error tool result） |
| `afterToolCall` | 工具调用后 | `{ content?; details?; isError?; terminate? }` 字段级覆盖 |
| `shouldStopAfterTurn` | 轮末 | `boolean \| AgentLoopTurnUpdate` |
| `prepareNextTurn` | 轮末 | `AgentLoopTurnUpdate`（改 context/model/thinkingLevel） |

> `terminate: true` 在 batch 内**所有** finalized tool result 都设了才触发提前终止。

## 4. Compaction（上下文压缩）

源码：`packages/agent/src/harness/compaction/compaction.ts`

| 函数 | 作用 |
| --- | --- |
| `shouldCompact(ctx, settings)` | 根据 token 预算判断是否该压 |
| `findCutPoint(messages, settings)` | 找保留/压缩分界点 |
| `prepareCompaction(ctx, settings)` | 准备 LLM 总结的 prompt |
| `generateSummary(...)` | 调 `completeSimple` 让 LLM 出总结 |
| `compact(...)` | 主入口：生成 CompactionEntry，写回 session |
| `calculateContextTokens` / `estimateTokens` | token 估算 |
| `findTurnStartIndex` | 找"轮"起点（user/assistant 边界） |
| `serializeConversation` | 把 AgentMessage[] → LLM 文本 |
| `getLastAssistantUsage` | 取最后一次的 usage |
| `DEFAULT_COMPACTION_SETTINGS` | `{ enabled, reserveTokens=16384, keepRecentTokens=20000 }` |

### 4.1 分支摘要

`branch-summarization.ts` 用于 `/tree` / `/fork` 等分支命令前，生成"如果从这个 entry 切出会丢失什么"的预览。

## 5. Session 持久化

`packages/agent/src/harness/session/`

- `session.ts` — 内存中维护当前 session 状态
- `jsonl-repo.ts` — JSONL 文件 repo（pi 默认的磁盘格式）
- `memory-repo.ts` — 内存 repo（测试用）
- `repo-utils.ts` — 共用读写
- `uuid.ts` — `uuidv7`（时间排序 UUID）

> 注意：真正的 `SessionManager` 在 `pi-coding-agent` 包内（`core/session-manager.ts`），这里只暴露**抽象 + 默认 JSONL 实现**，让 Agent 类不依赖具体存储。

## 6. Proxy

`packages/agent/src/proxy.ts`

- `ProxyAgent` 走 Node `http`/`https` 代理
- 提供给 LLM 调用链使用

## 7. Harness 辅助

| 文件 | 用途 |
| --- | --- |
| `harness/messages.ts` | `createCompactionSummaryMessage`, `createBranchSummaryMessage`, `createCustomMessage` |
| `harness/system-prompt.ts` | `buildSystemPrompt` (可独立于 `pi-coding-agent` 使用) |
| `harness/skills.ts` | `loadSkills`, `formatSkillsForPrompt`, `Skill` |
| `harness/prompt-templates.ts` | prompt template 解析与展开 |
| `harness/env/` | 环境变量 + 工具运行时环境 |
| `harness/utils/shell-output.ts` | shell 输出截断与转义 |
| `harness/utils/truncate.ts` | 通用 truncate |

## 8. 不变量

1. `StreamFn` **不得抛**或 reject；所有失败必须落到 `stopReason: "error"|"aborted"`
2. `convertToLlm` **不得抛**；不能转换的消息必须过滤
3. `agentLoopContinue` 要求 context 最后一条**不是** assistant，否则抛错（用法约束）
4. `beforeToolCall` 的 `block: true` 仍然产生 `error` tool result，循环不会中断
5. `shouldStopAfterTurn` 返回 `AgentLoopTurnUpdate` 时，下一轮会用新的 context/model
6. `terminate` 信号在并行模式下需"全员同意"

## 9. 测试要点

`packages/agent` 自身测试少（不依赖 UI），主要测试由 `pi-coding-agent/test/suite/` 通过 `harness.ts` + faux provider 覆盖：

- 顺序/并行工具执行
- compaction 触发条件
- branch 摘要
- 队列模式（`one-at-a-time` / `all`）
- 自动重试
