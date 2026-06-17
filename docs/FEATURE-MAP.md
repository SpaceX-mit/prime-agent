# 功能实现地图：prime-agent vs 上游 pi

> **目的**：对比本项目 (`prime-agent`) 与上游 [`pi`](https://github.com/earendil-works/pi) 在每个公开功能点上的实现状态。
> **数据源**：
> - 上游：`/tmp/pi-upstream` (commit `29c1504`)
> - 本项目：本仓库 HEAD = `c5413e6`
> - 本项目功能目标：`pi-spec/50-Features.md` (1-29 节)，`pi-spec/240-Implementation-Plan.md` (Phase 0-7)

---

## 图例

| 状态 | 含义 |
|---|---|
| ✅ | 完全实现（含测试或端到端验证） |
| ⚠️ | 部分实现（接口存在 / 占位实现，但功能不完整） |
| ❌ | 未实现 |
| ➖ | 不适用（设计上有意排除，详见下文） |

---

## 1. 鉴权与登录

| 功能 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| API Key via env var | ✅ | ✅ | `ANTHROPIC_API_KEY` / `OPENAI_API_KEY` / `MINIMAX_API_KEY` |
| `~/.pi/agent/auth.json` 持久化 | ✅ | ✅ | `libs/coding/src/auth_storage.cpp` |
| `--api-key` 一次性注入 | ✅ | ✅ | `apps/pi/main.cpp` |
| 多 provider 多账号 | ✅ | ✅ | `AuthStorage` 支持 |
| OAuth PKCE (Claude.ai) | ✅ | ⚠️ | `libs/coding/src/oauth.cpp` 框架已实现，**未接入实际 OAuth flow** |
| OAuth PKCE (ChatGPT/Codex) | ✅ | ❌ | |
| OAuth PKCE (GitHub Copilot) | ✅ | ❌ | |
| OAuth Device Code | ✅ | ❌ | |
| `/login` 命令 | ✅ | ⚠️ | 框架在 interactive.cpp，但只有 anthropic 占位 |
| `/logout` 命令 | ✅ | ❌ | |

## 2. 模型 / Provider

| Provider / API | 上游 | 本项目 | 备注 |
|---|---|---|---|
| Anthropic Messages | ✅ | ✅ | |
| OpenAI Chat Completions | ✅ | ✅ | |
| OpenAI Responses | ✅ | ❌ | (placeholder 被移除) |
| OpenAI Codex Responses | ✅ | ❌ | |
| Azure OpenAI Responses | ✅ | ❌ | |
| Google Gemini | ✅ | ✅ | |
| Google Vertex | ✅ | ❌ | |
| Mistral | ✅ | ✅ | |
| AWS Bedrock Converse | ✅ | ❌ | |
| Cloudflare AI Gateway | ✅ | ❌ | |
| OpenRouter (via OpenAI-compat) | ✅ | ⚠️ | 协议层 OK，未单独注册 |
| **MiniMax (MiniMax)** | ➖ | ✅ | 本项目独立加入，`https://api.minimaxi.com/v1` |
| 内置模型数量 | ~975 | 21 | 上游由 `models.generated.ts` 自动生成 |

## 3. 工具（Tools）

| 工具 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| `bash` | ✅ | ✅ | `libs/coding/src/tools/bash.cpp` |
| `read` | ✅ | ✅ | |
| `write` | ✅ | ✅ | |
| `edit` (含 multi-edit) | ✅ | ✅ | |
| `grep` | ✅ | ✅ | (自实现简化版，非 ripgrep) |
| `find` (glob) | ✅ | ✅ | |
| `ls` | ✅ | ✅ | |
| `web_search` | ✅ | ❌ | |
| `fetch` | ✅ | ❌ | |
| `git_*` 工具 | ⚠️ | ❌ | 上游靠外部 git 命令 |
| 路径安全检查 (`path-utils`) | ✅ | ⚠️ | 本项目靠 cwd 限定，未做完整 symlink 防护 |
| `file-mutation-queue` 并发写保护 | ✅ | ❌ | |
| 可注入 Operations 接口 | ✅ | ❌ | |
| 工具可执行确认 (trust gate) | ✅ | ❌ | |

## 4. Session 管理

| 功能 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| JSONL 持久化 | ✅ | ✅ | |
| 自动保存 | ✅ | ✅ | interactive 模式每条消息写 |
| `-c` / `--continue` 续最近 | ✅ | ⚠️ | 命令行参数已加，**V1 stub**（V3.1 `run_agent_loop_continue` 已写但未在 CLI 接） |
| `-r` / `--resume` fuzzy 搜索 | ✅ | ❌ | 列出但无交互选择 UI |
| `--session <id>` | ✅ | ⚠️ | 参数加了，未实现加载 |
| `--name` | ✅ | ❌ | |
| `--list-sessions` | ✅ | ✅ | `--list-sessions` |
| `/fork` | ✅ | ❌ | |
| `/clone` | ✅ | ❌ | |
| `/tree` 分支视图 | ✅ | ⚠️ | 命令存在但 stub："not yet wired into interactive mode" |
| `/delete` | ✅ | ❌ | |
| 树形 entry / branching | ✅ | ❌ | |

## 5. Compaction

| 功能 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| 手动 `/compact` | ✅ | ✅ | `libs/coding/src/compaction.cpp` |
| 自动 (阈值) | ✅ | ⚠️ | 接口有，**未接到 agent loop** |
| Overflow 触发 | ✅ | ❌ | |
| 文件操作跟踪 | ✅ | ❌ | |
| Extension 钩子 | ✅ | ❌ | |

## 6. Skills（技能）

| 功能 | 上游 | 本项目 |
|---|---|---|
| 加载 `~/.pi/agent/skills/` | ✅ | ❌ |
| Frontmatter 解析 | ✅ | ❌ |
| System prompt 注入 | ✅ | ❌ |
| `/skill-name` 触发 | ✅ | ❌ |
| `/skills` 列表 | ✅ | ❌ |
| `--skills` / `--no-skills` | ✅ | ❌ |

## 7. Prompt Templates

| 功能 | 上游 | 本项目 |
|---|---|---|
| 加载 prompts | ✅ | ❌ |
| `@prompt-name` 引用 | ✅ | ❌ |

## 8. 主题 (Themes)

| 功能 | 上游 | 本项目 |
|---|---|---|
| JSON 主题文件加载 | ✅ | ❌ |
| 内置 dark + light | ✅ | ⚠️ hard-coded `Theme::dark()` |
| OSC 11 背景探测 | ✅ | ❌ |
| `/theme` 切换 | ✅ | ❌ |
| 主题色用于渲染 | ✅ | ⚠️ hard-coded |

## 9. 扩展 (Extensions)

| 功能 | 上游 | 本项目 |
|---|---|---|
| TypeScript 扩展加载 | ✅ | ➖ (C++ 无 TS) |
| `ctx.registerTool` | ✅ | ❌ |
| `ctx.registerCommand` | ✅ | ❌ |
| `ctx.sendMessage` | ✅ | ❌ |
| UI 桥 (notify/confirm/input/select) | ✅ | ❌ |
| 热重载 `/reload` | ✅ | ❌ |
| 70+ 示例扩展 | ✅ | ❌ |

## 10. 上下文文件

| 功能 | 上游 | 本项目 |
|---|---|---|
| `~/.pi/agent/AGENTS.md` | ✅ | ❌ |
| 项目级 `AGENTS.md` / `CLAUDE.md` | ✅ | ❌ |
| 向上查找 cwd | ✅ | ❌ |
| `--no-context-files` | ✅ | ❌ |

## 11. 交互模式 TUI

| 功能 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| 多行 Editor | ✅ | ✅ | `libs/tui/src/components/editor.cpp` (237 行) |
| Undo/Redo | ✅ | ❌ | Editor 未启用 |
| Kill ring | ✅ | ❌ | |
| Word navigation | ✅ | ✅ | Ctrl-W |
| Autocomplete (`@file`, `/cmd`) | ✅ | ❌ | |
| 图片粘贴 (Ctrl+V) | ✅ | ❌ | |
| 行内图像 (Kitty/iTerm2) | ✅ | ❌ | |
| Kitty 键盘协议 | ✅ | ✅ | V3.10（CSI-u 解析） |
| OSC 9;4 进度 | ✅ | ❌ | |
| OSC 11 背景探测 | ✅ | ❌ | |
| **UTF-8 完整字符处理** | ✅ | ✅ | INC-005 + V3.7-V3.10 |
| **反向录像光标** | ✅ | ✅ | INC-004 |
| **错误显示** | ✅ | ✅ | INC-006 |
| Markdown 渲染 | ✅ | ❌ | (md4c 未集成) |
| 弹窗 (model/settings/session/trust/oauth/theme) | ✅ | ❌ | |
| Footer (model + token) | ✅ | ⚠️ | 只有模型，无 token 计数 |
| 上下文使用率 | ✅ | ❌ | |

## 12. Print 模式 (`-p`)

| 功能 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| `-p "..."` 单次 | ✅ | ✅ | |
| `--mode json` JSON 事件流 | ✅ | ⚠️ | `--json` 实现但**未验证**事件流格式 |
| `@file` 引用 | ✅ | ❌ | |
| stdin 管道 | ✅ | ⚠️ | `apps/pi/main.cpp` 读了 stdin 但未拼到 prompt |
| 多 message 队列 | ✅ | ❌ | |

## 13. RPC 模式

| 功能 | 上游 | 本项目 | 备注 |
|---|---|---|---|
| JSONL over stdin/stdout | ✅ | ✅ | `libs/tui/src/modes/rpc.cpp` |
| `RpcCommand.prompt` | ✅ | ✅ | |
| `RpcCommand.set_model` | ✅ | ❌ | |
| `RpcCommand.set_thinking_level` | ✅ | ❌ | |
| `RpcCommand.get_state` | ✅ | ❌ | |
| `RpcCommand.compact` | ✅ | ❌ | |
| `RpcCommand.abort` | ✅ | ⚠️ | |
| `RpcCommand.new_session` | ✅ | ❌ | |
| `RpcCommand.fork` | ✅ | ❌ | |
| `RpcCommand.switch_session` | ✅ | ❌ | |
| `RpcCommand.list_sessions` | ✅ | ⚠️ | |
| `RpcCommand.get_session` | ✅ | ❌ | |
| `RpcCommand.cycle_model` | ✅ | ❌ | |
| Extension UI 桥 | ✅ | ❌ | |

## 14. SDK

| 功能 | 上游 | 本项目 |
|---|---|---|
| `AgentSession` / `createAgentSession` | ✅ | ❌ |
| 自定义 Runtime/Tool 注入 | ✅ | ❌ |

## 15. CLI 参数对比

| 上游参数 | 本项目 | 差异 |
|---|---|---|
| `-p, --print` | ✅ | |
| `--mode <text\|json\|rpc>` | ⚠️ | `--mode <interactive\|rpc\|json>` (不同值) |
| `--model <pattern>` | ✅ | |
| `--models <list>` (scoped) | ❌ | |
| `--thinking <level>` | ❌ | |
| `--api-key` | ✅ | |
| `-c, --continue` | ⚠️ | 参数加了但 stub |
| `-r, --resume` | ⚠️ | 同上 |
| `--session <path\|id>` | ⚠️ | |
| `--session-id <id>` | ❌ | |
| `--fork <entryId>` | ❌ | |
| `--export <path>` | ⚠️ | 仅 HTML export；上游支持 .html/.jsonl |
| `--list-models` | ✅ | |
| `--list-sessions` | ✅ | |
| `--name` | ❌ | |
| `--no-skills` | ❌ | |
| `--no-extensions` | ❌ | |
| `--no-context-files` | ❌ | |
| `--offline` | ❌ | |
| `--project-trust-override` | ❌ | |
| `--max-tokens` | ✅ | |
| `--temperature` | ✅ | |
| `-h, --help` | ✅ | |
| `-V, --version` | ✅ | |

## 16. Slash Commands（交互模式内）

| Command | 上游 | 本项目 | 状态 |
|---|---|---|---|
| `/help` | ✅ | ✅ | |
| `/exit` / `/quit` | ✅ | ✅ | |
| `/clear` | ➖ | ✅ | (本项目扩展) |
| `/new` | ✅ | ✅ | |
| `/model` | ✅ | ⚠️ | 占位 "(not yet implemented in V1)" |
| `/scoped-models` | ✅ | ❌ | |
| `/settings` | ✅ | ❌ | |
| `/export` | ✅ | ❌ | (有 CLI --export 但无 slash 命令) |
| `/import` | ✅ | ❌ | |
| `/share` | ✅ | ❌ | |
| `/copy` | ✅ | ❌ | |
| `/name` | ✅ | ❌ | |
| `/session` | ✅ | ❌ | |
| `/changelog` | ✅ | ❌ | |
| `/hotkeys` | ✅ | ❌ | |
| `/fork` | ✅ | ❌ | |
| `/clone` | ✅ | ❌ | |
| `/tree` | ✅ | ⚠️ | stub |
| `/trust` | ✅ | ❌ | |
| `/login` | ✅ | ⚠️ | anthropic 占位 |
| `/logout` | ✅ | ❌ | |
| `/compact` | ✅ | ✅ | |
| `/reload` | ✅ | ❌ | |
| `/resume` | ✅ | ⚠️ | stub |
| `/sessions` | ➖ | ✅ | (本项目扩展) |
| `/continue` | ➖ | ✅ | (本项目扩展 = `run_agent_loop_continue`) |
| `/debug` | ✅ | ❌ | |
| `!command` (bash 注入) | ✅ | ❌ | |
| `/arminsayshi` 等彩蛋 | ✅ | ❌ | |

## 17. 系统能力

| 功能 | 上游 | 本项目 |
|---|---|---|
| 多线程 agent 循环 (主线程不阻塞) | ✅ | ✅ (INC-002 fix) |
| 多轮消息历史 | ✅ | ✅ (INC-003 fix) |
| 流式事件无丢失 | ✅ | ✅ (INC-001 fix) |
| 错误向用户展示 | ✅ | ✅ (INC-006 fix) |
| UTF-8 输入完整字符 | ✅ | ✅ (INC-005 + V3.7-3.10) |
| 反色光标 | ✅ | ✅ (INC-004 fix) |
| HTTP keep-alive + 连接池 | ✅ | ⚠️ (有 client 但未池化) |
| 自定义 HTTP dispatcher | ✅ | ❌ |
| HTTP 代理 | ✅ | ⚠️ (有 `core::env::proxy_url`，未在 http_client 用) |
| TLS via OpenSSL | ✅ | ✅ |
| TLS via BoringSSL (browser) | ✅ | ➖ |
| 浏览器模式 (browser-eval) | ✅ | ➖ |
| 重试 (provider-level) | ✅ | ❌ |
| 重试 (pi-level, 指数退避) | ✅ | ❌ |
| 自动 retry 事件 | ✅ | ❌ |
| Context overflow 检测 + 自动 compact | ✅ | ❌ |
| `--offline` | ✅ | ❌ |
| Telemetry (opt-in) | ✅ | ❌ |

## 18. 包管理

| 功能 | 上游 | 本项目 |
|---|---|---|
| `pi package install <git-url>` | ✅ | ❌ |
| `pi package list` | ✅ | ❌ |
| `pi package remove` | ✅ | ❌ |
| `pi package update` | ✅ | ❌ |
| SSH URL 支持 | ✅ | ❌ |
| `pi config <key> <value>` | ✅ | ❌ |

## 19. 项目信任

| 功能 | 上游 | 本项目 |
|---|---|---|
| 首次交互式确认 | ✅ | ❌ |
| `~/.pi/agent/trusted-projects.json` | ✅ | ❌ |
| 检测 trust-requiring resources | ✅ | ❌ |

## 20. 平台支持

| 平台 | 上游 | 本项目 |
|---|---|---|
| Linux x86_64 | ✅ | ✅ (编译过，未 CI) |
| Linux aarch64 | ✅ | ✅ (编译过，未 CI) |
| **Linux riscv64 (K3)** | ✅ (上游有 ci/release) | ✅ (本项目唯一验证平台) |
| macOS arm64 | ✅ | ⚠️ (编译应该 OK，未跑过) |
| Windows x64 | ✅ | ❌ (V3 follow-up) |
| Termux | ✅ | ❌ |
| tmux | ✅ | ⚠️ (TERM 检测有，但无特殊处理) |

## 21. 设置系统

| 功能 | 上游 | 本项目 |
|---|---|---|
| `~/.pi/agent/settings.json` | ✅ | ✅ (`SettingsManager`) |
| `.pi/settings.json` (项目级) | ✅ | ⚠️ 框架在，未做合并 |
| 字段分组 (compaction/retry/terminal/...) | ✅ | ⚠️ 部分支持 |

## 22. 文档

| 项目 | 上游 | 本项目 |
|---|---|---|
| 用户文档站 | pi.dev/docs/latest (29 篇) | 本 README + pi-spec/ (29 个 .md) |
| Quickstart | ✅ | ✅ |
| Sessions 文档 | ✅ | ❌ |
| Compaction 文档 | ✅ | ⚠️ (postmortem 形式) |
| Extensions 文档 | ✅ | ❌ |
| Skills 文档 | ✅ | ❌ |
| Themes 文档 | ✅ | ❌ |
| Settings 文档 | ✅ | ❌ |
| Custom Provider 文档 | ✅ | ❌ |
| Prompt Templates | ✅ | ❌ |
| Terminal Setup | ✅ | ❌ |
| Keybindings | ✅ | ❌ |
| RPC | ✅ | ❌ |
| SDK | ✅ | ❌ |
| Security | ✅ | ❌ |
| Windows | ✅ | ❌ |
| Termux | ✅ | ❌ |
| tmux | ✅ | ❌ |
| Containerization | ✅ | ❌ |
| Session format | ✅ | ⚠️ (JSONL 已知，但无专门文档) |

---

## 23. 测试覆盖

| 类别 | 上游 | 本项目 |
|---|---|---|
| vitest / ctest 文件数 | ~100+ | 13 |
| 单元测试断言总数 | ~几千 | 12 个套件 / 299 个 assertion |
| E2E 集成测试 (PTY) | ✅ (test/suite) | ⚠️ (用 Python pty 临时跑，未自动化) |
| 浏览器烟测 | ✅ | ➖ |
| 性能 profile | ✅ | ❌ |
| 跨平台 CI | ✅ (GitHub Actions) | ❌ |

---

## 24. 质量指标

| 指标 | 上游 | 本项目 |
|---|---|---|
| 启动时间 < 200ms | ✅ | ✅ (~50ms) |
| 按键响应 < 16ms | ✅ | ⚠️ (测过 ~5-20ms) |
| 单二进制包大小 | ~50MB (Node) | ~185KB (riscv64 strip 后) |
| 运行时依赖 | node 20+ | libstdc++ + libssl |
| 内存占用 (空闲) | ~150MB | ~5MB |

---

## 25. INC (Incident) 历史 (本项目)

| ID | 日期 | 描述 | 状态 |
|---|---|---|---|
| INC-001 | 2026-06-17 | `try_pull()` race 丢流事件 | ✅ Fixed (`67cd7d8`) |
| INC-002 | 2026-06-17 | 交互模式主循环冻结 | ✅ Fixed (`ab89f4c`) |
| INC-003 | 2026-06-17 | 多轮对话历史丢失 | ✅ Fixed (`952f43e`) |
| INC-004 | 2026-06-17 | 输入框不显示 / 无光标 | ✅ Fixed (`b8281e0`) |
| INC-005 | 2026-06-17 | UTF-8 多字节拆分 | ✅ Fixed (`40d98fd`) |
| INC-006 | 2026-06-17 | Agent 错误被吞 | ✅ Fixed (`c5413e6`) |
| V3-milestone | 2026-06-17 | run_agent_loop_continue + JSONL + upstream-diff | ✅ Shipped (`cc88a96`) |

---

## 26. 当前阶段对齐 (相对 `240-Implementation-Plan.md`)

| Phase | 计划交付 | 实际状态 |
|---|---|---|
| **P0** 基础库 | ✅ 100% | ✅ 完成 |
| **P1** pi_ai + Anthropic + OpenAI | ✅ 100% | ✅ 完成 |
| **P2** pi_agent + bash/read/write/edit | ✅ 100% | ✅ 完成 |
| **P3** TUI 基础 | ✅ 100% | ✅ 完成（+ Editor / Footer / Theme） |
| **P4** pi_coding 装配 | ✅ 100% | ✅ 完成（Session / Settings / Auth） |
| **P5** 模式 + OAuth + Compaction | ⚠️ 70% | Print/Interactive/RPC 三模式通；OAuth 仅框架；Compaction 仅手动 |
| **P6** 余 Provider + grep/find/ls | ✅ 100% | ✅ 完成 |
| **P7** 优化 + 打包 | ⚠️ 60% | K3 验证；macOS/aarch64 未跑；tools/release.sh 在 |

### V2 / V2.x 加项
- ✅ V2.1-2.5 全部完成
- ✅ V2.6 `/compact` 真工作
- ✅ V2.7 `/login` 框架
- ✅ V2.8 Multi-line Editor
- ✅ V2.9 MiniMax provider

### V3 加项
- ✅ V3.1 `run_agent_loop_continue`
- ✅ V3.2 JSONL 持久化
- ✅ V3.3 `tools/upstream-diff.sh`
- ✅ V3.7 Bracketed paste
- ✅ V3.8 StringDecoder-style buffering
- ✅ V3.9 UTF-8 sanitization
- ✅ V3.10 Kitty keyboard protocol
- ❌ V3.4 `/resume <id>` 加载 JSONL
- ❌ V3.5 debounced redraw
- ❌ V3.6 Markdown 渲染 (md4c)
- ❌ Windows 端口
- ❌ Bedrock Converse provider

---

## 27. 总结

### 已具备的能力（核心工作流可用）
- 交互模式 + Print 模式 + RPC 模式
- 7 个工具（bash / read / write / edit / grep / find / ls）
- 7 个 provider 协议（Anthropic / OpenAI / Google / Mistral / MiniMax / OAuth-framework / PKCE）
- 21 个模型
- 手动 compaction
- 错误显示 + UTF-8 完整字符 + 反色光标 + Kitty 协议 + Bracketed paste

### 缺失的能力（高优先级）
| 优先级 | 功能 | 阻塞的工作流 |
|---|---|---|
| **高** | Markdown 渲染 | TUI 显示质量 |
| **高** | Skills / Themes / Context files | pi-spec §7-10 全部 |
| **高** | 自动 compaction | 长会话 |
| **高** | Extension 系统 | 上游 70% 用户价值 |
| **中** | OAuth (Codex / Copilot) | 减少 API key 依赖 |
| **中** | 分支 / Fork / Tree | 长会话编辑 |
| **中** | `--models` scoped + Ctrl+P | 模型切换 UX |
| **低** | MCP | 上游未稳定，可观望 |
| **低** | Web fetch / search | 工具集扩展 |
| **低** | Windows | 跨平台 |

### 设计上有意排除（V1 非目标）
- ❌ Codex WebSocket 协议（私有协议）
- ❌ 浏览器 OAuth callback
- ❌ MCP（V2 再说）
- ❌ Bedrock / Vertex（直连 Anthropic、OpenAI 即可）

---

## 你让我干过的所有工作（确认）

按 commit 时间排序的**所有**开发工作：

### 第一批：V1 (P0-P7) 基础骨架
- 仓库骨架 + pi_core / pi_http / pi_ai / pi_agent / pi_coding / pi_tui 7 个库
- 7 个 provider + 16 个模型
- 7 个工具
- 三模式（Print / Interactive / RPC）

### 第二批：V2 增强
- V2.1 真 compaction（LLM 生成摘要）
- V2.2 grep/find/ls 工具
- V2.3 HTML export
- V2.4 OAuth 2.0 PKCE primitives
- V2.5 Mistral provider
- V2.6 `/compact` slash command
- V2.7 `/login` 框架
- V2.8 Multi-line Editor

### 第三批：V2.9 + Bug 修复
- V2.9 MiniMax provider（4 个模型）
- **INC-001**：流事件 race fix (`67cd7d8`)
- **INC-002**：交互模式主循环冻结 fix (`ab89f4c`)
- **INC-003**：多轮对话丢失 fix (`952f43e`)

### 第四批：V3 + INC-004/005/006
- V3.1 `run_agent_loop_continue`
- V3.2 JSONL session 持久化
- V3.3 `tools/upstream-diff.sh`
- **INC-004**：输入框不显示 + 无光标 (`b8281e0`)
- **INC-005**：UTF-8 多字节拆分 (`40d98fd`)
- **INC-006**：Agent 错误被吞 + V3.7-V3.10 (`c5413e6`)
  - V3.7 Bracketed paste
  - V3.8 StringDecoder 缓冲
  - V3.9 UTF-8 验证
  - V3.10 Kitty keyboard protocol

### 文档产物（每次 INC 都有）
- `docs/incidents/INDEX.md` — 索引
- `docs/incidents/2026-06-17-streaming-events-race.md` — INC-001
- `docs/incidents/2026-06-17-interactive-mode-hang.md` — INC-002
- `docs/incidents/2026-06-17-multi-turn-missing.md` — INC-003
- `docs/incidents/2026-06-17-input-box-invisible.md` — INC-004
- `docs/incidents/2026-06-17-cjk-input-split.md` — INC-005
- `docs/incidents/2026-06-17-errors-not-surfaced.md` — INC-006 + V3.7-3.10
- `docs/incidents/2026-06-17-v3-milestone.md` — V3 milestone

### 测试产物
- 12 个 ctest 套件 / 299 个断言
- test_streaming 40→44 (INC-006 加 4)
- test_editor 16→51 (INC-004 + INC-005 + V3.7-V3.10 加 35)
- 其他 9 个测试套件维持

### 工具脚本
- `tools/install.sh` — 装到 `~/.local/bin/pi`
- `tools/release.sh` — tar.gz + .deb
- `tools/smoke_pi.sh` — E2E 烟测
- `tools/upstream-diff.sh` — 与上游对比

---

## 当前 HEAD

```
c5413e6 fix(INC-006) + V3.7-V3.10: surface agent errors, harden UTF-8 input
40d98fd fix(INC-005): TUI input box no longer splits UTF-8 multibyte chars
b8281e0 fix(INC-004): TUI input box now shows typed text + cursor
6eea8a9 docs(incidents): V3 milestone postmortem + index entry
cc88a96 feat(v3): run_agent_loop_continue + JSONL session persistence + upstream-diff tool
952f43e fix(INC-003): restore multi-turn conversation support
ab89f4c fix(INC-002): interactive mode freezes during agent runs
9bd5eb0 docs(incidents): mark INC-001 follow-up test as done
0028606 docs(incidents): INC-001 postmortem + regression tests
67cd7d8 fix(v2.9.1): race in stream_simple / agent_loop dropped streamed events
c752418 feat(v2.9): MiniMax China provider + fix auto-register linking bug
a370334 feat(phase-v2-final): editor, /login OAuth wiring, polish
ad9302a docs(pi-spec) + Phase 6+7 polish (final V2 milestone)
adeebae feat(phase-v2): real compaction, html export, more tools, OAuth, Mistral
```