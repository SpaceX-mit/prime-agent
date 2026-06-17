# 50 · 功能清单 (Features)

> 把仓库能"做什么"按子系统分组列出来。每条都对应到 `packages/*/src/` 内的具体实现。验收指标见 [60-Acceptance-Criteria.md](./60-Acceptance-Criteria.md)。

## 1. 鉴权与登录

- **API Key 登录**：env 变量 + `~/.pi/agent/auth.json` 持久化
  - 实现：`packages/ai/src/env-api-keys.ts` + `packages/coding-agent/src/core/auth-storage.ts`
  - CLI：`/login` 选 API-key provider
- **OAuth / 订阅登录**：内置 Claude Pro/Max、ChatGPT Plus/Pro（Codex）、GitHub Copilot
  - 实现：`packages/ai/src/utils/oauth/` + `packages/coding-agent/src/modes/interactive/components/oauth-selector.ts`
- **临时 API Key**：`--api-key` 不入磁盘
- **多账号**：`auth.json` 支持多 provider 各一个凭证

## 2. 模型

- **30+ 内置 Provider**（见 [10-Module-pi-ai.md §3.1](./10-Module-pi-ai.md)）
- **模型切换**：`/model`、`Ctrl+L`
- **思考等级**：`off / minimal / low / medium / high / xhigh`，`Shift+Tab` 循环
- **Scoped models**：`--models a,b,c` + `Ctrl+P` / `Shift+Ctrl+P` 循环
- **Token / 成本计算**：自动从 usage 计算（cache read/write、1h cache 2x）
- **Cost 自定义**：每个 model 的 `cost` 字段

## 3. 工具（Tools）

- `bash` — 子进程、流式输出、超时、取消、detach 追踪
- `read` — 文件读取、行范围、图像自动检测
- `write` — 写文件
- `edit` — 文本替换（`oldString`/`newString`）、multi-edit
- `grep` — ripgrep 风格
- `find` — glob 搜索
- `ls` — 列目录
- **路径安全**：所有工具走 `path-utils.ts` 的安全检查
- **可替换后端**：每个工具都有 `*Operations` 接口（SSH / 容器可注入）
- **并发写保护**：`file-mutation-queue.ts`

## 4. Session 管理

- **自动保存**：JSONL，路径 `$AGENT_DIR/sessions/`
- **续最近**：`-c` / `--continue`
- **浏览历史**：`-r` / `--resume`（含 fuzzy 搜索）
- **打开指定**：`--session <path|id>` / `--session-id <id>`
- **重命名**：`--name`
- **分叉**：`--fork <entryId>` / `/fork`
- **复制**：`/clone`
- **树视图**：`/tree` 导航 entry
- **删除**：`/delete`

## 5. Compaction（上下文压缩）

- **手动触发**：`/compact`
- **自动触发**：context token > 阈值（`reserveTokens` + `keepRecentTokens`）
- **Overflow 触发**：context overflow 错误自动压
- **保留近期**：`keepRecentTokens` 之内的消息完整保留
- **文件操作跟踪**：compaction summary 记录 read/modified 文件列表
- **可扩展**：extension 可注入自定义 compaction 钩子

## 6. 分支摘要（Branch Summary）

- `/tree` 时生成"切出该 entry 会丢失什么"预览
- 跳过大 prompt 选项（`branchSummary.skipPrompt`）
- 用于 `/fork` 前的人类决策

## 7. Skills（技能）

- 加载自 `~/.pi/agent/skills/` 与 `.pi/skills/`
- Frontmatter 描述 + 内容
- 在 system prompt 中以 `<skill name="..." location="...">` 块注入
- 触发：消息中显式 `/skill-name` 或自动展开
- CLI：`/skills` 列表；`--skills` / `--no-skills` 过滤

## 8. Prompt 模板

- 加载自 `~/.pi/agent/prompts/` 与 `.pi/prompts/`
- CLI：`@prompt-name` 引用
- `expandPromptTemplate` 解析变量与嵌套

## 9. 主题

- 加载自 `~/.pi/agent/themes/` 与 `.pi/themes/`
- 内置 default + 多种
- 切换：`/theme`
- 颜色规范由 `terminal-colors.ts` 探测 OSC 11 背景色

## 10. 扩展（Extensions）

- TypeScript 模块，从 `~/.pi/agent/extensions/` 或 `.pi/extensions/` 自动加载
- 能力：注册工具、命令、快捷键、CLI flag、widget、autocomplete provider
- 事件：订阅所有 agent 生命周期
- 上下文修改：拦截工具、改写 payload、注入 prompt
- UI 桥：`ctx.ui.notify / confirm / input / select / custom`
- 热重载：`/reload`
- 例：`examples/extensions/`（gondolin、sandbox、with-deps、custom-provider-anthropic、custom-provider-gitlab-duo 等）

## 11. 上下文文件

- `~/.pi/agent/AGENTS.md` — 全局
- `AGENTS.md` / `CLAUDE.md` — 项目级（cwd 向上找）
- 启动时加载到 system prompt
- `--no-context-files` 禁用

## 12. 交互模式 TUI

- **多行编辑器**：undo/redo、kill ring、word nav、autocomplete
- **Autocomplete**：`@file` / `/command` fuzzy 搜索
- **图片粘贴**：Ctrl+V（Win: Alt+V）/ 拖拽（受支持终端）
- **行内图片**：Kitty / iTerm2 协议
- **Kitty 键盘协议**：自动协商
- **OSC 9;4 进度**：可选开启
- **主题色**：OSC 11 探测
- **弹窗**：model-selector、settings-selector、session-selector、trust-selector、oauth-selector、theme-selector 等
- **Status bar**：footer + 上下文使用率

## 13. Print 模式

- `pi -p "..."` 单次执行
- `pi --mode json "..."` 输出 JSON 事件流
- 支持 `@file` 与 stdin 管道
- 多 message 队列

## 14. RPC 模式

- JSONL over stdin/stdout
- 客户端 → 服务端：`RpcCommand`（含 `prompt`, `set_model`, `set_thinking_level`, `get_state`, `compact`, `abort`, `new_session`, `fork`, `switch_session`, `list_sessions`, `get_session`, `cycle_model`, `cycle_scoped_model`, `get_commands`, ...）
- 服务端 → 客户端：`RpcResponse` + `AgentSessionEvent` 流
- 扩展 UI 桥：服务端发 `RpcExtensionUIRequest`，客户端回 `RpcExtensionUIResponse`
- 用于 IDE 集成、外部 chatbot、自动化

## 15. SDK

- 暴露 `AgentSession` + `createAgentSession` 工厂
- 任何 TS 项目可作为 lib 依赖
- 自定义 `Runtime` / `Tool` / `Extension` 注入
- 例：`examples/sdk/`

## 16. 包管理

- `pi package install <git-url>` — 从 git 装扩展/技能/主题
- `pi package list` / `remove` / `update`
- `pi config <key> <value>` — 改 settings
- SSH / HTTPS URL 都支持
- `package-manager-ssh.test.ts` 等覆盖

## 17. 项目信任

- 首次进入项目：交互式确认
- 记录在 `~/.pi/agent/trusted-projects.json`
- 检测带 "trust-requiring resources" 的项目
- `--project-trust-override` 跳过

## 18. 设置系统

- `~/.pi/agent/settings.json` 全局
- `.pi/settings.json` 项目
- 覆盖：全局 < 项目
- 字段分组：compaction / retry / terminal / image / thinking / markdown / warning

## 19. HTTP / 网络

- 自定义 dispatcher（`core/http-dispatcher.ts`）
- 全局 idle timeout 控制
- 自定义 headers / user-agent
- 代理支持（`pi-agent-core/proxy.ts`）
- 离线模式：`--offline`

## 20. 重试

- **Provider SDK retry**：默认 2 次（OpenAI / Anthropic）
- **Pi 层 retry**：3 次（指数退避 2s/4s/8s）
- **`maxRetryDelayMs`**：限制服务器请求的最长等待
- **Auto retry 事件**：`auto_retry_start` / `auto_retry_end` 给扩展观察

## 21. 上下文溢出

- `isContextOverflow(err)` 检测
- 自动触发 compaction
- 失败后退回用户提示

## 22. 文件附件

- `pi @README.md "..."` 传文件
- `pi @screenshot.png "..."` 传图
- `processFileArguments` 自动处理
- 图像自动 resize（`autoResize`）

## 23. 剪贴板图像

- macOS：`pngpaste` / `osascript`
- Linux：`xclip` / `wl-paste`
- Windows：PowerShell
- BMP 自动转 PNG

## 24. 导出 / 分享

- `pi --export <html-path>` 把 session 转 HTML
- 自定义主题色
- 扩展可注入工具 renderer
- 上游配套：`badlogic/pi-share-hf`（Hugging Face dataset 分享）

## 25. 安装方式

- `npm install -g @earendil-works/pi-coding-agent`（推荐 `--ignore-scripts`）
- `bun add -g` 也支持
- 内部：K3 RISC-V 用 4 个本地 tarball 装（见 `02-Build-and-Release.md`）
- 自更新：`pi update --self`

## 26. Shell alias / 集成

- `!command` — TUI 中跑 shell 并把输出喂给模型
- `!!command` — 跑 shell 不喂给模型
- 自定义 `shell-aliases` 文档

## 27. 平台支持

- **Linux**：x86_64 / aarch64 / **riscv64**（K3 验证）
- **macOS**：Intel / Apple Silicon
- **Windows**：WSL 优先；原生支持 bash
- **Termux**：专门文档 `docs/termux.md`
- **tmux**：专门文档 `docs/tmux.md`

## 28. 测试

- 100+ vitest 文件
- 端到端靠 `test/suite/harness.ts` + faux provider
- 浏览器烟测：`scripts/check-browser-smoke.mjs`
- 性能 profile：`scripts/profile-coding-agent-node.mjs` (tui / rpc 模式)

## 29. 文档站

- `packages/coding-agent/docs/` 完整文档（quickstart / usage / providers / settings / extensions / skills / themes / keybindings / sessions / compaction / rpc / sdk / models / security / tmux / termux / windows / shell-aliases / containerization / custom-provider / prompt-templates / terminal-setup）
- 渲染到 <https://pi.dev/docs/latest>
