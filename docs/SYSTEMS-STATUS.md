# 四系统详细状态：prime-agent vs 上游 pi

> **目的**：把本项目按上游 pi 的 4 个 npm 包 (`pi-ai` / `pi-agent-core` / `pi-tui` / `pi-coding-agent`) 逐个剖开，列出每个系统的具体能力、本项目做了什么、还瘸着哪些。
> **不做开发**：纯对照分析。
> **数据源**：上游 `/tmp/pi-upstream` commit `29c1504`；本项目 HEAD `024fe35`。

---

## 0. 全局对比

| 维度 | 上游 TS | 本项目 C++ | 比例 |
|---|---:|---:|---:|
| 源码文件数 | 264 | 94 | 36% |
| 源码行数 | 101,539 | 10,910 | 11% |
| 内置模型数 | ~975 | 21 | 2% |
| 内置 Provider 协议 | 9 | 5 | 56% |
| 内置 Tools | 7 | 7 | 100% |
| Slash commands | 23 | 13 | 57% |
| 测试套件 | ~100+ vitest | 12 ctest | 12% |
| 测试断言 | ~几千 | 299 | 5-10% |

代码量差的来源主要是：上游有大量扩展机制、re-export 类型、错误处理、文档字符串、国际化注释、多浏览器兼容性分支等"装饰性"代码。本项目专注核心功能。

---

# 1. `@earendil-works/pi-ai` — 统一 LLM API 层

> 上游：**55 文件 / 31,588 行** | 本项目：**libs/ai 20 文件 / 2,064 行**

## 1.1 已实现 ✅

### 类型系统 (`libs/ai/include/pi_ai/types.hpp`)
- `TextContent` / `ThinkingContent` / `ImageContent` / `ToolCall` — 内容块
- `UserMessage` / `AssistantMessage` / `ToolResultMessage` — 消息
- `Message = std::variant<...>` — 消息联合
- `StopReason` 字符串：`stop` / `length` / `toolUse` / `error` / `aborted`
- `AssistantMessage::error_message` 错误携带
- `to_json()` 序列化（含思考块签名、超长处理）

### 流事件 (`libs/ai/include/pi_ai/event_stream.hpp`)
- `AssistantMessageEvent` — `Start` / `TextStart` / `TextDelta` / `TextEnd` / `ThinkingDelta` / `ToolCallStart` / `ToolCallDelta` / `ToolCallEnd` / `Done` / `Error`
- `EventStream::push()` / `pull()` / `try_pull()` (INC-001 后只有 `pull()` 是阻塞)
- `drain_to_completion()` / `result()` 收尾
- `end_with_error()` 错误终止

### 模型注册表 (`libs/ai/src/models.cpp`)
- `MODELS` 静态注册表：21 个模型（Anthropic 3 / OpenAI 4 / Google 2 / Mistral 3 / MiniMax 9）
- `find_model()` 按 `provider/id` 或裸 `id` 查找
- 类别 API：`anthropic_models()` / `openai_models()` / `google_models()` / `mistral_models()` / `minimax_models()`
- 字段：id / name / provider / api / baseUrl / contextWindow / maxTokens / cost (input/output/cache) / reasoning

### Provider 协议 (`libs/ai/include/pi_ai/provider.hpp`)
- `Provider` 抽象基类：`api()` / `name()` / `stream()` / `stream_simple()`
- `ProviderRegistry` 单例注册
- `ApiKind` 枚举：`AnthropicMessages` / `OpenAICompletions`

### 5 个真实 Provider 实现
| Provider | 文件 | 协议 |
|---|---|---|
| Anthropic | `providers/anthropic.cpp` | Anthropic Messages + SSE |
| OpenAI | `providers/openai.cpp` | OpenAI Chat Completions + SSE |
| Google Gemini | `providers/google.cpp` | Gemini + SSE |
| Mistral | `providers/mistral.cpp` | OpenAI-compat + SSE |
| MiniMax | `providers/minimax.cpp` | OpenAI-compat 委托 OpenAI provider |

### HTTP 客户端 (`libs/http/`)
- 基于 OpenSSL BIO 的自定义 HTTP/1.1 + TLS 客户端
- SSE 解析器 (`libs/http/src/sse_parser.cpp`) — 状态机式解析
- `http_client.hpp` — 连接管理、timeout、headers、body 上传

### 简单流 (`libs/ai/include/pi_ai/stream_simple.hpp`)
- `SimpleStreamOptions` — api_key / max_tokens / temperature / signal / on_payload / on_response
- `stream_simple()` 一站式：拿模型 + 创建 provider + 流式 + 收尾

### 实测验证
- 12/12 ctest pass
- MiniMax M3 真实端到端流式输出验证 (English / Chinese / math / JSON)
- Anthropic / OpenAI smoke 测过

## 1.2 还瘸着 ❌ / ⚠️

### 缺失的 Provider 协议 (高优)

| 协议 | 上游 | 本项目 | 影响 |
|---|---|---|---|
| `openai-responses` | ✅ | ❌ | OpenAI 新 Responses API |
| `openai-codex-responses` | ✅ | ❌ | ChatGPT Plus/Pro 订阅用户 |
| `azure-openai-responses` | ✅ | ❌ | Azure 企业用户 |
| `bedrock-converse-stream` | ✅ | ❌ | AWS 用户 |
| `google-vertex` | ✅ | ❌ | Google Cloud 企业用户 |
| `cloudflare` | ✅ | ❌ | Cloudflare AI Gateway |
| `anthropic-messages` 直接 Anthropic OAuth | ⚠️ | ❌ | Claude.ai Pro/Max |

### 缺失的功能 (高优)

| 功能 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| OAuth providers (Codex / Copilot / Claude.ai) | ✅ | ❌ | 影响"用 ChatGPT 订阅登录" |
| Image generation (OpenRouter Images API) | ✅ | ❌ | |
| Thinking / reasoning content blocks 完整 | ✅ | ⚠️ | `ThinkingContent` 存在但 SSE delta 协议未独立解析 |
| Cache retention (1h/short/none) | ✅ | ❌ | Anthropic prompt cache |
| `Transport` (sse/websocket/auto) | ✅ | ❌ | Codex 走 WebSocket |
| Retry (provider-level 2x + pi-level 3x) | ✅ | ❌ | 失败直接报，不重试 |
| `maxRetryDelayMs` | ✅ | ❌ | |
| `auto_retry_start` / `auto_retry_end` 事件 | ✅ | ❌ | 扩展监听不到 |
| `onPayload` / `onResponse` 回调给扩展 | ✅ | ⚠️ | stream_simple 接口有，但 agent_loop 未用 |
| Context overflow 检测 (`isContextOverflow`) | ✅ | ❌ | 不触发自动 compaction |
| 离线模式 (`--offline`) | ✅ | ❌ | |
| HTTP 代理支持 | ✅ | ⚠️ | 接口有 (`core::env::proxy_url`)，HTTP 客户端未读 |
| 自定义 HTTP dispatcher | ✅ | ❌ | |
| Telemetry (opt-in) | ✅ | ❌ | |
| 浏览器模式 (browser-eval) | ✅ | ➖ | C++ 跑浏览器不合理 |
| `sanitize-unicode` | ✅ | ⚠️ | 我们在 Input/Editor sanitize UTF-8，但 ai 层没共用 |
| `overflow.ts` | ✅ | ❌ | |
| `diagnostics.ts` (provider 调试信息) | ✅ | ❌ | |

### 设计差距

| 项目 | 上游 | 本项目 |
|---|---|---|
| Provider 热加载 | ✅ (`registerApiProvider`) | ⚠️ build-time 静态注册 |
| Per-model 自定义 base URL | ✅ | ✅ (但只有 MiniMax 真用) |
| OpenAI-compat provider (加 endpoint 就跑) | ✅ | ❌ (要自己写 provider 类) |
| 内置模型数量 | ~975 (auto-generated) | 21 (手写) |

---

# 2. `@earendil-works/pi-agent-core` — Agent 运行时

> 上游：**25 文件 / 8,078 行** | 本项目：**libs/agent 3 文件 / 627 行**

代码量差距巨大。本项目只有 `agent_loop.cpp` 一份。**这是最大瘸点之一**。

## 2.1 已实现 ✅

### `agent_loop.hpp`
- `AgentLoopConfig` — `model` / `tools` / `stream_opts` / `signal` / `messages` (V3.1)
- `AgentEvent` — `AgentStart` / `TurnStart` / `TurnEnd` / `MessageStart` / `MessageUpdate` / `MessageEnd` / `ToolExecutionStart` / `ToolExecutionEnd` / `AgentEnd`
- `AgentEventStream` (`std::shared_ptr<EventStream<AgentEvent>>`)
- `mutable std::vector<pi::ai::Message> messages` 字段 (INC-003)
- `run_agent_loop()` / `run_agent_loop_continue()`

### `agent_loop.cpp`
- 工具调用循环：LLM 响应 → 提取 tool_calls → 调 tool → tool result 进 history → 再调 LLM
- 错误事件：缺消息 / assistant 在最后时，`MessageEnd(stop_reason="error", error_message="...")`
- 错误回传 (INC-006)：错误消息经过 EventStream 到达 interactive.cpp 显示
- V3.1 `run_agent_loop_continue`：从已有 messages 恢复
- 4 个测试用例 in `test_streaming.cpp` (含 INC-006 错误流测试)

### `tool.hpp`
- `Tool` 抽象基类：`name()` / `description()` / `parameters_schema()` (JSON Schema) / `execute(args, signal)`
- `ToolResult` 联合类型 (`text` content)
- `AbortSignal` / `MutableAbort` (INC-002)：可中断的信号对象

## 2.2 还瘸着 ❌ / ⚠️ — **本项目最大缺口**

| 缺失能力 | 上游位置 | 本项目 | 影响 |
|---|---|---|---|
| **`Agent` 类** (完整面向对象封装) | `agent.ts` 557 行 | ❌ | 我们只有函数式 `run_agent_loop()`，没有 Agent 对象 |
| **Steering queue / Follow-up queue** | `agent.ts` Agent class | ❌ | Agent 运行时还能接收新 prompt 加进队列 |
| **Hooks**: `beforeToolCall` / `afterToolCall` / `onPayload` / `onResponse` / `transformContext` / `prepareNextTurn` | `agent.ts` | ❌ | 扩展/工具插不进来 |
| **`AgentMessage` 类型 vs LLM `Message`** | `harness/messages.ts` + `convertToLlm` | ❌ | 我们直接用 LLM Message，无应用层抽象 |
| **`AgentHarness` 类** (skill / prompt / session 整合) | `harness/agent-harness.ts` | ❌ | 业务层集成 |
| **`Transport` 抽象** (sse / websocket) | `types.ts` | ❌ | |
| **工具并发执行 (`Parallel` / `Sequential` mode)** | `agent.ts` `toolExecution` | ❌ | 我们顺序串行 |
| **`ToolExecutionMode`** (sequential / parallel) | `agent.ts` | ❌ | |
| **Token / cost 计算** | `harness/utils/...` | ❌ | UI 不显示成本 |
| **Retry** (auto_retry_start/end) | `agent-loop.ts` | ❌ | 失败直接报 |
| **Context overflow 检测 + 自动 compact** | `agent-loop.ts` + `overflow.ts` | ❌ | 长会话必爆 |
| **`thinkingBudgets`** (per-level token budget) | `types.ts` | ❌ | 不能控思考长度 |
| **Active run / abort handling** | `agent.ts` | ⚠️ | 有 MutableAbort 但没 active run 概念 |
| **`getApiKey` override** (扩展可重写 key 解析) | `agent.ts` | ❌ | |
| **Session 信息 (`sessionId`)** | `agent.ts` | ❌ | Agent 不知道 session |

### 与 harness 子系统的差距

上游 `pi-agent-core` 实际上包含 8 个 harness 子模块 (`harness/*`)，本项目**完全没有**：

| 上游 harness | 用途 | 本项目 |
|---|---|---|
| `agent-harness.ts` | Skill/Prompt/Session 一体化包装 | ❌ |
| `compaction/compaction.ts` | 真正的 compaction 引擎（auto + manual） | ❌（在 `libs/coding`） |
| `compaction/branch-summarization.ts` | `/fork` 前的预览 | ❌ |
| `messages.ts` | `convertToLlm` 等转换函数 | ❌ |
| `prompt-templates.ts` | `@prompt-name` 展开 | ❌ |
| `session/jsonl-repo.ts` | JSONL 持久化（双 repo） | ⚠️ 在 `libs/coding/session_manager` |
| `session/memory-repo.ts` | 内存 repo（测试用） | ❌ |
| `session/session.ts` | `Session<T>` 类 + tree/branch | ❌ |
| `session/repo-utils.ts` | createSessionId / createTimestamp | ⚠️ |
| `skills.ts` | Skill 加载与注入 | ❌ |
| `system-prompt.ts` | System prompt 构造 | ❌ |
| `types.ts` | `AgentHarnessResources` 等类型 | ❌ |
| `utils/shell-output.ts` | Bash 输出格式化 | ❌ |
| `utils/truncate.ts` | Token-aware truncation | ❌ |

**结论**：`libs/agent` 只有 **3.5%** 上游代码量（627 / 8078）。核心差距是**没有 Agent 类 + harness**。我们现在能跑通简单的 "问 → 答" 循环，但没有 Agent 框架的所有扩展点和生命周期管理。

---

# 3. `@earendil-works/pi-tui` — 终端 UI 库

> 上游：**28 文件 / 11,992 行** | 本项目：**libs/tui 19 文件 / 2,563 行**（含 6 个 .hpp）

## 3.1 已实现 ✅

### 终端层 (`terminal.cpp` / `terminal.hpp`)
- POSIX termios raw mode (有 SIGINT/SIGWINCH 处理)
- Alt screen + hide cursor
- 非阻塞 `try_read_key()` / 阻塞 `read_key()`
- **完整 UTF-8 处理 (V3.7-V3.10)**：
  - Lead byte 检测 + 续读 continuation bytes
  - Bracketed paste (`\x1b[200~ ... \x1b[201~`)
  - Kitty keyboard protocol CSI-u
  - StringDecoder-style 跨 `read()` 边界缓冲
  - UTF-8 清理 (orphan / overlong / surrogate → U+FFFD)
- ESC 序列解析 (Up/Down/Left/Right/Home/End/Delete/PageUp/PageDown/F1-F12)
- Ctrl-A..Z 分类
- 写入 raw bytes 到 stdout

### 渲染层 (`tui.cpp` / `tui.hpp`)
- `TUI` 主类：`set_root` / `render` / `handle_key` / `run` / `quit`
- 差分渲染：`prev_frame_` 比较，相等时 no-op
- `Component` 基类 + `render(width)` / `on_key(event)`
- 顶层重绘入口 + 50ms key poll

### 组件 (5 个)
| 组件 | 上游 | 本项目 | 状态 |
|---|---|---|---|
| `Box` | 137 行 | header-only 50 行 | ✅ |
| `Text` | 106 行 | header-only 30 行 | ✅ |
| `Input` (单行) | 447 行 | 263 行 | ✅ + 反色光标 (INC-004) |
| `Editor` (多行) | 2307 行 | 272 行 | ⚠️ 只有基础 |
| `Footer` | — | 36 行 | ✅ |

### Editor 已具备 (`libs/tui/src/components/editor.cpp`)
- 多行编辑（`\n` 字符支持）
- UTF-8 多字节字符处理
- 历史记录 (`push_history` / 上/下浏览)
- 行内移动 (Up/Down/Home/End/Left/Right)
- Word kill (Ctrl-W)
- 行删除 (Ctrl-K / Ctrl-U)
- Enter 插入换行 / Ctrl-J 提交
- Ctrl-C 取消

## 3.2 还瘸着 ❌ / ⚠️

### 缺失的关键组件

| 组件 | 上游 | 本项目 | 影响 |
|---|---|---|---|
| `Editor` 完整版 (单组件 2307 行) | undo/redo + kill ring + autocomplete + paste mark + IME | ⚠️ 只有 12% | 缺 undo / autocomplete / paste mark |
| `Markdown` 渲染器 | 826 行 (md4c / 降级 ANSI) | ❌ | TUI 显示原始 markdown |
| `Image` 组件 | 126 行 (Kitty/iTerm2 协议) | ❌ | 不能显示图像 |
| `Loader` | 92 行 | ❌ | 不能 spinner |
| `CancellableLoader` | 40 行 | ❌ | |
| `SelectList` (fuzzy) | 229 行 | ❌ | 不能交互选 |
| `SettingsList` | 250 行 | ❌ | `/settings` 不可能 |
| `Spacer` | 28 行 | ❌ | 布局少 1 个原语 |
| `TruncatedText` | 65 行 | ❌ | 长文本截断 |

### 缺失的核心能力

| 能力 | 上游 | 本项目 |
|---|---|---|
| **Undo/Redo stack** | `undo-stack.ts` | ❌ |
| **Kill ring** | `kill-ring.ts` | ❌ |
| **Word navigation** | `word-navigation.ts` | ⚠️ 只有 Ctrl-W（删除词） |
| **Autocomplete** | `autocomplete.ts` | ❌（无 `@file` / `/cmd` fuzzy） |
| **Fuzzy matching** | `fuzzy.ts` | ❌（`/resume` 选择时用不到） |
| **Image pasting** | `terminal-image.ts` | ❌ |
| **OSC 11 背景探测** | `terminal-colors.ts` | ❌ |
| **OSC 9;4 进度** | TUI 渲染 | ❌ |
| **Stdin buffer** (decoupled stdin handling) | `stdin-buffer.ts` 434 行 | ⚠️ Terminal 自己有 utf8_pending_，但不暴露 API |
| **Native modifiers** (Win/Mac) | `native-modifiers.ts` | ❌ |

### Keybindings 系统

| | 上游 | 本项目 |
|---|---|---|
| 完整 keymap 系统 | `keybindings.ts` 244 行 + `keys.ts` 1400 行 | ❌ |
| 用户可重映射 | ✅ | ❌ |
| `KeybindingsManager.create()` | ✅ | ❌ |
| 预设 keymap | ✅ | ❌ |
| `/hotkeys` 显示 | ✅ | ❌ |

### 渲染质量差距

| 维度 | 上游 | 本项目 |
|---|---|---|
| 终端尺寸自适应 | ✅ (terminal.ts) | ⚠️ 高度用 (void)，未滚动 |
| 滚动 + scrollbar | ✅ (editor.ts 自带) | ❌ |
| Hardware cursor 定位 | ✅ (TUI.ts `CURSOR_MARKER`) | ❌ |
| 真差分渲染 (按 cell) | ✅ (`tui.ts` 1641 行) | ⚠️ 全 string 比较 |
| 颜色主题感知 | ✅ (terminal-colors.ts OSC 11) | ❌ hard-coded dark |
| Tab 间路由 | ✅ (modal/overlay 系统) | ❌ |

---

# 4. `@earendil-works/pi-coding-agent` — CLI + 整合层

> 上游：**156 文件 / 49,881 行** | 本项目：**libs/coding 28 文件 + apps/pi 1 文件 = 3,489 行**

代码量差 **14 倍**。本项目 7%。

## 4.1 已实现 ✅

### 工具 (7 个 — 100% 覆盖)
| 工具 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| `bash` | 448 | 198 | 缺 streaming output / detach 追踪 / 临时文件持久化 |
| `read` | 362 | 142 | 缺图像 MIME 自动检测 |
| `write` | 267 | 49 | OK |
| `edit` | 437 | 101 | 缺 multi-edit 批量 / edit-diff 计算 / 路径操作可注入 |
| `find` | 367 | 111 | 自实现非 ripgrep |
| `grep` | 385 | 126 | 自实现 |
| `ls` | 225 | 87 | |

### CLI (`apps/pi/main.cpp` 585 行)
- 完整参数解析
- 5 个运行模式：interactive / `-p` / `--mode json` / `--mode rpc` / `--list-models` / `--list-sessions`
- 14 个 flag：--model / --provider / --api-key / --max-tokens / --temperature / -c / -r / --session / --export / --json 等
- 模型自动选择 (有 ANTHROPIC 就用 sonnet，有 OPENAI 就用 gpt-4o-mini)
- 错误 exit code 体系

### Auth (`libs/coding/src/auth_storage.cpp`)
- `auth.json` 持久化 (lockfile 包装)
- `AuthCredential` 支持 `ApiKey` / `OAuth` 两种类型
- `get()` / `set()` / `remove()` / `list()` / `read_all()`
- 13 个测试用例

### Session (`libs/coding/src/session_manager.cpp`)
- JSONL 持久化 (V3.2)
- `SessionHeader` / `SessionEntry` (message / compaction / 自定义 type)
- `initialize()` / `read_header()` / `append_entry()` / `read_entries()` / `read_entries_of_type()`
- `default_dir()` / `new_session_id()` / `list_all()` / `resolve_id_prefix()`
- 51 个测试用例 (test_session 是最大测试套件)

### Compaction (`libs/coding/src/compaction.cpp`)
- `compact()` 手动 LLM 生成摘要
- `CompactionSettings` / `CompactionResult`
- 7 个测试用例

### Settings (`libs/coding/src/settings_manager.cpp`)
- 全局 + 项目 settings.json 加载/合并
- `reload()` / `patch()` / `record_error()`
- 13 个测试 (在 test_oauth 里)

### HTML Export (`libs/coding/src/html_export.cpp`)
- 独立 HTML 文件 (含 dark theme + role badges + 折叠工具调用)
- 193 行

### OAuth Framework (`libs/coding/src/oauth.cpp`)
- PKCE 生成 (S256 challenge)
- `CallbackServer` (本地 HTTP server 接 OAuth redirect)
- Token exchange + refresh
- 364 行 — 但**未接入任何真实 provider 的 OAuth flow**

### Modes (`libs/tui/src/modes/`)
- `interactive.cpp` — 完整 TUI 模式（多轮、JSONL、错误显示、UTF-8）
- `rpc.cpp` — JSONL over stdin/stdout

### Slash Commands (13 个 — 57% 覆盖)
| 命令 | 状态 |
|---|---|
| `/help` | ✅ |
| `/exit` / `/quit` | ✅ |
| `/clear` | ✅ (本项目扩展) |
| `/new` | ✅ |
| `/compact` | ✅ |
| `/sessions` | ✅ (本项目扩展) |
| `/continue` | ✅ (本项目扩展) |
| `/tree` | ⚠️ stub |
| `/model` | ⚠️ stub |
| `/login` | ⚠️ stub (只 anthropic) |
| `/resume` | ⚠️ stub |
| `/help` | ✅ |

## 4.2 还瘸着 ❌ / ⚠️ — **第二大量缺口**

### 完全缺失的子系统

| 子系统 | 上游位置 | 本项目 | 备注 |
|---|---|---|---|
| **Skills** | `core/skills.ts` + `harness/skills.ts` | ❌ | 无 skill 加载/注入 |
| **Prompt Templates** | `core/prompt-templates.ts` | ❌ | 无 `@prompt-name` |
| **Themes** | `core/themes/` + `theme-schema.json` | ❌ | 只有 hard-coded `Theme::dark()` |
| **Resources / Context files** (AGENTS.md) | `core/resource-loader.ts` + `loadProjectContextFiles` | ❌ | |
| **Extensions** | `core/extensions/` (5 文件) + `extensions/types.ts` + `extensions/runner.ts` + `extensions/wrapper.ts` | ❌ | **最大缺口之一**：上游 70+ 示例扩展全部依赖 |
| **Package manager** (`pi package install`) | `core/package-manager.ts` + `package-manager-cli.ts` | ❌ | |
| **Project trust** | `core/project-trust.ts` + `core/trust-manager.ts` | ❌ | 首次进项目无确认 |
| **Bash executor** (fork/execve 信号管理) | `core/bash-executor.ts` + `core/exec.ts` | ⚠️ 工具内有简化版 |
| **HTTP dispatcher** | `core/http-dispatcher.ts` | ❌ | 无代理 / 无自定义 dispatcher |
| **Telemetry** | `core/telemetry.ts` | ❌ | |
| **Event bus** | `core/event-bus.ts` | ❌ | 扩展之间通信不了 |
| **SDK** (`createAgentSession`) | `core/sdk.ts` + `core/agent-session.ts` | ❌ | 无法作为库嵌入 |
| **AgentSession runtime** | `core/agent-session.ts` + `agent-session-runtime.ts` + `agent-session-services.ts` | ❌ | |
| **Slash commands framework** | `core/slash-commands.ts` (完整 builtin 列表) | ⚠️ 写死在 interactive.cpp |
| **System prompt 构造** | `core/system-prompt.ts` | ❌ | hard-coded / 空 |
| **Model registry** | `core/model-registry.ts` | ❌ | 我们只有 `find_model()` |
| **Model resolver** | `core/model-resolver.ts` (含 scoped models) | ❌ | 无 `--models a,b,c` |
| **Provider attribution / display names** | `core/provider-display-names.ts` | ❌ | |
| **Source info** (扩展来源) | `core/source-info.ts` | ❌ | |
| **Auth guidance** (no key 时的引导) | `core/auth-guidance.ts` | ❌ | |
| **Session cwd tracking** | `core/session-cwd.ts` | ❌ | |
| **Migrations** | `migrations.ts` | ❌ | |
| **CLI args 框架** | `cli/args.ts` + `cli/file-processor.ts` + `cli/initial-message.ts` + `cli/list-models.ts` + `cli/startup-ui.ts` + `cli/session-picker.ts` + `cli/project-trust.ts` | ⚠️ 在 main.cpp 内 |
| **Print mode** | `modes/print-mode.ts` | ⚠️ 在 main.cpp 内 inline |
| **Print JSON mode** | `modes/print-mode.ts` | ⚠️ `--json` flag 但未验证事件流格式 |
| **RPC mode** | `modes/rpc/*.ts` | ✅ 在 `libs/tui/src/modes/rpc.cpp` |

### 缺失的 Slash Commands (13/23 = 57% 覆盖)

| Command | 上游 | 本项目 |
|---|---|---|
| `/help` | ✅ | ✅ |
| `/exit` / `/quit` | ✅ | ✅ |
| `/new` | ✅ | ✅ |
| `/compact` | ✅ | ✅ |
| `/clear` | ➖ | ✅ (扩展) |
| `/sessions` | ➖ | ✅ (扩展) |
| `/continue` | ➖ | ✅ (扩展) |
| `/tree` | ✅ | ⚠️ stub |
| `/model` | ✅ | ⚠️ stub |
| `/login` | ✅ | ⚠️ stub |
| `/resume` | ✅ | ⚠️ stub |
| `/settings` | ✅ | ❌ |
| `/scoped-models` | ✅ | ❌ |
| `/export` | ✅ | ❌ (有 CLI `--export` 但无 slash) |
| `/import` | ✅ | ❌ |
| `/share` (gist) | ✅ | ❌ |
| `/copy` | ✅ | ❌ |
| `/name` | ✅ | ❌ |
| `/session` | ✅ | ❌ |
| `/changelog` | ✅ | ❌ |
| `/hotkeys` | ✅ | ❌ |
| `/fork` | ✅ | ❌ |
| `/clone` | ✅ | ❌ |
| `/trust` | ✅ | ❌ |
| `/logout` | ✅ | ❌ |
| `/reload` | ✅ | ❌ |
| `/debug` | ✅ | ❌ |
| `!command` (bash inject) | ✅ | ❌ |

### Bash tool 缺失能力

| 上游 | 本项目 |
|---|---|
| Streaming output (`onUpdate`) | ❌ |
| Output truncation (持久化到临时文件) | ❌ |
| Background process tracking (`killTrackedDetachedChildren`) | ❌ |
| `commandPrefix` (注入命令前缀) | ❌ |
| Sandbox check (`resolveSpawnContext`) | ❌ |
| Custom ops (SSH/容器可注入) | ❌ |

### Edit tool 缺失能力

| 上游 | 本项目 |
|---|---|
| Multi-edit (一次性多个 replace) | ❌ |
| Edit-diff 显示 (line numbers) | ❌ |
| `edits[]` JSON Schema 强校验 | ❌ |
| Custom operations 注入 | ❌ |

### Read tool 缺失能力

| 上游 | 本项目 |
|---|---|
| 图像 MIME 自动检测 + 传给 LLM | ❌ |
| 行范围 (`fromLine` / `limit`) | ❌ |
| 多文件批量读 | ❌ |

### Write tool 缺失能力

| 上游 | 本项目 |
|---|---|
| 并发写保护 (`file-mutation-queue`) | ❌ |
| 原子写 (tmp + rename) | ❌ |

### Compaction 缺失能力

| 上游 | 本项目 |
|---|---|
| 自动 compaction (threshold 检测) | ❌ |
| Context overflow 触发 | ❌ |
| 文件操作跟踪 (read/modified files) | ❌ |
| Extension 钩子 | ❌ |
| Branch summary (fork 前预览) | ❌ |
| Token 估算 (`estimateContextTokens`) | ❌ |

### Session 缺失能力

| 上游 | 本项目 |
|---|---|
| 树形 entry / branching | ❌ |
| Fork / clone | ❌ |
| Session metadata (`Session<TMetadata>`) | ❌ |
| `CURRENT_SESSION_VERSION = 3` migration | ❌ |
| Memory repo (test 用) | ❌ |
| 双 repo 抽象 (jsonl + memory) | ❌ |

### Settings 缺失能力

| 上游 | 本项目 |
|---|---|
| 字段分组 (compaction/retry/terminal/image/thinking/markdown) | ⚠️ 部分 |
| Modified fields tracking (避免覆盖用户改动) | ❌ |
| Project trust 检查 | ❌ |
| Storage backend 抽象 (file/empty) | ❌ |

### RPC mode 缺失命令

| 上游 RpcCommand | 本项目 |
|---|---|
| `prompt` | ✅ |
| `set_model` | ❌ |
| `set_thinking_level` | ❌ |
| `get_state` | ❌ |
| `compact` | ❌ |
| `abort` | ⚠️ |
| `new_session` | ❌ |
| `fork` | ❌ |
| `switch_session` | ❌ |
| `list_sessions` | ⚠️ |
| `get_session` | ❌ |
| `cycle_model` | ❌ |
| `cycle_scoped_model` | ❌ |
| `get_commands` | ❌ |
| Extension UI 桥 | ❌ |

### 缺失的 26 项上游文档

| 上游文档 | 本项目 |
|---|---|
| `quickstart.md` | README |
| `usage.md` | README |
| `sessions.md` | ❌ |
| `compaction.md` | postmortem 形式 |
| `extensions.md` | ❌ |
| `skills.md` | ❌ |
| `themes.md` | ❌ |
| `settings.md` | ❌ |
| `prompt-templates.md` | ❌ |
| `keybindings.md` | ❌ |
| `rpc.md` | ❌ |
| `sdk.md` | ❌ |
| `security.md` | ❌ |
| `windows.md` | ❌ |
| `termux.md` | ❌ |
| `tmux.md` | ❌ |
| `containerization.md` | ❌ |
| `custom-provider.md` | ❌ |
| `terminal-setup.md` | ❌ |
| `models.md` | ❌ |
| `providers.md` | ❌ |
| `packages.md` | ❌ |
| `json.md` | ❌ |
| `shell-aliases.md` | ❌ |
| `images/` | ❌ |
| `session-format.md` | ❌ |

---

# 5. 整体总结

## 5.1 我们做对了什么（核心工作流可用）

- ✅ 三模式 (interactive / print / rpc) 全跑通
- ✅ 7 工具 100% 命名一致 (虽然功能简化)
- ✅ 5 个 Provider 协议 (Anthropic / OpenAI / Google / Mistral / MiniMax)
- ✅ 21 个内置模型可即时调用
- ✅ 多轮对话 + JSONL 持久化 + 错误显示
- ✅ UTF-8 完整处理 (V3.7-V3.10 + INC-005) + Kitty 协议 + Bracketed paste
- ✅ 手动 compaction 真工作

## 5.2 关键差距优先级

### 高优先级 (影响日常工作流)
| 项 | 影响 |
|---|---|
| Markdown 渲染 | TUI 输出难读 |
| Skills / Themes / Context files | pi-spec §7-10 全部 |
| 自动 compaction | 长会话必爆 |
| Extension 系统 | 上游 70% 用户价值 |
| Image paste / image rendering | 多模态能力 |
| Streaming bash output | agent 调 bash 时看不到中间输出 |

### 中优先级
| 项 | 影响 |
|---|---|
| Agent 类 (取代 `run_agent_loop` 函数) | 失去扩展点 |
| OAuth (Codex / Copilot) | 减少 API key 依赖 |
| Session 分支 / Fork / Tree | 长会话编辑 |
| `--models` scoped + Ctrl+P | 模型切换 UX |
| Bash detach tracking | 清理子进程 |
| Bash output truncation | 大输出污染 context |
| Auto retry | 网络抖动体验 |

### 低优先级
| 项 | 影响 |
|---|---|
| MCP | 上游未稳定 |
| Web fetch / search | 工具扩展 |
| Windows | 跨平台 |
| SDK | 二方集成 |
| HTTP 代理 | 企业网络 |
| Telemetry | 优化产品 |

## 5.3 本项目独有（不在上游）

- MiniMax provider (4 个模型) — 中国大陆友好
- `/clear` / `/sessions` / `/continue` 命令 — 实用扩展
- `tools/upstream-diff.sh` — 周期对比脚本
- K3 RISC-V 唯一验证平台

## 5.4 量化打分

| 系统 | 上游 (行) | 本项目 (行) | 完成度 |
|---|---:|---:|---:|
| pi-ai | 31,588 | 2,064 | **~30%** (核心跑通，缺 4 个 provider 协议 + OAuth) |
| pi-agent-core | 8,078 | 627 | **~8%** (核心 loop 跑通，无 Agent 类 / hooks / harness) |
| pi-tui | 11,992 | 2,563 | **~21%** (Editor/Input 完整，缺 Markdown/Image/Undo/Select) |
| pi-coding-agent | 49,881 | 3,489 | **~7%** (核心 CLI 完整，缺 Skills/Themes/Extensions/Packages) |
| **总体** | **101,539** | **10,910** | **~11%** |

**一句话总结**：本项目是一个**功能完整但深度有限**的 pi 克隆 —— 三模式 + 7 工具 + 5 provider + 21 模型 + 错误显示 + UTF-8 全字符 + JSONL 持久化都已经可用；但**没有 Markdown 渲染、没有 Skills/Themes/Extensions、没有 Agent 框架、没有自动 compaction、没有图像支持、没有 OAuth** —— 这些是上游 70% 用户价值的来源。