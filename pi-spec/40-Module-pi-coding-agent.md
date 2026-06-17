# 40 · 模块 `@earendil-works/pi-coding-agent`

> 角色：把"Agent 运行时 + LLM API + TUI 库"装配成可发布的 `pi` CLI。包含 CLI 解析、Session 持久化、Tool 实现、Extension 系统、三种运行模式（Interactive / Print / RPC）。

## 1. 入口与导出面

源码：`packages/coding-agent/src/index.ts`

| 类别 | 关键符号 |
| --- | --- |
| CLI 入口 | `cli.ts`（用户级 wrapper）+ `main.ts`（实际逻辑） |
| 参数解析 | `parseArgs`, `Args`, `printHelp` (`cli/args.ts`) |
| Session | `AgentSession`, `AgentSessionConfig`, `AgentSessionEvent`, `parseSkillBlock` |
| Runtime | `createAgentSession`, `AgentSessionRuntime`, `AgentSessionServices` |
| 工具（核心） | `Bash`, `Edit`, `Read`, `Write`, `Grep`, `Find`, `Ls`（`core/tools/`） |
| 工具辅助 | `OutputAccumulator`, `Truncation`, `FileMutationQueue`, `ToolDefinitionWrapper` |
| 扩展 | `ExtensionAPI`, `ExtensionContext`, `ExtensionCommandContext`, `ExtensionUIContext`, `ExtensionRunner`, `loadExtensions` |
| 鉴权 | `AuthStorage`, `FileAuthStorageBackend`, `InMemoryAuthStorageBackend`, `OAuthCredential`, `ApiKeyCredential` |
| 模型 | `ModelRegistry`, `resolveCliModel`, `resolveModelScope`, `ScopedModel` |
| 资源 | `ResourceLoader`, `loadSkills`, `loadThemes`, `loadPromptTemplates`, `loadExtensions` |
| 设置 | `SettingsManager`, `CompactionSettings`, `RetrySettings`, `TerminalSettings` |
| Compaction | `compact`, `shouldCompact`, `generateBranchSummary`, `prepareCompaction` |
| 导出 | `exportSessionToHtml`, `ToolHtmlRenderer` |
| 事件 | `EventBus`, `createEventBus` |
| HTTP | `configureHttpDispatcher`, `formatHttpIdleTimeoutMs` |
| 路径 | `getAgentDir`, `getDocsPath`, `getExamplesPath`, `getPackageDir`, `getReadmePath`, `VERSION` |
| 三种模式 | `InteractiveMode`, `runPrintMode`, `runRpcMode` (`modes/`) |
| 包管理 | `DefaultPackageManager`, `handlePackageCommand`, `handleConfigCommand` |
| 信任 | `ProjectTrustStore`, `resolveProjectTrusted`, `TrustSelector` |
| Telemetry | `telemetry.ts` |
| Skills | `loadSkills`, `formatSkillsForPrompt`, `Skill` |

## 2. CLI 生命周期

`main.ts` 流程：

```
parseArgs()
   │
   ├── --help / --version      → 直接打印退出
   ├── --list-models           → 列模型退出
   ├── --export <path>         → 把已有 session 导成 HTML 退出
   ├── 解析 stdin 内容 / @file 内容 / 初始 prompt
   │
   ├── 第一次启动？
   │      yes → showFirstTimeSetup()
   │
   ├── 需要认证？
   │      yes → AuthStorage 列出 → 引导 /login
   │
   ├── 创建 AgentSessionServices
   │      ├─ SettingsManager
   │      ├─ ResourceLoader (skills, prompts, themes, extensions, AGENTS.md)
   │      ├─ ModelRegistry
   │      ├─ SessionManager
   │      └─ httpDispatcher
   │
   ├── 解析 cwd 与项目信任
   │
   ├── createAgentSessionRuntime
   │
   └── 解析 AppMode (interactive | print | rpc | json) → 路由到对应 mode
```

## 3. CLI 参数全表（`cli/args.ts`）

| Flag | 说明 |
| --- | --- |
| `--provider <p>` / `--model <m>` | 覆盖模型 |
| `--api-key <key>` | 临时 API key（不入磁盘） |
| `--system-prompt <s>` | 完全替换系统 prompt |
| `--append-system-prompt <s>`（可多次） | 追加到系统 prompt |
| `--thinking <level>` | `off \| minimal \| low \| medium \| high \| xhigh` |
| `-c` / `--continue` | 续最近 session |
| `-r` / `--resume` | 浏览选 session |
| `--session <path\|id>` / `--session-id <id>` | 打开指定 session |
| `--fork <id>` | 从某个 entry 切分支 |
| `--session-dir <path>` | session 目录 |
| `--name <n>` / `-n` | session 显示名 |
| `--no-session` | 不持久化 |
| `--models <a,b,c>` | Ctrl+P 切换的 scoped models |
| `--tools <a,b,c>` / `-t` | 启用工具白名单 |
| `--exclude-tools <a,b,c>` / `-xt` | 工具黑名单 |
| `--no-tools` / `-nt` | 完全关闭工具 |
| `--no-builtin-tools` / `-nbt` | 只允许扩展/SDK 工具 |
| `--extension <path>` / `-e` | 显式加载扩展 |
| `--no-extensions` | 不加载任何扩展 |
| `--skills <a,b,c>` / `--no-skills` | 技能过滤 |
| `--prompt-templates <a,b,c>` / `--no-prompt-templates` | prompt 模板过滤 |
| `--themes <a,b,c>` / `--no-themes` | 主题过滤 |
| `--no-context-files` | 忽略 AGENTS.md / CLAUDE.md |
| `--print` / `-p` | 单次打印模式（不进入 TUI） |
| `--mode <text\|json\|rpc>` | 显式指定运行模式 |
| `--export <path>` | 把 session 导成 HTML |
| `--list-models [provider]` | 列模型 |
| `--offline` | 不做网络请求（除已缓存） |
| `--verbose` | 调试日志 |
| `--project-trust-override` | 跳过项目信任检查 |
| 位置参数 | 初始 prompt；`@file` 内联文件 |

未知 flag 收集到 `unknownFlags` map，由扩展/包消费。

## 4. 三种运行模式

| 模式 | 入口 | 触发 | 行为 |
| --- | --- | --- | --- |
| **interactive** | `modes/interactive/interactive-mode.ts` | TTY + 默认 | 完整 TUI：editor、autocomplete、selectors、footer、对话框 |
| **print** | `modes/print-mode.ts` | `-p` 或 stdin 非 TTY | 一次性 prompt；输出最终回复（`text` 子模式）或 JSON 事件流（`json` 子模式） |
| **rpc** | `modes/rpc/rpc-mode.ts` | `--mode rpc` | JSONL over stdin/stdout；客户端发命令、接收事件、响应 extension UI 请求 |

### 4.1 RPC 协议

`modes/rpc/rpc-types.ts` 定义：

- `RpcCommand` — type + optional id + payload
- `RpcResponse` — `{ id, type: "response", command, success, data?, error? }`
- `RpcExtensionUIRequest` / `RpcExtensionUIResponse` — 扩展 UI 桥
- `RpcSessionState`、`RpcSlashCommand`

`rpc-mode.ts` 接管 stdout，监听 stdin JSONL，事件流式写回。客户端可注入扩展 UI（`daxnuts.ts` 例子）。

### 4.2 Print 模式

`modes/print-mode.ts` 暴露 `PrintModeOptions = { mode: "text" | "json", messages?, initialMessage?, initialImages? }`。`text` 输出 AssistantMessage 最终回复；`json` 流所有 `AgentSessionEvent`。

## 5. 核心内置工具（`core/tools/`）

| 工具 | 关键能力 | 风险 |
| --- | --- | --- |
| `bash.ts` | 启动子进程、流式输出、超时、取消、detach 进程追踪 | 高（默认开启） |
| `edit.ts` | 文本替换（`oldString`/`newString`），支持 multi-edit | 中（需路径白名单） |
| `read.ts` | 读文件、分页、行范围、自动识别图片类型 | 低 |
| `write.ts` | 写文件 | 中 |
| `grep.ts` | ripgrep 风格搜索 | 低 |
| `find.ts` | 按 glob / 名字搜索 | 低 |
| `ls.ts` | 列目录 | 低 |
| `file-mutation-queue.ts` | 并发写保护 | — |
| `truncate.ts` | 输出截断 | — |
| `path-utils.ts` | 路径白名单/安全检查 | — |

每个工具都有 `*Operations` 接口（如 `BashOperations`, `ReadOperations`, `EditOperations`），让 SSH / 容器后端可以替换默认本地实现。

## 6. Session 管理（`core/session-manager.ts`）

- 当前 session schema：`CURRENT_SESSION_VERSION = 3`
- 存储格式：JSONL 文件，路径 `$AGENT_DIR/sessions/`
- Entry 类型：`session`（header） / `message` / `thinking_level_change` / `compaction` / `branch_summary` / `model_change` / `custom_message` / `label`
- 工具：`new SessionManager(path)`, `appendEntry`, `readEntries`, `buildSessionContext`, `getLatestCompactionEntry`, `getLatestBranchSummaryEntry`
- 校验：`assertValidSessionId`

## 7. Compaction

`core/compaction/index.ts` 重新导出 `pi-agent-core` 的同名函数，并加：

- `DEFAULT_COMPACTION_SETTINGS`
- 自动 compaction 决策（基于 token usage 阈值）
- Session 重建（写回 CompactionEntry）

## 8. 扩展系统（`core/extensions/`）

`types.ts` 定义了完整的 Extension API。**这是 pi 最关键的可扩展面**。

| 钩子 | 用途 |
| --- | --- |
| `session_start` / `session_end` | 生命周期 |
| `turn_start` / `turn_end` | 单轮 |
| `message_start` / `message_update` / `message_end` | 单消息 |
| `tool_call` (`bash` / `read` / `edit` / `write` / `grep` / `find` / `ls`) | 各工具的细化钩子 |
| `context` | 上下文变更 |
| `before_agent_start` | 注入额外 prompt / 消息 |
| `before_provider_request` | 改写最终发给 provider 的 payload |
| `input` | 用户输入事件 |
| `agent_start` / `agent_end` | 整个 prompt-run 边界 |
| `model_select` | 模型切换 |
| `auto_retry_start` / `auto_retry_end` | 自动重试 |

`ExtensionAPI` 暴露：

```ts
pi.registerTool({ name, parameters, execute })
pi.registerCommand("name", { description, handler })
pi.registerShortcut("ctrl+x", handler)
pi.registerFlag("--my-flag", { type: "boolean" })
pi.appendEntry(customMessage)
pi.sendMessage(...)
pi.setActiveTools([...])
pi.setModel(...)
pi.events.on("...", handler)
pi.events.off(...)
```

`ExtensionContext` / `ExtensionCommandContext` / `ExtensionUIContext` 暴露：

- `ctx.ui.notify(text, level)`
- `ctx.ui.confirm(title, question)`
- `ctx.ui.input(prompt, options)`
- `ctx.ui.select(title, options)`
- `ctx.ui.custom<T>(factory, opts)` — 注入自定义 TUI 组件
- `ctx.ui.setWidget(component, { placement })` — 在 editor 上下方挂 widget
- `ctx.cwd`, `ctx.hasPermission`, `ctx.abortController`, `ctx.shutdown`

## 9. 资源加载

`core/resource-loader.ts` 把以下资源统一从 `~/.pi/agent/` 与 `.pi/` 加载：

- **扩展**：`.ts` 文件，扫描 `~/.pi/agent/extensions/` 和 `.pi/extensions/`
- **技能 (skills)**：`SKILL.md` 或 frontmatter
- **Prompt 模板**：`<name>.md`
- **主题**：`<name>.json`（或 `.ts`）
- **AGENTS.md / CLAUDE.md**：从 cwd 向上找到根

## 10. 模型注册表与解析

`core/model-registry.ts` 包装 `pi-ai` 的 `MODELS`，加：

- 自定义 provider（用户 JSON 配置）
- API key 解析（env → auth.json → config value）
- OAuth provider 缓存
- OpenRouter routing preferences
- Anthropic "extra usage" 警告

`core/model-resolver.ts` 处理 CLI `--provider/--model`、scoped model（Ctrl+P 循环）、`defaultModelPerProvider`。

## 11. 鉴权存储

`core/auth-storage.ts`：

- `AuthStorage` 抽象
- `FileAuthStorageBackend`（默认，`~/.pi/agent/auth.json`）
- `InMemoryAuthStorageBackend`（测试）
- 凭证类型：`ApiKeyCredential` / `OAuthCredential`
- proper-lockfile 保护并发写

## 12. 设置

`core/settings-manager.ts`：

- `enabled`、`scopedModels`、`compaction`、`branchSummary`、`retry`、`providerRetry`、`terminal`、`image`、`thinkingBudgets`、`markdown`、`warning`
- 全局 + 项目双层配置（项目覆盖全局）
- 异步锁保护并发写

## 13. Telemetry

`core/telemetry.ts` 提供匿名事件埋点开关（默认关）。

## 14. HTML 导出

`core/export-html/` 把 session 转成可分享的静态 HTML 页面：

- `index.ts` 入口
- `template.html` / `template.css` / `template.js`
- `ansi-to-html.ts` — ANSI → HTML
- `tool-renderer.ts` — 工具调用/结果的 HTML 渲染
- `vendor/` 第三方静态资源

可定制度：颜色用主题、扩展工具可注入自定义 renderer（`ToolHtmlRenderer`）。

## 15. Bash 执行

`core/bash-executor.ts`：

- 包装 `BashOperations`（默认 `createLocalBashOperations`）
- 流式输出（`onChunk` 回调）
- 超时与取消
- 输出截断（超限写 `fullOutputPath` 临时文件）
- ANSI 剥离（`stripAnsi`） + 二进制清洗（`sanitizeBinaryOutput`）
- `killTrackedDetachedChildren` 用于 SIGTERM 清理

## 16. 信任

`core/trust-manager.ts` + `core/project-trust.ts`：

- 首次进入新项目：询问是否信任
- `~/.pi/agent/trusted-projects.json` 记录
- `hasTrustRequiringProjectResources` 检测
- 跳过： `--project-trust-override`

## 17. 不变量

1. `AgentSession.prompt()` 是**单线程入口**（队列在内部管理）
2. Compaction 自动触发：context > `reserveTokens` 且 `keepRecentTokens` 阈值
3. Extension 抛错只 kill 自己，不影响主循环
4. SessionManager 写盘失败必须把错误冒泡到 UI（不能静默丢消息）
5. `output-guard.ts`：RPC / 扩展输出会 `takeOverStdout()` 接管写，结束后必须 `restoreStdout()`
6. `package-manager-cli.ts` 处理 `pi config` / `pi package` 子命令

## 18. 测试矩阵

`packages/coding-agent/test/` ~100+ 文件。`test/suite/` 用 `harness.ts` + faux provider 做端到端测试，覆盖：

- bash 持久化、并发、关闭
- compaction（含扩展触发、auto、queue）
- branching、tree navigation、clone
- extension runner、input event、compaction 扩展示例
- model registry、resolver
- session manager、cwd、info
- print mode、rpc、jsonl
- keybindings migration
- skills、settings、startup、first-time-setup
- 各种 UI 组件：footer、editor、theme、trust、session-selector、oauth-selector、tree-selector
